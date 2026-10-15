#ifndef VVD_QT_RULERTOOL_H
#define VVD_QT_RULERTOOL_H

// Rulers / locators for the Qt canvas: port of the ruler part of
// VRenderVulkanView (VRenderView.cpp).
//
//   wx                                       here
//   m_int_mode 5 (ruler creation)            SetIntMode(5) + SetRulerType()
//   m_int_mode 6 (select & move points)      SetIntMode(6)
//   m_int_mode 1 (camera)                    SetIntMode(1) (tool inactive)
//   m_ruler_list                             GetRulerList() (owned here)
//   AddRulerPoint / FinishRuler /            same names
//   GetRulerFinished / GetEditingRulerPoint /
//   SwitchRulerBalloonVisibility_Point
//   GetPointVolume / GetPointAndIntVolume /  same names and arguments
//   GetPointVolumeBox / GetPointVolumeBox2 /
//   GetPointPlane
//   DrawRulers (incl. landmarks, balloons)   DrawOverlay() -> DrawRulers()
//   m_text_renderer                          GetTextRenderer() (created on the
//                                            first frame, like the wx view
//                                            creates it right after InitVulkan)
//   ImportBigWarpCSV / ExportBigWarpCSV /    same names
//   ScatterRulers / WarpCurrentVolumeInternal (WarpCurrentVolume here; the
//                                            caller adds the result volume)
//   m_point_volume_mode, m_ruler_use_transf, Set/Get... (SettingDlg
//   m_ruler_time_dep, m_tseq_cur_num         "ruler_use_transf"/"ruler_time_dep")
//   m_draw_rulers, m_draw_landmarks          SetDrawRulers / SetDrawLandmarks
//
// The wx picking functions recompute the projection / camera matrices of the
// view (HandleProjection + HandleCamera + object transform). The canvas
// already has exactly those matrices while it draws the overlays, so they
// are cached in DrawOverlay() and used for picking: what is picked is what is
// on screen.
//
// Mouse handling follows VRenderVulkanView::OnMouse:
//   mode 5: left click adds a point (Alt+drag = camera), right click
//           finishes a polyline ruler, or leaves ruler mode when the last
//           ruler is finished (wx SetIntMode(1)).
//   mode 6: left press on a ruler point grabs it, dragging moves it
//           (re-picked in the volume with the pointing method), any other
//           drag is the camera.

#include <QObject>
#include <QPointer>
#include <QString>
#include <vector>
#include <memory>

#include <glm/glm.hpp>

#include "CanvasTool.h"
#include "FLIVR/Point.h"
#include "FLIVR/Color.h"
#include "Vulkan/Vulkan2dRender.h"

class RenderCanvas;
class VolumeData;
class Ruler;
class TextRenderer;
namespace FLIVR { class ThinPlateSpline; }
namespace rigidcells { struct Params; struct Report; }

class RulerTool : public QObject, public CanvasTool
{
	Q_OBJECT
public:
	// Registers itself on the canvas (canvas->AddTool(this)).
	explicit RulerTool(RenderCanvas* canvas, QObject* parent = nullptr);
	~RulerTool() override;

	RenderCanvas* Canvas() const;

	// ---- interaction mode (wx m_int_mode: 1 camera, 5 ruler creation, 6 edit)
	int GetIntMode() const { return m_int_mode; }
	void SetIntMode(int mode);
	int GetRulerType() const { return m_ruler_type; }   // 0 2pt, 1 multi-point, 2 locator, 3 probe, 4 protractor
	void SetRulerType(int type) { m_ruler_type = type; }
	// wx VRenderVulkanView::GetTextColor (Settings text colour mode)
	FLIVR::Color GetTextColor();
	// wx VRenderVulkanView::AddPaintRulerPoint: the centre of the brush
	// selection (VolumeSelector::ProcessSel(0.01)) becomes the next point of
	// the unfinished ruler (or of a new one); info "v<n>" = selected size
	void AddPaintRulerPoint(class VolumeSelector* sel);

	// ---- options (VRenderVulkanView members of the same name)
	int GetPointVolumeMode() const { return m_point_volume_mode; }   // 0 view plane, 1 max int, 2 accumulated
	void SetPointVolumeMode(int mode) { m_point_volume_mode = mode; }
	bool GetRulerUseTransf() const { return m_ruler_use_transf; }
	void SetRulerUseTransf(bool val) { m_ruler_use_transf = val; }
	bool GetRulerTimeDep() const { return m_ruler_time_dep; }
	void SetRulerTimeDep(bool val) { m_ruler_time_dep = val; }
	// Measurement "Auto-select" (not in wx; default off = wx behaviour): every
	// click that creates or moves a ruler point (modes 5 / 6, the stroke of a
	// paint ruler in mode 7) first picks AutoSelectVolume(mx, my), which then
	// is the volume the point is placed on (with the pointing method; the
	// View Plane method still places it on the view plane) and the point's
	// volume (Start / End Volume)
	bool GetRulerAutoSelect() const { return m_ruler_auto_select; }
	void SetRulerAutoSelect(bool val) { m_ruler_auto_select = val; }
	// With Auto-select on: among the displayed volumes of the view
	// (PopVolumeList, shown ones), the one whose brightest voxel on the ray
	// under (mx, my) is the brightest - GetPointVolume Maximum Intensity with
	// the voxel value after the volume's transfer function (normalized 0..1,
	// what is drawn) inside its clipping planes; ties keep the current
	// volume, else the first in layer order. A volume other than the current
	// one is announced with volumeAutoSelected (the main window selects it in
	// the Workspace tree; without a receiver it is made the canvas's current
	// volume). Returns null (Auto-select off, no volume hit): the caller
	// falls back to the current volume / view plane.
	VolumeData* AutoSelectVolume(int mx, int my);
	// current time point (wx m_tseq_cur_num); time-dependent ("transient")
	// rulers are shown/listed only at their time. Emits rulersChanged().
	int GetCurTime() const { return m_tseq_cur_num; }
	void SetCurTime(int t);
	bool GetDrawRulers() const { return m_draw_rulers; }
	void SetDrawRulers(bool b);
	// landmarks of the displayed volumes (wx SetLandmarkVisibility / m_draw_landmarks)
	bool GetDrawLandmarks() const { return m_draw_landmarks; }
	void SetDrawLandmarks(bool b);
	// SettingDlg "text color": 0 background inverted, 1 background, 2 secondary
	// colour of the current volume (wx VRenderVulkanView::GetTextColor)
	void SetTextColorMode(int mode) { m_text_color_mode = mode; Refresh(); }
	int GetTextColorMode() const { return m_text_color_mode; }

	// ---- text renderer (wx m_text_renderer). The font is loaded on the first
	// frame (the Vulkan device must exist). Default: <Fonts dir>/FreeSans.ttf,
	// size 14 (TextRenderer default). SetFont reloads the face (wx
	// VRenderFrame: LoadNewFace(path) + SetSize(size)).
	void SetFont(const QString& path, int size);
	void SetTextSize(int size);
	TextRenderer* GetTextRenderer() const { return m_text_renderer.get(); }

	// ---- ruler list (owned: the Ruler objects are deleted by this tool)
	std::vector<Ruler*>* GetRulerList() { return &m_ruler_list; }
	// delete all rulers (no signal) - e.g. before ProjectIO::LoadProject
	void ClearRulers();
	// the list was modified from outside (project load, list dialog edits):
	// drops a grabbed point, redraws, emits rulersChanged()
	void RulersModified();

	void FinishRuler();
	bool GetRulerFinished();
	void AddRulerPoint(int mx, int my);
	Ruler* GetLastRuler() { return m_ruler_list.empty() ? nullptr : m_ruler_list.back(); }

	// ---- picking (verbatim ports; mx/my in canvas coordinates, top-left origin)
	// max_value (mode 1, optional): the maximum value found on the ray
	double GetPointVolume(FLIVR::Point& mp, int mx, int my, VolumeData* vd, int mode, bool use_transf, double thresh = 0.5, double* max_value = nullptr);
	double GetPointAndIntVolume(FLIVR::Point& mp, double& intensity, bool normalize, int mx, int my, VolumeData* vd, double thresh = 0.5);
	// wx GetPointAndLabelMax (NA mode): along the ray under (mx, my), the
	// label of vd (shown segments only) at the voxel where the ref volumes'
	// original intensity is highest; returns the distance from the near
	// plane (-1: none), lblval = that label
	double GetPointAndLabelMax(FLIVR::Point& mp, int& lblval, int mx, int my, VolumeData* vd, const std::vector<VolumeData*>& ref);
	double GetPointVolumeBox(FLIVR::Point& mp, int mx, int my, VolumeData* vd, bool calc_mats = true);
	double GetPointVolumeBox2(FLIVR::Point& p1, FLIVR::Point& p2, int mx, int my, VolumeData* vd);
	double GetPointPlane(FLIVR::Point& mp, int mx, int my, FLIVR::Point* planep = nullptr, bool calc_mats = true);
	FLIVR::Point* GetEditingRulerPoint(int mx, int my);
	bool SwitchRulerBalloonVisibility_Point(int mx, int my);

	// ---- MeasureDlg actions that live in the wx view
	// Replaces the ruler list with the landmark pairs of a BigWarp csv.
	bool ImportBigWarpCSV(const QString& path);
	// Writes the 2-point rulers / locators as BigWarp landmarks. Returns false
	// when there is nothing to write or the file cannot be written.
	bool ExportBigWarpCSV(const QString& path);
	// Locators scattered uniformly (density = number of points) inside the
	// current volume's clipping box and mask.
	void ScatterRulers(long density);
	// WarpCurrentVolumeInternal without the "add to the scene" part: returns
	// the new volume (not yet added to the DataManager / canvas / tree) or
	// nullptr with *error set (the wx message box text). ttype: 0 TPS,
	// 1 affine, 2 similarity, 3 rigid, 4 translation; interp: 0 NN, 1 linear.
	// target: the output grid (null = the current volume's own grid; see
	// WarpVolumeByRulers in DataManager.h)
	VolumeData* WarpCurrentVolume(int ttype, double lambda, int interp, QString* error, VolumeData* target = nullptr);
	// The same with the "Cells: shape preserving" model (RigidCellsWarp.h,
	// port of the ImageJ plugin Apply_BigWarp_RigidCells): returns the new
	// volume or nullptr with *error set (empty when cancelled); *report gets
	// the object / landmark tables and the log.
	VolumeData* WarpCurrentVolumeCells(const rigidcells::Params& params, rigidcells::Report* report, QString* error,
		VolumeData* target = nullptr);
	// the current volume (the warp source)
	VolumeData* CurrentVolume() const { return CurVol(); }

	// ---- CanvasTool
	bool IsActive() const override { return m_int_mode == 5 || m_int_mode == 6; }
	bool MousePress(QMouseEvent* e) override;
	bool MouseMove(QMouseEvent* e) override;
	bool MouseRelease(QMouseEvent* e) override;
	void DrawOverlay(RenderCanvas* canvas, int nx, int ny) override;

Q_SIGNALS:
	// ruler list or a ruler changed (wx: MeasureDlg::GetSettings(m_vrv))
	void rulersChanged();
	// interaction mode changed from the canvas side (e.g. right click left
	// ruler mode); mode as in GetIntMode()
	void intModeChanged(int mode);
	// AutoSelectVolume picked a volume that is not the current one; the
	// receiver selects it in the Workspace tree (a user click on it)
	void volumeAutoSelected(VolumeData* vd);

private:
	void Refresh();
	void EnsureTextRenderer();
	void DrawRulers();
	VolumeData* CurVol() const;
	// picking state: the matrices / size / camera distance of the last frame
	bool PickState(int& nx, int& ny) const;

	QPointer<RenderCanvas> m_canvas;

	std::vector<Ruler*> m_ruler_list;
	std::vector<Ruler*> m_landmarks;
	std::vector<VolumeData*> m_lm_vdlist;
	FLIVR::Point* m_editing_ruler_point = nullptr;

	int m_int_mode = 1;
	int m_ruler_type = 0;
	int m_point_volume_mode = 0;
	bool m_ruler_use_transf = false;
	bool m_ruler_time_dep = true;
	bool m_ruler_auto_select = false;
	// volume auto-selected at the press of a paint-ruler stroke (mode 7), the
	// volume of the point AddPaintRulerPoint adds
	VolumeData* m_paint_auto_vd = nullptr;
public:
	// BrushTool mode 7 press hook: AutoSelectVolume + remember it for
	// AddPaintRulerPoint; returns true when the current volume changed
	bool AutoSelectForPaintRuler(int mx, int my);
private:
	int m_tseq_cur_num = 0;
	bool m_draw_rulers = true;
	bool m_draw_landmarks = false;
	int m_text_color_mode = 0;

	// text
	std::unique_ptr<TextRenderer> m_text_renderer;
	QString m_font_path;
	int m_font_size = 0;    // 0 = TextRenderer default
	bool m_font_dirty = false;

	// geometry of the last overlay draw
	Vulkan2dRender::V2dObject m_ruler_vobj;
	glm::mat4 m_mv_mat{ 1.0f };
	glm::mat4 m_proj_mat{ 1.0f };
	double m_distance = 10.0;
	int m_nx = 0, m_ny = 0;
	bool m_have_frame = false;

	// mouse state
	bool m_camera_drag = false;   // press handed to the canvas (camera)
public:
	// wx OnMouse RightUp: int mode 5 without Alt finishes the ruler
	bool HandlesRightClick(QMouseEvent* e) const override;
private:
	bool m_point_drag = false;    // dragging m_editing_ruler_point
};

#endif // VVD_QT_RULERTOOL_H
