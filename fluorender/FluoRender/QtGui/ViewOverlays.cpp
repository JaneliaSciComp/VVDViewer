// Port of the overlay drawing of VRenderVulkanView (VRenderView.cpp):
// DrawCamCtr, DrawLegend / DrawName, DrawScaleBar, DrawColormap /
// SetColormapColors, DrawFrame, DrawInfo, FixScaleBarLen / GetScaleBarFixed.
// See ViewOverlays.h for the member mapping.

#include "ViewOverlays.h"
#include "RenderCanvas.h"
#include "RulerTool.h"
#include "SettingsDialog.h"
#include "CellBody.h"

#include <QMouseEvent>
#include <QCursor>
#include <QDir>
#include <QCoreApplication>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "VVulkan.h"
#include "Vulkan/VulkanDevice.hpp"
#include "TextRenderer.h"
#include "DataManager.h"
#include "FLIVR/ImgShader.h"
#include "FLIVR/Transform.h"
#include "FLIVR/TextureRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/MultiVolumeRenderer.h"

using namespace FLIVR;

namespace
{
	inline double deg2rad(double d) { return d * 3.14159265358979323846 / 180.0; }

	// VRenderView.cpp: UpdateV2dObject (per-frame host-visible rings, as in
	// RulerTool.cpp)
	template <typename VTX>
	void UpdateV2dObject(vks::VulkanDevice* dev, Vulkan2dRender::V2dObject& obj,
		const std::vector<VTX>& vertex, const std::vector<uint32_t>& index)
	{
		VkDeviceSize vsize = vertex.size() * sizeof(VTX);
		VkDeviceSize isize = index.size() * sizeof(uint32_t);
		dev->GetNextHostVertexBuffer(vsize, obj.vertBuf, obj.vertOffset);
		memcpy((char*)obj.vertBuf.mapped + obj.vertOffset, vertex.data(), vsize);
		dev->GetNextHostIndexBuffer(isize, obj.idxBuf, obj.idxOffset);
		memcpy((char*)obj.idxBuf.mapped + obj.idxOffset, index.data(), isize);
		obj.vertCount = vertex.size();
		obj.idxCount = index.size();
	}

	inline std::wstring W(const QString& s) { return s.toStdWString(); }
}

// ---------------------------------------------------------------------------

ViewOverlays::ViewOverlays(RenderCanvas* canvas, RulerTool* ruler, QObject* parent)
	: QObject(parent), m_canvas(canvas), m_ruler(ruler)
{
	if (m_canvas)
		m_canvas->AddTool(this);
	m_timer.start();   // wx goTimer->start() at InitVulkan
}

ViewOverlays::~ViewOverlays()
{
	m_anchor_cancel->store(true);   // a cell body being computed stops early
	if (m_canvas)
		m_canvas->RemoveTool(this);
}

void ViewOverlays::Refresh()
{
	if (m_canvas) m_canvas->RefreshView();
}

void ViewOverlays::SetFont(const QString& path, int size)
{
	m_font_path = path;
	if (size > 0) m_font_size = size;
	m_font_dirty = true;
	Refresh();
}

TextRenderer* ViewOverlays::Text()
{
	if (m_ruler)
		return m_ruler->GetTextRenderer();
	// same as RulerTool::EnsureTextRenderer
	if (!m_canvas) return nullptr;
	std::shared_ptr<Vulkan2dRender> v2d = m_canvas->GetV2DRender();
	if (!v2d) return nullptr;
	QString path = m_font_path;
	if (path.isEmpty())
		path = QDir(SettingsDialog::FontsDir()).filePath(QStringLiteral("FreeSans.ttf"));
	if (!m_text_renderer)
	{
		m_text_renderer = std::make_unique<TextRenderer>(path.toStdString(), v2d);
		if (m_font_size > 0)
			m_text_renderer->SetSize(m_font_size);
		m_font_dirty = false;
	}
	else if (m_font_dirty)
	{
		m_text_renderer->LoadNewFace(path.toStdString(), m_font_size);
		m_font_dirty = false;
	}
	return m_text_renderer.get();
}

// VRenderVulkanView::GetTextColor (+ m_bg_color_inv from SetBackgroundColor)
Color ViewOverlays::GetTextColor()
{
	Color bg = m_canvas ? m_canvas->GetBackgroundColor() : Color(0.0, 0.0, 0.0);
	Color bg_inv;
	HSVColor hsv(bg);
	if (hsv.val() > 0.7 && hsv.sat() > 0.7)
		bg_inv = Color(HSVColor(hsv.hue() + 180.0, 1.0, 1.0));
	else if (hsv.val() > 0.7)
		bg_inv = Color(0.0, 0.0, 0.0);
	else
		bg_inv = Color(1.0, 1.0, 1.0);

	int mode = m_ruler ? m_ruler->GetTextColorMode() : m_text_color_mode;
	switch (mode)
	{
	case 0: return bg_inv;
	case 1: return bg;
	case 2:
		if (VolumeData* vd = m_canvas ? m_canvas->GetCurrentVolume() : nullptr)
			return vd->GetMaskColor();
		return bg_inv;
	}
	return bg_inv;
}

void ViewOverlays::RenderObj(Vulkan2dRender::V2DRenderParams& params)
{
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();
	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];
	// wx: params.clear = m_frame_clear - always false here, the canvas has
	// already drawn (and cleared) the swapchain image this frame
	params.clear = false;
	Color bg = m_canvas->GetBackgroundColor();
	params.clearColor = { (float)bg.r(), (float)bg.g(), (float)bg.b() };
	if (!fb->renderPass)
		fb->replaceRenderPass(params.pipeline.pass);
	v2drender->render(fb, params);
}

// ---------------------------------------------------------------------------
// CanvasTool
// ---------------------------------------------------------------------------

void ViewOverlays::HoverMove(QMouseEvent* e)
{
	m_mouse_pos = e->position().toPoint();
	m_have_mouse = true;
	// by hand: an open hand over a name (closed while one is dragged)
	if (!m_drag_vd && e->buttons() == Qt::NoButton)
		UpdateHandCursor(ToFrame(e->position()));
	// wx OnMouse (end): if (m_draw_coord) { m_drawing_coord = true; RefreshGL(false, false); }
	// (dragging returns earlier there; the camera redraws anyway)
	if (m_draw_info && m_draw_coord && m_canvas && e->buttons() == Qt::NoButton)
		m_canvas->RefreshNoLoopRestart();
}

void ViewOverlays::DrawOverlay(RenderCanvas* canvas, int nx, int ny)
{
	if (!canvas || canvas != m_canvas || nx <= 0 || ny <= 0)
		return;
	std::shared_ptr<VVulkan> vulkan = canvas->GetVulkan();
	if (!vulkan || !canvas->GetV2DRender() || !vulkan->frameBuffers[vulkan->currentBuffer])
		return;

	// wx goTimer->sample(), once per OnDraw
	{
		double dt = m_timer.nsecsElapsed() / 1.0e9;
		m_timer.restart();
		m_interval_sum -= m_intervals[m_interval_pos];
		m_interval_sum += dt;
		m_intervals[m_interval_pos] = dt;
		m_interval_pos = (m_interval_pos + 1) % 10;
	}

	// wx Draw(): DrawAnnotations comes right after the scene
	if (m_draw_annotations)
		DrawAnnotations(nx, ny);
	if (m_draw_camctr)
		DrawCamCtr(nx, ny);
	m_drawn_labels.clear();
	// the names placed by hand of volumes that are gone (before another
	// volume can be made at the same address)
	if (!m_name_pos.empty())
	{
		const std::vector<VolumeData*>& all = canvas->Volumes();
		for (auto it = m_name_pos.begin(); it != m_name_pos.end();)
		{
			if (it->first != m_drag_vd && std::find(all.begin(), all.end(), it->first) == all.end())
				it = m_name_pos.erase(it);
			else
				++it;
		}
	}
	if (m_draw_legend)
	{
		if (m_names_on_objects)
			DrawObjectNames(nx, ny);
		DrawLegend(nx, ny);
	}
	if (m_disp_scale_bar)
		DrawScaleBar(nx, ny);
	DrawColormap(nx, ny);
}

// wx OnDraw: PostDraw() (capture) comes before DrawFrame / DrawInfo
void ViewOverlays::DrawOverlayPost(RenderCanvas* canvas, int nx, int ny)
{
	if (!canvas || canvas != m_canvas || nx <= 0 || ny <= 0)
		return;
	std::shared_ptr<VVulkan> vulkan = canvas->GetVulkan();
	if (!vulkan || !canvas->GetV2DRender() || !vulkan->frameBuffers[vulkan->currentBuffer])
		return;
	if (m_draw_frame)
		DrawFrame(nx, ny);
	if (m_draw_info)
		DrawInfo(nx, ny);
}

void ViewOverlays::SyncCaptureFrame()
{
	if (m_canvas)
		m_canvas->SetCaptureFrame(m_draw_frame, m_frame_x, m_frame_y, m_frame_w, m_frame_h);
}

// ---------------------------------------------------------------------------
// VRenderVulkanView::DrawCamCtr
// ---------------------------------------------------------------------------

void ViewOverlays::DrawCamCtr(int, int)
{
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();

	float len;
	if (m_camctr_size > 0.0)
		len = m_canvas->GetCamDistance() * std::tan(deg2rad(m_canvas->GetAov() / 2.0)) * m_camctr_size / 10.0;
	else
		len = std::fabs(m_camctr_size);

	std::vector<Vulkan2dRender::Vertex> vertex;
	std::vector<uint32_t> index = { 0,1,2,3,4,5 };
	vertex.reserve(6);
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {len,  0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, len,  0.0f}, {0.0f, 1.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, len }, {0.0f, 0.0f, 1.0f} });
	UpdateV2dObject(vulkan->vulkanDevice, m_camctr_vobj, vertex, index);

	// wx draws with the camera model-view (Draw() restores it before
	// DrawCamCtr); the canvas hands tools the object model-view
	// mv * T(trans) * Rx * Ry * Rz * T(-ctr), so undo the object transform.
	double cx, cy, cz, rx, ry, rz, tx, ty, tz;
	m_canvas->GetObjCenter(cx, cy, cz);
	m_canvas->GetObjRot(rx, ry, rz);
	m_canvas->GetObjTrans(tx, ty, tz);
	glm::mat4 cam_mv = m_canvas->GetMvMat();
	cam_mv = glm::translate(cam_mv, glm::vec3(cx, cy, cz));
	cam_mv = glm::rotate(cam_mv, -float(rz), glm::vec3(0.0, 0.0, 1.0));
	cam_mv = glm::rotate(cam_mv, -float(ry), glm::vec3(0.0, 1.0, 0.0));
	cam_mv = glm::rotate(cam_mv, -float(rx), glm::vec3(1.0, 0.0, 0.0));
	cam_mv = glm::translate(cam_mv, glm::vec3(-tx, -ty, -tz));

	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];
	Vulkan2dRender::V2DRenderParams params = v2drender->GetNextV2dRenderSemaphoreSettings();
	params.pipeline =
		v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY_COLOR3,
			V2DRENDER_BLEND_OVER_UI,
			fb->attachments[0]->format,
			(int)fb->attachments.size(),
			0,
			fb->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_LINE_LIST
		);
	params.matrix[0] = m_canvas->GetProjMat() * cam_mv;
	params.obj = &m_camctr_vobj;
	RenderObj(params);
}

// ---------------------------------------------------------------------------
// VRenderVulkanView::DrawFrame
// ---------------------------------------------------------------------------

void ViewOverlays::DrawFrame(int nx, int ny)
{
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();
	glm::mat4 proj_mat = glm::ortho(float(0), float(nx), float(0), float(ny));

	std::vector<Vulkan2dRender::Vertex> vertex;
	std::vector<uint32_t> index = { 0,1,2,3,0 };
	vertex.reserve(4);
	vertex.push_back(Vulkan2dRender::Vertex{ {(float)(m_frame_x - 1), (float)(m_frame_y - 1), 0.0f}, {0.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {(float)(m_frame_x + m_frame_w + 1), (float)(m_frame_y - 1), 0.0f}, {0.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {(float)(m_frame_x + m_frame_w + 1), (float)(m_frame_y + m_frame_h + 1), 0.0f}, {0.0f, 0.0f, 0.0f} });
	vertex.push_back(Vulkan2dRender::Vertex{ {(float)(m_frame_x - 1), (float)(m_frame_y + m_frame_h + 1), 0.0f}, {0.0f, 0.0f, 0.0f} });
	UpdateV2dObject(vulkan->vulkanDevice, m_frame_vobj, vertex, index);

	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];
	Vulkan2dRender::V2DRenderParams params = v2drender->GetNextV2dRenderSemaphoreSettings();
	params.pipeline =
		v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			fb->attachments[0]->format,
			(int)fb->attachments.size(),
			0,
			fb->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_LINE_STRIP
		);
	params.loc[0] = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
	params.matrix[0] = proj_mat;
	params.obj = &m_frame_vobj;
	RenderObj(params);
}

// ---------------------------------------------------------------------------
// scale bar (VRenderVulkanView::FixScaleBarLen / GetScaleBarFixed / DrawScaleBar)
// ---------------------------------------------------------------------------

void ViewOverlays::FixScaleBarLen(bool fix, double len)
{
	QSize s = m_canvas ? m_canvas->GetDeviceSize() : QSize(1, 1);
	int nx = s.width(), ny = s.height();
	double sf = m_canvas ? m_canvas->GetScaleFactor() : 1.0;

	m_fix_sclbar = fix;
	if (len > 0.0) m_sb_length = len;
	m_fixed_sclbar_fac = m_sb_length * sf * std::min(nx, ny);
	Refresh();
}

void ViewOverlays::GetScaleBarFixed(bool& fix, double& len, int& unitid)
{
	QSize s = m_canvas ? m_canvas->GetDeviceSize() : QSize(1, 1);
	int nx = s.width(), ny = s.height();
	double sf = m_canvas ? m_canvas->GetScaleFactor() : 1.0;

	fix = m_fix_sclbar;
	if (m_fix_sclbar && sf * std::min(nx, ny) != 0.0)
		m_sb_length = m_fixed_sclbar_fac / (sf * std::min(nx, ny));
	len = m_sb_length;
	unitid = m_sb_unit;
}

void ViewOverlays::DrawScaleBar(int nx, int ny)
{
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();
	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];
	TextRenderer* text = Text();

	double offset = 0.0;
	if (m_draw_legend)
		offset = m_sb_height;

	float sx, sy;
	sx = 2.0 / nx;
	sy = 2.0 / ny;
	float px, py, ph;

	glm::mat4 proj_mat = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f);

	double sf = m_canvas->GetScaleFactor();
	if (m_fix_sclbar)
		m_sb_length = m_fixed_sclbar_fac / (sf * std::min(nx, ny));
	double ow = m_canvas->GetOrthoWidth();
	float len = ow != 0.0 ? m_sb_length / ow : 0.0;

	double len_txt = m_sb_length;
	QString unit_txt = m_sb_text;

	QString num_txt;
	if (m_sclbar_digit == 0)
		num_txt = QString::asprintf("%i ", (int)len_txt);
	else if (m_sclbar_digit > 0)
	{
		num_txt = QString::asprintf("%.9f", len_txt);
		int dot = num_txt.indexOf(QLatin1Char('.'));
		QString intstr = dot < 0 ? num_txt : num_txt.left(dot);
		QString fracstr = dot < 0 ? QString() : num_txt.mid(dot + 1);
		int fractextlen = m_sclbar_digit;
		if (fractextlen > fracstr.length()) fractextlen = fracstr.length();
		num_txt = intstr + QStringLiteral(".") + fracstr.mid(0, fractextlen) + QStringLiteral(" ");
	}

	std::wstring wsb_text = W(num_txt + unit_txt);

	float textlen = text ? text->RenderTextDims(wsb_text).width : 0.0f;
	Color text_color = GetTextColor();

	std::vector<Vulkan2dRender::Vertex> vertex;
	std::vector<uint32_t> index = { 0,1,2, 2,3,0 };
	vertex.reserve(4);

	if (m_draw_frame)
	{
		px = (0.95 * m_frame_w + m_frame_x) / nx;
		py = (0.95 * m_frame_h + m_frame_y - offset) / ny;
		ph = 5.0 / ny;
		vertex.push_back(Vulkan2dRender::Vertex{ {px, py, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px - len, py, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px - len, py + ph, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px, py + ph, 0}, {0.0f, 0.0f, 0.0f} });

		if (m_disp_scale_bar_text)
		{
			px = 0.95 * m_frame_w + m_frame_x - (len * nx + textlen + nx) / 2.0;
			py = 0.95 * m_frame_h + m_frame_y - 15.0f - offset - ny / 2.0;
			if (text)
				text->RenderText(fb, wsb_text, text_color, px * sx, py * sy);
		}
	}
	else
	{
		px = 0.95;
		py = 0.95 - offset / ny;
		ph = 5.0 / ny;
		vertex.push_back(Vulkan2dRender::Vertex{ {px, py, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px - len, py, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px - len, py + ph, 0}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {px, py + ph, 0}, {0.0f, 0.0f, 0.0f} });

		if (m_disp_scale_bar_text)
		{
			px = 0.95 * nx - (len * nx + textlen + nx) / 2.0;
			py = 0.95 * ny - 15.0f - offset - ny / 2.0;
			if (text)
				text->RenderText(fb, wsb_text, text_color, px * sx, py * sy);
		}
	}

	UpdateV2dObject(vulkan->vulkanDevice, m_scbar_vobj, vertex, index);

	Vulkan2dRender::V2DRenderParams params = v2drender->GetNextV2dRenderSemaphoreSettings();
	params.pipeline =
		v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			fb->attachments[0]->format,
			(int)fb->attachments.size(),
			0,
			fb->attachments[0]->is_swapchain_images
		);
	params.loc[0] = glm::vec4((float)text_color.r(), (float)text_color.g(), (float)text_color.b(), 1.0f);
	params.matrix[0] = proj_mat;
	params.obj = &m_scbar_vobj;
	RenderObj(params);
}

// ---------------------------------------------------------------------------
// VRenderVulkanView::DrawLegend / DrawName
// ---------------------------------------------------------------------------

void ViewOverlays::DrawLegend(int nx, int ny)
{
	TextRenderer* text = Text();
	if (!text)
		return;

	// wx m_vd_pop_list (displayed volumes) / m_md_pop_list (displayed meshes)
	std::vector<VolumeData*> vd_list = m_canvas->PopVolumeList();
	std::vector<MeshData*> md_list = m_canvas->GetPopMeshList();
	// wx: GetCurSelType() == 2 (volume) / 3 (mesh); the main window selects a
	// mesh with SetCurrentMesh(md) and a volume with SetCurrentMesh(nullptr)
	MeshData* sel_md = m_canvas->GetCurrentMesh();
	VolumeData* sel_vd = sel_md ? nullptr : m_canvas->GetCurrentVolume();

	double font_height = text->GetSize() + 3.0;

	float xoffset = 10.0;
	float yoffset = 10.0;
	if (m_draw_frame)
	{
		xoffset = 10.0 + m_frame_x;
		yoffset = 10.0 + m_frame_y;
	}

	std::wstring wstr;
	double length = 0.0;
	double name_len = 0.0;
	double gap_width = font_height * 1.5;
	int lines = 0;
	const double avail = double(m_draw_frame ? m_frame_w : nx) - gap_width;

	auto advance = [&](const QString& name, double* xpos, bool second) {
		wstr = W(name);
		if (xpos) *xpos = length;
		name_len = text->RenderTextDims(wstr).width + font_height;
		length += name_len;
		if (length < avail)
			length += gap_width;
		else
		{
			length = name_len + gap_width;
			if (second) { if (xpos) *xpos = 0.0; }
			else lines++;
			return true;
		}
		return false;
	};

	// Names on Objects: the volumes are labelled on their cell bodies
	// (DrawObjectNames), the list keeps the meshes
	if (m_names_on_objects)
		vd_list.clear();

	//first pass
	for (VolumeData* vd : vd_list)
		if (vd->GetLegend())
			advance(LegendName(QString::fromStdWString(vd->GetName())), nullptr, false);
	for (MeshData* md : md_list)
		if (md->GetLegend())
			advance(LegendName(QString::fromStdWString(md->GetName())), nullptr, false);

	//second pass: each volume's square (and its name when selected) in the
	//colour it gets on the screen, RenderCanvas::ScreenColor: its colour
	//through its Output Adjustments (wx: the colour itself -- black for a
	//dim channel that a strong gamma makes bright, and another hue where the
	//gamma lifts a small component: red with a little blue showed magenta)
	int cur_line = 0;
	double xpos;
	length = 0.0;
	for (VolumeData* vd : vd_list)
	{
		if (!vd->GetLegend())
			continue;
		QString name = LegendName(QString::fromStdWString(vd->GetName()));
		if (advance(name, &xpos, true))
			cur_line++;
		bool highlighted = sel_vd == vd;
		DrawName(xpos + xoffset, ny - (lines - cur_line + 0.1) * font_height - yoffset,
			nx, ny, name, m_canvas->ScreenColor(vd), font_height, highlighted);
	}
	for (MeshData* md : md_list)
	{
		if (!md->GetLegend())
			continue;
		QString name = LegendName(QString::fromStdWString(md->GetName()));
		if (advance(name, &xpos, true))
			cur_line++;
		Color amb, diff, spec;
		double shine, alpha;
		md->GetMaterial(amb, diff, spec, shine, alpha);
		Color c(diff.r(), diff.g(), diff.b());
		bool highlighted = sel_md == md;
		DrawName(xpos + xoffset, ny - (lines - cur_line + 0.1) * font_height - yoffset,
			nx, ny, name, c, font_height, highlighted);
	}

	m_sb_height = (lines + 1) * font_height;
}

void ViewOverlays::DrawName(double x, double y, int nx, int ny,
	const QString& name, Color color, double font_height, bool highlighted)
{
	TextRenderer* text = Text();
	if (!text)
		return;
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();
	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];

	float sx, sy;
	sx = 2.0 / nx;
	sy = 2.0 / ny;

	glm::mat4 proj_mat = glm::ortho(0.0f, float(nx), 0.0f, float(ny));

	glm::vec3 trans1 = glm::vec3((float)(x + 0.2 * font_height - 1.0), (float)(y - 0.2 * font_height + 1.0), 0.0f);
	glm::vec3 scale1 = glm::vec3((float)(0.6 * font_height + 2.0), (float)(0.6 * font_height + 2.0), 1.0f);
	glm::vec3 trans2 = glm::vec3((float)(x + 0.2 * font_height), (float)(y - 0.2 * font_height), 0.0f);
	glm::vec3 scale2 = glm::vec3((float)(0.6 * font_height), (float)(0.6 * font_height), 1.0f);

	// unit quad (wx m_name_vobj, created once there; rewritten into the
	// per-frame ring here)
	{
		std::vector<Vulkan2dRender::Vertex> vertex;
		std::vector<uint32_t> index = { 0,1,2,2,3,0 };
		vertex.reserve(4);
		vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 0.0f} });
		UpdateV2dObject(vulkan->vulkanDevice, m_name_vobj, vertex, index);
	}

	std::vector<Vulkan2dRender::V2DRenderParams> v2drender_params;
	Vulkan2dRender::V2dPipeline pl =
		v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_DISABLE,
			fb->attachments[0]->format,
			(int)fb->attachments.size(),
			0,
			fb->attachments[0]->is_swapchain_images
		);

	Color text_color = GetTextColor();
	Color bg = m_canvas->GetBackgroundColor();
	Vulkan2dRender::V2DRenderParams params1 = v2drender->GetNextV2dRenderSemaphoreSettings();
	params1.pipeline = pl;
	params1.loc[0] = glm::vec4((float)text_color.r(), (float)text_color.g(), (float)text_color.b(), 1.0f);
	params1.matrix[0] = proj_mat * glm::translate(trans1) * glm::scale(scale1);
	params1.clear = false;
	params1.clearColor = { (float)bg.r(), (float)bg.g(), (float)bg.b() };
	params1.obj = &m_name_vobj;

	Vulkan2dRender::V2DRenderParams params2;
	params2.pipeline = pl;
	params2.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), 1.0f);
	params2.matrix[0] = proj_mat * glm::translate(trans2) * glm::scale(scale2);
	params2.clear = false;
	params2.obj = &m_name_vobj;

	v2drender_params.push_back(params1);
	v2drender_params.push_back(params2);

	if (!fb->renderPass)
		fb->replaceRenderPass(pl.pass);

	v2drender->seq_render(fb, v2drender_params.data(), (int)v2drender_params.size());

	if (name.isEmpty())
		return;   // the square only (DrawObjectNames)
	float px1 = x + font_height - nx / 2;
	float py1 = y - 0.25 * font_height - ny / 2;
	std::wstring wstr = W(name);
	text->RenderText(fb, wstr, text_color, px1 * sx, py1 * sy);
	if (highlighted)
	{
		px1 -= 0.5;
		py1 -= 0.5;
		text->RenderText(fb, wstr, color, px1 * sx, py1 * sy);
	}
}

// ---------------------------------------------------------------------------
// Names on Objects (not in wx)
// ---------------------------------------------------------------------------

QString ViewOverlays::LegendName(const QString& name)
{
	QString r = name;
	for (int pass = 0; pass < 2; pass++)
	{
		bool packed = false;
		for (int dot = r.lastIndexOf('.'); dot > 0; dot = r.lastIndexOf('.', dot - 1))
		{
			int end = dot + 1;
			bool letter = false;
			while (end < r.size() && r[end].isLetterOrNumber())
			{
				letter = letter || r[end].isLetter();
				end++;
			}
			const int n = end - dot - 1;
			if (n < 2 || n > 6 || !letter || (end < r.size() && r[end] != '_'))
				continue;
			const QString ext = r.mid(dot + 1, n).toLower();
			packed = ext == "gz" || ext == "bz2" || ext == "xz" || ext == "zst" || ext == "zip";
			r.remove(dot, n + 1);
			break;
		}
		if (!packed)
			break;
	}
	return r.isEmpty() ? name : r;
}

void ViewOverlays::SetNamesOnObjects(bool b)
{
	if (m_names_on_objects == b)
		return;
	m_names_on_objects = b;
	if (!b)
	{
		m_drag_vd = nullptr;
		UpdateHandCursor(QPointF(-1.0, -1.0));
	}
	if (m_canvas)
		m_canvas->RefreshOverlayChange();
}

void ViewOverlays::SetNamesManual(bool b)
{
	if (m_names_manual == b)
		return;
	m_names_manual = b;
	if (!b)
	{
		m_drag_vd = nullptr;
		UpdateHandCursor(QPointF(-1.0, -1.0));
	}
	if (m_canvas)
		m_canvas->RefreshOverlayChange();
}

std::vector<std::pair<VolumeData*, QPointF>> ViewOverlays::NamePositions() const
{
	std::vector<std::pair<VolumeData*, QPointF>> r;
	if (!m_canvas)
		return r;
	// only volumes the view has (a deleted one's pointer is never followed)
	const std::vector<VolumeData*>& all = m_canvas->Volumes();
	for (const auto& [vd, pos] : m_name_pos)
		if (std::find(all.begin(), all.end(), vd) != all.end())
			r.emplace_back(vd, pos);
	return r;
}

void ViewOverlays::SetNamePosition(VolumeData* vd, const QPointF& offset)
{
	if (!vd)
		return;
	m_name_pos[vd] = offset;
	if (m_canvas)
		m_canvas->RefreshOverlayChange();
}

void ViewOverlays::ResetNamePositions()
{
	m_name_pos.clear();
	m_drag_vd = nullptr;
	if (m_canvas)
		m_canvas->RefreshOverlayChange();
}

QPointF ViewOverlays::ToFrame(const QPointF& widget_pos) const
{
	// the overlays are laid out in the view's logical size
	const double w = m_canvas ? m_canvas->width() : 0.0;
	const double s = w > 0.0 && m_label_frame.width() > 0 ? m_label_frame.width() / w : 1.0;
	return widget_pos * s;
}

VolumeData* ViewOverlays::NameAt(const QPointF& p) const
{
	VolumeData* hit = nullptr;
	int top = -1;
	for (const auto& [vd, l] : m_drawn_labels)
	{
		const QRectF sq(l.square.x() - m_label_edge, l.square.y() - m_label_edge, 2.0 * m_label_edge, 2.0 * m_label_edge);
		if ((l.box.contains(p) || sq.contains(p)) && l.order > top)
		{
			hit = vd;
			top = l.order;
		}
	}
	return hit;
}

void ViewOverlays::UpdateHandCursor(const QPointF& p)
{
	const bool over = m_drag_vd || (m_names_on_objects && m_names_manual && m_draw_legend && NameAt(p));
	if (over == m_hand_cursor || !m_canvas)
		return;
	m_hand_cursor = over;
	if (over)
		m_canvas->SetViewCursor(QCursor(m_drag_vd ? Qt::ClosedHandCursor : Qt::OpenHandCursor));
	else
		m_canvas->UnsetViewCursor();
}

// by hand: a left press on a name drags it
bool ViewOverlays::GrabPress(QMouseEvent* e)
{
	if (!m_names_on_objects || !m_names_manual || !m_draw_legend || !m_canvas || !e)
		return false;
	const QPointF p = ToFrame(e->position());
	VolumeData* vd = NameAt(p);
	if (!vd)
		return false;
	const DrawnLabel& l = m_drawn_labels[vd];
	auto it = m_name_pos.find(vd);
	m_drag_pos0 = it != m_name_pos.end() ? it->second : (l.square - l.anchor) / std::max(1.0, m_label_fh);
	m_name_pos[vd] = m_drag_pos0;
	m_drag_from = p;
	m_drag_vd = vd;
	m_hand_cursor = false;   // closed while dragging
	UpdateHandCursor(p);
	return true;
}

void ViewOverlays::DragMove(QMouseEvent* e)
{
	if (!m_drag_vd || !e)
		return;
	const double fh = std::max(1.0, m_label_fh);
	QPointF pos = m_drag_pos0 + (ToFrame(e->position()) - m_drag_from) / fh;
	// onto its cell body's square: beside it again
	if (std::hypot(pos.x(), pos.y()) * fh <= m_label_edge)
		pos = QPointF(0.0, 0.0);
	m_name_pos[m_drag_vd] = pos;
	if (m_canvas)
		m_canvas->RefreshOverlayChange();
}

void ViewOverlays::DragRelease(QMouseEvent* e)
{
	if (!m_drag_vd)
		return;
	DragMove(e);
	m_drag_vd = nullptr;
	// the arrow, or an open hand again over a name
	if (m_canvas) m_canvas->UnsetViewCursor();
	m_hand_cursor = false;
	if (e) UpdateHandCursor(ToFrame(e->position()));
}

bool ViewOverlays::LabelOf(VolumeData* vd, QPointF* anchor, QRectF* name_box, QPointF* name_square) const
{
	auto it = m_drawn_labels.find(vd);
	if (it == m_drawn_labels.end())
		return false;
	if (anchor) *anchor = it->second.anchor;
	if (name_box) *name_box = it->second.box;
	if (name_square) *name_square = it->second.square;
	return true;
}

// Each displayed volume with its legend on: its square on its cell body
// (DrawName's square, centred there; its frame marks the place) and its
// name after a square of its colour, as in the list, over a shadow; the
// selected volume's name bold. The name is right of the cell body's square,
// which is then the square before it, or, where it would leave the view,
// left of it after a square of its own. Laid out from the rightmost cell
// body: a name that would overlap a square, a name or a line placed before,
// or whose line would cross one, moves by whole lines (down, up, two down,
// ...) with its own square, right above or below the cell body's, a line
// running straight between them (left of the cell body: to a corner, then
// along the line to the end of the name). Names run right, so the lines of
// the names placed later, further left, pass left of them.
void ViewOverlays::DrawObjectNames(int nx, int ny)
{
	TextRenderer* text = Text();
	if (!text || !m_canvas)
		return;
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];
	m_label_frame = QSize(nx, ny);

	// the cell bodies of volumes that are gone
	const std::vector<VolumeData*>& all = m_canvas->Volumes();
	for (auto it = m_anchors.begin(); it != m_anchors.end();)
	{
		if (it->first != m_anchor_running_vd && std::find(all.begin(), all.end(), it->first) == all.end())
			it = m_anchors.erase(it);
		else
			++it;
	}

	Transform mv, p;
	glm::mat4 mv_mat = m_canvas->GetMvMat();
	glm::mat4 proj_mat = m_canvas->GetProjMat();
	mv.set(glm::value_ptr(mv_mat));
	p.set(glm::value_ptr(proj_mat));
	MeshData* sel_md = m_canvas->GetCurrentMesh();
	VolumeData* sel_vd = sel_md ? nullptr : m_canvas->GetCurrentVolume();
	const double fh = text->GetSize() + 3.0;

	struct Label
	{
		VolumeData* vd = nullptr;
		std::wstring name;
		Color color;
		double ax = 0.0, ay = 0.0, w = 0.0, ty = 0.0;
		bool left = false, moved = false, highlighted = false;
		bool fixed = false;   // placed by hand: its square at fx, ty
		double fx = 0.0;
	};
	std::vector<Label> labels;
	for (VolumeData* vd : m_canvas->PopVolumeList())
	{
		if (!vd || !vd->GetLegend())
			continue;
		std::shared_ptr<VL_Nrrd> vol = vd->GetVolume(false);
		Nrrd* n = vol ? vol->getNrrd() : nullptr;
		if (!n || !n->data)
			continue;
		const double thr = std::max(0.0, vd->GetLeftThresh() * vd->GetMaxValue());
		auto it = m_anchors.find(vd);
		if (it == m_anchors.end() || it->second.data != n->data || it->second.thr != thr || it->second.vol.lock() != vol)
		{
			QueueCellBody(vd);
			continue;
		}
		const CellBodyAnchor& a = it->second;
		if (!a.done || !a.found)
			continue;
		const Point tex(a.fx, a.fy, a.fz);
		// cut away by its clipping planes (texture space, as annotations)
		bool clipped = false;
		if (VolumeRenderer* vr = vd->GetVR())
			if (std::vector<Plane*>* planes = vr->get_planes())
				if (planes->size() == 6)
					for (Plane* pl : *planes)
						if (pl && pl->eval_point(tex) < 0.0) { clipped = true; break; }
		Transform* tf = vd->GetTexture() ? vd->GetTexture()->transform() : nullptr;
		if (clipped || !tf)
			continue;
		const Point pos = p.transform(mv.transform(tf->transform(tex)));
		if (pos.x() < -1.0 || pos.x() > 1.0 || pos.y() < -1.0 || pos.y() > 1.0 || pos.z() <= 0.0 || pos.z() >= 1.0)
			continue;
		Label l;
		l.vd = vd;
		l.name = LegendName(QString::fromStdWString(vd->GetName())).toStdWString();
		l.color = m_canvas->ScreenColor(vd);
		l.ax = (pos.x() + 1.0) * 0.5 * nx;
		l.ay = (pos.y() + 1.0) * 0.5 * ny;
		l.w = text->RenderTextDims(l.name).width;
		l.highlighted = vd == sel_vd;
		labels.push_back(l);
	}
	// hidden volumes (or in hidden groups) with their legend on too, after
	// the shown ones, in the background: a group shown for one frame
	// ("Capture each group separately") has its names
	for (VolumeData* vd : all)
	{
		if (!vd || !vd->GetLegend())
			continue;
		std::shared_ptr<VL_Nrrd> vol = vd->GetVolume(false);
		Nrrd* n = vol ? vol->getNrrd() : nullptr;
		if (!n || !n->data)
			continue;
		const double thr = std::max(0.0, vd->GetLeftThresh() * vd->GetMaxValue());
		auto it = m_anchors.find(vd);
		if (it == m_anchors.end() || it->second.data != n->data || it->second.thr != thr || it->second.vol.lock() != vol)
			QueueCellBody(vd);
	}
	if (labels.empty())
		return;

	// rightmost first
	std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) {
		return a.ax > b.ax || (a.ax == b.ax && a.ay < b.ay); });
	// the squares: 2 half + 1 pixels in a frame of 2 (DrawMarker, centred on
	// pixel centres); a name starts a few pixels past its square's frame; a
	// line of names holds a square
	const int half = std::max(2, int(std::lround(0.3 * fh)));
	const double edge = half + 2.5, gap = edge + 3.0;
	const double lh = std::max(fh, 2.0 * edge + 2.0);
	auto centre = [](double v) { return std::floor(v) + 0.5; };
	// the centre x of the square before a name, and where the name starts
	auto square_x = [gap](const Label& l) { return l.left ? l.ax - 2.0 * gap - l.w : l.ax; };
	auto text_x = [gap](const Label& l) { return l.left ? l.ax - gap - l.w : l.ax + gap; };
	// the box of a label on line cy: its name and the square before it,
	// unless that is the cell body's (beside it, right of it); moved left of
	// the cell body: as far as its line's corner
	auto box_of = [=](const Label& l, double cy) {
		const bool moved = std::abs(cy - l.ay) > 0.5;
		const double x0 = l.left || moved ? square_x(l) - edge - 1.0 : text_x(l) - 2.0;
		const double x1 = l.left && moved ? l.ax + 1.0 : text_x(l) + l.w + 2.0;
		return QRectF(x0, cy - 0.5 * lh, x1 - x0, lh);
	};
	// a moved label's line (y from, to), straight up or down from the cell
	// body's square to line cy: to the name's square right of the cell body,
	// to the corner (on the line) left of it
	auto line_y = [=](const Label& l, double cy, double& y0, double& y1) {
		const double ya = centre(l.ay), yc = centre(cy), end = l.left ? -0.75 : edge;
		if (cy > l.ay) { y0 = ya + edge; y1 = yc - end; }
		else { y0 = yc + end; y1 = ya - edge; }
	};
	auto line_box = [=](const Label& l, double cy) {
		double y0, y1;
		line_y(l, cy, y0, y1);
		return QRectF(centre(l.ax) - 1.0, y0 + 0.25, 2.0, std::max(0.0, y1 - y0 - 0.5));
	};
	std::vector<QRectF> squares, placed;   // the cell bodies' squares; names and lines placed
	for (const Label& l : labels)
		squares.push_back(QRectF(centre(l.ax) - edge, centre(l.ay) - edge, 2.0 * edge, 2.0 * edge));
	auto hits = [](const std::vector<QRectF>& rs, const QRectF& b) {
		for (const QRectF& r : rs)
			if (r.intersects(b)) return true;
		return false;
	};
	m_label_fh = fh;
	m_label_edge = edge;
	// by hand: a name keeps its place, its square at fx, ty (the cell body's
	// square when that is where it is: the name beside it), the name right
	// of it; placed first, the others are laid out around them and then
	// keep their places too
	auto beside = [](const Label& l) { return std::abs(l.fx - l.ax) < 0.5 && std::abs(l.ty - l.ay) < 0.5; };
	auto fixed_box = [=](const Label& l) {
		const double x0 = beside(l) ? l.ax + gap - 2.0 : l.fx - edge - 1.0;
		return QRectF(x0, l.ty - 0.5 * lh, (beside(l) ? l.ax : l.fx) + gap + l.w + 2.0 - x0, lh);
	};
	if (m_names_manual)
		for (Label& l : labels)
		{
			auto it = m_name_pos.find(l.vd);
			if (it == m_name_pos.end())
				continue;
			l.fixed = true;
			l.fx = l.ax + it->second.x() * fh;
			l.ty = l.ay + it->second.y() * fh;
			placed.push_back(fixed_box(l));
		}
	for (Label& l : labels)
	{
		if (l.fixed)
			continue;
		l.left = l.ax + gap + l.w > nx - 4.0 && l.ax - 2.0 * gap - l.w - edge >= 4.0;
		l.ty = l.ay;
		for (int k = 0; k <= 16; k++)
		{
			const int step = (k + 1) / 2 * (k % 2 ? 1 : -1);   // 0, +1, -1, +2, -2, ...
			const double cy = l.ay + step * lh;
			const QRectF b = box_of(l, cy);
			if (b.top() < 0.0 || b.bottom() > ny || hits(squares, b) || hits(placed, b))
				continue;
			if (step && (hits(squares, line_box(l, cy)) || hits(placed, line_box(l, cy))))
				continue;
			l.ty = cy;
			break;
		}
		l.moved = std::abs(l.ty - l.ay) > 0.5;
		placed.push_back(box_of(l, l.ty));
		if (l.moved)
			placed.push_back(line_box(l, l.ty));
		if (m_names_manual)
		{
			// kept here from now on
			l.fx = square_x(l);
			if (!l.moved && !l.left) l.fx = l.ax;
			l.fixed = true;
			m_name_pos[l.vd] = QPointF((l.fx - l.ax) / fh, (l.ty - l.ay) / fh);
		}
	}

	Color text_color = GetTextColor();
	Color shadow = m_canvas->GetBackgroundColor();
	const float sx = 2.0f / nx, sy = 2.0f / ny;
	const double off = std::max(1.0, fh / 16.0);
	int order = 0;
	for (const Label& l : labels)
	{
		if (l.fixed)
		{
			const QRectF b = fixed_box(l);
			if (!beside(l))
			{
				// a straight line from the cell body's square to the name's,
				// or to the end of the name when the cell body is right of it
				const QPointF a(centre(l.ax), centre(l.ay));
				const bool to_end = a.x() > b.center().x();
				const QPointF t = to_end ? QPointF(b.right() - 1.0, centre(l.ty)) : QPointF(centre(l.fx), centre(l.ty));
				const QPointF v = t - a;
				const double m = std::max(std::abs(v.x()), std::abs(v.y()));
				if (m > 0.0)
				{
					const QPointF s = a + v * (edge / m), e2 = to_end ? t : t - v * (edge / m);
					const QPointF d = e2 - s;
					if (d.x() * v.x() + d.y() * v.y() > 0.0 && std::hypot(d.x(), d.y()) >= 3.0)
						DrawLeader(s.x(), s.y(), e2.x(), e2.y(), nx, ny, text_color);
				}
			}
			DrawMarker(l.ax, l.ay, half, nx, ny, l.color, text_color);
			if (!beside(l))
				DrawMarker(l.fx, l.ty, half, nx, ny, l.color, text_color);
			const float px = float((beside(l) ? l.ax : l.fx) + gap - nx / 2.0);
			const float py = float(l.ty + 0.25 * fh - ny / 2.0);
			text->RenderText(fb, l.name, shadow, float((px + off) * sx), float((py + off) * sy));
			text->RenderText(fb, l.name, text_color, px * sx, py * sy);
			if (l.highlighted)
				text->RenderText(fb, l.name, text_color, float((px + 1.0) * sx), py * sy);
			m_drawn_labels[l.vd] = DrawnLabel{ QPointF(l.ax, l.ay), b, QPointF(l.fx, l.ty), order++ };
			continue;
		}
		if (l.moved)
		{
			double y0, y1;
			line_y(l, l.ty, y0, y1);
			DrawLeader(centre(l.ax), y0, centre(l.ax), y1, nx, ny, text_color);
			if (l.left)   // from the corner along the line to the end of the name
				DrawLeader(centre(l.ax), centre(l.ty), text_x(l) + l.w + 2.0, centre(l.ty), nx, ny, text_color);
		}
		DrawMarker(l.ax, l.ay, half, nx, ny, l.color, text_color);
		if (l.left || l.moved)
			DrawMarker(square_x(l), l.ty, half, nx, ny, l.color, text_color);
		// the name over a shadow in the background colour, readable on the data
		const float px = float(text_x(l) - nx / 2.0);
		const float py = float(l.ty + 0.25 * fh - ny / 2.0);
		text->RenderText(fb, l.name, shadow, float((px + off) * sx), float((py + off) * sy));
		text->RenderText(fb, l.name, text_color, px * sx, py * sy);
		// the selected volume: bold (the list draws its name in its colour,
		// which on its own cell body is hard to read)
		if (l.highlighted)
			text->RenderText(fb, l.name, text_color, float((px + 1.0) * sx), py * sy);
		m_drawn_labels[l.vd] = DrawnLabel{ QPointF(l.ax, l.ay), box_of(l, l.ty), QPointF(square_x(l), l.ty), order++ };
	}
}

// a line of about 1.5 pixels from (x0, y0) to (x1, y1), frame pixels y down
void ViewOverlays::DrawLeader(double x0, double y0, double x1, double y1, int nx, int ny, Color color)
{
	const double dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
	if (len < 1.0)
		return;
	const float ox = float(-dy / len * 0.75), oy = float(dx / len * 0.75);
	const float xy[8] = { float(x0) + ox, float(y0) + oy, float(x1) + ox, float(y1) + oy,
		float(x1) - ox, float(y1) - oy, float(x0) - ox, float(y0) - oy };
	DrawQuadPx(xy, nx, ny, color);
}

void ViewOverlays::DrawMarker(double x, double y, int half, int nx, int ny, Color color, Color frame_color)
{
	const float cx = float(std::floor(x)), cy = float(std::floor(y));
	auto square = [&](float r, Color c) {
		const float l = cx - r, t = cy - r, rr = cx + r + 1.0f, b = cy + r + 1.0f;
		const float xy[8] = { l, t, rr, t, rr, b, l, b };
		DrawQuadPx(xy, nx, ny, c);
	};
	square(float(half + 2), frame_color);
	square(float(half), color);
}

void ViewOverlays::DrawQuadPx(const float* xy, int nx, int ny, Color color)
{
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();
	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];
	std::vector<Vulkan2dRender::Vertex> vertex;
	for (int i = 0; i < 4; i++)
		vertex.push_back(Vulkan2dRender::Vertex{ { xy[2 * i], xy[2 * i + 1], 0.0f }, { 0.0f, 0.0f, 0.0f } });
	std::vector<uint32_t> index = { 0, 1, 2, 2, 3, 0 };
	UpdateV2dObject(vulkan->vulkanDevice, m_leader_vobj, vertex, index);
	Vulkan2dRender::V2DRenderParams params = v2drender->GetNextV2dRenderSemaphoreSettings();
	params.pipeline = v2drender->preparePipeline(IMG_SHDR_DRAW_GEOMETRY, V2DRENDER_BLEND_DISABLE,
		fb->attachments[0]->format, (int)fb->attachments.size(), 0, fb->attachments[0]->is_swapchain_images);
	params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), 1.0f);
	params.matrix[0] = glm::ortho(0.0f, float(nx), 0.0f, float(ny));
	params.obj = &m_leader_vobj;
	RenderObj(params);
}

void ViewOverlays::QueueCellBody(VolumeData* vd)
{
	if (!vd || vd == m_anchor_running_vd ||
		std::find(m_anchor_queue.begin(), m_anchor_queue.end(), vd) != m_anchor_queue.end())
		return;
	m_anchor_queue.push_back(vd);
	if (!m_anchor_running_vd)
		QTimer::singleShot(0, this, &ViewOverlays::StartNextCellBody);   // not inside the frame
}

// one at a time in the global thread pool (VVDFindCellBody uses every core);
// the task holds the volume data, so a volume deleted meanwhile is harmless
void ViewOverlays::StartNextCellBody()
{
	while (!m_anchor_running_vd && !m_anchor_queue.empty() && m_canvas)
	{
		VolumeData* vd = m_anchor_queue.front();
		m_anchor_queue.pop_front();
		const std::vector<VolumeData*>& all = m_canvas->Volumes();
		if (std::find(all.begin(), all.end(), vd) == all.end())
			continue;
		std::shared_ptr<VL_Nrrd> vol = vd->GetVolume(false);
		Nrrd* n = vol ? vol->getNrrd() : nullptr;
		if (!n || !n->data || n->dim < 3)
			continue;
		int bytes = 0;
		switch (n->type)
		{
		case nrrdTypeUChar: bytes = 1; break;
		case nrrdTypeUShort: bytes = 2; break;
		case nrrdTypeFloat: bytes = 4; break;
		default: break;
		}
		const double thr = std::max(0.0, vd->GetLeftThresh() * vd->GetMaxValue());
		CellBodyAnchor& a = m_anchors[vd];
		a = CellBodyAnchor();
		a.data = n->data;
		a.thr = thr;
		a.vol = vol;
		if (!bytes)
		{
			a.done = true;   // no label (other data types)
			continue;
		}
		const size_t nx = n->axis[0].size, ny = n->axis[1].size, nz = n->axis[2].size;
		double spcx = 1.0, spcy = 1.0, spcz = 1.0;
		vd->GetSpacings(spcx, spcy, spcz);
		m_anchor_running_vd = vd;
		QPointer<ViewOverlays> self(this);
		std::shared_ptr<std::atomic<bool>> cancel = m_anchor_cancel;
		const void* key = n->data;
		QThreadPool::globalInstance()->start([=]() {
			const VVDCellBody r = VVDFindCellBody(n->data, bytes, nx, ny, nz, spcx, spcy, spcz, thr, cancel.get());
			const bool found = r.found;
			const double fx = r.x / double(nx), fy = r.y / double(ny), fz = r.z / double(nz);
			// (vol keeps the data alive while it is read, and its identity until
			// the result is stored)
			QMetaObject::invokeMethod(QCoreApplication::instance(), [=]() {
				if (self) self->OnCellBody(vd, vol, key, thr, found, fx, fy, fz);
			}, Qt::QueuedConnection);
		});
	}
}

void ViewOverlays::OnCellBody(VolumeData* vd, const std::shared_ptr<VL_Nrrd>& vol, const void* data, double thr,
	bool found, double fx, double fy, double fz)
{
	if (m_anchor_running_vd == vd)
		m_anchor_running_vd = nullptr;
	auto it = m_anchors.find(vd);
	if (it != m_anchors.end() && it->second.data == data && it->second.thr == thr && it->second.vol.lock() == vol)
	{
		it->second.done = true;
		it->second.found = found;
		it->second.fx = fx;
		it->second.fy = fy;
		it->second.fz = fz;
	}
	StartNextCellBody();
	if (m_canvas && m_names_on_objects && m_draw_legend)
		m_canvas->RefreshOverlayChange();
}

// ---------------------------------------------------------------------------
// VRenderVulkanView::SetColormapColors / DrawColormap
// ---------------------------------------------------------------------------

void ViewOverlays::SetColormapColors(int colormap)
{
	switch (colormap)
	{
	case 0://rainbow
		m_color_1 = Color(0.0, 0.0, 1.0);
		m_color_2 = Color(0.0, 0.0, 1.0);
		m_color_3 = Color(0.0, 1.0, 1.0);
		m_color_4 = Color(0.0, 1.0, 0.0);
		m_color_5 = Color(1.0, 1.0, 0.0);
		m_color_6 = Color(1.0, 0.0, 0.0);
		m_color_7 = Color(1.0, 0.0, 0.0);
		break;
	case 1://reverse rainbow
		m_color_1 = Color(1.0, 0.0, 0.0);
		m_color_2 = Color(1.0, 0.0, 0.0);
		m_color_3 = Color(1.0, 1.0, 0.0);
		m_color_4 = Color(0.0, 1.0, 0.0);
		m_color_5 = Color(0.0, 1.0, 1.0);
		m_color_6 = Color(0.0, 0.0, 1.0);
		m_color_7 = Color(0.0, 0.0, 1.0);
		break;
	case 2://hot
		m_color_1 = Color(0.0, 0.0, 0.0);
		m_color_2 = Color(0.0, 0.0, 0.0);
		m_color_3 = Color(0.5, 0.0, 0.0);
		m_color_4 = Color(1.0, 0.0, 0.0);
		m_color_5 = Color(1.0, 1.0, 0.0);
		m_color_6 = Color(1.0, 1.0, 1.0);
		m_color_7 = Color(1.0, 1.0, 1.0);
		break;
	case 3://cool
		m_color_1 = Color(0.0, 1.0, 1.0);
		m_color_2 = Color(0.0, 1.0, 1.0);
		m_color_3 = Color(0.25, 0.75, 1.0);
		m_color_4 = Color(0.5, 0.5, 1.0);
		m_color_5 = Color(0.75, 0.25, 1.0);
		m_color_6 = Color(1.0, 0.0, 1.0);
		m_color_7 = Color(1.0, 0.0, 1.0);
		break;
	case 4://blue-red
		m_color_1 = Color(0.25, 0.3, 0.75);
		m_color_2 = Color(0.25, 0.3, 0.75);
		m_color_3 = Color(0.475, 0.5, 0.725);
		m_color_4 = Color(0.7, 0.7, 0.7);
		m_color_5 = Color(0.7, 0.35, 0.425);
		m_color_6 = Color(0.7, 0.0, 0.15);
		m_color_7 = Color(0.7, 0.0, 0.15);
		break;
	}
}

void ViewOverlays::DrawColormap(int nx, int ny)
{
	bool draw = false;

	int num = 0;
	VolumeData* vd_one = nullptr;
	double max_val = 255.0;
	bool enable_alpha = false;

	// wx GetDispVolumeNum / GetDispVolumeData (m_vd_pop_list)
	for (VolumeData* vd : m_canvas->PopVolumeList())
	{
		if (vd && vd->GetColormapMode() == 1 && vd->GetDisp())
		{
			num++;
			vd_one = vd;
		}
	}

	VolumeData* vd_view = nullptr;
	if (num == 0)
		return;
	else if (num == 1)
		vd_view = vd_one;
	else if (num > 1)
	{
		// wx: the volume selected in the tree (vr_frame->GetCurSelVol())
		VolumeData* vd = m_canvas->GetCurrentVolume();
		if (vd && vd->GetDisp() &&
			std::find(m_canvas->Volumes().begin(), m_canvas->Volumes().end(), vd) != m_canvas->Volumes().end() &&
			vd->GetColormapDisp())
			vd_view = vd;
	}
	if (vd_view)
	{
		draw = true;
		double low, high;
		vd_view->GetColormapValues(low, high);
		m_value_2 = low;
		m_value_6 = high;
		m_value_4 = (low + high) / 2.0;
		m_value_3 = (low + m_value_4) / 2.0;
		m_value_5 = (m_value_4 + high) / 2.0;
		max_val = vd_view->GetMaxValue();
		enable_alpha = vd_view->GetEnableAlpha();
		SetColormapColors(vd_view->GetColormap());
	}

	if (!draw)
		return;

	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();
	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];

	float offset = 0.0f;
	if (m_draw_legend)
		offset = m_sb_height;

	float sx, sy;
	sx = 2.0 / nx;
	sy = 2.0 / ny;

	glm::mat4 proj_mat = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f);

	std::vector<Vulkan2dRender::Vertex34> vertex;
	vertex.reserve(14);

	float px, py;
	float fx, fy, fw, fh;
	if (m_draw_frame)
	{
		fx = m_frame_x;
		fy = m_frame_y;
		fw = m_frame_w;
		fh = m_frame_h;
	}
	else
	{
		fx = 0.0f;
		fy = 0.0f;
		fw = nx;
		fh = ny;
	}

	auto V = [&](float x, float vy, const Color& c, float a) {
		vertex.push_back(Vulkan2dRender::Vertex34{
			{x, vy, 0.0f}, {(float)c.r(), (float)c.g(), (float)c.b(), a} });
	};
	px = (0.01 * fw + fx) / nx;
	py = (0.05 * fw + fx) / nx;
	float y;
	y = (fh - (0.1f * fh + fy + offset)) / ny;
	V(px, y, m_color_1, enable_alpha ? 0.0f : 1.0f);
	V(py, y, m_color_1, enable_alpha ? 0.0f : 1.0f);
	y = (fh - ((0.1f + 0.4f * m_value_2) * fh + fy + offset)) / ny;
	V(px, y, m_color_2, enable_alpha ? m_value_2 : 1.0f);
	V(py, y, m_color_2, enable_alpha ? m_value_2 : 1.0f);
	y = (fh - ((0.1f + 0.4f * m_value_3) * fh + fy + offset)) / ny;
	V(px, y, m_color_3, enable_alpha ? m_value_3 : 1.0f);
	V(py, y, m_color_3, enable_alpha ? m_value_3 : 1.0f);
	y = (fh - ((0.1f + 0.4f * m_value_4) * fh + fy + offset)) / ny;
	V(px, y, m_color_4, enable_alpha ? m_value_4 : 1.0f);
	V(py, y, m_color_4, enable_alpha ? m_value_4 : 1.0f);
	y = (fh - ((0.1f + 0.4f * m_value_5) * fh + fy + offset)) / ny;
	V(px, y, m_color_5, enable_alpha ? m_value_5 : 1.0f);
	V(py, y, m_color_5, enable_alpha ? m_value_5 : 1.0f);
	y = (fh - ((0.1f + 0.4f * m_value_6) * fh + fy + offset)) / ny;
	V(px, y, m_color_6, enable_alpha ? m_value_6 : 1.0f);
	V(py, y, m_color_6, enable_alpha ? m_value_6 : 1.0f);
	y = (fh - (0.5f * fh + fy + offset)) / ny;
	V(px, y, m_color_7, 1.0f);
	V(py, y, m_color_7, 1.0f);

	std::vector<uint32_t> index = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13 };
	UpdateV2dObject(vulkan->vulkanDevice, m_cmap_vobj, vertex, index);

	Vulkan2dRender::V2DRenderParams params = v2drender->GetNextV2dRenderSemaphoreSettings();
	params.pipeline =
		v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY_COLOR4,
			V2DRENDER_BLEND_OVER_UI,
			fb->attachments[0]->format,
			(int)fb->attachments.size(),
			0,
			fb->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
		);
	params.matrix[0] = proj_mat;
	params.obj = &m_cmap_vobj;
	RenderObj(params);

	if (TextRenderer* text = Text())
	{
		std::wstring wstr;
		Color text_color = GetTextColor();
		auto label = [&](double vy, int value) {
			float lx = 0.052 * fw + fx - nx / 2.0;
			float ly = vy - ny / 2.0;
			wstr = W(QString::asprintf("%d", value));
			text->RenderText(fb, wstr, text_color, lx * sx, ly * sy);
		};
		//value 1
		label(fh - (0.1 * fh + fy + offset), 0);
		//value 2
		label(fh - ((0.1 + 0.4 * m_value_2) * fh + fy + offset), int(m_value_2 * max_val));
		//value 4
		label(fh - ((0.1 + 0.4 * m_value_4) * fh + fy + offset), int(m_value_4 * max_val));
		//value 6
		label(fh - ((0.1 + 0.4 * m_value_6) * fh + fy + offset), int(m_value_6 * max_val));
		//value 7
		label(fh - (0.5 * fh + fy + offset), int(max_val));
	}
}

// ---------------------------------------------------------------------------
// VRenderVulkanView::DrawInfo
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// VRenderVulkanView::DrawAnnotations: mesh tree labels, mesh annotations and
// annotation layers drawn as text at their projected positions, or, when the
// layer carries a mesh (the "Mesh:" line of its file), as that mesh
// (RenderCanvas::DrawAnnotationMesh).
// ---------------------------------------------------------------------------

void ViewOverlays::DrawAnnotations(int nx, int ny)
{
	TextRenderer* text = nullptr;
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];

	float sx, sy;
	sx = 2.0f / nx;
	sy = 2.0f / ny;
	float px, py;

	Transform mv;
	Transform p;
	glm::mat4 mv_mat = m_canvas->GetMvMat();
	glm::mat4 proj_mat = m_canvas->GetProjMat();
	mv.set(glm::value_ptr(mv_mat));
	p.set(glm::value_ptr(proj_mat));

	Color text_color = GetTextColor();
	auto draw = [&](const std::wstring& wstr, Point pos) {
		pos = mv.transform(pos);
		pos = p.transform(pos);
		if (pos.x() >= -1.0 && pos.x() <= 1.0 &&
			pos.y() >= -1.0 && pos.y() <= 1.0)
		{
			if (pos.z() <= 0.0 || pos.z() >= 1.0)
				return;
			if (!text && !(text = Text()))
				return;
			px = pos.x() * nx / 2.0;
			py = pos.y() * ny / 2.0;
			text->RenderText(fb, wstr, text_color, px * sx, py * sy);
		}
	};
	auto s2ws = [](const std::string& s) { return QString::fromStdString(s).toStdWString(); };
	auto mesh_annotations = [&](MeshData* md) {
		Annotations* ann = md->GetAnnotations();
		for (int j = 0; j < ann->GetTextNum(); ++j)
		{
			Point pos = ann->GetTextPos(j);
			if (!md->InsideClippingPlanes(pos))
				continue;
			pos.z(-pos.z());
			draw(s2ws(ann->GetTextText(j)), pos);
		}
	};

	for (int i = 0; i < m_canvas->GetLayerNum(); i++)
	{
		TreeLayer* layer = m_canvas->GetLayer(i);
		if (!layer)
			continue;
		switch (layer->IsA())
		{
		case 3://mesh data
		{
			MeshData* md = (MeshData*)layer;
			if (md && md->GetDisp() && md->isTree())
			{
				auto labels = md->GetSubmeshLabels();
				auto active_ids = md->GetActiveLabelSet();
				for (auto ite = active_ids->begin(); ite != active_ids->end(); ++ite)
				{
					if (labels->count(*ite) > 0 && md->GetROIVisibility(*ite))
					{
						MeshRenderer::SubMeshLabel& lbl = (*labels)[*ite];
						for (auto pos : lbl.points)
						{
							if (!md->InsideClippingPlanes(pos))
								continue;
							pos.z(-pos.z());
							draw(lbl.name, pos);
						}
					}
				}
			}
			else if (md && md->GetDisp() && md->GetLabelVisibility() && md->GetAnnotations())
				mesh_annotations(md);
			break;
		}
		case 6://mesh group
		{
			MeshGroup* group = (MeshGroup*)layer;
			if (!group->GetDisp())
				continue;
			for (int j = 0; j < group->GetMeshNum(); j++)
			{
				MeshData* md = group->GetMeshData(j);
				if (md && md->GetDisp() && md->GetLabelVisibility() && md->GetAnnotations())
					mesh_annotations(md);
			}
			break;
		}
		case 4://annotations
		{
			Annotations* ann = (Annotations*)layer;
			if (!ann->GetDisp())
				continue;
			// a layer that carries a mesh is drawn as that mesh
			if (ann->GetMesh())
			{
				m_canvas->DrawAnnotationMesh(ann, nx, ny);
				continue;
			}
			// its volume was deleted: no positions (they all stacked at the origin)
			if (!ann->HasTransform())
				continue;
			for (int j = 0; j < ann->GetTextNum(); ++j)
			{
				Point pos = ann->GetTextPos(j);
				if (!ann->InsideClippingPlanes(pos))
					continue;
				draw(s2ws(ann->GetTextText(j)), ann->GetTextTransformedPos(j));
			}
			break;
		}
		}
	}
}

void ViewOverlays::DrawInfo(int nx, int ny)
{
	TextRenderer* text = Text();
	if (!text)
		return;
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	const std::unique_ptr<vks::VFrameBuffer>& fb = vulkan->frameBuffers[vulkan->currentBuffer];
	VolumeData* cur_vol = m_canvas->GetCurrentVolume();

	float sx, sy;
	sx = 2.0 / nx;
	sy = 2.0 / ny;
	float px, py;
	float gapw = text->GetSize();
	float gaph = gapw;

	double avg = m_interval_sum / 10.0;
	double fps_ = avg > 0.0 ? 1.0 / avg : 0.0;
	QString str;
	Color text_color = GetTextColor();
	if (TextureRenderer::get_streaming())
	{
		str = QString::asprintf(
			"FPS: %.2f, Bricks: %d, Time: %lu",
			fps_ >= 0.0 && fps_ < 300.0 ? fps_ : 0.0,
			TextureRenderer::get_total_brick_num(),
			TextureRenderer::get_cor_up_time());
	}
	else
		str = QString::asprintf("FPS: %.2f", fps_ >= 0.0 && fps_ < 300.0 ? fps_ : 0.0);

	if (cur_vol)
	{
		int resx = 0, resy = 0, resz = 0;
		if (cur_vol->GetTexture())
		{
			resx = cur_vol->GetTexture()->nx();
			resy = cur_vol->GetTexture()->ny();
			resz = cur_vol->GetTexture()->nz();
		}
		if (cur_vol->isBrxml())
			str += QString::asprintf(" VVD_Level: %d/%d W:%d H:%d D:%d,", cur_vol->GetLevel() + 1, cur_vol->GetLevelNum(), resx, resy, resz);
		long long used_mem = 0;
		int dtnum = 0, qnum = 0, dqnum = 0;
		if (VolumeLoader* loader = m_canvas->GetLoader())
			loader->GetPalams(used_mem, dtnum, qnum, dqnum);
		str += QString::asprintf(" Mem:%lld(MB) Th: %d Q: %d DQ: %d,", used_mem / (1024LL * 1024LL), dtnum, qnum, dqnum);
	}

	std::wstring wstr_temp = W(str);
	px = gapw - nx / 2.0f;
	py = gaph - ny / 2.0f;
	text->RenderText(fb, wstr_temp, text_color, px * sx, py * sy);

	gaph += text->GetSize();

	int tseq_cur_num = m_ruler ? m_ruler->GetCurTime() : 0;
	if (m_draw_coord)
	{
		Point p;
		// wx: ScreenToClient(wxGetMousePosition())
		QPoint mouse_pos = m_canvas->mapFromGlobal(QCursor::pos());
		if (m_ruler &&
			((cur_vol && m_ruler->GetPointVolumeBox(p, mouse_pos.x(), mouse_pos.y(), cur_vol) > 0.0) ||
			 m_ruler->GetPointPlane(p, mouse_pos.x(), mouse_pos.y()) > 0.0))
		{
			str = QString::asprintf("T: %d  X: %.2f  Y: %.2f  Z: %.2f",
				tseq_cur_num, p.x(), p.y(), p.z());
			wstr_temp = W(str);
			px = gapw - nx / 2;
			py = gaph - ny / 2;
			text->RenderText(fb, wstr_temp, text_color, px * sx, py * sy);
		}
	}
	else
	{
		str = QString::asprintf("T: %d", tseq_cur_num);
		wstr_temp = W(str);
		px = gapw - nx / 2;
		py = gaph - ny / 2;
		text->RenderText(fb, wstr_temp, text_color, px * sx, py * sy);
	}

	MultiVolumeRenderer* mvr = m_canvas->GetMultiVolumeRenderer();
	if (m_canvas->GetVolMethod() == RenderCanvas::VolMethodMulti && mvr)
	{
		str = QString::asprintf("SLICES: %d", mvr->get_slice_num());
		wstr_temp = W(str);
		px = gapw - nx / 2;
		py = ny / 2 - gaph * 1.5;
		text->RenderText(fb, wstr_temp, text_color, px * sx, py * sy);
		return;
	}
	std::vector<VolumeData*> vd_list = m_canvas->PopVolumeList();   // wx m_vd_pop_list
	for (int i = 0; i < (int)vd_list.size(); i++)
	{
		VolumeData* vd = vd_list[i];
		if (vd && vd->GetVR())
		{
			str = QString::asprintf("SLICES_%d: %d", i + 1, vd->GetVR()->get_slice_num());
			wstr_temp = W(str);
			px = gapw - nx / 2;
			py = ny / 2 - gaph * (3 + i) / 2;
			text->RenderText(fb, wstr_temp, text_color, px * sx, py * sy);
		}
	}
}
