#ifndef VVD_QT_RENDERCANVAS_H
#define VVD_QT_RENDERCANVAS_H

// Qt render canvas for VVDViewer.
//
// This is a deliberately small view on top of the existing FLIVR/Vulkan
// engine: it brings Vulkan up on the widget's native window, keeps a camera,
// and composites the visible volumes the same way VRenderVulkanView does in
// its "composite" mode (per-volume off-screen buffer -> final buffer ->
// swapchain). Interaction: left-drag rotates, right/middle-drag pans, wheel
// zooms. Meshes are drawn the way the wx view does in composite mode: all
// visible meshes go into a depth-peeling buffer (colour + depth, 1 layer, or
// m_peeling_layers when a mesh is transparent), that buffer is composited
// into the final buffer first and the volumes are then added on top
// (VRenderVulkanView::DrawVolumes / DrawMeshes / DrawOLShadowsMesh).
// Everything else the wx canvas does (brush, rulers, movie capture,
// streaming, depth/multi blend mode ...) is out of scope for phase 1 of the
// port and lives in VRenderView.cpp until it is extracted into a toolkit-
// neutral scene class.

#include <QWidget>
#include <QWindow>
#include <QString>
#include <memory>
#include <vector>
#include <algorithm>

#include <glm/glm.hpp>
#include "FLIVR/BBox.h"
#include "FLIVR/Color.h"
#include "FLIVR/Vector.h"
#include "FLIVR/Quaternion.h"
#include "Core/VVDString.h"
#include <functional>

class CanvasTool;

class VVulkan;
class Vulkan2dRender;
class VulkanSurfaceWindow;
class VolumeData;
class MeshData;
class TreeLayer;
class Annotations;
class DataGroup;
class MeshGroup;
class VolumeLoader;
class EmptyBlockDetector;
class Interpolator;
class TraceGroup;
namespace vks { class VFrameBuffer; class VTexture; }
namespace FLIVR { class MultiVolumeRenderer; class Texture; }

class RenderCanvas : public QWidget
{
	Q_OBJECT
public:
	explicit RenderCanvas(QWidget* parent = nullptr);
	~RenderCanvas() override;

	// Engine bring-up happens lazily on the first paint (the native window
	// must exist). Call this to force it, e.g. before loading data.
	bool EnsureVulkan();
	bool IsVulkanReady() const { return m_vulkan != nullptr; }
	// wx GetAvailableGraphicsMemory(device_id): free device memory (MB)
	double GetAvailableGraphicsMemory(int device_id) const;
	std::shared_ptr<VVulkan> GetVulkan() const { return m_vulkan; }

	// ---- several render views (wx VRenderFrame::m_vrv_list). The first
	// canvas brings up the Vulkan context; every further canvas shares it
	// (device, textures, pipelines, the engine's static renderer state) and
	// adds a presentation surface of its own (VVulkan::beginSurface). The
	// engine's brick-loop state (TextureRenderer start/done_update_loop,
	// brick counters, time budget) is static as well: each canvas keeps its
	// own copy and swaps it in before it draws. MakeCurrent() selects this
	// canvas's surface and loop state (done by Render / resize itself).
	bool MakeCurrent();   // false while another canvas is drawing its frame
	bool IsSecondarySurface() const { return m_secondary; }
	// the canvas the view keys forwarded to other panes act for (wx
	// OnIdle: the frame-wide actions -- time step, clip move, Alt+V, Ctrl+B,
	// F / L of the tracking dialog -- run once, for the current view);
	// null: every canvas (one view)
	static void SetKeyCanvas(RenderCanvas* c) { s_key_canvas = c; }
	static RenderCanvas* KeyCanvas() { return s_key_canvas; }
	// a streaming loop of this canvas is running (wx mem_swap &&
	// start_update_loop && !done_update_loop), from its own loop state
	bool IsLoopRunning() const;
	// every canvas, in creation order (linked rotations)
	static const std::vector<RenderCanvas*>& AllCanvases();

	// Scene content (not owned). The scene is the wx layer list
	// (VRenderVulkanView::m_layer_list, RenderCanvasLayers.cpp): volumes live
	// in DataGroups, meshes at the view level or in MeshGroups. Volumes() /
	// Meshes() are derived flat lists: every volume / mesh in layer order
	// (groups expanded, hidden groups included); PopVolumeList() /
	// PopMeshList() give the displayed ones (group visibility honoured).
	// AddVolume = wx AddVolumeData(vd) + the volume becomes current.
	void AddVolume(VolumeData* vd);
	void RemoveVolume(VolumeData* vd);
	void ClearVolumes();
	const std::vector<VolumeData*>& Volumes() const { return m_volumes; }

	// ---- layer list (RenderCanvasLayers.cpp; wx VRenderVulkanView API).
	// Groups are owned by the canvas; data stay owned by the DataManager:
	// the Remove* calls only take data out of the view (the caller deletes
	// them from the DataManager, wx does both in one call).
	int GetLayerNum() const { return (int)m_layer_list.size(); }
	TreeLayer* GetLayer(int index) const;
	int GetGroupNum() const;
	int GetAllVolumeNum() const { return (int)m_volumes.size(); }
	int GetDispVolumeNum() const { return (int)PopVolumeList().size(); }
	DataGroup* AddVolumeData(VolumeData* vd, const VVDString& group_name = VVDString());
	void AddMeshData(MeshData* md);
	// wx AddAnnotations / RemoveAnnotations (tree layer only; not drawn)
	void AddAnnotations(Annotations* ann);
	void RemoveAnnotations(Annotations* ann);
	// wx AddGroup: new group after prev_group (else appended), with the
	// Output Adjustments defaults (SetGroupDefaultsProvider); returns its name
	VVDString AddGroup(const VVDString& str = VVDString(), const VVDString& prev_group = VVDString());
	VVDString AddMGroup(const VVDString& str = VVDString());
	DataGroup* GetGroup(const VVDString& name) const;
	MeshGroup* GetMGroup(const VVDString& name) const;
	DataGroup* GetParentGroup(const VVDString& vol_name) const;
	MeshGroup* GetParentMGroup(const VVDString& mesh_name) const;
	DataGroup* GetParentGroup(VolumeData* vd) const;
	MeshGroup* GetParentMGroup(MeshData* md) const;
	DataGroup* GetCurrentVolGroup() const;   // wx GetCurrentVolGroup
	VolumeData* GetVolumeData(const VVDString& name) const;
	MeshData* GetMeshData(const VVDString& name) const;
	void RemoveVolumeData(const VVDString& name);
	// wx ReplaceVolumeData(name, dst), the layer part: the volume called name
	// (view level or group member) becomes dst; the caller replaces it in the
	// DataManager. False when the view has no such volume.
	bool ReplaceVolumeData(const VVDString& name, VolumeData* dst);
	void RemoveMeshData(const VVDString& name);
	// wx RemoveGroup: the group and its members leave the view; the members
	// are returned for the caller to delete (DataManager)
	void RemoveGroup(const VVDString& name, std::vector<VolumeData*>* removed_vols = nullptr,
		std::vector<MeshData*>* removed_meshes = nullptr);
	void Isolate(int type, const VVDString& name);
	void ShowAll();
	void RandomizeColor();
	// wx OrganizeLayers (UpdateTree): volumes at the view level go into the
	// last empty group, or a new one
	void OrganizeLayers();
	// a group's members were changed directly (DataGroup / MeshGroup API)
	void LayersModified() { LayersChanged(true); }
	VVDString CheckNewGroupName(const VVDString& name, int type) const;
	bool CheckGroupNames(const VVDString& name, int type) const;
	void MoveLayerinView(const VVDString& src_name, const VVDString& dst_name);
	void MoveLayerinGroup(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name, int insert_mode = 0);
	void MoveLayertoView(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name);
	void MoveLayertoGroup(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name);
	void MoveLayerfromtoGroup(const VVDString& src_group_name, const VVDString& dst_group_name,
		const VVDString& src_name, const VVDString& dst_name, int insert_mode = 0);
	void MoveMeshinGroup(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name, int insert_mode = 0);
	void MoveMeshtoView(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name);
	void MoveMeshtoGroup(const VVDString& group_name, const VVDString& src_name, const VVDString& dst_name);
	void MoveMeshfromtoGroup(const VVDString& src_group_name, const VVDString& dst_group_name,
		const VVDString& src_name, const VVDString& dst_name, int insert_mode = 0);
	// wx m_draw_all ("Toggle Visibility" of the view item, VRenderView::ToggleDraw)
	void SetDraw(bool draw) { m_draw_all = draw; RequestRender(); }
	void ToggleDraw() { SetDraw(!m_draw_all); }
	bool GetDraw() const { return m_draw_all; }
	// wx AddGroup reads AdjustView::GetDefaults
	void SetGroupDefaultsProvider(std::function<void(DataGroup*)> f) { m_group_defaults = std::move(f); }
	// displayed volumes / meshes in layer order (wx m_vd_pop_list / m_md_pop_list)
	std::vector<VolumeData*> PopVolumeList() const;
	std::vector<MeshData*> GetPopMeshList() const;
	void SetCurrentVolume(VolumeData* vd) { m_cur_vol = vd; }
	VolumeData* GetCurrentVolume() const { return m_cur_vol; }

	// Meshes (not owned; DataManager owns them). RemoveMesh waits for the
	// GPU queue to drain, so the caller may delete the MeshData right after.
	void AddMesh(MeshData* md);
	void RemoveMesh(MeshData* md);
	void ClearMeshes();
	const std::vector<MeshData*>& Meshes() const { return m_meshes; }
	// Selected mesh: drawn with its bounding box, like the wx frame does
	// on tree selection (SetDrawBounds). nullptr clears the highlight.
	void SetCurrentMesh(MeshData* md);
	MeshData* GetCurrentMesh() const { return m_cur_mesh; }
	// Depth peeling layers used when a mesh is transparent (SettingDlg
	// "peeling layers", default 1).
	void SetPeelingLayers(int n) { m_peeling_layers = std::max(1, n); RequestRender(); }
	int GetPeelingLayers() const { return m_peeling_layers; }
	// Recompute scene bounds/centre, e.g. after a mesh's radius scale or
	// visibility changed (wx: InitView(INIT_BOUNDS | INIT_CENTER)).
	void RecomputeBounds() { ComputeBounds(); RequestRender(); }

	// Camera
	void ResetView();              // fit bounds, reset rotation/translation
	void SetPerspective(bool persp);
	bool GetPerspective() const { return m_persp; }
	void SetBackgroundColor(const FLIVR::Color& c);
	FLIVR::Color GetBackgroundColor() const { return m_bg_color; }
	// wx SetRotations(.., ui_update, link_update): also re-applies the clipping
	// planes (A2QSync) and updates linked canvases
	void SetRotations(double rx, double ry, double rz, bool link_update = true);
	// View rotation in degrees, same convention as VRenderVulkanView
	// (m_rotx/m_roty/m_rotz <-> quaternion m_q; the X slider shows 360-rotx).
	void GetRotations(double& rx, double& ry, double& rz) const { rx = m_rotx; ry = m_roty; rz = m_rotz; }
	long long PresentedFrames() const { return m_presented_frames; }
	// Object translation (wx m_obj_trans*, panning).
	void GetObjTrans(double& x, double& y, double& z) const { x = m_obj_transx; y = m_obj_transy; z = m_obj_transz; }
	// Object centre / rotation of the object transform in GetMvMat()
	// (mv = camera * T(trans) * Rx * Ry * Rz * T(-ctr)), camera distance
	// (wx m_distance) and the brick loader (wx m_loader) - for ViewOverlays.
	void GetObjCenter(double& x, double& y, double& z) const { x = m_obj_ctrx; y = m_obj_ctry; z = m_obj_ctrz; }
	void GetObjRot(double& x, double& y, double& z) const { x = m_obj_rotx; y = m_obj_roty; z = m_obj_rotz; }
	double GetCamDistance() const { return m_distance; }
	VolumeLoader* GetLoader() const { return m_loader.get(); }
	void SetObjTrans(double x, double y, double z) { m_obj_transx = x; m_obj_transy = y; m_obj_transz = z; RequestRender(); }

	// ---- tools (brush, rulers): see CanvasTool.h
	void AddTool(CanvasTool* t) { if (t && std::find(m_tools.begin(), m_tools.end(), t) == m_tools.end()) m_tools.push_back(t); }
	void RemoveTool(CanvasTool* t) { m_tools.erase(std::remove(m_tools.begin(), m_tools.end(), t), m_tools.end()); }

	// ---- frame state for tools (valid inside a frame / after the first one)
	const glm::mat4& GetMvMat() const { return m_mv_mat; }      // object model-view
	const glm::mat4& GetProjMat() const { return m_proj_mat; }
	const glm::mat4& GetTexMat() const { return m_tex_mat; }
	std::shared_ptr<Vulkan2dRender> GetV2DRender() const { return m_v2drender; }
	std::shared_ptr<vks::VTexture> GetFinalTex() const { return m_tex_final; }   // composited scene
	// last per-volume buffer; wx Set2DWeight(m_tex_final, m_tex_wt2 ? m_tex_wt2 : m_tex):
	// the selected volume's own image (without its mask pass) once one exists
	std::shared_ptr<vks::VTexture> GetVolTex() const { return m_tex_wt2 ? m_tex_wt2 : m_tex; }
	QSize GetDeviceSize() const { return DeviceSize(); }
	bool InFrame() const { return m_rendering; }
	double GetOrthoWidth() const { return m_ortho_right - m_ortho_left; }
	double GetOrthoHeight() const { return m_ortho_top - m_ortho_bottom; }
	double GetNearClip() const { return m_near_clip; }
	double GetFarClip() const { return m_far_clip; }
	double GetRadius() const { return m_radius; }
	const FLIVR::BBox& GetBounds() const { return m_bounds; }
	// Save the next rendered frame (scene + overlays, as presented) to an
	// image file; emits frameCaptured(path, ok) after the frame.
	void CaptureToFile(const QString& path);
	// Testing: like CaptureToFile, but requests no frame of its own -- the
	// next frame something else asks for is saved (none: nothing is saved)
	void CaptureNextFrame(const QString& path) { m_capture_path = path; }
	// Testing: deliver drag-enter + drop of these files to the render surface.
	bool SimulateDrop(const QList<QUrl>& urls);
	// Testing: deliver a synthetic mouse event to the render surface, exactly
	// as a real one would arrive (positions in widget coordinates).
	// wx VRenderVulkanView::PickMesh: the shown meshes drawn with their index
	// into an integer buffer, the pixel under pos (widget coordinates) read
	// back. Returns the mesh there (null: none); seg (when given) receives the
	// SWC segment id under pos for tree meshes (-1 otherwise).
	MeshData* PickMesh(const QPoint& pos, int* seg = nullptr);
	void SimulateMouse(int type, const QPoint& pos, Qt::MouseButton button, Qt::MouseButtons buttons,
		Qt::KeyboardModifiers mods = Qt::NoModifier);
	// Testing: a synthetic tablet (pen) event on the render surface; pressure
	// 0..1 (QTabletEvent::pressure). Not accepted, so Qt follows it with the
	// mouse event a real pen produces only when SynthesizeMouse is set.
	void SimulateTablet(int type, const QPoint& pos, double pressure, Qt::MouseButtons buttons);
	// Pen pressure (wx WT_PACKET pkNormalPressure / 512, Wintab only in wx):
	// QTabletEvent::pressure() x 2 (a 1024-level pen at half force = 1.0,
	// like wx) while the pen touches the canvas; active = a pen is down.
	void GetTabletPressure(bool& active, double& pressure) const { active = m_tablet_down; pressure = m_tablet_pressure; }
	void OnTabletEvent(class QTabletEvent* e);
	// Testing: a synthetic key / wheel event on the render surface.
	void SimulateKey(int type, int key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString& text = QString());
	void SimulateWheel(const QPoint& pos, int delta_y);
	// Interactive (low-res) rendering while dragging; tools may set it.
	bool GetInteractive() const { return m_interactive; }
	double GetScaleFactor() const { return m_scale_factor; }
	void SetScaleFactor(double s);
	void SetInteractive(bool b) { m_interactive = b; }
	double GetAov() const { return m_aov; }
	void SetAov(double aov);       // degrees; <= 10 means orthographic

	// Stop drawing for good (window closing): a frame delivered while the
	// native view is being torn down acquires from a dead swapchain.
	void Shutdown() { m_shutdown = true; }

	// Redraw request (coalesced by Qt)
	void RefreshView() { RequestRender(); }
	// While suspended (nested), frames are not drawn; the first frame after
	// the last Resume restarts the brick loop. Used while a batch of volumes
	// is opened: the progress dialog keeps the event loop running, and every
	// frame of the half-loaded scene would stream bricks (one time budget per
	// frame) on the thread that also finishes the volumes.
	void SuspendRendering() { ++m_render_suspended; }
	void ResumeRendering() { if (m_render_suspended > 0 && --m_render_suspended == 0) RequestRender(); }
	// Draw one frame synchronously (wx Refresh -> UpdateScreen), e.g. so the
	// brush stroke is in the paint buffer before segmenting. No-op (request
	// instead) when called from inside a frame.
	void RenderNow() { Render(); }

	// ---- memory swap / streaming (RenderCanvasStreaming.cpp; port of the
	// VRenderVulkanView update loop). RefreshView()/RequestRender() are wx
	// RefreshGL(erase, start_loop = true); this one is RefreshGLOverlays:
	// re-present the retained final buffer plus overlays without restarting
	// the brick loop (ignored while the loop is still running).
	void RefreshOverlays();
	// An overlay-only change (e.g. the legend flags): a frame that shows it
	// right away without restarting the brick loop -- RefreshOverlays when
	// the loop is done and a frame is on the surface, one more frame of a
	// running loop (it draws the overlays on every frame), else a full
	// redraw. RefreshOverlays alone drops the request in the last two cases.
	void RefreshOverlayChange();
	// wx RefreshGL(false, false): another frame of the running loop.
	void RefreshNoLoopRestart();
	// wx m_res_mode ("Quality" combo: 0 Max, 1 Best ... 5 Min), pyramid
	// (brxml) level selection in switchLevel.
	void SetResMode(int mode);   // RenderCanvasModes.cpp (emits resModeChanged)
	int GetResMode() const { return m_res_mode; }
	// wx m_adaptive / m_adaptive_res: low-quality / low-res while dragging.
	void SetAdaptive(bool b) { m_adaptive = b; }
	bool GetAdaptive() const { return m_adaptive; }
	void SetAdaptiveRes(bool b) { m_adaptive_res = b; }
	bool GetAdaptiveRes() const { return m_adaptive_res; }
	// wx m_draw_mask: the selection (mask) pass DrawVolumesComp(list, true)
	// is drawn and the loop counts (and queues) mask bricks. On by default;
	// hidden while V is held (wx OnIdle) or by SetForceHideMask.
	void SetDrawMask(bool b) { m_draw_mask = b; RequestRender(); }
	bool GetDrawMask() const { return m_draw_mask; }
	// wx VRenderVulkanView::SetForceHideMask
	void SetForceHideMask(bool val) { m_force_hide_mask = val; m_draw_mask = !val && !m_key_v; RequestRender(); }
	bool GetForceHideMask() const { return m_force_hide_mask; }
	void SetKeyV(bool down);   // wx OnIdle draw_mask: V held hides the masks (key events, tests)
	// wx VRenderVulkanView::SetEnhanceSelection (m_enhance_sel, default on):
	// the mask pass is drawn with the mask colour's HDR boost and the
	// brightness scaled by the volume's mask alpha ("Mask Intensity")
	void SetEnhanceSelection(bool b) { m_enhance_sel = b; RequestRender(); }
	bool GetEnhanceSelection() const { return m_enhance_sel; }
	// shared mask / label volumes (VolumeData::GetSharedMaskName /
	// GetSharedLabelName): wx looks them up in the frame's DataManager
	void SetVolumeLookup(std::function<VolumeData*(const VVDString&)> f) { m_volume_lookup = std::move(f); }
	void SetEmptyBlockDetectorActive(bool b) { m_ebd_run = b; }
	// True when no streaming loop is in progress (the presented frame is
	// complete).
	bool IsUpdateLoopDone() const;
	void StreamingDebugFrame();   // VVD_QT_STREAM_DEBUG=1: per-second loop statistics on stderr

	// ---- volume blend modes, depth attenuation (RenderCanvasModes.cpp;
	// port of the VRenderVulkanView DrawVolumes / DrawVolumesComp /
	// DrawVolumesMulti / DrawVolumesDP family)
	// wx m_vol_method: VOL_METHOD_SEQ 1 "Layered", VOL_METHOD_MULTI 2
	// "Depth", VOL_METHOD_COMP 3 "Composite" (VRenderView.h values).
	enum { VolMethodSeq = 1, VolMethodMulti = 2, VolMethodComp = 3 };
	void SetVolMethod(int method);
	int GetVolMethod() const { return m_vol_method; }
	FLIVR::MultiVolumeRenderer* GetMultiVolumeRenderer() const { return m_mvr.get(); }   // wx m_mvr (Info overlay)
	// wx m_use_fog / m_fog_intensity ("Depth Int." column)
	void SetFog(bool b);
	bool GetFog() const { return m_use_fog; }
	void SetFogIntensity(double i);
	double GetFogIntensity() const { return m_fog_intensity; }
	// SettingDlg "Enable depth attenuation on meshes" (wx m_use_fog_mesh)
	void SetUseFogMesh(bool b) { m_use_fog_mesh = b; RequestRender(); }
	bool GetUseFogMesh() const { return m_use_fog_mesh; }
	// SettingDlg "micro blending" (wx m_blend_slices, Depth mode only)
	void SetBlendSlices(bool b) { m_blend_slices = b; RequestRender(); }
	bool GetBlendSlices() const { return m_blend_slices; }
	// wx PopVolumeList -> VRenderView::ToggleQualityDropDownBox: the
	// "Quality:" combo is shown only while a pyramid (brxml) volume is shown
	bool HasPyramidVolume() const { return m_has_brxml; }

	// ---- view-level 2D output adjustment (wx VRenderVulkanView m_gamma /
	// m_brightness / m_hdr / m_levels / m_sync_r,g,b / m_easy_2d_adjust; edited by
	// AdjustPanel when the view is selected; RenderCanvasCamera.cpp). Values
	// are the engine values (AdjustView.h GammaUI2 etc.), not the UI values.
	FLIVR::Color GetGamma() const { return m_gamma; }
	void SetGamma(const FLIVR::Color& c) { m_gamma = c; }
	FLIVR::Color GetBrightness() const { return m_brightness; }
	void SetBrightness(const FLIVR::Color& c) { m_brightness = c; }
	FLIVR::Color GetHdr() const { return m_hdr; }
	void SetHdr(const FLIVR::Color& c) { m_hdr = c; }
	FLIVR::Color GetLevels() const { return m_levels; }
	void SetLevels(const FLIVR::Color& c) { m_levels = c; }
	bool GetSyncR() const { return m_sync_r; }
	bool GetSyncG() const { return m_sync_g; }
	bool GetSyncB() const { return m_sync_b; }
	void SetSyncR(bool b) { m_sync_r = b; }
	void SetSyncG(bool b) { m_sync_g = b; }
	void SetSyncB(bool b) { m_sync_b = b; }
	bool GetEasy2DAdjustMode() const { return m_easy_2d_adjust; }
	void SetEasy2DAdjustMode(bool b) { m_easy_2d_adjust = b; RequestRender(); }
	// The 2D adjustment pass of a channel composite (wx DrawOVER / DrawMIP /
	// DrawVolumesMulti params_final): returns the ImgShader id
	// (IMG_SHDR_LEVELS in easy mode, else IMG_SHDR_BRIGHTNESS_CONTRAST_HDR)
	// and fills loc[0..2]. vd == nullptr: the view's own values (Depth mode).
	int Get2DAdjust(VolumeData* vd, glm::vec4* loc) const;

	// ---- camera modes (RenderCanvasCamera.cpp; wx VRenderVulkanView)
	// wx SetPersp: ortho also leaves free-fly mode (restoring the camera)
	void SetPersp(bool persp);
	// wx m_free ("FreeFly"): perspective camera flying around m_ctr*
	void SetFree(bool free);
	bool GetFree() const { return m_free; }
	// wx m_rot_lock ("45 Increments"): trackball snaps to 45 degrees
	void SetRotLock(bool b) { m_rot_lock = b; }
	bool GetRotLock() const { return m_rot_lock; }
	// wx m_linked_rot (rotation "Link"): every linked canvas follows
	void SetLinkedRot(bool b);
	bool GetLinkedRot() const { return m_linked_rot; }
	// wx VRenderView::OnRotLink: link all canvases and copy this rotation
	void LinkAllRotations(bool link);
	// wx SetCenter ("Reset Transl."): re-centre on the current (clipped) volume
	void SetCenter();
	// wx SetScale121 ("1:1"): one voxel per screen pixel
	void SetScale121();
	// wx m_vrv->m_use_dft_settings: a default view file was loaded, so the
	// first InitView keeps the default rotation and zoom
	void SetUseDefaultSettings(bool b) { m_use_dft_settings = b; }
	bool GetUseDefaultSettings() const { return m_use_dft_settings; }
	double GetAovValue() const { return m_aov; }   // wx GetAov (also while ortho)
	void SetAovValue(double aov) { m_aov = aov; RequestRender(); }   // wx VRenderVulkanView::SetAov (m_persp unchanged)
	// wx GetCenterEyeDist / SetCenterEyeDist (m_distance), SetRadius,
	// Get/SetInitDist (m_init_dist, set by InitView(INIT_TRANSL)), object
	// centre / rotation, near / far clip, m_min_ppi ("min_dpi"), m_draw_type
	// (always 1), view clipping fix parameters, SyncClippingPlaneRotations:
	// the project (.vrp) view state of VRenderFrame::Open/SaveProject
	double GetCenterEyeDist() const { return m_distance; }
	void SetCenterEyeDist(double d) { m_distance = d; RequestRender(); }
	void SetRadius(double r) { m_radius = r; RequestRender(); }
	double GetInitDist() const { return m_init_dist; }
	void SetInitDist(double d) { m_init_dist = d; }
	void SetObjCenters(double x, double y, double z) { m_obj_ctrx = x; m_obj_ctry = y; m_obj_ctrz = z; RequestRender(); }
	void SetObjRot(double x, double y, double z) { m_obj_rotx = x; m_obj_roty = y; m_obj_rotz = z; RequestRender(); }
	void SetNearClip(double nc) { m_near_clip = nc; RequestRender(); }
	void SetFarClip(double fc) { m_far_clip = fc; RequestRender(); }
	double GetMinPPI() const { return m_min_ppi; }
	void SetMinPPI(double ppi) { m_min_ppi = int(ppi); }
	int GetDrawType() const { return 1; }
	void SetClippingFixParams(int clip_mode, const FLIVR::Quaternion& q_cl_zero, const FLIVR::Quaternion& q_cl_fix,
		const FLIVR::Quaternion& q_fix, double rotx_cl_fix, double roty_cl_fix, double rotz_cl_fix,
		double rotx_fix, double roty_fix, double rotz_fix, const FLIVR::Vector& trans_fix)
	{
		m_clip_mode = clip_mode; m_q_cl_zero = q_cl_zero; m_q_cl_fix = q_cl_fix; m_q_fix = q_fix;
		m_rotx_cl_fix = rotx_cl_fix; m_roty_cl_fix = roty_cl_fix; m_rotz_cl_fix = rotz_cl_fix;
		m_rotx_fix = rotx_fix; m_roty_fix = roty_fix; m_rotz_fix = rotz_fix; m_trans_fix = trans_fix;
	}
	void GetClippingFixParams(int& clip_mode, FLIVR::Quaternion& q_cl_zero, FLIVR::Quaternion& q_cl_fix,
		FLIVR::Quaternion& q_fix, double& rotx_cl_fix, double& roty_cl_fix, double& rotz_cl_fix,
		double& rotx_fix, double& roty_fix, double& rotz_fix, FLIVR::Vector& trans_fix) const
	{
		clip_mode = m_clip_mode; q_cl_zero = m_q_cl_zero; q_cl_fix = m_q_cl_fix; q_fix = m_q_fix;
		rotx_cl_fix = m_rotx_cl_fix; roty_cl_fix = m_roty_cl_fix; rotz_cl_fix = m_rotz_cl_fix;
		rotx_fix = m_rotx_fix; roty_fix = m_roty_fix; rotz_fix = m_rotz_fix; trans_fix = m_trans_fix;
	}
	void SyncClippingPlaneRotations(bool force = false);   // RenderCanvasCamera.cpp
	void GetCenters(double& x, double& y, double& z) const { x = m_ctrx; y = m_ctry; z = m_ctrz; }
	void SetCenters(double x, double y, double z) { m_ctrx = x; m_ctry = y; m_ctrz = z; RequestRender(); }
	// wx keyboard state polled in OnIdle (Ctrl+arrows pan, a/d/space, w/s ...).
	// wx reads the global key state whenever any window of the application
	// has the focus (except a text control), so these keys work after a click
	// on a pane too; the Qt canvas installs an application-wide key filter
	// (RenderCanvasCamera.cpp ViewKeyFilter) that does the same.
	void SetKeyLock(bool b) { m_key_lock = b; }
	bool GetKeyLock() const { return m_key_lock; }
	// wx VRenderView m_glview->m_mouse_focus (view default "mouse_focus"):
	// the canvas takes the keyboard focus back after 1:1 / Reset Zoom /
	// Reset Rot.
	void SetMouseFocus(bool b) { m_mouse_focus = b; }
	bool GetMouseFocus() const { return m_mouse_focus; }
	void TakeFocusIfMouseFocus() { if (m_mouse_focus) setFocus(); }
	// wx SetIntp / GetIntp ("Intrp." of the view bar): pushed to every shown
	// volume in StartLoopUpdate and passed to the multi-volume renderer
	void SetIntp(bool b) { m_intp = b; RequestRender(); }
	bool GetIntp() const { return m_intp; }
	// SettingDlg GetShadowDir (volume and mesh shadows; 0,0 = from the viewer)
	void SetShadowDir(double x, double y) { m_shadow_dir_x = x; m_shadow_dir_y = y; RequestRender(); }
	void GetShadowDir(double& x, double& y) const { x = m_shadow_dir_x; y = m_shadow_dir_y; }
	// wx SetGradBg (SettingDlg "gradient background"): DrawGradBg first in Draw()
	// wx Settings test modes (SettingDlg::GetTestMode, VRenderFrame ctor):
	// 1 speed: the view redraws continuously (wx OnIdle m_test_speed);
	// 3 wireframe: m_test_wiref + m_draw_bounds (both without effect in the
	// Vulkan wx view: DrawBounds is commented out) + m_draw_grid (DrawGrid)
	void SetTestSpeed(bool b) { m_test_speed = b; RequestRender(); }
	bool GetTestSpeed() const { return m_test_speed; }
	void SetTestWiref(bool b) { m_test_wiref = b; m_draw_bounds = b; m_draw_grid = b; RequestRender(); }
	bool GetDrawGrid() const { return m_draw_grid; }
	// wx GetTextColor (Settings text colour mode); the grid colour
	void SetTextColorProvider(std::function<FLIVR::Color()> f) { m_text_color = std::move(f); }
	void SetGradBg(bool b) { m_grad_bg = b; RequestRender(); }
	bool GetGradBg() const { return m_grad_bg; }
	// wx (_DARWIN) StartLoopUpdate: slice mode of the volume renderers while a
	// clipping slab is at most 3 voxels thick (ClippingView GetXdist/GetYdist/
	// GetZdist, GetChannLink); the provider returns false when no clipping
	// pane is attached
	struct ClipSlabInfo { bool valid = false; bool chann_link = false; int dist[3] = { 0, 0, 0 }; };
	void SetClipSlabProvider(std::function<ClipSlabInfo()> f) { m_clip_slab = std::move(f); }
	std::function<ClipSlabInfo()> GetClipSlabProvider() const { return m_clip_slab; }
	// wx StartManipulation / EndManipulation: an animated camera move to the
	// given targets (nullptr: keep the current value) over m_manip_time ms at
	// m_manip_fps (wx: 300 ms, 20 fps -> 6 frames), used by a double click on
	// a ruler point (RenderCanvasMovie.cpp)
	void StartManipulation(const FLIVR::Point* view_trans, const FLIVR::Point* view_center, const FLIVR::Point* view_rot,
		const FLIVR::Point* obj_trans, const double* scale);
	void EndManipulation();
	bool IsManipulating() const { return m_manip; }
	// wx OnMouse LeftDClick on a ruler point: returns true when it handled
	// the double click (MainWindow: RulerTool::GetEditingRulerPoint ->
	// StartManipulation)
	void SetDoubleClickHook(std::function<bool(const QPoint&)> f) { m_dblclick_hook = std::move(f); }
	// wx VRenderView::OnRotLink(bool) (SettingDlg "Link all rendering views'
	// rotations"): every view's link flag set to b, rotations copied
	void LinkAllRotationsFromSettings(bool b);
	// wx DrawAnnotations, an annotation layer that carries a mesh: the mesh
	// drawn into its own buffer and ADDed to the swapchain with the layer's
	// alpha (called by ViewOverlays::DrawAnnotations)
	void DrawAnnotationMesh(Annotations* ann, int nx, int ny);

	// ---- clipping planes (RenderCanvasCamera.cpp): the view-level
	// ClippingLayer state of VRenderVulkanView ("Sync All Chan."), the
	// rotation <-> plane transforms (A2Q/Q2A, A2QSync/Q2ASync), combined
	// planes, and DrawClippingPlanes.
	void SetSyncClippingPlanes(bool val) { m_sync_clipping_planes = val; }
	void SetSyncClippingPlanes(bool val, VolumeData* copy);
	void SetSyncClippingPlanes(bool val, MeshData* copy);
	bool GetSyncClippingPlanes() const { return m_sync_clipping_planes; }
	void SetLinkedParam(int id, double p) { if (id >= 0 && id < 6) m_linked_plane_params[id] = p; }
	double GetLinkedParam(int id) const { return (id >= 0 && id < 6) ? m_linked_plane_params[id] : 0.0; }
	void SetClippingLinkX(bool v) { m_link_x_chk = v; }
	void SetClippingLinkY(bool v) { m_link_y_chk = v; }
	void SetClippingLinkZ(bool v) { m_link_z_chk = v; }
	bool GetClippingLinkX() const { return m_link_x_chk; }
	bool GetClippingLinkY() const { return m_link_y_chk; }
	bool GetClippingLinkZ() const { return m_link_z_chk; }
	void SetClipDistance(int x, int y, int z) { m_clip_dist_x = x; m_clip_dist_y = y; m_clip_dist_z = z; }
	void GetClipDistance(int& x, int& y, int& z) const { x = m_clip_dist_x; y = m_clip_dist_y; z = m_clip_dist_z; }
	void SetClipMode(int mode);                 // wx SetClipMode (0..4)
	int GetClipMode() const { return m_clip_mode; }
	void SetClippingPlaneRotations(double rx, double ry, double rz);
	void GetClippingPlaneRotations(double& rx, double& ry, double& rz) const { rx = m_rotx_cl; ry = m_roty_cl; rz = -m_rotz_cl; }
	void GetCameraSettings(double& rx, double& ry, double& rz, FLIVR::Vector& obj_ctr, FLIVR::Vector& obj_trans) const;
	void CalcAndSetCombinedClippingPlanes();
	void A2QSync();
	void Q2ASync();
	// clipping-plane overlay (wx m_draw_clip / m_clip_mask and the
	// ClippingView state DrawClippingPlanes reads)
	void SetDrawClip(bool b) { m_draw_clip = b; }
	bool GetDrawClip() const { return m_draw_clip; }
	void SetClipMask(int m) { m_clip_mask = m; }
	int GetClipMask() const { return m_clip_mask; }
	// plane_mode PM_NORMAL 0 .. PM_NORMALBACK 4; sel 2 = volume, 3 = mesh
	void SetClipDisplay(int plane_mode, bool chann_link, int sel_type, VolumeData* vd, MeshData* md)
	{ m_clip_plane_mode = plane_mode; m_clip_chann_link = chann_link; m_clip_sel_type = sel_type; m_clip_vd = vd; m_clip_md = md; }

	// ---- still capture (RenderCanvasCapture.cpp): back end of the wx
	// "Capture" button (VRenderView::OnCapture after the file dialog,
	// VRenderVulkanView::StartTileRendering / DrawTile / EndTileRendering /
	// PostDraw, VRenderView::StartGroupCapture). The file is an 8-bit RGB TIFF
	// written exactly like wx (CaptureWriter.h). It is taken from the first
	// frame after the streaming loop has finished; emits captureFinished.
	// A size equal to the view size captures the presented frame (scene +
	// camera centre / legend / scale bar / colour map, without the Info text,
	// frame border or brush cursor, as in wx); any other size is rendered in
	// tiles of at most ~2000 px (scene only, like the wx tiled path).
	void StartCapture(const QString& file, int resx, int resy, bool compress);
	// group: the wx DataGroup to isolate (groups named "background" stay
	// shown); volumes: used instead when group is null
	struct GroupCaptureEntry { QString filename; DataGroup* group = nullptr; std::vector<VolumeData*> volumes; };
	// wx m_capture_groups: one capture per entry with only its volumes shown
	// (their own visibility kept), visibility restored afterwards; emits
	// groupCaptureFinished. Returns false when the list is empty.
	bool StartGroupCapture(const std::vector<GroupCaptureEntry>& groups, bool compress);
	bool IsCapturing() const { return m_capture || m_tile_rendering || m_capture_groups; }
	void AbortCapture();                               // Esc during tile rendering
	void Get1x1DispSize(int& w, int& h);               // wx Get1x1DispSize ("1:1")
	int GetCaptureResX() const { return m_capture_resx; }   // wx m_capture_resx (-1: never tiled)
	int GetCaptureResY() const { return m_capture_resy; }
	void StartTileRendering(int w, int h, int tilew, int tileh);
	void EndTileRendering();
	bool IsTileRendering() const { return m_tile_rendering; }
	// wx m_draw_frame / m_frame_x,y,w,h: the capture is cropped to the frame
	void SetCaptureFrame(bool draw, int x, int y, int w, int h)
	{ m_draw_frame = draw; m_frame_x = x; m_frame_y = y; m_frame_w = w; m_frame_h = h; }
	bool GetFrameEnabled() const { return m_draw_frame; }
	void GetFrame(int& x, int& y, int& w, int& h) const { x = m_frame_x; y = m_frame_y; w = m_frame_w; h = m_frame_h; }

	// ---- movie (RenderCanvasMovie.cpp; wx VRenderVulkanView parts used by
	// VMovieView / RecorderDlg)
	// wx VRenderView::GetName (the key code of every animation key)
	void SetViewName(const QString& name) { m_view_name = name; }
	QString GetViewName() const { return m_view_name; }
	// wx camera translation (m_transx/y/z) and distance
	void GetTranslations(double& x, double& y, double& z) const { x = m_transx; y = m_transy; z = m_transz; }
	void SetTranslations(double x, double y, double z);
	const FLIVR::Quaternion& GetClippingQuat() const { return m_q_cl; }
	// wx CalcFrame: the default crop frame (a centred square of the view;
	// the capture size while tile rendering). Device pixels, y from the top.
	void CalcFrame(int& x, int& y, int& w, int& h) const;
	// wx SetParams(t): apply the interpolated keys of the Advanced tab
	// (emits paramsApplied); AddKeyFrame: one key group with the current
	// view / group / volume / mesh state
	void SetParams(Interpolator* interpolator, double t);
	void AddKeyFrame(Interpolator* interpolator, double duration, int interpolation, bool record_volume_params);
	// wx Get4DSeqFrames / Set4DSeqFrame (other time points of a 4D series,
	// through the brick loader's prefetched data or the reader),
	// Get3DBatFrames / Set3DBatFrame (other files of the folder)
	void Get4DSeqFrames(int& start_frame, int& end_frame, int& cur_frame) const;
	void Set4DSeqFrame(int frame);
	// wx m_run_script / m_script_file: refreshed from the Settings (4D Script)
	// by every Set4DSeqFrame, read by the 4D script runner (MainWindow
	// Run4DScript, which needs both set and the file to exist)
	void SetScriptSettingsProvider(std::function<void(bool&, QString&)> f) { m_script_settings = std::move(f); }
	bool GetRun4DScript() const { return m_run_script; }
	QString GetScriptFile() const { return m_script_file; }
	void SetRun4DScript(bool run) { m_run_script = run; }
	void Get3DBatFrames(int& start_frame, int& end_frame, int& cur_frame);
	void Set3DBatFrame(int offset);
	int GetTSeqCurNum() const { return m_tseq_cur_num; }
	int GetTSeqPrvNum() const { return m_tseq_prv_num; }   // wx m_tseq_prv_num
	void SetTSeqCurNum(int t) { m_tseq_cur_num = t; }
	QString GetBatFolder() const { return m_bat_folder; }
	// wx m_recording / IsRecordedFrameReady / DownloadRecordedFrame. While
	// recording, RequestRecordedFrame redraws the view and the first frame
	// whose streaming loop is complete is copied (as presented, overlays
	// included, as wx) and read back; IsRecordedFrameReady() then turns true.
	void SetRecording(bool b);
	bool IsRecording() const { return m_recording; }
	void RequestRecordedFrame();
	bool IsRecordedFrameReady() const { return m_rec_ready; }
	// RGBA (or BGRA when bgr) rows top to bottom, w x h
	const std::vector<unsigned char>& GetRecordedFrame(int& w, int& h, bool& bgr) const
	{ w = m_rec_w; h = m_rec_h; bgr = m_rec_bgr; return m_rec_buf; }

	// ---- traces (RenderCanvasTraces.cpp; wx VRenderVulkanView
	// m_trace_group, the Components & Tracking dialog's track map)
	TraceGroup* GetTraceGroup() const { return m_trace_group.get(); }
	void CreateTraceGroup();                     // wx CreateTraceGroup (replaces it)
	bool LoadTraceGroup(const QString& file);    // wx LoadTraceGroup (.track)
	bool SaveTraceGroup(const QString& file);    // wx SaveTraceGroup
	// wx GetTraces: the labels selected by the current volume's mask become
	// the trace group's cell list (cur = prv = m_tseq_cur_num); emits
	// tracesUpdated (wx: TraceDlg::GetSettings(vrv))
	void GetTraces();
	// wx DrawTraces: its body is commented out in wx (nothing is drawn);
	// kept as the hook of Draw()
	void DrawTraces() {}

Q_SIGNALS:
	void tracesUpdated();
	// SetParams applied the keys at t (wx: ClippingView::SetVolumeData,
	// UpdateTree, RecorderDlg::SetSelection)
	void paramsApplied(double t);
	// Set4DSeqFrame / Set3DBatFrame replaced data (wx UpdateTreeFrames,
	// MeasureDlg::UpdateList); t = new m_tseq_cur_num
	void dataFramesChanged(int t);
	void recordedFrameReady();
	// wx VRenderVulkanView::OnIdle key handling forwarded to other panes
	void frameStepRequested(int dir);     // 'd' / space: +1 (MoviePanel UpFrame), 'a': -1 (DownFrame)
	void clipMoveRequested(int dir);      // 's': 1, 'w': 0 (ClipPanel::MoveLinkedClippingPlanes)
	void copyLevelRequested();            // Alt+V (wx OnIdle: CopyLevel(m_cur_vol), m_selector.SetVolume)
	void warpRequested();                 // Ctrl+B (wx OnIdle: WarpCurrentVolume)
	void cellFullRequested();             // f (wx OnIdle: TraceDlg::CellFull)
	void cellLinkRequested();             // l (wx OnIdle: TraceDlg::CellLink(false))
	// wx SetManipParams: ClippingView::SetVolumeData(cur sel vol), UpdateTree
	void manipParamsApplied();
	void freeModeChanged(bool free);      // free-fly entered / left (FreeFly box, AOV)
	void volMethodChanged(int method);
	void fogChanged(bool on, double intensity);
	void resModeChanged(int mode);
	void pyramidVolumeShown(bool shown);   // ToggleQualityDropDownBox
	// wx AddVolumeData / MoveLayertoGroup / MoveLayerfromtoGroup /
	// OrganizeLayers -> AdjustView::SetVolumeData(vd) (when vd), SetGroupLink(group), UpdateSync()
	void groupLinkChanged(VolumeData* vd, DataGroup* group);
	void rotationChanged(double rx, double ry, double rz);
	void zoomChanged(double scale);
	void vulkanReady();
	// files dropped onto the render surface (the main window loads them)
	void filesDropped(const QList<QUrl>& urls);
	void frameCaptured(const QString& path, bool ok);
	// wx OnMouse, viewing mode (no tool active): a left click without
	// dragging (LeftUp with m_pick -> Pick()) and a left double click
	// (LeftDClick -> SelSegVolume / SelLabelSegVolumeMax); widget coordinates
	void pickRequested(const QPoint& pos);
	void segmentPickRequested(const QPoint& pos);
	// wx OnMouse RightUp (outside the ruler mode): the view's context menu
	// (VRenderVulkanView::OnContextMenu); widget coordinates. Emitted for a
	// right click only -- a right drag zooms (wx Dragging + RightIsDown) and
	// does not open the menu afterwards.
	void contextMenuRequested(const QPoint& pos);
	// a mouse button went down in the view (MainWindow: the active view)
	void viewActivated();
	void captureFinished(const QString& file, bool ok);   // StartCapture / each group
	void groupCaptureFinished();

protected:
	friend class VulkanSurfaceWindow;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void wheelEvent(QWheelEvent*) override;
	void keyPressEvent(QKeyEvent*) override;
	void keyReleaseEvent(QKeyEvent*) override;

private:
	bool InitVulkan();
	void RequestRender();
	void OnSurfaceResized();
	void OnExpose(bool exposed);
	void Render();
	void HandleProjection(int nx, int ny);
	void HandleCamera();
	void ComputeBounds();
	void PrepBuffers(int nx, int ny);
	void DrawVolumes(int nx, int ny);
	bool DrawMeshes(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, int peel,
		const std::shared_ptr<vks::VTexture> depth_tex = nullptr);
	void DrawMeshLayers(int nx, int ny);
	void DrawOLShadowsMesh(const std::shared_ptr<vks::VTexture>& tex_depth, double darkness, int nx, int ny);
	void PopMeshList();
	// tex: what to present (default m_tex_final; the Depth-mode mid-pass
	// mesh preview passes its own buffer)
	void DrawFinalBuffer(bool clear, vks::VTexture* tex = nullptr);
	void ClearScreen();
	QSize DeviceSize() const;

	// wx m_nx/m_ny: the tile size while tile rendering, else DeviceSize()
	QSize RenderSize() const { return m_tile_rendering ? QSize(m_tile_w, m_tile_h) : DeviceSize(); }

	VulkanSurfaceWindow* m_window = nullptr;   // owned by m_container
	QWidget* m_container = nullptr;

	std::shared_ptr<VVulkan> m_vulkan;
	std::shared_ptr<Vulkan2dRender> m_v2drender;
	std::unique_ptr<vks::VFrameBuffer> m_fbo;        // per-volume scratch
	std::shared_ptr<vks::VTexture> m_tex;
	std::unique_ptr<vks::VFrameBuffer> m_fbo_final;  // composited scene
	std::shared_ptr<vks::VTexture> m_tex_final;
	// mesh depth-peeling layers (colour + depth each) and shadow scratch
	std::vector<std::unique_ptr<vks::VFrameBuffer>> m_dp_fbo_list;
	std::vector<std::shared_ptr<vks::VTexture>> m_dp_tex_list;    // depth
	std::vector<std::shared_ptr<vks::VTexture>> m_dp_ctex_list;   // colour
	std::unique_ptr<vks::VFrameBuffer> m_fbo_ol2;
	std::shared_ptr<vks::VTexture> m_tex_ol2;
	bool m_clear_final_buffer = true;
	int m_finished_peeling_layer = 0;
	int m_peeling_layers = 1;
	bool m_init_failed = false;
	bool m_rendering = false;   // Render() in progress (re-entrancy guard)
	// last presented frame (OnExpose): the swapchain keeps showing it, so an
	// expose of an unchanged, already exposed surface needs no new frame
	int m_render_suspended = 0;   // SuspendRendering depth
	bool m_frame_presented = false;
	long long m_presented_frames = 0;   // frames submitted (testing: VVD_QT_SPIN)
	bool m_surface_exposed = false;
	QSize m_presented_size;
	bool m_shutdown = false;    // Shutdown() called; never render again

	std::vector<TreeLayer*> m_layer_list;     // wx m_layer_list (groups owned)
	std::vector<VolumeData*> m_volumes;       // derived: all volumes in layer order
	VolumeData* m_cur_vol = nullptr;
	std::vector<MeshData*> m_meshes;          // derived: all meshes in layer order
	bool m_draw_all = true;                   // wx m_draw_all
	std::function<void(DataGroup*)> m_group_defaults;
	void SyncFlatLists();                     // m_volumes / m_meshes from m_layer_list
	void LayersChanged(bool bounds);          // SyncFlatLists (+ ComputeBounds), redraw
	void ApplyGroupSettings(DataGroup* group, VolumeData* vd);   // wx AddVolumeData / MoveLayertoGroup body
	void DrawLayerVolumes(int peel, bool dp_variant);           // wx DrawVolumes / DrawVolumesDP layer loop
	std::vector<MeshData*> m_md_pop_list;   // visible meshes, rebuilt per frame
	MeshData* m_cur_mesh = nullptr;

	// camera
	FLIVR::BBox m_bounds;
	double m_radius = 5.0;
	double m_near_clip = 0.1;
	double m_far_clip = 100.0;
	double m_aov = 15.0;
	double m_scale_factor = 1.0;
	double m_distance = 10.0;
	double m_init_dist = 10.0;           // wx m_init_dist
	int m_min_ppi = 20;                  // wx m_min_ppi
	double m_transx = 0.0, m_transy = 0.0, m_transz = 10.0;
	FLIVR::Vector m_up{ 0.0, 1.0, 0.0 };
	double m_obj_ctrx = 0.0, m_obj_ctry = 0.0, m_obj_ctrz = 0.0;
	double m_obj_transx = 0.0, m_obj_transy = 0.0, m_obj_transz = 0.0;
	double m_obj_rotx = 0.0, m_obj_roty = 0.0, m_obj_rotz = 0.0;
	// view rotation (degrees) and its quaternion, as in VRenderVulkanView
	double m_rotx = 0.0, m_roty = 0.0, m_rotz = 0.0;
	FLIVR::Quaternion m_q;
	std::vector<CanvasTool*> m_tools;
	bool m_tablet_down = false;
	double m_tablet_pressure = 1.0;
	QString m_capture_path;
	std::unique_ptr<vks::VFrameBuffer> m_fbo_record;
	std::shared_ptr<vks::VTexture> m_tex_record;
	void RecordFrame(int nx, int ny);
	void SaveRecordedFrame();
	bool WriteRecordedFrame(const QString& path);
	FLIVR::Quaternion Trackball(int p1x, int p1y, int p2x, int p2y);
	void UpdateCameraFromQ();   // m_trans*, m_up from m_q (wx SetRotations / OnMouse)
	void Q2A();
	CanvasTool* ActiveTool() const;
	double m_ortho_left = -1, m_ortho_right = 1, m_ortho_bottom = -1, m_ortho_top = 1;
	bool m_persp = false;
	bool m_interactive = false;
	FLIVR::Color m_bg_color{ 0.0, 0.0, 0.0 };
	glm::mat4 m_mv_mat{ 1.0f };
	glm::mat4 m_proj_mat{ 1.0f };
	glm::mat4 m_tex_mat{ 1.0f };
	int m_vol_method_blend = 1;   // V2DRENDER_BLEND_OVER (Layered, the wx default); ADD in Composite

	// ---- blend modes / depth peeling / fog (RenderCanvasModes.cpp)
	int m_vol_method = VolMethodSeq;     // wx default (VRenderVulkanView ctor)
	bool m_dpeel = false;                // wx m_dpeel (set by StartLoopUpdate)
	int m_peeling_layer_progress = 0;    // wx m_peeling_layer_progress
	bool m_use_fog = true;               // wx m_use_fog
	bool m_use_fog_mesh = true;          // wx m_use_fog_mesh
	double m_fog_intensity = 0.0;
	double m_fog_start = 0.0;
	double m_fog_end = 0.0;
	bool m_blend_slices = false;         // wx m_blend_slices
	bool m_has_brxml = false;
	std::shared_ptr<FLIVR::MultiVolumeRenderer> m_mvr;   // wx m_mvr
	std::shared_ptr<vks::VTexture> m_vol_dp_fr_tex;      // wx m_vol_dp_fr_tex
	std::shared_ptr<vks::VTexture> m_vol_dp_bk_tex;      // wx m_vol_dp_bk_tex
	std::unique_ptr<vks::VFrameBuffer> m_fbo_ol1;        // wx DrawMIP scratch
	std::shared_ptr<vks::VTexture> m_tex_ol1;
	void CalcFogRange();
	void DrawVolumesComp(const std::vector<VolumeData*>& list, bool mask, int peel);
	void EnsureWt2Buffer(int nx, int ny);
	void SharedMaskLabel(VolumeData* vd, FLIVR::Texture*& ext_msk, FLIVR::Texture*& ext_lbl) const;
	void DrawMIP(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer>& fb, int peel, double sampling_frq_fac);
	void DrawVolumesMulti(const std::vector<VolumeData*>& list, int peel);
	// wx DrawVolumes "setup" part: dispatch on m_vol_method
	void DrawVolumesByMethod(int peel);
	// wx DrawVolumesDP (m_dpeel: Depth mode with meshes), incl. final buffer
	void DrawVolumesDP(int nx, int ny);
	void EnsureMeshLayerBuffers(int count, int nx, int ny);
	// Meshes in multi-frame streaming passes (not in wx, where meshes vanish
	// from the later frames of a pass): the mesh image the first frame of a
	// pass drew into the final buffer is copied to m_fbo_temp and becomes the
	// base the first volume channel is restored onto in the later frames
	// (RenderCanvasStreaming.cpp DrawScene).
	void SaveFinalToTemp(int nx, int ny);
	bool m_mesh_base_saved = false;
	// Depth mode with meshes (DrawVolumesDP): the mesh colour layers of the
	// slabs not finished yet are composited behind a copy of the final
	// buffer for the presented frame while the pass is running
	vks::VTexture* DrawDPMeshPreview(int nx, int ny, int peeling_layers);
	std::unique_ptr<vks::VFrameBuffer> m_fbo_dp_preview;
	std::shared_ptr<vks::VTexture> m_tex_dp_preview;
	void ResizeMultiVolumeRenderer();
	// view-level 2D adjustment used by Depth mode (wx m_gamma/m_brightness/m_hdr)
	void ViewAdjustment(FLIVR::Color& gamma, FLIVR::Color& brightness, FLIVR::Color& hdr) const;

	// ---- memory swap / streaming (RenderCanvasStreaming.cpp)
	void InitStreaming();                         // once, after Vulkan is up
	void ShutdownStreaming();                     // stop loader threads
	void StartLoopUpdate(bool reset_peeling_layer = true);
	void HaltLoopUpdate();
	void SetSortBricks();
	void switchLevel(VolumeData* vd);
	// wx Draw(): loop restart check before the scene is drawn
	void StreamingBeginFrame();
	// wx DrawVolumes() (composite, no tiles): meshes, volumes, final buffer
	// to the swapchain, end-of-frame loop bookkeeping
	void DrawScene(int nx, int ny);
	// wx DrawOVER: one volume into fb and composited into the final buffer
	void DrawVolumeOver(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer>& fb, double sampling_frq_fac, int peel = 0);
	// wx OnIdle: schedule the next frame while the loop is not done
	void StreamingIdle();
	void RequestFrame();                          // requestUpdate only (RenderCanvas.cpp)
	bool m_updating = true;              // wx m_updating
	bool m_draw_overlays_only = false;   // wx m_draw_overlays_only
	bool m_refresh_start_loop = false;   // wx m_refresh_start_loop
	bool m_start_loop_pending = true;    // RefreshGL(.., true) coalesced to the next frame
	bool m_int_res = false;              // wx m_int_res
	bool m_adaptive = true;              // wx m_adaptive
	bool m_adaptive_res = false;         // wx m_adaptive_res
	bool m_draw_mask = true;             // wx m_draw_mask
	bool m_force_hide_mask = false;      // wx m_force_hide_mask
	bool m_key_v = false;
	bool m_pick = false;                 // wx m_pick: left press, not dragged yet
	QPoint m_pick_pos;                   // wx old_mouse_X / old_mouse_Y of the press
	bool m_rclick = false;               // right press, not dragged (zoomed) yet
	QPoint m_rclick_pos;
	// wx m_fbo_pick / m_tex_pick / m_tex_pick_depth (PickMesh)
	std::unique_ptr<vks::VFrameBuffer> m_fbo_pick;
	std::shared_ptr<vks::VTexture> m_tex_pick;
	std::shared_ptr<vks::VTexture> m_tex_pick_depth;                // V held: masks hidden (wx OnIdle)
	bool m_enhance_sel = true;           // wx m_enhance_sel
	std::function<VolumeData*(const VVDString&)> m_volume_lookup;
	// wx m_fbo_wt2 / m_tex_wt2: the selected volume (with a mask) is drawn
	// here instead of m_fbo so that its image survives the mask pass
	std::unique_ptr<vks::VFrameBuffer> m_fbo_wt2;
	std::shared_ptr<vks::VTexture> m_tex_wt2;
	bool m_draw_label = false;           // wx m_draw_label
	bool m_ebd_run = true;               // wx m_ebd_run
	bool m_mdtrans = false;              // wx m_mdtrans
	int m_res_mode = 0;                  // wx m_res_mode
	bool m_streaming_init = false;
	// wx m_fbo_temp / m_fbo_temp_restore: final buffer saved before the
	// channel currently being streamed, restored every frame
	std::unique_ptr<vks::VFrameBuffer> m_fbo_temp;
	std::shared_ptr<vks::VTexture> m_tex_temp;
	std::unique_ptr<vks::VFrameBuffer> m_fbo_temp_restore;
	std::unique_ptr<VolumeLoader> m_loader;       // wx m_loader
	std::unique_ptr<EmptyBlockDetector> m_ebd;    // wx m_ebd

	// interaction
	QPoint m_last_pos;
	Qt::MouseButtons m_drag_buttons = Qt::NoButton;

	// ---- RenderCanvasCamera.cpp: camera modes, keyboard, clipping planes,
	// view 2D adjustment
	void OnCameraDrag(QMouseEvent* e);     // wx OnMouse dragging (m_int_mode 1)
	void OnCameraWheel(QWheelEvent* e);    // wx OnMouse wheel
	// wx OnIdle key state (application-wide, see SetKeyLock); press = key
	// down. Returns true when the key is one of the polled keys.
	bool OnViewKey(QKeyEvent* e, bool press);
	class ViewKeyFilter;
	friend class ViewKeyFilter;
	ViewKeyFilter* m_key_filter = nullptr;
	void CreateViewKeyFilter();
	bool m_alt_v_down = false;             // Alt+V / Ctrl+B handled once per press
	bool m_ctrl_b_down = false;
	bool m_mouse_focus = false;            // wx m_mouse_focus
	bool m_intp = true;                    // wx m_intp
	double m_shadow_dir_x = 0.0, m_shadow_dir_y = 0.0;   // SettingDlg GetShadowDir
	bool m_grad_bg = false;                // wx m_grad_bg
	void DrawGradBg();
	bool m_test_speed = false;             // wx m_test_speed
	bool m_test_wiref = false;             // wx m_test_wiref
	bool m_draw_bounds = false;            // wx m_draw_bounds (DrawBounds: empty in wx)
	bool m_draw_grid = false;              // wx m_draw_grid
	void DrawGrid();                       // wx DrawGrid (RenderCanvasCamera.cpp)
	std::function<FLIVR::Color()> m_text_color;
	std::function<ClipSlabInfo()> m_clip_slab;
	std::function<bool(const QPoint&)> m_dblclick_hook;
	// wx DrawOLShadows (volume shadows: depth map of the shadow volumes ->
	// gradient -> darkened fb)
	void DrawOLShadows(const std::vector<VolumeData*>& vlist, std::unique_ptr<vks::VFrameBuffer>& fb);
	// wx m_manip* (StartManipulation): the interpolator is created on use
	bool m_manip = false;
	double m_manip_fps = 20.0;
	int m_manip_time = 300;
	int m_manip_cur_num = 0;
	double m_manip_end_frame = 0.0;
	unsigned int m_saved_uptime = 0;
	bool m_pre_draw = true;                // wx m_pre_draw
	std::shared_ptr<Interpolator> m_manip_interpolator;
	void SetManipKey(double t, int interpolation, const FLIVR::Point* view_trans, const FLIVR::Point* view_center,
		const FLIVR::Point* view_rot, const FLIVR::Point* obj_trans, const double* scale);
	void SetManipParams(double t);
	void PreDraw();                        // wx PreDraw (manipulation step)
	// wx m_anno_fbo / m_anno_tex / m_anno_dp_tex (DrawAnnotations, mesh layers)
	std::unique_ptr<vks::VFrameBuffer> m_anno_fbo;
	std::shared_ptr<vks::VTexture> m_anno_tex;
	std::shared_ptr<vks::VTexture> m_anno_dp_tex;
	double CalcCameraDistance();
	double FreeScaleFactor();              // wx HandleProjection, m_free branch
	void RestoreFreeSaved();               // wx SetFree(false) / SetPersp(false)
	void PropagateLinkedRotation();        // wx SetRotations link_update
	void ApplyClipPlaneTransforms();       // body of wx A2Q / Q2A (sync mode)
	void DrawClippingPlanes(bool border, int face_winding);
	static void RegisterCanvas(RenderCanvas* c, bool add);
	// several canvases (MakeCurrent)
	void* m_surface_state = nullptr;           // VVulkan::SurfaceState of this canvas
	bool m_secondary = false;                  // shares another canvas's context
	struct LoopState
	{
		bool start_update_loop = false, done_update_loop = true, done_current_chan = true;
		int total_brick_num = 0, cur_brick_num = 0, cur_chan_brick_num = 0, cur_tid_offset_multi = 0;
		bool clear_chan_buffer = true, save_final_buffer = true, valid_temp_final_buffer = true;
		unsigned long st_time = 0, cor_up_time = 100, consumed_time = 0;
		bool interactive = false;
		int finished_bricks = 0;
	};
	LoopState m_loop_state;                    // valid while another canvas owns the statics
	static RenderCanvas* s_loop_owner;         // whose loop state is in TextureRenderer's statics
	static RenderCanvas* s_key_canvas;
	void BindLoopState();
	bool KeyActionsHere() const { return !s_key_canvas || s_key_canvas == this; }
	FLIVR::Color m_gamma{ 1.0, 1.0, 1.0 };
	FLIVR::Color m_brightness{ 1.0, 1.0, 1.0 };
	FLIVR::Color m_hdr{ 0.0, 0.0, 0.0 };
	FLIVR::Color m_levels{ 1.0, 1.0, 1.0 };
	bool m_sync_r = false, m_sync_g = false, m_sync_b = false;
	bool m_easy_2d_adjust = false;
	bool m_free = false;
	bool m_rot_lock = false;
	bool m_linked_rot = false;
	bool m_use_dft_settings = false;
	bool m_key_lock = false;
	bool m_frame_clear = true;             // wx m_frame_clear (swapchain not cleared yet this frame)
	double m_ctrx = 0.0, m_ctry = 0.0, m_ctrz = 0.0;
	double m_ctrx_saved = 0.0, m_ctry_saved = 0.0, m_ctrz_saved = 0.0;
	double m_transx_saved = 0.0, m_transy_saved = 0.0, m_transz_saved = 0.0;
	double m_rotx_saved = 0.0, m_roty_saved = 0.0, m_rotz_saved = 0.0;
	double m_obj_transx_saved = 0.0, m_obj_transy_saved = 0.0, m_obj_transz_saved = 0.0;
	double m_scale_factor_saved = 1.0;
	// view clipping state (wx VRenderVulkanView ClippingLayer-like members)
	bool m_sync_clipping_planes = false;
	int m_clip_mode = 2;
	FLIVR::Quaternion m_q_cl, m_q_cl_zero, m_q_cl_fix, m_q_fix;
	double m_rotx_cl = 0.0, m_roty_cl = 0.0, m_rotz_cl = 0.0;
	double m_rotx_cl_fix = 0.0, m_roty_cl_fix = 0.0, m_rotz_cl_fix = 0.0;
	double m_rotx_fix = 0.0, m_roty_fix = 0.0, m_rotz_fix = 0.0;
	FLIVR::Vector m_trans_fix{ 0.0, 0.0, 0.0 };
	double m_linked_plane_params[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
	bool m_link_x_chk = false, m_link_y_chk = false, m_link_z_chk = false;
	int m_clip_dist_x = 1, m_clip_dist_y = 1, m_clip_dist_z = 1;
	bool m_draw_clip = false;
	int m_clip_mask = -1;
	int m_clip_plane_mode = 0;
	bool m_clip_chann_link = false;
	int m_clip_sel_type = -1;
	VolumeData* m_clip_vd = nullptr;
	MeshData* m_clip_md = nullptr;

	// ---- still capture (RenderCanvasCapture.cpp)
	struct CaptureState;                       // GPU buffers, tile image, group queue
	std::shared_ptr<CaptureState> m_cap;       // created on first use
	CaptureState& Cap();
	void ResizeRenderers();                    // wx Resize(): vr->resize() for the new size
	void ApplyTileProjection(int nx, int ny);  // wx HandleProjection tile offsets
	void DrawTiledFinalBuffer();               // wx tile composition + DrawTile + tile advance
	void DrawTile();
	void CaptureSwapchain(int nx, int ny);     // wx PostDraw point, inside the frame
	void CapturePostDraw();                    // wx PostDraw, after the frame was submitted
	void SetupNextGroupCapture();
	void ReleaseCaptureResources();
	QString m_cap_file;                        // wx m_cap_file
	bool m_capture = false;                    // wx m_capture
	bool m_cap_compress = true;                // wx VRenderFrame::GetCompression()
	bool m_capture_groups = false;             // wx m_capture_groups
	bool m_postdraw = false;                   // wx m_postdraw (a tile is ready)
	int m_capture_resx = -1, m_capture_resy = -1;
	bool m_tile_rendering = false;
	int m_tile_w = 0, m_tile_h = 0, m_tile_xnum = 1, m_tile_ynum = 1, m_current_tileid = 0;
	int m_tmp_res_mode = 0;
	bool m_draw_frame = false;
	int m_frame_x = 0, m_frame_y = 0, m_frame_w = 0, m_frame_h = 0;

	// ---- movie (RenderCanvasMovie.cpp)
	QString m_view_name = QStringLiteral("Render View:1");
	int m_tseq_cur_num = 0, m_tseq_prv_num = 0;   // wx m_tseq_cur_num / m_tseq_prv_num
	bool m_run_script = false;
	QString m_script_file;
	std::function<void(bool&, QString&)> m_script_settings;
	QString m_bat_folder;                          // wx m_bat_folder
	bool m_recording = false;
	bool m_rec_requested = false;
	bool m_rec_ready = false;
	std::vector<unsigned char> m_rec_buf;
	int m_rec_w = 0, m_rec_h = 0;
	bool m_rec_bgr = false;
	void ReadRecordedFrame();
	std::unique_ptr<TraceGroup> m_trace_group;     // wx m_trace_group
};

#endif // VVD_QT_RENDERCANVAS_H
