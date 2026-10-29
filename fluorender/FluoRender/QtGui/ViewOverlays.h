#ifndef VVD_QT_VIEWOVERLAYS_H
#define VVD_QT_VIEWOVERLAYS_H

// Render-view overlays for the Qt canvas: port of the overlay part of
// VRenderVulkanView::OnDraw (VRenderView.cpp).
//
//   wx                                       here
//   m_draw_camctr / m_camctr_size            SetDrawCamCtr / SetCamCtrSize   (DrawCamCtr)
//   m_draw_legend, m_sb_height               SetDrawLegend                   (DrawLegend, DrawName)
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
// -> RefreshGL(false, false)).

#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QString>
#include <QElapsedTimer>
#include <memory>

#include "CanvasTool.h"
#include "FLIVR/Color.h"
#include "Vulkan/Vulkan2dRender.h"

class RenderCanvas;
class RulerTool;
class TextRenderer;
class VolumeData;

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
};

#endif // VVD_QT_VIEWOVERLAYS_H
