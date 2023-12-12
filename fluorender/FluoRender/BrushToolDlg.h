#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/stopwatch.h>
#include "DLLExport.h"

#ifndef _BRUSHTOOLDLG_H_
#define _BRUSHTOOLDLG_H_

class VRenderView;
class VolumeData;

#define BRUSH_TOOL_ITER_WEAK	20
#define BRUSH_TOOL_ITER_NORMAL	50
#define BRUSH_TOOL_ITER_STRONG	100

class EXPORT_API BrushToolDlg : public wxPanel
{
public:
	enum
	{
		//group1
		//toolbar
		ID_BrushAppend = wxID_HIGHEST+1301,
		ID_BrushDesel,
		ID_BrushDiffuse,
		ID_BrushErase,
		ID_BrushClear,
		ID_BrushCreate,
		ID_BrushUndo,
		ID_BrushRedo,
		//selection strength
		//falloff
		ID_BrushSclTranslateSldr,
		ID_BrushSclTranslateText,
		//2dinfl
		ID_Brush2dinflSldr,
		ID_Brush2dinflText,
		//edge detect
		ID_BrushEdgeDetectChk,
		//hidden removal
		ID_BrushHiddenRemovalChk,
		//select group
		ID_BrushSelectGroupChk,
		//brush properties
		//brush size 1
		ID_BrushSize1Sldr,
		ID_BrushSize1Text,
		//brush size 2
		ID_BrushSize2Chk,
		ID_BrushSize2Sldr,
		ID_BrushSize2Text,
        //use absolute value for threahold
        ID_BrushUseAbsoluteValue,
		//iterations
		ID_BrushIterWRd,
		ID_BrushIterSRd,
		ID_BrushIterSSRd,
		//dslt brush
		ID_DSLTBrushChk,
		ID_DSLTBrushRadSldr,
		ID_DSLTBrushRadText,
		ID_DSLTBrushQualitySldr,
		ID_DSLTBrushQualityText,
		ID_DSLTBrushCSldr,
		ID_DSLTBrushCText,
		//component analyze
		ID_CASelectOnlyChk,
		ID_CAMinText,
		ID_CAMaxText,
		ID_CAIgnoreMaxChk,
		ID_CAThreshSldr,
		ID_CAThreshText,
		ID_CAAnalyzeBtn,
		ID_CAMultiChannBtn,
		ID_CARandomColorBtn,
		ID_CAAnnotationsBtn,
		ID_CACompsText,
		ID_CAVolumeText,
		//noise removal
		ID_NRSizeSldr,
		ID_NRSizeText,
		ID_NRAnalyzeBtn,
		ID_NRRemoveBtn,
		//EVE
		ID_EVEMinRadiusSldr,
		ID_EVEMinRadiusText,
		ID_EVEMaxRadiusSldr,
		ID_EVEMaxRadiusText,
		ID_EVEThresholdSldr,
		ID_EVEThresholdText,
		ID_EVEAnalyzeBtn,
		//help
		ID_HelpBtn,
		//default
		ID_SaveDefault,

		//group2
		//calculations
		//operand A
		ID_CalcLoadABtn,
		ID_CalcAText,
		//operand B
		ID_CalcLoadBBtn,
		ID_CalcBText,
		//two-opeartors
		ID_CalcSubBtn,
		ID_CalcAddBtn,
		ID_CalcDivBtn,
		ID_CalcIscBtn,
		//one-opeartors
		ID_CalcFillBtn,
        ID_Timer
	};

	BrushToolDlg(wxWindow* frame,
		wxWindow* parent);
	~BrushToolDlg();

	void GetSettings(VRenderView* vrv);

	//set the brush icon down
	void SelectBrush(int id);
	//update undo status
	void UpdateUndoRedo();

	//get some default values
	double GetDftCAMin() {return m_dft_ca_min;}
	void SetDftCAMin(double min) {m_dft_ca_min = min;}
	double GetDftCAMax() {return m_dft_ca_max;}
	void SetDftCAMax(double max) {m_dft_ca_max = max;}
	double GetDftCAThresh() {return m_dft_ca_thresh;}
	void SetDftCAThresh(double thresh) {m_dft_ca_thresh = thresh;}
	double GetDftCAFalloff() {return m_dft_ca_falloff;}
	void SetDftCAFalloff(double falloff) {m_dft_ca_falloff = falloff;}
	double GetDftNRThresh() {return m_dft_nr_thresh;}
	void SetDftNRThresh(double thresh) {m_dft_nr_thresh = thresh;}
	double GetDftNRSize() {return m_dft_nr_size;}
	void SetDftNRSize(double size) {m_dft_nr_size = size;}

	int GetDftEVEMinRadius() { return m_dft_eve_min_radius; }
	void SetDftEVEMinRadius(int r) { m_dft_eve_min_radius = r; }
	int GetDftEVEMaxRadius() { return m_dft_eve_max_radius; }
	void SetDftEVEMaxRadius(int r) { m_dft_eve_max_radius = r; }
	double GetDftEVEThresh() { return m_dft_eve_thresh; }
	void SetDftEVEThresh(double thresh) { m_dft_eve_thresh = thresh; }
    
    void SetBrushSclTranslate(double val)
    {
        wxString str;
        m_dft_scl_translate = val;
        str = wxString::Format("%.1f", m_dft_scl_translate*m_max_value);
        m_brush_scl_translate_sldr->SetRange(0, int(m_max_value*10.0));
        m_brush_scl_translate_text->SetValue(str);
    }

	//save default
	void SaveDefault();

	void SetCalcA(wxString name){m_calc_a_text->SetValue(name);}
	void SetCalcB(wxString name){m_calc_b_text->SetValue(name);}
    
    void BrushUndo();
    void BrushRedo();
	
private:
	wxWindow* m_frame;

	//current view
	VRenderView *m_cur_view;
	//current volume
	VolumeData *m_vol1;
	VolumeData *m_vol2;

	//max volume value
	double m_max_value;
	//default brush properties
	double m_dft_ini_thresh;
	double m_dft_gm_falloff;
	double m_dft_scl_falloff;
	double m_dft_scl_translate;
	//default ca properties
	double m_dft_ca_min;
	double m_dft_ca_max;
	double m_dft_ca_thresh;
	double m_dft_ca_falloff;
	double m_dft_nr_thresh;
	double m_dft_nr_size;

	double m_dft_dslt_c;

	int m_dft_eve_min_radius;
	int m_dft_eve_max_radius;
	double m_dft_eve_thresh;
    
    bool mask_undo_num_modified;
	
	//tab control
	wxNotebook *m_notebook;
	//paint tools
	//toolbar
	wxToolBar *m_toolbar;

	//stop at boundary
	wxCheckBox* m_edge_detect_chk;
	//hidden removal
	wxCheckBox* m_hidden_removal_chk;
	//group selection
	wxCheckBox* m_select_group_chk;
	//translate
	wxSlider* m_brush_scl_translate_sldr;
	wxTextCtrl* m_brush_scl_translate_text;
	//2d influence
	wxSlider* m_brush_2dinfl_sldr;
	wxTextCtrl* m_brush_2dinfl_text;
	//brush properties
	//size 1
	wxSlider* m_brush_size1_sldr;
	wxTextCtrl *m_brush_size1_text;
	//size 2
	wxCheckBox* m_brush_size2_chk;
	wxSlider* m_brush_size2_sldr;
	wxTextCtrl* m_brush_size2_text;
    //use absolute value
    wxCheckBox *m_brush_use_absolute_chk;
	//dslt
	wxCheckBox* m_dslt_chk;
	wxStaticText *st_dslt_r;
	wxSlider* m_dslt_r_sldr;
	wxTextCtrl* m_dslt_r_text;
	wxStaticText *st_dslt_q;
	wxSlider* m_dslt_q_sldr;
	wxTextCtrl* m_dslt_q_text;
	wxStaticText *st_dslt_c;
	wxSlider* m_dslt_c_sldr;
	wxTextCtrl* m_dslt_c_text;
	//growth
	wxRadioButton* m_brush_iterw_rb;
	wxRadioButton* m_brush_iters_rb;
	wxRadioButton* m_brush_iterss_rb;
	//component analyzer
	wxCheckBox *m_ca_select_only_chk;
	wxTextCtrl *m_ca_min_text;
	wxTextCtrl *m_ca_max_text;
	wxCheckBox *m_ca_ignore_max_chk;
	wxSlider *m_ca_thresh_sldr;
	wxTextCtrl *m_ca_thresh_text;
	wxButton *m_ca_analyze_btn;
	wxButton *m_ca_multi_chann_btn;
	wxButton *m_ca_random_color_btn;
	wxButton *m_ca_annotations_btn;
	wxTextCtrl *m_ca_comps_text;
	wxTextCtrl *m_ca_volume_text;
	//noise removal
	wxSlider *m_nr_size_sldr;
	wxTextCtrl *m_nr_size_text;
	wxButton *m_nr_analyze_btn;
	wxButton *m_nr_remove_btn;
	//EVE
	wxSlider* m_eve_min_radius_sldr;
	wxTextCtrl* m_eve_min_radius_text;
	wxSlider* m_eve_max_radius_sldr;
	wxTextCtrl* m_eve_max_radius_text;
	wxSlider* m_eve_threshold_sldr;
	wxTextCtrl* m_eve_threshold_text;
	wxButton* m_eve_analyze_btn;
	//help button
	//wxButton* m_help_btn;

	//calculations
	//operands
	wxButton *m_calc_load_a_btn;
	wxTextCtrl *m_calc_a_text;
	wxButton *m_calc_load_b_btn;
	wxTextCtrl *m_calc_b_text;
	//two-operators
	wxButton *m_calc_sub_btn;
	wxButton *m_calc_add_btn;
	wxButton *m_calc_div_btn;
	wxButton *m_calc_isc_btn;
	//one-operators
	wxButton *m_calc_fill_btn;

	wxStopWatch m_watch;
    
    wxTimer *m_idleTimer;

#if defined(__WXGTK__)
	static constexpr int size_fix_w = 10;
#else
	static constexpr int size_fix_w = 0;
#endif

private:
	void LoadDefault();
	wxWindow* CreateBrushPage(wxWindow *parent);
	wxWindow* CreateCalculationPage(wxWindow *parent);
	wxWindow* CreateAnalysisPage(wxWindow *parent);

	void LoadVolumes();
	void DrawBrush(double val);

	//event handling
	//paint tools
	//brush commands
	void OnBrushUndo(wxCommandEvent& event);
	void OnBrushRedo(wxCommandEvent& event);
	void OnBrushAppend(wxCommandEvent& event);
	void OnBrushDesel(wxCommandEvent& event);
	void OnBrushDiffuse(wxCommandEvent& event);
	void OnBrushErase(wxCommandEvent& event);
	void OnBrushClear(wxCommandEvent& event);
	void OnBrushCreate(wxCommandEvent& event);
	//selection adjustment
	//2d influence
	void OnBrush2dinflChange(wxScrollEvent &event);
	void OnBrush2dinflText(wxCommandEvent &event);
	//edge detect
	void OnBrushEdgeDetectChk(wxCommandEvent &event);
	//hidden removal
	void OnBrushHiddenRemovalChk(wxCommandEvent &event);
	//select group
	void OnBrushSelectGroupChk(wxCommandEvent &event);
	//translate
	void OnBrushSclTranslateChange(wxScrollEvent &event);
	void OnBrushSclTranslateText(wxCommandEvent &event);
	void OnBrushSclTranslateTextEnter(wxCommandEvent &event);
	//brush properties
	//brush size 1
	void OnBrushSize1Change(wxScrollEvent &event);
	void OnBrushSize1Text(wxCommandEvent &event);
	//brush size 2
	void OnBrushSize2Chk(wxCommandEvent &event);
	void OnBrushSize2Change(wxScrollEvent &event);
	void OnBrushSize2Text(wxCommandEvent &event);
    //use absolute value
    void OnBrushUseAbsoluteValueChk(wxCommandEvent &event);
	//dslt
	void OnDSLTBrushChk(wxCommandEvent &event);
	void OnDSLTBrushRadChange(wxScrollEvent &event);
	void OnDSLTBrushRadText(wxCommandEvent &event);
	void OnDSLTBrushQualityChange(wxScrollEvent &event);
	void OnDSLTBrushQualityText(wxCommandEvent &event);
	void OnDSLTBrushCChange(wxScrollEvent &event);
	void OnDSLTBrushCText(wxCommandEvent &event);
	//brush iterations
	void OnBrushIterCheck(wxCommandEvent& event);
	//component analyzer
	void OnCAThreshChange(wxScrollEvent &event);
	void OnCAThreshText(wxCommandEvent &event);
	void OnCAAnalyzeBtn(wxCommandEvent &event);
	void OnCAIgnoreMaxChk(wxCommandEvent &event);
	void OnCAMultiChannBtn(wxCommandEvent &event);
	void OnCARandomColorBtn(wxCommandEvent &event);
	void OnCAAnnotationsBtn(wxCommandEvent &event);
	//noise removal
	void OnNRSizeChange(wxScrollEvent &event);
	void OnNRSizeText(wxCommandEvent &event);
	void OnNRAnalyzeBtn(wxCommandEvent &event);
	void OnNRRemoveBtn(wxCommandEvent &event);
	//EVE
	void OnEVEMinRadiusChange(wxScrollEvent& event);
	void OnEVEMinRadiusText(wxCommandEvent& event);
	void OnEVEMaxRadiusChange(wxScrollEvent& event);
	void OnEVEMaxRadiusText(wxCommandEvent& event);
	void OnEVEThresholdChange(wxScrollEvent& event);
	void OnEVEThresholdText(wxCommandEvent& event);
	void OnEVEAnalyzeBtn(wxCommandEvent& event);
	//help
	void OnHelpBtn(wxCommandEvent& event);

	//calculations
	//operands
	void OnLoadA(wxCommandEvent &event);
	void OnLoadB(wxCommandEvent &event);
	//operators
	void OnCalcSub(wxCommandEvent &event);
	void OnCalcAdd(wxCommandEvent &event);
	void OnCalcDiv(wxCommandEvent &event);
	void OnCalcIsc(wxCommandEvent &event);
	//one-operators
	void OnCalcFill(wxCommandEvent &event);
    
    void OnIdle(wxTimerEvent& event);

	DECLARE_EVENT_TABLE();
};

#endif//_BRUSHTOOLDLG_H_
