#include "RenderCanvas.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTabletEvent>
#include <QResizeEvent>
#include <QExposeEvent>
#include <cstdio>
#include <QElapsedTimer>
#include "CanvasTool.h"
#include <QImage>
#include <QDir>
#include <QMimeData>
#include <QDropEvent>
#include <QUrl>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QWindow>
#include <QVBoxLayout>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <cmath>
#include <stdexcept>
#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "VVulkan.h"
#include "Vulkan/Vulkan2dRender.h"
#include "Vulkan/VulkanDevice.hpp"
#include "FLIVR/TextureRenderer.h"
#include "TextRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/MeshRenderer.h"
#include "FLIVR/ImgShader.h"
#include "FLIVR/Transform.h"
#include "DataManager.h"
#include "Core/PaintTiming.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include "MetalCompatibility.h"
#elif defined(VK_USE_PLATFORM_XCB_KHR)
#include <xcb/xcb.h>
#endif

using namespace FLIVR;

namespace
{
	inline double deg2rad(double d) { return d * 3.14159265358979323846 / 180.0; }
}

// ---------------------------------------------------------------------------
// VulkanSurfaceWindow: the native surface. Events are handed to the canvas.
// ---------------------------------------------------------------------------

class VulkanSurfaceWindow : public QWindow
{
public:
	explicit VulkanSurfaceWindow(RenderCanvas* canvas) : m_canvas(canvas)
	{
		// Must be set before create(): on macOS Qt then backs the NSView with
		// a CAMetalLayer (MoltenVK needs one); elsewhere it just marks the
		// window as not raster-painted by Qt.
		// On macOS Qt uses its QMetalLayer subclass (presentsWithTransaction
		// + display lock, made for Qt's own Metal renderer) for VulkanSurface
		// as well as MetalSurface; QtMain.cpp sets QT_MTL_NO_TRANSACTION so a
		// plain CAMetalLayer is created, which is what MoltenVK presents into.
		setSurfaceType(QSurface::VulkanSurface);
	}
	void Detach() { m_canvas = nullptr; }

protected:
	bool event(QEvent* e) override
	{
		if (!m_canvas) return QWindow::event(e);
		switch (e->type())
		{
		// drag & drop of files onto the render view (wx: vrv->SetDropTarget(DnDFile))
		case QEvent::DragEnter:
		case QEvent::DragMove:
		{
			auto* de = static_cast<QDropEvent*>(e);
			if (de->mimeData() && de->mimeData()->hasUrls()) de->acceptProposedAction();
			else de->ignore();
			return true;
		}
		case QEvent::DragLeave:
			return true;
		case QEvent::Drop:
		{
			auto* de = static_cast<QDropEvent*>(e);
			if (de->mimeData() && de->mimeData()->hasUrls())
			{
				de->acceptProposedAction();
				Q_EMIT m_canvas->filesDropped(de->mimeData()->urls());
			}
			return true;
		}
		case QEvent::UpdateRequest:
			m_canvas->Render();
			return true;
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonDblClick:
			m_canvas->mousePressEvent(static_cast<QMouseEvent*>(e));
			return true;
		case QEvent::MouseButtonRelease:
			m_canvas->mouseReleaseEvent(static_cast<QMouseEvent*>(e));
			return true;
		case QEvent::MouseMove:
			m_canvas->mouseMoveEvent(static_cast<QMouseEvent*>(e));
			return true;
		case QEvent::Wheel:
			m_canvas->wheelEvent(static_cast<QWheelEvent*>(e));
			return true;
		case QEvent::TabletPress:
		case QEvent::TabletMove:
		case QEvent::TabletRelease:
			// pen pressure for the brush; the event is left unaccepted so
			// the pen still drives the view through its mouse events
			m_canvas->OnTabletEvent(static_cast<QTabletEvent*>(e));
			e->ignore();
			return false;
		case QEvent::KeyPress:
			m_canvas->keyPressEvent(static_cast<QKeyEvent*>(e));
			return e->isAccepted();
		case QEvent::KeyRelease:
			m_canvas->keyReleaseEvent(static_cast<QKeyEvent*>(e));
			return e->isAccepted();
		default:
			return QWindow::event(e);
		}
	}
	void exposeEvent(QExposeEvent*) override
	{
		// Do not render here. On macOS the expose event is delivered
		// synchronously from -[CALayer display], i.e. inside the CATransaction
		// commit on the main thread. Acquiring a swapchain drawable there
		// (which MoltenVK does lazily, on the first vkQueueSubmit that targets
		// the image) can block the commit while the drawables it is waiting
		// for are held by presents that in turn wait for the commit. Schedule
		// a frame instead; UpdateRequest arrives from the display link / timer
		// outside any transaction, and requestUpdate() coalesces repeats.
		if (m_canvas) m_canvas->OnExpose(isExposed());
	}
	void resizeEvent(QResizeEvent* e) override
	{
		QWindow::resizeEvent(e);
		if (m_canvas) m_canvas->OnSurfaceResized();
	}

private:
	RenderCanvas* m_canvas;
};

// ---------------------------------------------------------------------------

RenderCanvas::RenderCanvas(QWidget* parent)
	: QWidget(parent)
{
	m_window = new VulkanSurfaceWindow(this);
	m_container = QWidget::createWindowContainer(m_window, this);
	m_container->setFocusPolicy(Qt::StrongFocus);
	m_container->setMinimumSize(64, 64);

	auto* lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->addWidget(m_container);

	setFocusPolicy(Qt::StrongFocus);
	setFocusProxy(m_container);
	setMinimumSize(64, 64);
	RegisterCanvas(this, true);   // linked rotations (RenderCanvasCamera.cpp)
	CreateViewKeyFilter();        // wx OnIdle key polling (RenderCanvasCamera.cpp)
}

// ---------------------------------------------------------------------------
// interactive (reduced quality) period
// ---------------------------------------------------------------------------
int RenderCanvas::IntSettleMs()
{
	static const int ms = [] {
		bool ok = false;
		const int v = qEnvironmentVariableIntValue("VVD_INT_SETTLE_MS", &ok);
		return ok && v >= 0 ? v : 300;
	}();
	return ms;
}

void RenderCanvas::NoteInteraction()
{
	if (m_adaptive) m_interactive = true;
	if (m_adaptive_res) m_int_res = true;
	if (!m_interactive && !m_int_res)
		return;
	m_last_interaction_ms = QDateTime::currentMSecsSinceEpoch();
	if (!m_int_settle_timer)
	{
		m_int_settle_timer = new QTimer(this);
		m_int_settle_timer->setSingleShot(true);
		connect(m_int_settle_timer, &QTimer::timeout, this, &RenderCanvas::OnInteractionSettleTimer);
	}
	m_int_settle_timer->start(IntSettleMs());
}

bool RenderCanvas::InteractionSettled() const
{
	return m_last_interaction_ms < 0 ||
		QDateTime::currentMSecsSinceEpoch() - m_last_interaction_ms >= IntSettleMs();
}

// the input has been still for IntSettleMs: draw at full quality
void RenderCanvas::OnInteractionSettleTimer()
{
	if (!InteractionSettled())
	{
		const qint64 left = IntSettleMs() - (QDateTime::currentMSecsSinceEpoch() - m_last_interaction_ms);
		m_int_settle_timer->start(int(std::max<qint64>(1, left)));
		return;
	}
	if (!m_interactive && !m_int_res)
		return;
	m_interactive = false;
	m_int_res = false;
	m_refresh_start_loop = true;
	RequestRender();
}

RenderCanvas* RenderCanvas::s_loop_owner = nullptr;
RenderCanvas* RenderCanvas::s_key_canvas = nullptr;

// ---------------------------------------------------------------------------
// several canvases: surface + brick-loop state (see RenderCanvas.h)
// ---------------------------------------------------------------------------

void RenderCanvas::BindLoopState()
{
	if (s_loop_owner == this)
		return;
	// never in the middle of another canvas's frame
	if (s_loop_owner && s_loop_owner->m_rendering)
		return;
	if (s_loop_owner)
	{
		LoopState& o = s_loop_owner->m_loop_state;
		o.start_update_loop = TextureRenderer::start_update_loop_;
		o.done_update_loop = TextureRenderer::done_update_loop_;
		o.done_current_chan = TextureRenderer::done_current_chan_;
		o.total_brick_num = TextureRenderer::total_brick_num_;
		o.cur_brick_num = TextureRenderer::cur_brick_num_;
		o.cur_chan_brick_num = TextureRenderer::cur_chan_brick_num_;
		o.cur_tid_offset_multi = TextureRenderer::cur_tid_offset_multi_;
		o.clear_chan_buffer = TextureRenderer::clear_chan_buffer_;
		o.save_final_buffer = TextureRenderer::save_final_buffer_;
		o.valid_temp_final_buffer = TextureRenderer::valid_temp_final_buffer_;
		o.st_time = TextureRenderer::st_time_;
		o.cor_up_time = TextureRenderer::cor_up_time_;
		o.consumed_time = TextureRenderer::consumed_time_;
		o.interactive = TextureRenderer::interactive_;
		o.finished_bricks = TextureRenderer::finished_bricks_;
		const LoopState& n = m_loop_state;
		TextureRenderer::start_update_loop_ = n.start_update_loop;
		TextureRenderer::done_update_loop_ = n.done_update_loop;
		TextureRenderer::done_current_chan_ = n.done_current_chan;
		TextureRenderer::total_brick_num_ = n.total_brick_num;
		TextureRenderer::cur_brick_num_ = n.cur_brick_num;
		TextureRenderer::cur_chan_brick_num_ = n.cur_chan_brick_num;
		TextureRenderer::cur_tid_offset_multi_ = n.cur_tid_offset_multi;
		TextureRenderer::clear_chan_buffer_ = n.clear_chan_buffer;
		TextureRenderer::save_final_buffer_ = n.save_final_buffer;
		TextureRenderer::valid_temp_final_buffer_ = n.valid_temp_final_buffer;
		TextureRenderer::st_time_ = n.st_time;
		TextureRenderer::cor_up_time_ = n.cor_up_time;
		TextureRenderer::consumed_time_ = n.consumed_time;
		TextureRenderer::interactive_ = n.interactive;
		TextureRenderer::finished_bricks_ = n.finished_bricks;
	}
	// the first owner keeps the statics as they are (one canvas: no change)
	s_loop_owner = this;
}

bool RenderCanvas::MakeCurrent()
{
	if (s_loop_owner && s_loop_owner != this && s_loop_owner->m_rendering)
		return false;
	if (m_vulkan && m_surface_state)
		m_vulkan->activateSurface(static_cast<VVulkan::SurfaceState*>(m_surface_state));
	BindLoopState();
	return true;
}

bool RenderCanvas::IsLoopRunning() const
{
	if (!TextureRenderer::get_mem_swap()) return false;
	if (s_loop_owner == this || !s_loop_owner)
		return TextureRenderer::get_start_update_loop() && !TextureRenderer::get_done_update_loop();
	return m_loop_state.start_update_loop && !m_loop_state.done_update_loop;
}

RenderCanvas::~RenderCanvas()
{
	RegisterCanvas(this, false);
	if (s_key_canvas == this) s_key_canvas = nullptr;
	if (m_window) m_window->Detach();
	ShutdownStreaming();
	ReleaseCaptureResources();   // RenderCanvasCapture.cpp
	m_fbo.reset();
	m_tex.reset();
	m_fbo_wt2.reset();
	m_tex_wt2.reset();
	m_fbo_final.reset();
	m_tex_final.reset();
	m_dp_fbo_list.clear();
	m_dp_ctex_list.clear();
	m_dp_tex_list.clear();
	m_fbo_ol2.reset();
	m_tex_ol2.reset();
	// a view sharing another canvas's context gives its surface back
	if (m_secondary && m_vulkan && m_surface_state)
	{
		m_vulkan->removeSurface(static_cast<VVulkan::SurfaceState*>(m_surface_state));
		m_surface_state = nullptr;
	}
	if (s_loop_owner == this)
		s_loop_owner = nullptr;
	// The engine's static renderer state points at the first canvas's
	// context; the wx view has the same lifetime coupling. Volumes are owned
	// by DataManager.
}

QSize RenderCanvas::DeviceSize() const
{
	// The wx canvas hands logical (point) sizes to the swapchain as well and
	// lets the platform surface scale; keep parity.
	QSize s = m_window ? m_window->size() : QSize();
	return s.isEmpty() ? size() : s;
}

// RenderCanvas::RequestRender (wx RefreshGL with start_loop) lives in
// RenderCanvasStreaming.cpp with the rest of the update loop.

// one more frame, no loop restart (QWindow::requestUpdate coalesces)
void RenderCanvas::RequestFrame()
{
	if (m_window) m_window->requestUpdate();
}

// ---------------------------------------------------------------------------
// Vulkan bring-up (mirrors VRenderVulkanView::InitVulkan)
// ---------------------------------------------------------------------------

bool RenderCanvas::EnsureVulkan()
{
	if (m_vulkan) return true;
	if (m_init_failed) return false;
	if (!m_window->handle())
		m_window->create();   // native window (NSView / HWND / xcb window)
	// Data loaded before the first expose (files on the command line, a drop
	// right at start-up) would bring Vulkan up on a layer the platform has not
	// attached yet; on macOS that swapchain then crashes in the first
	// vkAcquireNextImageKHR. Let the window get exposed first.
	if (isVisible() && !m_window->isExposed())
	{
		QElapsedTimer t;
		t.start();
		while (!m_window->isExposed() && t.elapsed() < 3000)
			QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
	}
	return InitVulkan();
}

bool RenderCanvas::InitVulkan()
{
	if (m_vulkan) return true;
	WId wid = m_window->winId();
	if (!wid) return false;

	int gpu = -1;   // default device; the wx build reads this from the settings file
	// a further render view: share the first canvas's context, add a surface
	{
		std::shared_ptr<VVulkan> shared = TextureRenderer::get_vulkan();
		bool owner_alive = false;
		for (RenderCanvas* c : AllCanvases())
			if (c != this && c->m_vulkan && c->m_vulkan == shared && !c->m_secondary) owner_alive = true;
		if (shared && owner_alive)
		{
			try
			{
				VVulkan::SurfaceState* prev = shared->activeSurface();
				VVulkan::SurfaceState* st = shared->beginSurface();
#if defined(_WIN32)
				shared->setWindow((HWND)wid, GetModuleHandle(NULL));
#elif defined(__APPLE__)
				void* metal_layer = metalLayerForView((void*)wid);
				if (!metal_layer)
				{
					describeViewLayer("RenderCanvas", (void*)wid);
					shared->removeSurface(st);
					shared->activateSurface(prev);
					throw std::runtime_error("no CAMetalLayer behind the Qt render window");
				}
				shared->setWindow(metal_layer);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
				xcb_connection_t* c = xcb_connect(NULL, NULL);
				if (xcb_connection_has_error(c) > 0)
				{
					shared->removeSurface(st);
					shared->activateSurface(prev);
					throw std::runtime_error("cannot connect to XCB");
				}
				shared->setWindow((xcb_window_t)wid, c);
#endif
				QSize s = DeviceSize();
				shared->finishSurface(std::max(1, s.width()), std::max(1, s.height()));
				shared->activateSurface(prev);
				m_vulkan = shared;
				m_v2drender = TextureRenderer::get_vulkan2drender();
				m_surface_state = st;
				m_secondary = true;
			}
			catch (const std::exception& e)
			{
				qCritical("RenderCanvas: Vulkan surface for a further view failed: %s", e.what());
				m_vulkan.reset();
				m_v2drender.reset();
				m_init_failed = true;
				return false;
			}
			Q_EMIT vulkanReady();
			return true;
		}
	}
	try
	{
		m_vulkan = std::make_shared<VVulkan>();
#if defined(_WIN32)
		m_vulkan->initVulkan(gpu);
		m_vulkan->setWindow((HWND)wid, GetModuleHandle(NULL));
#elif defined(__APPLE__)
		m_vulkan->initVulkan(gpu);
		// Qt owns the view's layer tree (a QContainerLayer wrapping the
		// CAMetalLayer since Qt 6.10), so do not swap layers like the wx view
		// does; hand MoltenVK the CAMetalLayer itself.
		void* metal_layer = metalLayerForView((void*)wid);
		if (!metal_layer)
		{
			describeViewLayer("RenderCanvas", (void*)wid);
			throw std::runtime_error("no CAMetalLayer behind the Qt render window");
		}
		m_vulkan->setWindow(metal_layer);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		// X11/xcb. Wayland needs the wl_surface of the QWindow through the
		// platform native interface; until that is wired up, run Qt with
		// QT_QPA_PLATFORM=xcb (XWayland) on Wayland desktops.
		m_vulkan->initVulkan(VVulkan::PLATFORM_X11, gpu);
		xcb_connection_t* c = xcb_connect(NULL, NULL);
		if (xcb_connection_has_error(c) > 0)
		{
			qWarning("RenderCanvas: cannot connect to XCB");
			m_vulkan.reset();
			m_init_failed = true;
			return false;
		}
		m_vulkan->setWindow((xcb_window_t)wid, c);
#else
#error "RenderCanvas: unsupported platform"
#endif
		m_vulkan->prepare();
		m_surface_state = m_vulkan->primarySurface();
		m_v2drender = std::make_shared<Vulkan2dRender>(m_vulkan);
		TextureRenderer::set_vulkan(m_vulkan, m_v2drender);
		m_vulkan->ResetRenderSemaphores();
		VolumeRenderer::init();
		MeshRenderer::init(m_vulkan);

		// Renderer settings the wx frame takes from SettingDlg; use its
		// defaults (no streaming, 1 GB texture budget) for now.
		TextureRenderer::set_streaming(false);
		TextureRenderer::set_use_mem_limit(true);
		TextureRenderer::set_mem_limit(1000.0);
		TextureRenderer::set_available_mem(1000.0);
		TextureRenderer::set_large_data_size(1000.0);
		TextureRenderer::set_force_brick_size(128);
		TextureRenderer::set_up_time(100);
		TextureRenderer::set_update_order(0);

		QSize s = DeviceSize();
		m_vulkan->setSize(std::max(1, s.width()), std::max(1, s.height()));
	}
	catch (const std::exception& e)
	{
		qCritical("RenderCanvas: Vulkan initialisation failed: %s", e.what());
		m_vulkan.reset();
		m_v2drender.reset();
		m_init_failed = true;
		return false;
	}
	Q_EMIT vulkanReady();
	return true;
}

// ---------------------------------------------------------------------------
// scene content
// ---------------------------------------------------------------------------

// wx: VRenderFrame::OnSelection -- the previously selected mesh loses its
// bounding box, the newly selected one gets it
void RenderCanvas::SetCurrentMesh(MeshData* md)
{
	if (m_cur_mesh && m_cur_mesh != md &&
		std::find(m_meshes.begin(), m_meshes.end(), m_cur_mesh) != m_meshes.end())
		m_cur_mesh->SetDrawBounds(false);
	m_cur_mesh = md;
	if (md) md->SetDrawBounds(true);
	RequestRender();
}

// mirrors VRenderVulkanView::InitView(INIT_BOUNDS | INIT_CENTER | INIT_TRANSL ...)
void RenderCanvas::ComputeBounds()
{
	bool had_bounds = m_bounds.valid();
	m_bounds.reset();
	for (VolumeData* vd : PopVolumeList())   // wx: m_vd_pop_list
	{
		if (!vd || !vd->GetTexture()) continue;
		Transform* tform = vd->GetTexture()->transform();
		if (!tform) continue;
		Transform tform_copy;
		double mvmat[16];
		tform->get_trans(mvmat);
		std::swap(mvmat[3], mvmat[12]);
		std::swap(mvmat[7], mvmat[13]);
		std::swap(mvmat[11], mvmat[14]);
		tform_copy.set(mvmat);
		const Point p[8] = {
			Point(0.0, 0.0, 0.0), Point(1.0, 0.0, 0.0), Point(1.0, 1.0, 0.0), Point(0.0, 1.0, 0.0),
			Point(0.0, 0.0, 1.0), Point(1.0, 0.0, 1.0), Point(1.0, 1.0, 1.0), Point(0.0, 1.0, 1.0) };
		for (int j = 0; j < 8; ++j)
			m_bounds.extend(tform_copy.project(p[j]));
	}
	PopMeshList();
	for (MeshData* md : m_md_pop_list)
		m_bounds.extend(md->GetBounds());
	if (m_bounds.valid())
	{
		Vector diag = m_bounds.diagonal();
		m_radius = std::sqrt(diag.x() * diag.x() + diag.y() * diag.y()) / 2.0;
		if (m_radius <= 0.0) m_radius = 5.0;
		m_near_clip = m_radius / 1000.0;
		m_far_clip = m_radius * 100.0;
		m_obj_ctrx = (m_bounds.min().x() + m_bounds.max().x()) / 2.0;
		m_obj_ctry = (m_bounds.min().y() + m_bounds.max().y()) / 2.0;
		m_obj_ctrz = (m_bounds.min().z() + m_bounds.max().z()) / 2.0;
		if (!had_bounds)
		{
			if (m_use_dft_settings)
			{
				// wx InitView(INIT_TRANSL | INIT_ROTATE) with default view
				// settings loaded: keep the default zoom and rotation
				m_distance = m_radius / std::tan(deg2rad(m_aov / 2.0));
				m_init_dist = m_distance;
				m_obj_transx = m_obj_transy = m_obj_transz = 0.0;
				UpdateCameraFromQ();
				Q_EMIT zoomChanged(m_scale_factor);
			}
			else
				ResetView();
		}
		else
			m_distance = m_radius / std::tan(deg2rad(m_aov / 2.0)) / m_scale_factor;
	}
	// wx InitView: md->SetBounds(m_bounds) for every mesh, then (the clipping
	// view always exists in the wx frame) md->RecalcBounds(), which restores
	// each mesh's own geometry bounds/centre -- the net effect is RecalcBounds.
	for (MeshData* md : m_md_pop_list)
		md->RecalcBounds();
	// wx InitView tail: clipping planes follow the new bounds
	CalcAndSetCombinedClippingPlanes();
	A2QSync();
}

double RenderCanvas::GetAvailableGraphicsMemory(int device_id) const
{
	if (m_vulkan && device_id >= 0 && device_id < (int)m_vulkan->devices.size())
		return m_vulkan->devices[device_id]->available_mem;
	return 0.0;
}

void RenderCanvas::ResetView()
{
	m_scale_factor = 1.0;
	m_distance = m_radius / std::tan(deg2rad(m_aov / 2.0));
	m_init_dist = m_distance;   // wx InitView(INIT_TRANSL)
	m_transx = 0.0; m_transy = 0.0; m_transz = m_distance;
	m_obj_transx = m_obj_transy = m_obj_transz = 0.0;
	m_obj_rotx = m_obj_roty = m_obj_rotz = 0.0;
	m_rotx = m_roty = m_rotz = 0.0;
	m_q = FLIVR::Quaternion();
	UpdateCameraFromQ();
	Q_EMIT rotationChanged(0.0, 0.0, 0.0);
	Q_EMIT zoomChanged(m_scale_factor);
	RequestRender();
}

void RenderCanvas::SetPerspective(bool persp) { m_persp = persp; RequestRender(); }
void RenderCanvas::SetAov(double aov)
{
	m_persp = aov > 10.0;
	if (m_persp) m_aov = aov;
	RequestRender();
}

void RenderCanvas::SetBackgroundColor(const Color& c) { m_bg_color = c; RequestRender(); }

namespace
{
	double Wrap360(double a)
	{
		if (a > 360.0) a -= 360.0;
		if (a < 0.0) a += 360.0;
		return a;
	}
}

// VRenderVulkanView::SetRotations
void RenderCanvas::SetRotations(double rx, double ry, double rz, bool link_update)
{
	m_rotx = Wrap360(rx); m_roty = Wrap360(ry); m_rotz = Wrap360(rz);
	A2QSync();   // m_q from the angles + clipping planes (RenderCanvasCamera.cpp)
	UpdateCameraFromQ();
	Q_EMIT rotationChanged(m_rotx, m_roty, m_rotz);
	if (m_linked_rot && link_update)
		PropagateLinkedRotation();
	RequestRender();
}

void RenderCanvas::UpdateCameraFromQ()
{
	FLIVR::Quaternion cam_pos(0.0, 0.0, m_distance, 0.0);
	FLIVR::Quaternion cam_pos2 = (-m_q) * cam_pos * m_q;
	m_transx = cam_pos2.x;
	m_transy = cam_pos2.y;
	m_transz = cam_pos2.z;
	FLIVR::Quaternion up(0.0, 1.0, 0.0, 0.0);
	FLIVR::Quaternion up2 = (-m_q) * up * m_q;
	m_up = Vector(up2.x, up2.y, up2.z);
}

void RenderCanvas::Q2A()
{
	m_q.ToEuler(m_rotx, m_roty, m_rotz);
	m_rotx = Wrap360(m_rotx); m_roty = Wrap360(m_roty); m_rotz = Wrap360(m_rotz);
}

// VRenderVulkanView::Trackball
FLIVR::Quaternion RenderCanvas::Trackball(int p1x, int p1y, int p2x, int p2y)
{
	FLIVR::Quaternion q;
	if (p1x == p2x && p1y == p2y)
		return q;
	if (m_rot_lock && std::abs(p2x - p1x) < 50 && std::abs(p2y - p1y) < 50)
		return q;
	Vector a(p2y - p1y, p2x - p1x, 0.0);
	double phi = a.length() / 3.0;
	a.normalize();
	FLIVR::Quaternion q_a(a);
	FLIVR::Quaternion q_a2 = (-m_q) * q_a * m_q;
	a = Vector(q_a2.x, q_a2.y, q_a2.z);
	a.normalize();
	q = FLIVR::Quaternion(phi, a);
	q.Normalize();
	if (m_rot_lock)
	{
		double rotx, roty, rotz;
		q.ToEuler(rotx, roty, rotz);
		rotx = int(rotx / 45.0) * 45.0;
		roty = int(roty / 45.0) * 45.0;
		rotz = int(rotz / 45.0) * 45.0;
		q.FromEuler(rotx, roty, rotz);
	}
	return q;
}

// Virtual sphere rotation (Settings "Virtual sphere"). The drag
// points are put on a sphere of radius R centred in the view (y down, z to
// the viewer, the frame Trackball uses); outside the sphere they lie on its
// rim, so a drag around the centre there turns the data about the view axis
// only. The in-plane part runs at the Trackball speed (len / 3 degrees per
// pixel at the centre); the roll follows the mouse angle 1:1, like a knob.
FLIVR::Quaternion RenderCanvas::SphereTrackball(const QPoint& from, const QPoint& to, int w, int h)
{
	FLIVR::Quaternion q;
	if (from == to)
		return q;
	const double R = 0.45 * std::max(1, std::min(w, h));
	auto on_sphere = [&](const QPoint& p) {
		const double x = (p.x() - 0.5 * w) / R, y = (p.y() - 0.5 * h) / R;
		const double d2 = x * x + y * y;
		if (d2 >= 1.0)
		{
			const double d = std::sqrt(d2);
			return Vector(x / d, y / d, 0.0);
		}
		return Vector(x, y, std::sqrt(1.0 - d2));
	};
	const Vector v1 = on_sphere(from), v2 = on_sphere(to);
	Vector axis = Cross(v1, v2);
	const double s = axis.length();
	if (s < 1e-12)
		return q;
	const double theta = std::atan2(s, std::max(-1.0, std::min(1.0, Dot(v1, v2))));
	axis /= s;
	// rotation vector (degrees): in-plane part scaled to the wx speed
	const double g = R / 3.0;   // = (R * pi/180) / 3 per radian, in degrees
	const double deg = 180.0 / PI;
	Vector rv(axis.x() * theta * g, axis.y() * theta * g, axis.z() * theta * deg);
	const double phi = rv.length();
	if (phi < 1e-9)
		return q;
	Vector a = rv / phi;
	// view frame -> object frame (as in Trackball)
	FLIVR::Quaternion q_a(a);
	FLIVR::Quaternion q_a2 = (-m_q) * q_a * m_q;
	a = Vector(q_a2.x, q_a2.y, q_a2.z);
	a.normalize();
	q = FLIVR::Quaternion(phi, a);
	q.Normalize();
	return q;
}

CanvasTool* RenderCanvas::ActiveTool() const
{
	for (CanvasTool* t : m_tools)
		if (t && t->IsActive()) return t;
	return nullptr;
}

void RenderCanvas::SetScaleFactor(double s)
{
	m_scale_factor = std::clamp(s, 0.01, 100.0);
	Q_EMIT zoomChanged(m_scale_factor);
	RequestRender();
}

// ---------------------------------------------------------------------------
// camera (verbatim logic from VRenderVulkanView::HandleProjection/HandleCamera)
// ---------------------------------------------------------------------------

void RenderCanvas::HandleProjection(int nx, int ny)
{
	// wx: if (m_tile_rendering) HandleProjection(m_capture_resx, m_capture_resy)
	if (m_tile_rendering) { nx = m_capture_resx; ny = m_capture_resy; }
	double aspect = (double)nx / (double)ny;
	if (!m_free)
		m_distance = m_radius / std::tan(deg2rad(m_aov / 2.0)) / m_scale_factor;
	else
		m_scale_factor = FreeScaleFactor();   // RenderCanvasCamera.cpp
	if (aspect > 1.0)
	{
		m_ortho_left = -m_radius * aspect / m_scale_factor;
		m_ortho_right = -m_ortho_left;
		m_ortho_bottom = -m_radius / m_scale_factor;
		m_ortho_top = -m_ortho_bottom;
	}
	else
	{
		m_ortho_left = -m_radius / m_scale_factor;
		m_ortho_right = -m_ortho_left;
		m_ortho_bottom = -m_radius / aspect / m_scale_factor;
		m_ortho_top = -m_ortho_bottom;
	}
	if (m_persp)
		m_proj_mat = glm::perspective(glm::radians((float)m_aov), (float)aspect, (float)m_near_clip, (float)m_far_clip);
	else
		m_proj_mat = glm::ortho((float)m_ortho_left, (float)m_ortho_right, (float)m_ortho_bottom, (float)m_ortho_top,
			(float)(-m_far_clip / 100.0), (float)m_far_clip);
	if (m_tile_rendering)
		ApplyTileProjection(nx, ny);   // RenderCanvasCapture.cpp
}

void RenderCanvas::HandleCamera()
{
	Vector pos(m_transx, m_transy, m_transz);
	pos.normalize();
	if (m_free)
		pos *= m_radius * 0.01;
	else
		pos *= m_distance;
	m_transx = pos.x(); m_transy = pos.y(); m_transz = pos.z();
	if (m_free)
		m_mv_mat = glm::lookAt(glm::vec3(m_transx + m_ctrx, m_transy + m_ctry, m_transz + m_ctrz),
			glm::vec3(m_ctrx, m_ctry, m_ctrz), glm::vec3(m_up.x(), m_up.y(), m_up.z()));
	else
		m_mv_mat = glm::lookAt(glm::vec3(m_transx, m_transy, m_transz), glm::vec3(0.0f),
			glm::vec3(m_up.x(), m_up.y(), m_up.z()));
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

void RenderCanvas::PrepBuffers(int nx, int ny)
{
	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (m_fbo && (m_fbo->w != nx || m_fbo->h != ny)) { m_fbo.reset(); m_tex.reset(); }
	if (m_fbo_final && (m_fbo_final->w != nx || m_fbo_final->h != ny)) { m_fbo_final.reset(); m_tex_final.reset(); }
	if (!m_fbo)
	{
		m_fbo = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo->w = nx; m_fbo->h = ny; m_fbo->device = prim_dev;
		m_tex = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_fbo->addAttachment(m_tex);
	}
	if (!m_fbo_final)
	{
		m_fbo_final = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_final->w = nx; m_fbo_final->h = ny; m_fbo_final->device = prim_dev;
		m_tex_final = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_fbo_final->addAttachment(m_tex_final);
	}
}

void RenderCanvas::ClearScreen()
{
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	if (!current_fbo) return;
	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	if (!current_fbo->renderPass)
	{
		params.pipeline = m_v2drender->preparePipeline(IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR, V2DRENDER_BLEND_OVER,
			current_fbo->attachments[0]->format, (int)current_fbo->attachments.size(), 0,
			current_fbo->attachments[0]->is_swapchain_images);
		current_fbo->replaceRenderPass(params.pipeline.pass);
	}
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
	m_v2drender->clear(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

// verbatim from VRenderVulkanView::DrawMeshes (no mem-swap progress, no
// capture)
bool RenderCanvas::DrawMeshes(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, int peel,
	const std::shared_ptr<vks::VTexture> depth_tex)
{
	// wx: layer order, hidden mesh groups skipped (= PopMeshList)
	for (MeshData* md : GetPopMeshList())
	{
		if (!md || !md->GetDisp())
			continue;
		md->SetMatrices(m_mv_mat, m_proj_mat);
		md->SetFog(m_use_fog && m_use_fog_mesh, m_fog_intensity, m_fog_start, m_fog_end, m_bg_color);
		md->SetDepthTex(depth_tex);
		md->SetDevice(m_vulkan->devices[0]);
		md->Draw(framebuf, clear_framebuf, peel);
		clear_framebuf = false;
	}
	return true;
}

// VRenderVulkanView::DrawAnnotations, case 4 (annotation layer) with a mesh
// (Annotations::GetMesh: the "Mesh:" line of the annotation file): the mesh
// is drawn alone into m_anno_fbo (no depth texture, no bounds) and ADDed to
// the swapchain image with the layer's alpha as brightness
void RenderCanvas::DrawAnnotationMesh(Annotations* ann, int nx, int ny)
{
	if (!ann || !m_vulkan || !m_v2drender)
		return;
	MeshData* md = ann->GetMesh();
	if (!md)
		return;
	vks::VulkanDevice* prim_dev = m_vulkan->devices[0];
	if (m_anno_fbo && (m_anno_fbo->w != nx || m_anno_fbo->h != ny))
	{
		m_anno_fbo.reset();
		m_anno_tex.reset();
		m_anno_dp_tex.reset();
	}
	if (!m_anno_fbo)
	{
		m_anno_fbo = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_anno_fbo->w = nx;
		m_anno_fbo->h = ny;
		m_anno_fbo->device = prim_dev;

		m_anno_tex = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_anno_fbo->addAttachment(m_anno_tex);

		m_anno_dp_tex = prim_dev->GenTexture2D(
			m_vulkan->depthFormat,
			VK_FILTER_LINEAR,
			nx, ny,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		);
		m_anno_fbo->addAttachment(m_anno_dp_tex);
	}

	md->SetDepthTex(nullptr);
	md->SetMatrices(m_mv_mat, m_proj_mat);
	md->SetFog(m_use_fog && m_use_fog_mesh, m_fog_intensity, m_fog_start, m_fog_end, m_bg_color);
	md->SetDevice(m_vulkan->devices[0]);
	md->SetDrawBounds(false);
	md->Draw(m_anno_fbo, true, 0);

	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_BRIGHTNESS_CONTRAST,
			V2DRENDER_BLEND_ADD,
			current_fbo->attachments[0]->format,
			(int)current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images);
	params.loc[0] = glm::vec4(1.0f);
	params.loc[1] = glm::vec4((float)ann->GetAlpha(), (float)ann->GetAlpha(), (float)ann->GetAlpha(), 1.0f);
	params.tex[0] = m_anno_tex.get();
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b() };
	m_frame_clear = false;
	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);
	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

// The mesh block at the top of VRenderVulkanView::DrawVolumes: render the
// visible meshes into the depth-peeling layers (colour + depth each), then
// composite the layers back to front into the final buffer, then the mesh
// shadow. Volumes are added on top afterwards (composite mode; as in the wx
// view the single-volume renderer does not read the mesh depth there).
void RenderCanvas::DrawMeshLayers(int nx, int ny)
{
	vks::VulkanDevice* prim_dev = m_vulkan->devices[0];

	bool mdtrans = false;
	for (MeshData* md : m_md_pop_list)
	{
		Color amb, diff, spec;
		double shine, alpha;
		md->GetMaterial(amb, diff, spec, shine, alpha);
		if (alpha < 1.0) mdtrans = true;
	}
	int peeling_layers = mdtrans ? m_peeling_layers : 1;

	// wx: m_resize -> drop the layer buffers
	if (!m_dp_fbo_list.empty() && m_dp_fbo_list[0] &&
		(m_dp_fbo_list[0]->w != nx || m_dp_fbo_list[0]->h != ny))
	{
		m_dp_fbo_list.clear();
		m_dp_ctex_list.clear();
		m_dp_tex_list.clear();
	}
	// wx: StartLoopUpdate(reset_peeling_layer = true) every frame
	m_finished_peeling_layer = 0;

	if ((int)m_dp_fbo_list.size() < peeling_layers)
	{
		m_dp_fbo_list.resize(peeling_layers);
		m_dp_ctex_list.resize(peeling_layers);
		m_dp_tex_list.resize(peeling_layers);
	}

	for (int i = m_finished_peeling_layer; i < peeling_layers; i++)
	{
		if (!m_dp_fbo_list[i])
		{
			m_dp_fbo_list[i] = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			m_dp_fbo_list[i]->w = nx;
			m_dp_fbo_list[i]->h = ny;
			m_dp_fbo_list[i]->device = prim_dev;

			m_dp_ctex_list[i] = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
			m_dp_fbo_list[i]->addAttachment(m_dp_ctex_list[i]);

			m_dp_tex_list[i] = prim_dev->GenTexture2D(
				m_vulkan->depthFormat,
				VK_FILTER_LINEAR,
				nx, ny,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			);
			m_dp_fbo_list[i]->addAttachment(m_dp_tex_list[i]);
		}

		bool finished = false;
		if (i == 0)
			finished = DrawMeshes(m_dp_fbo_list[i], true, 0);
		else
			finished = DrawMeshes(m_dp_fbo_list[i], true, 1, m_dp_tex_list[i - 1]);

		if (finished)
			m_finished_peeling_layer = i + 1;
		else
			break;
	}

	// combine result images of mesh renderer
	std::vector<Vulkan2dRender::V2DRenderParams> v2drender_params;
	Vulkan2dRender::V2dPipeline pl =
		m_v2drender->preparePipeline(
			IMG_SHADER_TEXTURE_LOOKUP_BLEND,
			V2DRENDER_BLEND_OVER,
			VK_FORMAT_R32G32B32A32_SFLOAT,
			1,
			0,
			m_fbo_final->attachments[0]->is_swapchain_images);
	int layers = m_finished_peeling_layer - 1;
	if (layers < 0) layers = 0;
	for (int i = layers; i >= 0; i--)
	{
		Vulkan2dRender::V2DRenderParams param;
		param.pipeline = pl;
		param.clear = false;
		param.tex[0] = m_dp_ctex_list[i].get();
		v2drender_params.push_back(param);
	}
	v2drender_params[0].clear = m_clear_final_buffer;
	m_clear_final_buffer = false;
	vks::VulkanSemaphoreSettings sem = prim_dev->GetNextRenderSemaphoreSettings();
	v2drender_params[0].waitSemaphores = sem.waitSemaphores;
	v2drender_params[0].waitSemaphoreCount = sem.waitSemaphoreCount;
	v2drender_params[0].signalSemaphores = sem.signalSemaphores;
	v2drender_params[0].signalSemaphoreCount = sem.signalSemaphoreCount;
	if (!m_fbo_final->renderPass)
		m_fbo_final->replaceRenderPass(pl.pass);
	m_v2drender->seq_render(m_fbo_final, v2drender_params.data(), (int)v2drender_params.size());

	if (m_md_pop_list[0]->GetShadow())
	{
		double darkness = 0.0;
		m_md_pop_list[0]->GetShadowParams(darkness);
		DrawOLShadowsMesh(m_dp_tex_list[0], (float)darkness, nx, ny);
	}
}

// VRenderVulkanView::DrawOLShadowsMesh (shadow direction: SettingDlg
// GetShadowDir, SetShadowDir)
void RenderCanvas::DrawOLShadowsMesh(const std::shared_ptr<vks::VTexture>& tex_depth, double darkness, int nx, int ny)
{
	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;

	// generate gradient map from depth map
	if (m_fbo_ol2 && (m_fbo_ol2->w != nx || m_fbo_ol2->h != ny))
	{
		m_fbo_ol2.reset();
		m_tex_ol2.reset();
	}
	if (!m_fbo_ol2)
	{
		m_fbo_ol2 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_ol2->w = nx;
		m_fbo_ol2->h = ny;
		m_fbo_ol2->device = prim_dev;

		m_tex_ol2 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_fbo_ol2->addAttachment(m_tex_ol2);
	}

	Vulkan2dRender::V2DRenderParams params_ol2 = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	params_ol2.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DEPTH_TO_GRADIENT,
			V2DRENDER_BLEND_DISABLE,
			m_fbo_ol2->attachments[0]->format,
			(int)m_fbo_ol2->attachments.size(),
			0,
			m_fbo_ol2->attachments[0]->is_swapchain_images);
	params_ol2.tex[0] = tex_depth.get();

	// SettingDlg GetShadowDir (SetShadowDir)
	const double dir_x = m_shadow_dir_x, dir_y = m_shadow_dir_y;
	params_ol2.loc[0] = { 1.0f / nx, 1.0f / ny, m_persp ? 2e10f : 1e6f, 0.0f };
	params_ol2.loc[1] = { (float)dir_x, (float)dir_y, 0.0f, 0.0f };

	params_ol2.clear = true;
	params_ol2.clearColor = { 1.0, 1.0, 1.0, 1.0 };

	if (!m_fbo_ol2->renderPass)
		m_fbo_ol2->replaceRenderPass(params_ol2.pipeline.pass);
	m_v2drender->render(m_fbo_ol2, params_ol2);

	// draw shadow. The wx view submits this one without taking a slot in the
	// render-semaphore chain; take one here so it is ordered after the
	// gradient pass above and before the volume passes that follow.
	Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	params_final.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_GRADIENT_TO_SHADOW_MESH,
			V2DRENDER_BLEND_SHADE_SHADOW,
			m_fbo_final->attachments[0]->format,
			(int)m_fbo_final->attachments.size(),
			0,
			m_fbo_final->attachments[0]->is_swapchain_images);
	params_final.tex[0] = m_tex_ol2.get();
	params_final.tex[1] = tex_depth.get();
	params_final.tex[2] = m_tex_final.get();	// same as wx (reads the target; see TODO there)
	params_final.loc[0] = { 1.0f / nx, 1.0f / ny, (float)std::max(m_scale_factor, 1.0), 0.0f };
	params_final.loc[1] = { (float)darkness, 0.0f, 0.0f, 0.0f };
	params_final.clear = m_clear_final_buffer;
	m_clear_final_buffer = false;

	if (!m_fbo_final->renderPass)
		m_fbo_final->replaceRenderPass(params_final.pipeline.pass);
	m_v2drender->render(m_fbo_final, params_final);
}

// composite mode of VRenderVulkanView::DrawVolumesComp / DrawOVER
void RenderCanvas::DrawVolumes(int nx, int ny)
{
	// Layered / Depth / Composite (RenderCanvasModes.cpp, wx DrawVolumes
	// "handle intermixing modes"): each volume is drawn into m_fbo
	// (accumulated across frames while streaming) and composited with
	// brightness/contrast/hdr into the final buffer (wx DrawOVER / DrawMIP /
	// DrawVolumesMulti)
	DrawVolumesByMethod(0);
	if (m_clear_final_buffer)
	{
		// nothing drawn: still clear the final buffer so stale content is not shown
		// (wx: the m_clear_final_buffer block at the end of DrawVolumes)
		m_clear_final_buffer = false;
		Vulkan2dRender::V2DRenderParams p = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		p.pipeline = m_v2drender->preparePipeline(IMG_SHADER_TEXTURE_LOOKUP, V2DRENDER_BLEND_DISABLE,
			m_fbo_final->attachments[0]->format, (int)m_fbo_final->attachments.size(), 0,
			m_fbo_final->attachments[0]->is_swapchain_images);
		if (!m_fbo_final->renderPass)
			m_fbo_final->replaceRenderPass(p.pipeline.pass);
		p.clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		m_v2drender->clear(m_fbo_final, p);
	}
}

// verbatim from VRenderVulkanView::DrawFinalBuffer
void RenderCanvas::SimulateMouse(int type, const QPoint& pos, Qt::MouseButton button, Qt::MouseButtons buttons,
	Qt::KeyboardModifiers mods)
{
	if (!m_window) return;
	QPointF p(pos);
	QMouseEvent ev((QEvent::Type)type, p, p, m_window->mapToGlobal(pos), button, buttons, mods);
	QCoreApplication::sendEvent(m_window, &ev);
}

void RenderCanvas::OnTabletEvent(QTabletEvent* e)
{
	if (!e) return;
	switch (e->type())
	{
	case QEvent::TabletPress:
		m_tablet_down = true;
		m_tablet_pressure = e->pressure() * 2.0;
		break;
	case QEvent::TabletMove:
		m_tablet_down = e->buttons() != Qt::NoButton;
		m_tablet_pressure = m_tablet_down ? e->pressure() * 2.0 : 1.0;
		break;
	case QEvent::TabletRelease:
		m_tablet_down = false;
		m_tablet_pressure = 1.0;
		break;
	default:
		break;
	}
}

void RenderCanvas::SimulateTablet(int type, const QPoint& pos, double pressure, Qt::MouseButtons buttons)
{
	if (!m_window) return;
	QPointF p(pos);
	const Qt::MouseButton button = type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton;
	QTabletEvent ev((QEvent::Type)type, QPointingDevice::primaryPointingDevice(), p, m_window->mapToGlobal(p),
		pressure, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, Qt::NoModifier, button, buttons);
	QCoreApplication::sendEvent(m_window, &ev);
}

void RenderCanvas::SimulateKey(int type, int key, Qt::KeyboardModifiers mods, const QString& text)
{
	if (!m_window) return;
	QKeyEvent ev((QEvent::Type)type, key, mods, text);
	QCoreApplication::sendEvent(m_window, &ev);
}

void RenderCanvas::SimulateWheel(const QPoint& pos, int delta_y)
{
	if (!m_window) return;
	QPointF p(pos);
	QWheelEvent ev(p, m_window->mapToGlobal(p), QPoint(), QPoint(0, delta_y), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
	QCoreApplication::sendEvent(m_window, &ev);
}

bool RenderCanvas::SimulateDrop(const QList<QUrl>& urls)
{
	if (!m_window) return false;
	QMimeData mime;
	mime.setUrls(urls);
	QPointF p(m_window->width() / 2.0, m_window->height() / 2.0);
	QDragEnterEvent enter(p.toPoint(), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
	QCoreApplication::sendEvent(m_window, &enter);
	if (!enter.isAccepted()) return false;
	QDropEvent drop(p, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
	QCoreApplication::sendEvent(m_window, &drop);
	return drop.isAccepted();
}

void RenderCanvas::CaptureToFile(const QString& path)
{
	m_capture_path = path;
	RequestRender();
}

// Copy the swapchain image of this frame into an off-screen texture (the
// recording path of VRenderVulkanView::OnDraw).
void RenderCanvas::RecordFrame(int nx, int ny)
{
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (m_fbo_record && (m_fbo_record->w != nx || m_fbo_record->h != ny))
	{
		m_fbo_record.reset();
		m_tex_record.reset();
	}
	if (!m_fbo_record)
	{
		m_fbo_record = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_record->w = nx;
		m_fbo_record->h = ny;
		m_fbo_record->device = prim_dev;
		m_tex_record = prim_dev->GenTexture2D(current_fbo->attachments[0]->format, VK_FILTER_LINEAR, nx, ny);
		m_fbo_record->addAttachment(m_tex_record);
	}
	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	params.pipeline = m_v2drender->preparePipeline(IMG_SHADER_TEXTURE_LOOKUP, V2DRENDER_BLEND_DISABLE,
		m_fbo_record->attachments[0]->format, (int)m_fbo_record->attachments.size(), 0,
		m_fbo_record->attachments[0]->is_swapchain_images);
	params.tex[0] = current_fbo->attachments[0].get();
	params.clear = true;
	if (!m_fbo_record->renderPass)
		m_fbo_record->replaceRenderPass(params.pipeline.pass);
	m_v2drender->render(m_fbo_record, params);
}

void RenderCanvas::SaveRecordedFrame()
{
	QString path = m_capture_path;
	m_capture_path.clear();
	const bool ok = WriteRecordedFrame(path);
	Q_EMIT frameCaptured(path, ok);
}

// the image copied by RecordFrame, as PNG/TIFF/... (format from the suffix)
bool RenderCanvas::WriteRecordedFrame(const QString& path)
{
	bool ok = false;
	if (m_tex_record)
	{
		vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
		int w = m_fbo_record->w, h = m_fbo_record->h;
		VkFormat fmt = m_tex_record->format;
		if (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB ||
			fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_SRGB)
		{
			QImage img(w, h, QImage::Format_ARGB32);
			std::vector<unsigned char> buf(size_t(w) * h * 4);
			if (m_vulkan->vulkanDevice->DownloadTexture(m_tex_record, buf.data()))
			{
				bool bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
				for (int y = 0; y < h; ++y)
				{
					const unsigned char* s = &buf[size_t(y) * w * 4];
					QRgb* d = reinterpret_cast<QRgb*>(img.scanLine(y));
					for (int x = 0; x < w; ++x, s += 4)
						d[x] = bgra ? qRgb(s[2], s[1], s[0]) : qRgb(s[0], s[1], s[2]);
				}
				ok = img.save(path);
			}
		}
		else
			qWarning("RenderCanvas: capture: unsupported swapchain format %d", (int)fmt);
	}
	return ok;
}

void RenderCanvas::DrawFinalBuffer(bool clear, vks::VTexture* tex)
{
	// wx DrawVolumes / DrawVolumesDP "final composition" tile branch
	if (m_tile_rendering && !m_draw_overlays_only) { DrawTiledFinalBuffer(); return; }
	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline = m_v2drender->preparePipeline(IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR, V2DRENDER_BLEND_OVER,
		current_fbo->attachments[0]->format, (int)current_fbo->attachments.size(), 0,
		current_fbo->attachments[0]->is_swapchain_images);
	params.tex[0] = tex ? tex : m_tex_final.get();
	params.loc[0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params.loc[1] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params.loc[2] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	// wx m_frame_clear: back-facing clipping planes may already be on the
	// swapchain image (DrawClippingPlanes(false, BACK_FACE))
	params.clear = clear && m_frame_clear;
	m_frame_clear = false;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);
	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

void RenderCanvas::Render()
{
	// A frame is one acquire / present pair on the swapchain; never nest one
	// inside another (an event flushed from inside the frame could otherwise
	// re-enter here and acquire a second image before the first is presented).
	if (m_shutdown) return;
	if (m_render_suspended > 0) return;   // ResumeRendering requests the next frame
	if (m_rendering) { if (m_window) m_window->requestUpdate(); return; }
	// scene buffers: wx m_nx/m_ny (the tile size while tile rendering);
	// overlays and tools draw on the swapchain (wnx x wny)
	QSize s = RenderSize();
	int nx = s.width(), ny = s.height();
	const QSize ws = DeviceSize();
	const int wnx = ws.width(), wny = ws.height();
	if (nx <= 0 || ny <= 0 || wnx <= 0 || wny <= 0) return;
	if (!m_window->isExposed()) return;
	if (!EnsureVulkan()) return;
	// several views: this canvas's swapchain and brick-loop state
	if (s_loop_owner && s_loop_owner != this && s_loop_owner->m_rendering) { if (m_window) m_window->requestUpdate(); return; }
	MakeCurrent();

	struct Guard { bool& f; Guard(bool& b) : f(b) { f = true; } ~Guard() { f = false; } } guard(m_rendering);
	const bool ptm = vvd::ptime::On() && vvd::ptime::Stroke().active;
	const double pt_frame0 = ptm ? vvd::ptime::NowMs() : 0.0;
	const bool pt_overlays = m_draw_overlays_only;

	m_vulkan->ResetRenderSemaphores();
	m_vulkan->prepareFrame();
	m_frame_clear = true;

	bool any_visible = !PopVolumeList().empty();
	PopMeshList();

	// wx UpdateScreen: PreDraw() (animated camera move, RenderCanvasMovie.cpp),
	// then Draw(), which starts with the gradient background
	PreDraw();
	if (m_grad_bg)
		DrawGradBg();   // RenderCanvasCamera.cpp
	if (!m_md_pop_list.empty()) any_visible = true;
	// wx Draw(): nothing but the background while the view's "Toggle
	// Visibility" (m_draw_all) is off
	if (!m_draw_all) any_visible = false;

	// overlays lay text out in logical units (wnx x wny) while a Retina
	// swapchain image is in physical pixels; TextRenderer rasterizes glyphs
	// at that scale (wx: the CAMetalLayer keeps contentsScale 1)
	TextRenderer::SetSwapchainLayoutSize(wnx, wny);
	for (CanvasTool* t : m_tools) t->PreDraw(this, wnx, wny);

	if (!any_visible)
	{
		// nothing to stream: end a loop left running by now-hidden data, or
		// StreamingIdle would keep requesting frames for it
		if (TextureRenderer::get_start_update_loop() && !TextureRenderer::get_done_update_loop())
		{
			TextureRenderer::set_done_update_loop(true);
			HaltLoopUpdate();
		}
		// wx Draw(): the background is cleared only while nothing was drawn
		// on the swapchain image yet (the gradient background)
		HandleProjection(nx, ny);
		HandleCamera();
		glm::mat4 mv_temp = m_mv_mat;
		m_mv_mat = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
		m_mv_mat = glm::translate(m_mv_mat, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));
		// wx Draw(): the grid before the (empty) scene
		if (m_draw_grid && m_draw_all)
			DrawGrid();
		if (m_frame_clear)
			ClearScreen();
		m_frame_clear = false;
		for (CanvasTool* t : m_tools) t->DrawOverlay(this, wnx, wny);
		m_mv_mat = mv_temp;
	}
	else
	{
		// wx Draw(): restart the brick loop when the last one is done (or a
		// refresh asked for it) -- RenderCanvasStreaming.cpp
		StreamingBeginFrame();
		HandleProjection(nx, ny);
		HandleCamera();
		glm::mat4 mv_temp = m_mv_mat;
		m_mv_mat = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
		m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
		m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
		m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
		m_mv_mat = glm::translate(m_mv_mat, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

		// meshes + volumes into the final buffer (streamed: a time slice of
		// bricks per frame, accumulated), final buffer to the swapchain
		// wx Draw(): clipping planes behind (before) and in front of (after)
		// the scene while the Clipping Planes pane is hovered / held
		// (not while tile rendering: the swapchain shows the tile preview)
		const bool draw_clip = m_draw_clip && !m_tile_rendering && !(TextureRenderer::get_done_update_loop() && m_draw_overlays_only);
		// wx Draw(): the grid (wireframe test mode) first
		if (m_draw_grid && !m_tile_rendering && !(TextureRenderer::get_done_update_loop() && m_draw_overlays_only))
			DrawGrid();
		if (draw_clip)
			DrawClippingPlanes(false, 2 /*BACK_FACE*/);
		DrawScene(nx, ny);
		if (m_draw_clip && !m_tile_rendering)
			DrawClippingPlanes(true, 1 /*FRONT_FACE*/);
		if (!m_tile_rendering)
			for (CanvasTool* t : m_tools) t->DrawOverlay(this, wnx, wny);
		m_mv_mat = mv_temp;
	}
	// "Capture" button: wx PostDraw reads the frame back here, before the
	// frame border / Info / brush (RenderCanvasCapture.cpp)
	CaptureSwapchain(wnx, wny);
	if (!m_tile_rendering)
		for (CanvasTool* t : m_tools) t->DrawOverlayPost(this, wnx, wny);
	// CaptureToFile takes the first frame after the streaming loop has
	// finished, as presented
	const bool capture = !m_capture_path.isEmpty() && IsUpdateLoopDone();
	// movie recording (wx OnDraw: m_recording && (done_update_loop || !mem_swap)),
	// after the frame border / Info like wx
	const bool record = m_recording && m_rec_requested && !m_tile_rendering && IsUpdateLoopDone();
	// Testing aid: VVD_QT_GRAB_MIDPASS=<dir> saves the presented frames of
	// unfinished streaming passes (brick loop still running) as
	// <dir>/midpass_<n>.png, at most VVD_QT_GRAB_MIDPASS_MAX (default 60)
	static const QString midpass_dir = qEnvironmentVariable("VVD_QT_GRAB_MIDPASS");
	static const int midpass_max = qEnvironmentVariableIsSet("VVD_QT_GRAB_MIDPASS_MAX") ?
		qEnvironmentVariableIntValue("VVD_QT_GRAB_MIDPASS_MAX") : 60;
	static int midpass_count = 0;
	const bool midpass = !midpass_dir.isEmpty() && !capture && !record && !m_tile_rendering &&
		midpass_count < midpass_max && any_visible &&
		TextureRenderer::get_mem_swap() && TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop();
	if (capture || record || midpass)
		RecordFrame(wnx, wny);
	m_vulkan->submitFrame();
	m_frame_presented = true;
	m_presented_frames++;
	m_presented_size = ws;
	if (record)
		ReadRecordedFrame();
	if (capture)
		SaveRecordedFrame();
	if (midpass)
	{
		const QString f = QDir(midpass_dir).filePath(QString("midpass_%1.png").arg(midpass_count++, 3, 10, QChar('0')));
		WriteRecordedFrame(f);
	}
	CapturePostDraw();
	if (ptm)
	{
		vvd::ptime::StrokeWindow& w = vvd::ptime::Stroke();
		const double t = vvd::ptime::NowMs();
		w.frames++;
		w.frame_ms += t - pt_frame0;
		const bool done = IsUpdateLoopDone();
		if (w.release_ms > 0.0)
		{
			vvd::ptime::Log("frame after release: %.1f ms (requested overlays-only %d, mask drawn %d), %.1f ms since release, loop %d/%d done %d",
				t - pt_frame0, (int)pt_overlays, (int)m_draw_mask, t - w.release_ms, TextureRenderer::get_cur_brick_num(),
				TextureRenderer::get_total_brick_num(), (int)done);
			if (done)
			{
				vvd::ptime::Log("redraw after release finished: %d frames, %.1f ms rendering, %.1f ms since release; press to finished image %.1f ms",
					w.frames, w.frame_ms, t - w.release_ms, t - w.press_ms);
				w.active = false;
			}
		}
	}
	// wx OnIdle: keep drawing while the brick loop is running
	StreamingIdle();
	StreamingDebugFrame();
}

// ---------------------------------------------------------------------------
// Qt events
// ---------------------------------------------------------------------------

// Expose events of the native surface. On macOS one arrives for every
// presented frame (the CAMetalLayer is displayed in the CATransaction commit
// that follows the present), so scheduling a frame for each of them redrew
// the view at the display refresh rate forever, even when nothing changed.
// The swapchain keeps showing the last presented image: only a surface that
// has just become exposed, or one whose size differs from the last presented
// frame, needs a new frame. Scene changes request their own frames.
void RenderCanvas::OnExpose(bool exposed)
{
	if (!exposed)
	{
		m_surface_exposed = false;
		m_frame_presented = false;
		return;
	}
	const bool newly_exposed = !m_surface_exposed;
	m_surface_exposed = true;
	if (newly_exposed || !m_frame_presented || m_presented_size != DeviceSize())
		RequestFrame();
}

void RenderCanvas::OnSurfaceResized()
{
	m_frame_presented = false;
	if (!m_vulkan) return;
	QSize s = DeviceSize();
	if (s.width() <= 0 || s.height() <= 0) return;
	if (!MakeCurrent())
	{
		QTimer::singleShot(0, this, [this]() { OnSurfaceResized(); });
		return;
	}
	for (VolumeData* vd : m_volumes)
		if (vd && vd->GetVR()) vd->GetVR()->resize();
	ResizeMultiVolumeRenderer();   // wx: if (m_mvr) m_mvr->resize();
	m_vulkan->setSize(s.width(), s.height());
	RequestRender();
}

void RenderCanvas::mousePressEvent(QMouseEvent* e)
{
	Q_EMIT viewActivated();
	m_container->setFocus();
	m_pick = false;
	// wx RightDown: any interaction mode (the menu opens on RightUp)
	m_rclick = e->button() == Qt::RightButton && e->type() != QEvent::MouseButtonDblClick;
	m_rclick_pos = e->pos();
	// wx OnMouse: LeftDClick comes first in every interaction mode; on a
	// ruler point (GetEditingRulerPoint) it starts the animated move that
	// centres the point (StartManipulation)
	if (e->button() == Qt::LeftButton && e->type() == QEvent::MouseButtonDblClick &&
		m_dblclick_hook && m_dblclick_hook(e->pos()))
	{
		m_drag_buttons = Qt::NoButton;
		m_pick = false;
		return;
	}
	if (CanvasTool* t = ActiveTool())
		if (t->MousePress(e)) { m_drag_buttons = Qt::NoButton; return; }
	m_last_pos = e->pos();
	m_drag_buttons = e->buttons();
	if (e->button() == Qt::LeftButton)
	{
		if (e->type() == QEvent::MouseButtonDblClick)
		{
			// wx LeftDClick: segment selection, no camera drag
			m_drag_buttons = Qt::NoButton;
			Q_EMIT segmentPickRequested(e->pos());
			return;
		}
		// wx LeftDown in int mode 1: m_pick = true
		m_pick = true;
		m_pick_pos = e->pos();
	}
}

void RenderCanvas::mouseReleaseEvent(QMouseEvent* e)
{
	// wx RightUp -> OnContextMenu (in every mode but the ruler's)
	const bool rclick = e->button() == Qt::RightButton && m_rclick;
	if (e->button() == Qt::RightButton) m_rclick = false;
	if (CanvasTool* t = ActiveTool())
	{
		const bool tool_right = rclick && t->HandlesRightClick(e);
		if (t->MouseRelease(e))
		{
			m_drag_buttons = e->buttons();
			m_pick = false;
			if (rclick && !tool_right) Q_EMIT contextMenuRequested(e->pos());
			return;
		}
	}
	m_drag_buttons = e->buttons();
	if (rclick)
		Q_EMIT contextMenuRequested(e->pos());
	// wx LeftUp, int mode 1: if (m_pick && !na_mode) Pick()
	if (e->button() == Qt::LeftButton && m_pick)
	{
		m_pick = false;
		Q_EMIT pickRequested(m_pick_pos);
	}
	// full quality after the release only once the drag has settled
	// (OnInteractionSettleTimer otherwise)
	if (m_drag_buttons == Qt::NoButton && (m_interactive || m_int_res) && InteractionSettled())
	{
		m_interactive = false;
		m_int_res = false;
		m_refresh_start_loop = true;
		RequestRender();   // one full-quality frame after interaction
	}
}

// Camera interaction as in VRenderVulkanView::OnMouse (m_int_mode 1):
// left drag = trackball rotation, middle or Ctrl+left drag = pan,
// right drag = zoom.
void RenderCanvas::mouseMoveEvent(QMouseEvent* e)
{
	for (CanvasTool* t : m_tools)
		if (t) t->HoverMove(e);
	// a right drag (zoom; painting in the paint modes) opens no menu
	if ((e->buttons() & Qt::RightButton) && e->pos() != m_rclick_pos)
		m_rclick = false;
	if (CanvasTool* t = ActiveTool())
		if (t->MouseMove(e)) return;
	if (m_drag_buttons == Qt::NoButton) return;
	// wx Dragging() in int mode 1: "disable picking"
	if (e->pos() != m_pick_pos)
		m_pick = false;
	OnCameraDrag(e);   // RenderCanvasCamera.cpp (wx OnMouse dragging)
}

// wx VRenderVulkanView::PickMesh
MeshData* RenderCanvas::PickMesh(const QPoint& pos, int* seg)
{
	if (seg) *seg = -1;
	if (m_rendering || !EnsureVulkan()) return nullptr;
	const QSize s = RenderSize();
	const int nx = s.width(), ny = s.height();
	if (nx <= 0 || ny <= 0 || width() <= 0 || height() <= 0) return nullptr;
	const int px = pos.x() * nx / width();
	const int py = pos.y() * ny / height();
	if (px < 0 || px >= nx || py < 0 || py >= ny) return nullptr;

	PopMeshList();
	if (m_md_pop_list.empty()) return nullptr;
	//projection
	HandleProjection(nx, ny);
	//Transformation
	HandleCamera();
	glm::mat4 mv_temp = m_mv_mat;
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	vks::VulkanDevice* prim_dev = m_vulkan->devices[0];
	if (m_fbo_pick && (m_fbo_pick->w != nx || m_fbo_pick->h != ny))
	{
		m_fbo_pick.reset();
		m_tex_pick.reset();
		m_tex_pick_depth.reset();
	}
	if (!m_fbo_pick)
	{
		m_fbo_pick = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_pick->w = nx;
		m_fbo_pick->h = ny;
		m_fbo_pick->device = prim_dev;
		m_tex_pick = prim_dev->GenTexture2D(VK_FORMAT_R32_UINT, VK_FILTER_NEAREST, nx, ny);
		m_fbo_pick->addAttachment(m_tex_pick);
		m_tex_pick_depth = prim_dev->GenTexture2D(m_vulkan->depthFormat, VK_FILTER_NEAREST, nx, ny,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		m_fbo_pick->addAttachment(m_tex_pick_depth);
	}

	bool clear = true;
	for (size_t i = 0; i < m_md_pop_list.size(); i++)
	{
		MeshData* md = m_md_pop_list[i];
		if (!md) continue;
		md->SetMatrices(m_mv_mat, m_proj_mat);
		md->SetDevice(m_vulkan->devices[0]);
		md->DrawInt((unsigned int)i + 1, m_fbo_pick, clear);
		clear = false;
	}
	unsigned int choose = 0;
	const VkOffset2D offset = { px, py };
	const VkExtent2D extent = { 1, 1 };
	prim_dev->DownloadSubTexture2D(m_fbo_pick->attachments[0], &choose, offset, extent);
	MeshData* picked = nullptr;
	if (choose > 0 && choose <= m_md_pop_list.size())
	{
		picked = m_md_pop_list[choose - 1];
		if (picked && picked->isTree())
		{
			// wx: the segment ids of the tree mesh under the cursor
			picked->SetMatrices(m_mv_mat, m_proj_mat);
			picked->SetDevice(m_vulkan->devices[0]);
			picked->DrawInt(0, m_fbo_pick, true, VkRect2D{ { 0, 0 }, { 0, 0 } }, true);
			unsigned int id = 0;
			prim_dev->DownloadSubTexture2D(m_fbo_pick->attachments[0], &id, offset, extent);
			if (seg) *seg = (int)id;
		}
	}
	m_mv_mat = mv_temp;
	return picked;
}

void RenderCanvas::wheelEvent(QWheelEvent* e)
{
	if (CanvasTool* t = ActiveTool())
		if (t->Wheel(e)) return;
	OnCameraWheel(e);   // RenderCanvasCamera.cpp (wx OnMouse wheel)
}

void RenderCanvas::SetKeyV(bool down)
{
	if (m_key_v == down)
		return;
	m_key_v = down;
	// wx: if (!m_force_hide_mask) m_draw_mask = !GetKeyState('V'); refresh
	const bool draw = !m_force_hide_mask && !m_key_v;
	if (draw != m_draw_mask)
	{
		m_draw_mask = draw;
		RequestRender();
	}
}

// The wx OnIdle keys (V held, Ctrl+arrows, a / d / space, s / w, Alt+V,
// Ctrl+B, Esc while tile rendering) are read application-wide by the
// ViewKeyFilter (RenderCanvasCamera.cpp), whichever widget has the focus;
// here only what reaches the canvas itself.
void RenderCanvas::keyReleaseEvent(QKeyEvent* e)
{
	QWidget::keyReleaseEvent(e);
}

void RenderCanvas::keyPressEvent(QKeyEvent* e)
{
	if (CanvasTool* t = ActiveTool())
		if (t->KeyPress(e)) return;
	// (the Qt port had R = reset view; wx has no such key)
	QWidget::keyPressEvent(e);
}
