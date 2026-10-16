// Port of the overlay drawing of VRenderVulkanView (VRenderView.cpp):
// DrawCamCtr, DrawLegend / DrawName, DrawScaleBar, DrawColormap /
// SetColormapColors, DrawFrame, DrawInfo, FixScaleBarLen / GetScaleBarFixed.
// See ViewOverlays.h for the member mapping.

#include "ViewOverlays.h"
#include "RenderCanvas.h"
#include "RulerTool.h"
#include "SettingsDialog.h"

#include <QMouseEvent>
#include <QCursor>
#include <QDir>

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
	if (m_draw_legend)
		DrawLegend(nx, ny);
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

	//first pass
	for (VolumeData* vd : vd_list)
		if (vd->GetLegend())
			advance(QString::fromStdWString(vd->GetName()), nullptr, false);
	for (MeshData* md : md_list)
		if (md->GetLegend())
			advance(QString::fromStdWString(md->GetName()), nullptr, false);

	//second pass
	int cur_line = 0;
	double xpos;
	length = 0.0;
	for (VolumeData* vd : vd_list)
	{
		if (!vd->GetLegend())
			continue;
		QString name = QString::fromStdWString(vd->GetName());
		if (advance(name, &xpos, true))
			cur_line++;
		bool highlighted = sel_vd && QString::fromStdWString(sel_vd->GetName()) == name;
		DrawName(xpos + xoffset, ny - (lines - cur_line + 0.1) * font_height - yoffset,
			nx, ny, name, vd->GetColor(), font_height, highlighted);
	}
	for (MeshData* md : md_list)
	{
		if (!md->GetLegend())
			continue;
		QString name = QString::fromStdWString(md->GetName());
		if (advance(name, &xpos, true))
			cur_line++;
		Color amb, diff, spec;
		double shine, alpha;
		md->GetMaterial(amb, diff, spec, shine, alpha);
		Color c(diff.r(), diff.g(), diff.b());
		bool highlighted = sel_md && QString::fromStdWString(sel_md->GetName()) == name;
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
