#ifndef VVD_QT_VIEWPANEL_H
#define VVD_QT_VIEWPANEL_H

// "Render View:1" pane: Qt port of the VRenderView chrome around the canvas.
//
//   [ Layered Depth Composite | Capture | Background: [] Center Info. Legend Intrp. V.AOV ... FreeFly ]
//   [ depth-int column ][            RenderCanvas            ][ zoom column ]
//   [ Link Reset Rot. X: --- Y: --- Z: ---  45 Increments  Reset Transl.  Save as Default ]
//
// Same widget order and sizes as VRenderView::CreateBar.

#include <QWidget>
#include <functional>
#include <QPoint>
#include "CaptureDialog.h"

class QCheckBox;
class QRadioButton;
class QPushButton;
class QSlider;
class QLineEdit;
class QLabel;
class QComboBox;
class QSpinBox;
class RenderCanvas;
class VolumeData;
class ViewOverlays;
class LegendMenu;

class ViewPanel : public QWidget
{
	Q_OBJECT
public:
	explicit ViewPanel(QWidget* parent = nullptr);

	RenderCanvas* Canvas() const { return m_canvas; }
	void SetCurrentVolume(VolumeData* vd);
	// Center / Info. / Legend check boxes and the Legend button drive these
	// overlays (not owned). SyncOverlayChecks re-reads their state (e.g.
	// after loading view defaults); ReloadLegend refreshes an open legend
	// list after volumes / meshes were added or removed.
	void SetOverlays(ViewOverlays* ov);
	void SyncOverlayChecks();
	void ReloadLegend();
	// testing (VVD_QT_GRAB_LEGEND): the Legend button and its drop-down
	QPushButton* LegendButton() const { return m_legend_btn; }
	LegendMenu* GetLegendMenu() const { return m_legend_menu; }
	// wx VRenderView::LoadSettings / SaveDefault: default_view_settings.dft
	// (same file and keys as the wx app). LoadDefaultSettings runs at the end
	// of the constructor; the Center/Info./Legend defaults are applied when
	// SetOverlays is called.
	void LoadDefaultSettings();
	void SaveDefaultSettings();
	// "Capture" button (CaptureDialog.cpp): main-window services it needs
	void SetCaptureHooks(const CaptureDialog::Hooks& h) { m_capture_hooks = h; }
	// the Capture button without its dialog (VVD_QT_CAPTURE test path)
	void Capture(const QString& file, const CaptureDialog::Options& opt);
	// wx OnAovSldrIdle: the clipping planes are shown while the pointer is
	// over the V. AOV slider, unless the Clipping Planes pane holds them
	// ("Display Planes"); the provider gives that pane's plane mode and hold
	// state (false: no pane)
	void SetClipStateProvider(std::function<bool(int& plane_mode, bool& hold)> f) { m_clip_state = std::move(f); }
	QSlider* AovSlider() const { return m_aov_sldr; }   // testing
	void PollAovHover();                // one wx OnAovSldrIdle tick (timer, tests)
	// testing: a pointer position (global) used instead of QCursor::pos()
	void SetCursorOverride(bool on, const QPoint& global = QPoint()) { m_cursor_override = on; m_cursor_pos = global; }

private Q_SLOTS:
	void OnBackground();
	void OnAov(int v);
	void OnRotSlider(int axis, int v);
	void OnRotText(int axis);
	void OnZoomSlider(int v);
	void OnZoomText();
	void OnCanvasRotation(double rx, double ry, double rz);
	void OnCanvasZoom(double s);
	void SyncVolMethod(int method);
	void SyncFog(bool on, double intensity);
	void OnAovText();
	void OnFreeChk(bool on);
	void OnRotLock(bool on);
	void UpdateView();                  // wx VRenderView::UpdateView: texts -> rotations
	void OnRotSpin(int axis, int delta);
	void OnZoomSpin(int delta);

private:
	QWidget* BuildTopBar();
	QWidget* BuildDepthColumn();
	QWidget* BuildZoomColumn();
	QWidget* BuildBottomBar();
	void SetAovText(int v);

	RenderCanvas* m_canvas = nullptr;
	QPushButton* m_capture_btn = nullptr;
	CaptureDialog::Hooks m_capture_hooks;
	QRadioButton* m_layered_rd = nullptr;
	QRadioButton* m_depth_rd = nullptr;
	QRadioButton* m_comp_rd = nullptr;
	QPushButton* m_bg_btn = nullptr;
	QCheckBox* m_center_chk = nullptr;
	QCheckBox* m_info_chk = nullptr;
	QCheckBox* m_legend_chk = nullptr;
	QPushButton* m_legend_btn = nullptr;
	ViewOverlays* m_overlays = nullptr;
	LegendMenu* m_legend_menu = nullptr;   // drop-down of m_legend_btn
	bool m_overlay_conns = false;
	QCheckBox* m_intp_chk = nullptr;
	QSlider* m_aov_sldr = nullptr;
	QLineEdit* m_aov_text = nullptr;
	QCheckBox* m_free_chk = nullptr;
	QSlider* m_rot_sldr[3] = {};
	QLineEdit* m_rot_text[3] = {};
	QSlider* m_zoom_sldr = nullptr;
	QLineEdit* m_zoom_text = nullptr;
	QCheckBox* m_rot_link_chk = nullptr;
	QCheckBox* m_rot_lock_chk = nullptr;
	QCheckBox* m_depth_chk = nullptr;
	QPushButton* m_depth_reset_btn = nullptr;
	QLabel* m_res_label = nullptr;
	QComboBox* m_res_combo = nullptr;
	QSlider* m_depth_sldr = nullptr;
	QLineEdit* m_depth_text = nullptr;
	VolumeData* m_vd = nullptr;
	bool m_updating = false;
	std::function<bool(int&, bool&)> m_clip_state;
	bool m_draw_clip = false;           // wx VRenderView::m_draw_clip (AOV hover)
	bool m_cursor_override = false;
	QPoint m_cursor_pos;
	// view defaults (wx m_use_dft_settings, m_dft_*)
	bool m_use_dft_settings = false;
	double m_dft_scale_factor = 100.0;
	double m_dft_depth_atten_factor = 0.0;
	double m_dft_rot[3] = { 0.0, 0.0, 0.0 };
	struct PendingOverlay { bool has_ctr = false, ctr = false; bool has_ctr_size = false; double ctr_size = 2.0;
		bool has_fps = false, fps = false; bool has_legend = false, legend = false; } m_pending_overlay;
};

#endif // VVD_QT_VIEWPANEL_H
