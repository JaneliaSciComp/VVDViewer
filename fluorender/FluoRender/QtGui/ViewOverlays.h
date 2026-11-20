#ifndef VVD_QT_VIEWOVERLAYS_H
#define VVD_QT_VIEWOVERLAYS_H

// Render-view overlays for the Qt canvas: port of the overlay part of
// VRenderVulkanView::OnDraw (VRenderView.cpp).
//
//   wx                                       here
//   m_draw_camctr / m_camctr_size            SetDrawCamCtr / SetCamCtrSize   (DrawCamCtr)
//   m_draw_legend, m_sb_height               SetDrawLegend                   (DrawLegend, DrawName;
//                                            not in wx: SetNamesOnObjects, DrawObjectNames)
//   m_disp_scale_bar, m_disp_scale_bar_text, EnableScaleBar / EnableSBText /  (DrawScaleBar)
//   m_sb_length, m_sb_text, m_sb_unit,       SetScaleBarLen / SetSBText /
//   m_sb_num, m_sclbar_digit,                SetSbUnitSel / SetSbNumText /
//   m_fix_sclbar, m_fixed_sclbar_fac         SetScaleBarDigit / FixScaleBarLen /
//                                            GetScaleBarFixed (same names)
//   DrawColormap / SetColormapColors         always, like wx
//   m_draw_frame, m_frame_x/y/w/h            SetDrawFrame / SetFrame         (DrawFrame)
//   m_draw_info, m_draw_coord, goTimer       SetDrawInfo / SetDrawCoord      (DrawInfo)
//   GetTextColor (SettingDlg "text color")   text colour mode of the RulerTool (shared
//                                            setting), or SetTextColorMode without one
//   m_text_renderer                          RulerTool::GetTextRenderer() (shared), or
//                                            an own one created the same way
//
// Draw order (DrawOverlay) is the one of OnDraw: CamCtr, Legend, ScaleBar,
// Colormap, Frame, Info. The tool is never active (no mouse capture); it
// follows the mouse through CanvasTool::HoverMove for the Info coordinates
// and redraws on mouse moves while they are shown (wx OnMouse: m_draw_coord
// -> RefreshGL(false, false)). With Names on Objects placed by hand
// (SetNamesManual) a press on a name takes the drag (CanvasTool::GrabPress)
// and moves that name; the cursor is an open hand over a name.

#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QElapsedTimer>
#include <atomic>
#include <deque>
#include <map>
#include <utility>
#include <vector>
#include <memory>

#include "CanvasTool.h"
#include "FLIVR/Color.h"
#include "Vulkan/Vulkan2dRender.h"

class RenderCanvas;
class RulerTool;
class TextRenderer;
class VolumeData;
namespace FLIVR { class VL_Nrrd; }

class ViewOverlays : public QObject, public CanvasTool
{
	Q_OBJECT
public:
	// Registers itself on the canvas (canvas->AddTool(this)); register it
	// after the ruler tool so rulers are drawn first, as in wx Draw().
	// ruler (optional) supplies the text renderer, the text colour mode, the
	// current time point and the Info picking functions.
	ViewOverlays(RenderCanvas* canvas, RulerTool* ruler, QObject* parent = nullptr);
	~ViewOverlays() override;

	// ---- annotations (wx m_draw_annotations, default on): the text of
	// annotation layers (component analysis "Annotations") and mesh labels
	void SetDrawAnnotations(bool b) { m_draw_annotations = b; Refresh(); }
	bool GetDrawAnnotations() const { return m_draw_annotations; }

	// ---- camera centre ("Center" check box)
	void SetDrawCamCtr(bool b) { m_draw_camctr = b; Refresh(); }
	bool GetDrawCamCtr() const { return m_draw_camctr; }
	void SetCamCtrSize(double s) { m_camctr_size = s; Refresh(); }   // wx "camctr_size", default 2.0
	double GetCamCtrSize() const { return m_camctr_size; }

	// ---- info ("Info." check box: OnFpsCheck sets m_draw_info only; the
	// default-settings load sets m_draw_info and m_draw_coord)
	void SetDrawInfo(bool b) { m_draw_info = b; Refresh(); }
	bool GetDrawInfo() const { return m_draw_info; }
	void SetDrawCoord(bool b) { m_draw_coord = b; Refresh(); }
	bool GetDrawCoord() const { return m_draw_coord; }

	// ---- legend (hidden "Legend" check box, default on)
	void SetDrawLegend(bool b) { m_draw_legend = b; Refresh(); }
	bool GetDrawLegend() const { return m_draw_legend; }
	// "Names on Objects" of the Legend menu (not in wx; saved with the view):
	// each displayed volume with its legend on gets its colour square on its
	// cell body (VVDFindCellBody, computed in the background once per volume
	// data and threshold) with its name beside it after a square of its
	// colour, instead of a row of the list at the bottom; names that would
	// overlap move by whole lines with their square, a line running straight
	// to it from the cell body's. Meshes stay in the list.
	void SetNamesOnObjects(bool b);
	bool GetNamesOnObjects() const { return m_names_on_objects; }
	// a volume's or mesh's name as the legend shows it (the list and the
	// names on objects; not in wx): without its file extension, the last
	// ".<2-6 letters / digits, one a letter>" at the end or before an "_"
	// suffix, and a ".nii" etc. before ".gz" / ".bz2" / ".xz" / ".zst" /
	// ".zip": "cell.nrrd" -> "cell", "stack.tif_Ch1" -> "stack_Ch1",
	// "cell.nrrd_1" -> "cell_1", "x.nii.gz" -> "x", "cell_v1.5" stays
	static QString LegendName(const QString& name);
	// how the names are placed: automatically in every frame (default), or
	// by hand: each name keeps its place relative to its cell body, the one
	// it was laid out at when it was first shown by hand or the one it was
	// dragged to in the view (a name dragged onto its cell body's square
	// goes back beside it). Saved with the view.
	void SetNamesManual(bool b);
	bool GetNamesManual() const { return m_names_manual; }
	// the places by hand: each name's square relative to its cell body's,
	// in lines of the legend's text (x right, y down), for the volumes the
	// view has
	std::vector<std::pair<VolumeData*, QPointF>> NamePositions() const;
	void SetNamePosition(VolumeData* vd, const QPointF& offset);
	// every name laid out automatically again (by hand: and kept there)
	void ResetNamePositions();
	// testing: the label a volume got in the last frame (frame pixels, y
	// down): the centre of its square on the cell body, the box of its name
	// with the square before it and the centre of that square (the cell
	// body's when the name is beside it, right of it: then not in the box);
	// false: none (no cell body yet, clipped, off screen, legend off)
	bool LabelOf(VolumeData* vd, QPointF* anchor, QRectF* name_box, QPointF* name_square = nullptr) const;
	// the frame size those are in (logical: the overlays' layout size)
	QSize LabelFrameSize() const { return m_label_frame; }
	// testing: a volume's cell body (texture coordinates); false: none (yet)
	bool CellBodyOf(VolumeData* vd, double& fx, double& fy, double& fz) const
	{
		auto it = m_anchors.find(vd);
		if (it == m_anchors.end() || !it->second.done || !it->second.found) return false;
		fx = it->second.fx; fy = it->second.fy; fz = it->second.fz;
		return true;
	}
	// cell bodies being computed or waiting
	int PendingCellBodies() const { return int(m_anchor_queue.size()) + (m_anchor_running_vd ? 1 : 0); }

	// ---- scale bar (VPropView controls)
	void EnableScaleBar() { m_disp_scale_bar = true; Refresh(); }
	void DisableScaleBar() { m_disp_scale_bar = false; Refresh(); }
	bool GetScaleBar() const { return m_disp_scale_bar; }
	void EnableSBText() { m_disp_scale_bar_text = true; Refresh(); }
	void DisableSBText() { m_disp_scale_bar_text = false; Refresh(); }
	bool GetSBTextEnabled() const { return m_disp_scale_bar_text; }
	void SetScaleBarLen(double len) { m_sb_length = len; Refresh(); }
	double GetScaleBarLen() const { return m_sb_length; }
	void SetSBText(const QString& text) { m_sb_text = text; Refresh(); }   // unit text ("nm", "µm", "mm")
	QString GetSBText() const { return m_sb_text; }
	void SetSbNumText(const QString& text) { m_sb_num = text; }
	QString GetSbNumText() const { return m_sb_num; }
	void SetSbUnitSel(int sel) { m_sb_unit = sel; Refresh(); }   // 0 nm, 1 um, 2 mm
	int GetSbUnitSel() const { return m_sb_unit; }
	void FixScaleBarLen(bool fix, double len = -1.0);
	void GetScaleBarFixed(bool& fix, double& len, int& unitid);
	void SetScaleBarDigit(int digit) { m_sclbar_digit = (digit >= 0 && digit <= 8) ? digit : 3; Refresh(); }
	int GetScaleBarDigit() const { return m_sclbar_digit; }

	// ---- capture frame (wx EnableFrame / DisableFrame + CalcFrame result)
	void SetDrawFrame(bool b) { m_draw_frame = b; SyncCaptureFrame(); Refresh(); }
	bool GetDrawFrame() const { return m_draw_frame; }
	void SetFrame(int x, int y, int w, int h) { m_frame_x = x; m_frame_y = y; m_frame_w = w; m_frame_h = h; SyncCaptureFrame(); }
	void SyncCaptureFrame();   // the canvas crops captures to the frame (wx PostDraw)

	// ---- text (used only when no RulerTool was given)
	void SetTextColorMode(int mode) { m_text_color_mode = mode; Refresh(); }
	void SetFont(const QString& path, int size);

	// ---- CanvasTool
	bool IsActive() const override { return false; }
	void HoverMove(QMouseEvent* e) override;
	bool GrabPress(QMouseEvent* e) override;
	void DragMove(QMouseEvent* e) override;
	void DragRelease(QMouseEvent* e) override;
	void DrawOverlay(RenderCanvas* canvas, int nx, int ny) override;
	void DrawOverlayPost(RenderCanvas* canvas, int nx, int ny) override;   // frame, Info

private:
	void Refresh();
	TextRenderer* Text();
	FLIVR::Color GetTextColor();
	void DrawCamCtr(int nx, int ny);
	void DrawFrame(int nx, int ny);
	void DrawScaleBar(int nx, int ny);
	void DrawLegend(int nx, int ny);
	void DrawName(double x, double y, int nx, int ny, const QString& name,
		FLIVR::Color color, double font_height, bool highlighted);
	// Names on Objects: the labels, the background computation of the cell
	// bodies (one at a time) and its result on the main thread
	void DrawObjectNames(int nx, int ny);
	void DrawLeader(double x0, double y0, double x1, double y1, int nx, int ny, FLIVR::Color color);
	// a square of 2 * half + 1 pixels centred on (x, y) in color, in a frame of
	// 2 pixels in frame_color, every edge on a whole pixel
	void DrawMarker(double x, double y, int half, int nx, int ny, FLIVR::Color color, FLIVR::Color frame_color);
	// a quad (x, y pairs; frame pixels, y down) in one colour
	void DrawQuadPx(const float* xy, int nx, int ny, FLIVR::Color color);
	void QueueCellBody(VolumeData* vd);
	void StartNextCellBody();
	void OnCellBody(VolumeData* vd, const std::shared_ptr<FLIVR::VL_Nrrd>& vol, const void* data, double thr,
		bool found, double fx, double fy, double fz);
	void SetColormapColors(int colormap);
	void DrawColormap(int nx, int ny);
	void DrawInfo(int nx, int ny);
	void DrawAnnotations(int nx, int ny);   // wx DrawAnnotations (text labels)
	// params.pipeline/obj/matrix are filled by the caller
	void RenderObj(Vulkan2dRender::V2DRenderParams& params);

	QPointer<RenderCanvas> m_canvas;
	QPointer<RulerTool> m_ruler;

	bool m_draw_annotations = true;
	bool m_draw_camctr = false;
	double m_camctr_size = 2.0;
	bool m_draw_info = false;
	bool m_draw_coord = false;
	bool m_draw_legend = true;
	bool m_disp_scale_bar = false;
	bool m_disp_scale_bar_text = false;
	double m_sb_length = 50.0;
	QString m_sb_text = QString::fromUtf8("\xc2\xb5m");
	QString m_sb_num = QStringLiteral("50");
	int m_sb_unit = 1;
	double m_sb_height = 0.0;
	bool m_fix_sclbar = false;
	double m_fixed_sclbar_fac = 0.0;
	int m_sclbar_digit = 0;
	bool m_draw_frame = false;
	int m_frame_x = 0, m_frame_y = 0, m_frame_w = 0, m_frame_h = 0;
	int m_text_color_mode = 0;

	// colormap bar (wx m_value_* / m_color_*)
	double m_value_2 = 0.0, m_value_3 = 0.0, m_value_4 = 0.0, m_value_5 = 0.0, m_value_6 = 0.0;
	FLIVR::Color m_color_1, m_color_2, m_color_3, m_color_4, m_color_5, m_color_6, m_color_7;

	// wx goTimer (nv::Timer(10)): box filter over the last 10 frame intervals
	QElapsedTimer m_timer;
	double m_intervals[10] = {};
	double m_interval_sum = 0.0;
	int m_interval_pos = 0;

	QPoint m_mouse_pos;
	bool m_have_mouse = false;

	// own text renderer (no RulerTool)
	std::unique_ptr<TextRenderer> m_text_renderer;
	QString m_font_path;
	int m_font_size = 0;
	bool m_font_dirty = false;

	Vulkan2dRender::V2dObject m_camctr_vobj, m_frame_vobj, m_scbar_vobj, m_name_vobj, m_cmap_vobj;

	// Names on Objects
	bool m_names_on_objects = false;
	struct CellBodyAnchor
	{
		const void* data = nullptr;   // the volume data it was computed from
		double thr = -1.0;            // and its threshold (data units)
		// and the object holding that data: a volume deleted and another made
		// at its address (opening a project) cannot inherit the result
		std::weak_ptr<FLIVR::VL_Nrrd> vol;
		bool done = false, found = false;
		double fx = 0.0, fy = 0.0, fz = 0.0;   // texture coordinates
	};
	std::map<VolumeData*, CellBodyAnchor> m_anchors;
	std::deque<VolumeData*> m_anchor_queue;
	VolumeData* m_anchor_running_vd = nullptr;   // the one being computed
	QSize m_label_frame;
	std::shared_ptr<std::atomic<bool>> m_anchor_cancel = std::make_shared<std::atomic<bool>>(false);
	struct DrawnLabel { QPointF anchor; QRectF box; QPointF square; int order = 0; };
	std::map<VolumeData*, DrawnLabel> m_drawn_labels;
	double m_label_fh = 0.0, m_label_edge = 0.0;   // the last layout's text line and square half size
	// by hand: each name's square - its cell body's square, in text lines
	bool m_names_manual = false;
	std::map<VolumeData*, QPointF> m_name_pos;
	VolumeData* m_drag_vd = nullptr;   // the name being dragged
	QPointF m_drag_from, m_drag_pos0;  // where the drag started (frame pixels), the name's place then
	bool m_hand_cursor = false;
	// the name drawn under p (frame pixels: its box or the square before it),
	// the one drawn last if several
	VolumeData* NameAt(const QPointF& p) const;
	QPointF ToFrame(const QPointF& widget_pos) const;
	void UpdateHandCursor(const QPointF& p);
	Vulkan2dRender::V2dObject m_leader_vobj;
};

#endif // VVD_QT_VIEWOVERLAYS_H
