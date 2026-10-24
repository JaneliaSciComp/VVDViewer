#ifndef VVD_QT_BRUSHTOOL_H
#define VVD_QT_BRUSHTOOL_H

// Paint brush of the render canvas: Qt port of the painting part of
// VRenderVulkanView (VRenderView.cpp): interaction modes m_int_mode 2 / 4
// (paint, stroke finished) and 7 / 8 (paint inside the ruler tool), stroke
// recording, PaintStroke into the 2D paint buffer (m_fbo_paint/m_tex_paint),
// DrawBrush / DrawCircle cursor, DisplayStroke, Segment(), the Shift / Z / X
// hold-to-paint state machine (UpdateBrushState), wheel brush sizes, Ctrl+Z /
// Ctrl+Y mask undo/redo, LoadDefaultBrushSettings, and the brush getters /
// setters the wx BrushToolDlg calls on the view.
//
// The selector / calculator are the ones of AnalysisContext (wx: owned by the
// view). Pen pressure (wx m_use_pres, project key brush_use_pres; wx reads
// it from Wintab on Windows only) comes from Qt tablet events on every
// platform: brush radii and the stamp spacing are scaled by it while a pen
// touches the view; mouse strokes keep pressure 1.0.
//
// Register the tool on the canvas BEFORE the ruler tool so that it gets the
// events first in modes 7/8 (RenderCanvas::ActiveTool picks the first active).

#include <QObject>
#include <functional>
#include <QPoint>
#include <QPointer>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include "FLIVR/Color.h"
#include "CanvasTool.h"

#ifndef BRUSH_TOOL_ITER_WEAK
#define BRUSH_TOOL_ITER_WEAK	20
#define BRUSH_TOOL_ITER_NORMAL	50
#define BRUSH_TOOL_ITER_STRONG	100
#endif

class QWidget;
class RenderCanvas;
class AnalysisContext;
class TextRenderer;
class VolumeLoader;
class VolumeData;
class VolumeSelector;
class VolumeCalculator;
namespace vks { class VFrameBuffer; class VTexture; }
namespace FLIVR { class Color; }

class BrushTool : public QObject, public CanvasTool
{
	Q_OBJECT
public:
	BrushTool(RenderCanvas* canvas, AnalysisContext* ctx, QObject* parent = nullptr);
	~BrushTool() override;
	RenderCanvas* Canvas() const;
	// Drop the GPU objects (paint buffer, text renderer). Call before the
	// canvas / Vulkan device goes away (MainWindow::closeEvent).
	void ReleaseResources();

	// ---- CanvasTool
	// Active (consumes mouse/wheel) in int modes 2, 4, 7, 8.
	bool IsActive() const override;
	bool MousePress(QMouseEvent* e) override;
	bool MouseMove(QMouseEvent* e) override;
	bool MouseRelease(QMouseEvent* e) override;
	bool Wheel(QWheelEvent* e) override;
	bool KeyPress(QKeyEvent* e) override;
	// captures the scene matrices (camera + object transforms) for Segment()
	void DrawOverlay(RenderCanvas* canvas, int nx, int ny) override;
	void DrawOverlayPost(RenderCanvas* canvas, int nx, int ny) override;   // after the capture point (wx: painting after PostDraw)

	// ---- interaction / paint modes (wx VRenderVulkanView)
	// int mode: 1 view, 2 paint, 4 paint (stroke done: clear next frame),
	// 5 ruler (owned by the ruler tool), 7 ruler-paint, 8 ruler-paint done.
	int GetIntMode() const { return m_int_mode; }
	void SetIntMode(int mode);                 // wx SetIntMode
	// selector mode: 1 select, 2 append, 3 unselect, 4 diffuse, 5 flood,
	// 6 clear, 7 all, 8 solid
	void SetPaintMode(int mode);               // wx SetPaintMode (also m_brush_state)
	int GetPaintMode() const;
	void SetBrush(int mode);                   // wx SetBrush
	void SetPaintUse2d(bool use2d);
	bool GetPaintUse2d() const;
	// Ruler tool type (wx m_ruler_type; 3 = "paint" ruler -> solid brush in mode 7)
	void SetRulerType(int type) { m_ruler_type = type; }
	// mode 7 (paint in ruler mode) press, after the old selection is cleared
	// and before the stroke: the Measurement "Auto-select" (RulerTool::
	// AutoSelectForPaintRuler). Returns true when the current volume changed
	// (the new volume's selection is then cleared too).
	void SetRulerPressHook(std::function<bool(int, int)> f) { m_ruler_press_hook = std::move(f); }

	// ---- toolbar commands (wx TreePanel::BrushAppend/Diffuse/Desel/Solid,
	// BrushClear, BrushErase, BrushCreate and DataTreeCtrl::BrushClear/
	// BrushCreate/BrushCreateInv). paint_mode 0 = button released.
	// SelectBrushMode(2) = Highlight, (4) = Diffuse, (3) = Reset, (8) = Solid.
	void SelectBrushMode(int paint_mode);
	void BrushClear();     // "Reset All": clear the current volume's mask
	void BrushErase();     // "Erase": Calculate(6) -> "<name>_DELETED"
	void BrushCreate();    // "Extract": Calculate(5) -> "<name>_EXTRACTED"
	// mask undo / redo of the current volume (wx BrushToolDlg::BrushUndo/Redo)
	void BrushUndo();
	void BrushRedo();
	bool CanUndo() const;
	bool CanRedo() const;

	// segment volumes with the current 2D paint mask (wx Segment())
	void Segment();

	// ---- brush properties (wx VRenderVulkanView getters / setters)
	void LoadDefaultBrushSettings();           // default_brush_settings.dft
	// testing aids: Shift held / released (-1: the real keyboard) and the
	// wx OnIdle / OnMouse UpdateBrushState call
	void SetTestShift(int held) { m_test_shift = held; }
	void UpdateBrushStateNow() { UpdateBrushState(); }
	double GetPressure() const { return m_pressure; }
	void SetBrushUsePres(bool pres) { m_use_pres = pres; }
	bool GetBrushUsePres() const { return m_use_pres; }
	void SetUseBrushSize2(bool val);
	bool GetBrushSize2Link() const { return m_use_brush_radius2; }
	void SetBrushSize(double size1, double size2);
	double GetBrushSize1() const { return m_brush_radius1; }
	double GetBrushSize2() const { return m_brush_radius2; }
	void SetBrushSpacing(double spacing) { if (spacing > 0.0) m_brush_spacing = spacing; }
	double GetBrushSpacing() const { return m_brush_spacing; }
	void SetBrushIteration(int num);
	int GetBrushIteration() const;
	void SetBrushSclTranslate(double val);     // also the calculator threshold
	double GetBrushSclTranslate() const;
	void SetBrushGmFalloff(double val);
	double GetBrushGmFalloff() const;
	void SetW2d(double val);
	double GetW2d() const;
	void SetEdgeDetect(bool value);
	bool GetEdgeDetect() const;
	void SetHiddenRemoval(bool value);
	bool GetHiddenRemoval() const;
	void SetSelectGroup(bool value);
	bool GetSelectGroup() const;
	void SetUseAbsoluteValue(bool value) { m_use_absolute_value = value; }
	bool GetUseAbsoluteValue() const { return m_use_absolute_value; }
	void SetEstimateThresh(bool value);
	bool GetEstimateThresh() const;
	void SetSelectBoth(bool value);
	bool GetSelectBoth() const;
	void SetUseDSLTBrush(bool value);
	bool GetUseDSLTBrush() const;
	void SetBrushDSLT_R(int rad);
	int GetBrushDSLT_R() const;
	void SetBrushDSLT_Q(int quality);
	int GetBrushDSLT_Q() const;
	void SetBrushDSLT_C(double c);
	double GetBrushDSLT_C() const;

	// 2D paint buffer (wx m_tex_paint) for VolumeSelector::Set2DMask; null
	// until the first stroke. The 2D weights are canvas->GetFinalTex() /
	// canvas->GetVolTex() (wx Set2DWeight(m_tex_final, m_tex_wt2 ? m_tex_wt2 : m_tex)).
	std::shared_ptr<vks::VTexture> PaintTex() const { return m_tex_paint; }

	// Brush cursor colour / text (wx GetTextColor + m_text_renderer).
	// mode: 0 inverted background (default), 1 background, 2 mask colour of
	// the current volume (SettingDlg "text color").
	void SetTextColorMode(int mode) { m_text_color_mode = mode; }
	// Share a text renderer (not owned); otherwise the tool creates its own
	// from Fonts/FreeSans.ttf on first use.
	void SetTextRenderer(TextRenderer* tr) { m_ext_text_renderer = tr; }
	void SetTextSize(int size);

Q_SIGNALS:
	// The selected brush changed (SelectBrushMode, Shift/Z/X hold, end of a
	// hold, Erase/Extract). paint_mode: 0 none, 2 Highlight, 4 Diffuse, 3 Reset.
	// wx: TreePanel::SelectBrush / BrushToolDlg::SelectBrush. Toolbars should
	// update their check state without re-emitting a request.
	void brushSelected(int paint_mode);
	// Brush sizes / masks changed (after Segment, wheel, undo/redo):
	// wx BrushToolDlg::GetSettings(vrv) (dialog re-reads sizes + undo state).
	void settingsChanged();
	// wx VRenderVulkanView::Segment end: the Components & Tracking dialog's
	// auto ID (CellNewID / CellEraseID by selector mode) and manual assist
	// (CellUpdate, CellLink(true)); sel_mode = VolumeSelector::GetMode()
	void segmented(int sel_mode);
	// Mode 7 release (wx AddRulerPoint(x, y) when m_ruler_type == 3, else
	// AddPaintRulerPoint()); for the ruler tool.
	void rulerPointRequested(int x, int y);
	void paintRulerPointRequested();
	// The interaction mode changed (2 paint, 7 paint in ruler mode, 5 ruler
	// mode again after a hold, 1 camera, ...; the transient 4 / 8 are reported
	// as 2 / 7); MainWindow mirrors it into the ruler tool (wx: one m_int_mode).
	void intModeChanged(int mode);

protected:
	bool eventFilter(QObject* obj, QEvent* e) override;

private:
	struct Stroke { QPoint prv, cur; double pressure = 1.0; };
	double PenPressure() const;   // the canvas's tablet pressure, 1.0 without a pen

	void UpdateBrushState();
	bool KeyHeld(int key) const;
	bool MouseInCanvas() const;
	bool Editing() const;
	void FlushStrokes();     // render pending strokes now (before Segment)
	void PaintStroke(int nx, int ny, const QPoint& prv, const QPoint& cur, double stroke_pressure);
	void DisplayStroke();
	void DrawBrush(int nx, int ny);
	void DrawCircle(double cx, double cy, double radius, const FLIVR::Color& color, const glm::mat4& matrix);
	FLIVR::Color GetTextColor() const;
	TextRenderer* GetTextRendererInst();
	void PreloadMaskLevel(VolumeData* vd);
	VolumeSelector* Sel() const;
	VolumeCalculator* Calc() const;
	VolumeData* CurVolume() const;

	QPointer<RenderCanvas> m_canvas;
	AnalysisContext* m_ctx;

	// wx VRenderVulkanView painting state
	int m_int_mode = 1;
	int m_notified_int_mode = 1;
	void NotifyIntMode();
	int m_brush_state = 0;
	bool m_draw_brush = false;
	bool m_paint_display = false;
	bool m_clear_paint = true;
	bool m_on_press = false;          // left button held in paint mode
	bool m_use_pres = false;
	double m_pressure = 1.0;
	double m_brush_radius1 = 10.0;
	double m_brush_radius2 = 30.0;
	bool m_use_brush_radius2 = true;
	double m_brush_spacing = 0.1;
	bool m_use_absolute_value = false;
	int m_ruler_type = 0;
	std::function<bool(int, int)> m_ruler_press_hook;
	QPoint m_old_mouse{ -1, -1 };     // wx old_mouse_X/Y
	QPoint m_prv_mouse{ -1, -1 };     // wx prv_mouse_X/Y
	// Qt renders asynchronously (several mouse moves per frame): every drag
	// segment is kept and painted in the next frame.
	std::vector<Stroke> m_strokes;
	int m_pt_moves = 0;   // VVD_PAINT_TIMING: drag events of the stroke
	QPointer<QWidget> m_prev_focus;

	// paint buffer (canvas size)
	std::unique_ptr<vks::VFrameBuffer> m_fbo_paint;
	std::shared_ptr<vks::VTexture> m_tex_paint;
	struct BrushGeom;                 // Vulkan2dRender::V2dObject of the cursor circle
	std::unique_ptr<BrushGeom> m_geom;

	// object-space matrices of the last frame (Segment runs outside a frame)
	glm::mat4 m_mv_mat{ 1.0f };
	glm::mat4 m_proj_mat{ 1.0f };
	glm::mat4 m_tex_mat{ 1.0f };
	bool m_have_matrices = false;

	// key state for the hold-to-paint keys (wx VRenderFrame::GetKeyState)
	int m_test_shift = -1;
	bool m_key_z = false;
	bool m_key_x = false;
	bool m_undo_keydown = false;
	bool m_redo_keydown = false;
	const QEvent* m_last_key_event = nullptr;
	ulong m_last_key_ts = 0;
	int m_last_key_type = 0;
	int m_last_key_code = 0;

	int m_text_color_mode = 0;
	int m_text_size = 12;
	TextRenderer* m_ext_text_renderer = nullptr;
	std::unique_ptr<TextRenderer> m_text_renderer;
	bool m_text_renderer_failed = false;
	std::unique_ptr<VolumeLoader> m_loader;
};

#endif // VVD_QT_BRUSHTOOL_H
