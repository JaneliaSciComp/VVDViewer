#include "BrushTool.h"

#include <QApplication>
#include <QGuiApplication>
#include <QWidget>
#include <QWindow>
#include <QCursor>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QDir>
#include <QFile>

#include <cmath>
#include <algorithm>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "RenderCanvas.h"
#include "AnalysisContext.h"
#include "VVulkan.h"
#include "Vulkan/Vulkan2dRender.h"
#include "Vulkan/VulkanDevice.hpp"
#include "FLIVR/TextureRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/Texture.h"
#include "FLIVR/ImgShader.h"
#include "DataManager.h"
#include "VolumeSelector.h"
#include "VolumeCalculator.h"
#include "TextRenderer.h"
#include "Core/FileUtil.h"
#include "Core/PaintTiming.h"

using namespace FLIVR;

struct BrushTool::BrushGeom
{
	Vulkan2dRender::V2dObject vobj;   // wx m_brush_vobj
};

namespace
{
	const double kPi = 3.14159265358979323846;

	// verbatim from VRenderView.cpp: overlay geometry goes into the device's
	// per-frame host-visible rings (rewritten every draw)
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

	QString FromVVD(const VVDString& s) { return QString::fromStdWString(s); }
}

BrushTool::BrushTool(RenderCanvas* canvas, AnalysisContext* ctx, QObject* parent)
	: QObject(parent),
	m_canvas(canvas),
	m_ctx(ctx),
	m_geom(std::make_unique<BrushGeom>())
{
	// wx polls the keyboard in OnIdle / OnMouse (VRenderFrame::GetKeyState);
	// here the key state comes from an application-wide filter.
	if (qApp)
		qApp->installEventFilter(this);
	LoadDefaultBrushSettings();
}

BrushTool::~BrushTool()
{
	if (qApp)
		qApp->removeEventFilter(this);
	if (m_canvas)
		m_canvas->RemoveTool(this);
	ReleaseResources();
}

RenderCanvas* BrushTool::Canvas() const
{
	return m_canvas.data();
}

void BrushTool::ReleaseResources()
{
	if (m_canvas && m_canvas->IsVulkanReady() && (m_fbo_paint || m_text_renderer))
		if (std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan())
			for (auto dev : vulkan->devices)
				dev->WaitIdleAllFrameSlots();
	m_fbo_paint.reset();
	m_tex_paint.reset();
	m_text_renderer.reset();
	m_text_renderer_failed = false;
}

VolumeSelector* BrushTool::Sel() const { return m_ctx ? m_ctx->GetSelector() : nullptr; }
VolumeCalculator* BrushTool::Calc() const { return m_ctx ? m_ctx->GetCalculator() : nullptr; }
VolumeData* BrushTool::CurVolume() const { return m_ctx ? m_ctx->CurrentVolume() : nullptr; }

bool BrushTool::IsActive() const
{
	return m_int_mode == 2 || m_int_mode == 4 || m_int_mode == 7 || m_int_mode == 8;
}

// ---------------------------------------------------------------------------
// modes (VRenderVulkanView::SetIntMode / SetPaintMode / SetBrush)
// ---------------------------------------------------------------------------

void BrushTool::SetIntMode(int mode)
{
	m_int_mode = mode;
	if (m_int_mode == 1)
	{
		m_brush_state = 0;
		m_draw_brush = false;
	}
	if (m_canvas) m_canvas->RefreshView();
	NotifyIntMode();
}

// The wx view has one m_int_mode for the brush and the ruler tools; the Qt
// tools keep their own copy and MainWindow keeps them in step from this
// signal. 4 / 8 are the one-frame "stroke just ended" states of 2 / 7.
void BrushTool::NotifyIntMode()
{
	const int logical = m_int_mode == 4 ? 2 : (m_int_mode == 8 ? 7 : m_int_mode);
	if (logical == m_notified_int_mode)
		return;
	m_notified_int_mode = logical;
	Q_EMIT intModeChanged(logical);
}

void BrushTool::SetPaintMode(int mode)
{
	if (VolumeSelector* sel = Sel())
		sel->SetMode(mode);
	m_brush_state = mode;
}

int BrushTool::GetPaintMode() const
{
	VolumeSelector* sel = Sel();
	return sel ? sel->GetMode() : 0;
}

void BrushTool::SetPaintUse2d(bool use2d)
{
	if (VolumeSelector* sel = Sel()) sel->SetPaintUse2d(use2d);
}

bool BrushTool::GetPaintUse2d() const
{
	VolumeSelector* sel = Sel();
	return sel ? sel->GetPaintUse2d() : false;
}

void BrushTool::SetBrush(int mode)
{
	// wx: m_prev_focus = FindFocus(); SetFocus();
	if (m_canvas && !m_canvas->hasFocus())
	{
		if (!m_prev_focus)
			m_prev_focus = QApplication::focusWidget();
		m_canvas->setFocus();
	}
	// no TraceDlg in the Qt port: autoid is false
	VolumeSelector* sel = Sel();
	if (m_int_mode == 5 ||
		m_int_mode == 7)
	{
		m_int_mode = 7;
		if (sel) sel->SetMode(m_ruler_type == 3 ? 8 : 1);
	}
	else if (m_int_mode == 8)
	{
		if (sel) sel->SetMode(m_ruler_type == 3 ? 8 : 1);
	}
	else
	{
		m_int_mode = 2;
		if (sel) sel->SetMode(mode);
	}
	m_paint_display = true;
	m_draw_brush = true;
	NotifyIntMode();
	// create the text renderer outside a frame (wx: in InitVulkan)
	if (m_canvas && m_canvas->IsVulkanReady())
		GetTextRendererInst();
}

// wx TreePanel::BrushAppend / BrushDiffuse / BrushDesel / BrushSolid
void BrushTool::SelectBrushMode(int paint_mode)
{
	if (paint_mode > 0)
	{
		SetIntMode(2);
		SetPaintMode(paint_mode);
	}
	else
		SetIntMode(1);
	Q_EMIT brushSelected((paint_mode == 2 || paint_mode == 3 || paint_mode == 4) ? paint_mode : 0);
	if (m_canvas) m_canvas->RefreshView();
}

// ---------------------------------------------------------------------------
// keyboard state (wx VRenderVulkanView::UpdateBrushState, OnIdle)
// ---------------------------------------------------------------------------

bool BrushTool::KeyHeld(int key) const
{
	if (key == Qt::Key_Shift && m_test_shift >= 0)
		return m_test_shift != 0;
	Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
	if (key == Qt::Key_Shift)
		return mods & Qt::ShiftModifier;
	// Ctrl+Z / Ctrl+Y are undo/redo, not the diffuse brush
	if (mods & Qt::ControlModifier)
		return false;
	if (key == Qt::Key_Z) return m_key_z;
	if (key == Qt::Key_X) return m_key_x;
	return false;
}

bool BrushTool::MouseInCanvas() const
{
	if (!m_canvas || !m_canvas->isVisible())
		return false;
	if (QGuiApplication::applicationState() != Qt::ApplicationActive)
		return false;
	return m_canvas->rect().contains(m_canvas->mapFromGlobal(QCursor::pos()));
}

// wx: the focused window is a wxTextCtrl ("editting") / m_key_lock
bool BrushTool::Editing() const
{
	QWidget* f = QApplication::focusWidget();
	if (!f) return false;
	if (qobject_cast<QLineEdit*>(f) || qobject_cast<QAbstractSpinBox*>(f) ||
		qobject_cast<QTextEdit*>(f) || qobject_cast<QPlainTextEdit*>(f))
		return true;
	if (QComboBox* cb = qobject_cast<QComboBox*>(f))
		return cb->isEditable();
	return false;
}

bool BrushTool::eventFilter(QObject* obj, QEvent* e)
{
	switch (e->type())
	{
	case QEvent::KeyPress:
	case QEvent::KeyRelease:
	{
		auto* ke = static_cast<QKeyEvent*>(e);
		// application filters see an event again for every widget it is
		// propagated to; handle it once
		// (same object, time stamp, type and key: a stack-allocated event
		// can reuse the address of the previous one)
		if (e == m_last_key_event && ke->timestamp() == m_last_key_ts &&
			e->type() == m_last_key_type && ke->key() == m_last_key_code)
			break;
		m_last_key_event = e;
		m_last_key_ts = ke->timestamp();
		m_last_key_type = e->type();
		m_last_key_code = ke->key();

		bool down = e->type() == QEvent::KeyPress;
		if (!ke->isAutoRepeat())
		{
			if (ke->key() == Qt::Key_Z) m_key_z = down;
			else if (ke->key() == Qt::Key_X) m_key_x = down;
		}
		if (Editing())
			break;
		// Ctrl+Z / Ctrl+Y (wx OnIdle -> BrushToolDlg::BrushUndo / BrushRedo),
		// only the current view's brush (wx: this == cur_glview)
		const bool key_here = !RenderCanvas::KeyCanvas() || RenderCanvas::KeyCanvas() == Canvas();
		Qt::KeyboardModifiers mods = ke->modifiers() &
			(Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
		if (ke->key() == Qt::Key_Z)
		{
			if (down && key_here && mods == Qt::ControlModifier && !m_undo_keydown)
			{
				m_undo_keydown = true;
				BrushUndo();
			}
			if (!down) m_undo_keydown = false;
		}
		else if (ke->key() == Qt::Key_Y)
		{
			if (down && key_here && mods == Qt::ControlModifier && !m_redo_keydown)
			{
				m_redo_keydown = true;
				BrushRedo();
			}
			if (!down) m_redo_keydown = false;
		}
		if (MouseInCanvas())
			UpdateBrushState();
		break;
	}
	case QEvent::MouseMove:
	case QEvent::MouseButtonPress:
		// wx OnMouse: UpdateBrushState() first when the pointer is in the view
		if (obj && obj->isWindowType() && MouseInCanvas() && !Editing())
			UpdateBrushState();
		break;
	case QEvent::ApplicationStateChange:
		if (QGuiApplication::applicationState() != Qt::ApplicationActive)
		{
			m_key_z = m_key_x = false;
			m_undo_keydown = m_redo_keydown = false;
		}
		break;
	default:
		break;
	}
	return false;
}

void BrushTool::UpdateBrushState()
{
	bool shift = KeyHeld(Qt::Key_Shift);
	bool key_z = KeyHeld(Qt::Key_Z);
	bool key_x = KeyHeld(Qt::Key_X);

	if (m_int_mode != 2 && m_int_mode != 7)
	{
		if (shift)
		{
			SetBrush(2);
			Q_EMIT brushSelected(2);
			m_clear_paint = true;
			if (m_canvas) m_canvas->RefreshView();
		}
		else if (key_z)
		{
			SetBrush(4);
			Q_EMIT brushSelected(4);
			m_clear_paint = true;
			if (m_canvas) m_canvas->RefreshView();
		}
		else if (key_x)
		{
			SetBrush(3);
			Q_EMIT brushSelected(3);
			m_clear_paint = true;
			if (m_canvas) m_canvas->RefreshView();
		}
	}
	else
	{
		if (m_brush_state)
		{
			if (shift)
			{
				m_brush_state = 0;
				SetBrush(2);
				Q_EMIT brushSelected(2);
				m_clear_paint = true;
				if (m_canvas) m_canvas->RefreshView();
			}
			else if (key_z)
			{
				m_brush_state = 0;
				SetBrush(4);
				Q_EMIT brushSelected(4);
				m_clear_paint = true;
				if (m_canvas) m_canvas->RefreshView();
			}
			else if (key_x)
			{
				m_brush_state = 0;
				SetBrush(3);
				Q_EMIT brushSelected(3);
				m_clear_paint = true;
				if (m_canvas) m_canvas->RefreshView();
			}
			else
			{
				SetBrush(m_brush_state);
				if (m_canvas) m_canvas->RefreshView();
			}
		}
		else if (!shift && !key_z && !key_x)
		{
			m_clear_paint = true;

			bool paint_enable = m_on_press;   // wx m_paint_enable: a stroke in progress
			if (m_int_mode == 7)
				m_int_mode = 5;
			else
				m_int_mode = 1;
			m_paint_display = false;
			m_draw_brush = false;
			m_on_press = false;
			Q_EMIT brushSelected(0);

			if (paint_enable)
			{
				FlushStrokes();
				Segment();
				if (m_ctx) m_ctx->RefreshView();
			}
			else if (m_canvas)
				m_canvas->RefreshView();

			if (m_prev_focus)
			{
				m_prev_focus->setFocus();
				m_prev_focus = nullptr;
			}
			NotifyIntMode();   // 7 -> 5 (ruler mode again) or 2 -> 1
		}
	}
}

// ---------------------------------------------------------------------------
// mouse (VRenderVulkanView::OnMouse, int modes 2 / 7)
// ---------------------------------------------------------------------------

bool BrushTool::MousePress(QMouseEvent* e)
{
	if (!IsActive())
		return false;
	// a click right after a stroke (the frame that ends mode 4 has not run yet)
	if (m_int_mode == 4) m_int_mode = 2;
	if (m_int_mode == 8) m_int_mode = 7;

	QPoint p = e->pos();
	if (e->button() == Qt::LeftButton)
	{
		m_old_mouse = p;
		m_prv_mouse = p;
		if (m_int_mode == 2 || m_int_mode == 7)
		{
			if (m_int_mode == 7)
			{
				auto clear_sel = [this]() {
					int int_mode = GetIntMode();
					int paint_mode = GetPaintMode();
					int brush_state = m_brush_state;
					SetPaintMode(6);
					Segment();
					SetPaintMode(paint_mode);
					m_brush_state = brush_state;
					m_int_mode = int_mode;
				};
				clear_sel();
				//Measurement Auto-select: the stroke paints the volume under it
				if (m_ruler_press_hook && m_ruler_press_hook(p.x(), p.y()))
					clear_sel();
			}
			m_on_press = true;
			m_clear_paint = true;
			if (vvd::ptime::On())
			{
				vvd::ptime::StrokeWindow& w = vvd::ptime::Stroke();
				w = vvd::ptime::StrokeWindow();
				w.active = true;
				w.press_ms = vvd::ptime::NowMs();
				m_pt_moves = 0;
				vvd::ptime::Log("press: paint mode %d, brush %.0f/%.0f", GetPaintMode(), m_brush_radius1, m_brush_radius2);
			}
			m_pressure = PenPressure();
			m_strokes.push_back({ p, p, m_pressure });
			if (m_canvas) m_canvas->RefreshOverlays();   // wx RefreshGLOverlays
		}
	}
	else
		m_old_mouse = p;   // wx RightDown / MiddleDown
	return true;
}

bool BrushTool::MouseMove(QMouseEvent* e)
{
	if (!IsActive())
		return false;
	QPoint p = e->pos();
	if (e->buttons() != Qt::NoButton)
	{
		// dragging
		bool paint = (m_int_mode == 2 || m_int_mode == 7) && m_on_press &&
			(e->buttons() & Qt::LeftButton);
		if (paint)
			m_pressure = PenPressure();   // wx: 1.0 (mouse); the pen's pressure while a pen is down
		//update mouse position
		if (m_old_mouse.x() >= 0 && m_old_mouse.y() >= 0)
		{
			m_prv_mouse = m_old_mouse;
			m_old_mouse = p;
		}
		else
		{
			m_old_mouse = p;
			m_prv_mouse = p;
		}
		if (paint)
		{
			m_pt_moves++;
			m_strokes.push_back({ m_prv_mouse, m_old_mouse, m_pressure });
			if (m_canvas) m_canvas->RefreshOverlays();   // wx RefreshGLOverlays
		}
		return true;
	}
	// draw the brush shape
	if (m_draw_brush)
	{
		m_old_mouse = p;
		if (m_canvas) m_canvas->RefreshOverlays();   // wx RefreshGLOverlays
	}
	return true;
}

bool BrushTool::MouseRelease(QMouseEvent* e)
{
	if (!IsActive())
		return false;
	if (e->button() != Qt::LeftButton)
		return true;
	if (m_int_mode == 4) m_int_mode = 2;
	if (m_int_mode == 8) m_int_mode = 7;

	const bool ptm = vvd::ptime::On();
	const double pt0 = ptm ? vvd::ptime::NowMs() : 0.0;
	if (ptm)
		vvd::ptime::Log("release: %.1f ms after press, %d drag events, %d frames while held (%.1f ms rendering), %zu strokes pending",
			pt0 - vvd::ptime::Stroke().press_ms, m_pt_moves, vvd::ptime::Stroke().frames, vvd::ptime::Stroke().frame_ms, m_strokes.size());
	if (m_int_mode == 2)
	{
		//segment volumes
		{
			vvd::ptime::Scope sc("release: FlushStrokes (RenderNow)");
			FlushStrokes();
		}
		m_on_press = false;
		Segment();
		m_int_mode = 4;
		m_clear_paint = true;
	}
	else if (m_int_mode == 7)
	{
		//segment volume, calculate center, add ruler point
		FlushStrokes();
		m_on_press = false;
		Segment();
		if (m_ruler_type == 3)
			Q_EMIT rulerPointRequested(e->pos().x(), e->pos().y());
		else
			Q_EMIT paintRulerPointRequested();
		m_int_mode = 8;
		m_clear_paint = true;
	}
	m_on_press = false;
	if (m_ctx) m_ctx->RefreshView();
	else if (m_canvas) m_canvas->RefreshView();
	if (ptm)
	{
		const double t = vvd::ptime::NowMs();
		vvd::ptime::Log("release handler total %.1f ms", t - pt0);
		vvd::ptime::Stroke().release_ms = t;
		vvd::ptime::Stroke().frames = 0;
		vvd::ptime::Stroke().frame_ms = 0.0;
	}
	return true;
}

bool BrushTool::Wheel(QWheelEvent* e)
{
	if (!IsActive())
		return false;
	int wheel = RenderCanvas::WxWheelRotation(e);   // wx event.GetWheelRotation()
	if (!wheel)
		return true;
	VolumeSelector* sel = Sel();
	if (m_use_brush_radius2 && (!sel || sel->GetMode() != 8))
	{
		double delta = wheel / 100.0;
		m_brush_radius2 += delta;
		m_brush_radius2 = std::max(1.0, m_brush_radius2);
		m_brush_radius1 = std::min(m_brush_radius2, m_brush_radius1);
	}
	else
	{
		m_brush_radius1 += wheel / 100.0;
		m_brush_radius1 = std::max(m_brush_radius1, 1.0);
		m_brush_radius2 = m_brush_radius1;
	}
	Q_EMIT settingsChanged();
	if (m_canvas) m_canvas->RefreshView();
	return true;
}

bool BrushTool::KeyPress(QKeyEvent*)
{
	// hold keys / undo are handled in eventFilter; let the canvas see the key
	return false;
}

// Strokes are painted inside a frame; render one synchronously before the
// selector reads the paint buffer (the wx view refreshes synchronously on
// every mouse event, so its buffer is always up to date at LeftUp).
void BrushTool::FlushStrokes()
{
	if (m_canvas && m_canvas->IsVulkanReady())
		m_canvas->RenderNow();
}

// ---------------------------------------------------------------------------
// drawing (inside the canvas frame)
// ---------------------------------------------------------------------------

// The matrices Segment() hands to the group / select-both volumes: captured
// here, while the canvas has the object transforms applied (camera +
// translate/rotate/-centre, as wx recomputes before SetMatrices). In
// DrawOverlayPost the canvas has already restored the camera-only matrix, so
// group strokes were projected shifted by the object centre.
void BrushTool::DrawOverlay(RenderCanvas* canvas, int /*nx*/, int /*ny*/)
{
	if (!canvas)
		return;
	m_mv_mat = canvas->GetMvMat();
	m_proj_mat = canvas->GetProjMat();
	m_tex_mat = canvas->GetTexMat();
	m_have_matrices = true;
}

// VRenderVulkanView::UpdateScreen, painting part
void BrushTool::DrawOverlayPost(RenderCanvas* canvas, int nx, int ny)
{
	if (!canvas || !canvas->GetV2DRender())
		return;

	bool paint_enable = !m_strokes.empty();
	if (m_int_mode == 2 ||
		m_int_mode == 7)  //painting mode
	{
		if (m_draw_brush)
			DrawBrush(nx, ny);
		if (paint_enable || m_clear_paint)
		{
			//paiting mode
			//for volume segmentation
			if (m_strokes.empty())
				PaintStroke(nx, ny, m_prv_mouse, m_old_mouse, m_pressure);
			else
				for (const Stroke& s : m_strokes)
					PaintStroke(nx, ny, s.prv, s.cur, s.pressure);
		}
		if (m_on_press && m_paint_display)
		{
			//show the paint strokes
			DisplayStroke();
		}
	}
	else if (m_clear_paint)
	{
		PaintStroke(nx, ny, m_prv_mouse, m_old_mouse, m_pressure);
	}
	m_strokes.clear();

	if (m_int_mode == 4)
		m_int_mode = 2;
	if (m_int_mode == 8)
		m_int_mode = 7;
}

void BrushTool::DrawCircle(double cx, double cy, double radius, const Color& color, const glm::mat4& matrix)
{
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();

	int secs = 60;
	double deg = 0.0;

	std::vector<Vulkan2dRender::Vertex> vertex;
	std::vector<uint32_t> index;
	vertex.reserve(secs * 6);
	for (int i = 0; i < secs; ++i)
	{
		deg = i * 2 * kPi / secs;
		vertex.push_back(
			Vulkan2dRender::Vertex{
				{(float)(cx + radius * sin(deg)), (float)(cy + radius * cos(deg)), 0.0f},
				{0.0f, 0.0f, 0.0f}
			}
		);
		index.push_back(i);
	}
	index.push_back(0);

	UpdateV2dObject(vulkan->vulkanDevice, m_geom->vobj, vertex, index);

	Vulkan2dRender::V2DRenderParams params = v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = vulkan->frameBuffers[vulkan->currentBuffer].get();
	params.pipeline =
		v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			(int)current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
	params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), 1.0f);
	params.matrix[0] = matrix;
	// the scene is already in the swapchain image (RenderCanvas::DrawFinalBuffer
	// cleared it): never clear here (wx m_frame_clear is false by now)
	params.clear = false;
	Color bg = m_canvas->GetBackgroundColor();
	params.clearColor = { (float)bg.r(), (float)bg.g(), (float)bg.b() };

	params.obj = &m_geom->vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	v2drender->render(vulkan->frameBuffers[vulkan->currentBuffer], params);
}

//draw the brush shape
void BrushTool::DrawBrush(int nx, int ny)
{
	double pressure = m_use_pres && m_on_press ? m_pressure : 1.0;

	QPoint pos1 = m_old_mouse;
	QRect reg(0, 0, nx, ny);
	if (!reg.contains(pos1))
		return;

	double cx = pos1.x();
	double cy = pos1.y();
	float sx, sy;
	sx = 2.0f / nx;
	sy = 2.0f / ny;

	//draw the circles
	//set up the matrices
	glm::mat4 proj_mat = glm::ortho(float(0), float(nx), float(0), float(ny));

	//attributes
	int mode = GetPaintMode();
	Color text_color = GetTextColor();

	if (mode == 1 ||
		mode == 2 ||
		mode == 8)
	{
		DrawCircle(cx, cy, m_brush_radius1 * pressure,
			text_color, proj_mat);
	}

	if (mode == 1 ||
		mode == 2 ||
		mode == 3 ||
		mode == 4)
	{
		DrawCircle(cx, cy, m_brush_radius2 * pressure,
			text_color, proj_mat);
	}

	float px, py;
	px = float(cx - 7 - nx / 2.0);
	py = float(cy + 3 - ny / 2.0);
	std::wstring wstr;
	switch (mode)
	{
	case 1:
		wstr = L"S";
		break;
	case 2:
		wstr = L"+";
		break;
	case 3:
		wstr = L"-";
		break;
	case 4:
		wstr = L"*";
		break;
	}
	if (wstr.empty())
		return;
	if (TextRenderer* tr = GetTextRendererInst())
	{
		std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
		tr->RenderText(vulkan->frameBuffers[vulkan->currentBuffer],
			wstr, text_color, px * sx, py * sy);
	}
}

//paint strokes on the paint fbo
double BrushTool::PenPressure() const
{
	bool active = false;
	double p = 1.0;
	if (m_canvas) m_canvas->GetTabletPressure(active, p);
	return active ? p : 1.0;
}

void BrushTool::PaintStroke(int nx, int ny, const QPoint& prv, const QPoint& cur, double stroke_pressure)
{
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();
	if (!vulkan || !v2drender)
		return;

	// wx: m_use_pres ? m_pressure : 1.0, with the pressure of this drag segment
	double pressure = m_use_pres ? stroke_pressure : 1.0;

	vks::VulkanDevice* prim_dev = vulkan->devices[0];

	if (m_fbo_paint && (m_fbo_paint->w != nx || m_fbo_paint->h != ny))
	{
		m_fbo_paint.reset();
		m_tex_paint.reset();
	}
	if (!m_fbo_paint)
	{
		m_fbo_paint = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_paint->w = nx;
		m_fbo_paint->h = ny;
		m_fbo_paint->device = prim_dev;

		m_tex_paint = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_fbo_paint->addAttachment(m_tex_paint);
		m_clear_paint = true;   // fresh image: contents undefined
	}

	double px = double(cur.x() - prv.x());
	double py = double(cur.y() - prv.y());
	double dist = sqrt(px * px + py * py);
	double step = m_brush_radius1 * pressure * m_brush_spacing;
	int repeat = step > 0.0 ? int(dist / step + 0.5) : 0;
	double spx = (double)prv.x();
	double spy = (double)prv.y();
	if (repeat > 0)
	{
		px /= repeat;
		py /= repeat;
	}

	double x, y;
	double radius1 = m_brush_radius1;
	double radius2 = m_brush_radius2;
	int sel_mode = GetPaintMode();
	for (int i = 0; i <= repeat; i++)
	{
		x = spx + i * px;
		y = spy + i * py;
		switch (sel_mode)
		{
		case 3:
			radius1 = m_brush_radius2;
			break;
		case 4:
			radius1 = 0.0;
			break;
		case 8:
			radius2 = radius1;
			break;
		default:
			break;
		}

		Vulkan2dRender::V2DRenderParams params = v2drender->GetNextV2dRenderSemaphoreSettings();
		params.pipeline =
			v2drender->preparePipeline(
				IMG_SHDR_PAINT,
				V2DRENDER_BLEND_MAX,
				m_fbo_paint->attachments[0]->format,
				(int)m_fbo_paint->attachments.size(),
				0,
				m_fbo_paint->attachments[0]->is_swapchain_images);
		params.loc[0] = glm::vec4(float(x), float(y), float(radius1 * pressure), float(radius2 * pressure));
		params.loc[1] = glm::vec4((float)nx, (float)ny, 0.0f, 0.0f);
		params.clear = m_clear_paint;
		m_clear_paint = false;

		if (!m_fbo_paint->renderPass)
			m_fbo_paint->replaceRenderPass(params.pipeline.pass);
		v2drender->render(m_fbo_paint, params);
	}
}

//show the stroke buffer
void BrushTool::DisplayStroke()
{
	if (!m_tex_paint)
		return;
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();

	Vulkan2dRender::V2DRenderParams params = v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = vulkan->frameBuffers[vulkan->currentBuffer].get();
	params.pipeline =
		v2drender->preparePipeline(
			IMG_SHADER_TEXTURE_LOOKUP,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			(int)current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images);
	params.tex[0] = m_tex_paint.get();
	params.clear = false;
	Color bg = m_canvas->GetBackgroundColor();
	params.clearColor = { (float)bg.r(), (float)bg.g(), (float)bg.b() };

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	v2drender->render(vulkan->frameBuffers[vulkan->currentBuffer], params);
}

// VRenderVulkanView::GetTextColor (+ m_bg_color_inv from SetBackgroundColor)
Color BrushTool::GetTextColor() const
{
	Color bg = m_canvas ? m_canvas->GetBackgroundColor() : Color(0.0, 0.0, 0.0);
	Color inv;
	HSVColor bg_color(bg);
	if (bg_color.val() > 0.7 && bg_color.sat() > 0.7)
		inv = Color(HSVColor(bg_color.hue() + 180.0, 1.0, 1.0));
	else if (bg_color.val() > 0.7)
		inv = Color(0.0, 0.0, 0.0);
	else
		inv = Color(1.0, 1.0, 1.0);
	switch (m_text_color_mode)
	{
	case 0://background inverted
		return inv;
	case 1://background
		return bg;
	case 2://secondary color of current volume
	{
		VolumeData* vd = m_canvas ? m_canvas->GetCurrentVolume() : nullptr;
		return vd ? vd->GetMaskColor() : inv;
	}
	}
	return inv;
}

TextRenderer* BrushTool::GetTextRendererInst()
{
	if (m_ext_text_renderer)
		return m_ext_text_renderer;
	if (m_text_renderer)
		return m_text_renderer.get();
	if (m_text_renderer_failed || !m_canvas || !m_canvas->GetV2DRender())
		return nullptr;
	// wx VRenderVulkanView ctor: <resources>/Fonts/FreeSans.ttf
	QString font = QDir(FromVVD(vvd::GetResourcesDir())).filePath("Fonts/FreeSans.ttf");
	if (!QFile::exists(font))
	{
		m_text_renderer_failed = true;
		return nullptr;
	}
	m_text_renderer = std::make_unique<TextRenderer>(QFile::encodeName(font).toStdString(), m_canvas->GetV2DRender());
	m_text_renderer->SetSize(m_text_size);
	return m_text_renderer.get();
}

void BrushTool::SetTextSize(int size)
{
	if (size <= 0) return;
	m_text_size = size;
	if (m_text_renderer) m_text_renderer->SetSize(size);
}

// ---------------------------------------------------------------------------
// segmentation (VRenderVulkanView::Segment)
// ---------------------------------------------------------------------------

void BrushTool::PreloadMaskLevel(VolumeData* vd)
{
	if (!vd || !vd->isBrxml())
		return;
	if (!m_loader)
		m_loader = std::make_unique<VolumeLoader>();
	m_loader->SetMemoryLimitByte((long long)TextureRenderer::get_mainmem_buf_size() * 1024LL * 1024LL);
	m_loader->PreloadLevel(vd, vd->GetMaskLv(), true);
}

void BrushTool::Segment()
{
	VolumeSelector* sel = Sel();
	if (!sel || !m_canvas || !m_canvas->IsVulkanReady())
		return;

	const bool ptm = vvd::ptime::On();
	double pt0 = ptm ? vvd::ptime::NowMs() : 0.0;
	const double pt_start = pt0;
	//segmentation runs compute passes outside the per-frame slot cycle;
	//they reuse per-slot resources, so all in-flight frames must retire first
	if (std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan())
		for (auto dev : vulkan->devices)
			dev->WaitIdleAllFrameSlots();
	if (ptm) { double t = vvd::ptime::NowMs(); vvd::ptime::Log("Segment: frame slot wait %.1f ms", t - pt0); pt0 = t; }

	// wx: HandleCamera + object transforms -> m_mv_mat. The canvas does the
	// same each frame; the tool keeps the matrices of the last frame.
	glm::mat4 mv_mat = m_mv_mat;
	glm::mat4 proj_mat = m_proj_mat;
	glm::mat4 tex_mat = m_tex_mat;

	sel->Set2DMask(m_tex_paint);
	sel->Set2DWeight(m_canvas->GetFinalTex(), m_canvas->GetVolTex());
	//orthographic
	sel->SetOrthographic(!m_canvas->GetPerspective());
	sel->SetUseAbsoluteValue(m_use_absolute_value);

	if (sel->GetSelectGroup())
	{
		// wx Segment: every volume of the selected volume's group
		VolumeData* vd = sel->GetVolume();
		DataGroup* dgroup = vd ? m_canvas->GetParentGroup(vd) : nullptr;
		if (dgroup && dgroup->GetVolumeNum() > 0)
		{
			std::vector<VolumeData*> group;
			for (int gi = 0; gi < dgroup->GetVolumeNum(); gi++)
				group.push_back(dgroup->GetVolumeData(gi));
			for (VolumeData* tmp_vd : group)
			{
				if (!tmp_vd) continue;
				tmp_vd->SetMatrices(mv_mat, proj_mat, tex_mat);
				sel->SetVolume(tmp_vd);
				PreloadMaskLevel(tmp_vd);
				sel->Select(m_brush_radius2 - m_brush_radius1);
			}
			sel->SetVolume(vd);
		}
	}
	else if (sel->GetSelectBoth())
	{
		VolumeCalculator* cal = Calc();
		VolumeData* vd = cal ? cal->GetVolumeA() : nullptr;
		if (vd)
		{
			vd->SetMatrices(mv_mat, proj_mat, tex_mat);
			sel->SetVolume(vd);
			PreloadMaskLevel(vd);
			sel->Select(m_brush_radius2 - m_brush_radius1);
		}
		vd = cal ? cal->GetVolumeB() : nullptr;
		if (vd)
		{
			vd->SetMatrices(mv_mat, proj_mat, tex_mat);
			sel->SetVolume(vd);
			PreloadMaskLevel(vd);
			sel->Select(m_brush_radius2 - m_brush_radius1);
		}
	}
	else
	{
		VolumeData* vd = sel->GetVolume();
		if (!vd && m_canvas->GetCurrentVolume())
		{
			sel->SetVolume(m_canvas->GetCurrentVolume());
			vd = sel->GetVolume();
		}
		if (vd)
		{
			PreloadMaskLevel(vd);
			sel->Select(m_brush_radius2 - m_brush_radius1);
		}
	}

	if (ptm) { double t = vvd::ptime::NowMs(); vvd::ptime::Log("Segment: Select calls %.1f ms", t - pt0); pt0 = t; }
	// wx: TraceDlg auto-ID / manual assist (MainWindow -> TraceDialog), then
	// BrushToolDlg::GetSettings
	Q_EMIT segmented(sel->GetMode());
	Q_EMIT settingsChanged();
	if (ptm)
	{
		double t = vvd::ptime::NowMs();
		vvd::ptime::Log("Segment: settingsChanged (dialog refreshes) %.1f ms; Segment total %.1f ms", t - pt0, t - pt_start);
	}
}

// ---------------------------------------------------------------------------
// toolbar commands
// ---------------------------------------------------------------------------

// wx TreePanel::BrushClear -> DataTreeCtrl::BrushClear, then SetIntMode(4)
void BrushTool::BrushClear()
{
	VolumeData* vd = CurVolume();
	if (vd)
	{
		int int_mode = GetIntMode();
		int paint_mode = GetPaintMode();
		int brush_state = m_brush_state;
		if (VolumeCalculator* cal = Calc()) cal->SetVolumeA(vd);   // wx SetVolumeA
		if (VolumeSelector* sel = Sel()) sel->SetVolume(vd);
		SetPaintMode(6);
		Segment();
		if (m_ctx) m_ctx->RefreshView();
		SetPaintMode(paint_mode);
		m_brush_state = brush_state;
		SetIntMode(int_mode);
	}
	// wx SetIntMode(4): clear the paint buffer in the next frame
	m_clear_paint = true;
	if (m_int_mode == 2) m_int_mode = 4;
	else if (m_int_mode == 7) m_int_mode = 8;
	if (m_canvas) m_canvas->RefreshView();
}

namespace
{
	// wx DataTreeCtrl::BrushCreate / BrushCreateInv for a volume in NA mode:
	// up to three NA volumes of the group that have labels -> A, B, C
	bool SetNAOperands(RenderCanvas* canvas, VolumeData* vd, VolumeCalculator* cal, VolumeSelector* sel)
	{
		std::vector<VolumeData*> vols;
		DataGroup* group = canvas->GetParentGroup(vd);
		if (!group)
			return false;
		for (int j = 0; j < group->GetVolumeNum(); j++)
		{
			VolumeData* gvd = group->GetVolumeData(j);
			if (gvd && gvd->GetNAMode() && (gvd->GetLabel(false) || !gvd->GetSharedLabelName().IsEmpty()))
				vols.push_back(gvd);
		}
		if (vols.size() > 0) { cal->SetVolumeA(vols[0]); if (sel) sel->SetVolume(vols[0]); }
		if (vols.size() > 1) cal->SetVolumeB(vols[1]);
		if (vols.size() > 2) cal->SetVolumeC(vols[2]);
		return true;
	}
}

// wx TreePanel::BrushErase -> DataTreeCtrl::BrushCreateInv: Calculate(6)
void BrushTool::BrushErase()
{
	SetIntMode(1);
	Q_EMIT brushSelected(0);
	VolumeData* vd = CurVolume();
	VolumeCalculator* cal = Calc();
	if (!vd || !cal || !m_ctx)
		return;
	if (vd->GetNAMode())
	{
		if (SetNAOperands(m_canvas, vd, cal, Sel()))
			m_ctx->Calculate(11);
	}
	else
	{
		cal->SetVolumeA(vd);
		if (VolumeSelector* sel = Sel()) sel->SetVolume(vd);
		m_ctx->Calculate(6);
	}
}

// wx TreePanel::BrushCreate -> DataTreeCtrl::BrushCreate: Calculate(5)
void BrushTool::BrushCreate()
{
	SetIntMode(1);
	Q_EMIT brushSelected(0);
	VolumeData* vd = CurVolume();
	VolumeCalculator* cal = Calc();
	if (!vd || !cal || !m_ctx)
		return;
	if (vd->GetNAMode())
	{
		if (SetNAOperands(m_canvas, vd, cal, Sel()))
			m_ctx->Calculate(10);
	}
	else
	{
		cal->SetVolumeA(vd);
		if (VolumeSelector* sel = Sel()) sel->SetVolume(vd);
		m_ctx->Calculate(5);
	}
}

// wx BrushToolDlg::BrushUndo / BrushRedo
void BrushTool::BrushUndo()
{
	VolumeData* sel_vol = CurVolume();
	if (sel_vol && sel_vol->GetTexture())
	{
		sel_vol->GetTexture()->mask_undos_backward();
		if (sel_vol->GetVR())
			sel_vol->GetVR()->clear_tex_current_mask();
	}
	if (m_ctx) m_ctx->RefreshView();
	Q_EMIT settingsChanged();
}

void BrushTool::BrushRedo()
{
	VolumeData* sel_vol = CurVolume();
	if (sel_vol && sel_vol->GetTexture())
	{
		sel_vol->GetTexture()->mask_undos_forward();
		if (sel_vol->GetVR())
			sel_vol->GetVR()->clear_tex_current_mask();
	}
	if (m_ctx) m_ctx->RefreshView();
	Q_EMIT settingsChanged();
}

bool BrushTool::CanUndo() const
{
	VolumeData* vd = CurVolume();
	return vd && vd->GetTexture() && vd->GetTexture()->get_undo();
}

bool BrushTool::CanRedo() const
{
	VolumeData* vd = CurVolume();
	return vd && vd->GetTexture() && vd->GetTexture()->get_redo();
}

// ---------------------------------------------------------------------------
// brush properties (VRenderVulkanView)
// ---------------------------------------------------------------------------

//load default (VRenderVulkanView::LoadDefaultBrushSettings)
void BrushTool::LoadDefaultBrushSettings()
{
	VolumeSelector* sel = Sel();
	if (!sel)
		return;
	const VVDString name(L"default_brush_settings.dft");
	const VVDString dft = vvd::SettingsReadPath(name);
	vvd::IniFile fconfig;
	if (!vvd::FileExists(dft) || !fconfig.Load(dft))
		return;

	double val;
	int ival;
	bool bval;

	//brush properties
	if (fconfig.Read(L"brush_ini_thresh", &val))
		sel->SetBrushIniThresh(val);
	if (fconfig.Read(L"brush_gm_falloff", &val))
		sel->SetBrushGmFalloff(val);
	if (fconfig.Read(L"brush_scl_falloff", &val))
		sel->SetBrushSclFalloff(val);
	if (fconfig.Read(L"brush_scl_translate", &val))
	{
		sel->SetBrushSclTranslate(val);
		if (VolumeCalculator* cal = Calc()) cal->SetThreshold(val);
	}
	//auto thresh
	if (fconfig.Read(L"auto_thresh", &bval))
		sel->SetEstimateThreshold(bval);
	//edge detect
	if (fconfig.Read(L"edge_detect", &bval))
		sel->SetEdgeDetect(bval);
	//hidden removal
	if (fconfig.Read(L"hidden_removal", &bval))
		sel->SetHiddenRemoval(bval);
	//select group
	if (fconfig.Read(L"select_group", &bval))
		sel->SetSelectGroup(bval);
	//2d influence
	if (fconfig.Read(L"brush_2dinfl", &val))
		sel->SetW2d(val);
	//size 1
	if (fconfig.Read(L"brush_size1", &val) && val > 0.0)
		m_brush_radius1 = val;
	//size 2 link
	if (fconfig.Read(L"use_brush_size2", &bval))
	{
		m_use_brush_radius2 = bval;
		sel->SetUseBrushSize2(bval);
	}
	//size 2
	if (fconfig.Read(L"brush_size2", &val) && val > 0.0)
		m_brush_radius2 = val;
	//iterations
	if (fconfig.Read(L"brush_iters", &ival))
	{
		switch (ival)
		{
		case 1:
			sel->SetBrushIteration(BRUSH_TOOL_ITER_WEAK);
			break;
		case 2:
			sel->SetBrushIteration(BRUSH_TOOL_ITER_NORMAL);
			break;
		case 3:
			sel->SetBrushIteration(BRUSH_TOOL_ITER_STRONG);
			break;
		}
	}
	if (fconfig.Read(L"use_dslt", &bval))
		sel->SetUseDSLTBrush(bval);
	if (fconfig.Read(L"dslt_r", &val) && val > 0.0)
		sel->SetBrushDSLT_R((int)val);
	if (fconfig.Read(L"dslt_q", &val) && val > 0.0)
		sel->SetBrushDSLT_Q((int)val);
	if (fconfig.Read(L"dslt_c", &val))
		sel->SetBrushDSLT_C(val);
}

void BrushTool::SetUseBrushSize2(bool val)
{
	m_use_brush_radius2 = val;
	if (VolumeSelector* sel = Sel()) sel->SetUseBrushSize2(val);
	if (!val)
		m_brush_radius2 = m_brush_radius1;
}

void BrushTool::SetBrushSize(double size1, double size2)
{
	if (size1 > 0.0)
		m_brush_radius1 = size1;
	if (size2 > 0.0)
		m_brush_radius2 = size2;
	if (!m_use_brush_radius2)
		m_brush_radius2 = m_brush_radius1;
}

void BrushTool::SetBrushIteration(int num) { if (VolumeSelector* s = Sel()) s->SetBrushIteration(num); }
int BrushTool::GetBrushIteration() const { VolumeSelector* s = Sel(); return s ? s->GetBrushIteration() : BRUSH_TOOL_ITER_WEAK; }

//brush translate
void BrushTool::SetBrushSclTranslate(double val)
{
	if (VolumeSelector* s = Sel()) s->SetBrushSclTranslate(val);
	if (VolumeCalculator* c = Calc()) c->SetThreshold(val);
}
double BrushTool::GetBrushSclTranslate() const { VolumeSelector* s = Sel(); return s ? s->GetBrushSclTranslate() : 0.0; }

//gm falloff
void BrushTool::SetBrushGmFalloff(double val) { if (VolumeSelector* s = Sel()) s->SetBrushGmFalloff(val); }
double BrushTool::GetBrushGmFalloff() const { VolumeSelector* s = Sel(); return s ? s->GetBrushGmFalloff() : 0.0; }

void BrushTool::SetW2d(double val) { if (VolumeSelector* s = Sel()) s->SetW2d(val); }
double BrushTool::GetW2d() const { VolumeSelector* s = Sel(); return s ? s->GetW2d() : 0.0; }

void BrushTool::SetEdgeDetect(bool value) { if (VolumeSelector* s = Sel()) s->SetEdgeDetect(value); }
bool BrushTool::GetEdgeDetect() const { VolumeSelector* s = Sel(); return s ? s->GetEdgeDetect() : false; }

void BrushTool::SetHiddenRemoval(bool value) { if (VolumeSelector* s = Sel()) s->SetHiddenRemoval(value); }
bool BrushTool::GetHiddenRemoval() const { VolumeSelector* s = Sel(); return s ? s->GetHiddenRemoval() : false; }

void BrushTool::SetSelectGroup(bool value) { if (VolumeSelector* s = Sel()) s->SetSelectGroup(value); }
bool BrushTool::GetSelectGroup() const { VolumeSelector* s = Sel(); return s ? s->GetSelectGroup() : false; }

void BrushTool::SetEstimateThresh(bool value) { if (VolumeSelector* s = Sel()) s->SetEstimateThreshold(value); }
bool BrushTool::GetEstimateThresh() const { VolumeSelector* s = Sel(); return s ? s->GetEstimateThreshold() : false; }

void BrushTool::SetSelectBoth(bool value) { if (VolumeSelector* s = Sel()) s->SetSelectBoth(value); }
bool BrushTool::GetSelectBoth() const { VolumeSelector* s = Sel(); return s ? s->GetSelectBoth() : false; }

void BrushTool::SetUseDSLTBrush(bool value) { if (VolumeSelector* s = Sel()) s->SetUseDSLTBrush(value); }
bool BrushTool::GetUseDSLTBrush() const { VolumeSelector* s = Sel(); return s ? s->GetUseDSLTBrush() : false; }

void BrushTool::SetBrushDSLT_R(int rad) { if (VolumeSelector* s = Sel()) s->SetBrushDSLT_R(rad); }
int BrushTool::GetBrushDSLT_R() const { VolumeSelector* s = Sel(); return s ? s->GetBrushDSLT_R() : 0; }

void BrushTool::SetBrushDSLT_Q(int quality) { if (VolumeSelector* s = Sel()) s->SetBrushDSLT_Q(quality); }
int BrushTool::GetBrushDSLT_Q() const { VolumeSelector* s = Sel(); return s ? s->GetBrushDSLT_Q() : 0; }

void BrushTool::SetBrushDSLT_C(double c) { if (VolumeSelector* s = Sel()) s->SetBrushDSLT_C(c); }
double BrushTool::GetBrushDSLT_C() const { VolumeSelector* s = Sel(); return s ? s->GetBrushDSLT_C() : 0.0; }
