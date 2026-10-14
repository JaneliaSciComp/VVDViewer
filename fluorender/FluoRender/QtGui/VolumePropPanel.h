#ifndef VVD_QT_VOLUMEPROPPANEL_H
#define VVD_QT_VOLUMEPROPPANEL_H

// Volume "Properties" panel: Qt port of VPropView's controls. Every control
// is a slider + editable value box; the layout is a QGridLayout that reports
// its real minimum size, so the pane can never clip its bottom row (the bug
// that motivated the port).

#include <QWidget>
#include <vector>
#include <functional>

class QSlider;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QLineEdit;
class QComboBox;
class VolumeData;
class DataGroup;
class ViewOverlays;
class DataManager;
class RenderCanvas;

class VolumePropPanel : public QWidget
{
	Q_OBJECT
public:
	explicit VolumePropPanel(QWidget* parent = nullptr);

	void SetVolume(VolumeData* vd);   // nullptr disables the panel
	VolumeData* GetVolume() const { return m_vd; }
	void GetSettings();               // refresh controls from the volume
	// wx VPropView::UpdateUIsROI: in index colour mode the colormap row is
	// replaced by the "Segment:" row (name, colour, display mode) of the
	// volume's edited segment (GetEditSelID)
	void UpdateUIsROI();
	// Scale-bar controls (SclBar / Text / length / unit / Digit / Fix length)
	// edit these view overlays (not owned), like VPropView edits the view.
	void SetOverlays(ViewOverlays* ov);
	void GetScaleBarSettings();       // refresh the scale-bar controls from the overlays
	// "Group:" Sync / Sync Spacings / Depth (wx VPropView::SetGroup). The Qt
	// app has no DataGroup objects yet: SetGroupVolumes gives the volumes of
	// the selected volume's (single) group instead; SetGroup takes precedence.
	void SetGroup(DataGroup* group);
	DataGroup* GetGroup() const { return m_group; }
	void SetGroupVolumes(const std::vector<VolumeData*>& vols) { m_group_vols = vols; }
	// wx VPropView::m_frame->GetDataManager(): the m_vol_* defaults of
	// "Save as Default" / "Reset"
	void SetDataManager(DataManager* mgr) { m_data_mgr = mgr; }
	// wx VPropView::m_vrv: Shadow / Smoothing / Sample Rate go to every
	// volume of the view in Depth (VOL_METHOD_MULTI) mode, MIP is refused there
	void SetRenderCanvas(RenderCanvas* canvas) { m_canvas = canvas; }
	// wx default_volume_settings.dft (next to the executable / in the app
	// bundle's Resources, else the user config dir); VVD_QT_VOLUME_DFT
	// overrides the path (tests)
	static QString DefaultSettingsPath();
	// testing aids: the wx handlers behind the buttons / check boxes
	void SaveDefault() { OnSaveDefault(); }
	void ResetDefault() { OnResetDefault(); }
	QCheckBox* MipCheck() const { return m_mip_chk; }
	QCheckBox* AlphaCheck() const { return m_alpha_chk; }
	QCheckBox* ShadingCheck() const { return m_shading_chk; }
	QCheckBox* ShadowCheck() const { return m_shadow_chk; }
	QCheckBox* NRCheck() const { return m_nr_chk; }
	QDoubleSpinBox* MaxValueBox() const { return m_max_value; }
	QDoubleSpinBox* SampleRateBox() const { return m_sample_rate.spin; }
	QDoubleSpinBox* ShadowBox() const { return m_shadow.spin; }
	QDoubleSpinBox* AlphaBox() const { return m_alpha.spin; }
	QDoubleSpinBox* GammaBox() const { return m_gamma.spin; }
	// enabled state of a row: 0 gamma, 1 saturation, 2 luminance, 3 alpha,
	// 4 low shading, 5 boundary, 6 left threshold, 7 right threshold
	bool RowEnabled(int row) const;
	QString ThresholdLabel() const;

Q_SIGNALS:
	void volumeChanged();             // something that needs a redraw changed
	// colormap mode changed (":Index" / colormap): wx OnIDCLCheck calls
	// AdjustView::UpdateSync -- connect to AdjustPanel::UpdateSync
	void colormapModeChanged();
	// colour changed from the Color box / picker: tree bulb colours and the
	// Output Adjustments sync follow (wx OnColorChange -> AdjustView::UpdateSync)
	void colorChanged();
	// wx VPropView::SaveROIName: the "Segment:" name box was confirmed
	// (MainWindow sets the ROI name and rebuilds / reselects the tree)
	void roiNameEntered(const QString& name);
	// wx OnIDCLCheck: UpdateTree(name, 2) -- the segment items of an index
	// volume appear / disappear
	void treeRefreshRequested();

private:
	struct SliderRow
	{
		QLabel* label = nullptr;
		QSlider* slider = nullptr;
		QDoubleSpinBox* spin = nullptr;
		double scale = 1.0;           // slider int = value * scale
	};
	SliderRow MakeRow(const QString& name, int smin, int smax, double scale, int decimals, double vmin, double vmax);
	void Bind(SliderRow& r, void (VolumePropPanel::*apply)(double));
	void SetRow(SliderRow& r, double value);

	// appliers (value in real units as shown in the spin box)
	void ApplyGamma(double v);
	void ApplyBoundary(double v);
	void ApplySaturation(double v);
	void ApplyLuminance(double v);
	void ApplyAlpha(double v);
	void ApplyLowShading(double v);
	void ApplyHiShading(double v);
	void ApplyShadow(double v);
	void ApplyLeftThresh(double v);
	void ApplyRightThresh(double v);
	void ApplySampleRate(double v);
	void ApplyColormapLow(double v);
	void ApplyColormapHigh(double v);
	void ApplySpacing();

	void OnColorButton();
	void OnColorTextEdited(const QString& text);   // wx OnColorTextChange
	void OnColorTextFinished();
	void ApplyTypedColor(const QColor& c);
	bool eventFilter(QObject* obj, QEvent* ev) override;
	bool m_color_typing = false;   // user is typing in the Color box: don't overwrite it
	void OnResetDefault();
	void OnSaveDefault();

	// group sync (wx m_group, m_sync_group, m_sync_group_spc)
	std::vector<VolumeData*> GroupVolumes() const;
	bool HasGroup() const { return !GroupVolumes().empty(); }
	// wx "if (m_sync_group && m_group) m_group->SetX(..) else m_vd->SetX(..)";
	// depth: also when the group is in Depth blend mode (GetBlendMode() == 2)
	void ForTargets(const std::function<void(VolumeData*)>& f, bool depth = false);
	void OnSyncGroupCheck();
	void OnSyncGroupSpcCheck();
	void OnIdclCheck();
	void OnDepthCheck();
	void OnMaskHide(bool outside);
	void UpdateIdclUi();
	// wx GetSettings: alpha / shading / MIP / index enable states and the
	// "Threshold:" / "Shade Threshold:" label
	void UpdateEnables();
	void SetRowEnabled(SliderRow& r, bool on);
	void OnAlphaCheck();      // wx OnAlphaCheck
	void OnShadingCheck();    // wx OnShadingEnable
	void OnMIPCheck();        // wx OnMIPCheck
	void OnShadowCheck();     // wx OnShadowEnable
	void OnNRCheck();         // wx OnNRCheck
	void UpdateMaxValue();    // wx UpdateMaxValue
	// wx "if (m_vrv && m_vrv->GetVolMethod()==VOL_METHOD_MULTI) every volume
	// of the view, else <group / volume rule>"
	void ForTargetsView(const std::function<void(VolumeData*)>& f, bool depth);
	DataManager* m_data_mgr = nullptr;
	RenderCanvas* m_canvas = nullptr;
	QWidget* m_cmap_row = nullptr;       // wx m_sizer_r5 (colormap)
	QWidget* m_roi_row = nullptr;        // wx m_sizer_r6 (segment)
	QLabel* m_roi_st = nullptr;
	QLineEdit* m_roi_text = nullptr;
	QPushButton* m_roi_color_btn = nullptr;
	QComboBox* m_roi_disp_mode_combo = nullptr;
	void SetROIindex(int id);
	void OnROIColorBtn();
	DataGroup* m_group = nullptr;
	std::vector<VolumeData*> m_group_vols;
	bool m_sync_group = false;
	bool m_sync_group_spc = false;
	QCheckBox* m_idcl_chk = nullptr;
	QCheckBox* m_mask_out_chk = nullptr;
	QCheckBox* m_mask_in_chk = nullptr;
	QCheckBox* m_sync_group_chk = nullptr;
	QCheckBox* m_sync_spc_chk = nullptr;
	QCheckBox* m_depth_chk = nullptr;

	VolumeData* m_vd = nullptr;
	double m_max_val = 255.0;
	bool m_is_float = false;
	bool m_updating = false;

	SliderRow m_gamma, m_saturation, m_luminance, m_alpha, m_low_shading;
	SliderRow m_boundary, m_left_thresh, m_right_thresh, m_hi_shading, m_sample_rate;
	SliderRow m_colormap_low, m_colormap_high, m_shadow;
	QDoubleSpinBox* m_spc_x = nullptr;
	QDoubleSpinBox* m_spc_y = nullptr;
	QDoubleSpinBox* m_spc_z = nullptr;
	QDoubleSpinBox* m_max_value = nullptr;
	QPushButton* m_color_btn = nullptr;
	QLineEdit* m_color_text = nullptr;
	QCheckBox* m_alpha_chk = nullptr;
	QCheckBox* m_shading_chk = nullptr;
	QCheckBox* m_shadow_chk = nullptr;
	QCheckBox* m_inv_chk = nullptr;
	QCheckBox* m_mip_chk = nullptr;
	QCheckBox* m_interp_chk = nullptr;
	QCheckBox* m_nr_chk = nullptr;
	QCheckBox* m_colormap_chk = nullptr;
	QCheckBox* m_legend_chk = nullptr;

	// scale bar
	QString ScaleUnitText() const;
	void OnScaleCheck();
	void OnScaleTextCheck();
	void OnScaleTextEditing();
	void OnScaleUnitSelected();
	void OnScaleDigitSelected();
	void OnScaleLenFixCheck();
	ViewOverlays* m_overlays = nullptr;
	QCheckBox* m_scale_chk = nullptr;
	QCheckBox* m_scale_te_chk = nullptr;
	QLineEdit* m_scale_text = nullptr;
	QComboBox* m_scale_cmb = nullptr;
	QComboBox* m_scale_digit_cmb = nullptr;
	QCheckBox* m_scale_lenfix_chk = nullptr;
};

#endif // VVD_QT_VOLUMEPROPPANEL_H
