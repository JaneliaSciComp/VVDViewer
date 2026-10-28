#ifndef VVD_QT_BRUSHTOOLDIALOG_H
#define VVD_QT_BRUSHTOOLDIALOG_H

// "Analyze" (paint brush) tool window: Qt port of the wx BrushToolDlg
// (BrushToolDlg.cpp/.h). Three pages, same controls / labels / arrangement:
//   Paint Brush:  Undo, Redo | Select, Diffuse, Unselect, Reset All | Erase,
//                 Extract; Selection Settings; Brush Properties; Mask Display
//   Calculations: Volume A/B, 3D Fill Holes, Subtract/Max/Divide/Colocalize
//   Analysis:     Component Analyzer, Noise Removal, EVE
// Settings are read from / written to default_brush_settings.dft exactly like
// the wx dialog (LoadDefault on construction, SaveDefault on destruction).
//
// wx m_cur_view (VRenderView) is replaced by the BrushTool (brush state and
// selector settings) and the AnalysisContext (data, calculator, analysis).

#include <QDialog>

class QTabWidget;
class QToolBar;
class QAction;
class QCheckBox;
class QSlider;
class QLineEdit;
class QLabel;
class QRadioButton;
class QPushButton;
class QTimer;
class QWidget;
class AnalysisContext;
class BrushTool;
class VolumeData;

class BrushToolDialog : public QDialog
{
	Q_OBJECT
public:
	BrushToolDialog(AnalysisContext* ctx, BrushTool* tool, QWidget* parent = nullptr);
	~BrushToolDialog() override;

	// default values the wx frame reads from / writes to fluorender.set
	double GetDftCAMin() const { return m_dft_ca_min; }
	void SetDftCAMin(double min) { m_dft_ca_min = min; }
	double GetDftCAMax() const { return m_dft_ca_max; }
	void SetDftCAMax(double max) { m_dft_ca_max = max; }
	double GetDftCAThresh() const { return m_dft_ca_thresh; }
	void SetDftCAThresh(double thresh) { m_dft_ca_thresh = thresh; }
	double GetDftCAFalloff() const { return m_dft_ca_falloff; }
	void SetDftCAFalloff(double falloff) { m_dft_ca_falloff = falloff; }
	double GetDftNRThresh() const { return m_dft_nr_thresh; }
	void SetDftNRThresh(double thresh) { m_dft_nr_thresh = thresh; }
	double GetDftNRSize() const { return m_dft_nr_size; }
	void SetDftNRSize(double size) { m_dft_nr_size = size; }
	int GetDftEVEMinRadius() const { return m_dft_eve_min_radius; }
	void SetDftEVEMinRadius(int r) { m_dft_eve_min_radius = r; }
	int GetDftEVEMaxRadius() const { return m_dft_eve_max_radius; }
	void SetDftEVEMaxRadius(int r) { m_dft_eve_max_radius = r; }
	double GetDftEVEThresh() const { return m_dft_eve_thresh; }
	void SetDftEVEThresh(double thresh) { m_dft_eve_thresh = thresh; }

	void SetBrushSclTranslate(double val);
	void SetCalcA(const QString& name);
	void SetCalcB(const QString& name);

	//save default (default_brush_settings.dft)
	void SaveDefault();

public Q_SLOTS:
	// wx GetSettings(vrv): re-read the brush / selector state and the ranges
	// of the current volume. Connect to BrushTool::settingsChanged and call
	// on volume selection.
	void GetSettings();
	// wx BrushToolDlg::GetSettings(vrv): another render view's brush
	void SetTool(BrushTool* tool);
	BrushTool* GetTool() const { return m_tool; }
	// set the brush button down (paint_mode 2 Select, 4 Diffuse, 3 Unselect,
	// anything else: none); no request is sent. wx SelectBrush(id).
	void SelectBrush(int paint_mode);
	//update undo status
	void UpdateUndoRedo();
	void BrushUndo();
	void BrushRedo();

protected:
	void showEvent(QShowEvent* e) override;

private:
	QWidget* CreateBrushPage(QWidget* parent);
	QWidget* CreateCalculationPage(QWidget* parent);
	QWidget* CreateAnalysisPage(QWidget* parent);
	void LoadDefault();
	void LoadVolumes();
	void DrawBrush(double val);
	void Calc(int type, bool need_b);
	void SetDSLTEnabled(bool enable);
	void FitToCurrentPage();
	VolumeData* CurSelVol() const;

	//paint tools
	void OnBrushAppend(bool checked);
	void OnBrushDiffuse(bool checked);
	void OnBrushDesel(bool checked);
	void OnBrushClear();
	void OnBrushErase();
	void OnBrushCreate();
	//selection adjustment
	void OnBrush2dinflChange(int ival);
	void OnBrush2dinflText(const QString& str);
	void OnBrushEdgeDetectChk(bool checked);
	void OnBrushHiddenRemovalChk(bool checked);
	void OnBrushSelectGroupChk(bool checked);
	void OnBrushUseAbsoluteValueChk(bool checked);
	void OnBrushSclTranslateChange(int ival);
	void OnBrushSclTranslateText(const QString& str);
	void OnBrushSclTranslateTextEnter();
	//brush properties
	void OnBrushSize1Change(int ival);
	void OnBrushSize1Text(const QString& str);
	void OnBrushSize2Chk(bool checked);
	void OnBrushSize2Change(int ival);
	void OnBrushSize2Text(const QString& str);
	void OnBrushIterCheck();
	void OnDSLTBrushChk(bool checked);
	void OnDSLTBrushRadChange(int ival);
	void OnDSLTBrushRadText(const QString& str);
	void OnDSLTBrushQualityChange(int ival);
	void OnDSLTBrushQualityText(const QString& str);
	void OnDSLTBrushCChange(int ival);
	void OnDSLTBrushCText(const QString& str);
	//mask display
	void OnMaskAlphaChange(int ival);
	void OnMaskAlphaText(const QString& str);
	//component analyzer
	void OnCAThreshChange(int ival);
	void OnCAThreshText(const QString& str);
	void OnCAAnalyzeBtn();
	void OnCAIgnoreMaxChk(bool checked);
	void OnCAMultiChannBtn();
	void OnCARandomColorBtn();
	void OnCAAnnotationsBtn();
	//noise removal
	void OnNRSizeChange(int ival);
	void OnNRSizeText(const QString& str);
	void OnNRAnalyzeBtn();
	void OnNRRemoveBtn();
	//EVE
	void OnEVEMinRadiusChange(int ival);
	void OnEVEMinRadiusText(const QString& str);
	void OnEVEMaxRadiusChange(int ival);
	void OnEVEMaxRadiusText(const QString& str);
	void OnEVEThresholdChange(int ival);
	void OnEVEThresholdText(const QString& str);
	void OnEVEAnalyzeBtn();
	//calculations
	void OnLoadA();
	void OnLoadB();
	void OnIdle();

	AnalysisContext* m_ctx;
	BrushTool* m_tool;

	VolumeData* m_vol1 = nullptr;
	VolumeData* m_vol2 = nullptr;

	//max volume value
	double m_max_value = 255.0;
	//default brush properties
	double m_dft_ini_thresh = 0.0;
	double m_dft_gm_falloff = 0.0;
	double m_dft_scl_falloff = 0.0;
	double m_dft_scl_translate = 0.0;
	//default ca properties
	double m_dft_ca_min = 0.0;
	double m_dft_ca_max = 0.0;
	double m_dft_ca_thresh = 0.0;
	double m_dft_ca_falloff = 1.0;
	double m_dft_nr_thresh = 0.0;
	double m_dft_nr_size = 0.0;
	double m_dft_dslt_c = 0.0;
	int m_dft_eve_min_radius = 5;
	int m_dft_eve_max_radius = 5;
	double m_dft_eve_thresh = 0.1;
	double m_dft_mask_alpha = 255.0;
	bool m_mask_undo_num_modified = false;

	QTabWidget* m_notebook = nullptr;
	QToolBar* m_toolbar = nullptr;
	QAction* m_undo_act = nullptr;
	QAction* m_redo_act = nullptr;
	QAction* m_append_act = nullptr;
	QAction* m_diffuse_act = nullptr;
	QAction* m_desel_act = nullptr;

	QCheckBox* m_edge_detect_chk = nullptr;
	QCheckBox* m_hidden_removal_chk = nullptr;
	QCheckBox* m_select_group_chk = nullptr;
	QSlider* m_brush_scl_translate_sldr = nullptr;
	QLineEdit* m_brush_scl_translate_text = nullptr;
	QSlider* m_brush_2dinfl_sldr = nullptr;
	QLineEdit* m_brush_2dinfl_text = nullptr;
	QSlider* m_brush_size1_sldr = nullptr;
	QLineEdit* m_brush_size1_text = nullptr;
	QCheckBox* m_brush_size2_chk = nullptr;
	QSlider* m_brush_size2_sldr = nullptr;
	QLineEdit* m_brush_size2_text = nullptr;
	QCheckBox* m_brush_use_absolute_chk = nullptr;
	QCheckBox* m_dslt_chk = nullptr;
	QLabel* st_dslt_r = nullptr;
	QSlider* m_dslt_r_sldr = nullptr;
	QLineEdit* m_dslt_r_text = nullptr;
	QLabel* st_dslt_q = nullptr;
	QSlider* m_dslt_q_sldr = nullptr;
	QLineEdit* m_dslt_q_text = nullptr;
	QLabel* st_dslt_c = nullptr;
	QSlider* m_dslt_c_sldr = nullptr;
	QLineEdit* m_dslt_c_text = nullptr;
	QRadioButton* m_brush_iterw_rb = nullptr;
	QRadioButton* m_brush_iters_rb = nullptr;
	QRadioButton* m_brush_iterss_rb = nullptr;
	QCheckBox* m_ca_select_only_chk = nullptr;
	QLineEdit* m_ca_min_text = nullptr;
	QLineEdit* m_ca_max_text = nullptr;
	QCheckBox* m_ca_ignore_max_chk = nullptr;
	QSlider* m_ca_thresh_sldr = nullptr;
	QLineEdit* m_ca_thresh_text = nullptr;
	QPushButton* m_ca_analyze_btn = nullptr;
	QPushButton* m_ca_multi_chann_btn = nullptr;
	QPushButton* m_ca_random_color_btn = nullptr;
	QPushButton* m_ca_annotations_btn = nullptr;
	QLineEdit* m_ca_comps_text = nullptr;
	QLineEdit* m_ca_volume_text = nullptr;
	QSlider* m_nr_size_sldr = nullptr;
	QLineEdit* m_nr_size_text = nullptr;
	QPushButton* m_nr_analyze_btn = nullptr;
	QPushButton* m_nr_remove_btn = nullptr;
	QSlider* m_eve_min_radius_sldr = nullptr;
	QLineEdit* m_eve_min_radius_text = nullptr;
	QSlider* m_eve_max_radius_sldr = nullptr;
	QLineEdit* m_eve_max_radius_text = nullptr;
	QSlider* m_eve_threshold_sldr = nullptr;
	QLineEdit* m_eve_threshold_text = nullptr;
	QPushButton* m_eve_analyze_btn = nullptr;
	QSlider* m_mask_overlay_alpha_sldr = nullptr;
	QLineEdit* m_mask_overlay_alpha_text = nullptr;
	QPushButton* m_calc_load_a_btn = nullptr;
	QLineEdit* m_calc_a_text = nullptr;
	QPushButton* m_calc_load_b_btn = nullptr;
	QLineEdit* m_calc_b_text = nullptr;
	QPushButton* m_calc_sub_btn = nullptr;
	QPushButton* m_calc_add_btn = nullptr;
	QPushButton* m_calc_div_btn = nullptr;
	QPushButton* m_calc_isc_btn = nullptr;
	QPushButton* m_calc_fill_btn = nullptr;

	QTimer* m_idleTimer = nullptr;
	bool m_loading = false;   // LoadDefault: no tool / view updates (wx m_cur_view is null then)
	BrushTool* Tool() const { return m_loading ? nullptr : m_tool; }
};

#endif // VVD_QT_BRUSHTOOLDIALOG_H
