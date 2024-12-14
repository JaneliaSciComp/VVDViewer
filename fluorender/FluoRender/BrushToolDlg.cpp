#include "BrushToolDlg.h"
#include "VRenderFrame.h"
#include <wx/valnum.h>
#include <wx/stdpaths.h>
#include "Formats/png_resource.h"

//resources
#include "img/icons.h"

#define GM_2_ESTR(x) (1.0 - sqrt(1.0 - (x - 1.0) * (x - 1.0)))

BEGIN_EVENT_TABLE(BrushToolDlg, wxPanel)
//paint tools
//brush commands
EVT_TOOL(ID_BrushUndo, BrushToolDlg::OnBrushUndo)
EVT_TOOL(ID_BrushRedo, BrushToolDlg::OnBrushRedo)
EVT_TOOL(ID_BrushAppend, BrushToolDlg::OnBrushAppend)
EVT_TOOL(ID_BrushDesel, BrushToolDlg::OnBrushDesel)
EVT_TOOL(ID_BrushDiffuse, BrushToolDlg::OnBrushDiffuse)
EVT_TOOL(ID_BrushClear, BrushToolDlg::OnBrushClear)
EVT_TOOL(ID_BrushErase, BrushToolDlg::OnBrushErase)
EVT_TOOL(ID_BrushCreate, BrushToolDlg::OnBrushCreate)
//help
EVT_TOOL(ID_HelpBtn, BrushToolDlg::OnHelpBtn)
//selection adjustment
//translate
EVT_COMMAND_SCROLL(ID_BrushSclTranslateSldr, BrushToolDlg::OnBrushSclTranslateChange)
EVT_TEXT(ID_BrushSclTranslateText, BrushToolDlg::OnBrushSclTranslateText)
EVT_TEXT_ENTER(ID_BrushSclTranslateText, BrushToolDlg::OnBrushSclTranslateTextEnter)
//2d influence
EVT_COMMAND_SCROLL(ID_Brush2dinflSldr, BrushToolDlg::OnBrush2dinflChange)
EVT_TEXT(ID_Brush2dinflText, BrushToolDlg::OnBrush2dinflText)
//edge detect
EVT_CHECKBOX(ID_BrushEdgeDetectChk, BrushToolDlg::OnBrushEdgeDetectChk)
//hidden removal
EVT_CHECKBOX(ID_BrushHiddenRemovalChk, BrushToolDlg::OnBrushHiddenRemovalChk)
//select group
EVT_CHECKBOX(ID_BrushSelectGroupChk, BrushToolDlg::OnBrushSelectGroupChk)
//brush properties
//brush size 1
EVT_COMMAND_SCROLL(ID_BrushSize1Sldr, BrushToolDlg::OnBrushSize1Change)
EVT_TEXT(ID_BrushSize1Text, BrushToolDlg::OnBrushSize1Text)
//brush size 2
EVT_CHECKBOX(ID_BrushSize2Chk, BrushToolDlg::OnBrushSize2Chk)
EVT_COMMAND_SCROLL(ID_BrushSize2Sldr, BrushToolDlg::OnBrushSize2Change)
EVT_TEXT(ID_BrushSize2Text, BrushToolDlg::OnBrushSize2Text)
//use absolute value
EVT_CHECKBOX(ID_BrushUseAbsoluteValue, BrushToolDlg::OnBrushUseAbsoluteValueChk)
//dslt
EVT_CHECKBOX(ID_DSLTBrushChk, BrushToolDlg::OnDSLTBrushChk)
EVT_COMMAND_SCROLL(ID_DSLTBrushRadSldr, BrushToolDlg::OnDSLTBrushRadChange)
EVT_TEXT(ID_DSLTBrushRadText, BrushToolDlg::OnDSLTBrushRadText)
EVT_COMMAND_SCROLL(ID_DSLTBrushQualitySldr, BrushToolDlg::OnDSLTBrushQualityChange)
EVT_TEXT(ID_DSLTBrushQualityText, BrushToolDlg::OnDSLTBrushQualityText)
EVT_COMMAND_SCROLL(ID_DSLTBrushCSldr, BrushToolDlg::OnDSLTBrushCChange)
EVT_TEXT(ID_DSLTBrushCText, BrushToolDlg::OnDSLTBrushCText)
//brush iterrations
EVT_RADIOBUTTON(ID_BrushIterWRd, BrushToolDlg::OnBrushIterCheck)
EVT_RADIOBUTTON(ID_BrushIterSRd, BrushToolDlg::OnBrushIterCheck)
EVT_RADIOBUTTON(ID_BrushIterSSRd, BrushToolDlg::OnBrushIterCheck)
//component analyzer
EVT_COMMAND_SCROLL(ID_CAThreshSldr, BrushToolDlg::OnCAThreshChange)
EVT_TEXT(ID_CAThreshText, BrushToolDlg::OnCAThreshText)
EVT_CHECKBOX(ID_CAIgnoreMaxChk, BrushToolDlg::OnCAIgnoreMaxChk)
EVT_BUTTON(ID_CAAnalyzeBtn, BrushToolDlg::OnCAAnalyzeBtn)
EVT_BUTTON(ID_CAMultiChannBtn, BrushToolDlg::OnCAMultiChannBtn)
EVT_BUTTON(ID_CARandomColorBtn, BrushToolDlg::OnCARandomColorBtn)
EVT_BUTTON(ID_CAAnnotationsBtn, BrushToolDlg::OnCAAnnotationsBtn)
//noise removal
EVT_COMMAND_SCROLL(ID_NRSizeSldr, BrushToolDlg::OnNRSizeChange)
EVT_TEXT(ID_NRSizeText, BrushToolDlg::OnNRSizeText)
EVT_BUTTON(ID_NRAnalyzeBtn, BrushToolDlg::OnNRAnalyzeBtn)
EVT_BUTTON(ID_NRRemoveBtn, BrushToolDlg::OnNRRemoveBtn)
//EVE
EVT_COMMAND_SCROLL(ID_EVEMinRadiusSldr, BrushToolDlg::OnEVEMinRadiusChange)
EVT_TEXT(ID_EVEMinRadiusText, BrushToolDlg::OnEVEMinRadiusText)
EVT_COMMAND_SCROLL(ID_EVEMaxRadiusSldr, BrushToolDlg::OnEVEMaxRadiusChange)
EVT_TEXT(ID_EVEMaxRadiusText, BrushToolDlg::OnEVEMaxRadiusText)
EVT_COMMAND_SCROLL(ID_EVEThresholdSldr, BrushToolDlg::OnEVEThresholdChange)
EVT_TEXT(ID_EVEThresholdText, BrushToolDlg::OnEVEThresholdText)
EVT_BUTTON(ID_EVEAnalyzeBtn, BrushToolDlg::OnEVEAnalyzeBtn)
//Mask Display
EVT_COMMAND_SCROLL(ID_MaskAlphaSldr, BrushToolDlg::OnMaskAlphaChange)
EVT_TEXT(ID_MaskAlphaText, BrushToolDlg::OnMaskAlphaText)

//calculations
//operands
EVT_BUTTON(ID_CalcLoadABtn, BrushToolDlg::OnLoadA)
EVT_BUTTON(ID_CalcLoadBBtn, BrushToolDlg::OnLoadB)
//operators
EVT_BUTTON(ID_CalcSubBtn, BrushToolDlg::OnCalcSub)
EVT_BUTTON(ID_CalcAddBtn, BrushToolDlg::OnCalcAdd)
EVT_BUTTON(ID_CalcDivBtn, BrushToolDlg::OnCalcDiv)
EVT_BUTTON(ID_CalcIscBtn, BrushToolDlg::OnCalcIsc)
//one-operators
EVT_BUTTON(ID_CalcFillBtn, BrushToolDlg::OnCalcFill)
EVT_TIMER(ID_Timer, BrushToolDlg::OnIdle)
END_EVENT_TABLE()

wxWindow* BrushToolDlg::CreateBrushPage(wxWindow *parent)
{
	wxPanel *page = new wxPanel(parent);
	wxStaticText *st = 0;
	//validator: floating point 1
	wxFloatingPointValidator<double> vald_fp1(1);
	//validator: floating point 2
	wxFloatingPointValidator<double> vald_fp2(2);
	//validator: floating point 3
	wxFloatingPointValidator<double> vald_fp3(3);
	vald_fp3.SetRange(0.0, 1.0);
	//validator: integer
	wxIntegerValidator<unsigned int> vald_int;

	//toolbar
	m_toolbar = new wxToolBar(page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_FLAT|wxTB_TOP|wxTB_NODIVIDER|wxTB_TEXT);
    m_toolbar->SetToolBitmapSize(wxSize(20,20));
	m_toolbar->AddTool(ID_BrushUndo, "Undo",
		wxGetBitmapFromMemory(undo),
		"Rollback previous brush operation");
	m_toolbar->AddTool(ID_BrushRedo, "Redo",
		wxGetBitmapFromMemory(redo),
		"Redo the rollback brush operation");
	m_toolbar->AddSeparator();
	m_toolbar->AddCheckTool(ID_BrushAppend, "Select",
		wxGetBitmapFromMemory(listicon_brushappend),
		wxNullBitmap,
		"Highlight structures by painting on the render view (hold Shift)");
	m_toolbar->AddCheckTool(ID_BrushDiffuse, "Diffuse",
		wxGetBitmapFromMemory(listicon_brushdiffuse),
		wxNullBitmap,
		"Diffuse highlighted structures by painting (hold Z)");
	m_toolbar->AddCheckTool(ID_BrushDesel, "Unselect",
		wxGetBitmapFromMemory(listicon_brushdesel),
		wxNullBitmap,
		"Reset highlighted structures by painting (hold X)");
	m_toolbar->AddTool(ID_BrushClear, "Reset All",
		wxGetBitmapFromMemory(listicon_brushclear),
		"Reset all highlighted structures");
	m_toolbar->AddSeparator();
	m_toolbar->AddTool(ID_BrushErase, "Erase",
		wxGetBitmapFromMemory(listicon_brusherase),
		"Erase highlighted structures");
	m_toolbar->AddTool(ID_BrushCreate, "Extract",
		wxGetBitmapFromMemory(listicon_brushcreate),
		"Extract highlighted structures out and create a new volume");
    wxColour col =  m_notebook->GetThemeBackgroundColour();
    if (col.Ok())
        m_toolbar->SetBackgroundColour(col);
	m_toolbar->Realize();

	//Selection adjustment
	wxBoxSizer *sizer1 = new wxStaticBoxSizer(
		new wxStaticBox(page, wxID_ANY, "Selection Settings"),
		wxVERTICAL);
	//stop at boundary
	wxBoxSizer *sizer1_1 = new wxBoxSizer(wxHORIZONTAL);
	m_edge_detect_chk = new wxCheckBox(page, ID_BrushEdgeDetectChk, "Edge Detect:",
		wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	m_hidden_removal_chk = new wxCheckBox(page, ID_BrushHiddenRemovalChk, "Visible Only:",
		wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	m_select_group_chk = new wxCheckBox(page, ID_BrushSelectGroupChk, "Apply to Group:",
		wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_brush_use_absolute_chk = new wxCheckBox(page, ID_BrushUseAbsoluteValue, "Absolute Value:",
        wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	sizer1_1->Add(m_edge_detect_chk, 0, wxALIGN_CENTER);
	sizer1_1->Add(5, 5);
	sizer1_1->Add(m_hidden_removal_chk, 0, wxALIGN_CENTER);
	sizer1_1->Add(5, 5);
	sizer1_1->Add(m_select_group_chk, 0, wxALIGN_CENTER);
    sizer1_1->Add(5, 5);
    sizer1_1->Add(m_brush_use_absolute_chk, 0, wxALIGN_CENTER);
    
    m_edge_detect_chk->Hide();
    m_hidden_removal_chk->Hide();
    
	//threshold4
	wxBoxSizer *sizer1_2 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "Threshold:",
		wxDefaultPosition, wxSize(70, 20));
	m_brush_scl_translate_sldr = new wxSlider(page, ID_BrushSclTranslateSldr, 0, 0, 2550,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_brush_scl_translate_text = new wxTextCtrl(page, ID_BrushSclTranslateText, "0.0",
		wxDefaultPosition, wxSize(50+size_fix_w, 20), wxTE_PROCESS_ENTER, vald_fp1);
	sizer1_2->Add(5, 5);
	sizer1_2->Add(st, 0, wxALIGN_CENTER);
	sizer1_2->Add(m_brush_scl_translate_sldr, 1, wxEXPAND);
	sizer1_2->Add(m_brush_scl_translate_text, 0, wxALIGN_CENTER);
	sizer1_2->Add(15, 15);
	//2d
	wxBoxSizer *sizer1_4 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "2D Adj. Infl.:",
		wxDefaultPosition, wxSize(70, 20));
	m_brush_2dinfl_sldr = new wxSlider(page, ID_Brush2dinflSldr, 100, 0, 200,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL|wxSL_INVERSE);
	m_brush_2dinfl_text = new wxTextCtrl(page, ID_Brush2dinflText, "1.00",
		wxDefaultPosition, wxSize(40+size_fix_w, 20), 0, vald_fp2);
	sizer1_4->Add(5, 5);
	sizer1_4->Add(st, 0, wxALIGN_CENTER);
	sizer1_4->Add(m_brush_2dinfl_sldr, 1, wxEXPAND);
	sizer1_4->Add(m_brush_2dinfl_text, 0, wxALIGN_CENTER);
	sizer1_4->Add(15, 15);
	//sizer1
	sizer1->Add(sizer1_1, 0, wxEXPAND);
	sizer1->Add(10, 10);
	sizer1->Add(sizer1_2, 0, wxEXPAND);
	sizer1->Add(sizer1_4, 0, wxEXPAND);
	sizer1->Hide(sizer1_4, true);

	//Brush properties
	wxBoxSizer *sizer2 = new wxStaticBoxSizer(
		new wxStaticBox(page, wxID_ANY, "Brush Properties"),
		wxVERTICAL);
	wxBoxSizer *sizer2_1 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "Brush sizes can also be set with mouse wheel in painting mode.");
	sizer2_1->Add(5, 5);
	sizer2_1->Add(st, 0, wxALIGN_CENTER);
	//brush size 1
	wxBoxSizer *sizer2_2 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "Center Size:",
		wxDefaultPosition, wxSize(80, 20));
	m_brush_size1_sldr = new wxSlider(page, ID_BrushSize1Sldr, 10, 1, 300,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_brush_size1_text = new wxTextCtrl(page, ID_BrushSize1Text, "10",
		wxDefaultPosition, wxSize(50+size_fix_w, 20), 0, vald_int);
	sizer2_2->Add(5, 5);
	sizer2_2->Add(st, 0, wxALIGN_CENTER);
	sizer2_2->Add(m_brush_size1_sldr, 1, wxEXPAND);
	sizer2_2->Add(m_brush_size1_text, 0, wxALIGN_CENTER);
	st = new wxStaticText(page, 0, "px",
		wxDefaultPosition, wxSize(25, 15));
	sizer2_2->Add(st, 0, wxALIGN_CENTER);
	//brush size 2
	wxBoxSizer *sizer2_3 = new wxBoxSizer(wxHORIZONTAL);
	m_brush_size2_chk = new wxCheckBox(page, ID_BrushSize2Chk, "GrowSize",
		wxDefaultPosition, wxSize(80, 20), wxALIGN_RIGHT);
	st = new wxStaticText(page, 0, ":",
		wxDefaultPosition, wxSize(5, 20), wxALIGN_RIGHT);
	st->Hide();
	m_brush_size2_sldr = new wxSlider(page, ID_BrushSize2Sldr, 30, 1, 300,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_brush_size2_text = new wxTextCtrl(page, ID_BrushSize2Text, "30",
		wxDefaultPosition, wxSize(50+size_fix_w, 20), 0, vald_int);
	m_brush_size2_chk->SetValue(true);
	m_brush_size2_sldr->Enable();
	m_brush_size2_text->Enable();

	m_brush_size2_chk->Hide();
	m_brush_size2_sldr->Hide();
	m_brush_size2_text->Hide();

	sizer2_3->Add(m_brush_size2_chk, 0, wxALIGN_CENTER);
	sizer2_3->Add(st, 0, wxALIGN_CENTER);
	sizer2_3->Add(m_brush_size2_sldr, 1, wxEXPAND);
	sizer2_3->Add(m_brush_size2_text, 0, wxALIGN_CENTER);
	st = new wxStaticText(page, 0, "px",
		wxDefaultPosition, wxSize(25, 15));

	st->Hide();

	sizer2_3->Add(st, 0, wxALIGN_CENTER);
	//iterations
	wxBoxSizer *sizer2_4 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "Growth:",
		wxDefaultPosition, wxSize(70, 20));
	m_brush_iterw_rb = new wxRadioButton(page, ID_BrushIterWRd, "Weak",
		wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_brush_iters_rb = new wxRadioButton(page, ID_BrushIterSRd, "Normal",
		wxDefaultPosition, wxDefaultSize);
	m_brush_iterss_rb = new wxRadioButton(page, ID_BrushIterSSRd, "Strong",
		wxDefaultPosition, wxDefaultSize);
	m_brush_iterw_rb->SetValue(false);
	m_brush_iters_rb->SetValue(true);
	m_brush_iterss_rb->SetValue(false);

	//m_brush_iterw_rb->Hide();
	//m_brush_iters_rb->Hide();
	//m_brush_iterss_rb->Hide();

	sizer2_4->Add(5, 5);
	sizer2_4->Add(st, 0, wxALIGN_CENTER);
	sizer2_4->Add(m_brush_iterw_rb, 0, wxALIGN_CENTER);
	sizer2_4->Add(15, 15);
	sizer2_4->Add(m_brush_iters_rb, 0, wxALIGN_CENTER);
	sizer2_4->Add(15, 15);
	sizer2_4->Add(m_brush_iterss_rb, 0, wxALIGN_CENTER);

	m_dslt_chk = new wxCheckBox(page, ID_DSLTBrushChk, "Use DSLT",
		wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	st_dslt_r = new wxStaticText(page, 0, "Radius:",
		wxDefaultPosition, wxSize(70, 20));
	m_dslt_r_sldr = new wxSlider(page, ID_DSLTBrushRadSldr, 10, 1, 100,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_dslt_r_text = new wxTextCtrl(page, ID_DSLTBrushRadText, "10",
		wxDefaultPosition, wxSize(50+size_fix_w, 20), 0, vald_int);
	wxBoxSizer *sizer2_5 = new wxBoxSizer(wxHORIZONTAL);
	sizer2_5->Add(5, 5);
	sizer2_5->Add(st_dslt_r, 0, wxALIGN_CENTER);
	sizer2_5->Add(m_dslt_r_sldr, 1, wxEXPAND);
	sizer2_5->Add(m_dslt_r_text, 0, wxALIGN_CENTER);
	st_dslt_q = new wxStaticText(page, 0, "Quality:",
		wxDefaultPosition, wxSize(70, 20));
	m_dslt_q_sldr = new wxSlider(page, ID_DSLTBrushQualitySldr, 3, 1, 10,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_dslt_q_text = new wxTextCtrl(page, ID_DSLTBrushQualityText, "3",
		wxDefaultPosition, wxSize(50+size_fix_w, 20), 0, vald_int);
	wxBoxSizer *sizer2_6 = new wxBoxSizer(wxHORIZONTAL);
	sizer2_6->Add(5, 5);
	sizer2_6->Add(st_dslt_q, 0, wxALIGN_CENTER);
	sizer2_6->Add(m_dslt_q_sldr, 1, wxEXPAND);
	sizer2_6->Add(m_dslt_q_text, 0, wxALIGN_CENTER);
	st_dslt_c = new wxStaticText(page, 0, "C:",
		wxDefaultPosition, wxSize(70, 20));
	m_dslt_c_sldr = new wxSlider(page, ID_DSLTBrushCSldr, 0, 0, 2550,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_dslt_c_text = new wxTextCtrl(page, ID_DSLTBrushCText, "0.0",
		wxDefaultPosition, wxSize(50, 20), 0, vald_fp1);
	wxBoxSizer *sizer2_7 = new wxBoxSizer(wxHORIZONTAL);
	sizer2_7->Add(5, 5);
	sizer2_7->Add(st_dslt_c, 0, wxALIGN_CENTER);
	sizer2_7->Add(m_dslt_c_sldr, 1, wxEXPAND);
	sizer2_7->Add(m_dslt_c_text, 0, wxALIGN_CENTER);

	//sizer2
	sizer2->Add(sizer2_4, 0, wxEXPAND);
	sizer2->Add(10, 10);
	sizer2->Add(sizer2_2, 0, wxEXPAND);
	sizer2->Add(sizer2_3, 0, wxEXPAND);
	sizer2->Add(10, 10);
	sizer2->Add(sizer2_1, 0, wxEXPAND);
	sizer2->Add(10, 10);
	sizer2->Add(m_dslt_chk, 0, wxALIGN_LEFT);
	sizer2->Add(5, 5);
	sizer2->Add(sizer2_5, 0, wxEXPAND);
	sizer2->Add(sizer2_6, 0, wxEXPAND);
	sizer2->Add(sizer2_7, 0, wxEXPAND);

    wxBoxSizer* sizer3 = new wxStaticBoxSizer(
        new wxStaticBox(page, wxID_ANY, "Mask Display Settings"),
        wxVERTICAL);
    wxBoxSizer* sizer3_1 = new wxBoxSizer(wxHORIZONTAL);
    st = new wxStaticText(page, 0, "Mask Intensity:",
        wxDefaultPosition, wxSize(80, 20));
    m_mask_overlay_alpha_sldr = new wxSlider(page, ID_MaskAlphaSldr, 255, 0, 255,
        wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
    m_mask_overlay_alpha_text = new wxTextCtrl(page, ID_MaskAlphaText, "255",
        wxDefaultPosition, wxSize(50 + size_fix_w, 20), 0, vald_int);
    sizer3_1->Add(5, 5);
    sizer3_1->Add(st, 0, wxALIGN_CENTER);
    sizer3_1->Add(m_mask_overlay_alpha_sldr, 1, wxEXPAND);
    sizer3_1->Add(m_mask_overlay_alpha_text, 0, wxALIGN_CENTER);
    sizer3->Add(sizer3_1, 0, wxEXPAND);

	//vertical sizer
	wxBoxSizer* sizer_v = new wxBoxSizer(wxVERTICAL);
	sizer_v->Add(10, 10);
	sizer_v->Add(m_toolbar, 0, wxEXPAND);
	sizer_v->Add(10, 30);
	sizer_v->Add(sizer1, 0, wxEXPAND);
	sizer_v->Add(10, 30);
	sizer_v->Add(sizer2, 0, wxEXPAND);
	sizer_v->Add(10, 30);
    sizer_v->Add(sizer3, 0, wxEXPAND);
    sizer_v->Add(10, 30);

	st_dslt_r->Disable();
	m_dslt_r_sldr->Disable();
	m_dslt_r_text->Disable();
	st_dslt_q->Disable();
	m_dslt_q_sldr->Disable();
	m_dslt_q_text->Disable();
	st_dslt_c->Disable();
	m_dslt_c_sldr->Disable();
	m_dslt_c_text->Disable();
    
    m_dslt_chk->Hide();
    st_dslt_r->Hide();
    m_dslt_r_sldr->Hide();
    m_dslt_r_text->Hide();
    st_dslt_q->Hide();
    m_dslt_q_sldr->Hide();
    m_dslt_q_text->Hide();
    st_dslt_c->Hide();
    m_dslt_c_sldr->Hide();
    m_dslt_c_text->Hide();

	//set the page
	page->SetSizer(sizer_v);
	return page;
}

wxWindow* BrushToolDlg::CreateCalculationPage(wxWindow *parent)
{
	wxPanel *page = new wxPanel(parent);
	wxStaticText *st = 0;

	//operand A
	wxBoxSizer *sizer1 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "Volume A:",
		wxDefaultPosition, wxSize(75, 20));
	m_calc_load_a_btn = new wxButton(page, ID_CalcLoadABtn, "Load",
		wxDefaultPosition, wxSize(50, 20));
	m_calc_a_text = new wxTextCtrl(page, ID_CalcAText, "",
		wxDefaultPosition, wxSize(200, 20));
	sizer1->Add(st, 0, wxALIGN_CENTER);
	sizer1->Add(m_calc_load_a_btn, 0, wxALIGN_CENTER);
	sizer1->Add(m_calc_a_text, 1, wxEXPAND);
	//operand B
	wxBoxSizer *sizer2 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "Volume B:",
		wxDefaultPosition, wxSize(75, 20));
	m_calc_load_b_btn = new wxButton(page, ID_CalcLoadBBtn, "Load",
		wxDefaultPosition, wxSize(50, 20));
	m_calc_b_text = new wxTextCtrl(page, ID_CalcBText, "",
		wxDefaultPosition, wxSize(200, 20));
	sizer2->Add(st, 0, wxALIGN_CENTER);
	sizer2->Add(m_calc_load_b_btn, 0, wxALIGN_CENTER);
	sizer2->Add(m_calc_b_text, 1, wxEXPAND);
	//single operators
	wxBoxSizer *sizer3 = new wxStaticBoxSizer(
		new wxStaticBox(page, wxID_ANY,
		"Single-valued Operators (Require A)"), wxVERTICAL);
	//sizer3
	m_calc_fill_btn = new wxButton(page, ID_CalcFillBtn, "3D Fill Holes",
		wxDefaultPosition, wxDefaultSize);
	sizer3->Add(m_calc_fill_btn, 0, wxEXPAND);
	//two operators
	wxBoxSizer *sizer4 = new wxStaticBoxSizer(
		new wxStaticBox(page, wxID_ANY,
		"Two-valued Operators (Require both A and B)"), wxHORIZONTAL);
	m_calc_sub_btn = new wxButton(page, ID_CalcSubBtn, "Subtract",
		wxDefaultPosition, wxSize(50, 25));
	m_calc_add_btn = new wxButton(page, ID_CalcAddBtn, "Max",
		wxDefaultPosition, wxSize(50, 25));
	m_calc_div_btn = new wxButton(page, ID_CalcDivBtn, "Divide",
		wxDefaultPosition, wxSize(50, 25));
	m_calc_isc_btn = new wxButton(page, ID_CalcIscBtn, "Colocalize",
		wxDefaultPosition, wxSize(50, 25));
	sizer4->Add(m_calc_sub_btn, 1, wxEXPAND);
	sizer4->Add(m_calc_add_btn, 1, wxEXPAND);
	sizer4->Add(m_calc_div_btn, 1, wxEXPAND);
	sizer4->Add(m_calc_isc_btn, 1, wxEXPAND);

	//vertical sizer
	wxBoxSizer* sizer_v = new wxBoxSizer(wxVERTICAL);
	sizer_v->Add(10, 10);
	sizer_v->Add(sizer1, 0, wxEXPAND);
	sizer_v->Add(10, 10);
	sizer_v->Add(sizer2, 0, wxEXPAND);
	sizer_v->Add(10, 30);
	sizer_v->Add(sizer3, 0, wxEXPAND);
	sizer_v->Add(10, 30);
	sizer_v->Add(sizer4, 0, wxEXPAND);
	sizer_v->Add(10, 10);

	//set the page
	page->SetSizer(sizer_v);
	return page;
}

wxWindow* BrushToolDlg::CreateAnalysisPage(wxWindow *parent)
{
	wxPanel *page = new wxPanel(parent);
	wxStaticText *st = 0;
	//validator: floating point 1
	wxFloatingPointValidator<double> vald_fp1(1);
	//validator: integer
	wxIntegerValidator<unsigned int> vald_int;

	//component analyzer
	wxBoxSizer *sizer1 = new wxStaticBoxSizer(
		new wxStaticBox(page, wxID_ANY, "Component Analyzer"),
		wxVERTICAL);
	//threshold of ccl
	wxBoxSizer *sizer1_1 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "Threshold:",
		wxDefaultPosition, wxSize(75, 20));
	m_ca_thresh_sldr = new wxSlider(page, ID_CAThreshSldr, 0, 0, 2550,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_ca_thresh_text = new wxTextCtrl(page, ID_CAThreshText, "0.0",
		wxDefaultPosition, wxSize(50+size_fix_w, 20), 0, vald_fp1);
	sizer1_1->Add(st, 0, wxALIGN_CENTER);
	sizer1_1->Add(m_ca_thresh_sldr, 1, wxEXPAND);
	sizer1_1->Add(m_ca_thresh_text, 0, wxALIGN_CENTER);
	m_ca_analyze_btn = new wxButton(page, ID_CAAnalyzeBtn, "Analyze",
		wxDefaultPosition, wxSize(-1, 23));
	sizer1_1->Add(m_ca_analyze_btn, 0, wxALIGN_CENTER);
	//size of ccl
	wxBoxSizer *sizer1_2 = new wxBoxSizer(wxHORIZONTAL);
	m_ca_select_only_chk = new wxCheckBox(page, ID_CASelectOnlyChk, "Selct. Only",
		wxDefaultPosition, wxSize(90, 20));
	sizer1_2->Add(5, 5);
	sizer1_2->Add(m_ca_select_only_chk, 0, wxALIGN_CENTER);
	sizer1_2->AddStretchSpacer();
	st = new wxStaticText(page, 0, "Min:",
		wxDefaultPosition, wxSize(40, 15));
	sizer1_2->Add(st, 0, wxALIGN_CENTER);
	m_ca_min_text = new wxTextCtrl(page, ID_CAMinText, "0",
		wxDefaultPosition, wxSize(40+size_fix_w, 20), 0, vald_int);
	sizer1_2->Add(m_ca_min_text, 0, wxALIGN_CENTER);
	st = new wxStaticText(page, 0, "vx",
		wxDefaultPosition, wxSize(15, 15));
	sizer1_2->Add(st, 0, wxALIGN_CENTER);
	sizer1_2->AddStretchSpacer();
	st = new wxStaticText(page, 0, "Max:",
		wxDefaultPosition, wxSize(40, 15));
	sizer1_2->Add(st, 0, wxALIGN_CENTER);
	m_ca_max_text = new wxTextCtrl(page, ID_CAMaxText, "1000",
		wxDefaultPosition, wxSize(40+size_fix_w, 20), 0, vald_int);
	sizer1_2->Add(m_ca_max_text, 0, wxALIGN_CENTER);
	st = new wxStaticText(page, 0, "vx",
		wxDefaultPosition, wxSize(15, 15));
	sizer1_2->Add(st, 0, wxALIGN_CENTER);
	sizer1_2->AddStretchSpacer();
	m_ca_ignore_max_chk = new wxCheckBox(page, ID_CAIgnoreMaxChk, "Ignore Max");
	sizer1_2->Add(m_ca_ignore_max_chk, 0, wxALIGN_CENTER);
	sizer1_2->Add(5, 5);
	//text result
	wxBoxSizer *sizer1_3 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "Components:");
	sizer1_3->Add(5, 5);
	sizer1_3->Add(st, 0, wxALIGN_CENTER);
	m_ca_comps_text = new wxTextCtrl(page, ID_CACompsText, "0",
		wxDefaultPosition, wxSize(70+size_fix_w, 20), wxTE_READONLY);
	sizer1_3->Add(m_ca_comps_text, 0, wxALIGN_CENTER);
	sizer1_3->AddStretchSpacer();
	st = new wxStaticText(page, 0, "Total Volume:");
	sizer1_3->Add(st, 0, wxALIGN_CENTER);
	m_ca_volume_text = new wxTextCtrl(page, ID_CAVolumeText, "0",
		wxDefaultPosition, wxSize(70+size_fix_w, 20), wxTE_READONLY);
	sizer1_3->Add(m_ca_volume_text, 0, wxALIGN_CENTER);
	//export
	wxBoxSizer *sizer1_4 = new wxBoxSizer(wxHORIZONTAL);
	sizer1_4->AddStretchSpacer();
	st = new wxStaticText(page, 0, "Output:  ");
	sizer1_4->Add(st, 0, wxALIGN_CENTER);
	m_ca_multi_chann_btn = new wxButton(page, ID_CAMultiChannBtn, "Individual Channels",
		wxDefaultPosition, wxSize(-1, 23));
	m_ca_random_color_btn = new wxButton(page, ID_CARandomColorBtn, "Random RGB",
		wxDefaultPosition, wxSize(-1, 23));
	m_ca_annotations_btn = new wxButton(page, ID_CAAnnotationsBtn, "Annotations",
		wxDefaultPosition, wxSize(-1, 23));
	sizer1_4->Add(m_ca_multi_chann_btn, 0, wxALIGN_CENTER);
	sizer1_4->Add(m_ca_random_color_btn, 0, wxALIGN_CENTER);
	sizer1_4->Add(m_ca_annotations_btn, 0, wxALIGN_CENTER);
	//sizer1
	sizer1->Add(10, 10);
	sizer1->Add(sizer1_1, 0, wxEXPAND);
	sizer1->Add(10, 10);
	sizer1->Add(sizer1_2, 0, wxEXPAND);
	sizer1->Add(10, 10);
	sizer1->Add(sizer1_3, 0, wxEXPAND);
	sizer1->Add(10, 10);
	sizer1->Add(sizer1_4, 0, wxEXPAND);
	sizer1->Add(10, 10);
	//noise removal
	wxBoxSizer *sizer2 = new wxStaticBoxSizer(
		new wxStaticBox(page, wxID_ANY, "Noise Removal"),
		wxVERTICAL);
	//size threshold
	wxBoxSizer *sizer2_1 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(page, 0, "Size Thresh.:",
		wxDefaultPosition, wxSize(75, -1));
	m_nr_size_sldr = new wxSlider(page, ID_NRSizeSldr, 10, 1, 100,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_nr_size_text = new wxTextCtrl(page, ID_NRSizeText, "10",
		wxDefaultPosition, wxSize(40+size_fix_w, -1), 0, vald_int);
	sizer2_1->Add(5, 5);
	sizer2_1->Add(st, 0, wxALIGN_CENTER);
	sizer2_1->Add(m_nr_size_sldr, 1, wxEXPAND);
	sizer2_1->Add(m_nr_size_text, 0, wxALIGN_CENTER);
	st = new wxStaticText(page, 0, "vx",
		wxDefaultPosition, wxSize(25, 15));
	sizer2_1->Add(st, 0, wxALIGN_CENTER);
	//buttons
	wxBoxSizer *sizer2_2 = new wxBoxSizer(wxHORIZONTAL);
	m_nr_analyze_btn = new wxButton(page, ID_NRAnalyzeBtn, "Analyze",
		wxDefaultPosition, wxSize(-1, 23));
	m_nr_remove_btn = new wxButton(page, ID_NRRemoveBtn, "Remove",
		wxDefaultPosition, wxSize(-1, 23));
	sizer2_2->AddStretchSpacer();
	sizer2_2->Add(m_nr_analyze_btn, 0, wxALIGN_CENTER);
	sizer2_2->Add(m_nr_remove_btn, 0, wxALIGN_CENTER);
	//sizer2
	sizer2->Add(10, 10);
	sizer2->Add(sizer2_1, 0, wxEXPAND);
	sizer2->Add(10, 10);
	sizer2->Add(sizer2_2, 0, wxEXPAND);
	sizer2->Add(10, 10);

    //EVE
    wxBoxSizer* sizer3 = new wxStaticBoxSizer(
        new wxStaticBox(page, wxID_ANY, "EVE (Partice Detection)"),
        wxVERTICAL);
    //min radius
    wxBoxSizer* sizer3_1 = new wxBoxSizer(wxHORIZONTAL);
    st = new wxStaticText(page, 0, "Min Radius:",
        wxDefaultPosition, wxSize(75, -1));
    m_eve_min_radius_sldr = new wxSlider(page, ID_EVEMinRadiusSldr, 5, 1, 50,
        wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
    m_eve_min_radius_text = new wxTextCtrl(page, ID_EVEMinRadiusText, "5",
        wxDefaultPosition, wxSize(40+size_fix_w, -1), 0, vald_int);
    sizer3_1->Add(5, 5);
    sizer3_1->Add(st, 0, wxALIGN_CENTER);
    sizer3_1->Add(m_eve_min_radius_sldr, 1, wxEXPAND);
    sizer3_1->Add(m_eve_min_radius_text, 0, wxALIGN_CENTER);
    st = new wxStaticText(page, 0, "vx",
        wxDefaultPosition, wxSize(25, 15));
    sizer3_1->Add(st, 0, wxALIGN_CENTER);
    //max radius
    wxBoxSizer* sizer3_2 = new wxBoxSizer(wxHORIZONTAL);
    st = new wxStaticText(page, 0, "Max Radius:",
        wxDefaultPosition, wxSize(75, -1));
    m_eve_max_radius_sldr = new wxSlider(page, ID_EVEMaxRadiusSldr, 5, 1, 50,
        wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
    m_eve_max_radius_text = new wxTextCtrl(page, ID_EVEMaxRadiusText, "5",
        wxDefaultPosition, wxSize(40+size_fix_w, -1), 0, vald_int);
    sizer3_2->Add(5, 5);
    sizer3_2->Add(st, 0, wxALIGN_CENTER);
    sizer3_2->Add(m_eve_max_radius_sldr, 1, wxEXPAND);
    sizer3_2->Add(m_eve_max_radius_text, 0, wxALIGN_CENTER);
    st = new wxStaticText(page, 0, "vx",
        wxDefaultPosition, wxSize(25, 15));
    sizer3_2->Add(st, 0, wxALIGN_CENTER);
    //threshold
    wxBoxSizer* sizer3_3 = new wxBoxSizer(wxHORIZONTAL);
    st = new wxStaticText(page, 0, "Min Brightness:",
        wxDefaultPosition, wxSize(100, -1));
    m_eve_threshold_sldr = new wxSlider(page, ID_EVEThresholdSldr, 0, 0, 65535,
        wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
    m_eve_threshold_text = new wxTextCtrl(page, ID_EVEThresholdText, "0",
        wxDefaultPosition, wxSize(60+size_fix_w, -1), 0, vald_int);
    sizer3_3->Add(5, 5);
    sizer3_3->Add(st, 0, wxALIGN_CENTER);
    sizer3_3->Add(m_eve_threshold_sldr, 1, wxEXPAND);
    sizer3_3->Add(m_eve_threshold_text, 0, wxALIGN_CENTER);
    //buttons
    wxBoxSizer* sizer3_4 = new wxBoxSizer(wxHORIZONTAL);
    m_eve_analyze_btn = new wxButton(page, ID_EVEAnalyzeBtn, "Analyze",
        wxDefaultPosition, wxSize(-1, 23));
    sizer3_4->AddStretchSpacer();
    sizer3_4->Add(m_eve_analyze_btn, 0, wxALIGN_CENTER);
    //sizer2
    sizer3->Add(10, 10);
    sizer3->Add(sizer3_1, 0, wxEXPAND);
    sizer3->Add(10, 10);
    sizer3->Add(sizer3_2, 0, wxEXPAND);
    sizer3->Add(10, 10);
    sizer3->Add(sizer3_3, 0, wxEXPAND);
    sizer3->Add(10, 10);
    sizer3->Add(sizer3_4, 0, wxEXPAND);
    sizer3->Add(10, 10);

	//vertical sizer
	wxBoxSizer* sizer_v = new wxBoxSizer(wxVERTICAL);
	sizer_v->Add(10, 10);
	sizer_v->Add(sizer1, 0, wxEXPAND);
	sizer_v->Add(10, 30);
	sizer_v->Add(sizer2, 0, wxEXPAND);
	sizer_v->Add(10, 10);
    sizer_v->Add(sizer3, 0, wxEXPAND);
    sizer_v->Add(10, 10);

	//set the page
	page->SetSizer(sizer_v);
	return page;
}

BrushToolDlg::BrushToolDlg(wxWindow *frame, wxWindow *parent)
: wxPanel(parent, wxID_ANY,
      wxPoint(500, 150),
      wxSize(400, 550),
      0, "BrushToolDlg"),
   m_frame(parent),
   m_cur_view(0),
   m_vol1(0),
   m_vol2(0),
   m_max_value(255.0),
   m_dft_ini_thresh(0.0),
   m_dft_gm_falloff(0.0),
   m_dft_scl_falloff(0.0),
   m_dft_scl_translate(0.0),
   m_dft_ca_min(0.0),
   m_dft_ca_max(0.0),
   m_dft_ca_thresh(0.0),
	m_dft_ca_falloff(1.0),
   m_dft_nr_thresh(0.0),
   m_dft_nr_size(0.0),
   m_dft_dslt_c(0.0),
   m_dft_eve_thresh(0.1),
    mask_undo_num_modified(false)
{
	SetEvtHandlerEnabled(false);
	Freeze();

   //notebook
	m_notebook = new wxNotebook(this, wxID_ANY);
	m_notebook->AddPage(CreateBrushPage(m_notebook), "Paint Brush");
	m_notebook->AddPage(CreateCalculationPage(m_notebook), "Calculations");
	m_notebook->AddPage(CreateAnalysisPage(m_notebook), "Analysis");

	//all controls
	wxBoxSizer *sizerV = new wxBoxSizer(wxVERTICAL);
	sizerV->Add(10, 10);
	sizerV->Add(m_notebook, 0, wxEXPAND);
	sizerV->Add(10, 10);

	SetSizerAndFit(sizerV);
	Layout();

	LoadDefault();

	Thaw();
	SetEvtHandlerEnabled(true);
	//m_watch.Start();
    
    m_idleTimer = new wxTimer(this, ID_Timer);
    m_idleTimer->Start(100);
}

BrushToolDlg::~BrushToolDlg()
{
    m_idleTimer->Stop();
    wxDELETE(m_idleTimer);
    SaveDefault();
}

void BrushToolDlg::GetSettings(VRenderView* vrv)
{
   if (!vrv)
      return;

   VolumeData* sel_vol = 0;
   VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
   if (vr_frame)
   {
      sel_vol = vr_frame->GetCurSelVol();
      vr_frame->GetNoiseCancellingDlg()->GetSettings(vrv);
      vr_frame->GetCountingDlg()->GetSettings(vrv);
      vr_frame->GetColocalizationDlg()->GetSettings(vrv);
   }

   m_cur_view = vrv;
   double dval = 0.0;
   int ival = 0;
   bool bval = false;

   SetEvtHandlerEnabled(false);

   //selection strength
   dval = vrv->GetBrushSclTranslate();
	m_dft_scl_translate = dval;
   m_brush_scl_translate_sldr->SetValue(int(dval*1000.0+0.5));
   m_brush_scl_translate_text->ChangeValue(wxString::Format("%.2f", dval));
   //2d influence
   dval = vrv->GetW2d();
   m_brush_2dinfl_sldr->SetValue(int(dval*100.0+0.5));
   m_brush_2dinfl_text->ChangeValue(wxString::Format("%.2f", dval));
   //edge detect
   bval = vrv->GetEdgeDetect();
   m_edge_detect_chk->SetValue(bval);
   //hidden removal
   bval = vrv->GetHiddenRemoval();
   m_hidden_removal_chk->SetValue(bval);
   //select group
   bval = vrv->GetSelectGroup();
   m_select_group_chk->SetValue(bval);

   //size1
   dval = vrv->GetBrushSize1();
   m_brush_size1_sldr->SetValue(int(dval));
   m_brush_size1_text->ChangeValue(wxString::Format("%.0f", dval));
   //size2
   m_brush_size2_chk->SetValue(vrv->GetBrushSize2Link());
   if (vrv->GetBrushSize2Link())
   {
      m_brush_size2_sldr->Enable();
      m_brush_size2_text->Enable();
   }
   else
   {
      m_brush_size2_sldr->Disable();
      m_brush_size2_text->Disable();
   }
   dval = vrv->GetBrushSize2();
   m_brush_size2_sldr->SetValue(int(dval));
   m_brush_size2_text->ChangeValue(wxString::Format("%.0f", dval));

   //iteration number
   ival = vrv->GetBrushIteration();
   if (ival<=BRUSH_TOOL_ITER_WEAK)
   {
      m_brush_iterw_rb->SetValue(true);
      m_brush_iters_rb->SetValue(false);
      m_brush_iterss_rb->SetValue(false);
   }
   else if (ival<=BRUSH_TOOL_ITER_NORMAL)
   {
      m_brush_iterw_rb->SetValue(false);
      m_brush_iters_rb->SetValue(true);
      m_brush_iterss_rb->SetValue(false);
   }
   else
   {
      m_brush_iterw_rb->SetValue(false);
      m_brush_iters_rb->SetValue(false);
      m_brush_iterss_rb->SetValue(true);
   }

   bval = vrv->GetUseDSLTBrush();
   m_dslt_chk->SetValue(bval);
   ival = vrv->GetBrushDSLT_R();
   m_dslt_r_sldr->SetValue(ival);
   m_dslt_r_text->ChangeValue(wxString::Format("%d", ival));
   ival = vrv->GetBrushDSLT_Q();
   m_dslt_q_sldr->SetValue(ival);
   m_dslt_q_text->ChangeValue(wxString::Format("%d", ival));
   dval = vrv->GetBrushDSLT_C();
   m_dft_dslt_c = dval;

   //threshold range
   if (sel_vol)
   {
      m_max_value = sel_vol->GetMaxValue();
      //falloff
      m_brush_scl_translate_sldr->SetRange(0, int(m_max_value*10.0));
      m_brush_scl_translate_text->SetValue(wxString::Format("%.1f", m_dft_scl_translate*m_max_value));
      m_ca_thresh_sldr->SetRange(0, int(m_max_value*10.0));
      m_ca_thresh_sldr->SetValue(int(m_dft_ca_thresh*m_max_value*10.0+0.5));
      m_ca_thresh_text->ChangeValue(wxString::Format("%.1f", m_dft_ca_thresh*m_max_value));
	  
	  m_dslt_c_sldr->SetRange(0, int(m_max_value*10.0));
      m_dslt_c_sldr->SetValue(int(m_dft_dslt_c*m_max_value*10.0+0.5));
	  m_dslt_c_text->ChangeValue(wxString::Format("%.1f", m_dft_dslt_c*m_max_value));

      m_eve_threshold_sldr->SetRange(0, int(m_max_value));
      m_eve_threshold_sldr->SetValue(int(m_dft_eve_thresh * m_max_value + 0.5));
      m_eve_threshold_text->ChangeValue(wxString::Format("%d", int(m_dft_eve_thresh * m_max_value + 0.5)));

      int alpha = (int)(sel_vol->GetMaskAlpha() * 255.0 + 0.5);
      m_mask_overlay_alpha_sldr->SetValue(alpha);
      m_mask_overlay_alpha_text->ChangeValue(wxString::Format("%d", alpha));
   }

   SetEvtHandlerEnabled(true);

	UpdateUndoRedo();
}

void BrushToolDlg::SelectBrush(int id)
{
   m_toolbar->ToggleTool(ID_BrushAppend, false);
   m_toolbar->ToggleTool(ID_BrushDiffuse, false);
   m_toolbar->ToggleTool(ID_BrushDesel, false);

   switch (id)
   {
   case ID_BrushAppend:
      m_toolbar->ToggleTool(ID_BrushAppend, true);
      break;
   case ID_BrushDiffuse:
      m_toolbar->ToggleTool(ID_BrushDiffuse, true);
      break;
   case ID_BrushDesel:
      m_toolbar->ToggleTool(ID_BrushDesel, true);
      break;
   }
}

void BrushToolDlg::UpdateUndoRedo()
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		VolumeData* vd = vr_frame->GetCurSelVol();
		if (vd && vd->GetTexture())
		{
			m_toolbar->EnableTool(ID_BrushUndo,
				vd->GetTexture()->get_undo());
			m_toolbar->EnableTool(ID_BrushRedo,
				vd->GetTexture()->get_redo());
		}
	}
}

//brush commands
void BrushToolDlg::OnBrushAppend(wxCommandEvent &event)
{
   m_toolbar->ToggleTool(ID_BrushDiffuse, false);
   m_toolbar->ToggleTool(ID_BrushDesel, false);

   VRenderFrame* frame = (VRenderFrame*)m_frame;
   if (frame && frame->GetTree())
   {
      if (m_toolbar->GetToolState(ID_BrushAppend))
         frame->GetTree()->SelectBrush(TreePanel::ID_BrushAppend);
      else
         frame->GetTree()->SelectBrush(0);
      frame->GetTree()->BrushAppend();
   }
}

void BrushToolDlg::OnBrushDiffuse(wxCommandEvent &event)
{
   m_toolbar->ToggleTool(ID_BrushAppend, false);
   m_toolbar->ToggleTool(ID_BrushDesel, false);

   VRenderFrame* frame = (VRenderFrame*)m_frame;
   if (frame && frame->GetTree())
   {
      if (m_toolbar->GetToolState(ID_BrushDiffuse))
         frame->GetTree()->SelectBrush(TreePanel::ID_BrushDiffuse);
      else
         frame->GetTree()->SelectBrush(0);
      frame->GetTree()->BrushDiffuse();
   }
}

void BrushToolDlg::OnBrushDesel(wxCommandEvent &event)
{
   m_toolbar->ToggleTool(ID_BrushAppend, false);
   m_toolbar->ToggleTool(ID_BrushDiffuse, false);

   VRenderFrame* frame = (VRenderFrame*)m_frame;
   if (frame && frame->GetTree())
   {
      if (m_toolbar->GetToolState(ID_BrushDesel))
         frame->GetTree()->SelectBrush(TreePanel::ID_BrushDesel);
      else
         frame->GetTree()->SelectBrush(0);
      frame->GetTree()->BrushDesel();
   }
}

void BrushToolDlg::OnBrushClear(wxCommandEvent &event)
{
   VRenderFrame* frame = (VRenderFrame*)m_frame;
   if (frame && frame->GetTree())
      frame->GetTree()->BrushClear();
}

void BrushToolDlg::OnBrushErase(wxCommandEvent &event)
{
   m_toolbar->ToggleTool(ID_BrushAppend, false);
   m_toolbar->ToggleTool(ID_BrushDiffuse, false);
   m_toolbar->ToggleTool(ID_BrushDesel, false);

   VRenderFrame* frame = (VRenderFrame*)m_frame;
   if (frame && frame->GetTree())
      frame->GetTree()->BrushErase();
}

void BrushToolDlg::OnBrushCreate(wxCommandEvent &event)
{
   m_toolbar->ToggleTool(ID_BrushAppend, false);
   m_toolbar->ToggleTool(ID_BrushDiffuse, false);
   m_toolbar->ToggleTool(ID_BrushDesel, false);

   VRenderFrame* frame = (VRenderFrame*)m_frame;
   if (frame && frame->GetTree())
      frame->GetTree()->BrushCreate();
}

void BrushToolDlg::BrushUndo()
{
    VolumeData* sel_vol = 0;
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (vr_frame)
        sel_vol = vr_frame->GetCurSelVol();
    if (sel_vol && sel_vol->GetTexture())
    {
        sel_vol->GetTexture()->mask_undos_backward();
        sel_vol->GetVR()->clear_tex_current_mask();
    }
    vr_frame->RefreshVRenderViews();
    UpdateUndoRedo();
}

void BrushToolDlg::OnBrushUndo(wxCommandEvent &event)
{
    BrushUndo();
}

void BrushToolDlg::BrushRedo()
{
    VolumeData* sel_vol = 0;
    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (vr_frame)
        sel_vol = vr_frame->GetCurSelVol();
    if (sel_vol && sel_vol->GetTexture())
    {
        sel_vol->GetTexture()->mask_undos_forward();
        sel_vol->GetVR()->clear_tex_current_mask();
    }
    vr_frame->RefreshVRenderViews();
    UpdateUndoRedo();
}

void BrushToolDlg::OnBrushRedo(wxCommandEvent &event)
{
    BrushRedo();
}

void BrushToolDlg::DrawBrush(double val)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	VolumeData* sel_vol = 0;
	if (m_cur_view)
		sel_vol = m_cur_view->GetCurrentVolume();
	if (vr_frame && sel_vol)
	{
		if (!m_select_group_chk->GetValue())
		{
			double thval = val / sel_vol->GetMaxValue();
			if (thval < sel_vol->GetLeftThresh())
				thval = sel_vol->GetLeftThresh();
			if (sel_vol->GetMask(false))
            {
				sel_vol->DrawMaskThreshold((float)thval, m_cur_view->GetPersp(), m_brush_use_absolute_chk->IsChecked());
                sel_vol->GetMask(true);
            }
		}
		else
		{
			DataGroup *group = m_cur_view->GetCurrentVolGroup();
			if (group)
			{
				for (int i = 0; i < group->GetVolumeNum(); i++)
				{
					VolumeData *vd = group->GetVolumeData(i);
					if (vd && vd->GetDisp())
					{
						double thval = val / vd->GetMaxValue();
						if (thval < vd->GetLeftThresh())
							thval = vd->GetLeftThresh();
						if (vd->GetMask(false))
                        {
							vd->DrawMaskThreshold((float)thval, m_cur_view->GetPersp(), m_brush_use_absolute_chk->IsChecked());
                            sel_vol->GetMask(true);
                        }
					}
				}
			}
		}
		vr_frame->RefreshVRenderViews();
	}
}

//selection adjustment
//scalar translate
void BrushToolDlg::OnBrushSclTranslateChange(wxScrollEvent &event)
{
   int ival = event.GetPosition();
   double val = double(ival)/10.0;
   wxString str = wxString::Format("%.1f", val);
   m_brush_scl_translate_text->SetValue(str);

   DrawBrush(val);
}

void BrushToolDlg::OnBrushSclTranslateTextEnter(wxCommandEvent &event)
{
   wxString str = m_brush_scl_translate_text->GetValue();
   double val;
   str.ToDouble(&val);
   m_dft_scl_translate = val/m_max_value;
   m_brush_scl_translate_sldr->SetValue(int(val*10.0+0.5));

   DrawBrush(val);
   
   //set translate
   if (m_cur_view)
      m_cur_view->SetBrushSclTranslate(m_dft_scl_translate);
}

void BrushToolDlg::OnBrushSclTranslateText(wxCommandEvent &event)
{
   wxString str = m_brush_scl_translate_text->GetValue();
   double val;
   str.ToDouble(&val);
   m_dft_scl_translate = val/m_max_value;
   m_brush_scl_translate_sldr->SetValue(int(val*10.0+0.5));

	//set translate
   if (m_cur_view)
      m_cur_view->SetBrushSclTranslate(m_dft_scl_translate);
}

//2d influence
void BrushToolDlg::OnBrush2dinflChange(wxScrollEvent &event)
{
   int ival = event.GetPosition();
   double val = double(ival)/100.0;
   wxString str = wxString::Format("%.2f", val);
   m_brush_2dinfl_text->SetValue(str);
}

void BrushToolDlg::OnBrush2dinflText(wxCommandEvent &event)
{
   wxString str = m_brush_2dinfl_text->GetValue();
   double val;
   str.ToDouble(&val);
   m_brush_2dinfl_sldr->SetValue(int(val*100.0));

   //set 2d weight
   if (m_cur_view)
      m_cur_view->SetW2d(val);
}

//edge detect
void BrushToolDlg::OnBrushEdgeDetectChk(wxCommandEvent &event)
{
   bool edge_detect = m_edge_detect_chk->GetValue();

   //set edge detect
   if (m_cur_view)
      m_cur_view->SetEdgeDetect(edge_detect);
}

//hidden removal
void BrushToolDlg::OnBrushHiddenRemovalChk(wxCommandEvent &event)
{
   bool hidden_removal = m_hidden_removal_chk->GetValue();

   //set hidden removal
   if (m_cur_view)
      m_cur_view->SetHiddenRemoval(hidden_removal);
}

//select group
void BrushToolDlg::OnBrushSelectGroupChk(wxCommandEvent &event)
{
   bool select_group = m_select_group_chk->GetValue();

   //set select group
   if (m_cur_view)
      m_cur_view->SetSelectGroup(select_group);
}

//use absolute value
void BrushToolDlg::OnBrushUseAbsoluteValueChk(wxCommandEvent &event)
{
   bool use_absolute = m_brush_use_absolute_chk->GetValue();

   if (m_cur_view)
      m_cur_view->SetUseAbsoluteValue(use_absolute);
}

//brush size 1
void BrushToolDlg::OnBrushSize1Change(wxScrollEvent &event)
{
   int ival = event.GetPosition();
   wxString str = wxString::Format("%d", ival);
   m_brush_size1_text->SetValue(str);
}

void BrushToolDlg::OnBrushSize1Text(wxCommandEvent &event)
{
   wxString str = m_brush_size1_text->GetValue();
   double val;
   str.ToDouble(&val);
   m_brush_size1_sldr->SetValue(int(val));

   //set size1
   if (m_cur_view)
   {
      m_cur_view->SetBrushSize(val, -1.0);
      if (m_cur_view->GetIntMode()==2)
         m_cur_view->RefreshGL();
   }
}

//brush size 2
void BrushToolDlg::OnBrushSize2Chk(wxCommandEvent &event)
{
   wxString str = m_brush_size1_text->GetValue();
   double val1;
   str.ToDouble(&val1);
   str = m_brush_size2_text->GetValue();
   double val2;
   str.ToDouble(&val2);

   if (m_brush_size2_chk->GetValue())
   {
      m_brush_size2_sldr->Enable();
      m_brush_size2_text->Enable();
      if (m_cur_view)
      {
         m_cur_view->SetUseBrushSize2(true);
         m_cur_view->SetBrushSize(val1, val2);
         m_cur_view->RefreshGL();
      }
   }
   else
   {
      m_brush_size2_sldr->Disable();
      m_brush_size2_text->Disable();
      if (m_cur_view)
      {
         m_cur_view->SetUseBrushSize2(false);
         m_cur_view->SetBrushSize(val1, val2);
         m_cur_view->RefreshGL();
      }
   }
}

void BrushToolDlg::OnBrushSize2Change(wxScrollEvent &event)
{
   int ival = event.GetPosition();
   wxString str = wxString::Format("%d", ival);
   m_brush_size2_text->SetValue(str);
}

void BrushToolDlg::OnBrushSize2Text(wxCommandEvent &event)
{
   wxString str = m_brush_size2_text->GetValue();
   double val;
   str.ToDouble(&val);
   m_brush_size2_sldr->SetValue(int(val));

   //set size2
   if (m_cur_view)
   {
      m_cur_view->SetBrushSize(-1.0, val);
      if (m_cur_view->GetIntMode()==2)
         m_cur_view->RefreshGL();
   }
}

//brush iterations
void BrushToolDlg::OnBrushIterCheck(wxCommandEvent& event)
{
   if (m_brush_iterw_rb->GetValue())
   {
      if (m_cur_view)
         m_cur_view->SetBrushIteration(BRUSH_TOOL_ITER_WEAK);
   }
   else if (m_brush_iters_rb->GetValue())
   {
      if (m_cur_view)
         m_cur_view->SetBrushIteration(BRUSH_TOOL_ITER_NORMAL);
   }
   else if (m_brush_iterss_rb->GetValue())
   {
      if (m_cur_view)
         m_cur_view->SetBrushIteration(BRUSH_TOOL_ITER_STRONG);
   }
}

void BrushToolDlg::OnDSLTBrushChk(wxCommandEvent &event)
{
	bool dslt = m_dslt_chk->GetValue();

   //set hidden removal
   if (m_cur_view)
	   m_cur_view->SetUseDSLTBrush(dslt);

   if (dslt)
   {
	   st_dslt_r->Enable();
	   m_dslt_r_sldr->Enable();
	   m_dslt_r_text->Enable();
	   st_dslt_q->Enable();
	   m_dslt_q_sldr->Enable();
	   m_dslt_q_text->Enable();
	   st_dslt_c->Enable();
	   m_dslt_c_sldr->Enable();
	   m_dslt_c_text->Enable();
   }
   else
   {
	   st_dslt_r->Disable();
	   m_dslt_r_sldr->Disable();
	   m_dslt_r_text->Disable();
	   st_dslt_q->Disable();
	   m_dslt_q_sldr->Disable();
	   m_dslt_q_text->Disable();
	   st_dslt_c->Disable();
	   m_dslt_c_sldr->Disable();
	   m_dslt_c_text->Disable();
   }
}

void BrushToolDlg::OnDSLTBrushRadChange(wxScrollEvent &event)
{
   int ival = event.GetPosition();
   wxString str = wxString::Format("%d", ival);
   m_dslt_r_text->SetValue(str);
}

void BrushToolDlg::OnDSLTBrushRadText(wxCommandEvent &event)
{
   wxString str = m_dslt_r_text->GetValue();
   double val;
   str.ToDouble(&val);
   m_dslt_r_sldr->SetValue(int(val));

   if (m_cur_view)
	   m_cur_view->SetBrushDSLT_R(val);
}

void BrushToolDlg::OnDSLTBrushQualityChange(wxScrollEvent &event)
{
   int ival = event.GetPosition();
   wxString str = wxString::Format("%d", ival);
   m_dslt_q_text->SetValue(str);
}
void BrushToolDlg::OnDSLTBrushQualityText(wxCommandEvent &event)
{
   wxString str = m_dslt_q_text->GetValue();
   double val;
   str.ToDouble(&val);
   m_dslt_q_sldr->SetValue(int(val));

   if (m_cur_view)
	   m_cur_view->SetBrushDSLT_Q(val);
}

void BrushToolDlg::OnDSLTBrushCChange(wxScrollEvent &event)
{
   int ival = event.GetPosition();
   double val = double(ival)/10.0;
   wxString str = wxString::Format("%.1f", val);
   m_dslt_c_text->SetValue(str);
}

void BrushToolDlg::OnDSLTBrushCText(wxCommandEvent &event)
{
   wxString str = m_dslt_c_text->GetValue();
   double val;
   str.ToDouble(&val);
   m_dft_dslt_c = val/m_max_value;
   m_dslt_c_sldr->SetValue(int(val*10.0+0.5));

	//set translate
   if (m_cur_view)
	   m_cur_view->SetBrushDSLT_C(m_dft_dslt_c);
}

//component analyze
void BrushToolDlg::OnCAThreshChange(wxScrollEvent &event)
{
   int ival = event.GetPosition();
   wxString str = wxString::Format("%.1f", double(ival)/10.0);
   m_ca_thresh_text->SetValue(str);
}

void BrushToolDlg::OnCAThreshText(wxCommandEvent &event)
{
    wxString str = m_ca_thresh_text->GetValue();
    double val;
    str.ToDouble(&val);
    m_dft_ca_thresh = val/m_max_value;
    m_ca_thresh_sldr->SetValue(int(val*10.0+0.5));
    
    if (m_cur_view && m_cur_view->GetVolumeA())
    {
        VolumeData *vd = m_cur_view->GetVolumeA();
        vd->SetMaskThreshold(m_dft_ca_thresh);
        m_cur_view->RefreshGL();
    }
    /*
     //change mask threshold
     VolumeData* sel_vol = 0;
     VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
     if (vr_frame)
     sel_vol = vr_frame->GetCurSelVol();
     //if (sel_vol)
     //   sel_vol->SetMaskThreshold(m_dft_ca_thresh);
     vr_frame->RefreshVRenderViews();
     */
}

void BrushToolDlg::OnCAAnalyzeBtn(wxCommandEvent &event)
{
   if (m_cur_view)
   {
      bool select = m_ca_select_only_chk->GetValue();
      double min_voxels, max_voxels;
      wxString str = m_ca_min_text->GetValue();
      str.ToDouble(&min_voxels);
      str = m_ca_max_text->GetValue();
      str.ToDouble(&max_voxels);
      bool ignore_max = m_ca_ignore_max_chk->GetValue();

      int comps = m_cur_view->CompAnalysis(min_voxels, ignore_max?-1.0:max_voxels, m_dft_ca_thresh, select, true);
      int volume = m_cur_view->GetVolumeSelector()->GetVolumeNum();
      //change mask threshold
      VolumeData* sel_vol = 0;
      VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
      if (vr_frame)
         sel_vol = vr_frame->GetCurSelVol();
      /*if (sel_vol)
      {
         sel_vol->SetUseMaskThreshold(true);
         sel_vol->SetMaskThreshold(m_dft_ca_thresh);
      }*/
      m_ca_comps_text->SetValue(wxString::Format("%d", comps));
      m_ca_volume_text->SetValue(wxString::Format("%d", volume));
      if (vr_frame)
         vr_frame->RefreshVRenderViews();
   }
}

void BrushToolDlg::OnCAIgnoreMaxChk(wxCommandEvent &event)
{
   if (m_ca_ignore_max_chk->GetValue())
      m_ca_max_text->Disable();
   else
      m_ca_max_text->Enable();
}

void BrushToolDlg::OnCAMultiChannBtn(wxCommandEvent &event)
{
   if (m_cur_view)
   {
      bool select = m_ca_select_only_chk->GetValue();
      m_cur_view->CompExport(0, select);
   }
}

void BrushToolDlg::OnCARandomColorBtn(wxCommandEvent &event)
{
   if (m_cur_view)
   {
      bool select = m_ca_select_only_chk->GetValue();
      m_cur_view->CompExport(1, select);
   }
}

void BrushToolDlg::OnCAAnnotationsBtn(wxCommandEvent &event)
{
   if (m_cur_view)
      m_cur_view->ShowAnnotations();
}

//noise removal
void BrushToolDlg::OnNRSizeChange(wxScrollEvent &event)
{
   int ival = event.GetPosition();
   wxString str = wxString::Format("%d", ival);
   m_nr_size_text->SetValue(str);
}

void BrushToolDlg::OnNRSizeText(wxCommandEvent &event)
{
   wxString str = m_nr_size_text->GetValue();
   double val;
   str.ToDouble(&val);
   m_nr_size_sldr->SetValue(int(val));
}

void BrushToolDlg::OnNRAnalyzeBtn(wxCommandEvent &event)
{
   if (m_cur_view)
   {
      double min_voxels, max_voxels;
      min_voxels = 0.0;
      wxString str = m_nr_size_text->GetValue();
      str.ToDouble(&max_voxels);

      int comps = m_cur_view->NoiseAnalysis(0.0, max_voxels, m_dft_ca_thresh);
	  int volume = m_cur_view->GetVolumeSelector()->GetVolumeNum();
      //change mask threshold
      VolumeData* sel_vol = 0;
      VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
      if (vr_frame)
         sel_vol = vr_frame->GetCurSelVol();
      //if (sel_vol)
      //{
      //   sel_vol->SetUseMaskThreshold(true);
      //   sel_vol->SetMaskThreshold(m_dft_ca_thresh);
      //}
      m_ca_comps_text->SetValue(wxString::Format("%d", comps));
      m_ca_volume_text->SetValue(wxString::Format("%d", volume));
      if (vr_frame)
         vr_frame->RefreshVRenderViews();
   }
}

void BrushToolDlg::OnNRRemoveBtn(wxCommandEvent &event)
{
   if (m_cur_view)
   {
      double max_voxels;
      wxString str = m_nr_size_text->GetValue();
      str.ToDouble(&max_voxels);

      m_cur_view->NoiseRemoval(int(max_voxels), m_dft_ca_thresh);
      m_cur_view->RefreshGL();
   }
}

//eve
void BrushToolDlg::OnEVEMinRadiusChange(wxScrollEvent& event)
{
    int ival = event.GetPosition();
    wxString str = wxString::Format("%d", ival);
    if (m_eve_min_radius_text) m_eve_min_radius_text->SetValue(str);
}

void BrushToolDlg::OnEVEMinRadiusText(wxCommandEvent& event)
{
    double minval = 0.0;
    double maxval = 0.0;
    if (m_eve_min_radius_text && m_eve_min_radius_sldr)
    {
        wxString str = m_eve_min_radius_text->GetValue();
        str.ToDouble(&minval);
        m_dft_eve_min_radius = minval;
        m_eve_min_radius_sldr->SetValue(int(minval));
    }
    
    if (m_eve_max_radius_text && m_eve_max_radius_sldr)
    {
        wxString str = m_eve_max_radius_text->GetValue();
        str.ToDouble(&maxval);
        if (maxval < minval)
        {
            m_eve_max_radius_sldr->SetValue(int(minval));
            m_eve_max_radius_text->ChangeValue(wxString::Format("%d", int(minval)));
        }
    }
}

void BrushToolDlg::OnEVEMaxRadiusChange(wxScrollEvent& event)
{
    int ival = event.GetPosition();
    wxString str = wxString::Format("%d", ival);
    if (m_eve_max_radius_text) m_eve_max_radius_text->SetValue(str);
}

void BrushToolDlg::OnEVEMaxRadiusText(wxCommandEvent& event)
{
    double minval = 0.0;
    double maxval = 0.0;
    if (m_eve_max_radius_text && m_eve_max_radius_sldr)
    {
        wxString str = m_eve_max_radius_text->GetValue();
        str.ToDouble(&maxval);
        m_dft_eve_max_radius = maxval;
        m_eve_max_radius_sldr->SetValue(int(maxval));
    }
    
    if (m_eve_min_radius_text && m_eve_min_radius_sldr)
    {
        wxString str = m_eve_min_radius_text->GetValue();
        str.ToDouble(&minval);
        if (minval > maxval)
        {
            m_eve_min_radius_sldr->SetValue(int(maxval));
            m_eve_min_radius_text->ChangeValue(wxString::Format("%d", int(maxval)));
        }
    }
}

void BrushToolDlg::OnEVEThresholdChange(wxScrollEvent& event)
{
    int ival = (int)(event.GetPosition() + 0.5);
    wxString str = wxString::Format("%d", ival);
    m_eve_threshold_text->SetValue(str);
}

void BrushToolDlg::OnEVEThresholdText(wxCommandEvent& event)
{
    wxString str = m_eve_threshold_text->GetValue();
    double val;
    str.ToDouble(&val);
    m_dft_eve_thresh = val / m_max_value;
    m_eve_threshold_sldr->SetValue(int(val + 0.5));
    
    if (m_cur_view && m_cur_view->GetVolumeA())
    {
        VolumeData *vd = m_cur_view->GetVolumeA();
        vd->SetMaskThreshold(m_dft_eve_thresh);
        m_cur_view->RefreshGL();
    }
}

void BrushToolDlg::OnEVEAnalyzeBtn(wxCommandEvent& event)
{
    if (m_cur_view)
    {
        double min_radius;
        min_radius = 1;
        wxString str = m_eve_min_radius_text->GetValue();
        str.ToDouble(&min_radius);

        double max_radius;
        max_radius = 1;
        str = m_eve_max_radius_text->GetValue();
        str.ToDouble(&max_radius);

        double threshold;
        threshold = 0.0;
        str = m_eve_threshold_text->GetValue();
        str.ToDouble(&threshold);

        string annotation_name = m_cur_view->EVEAnalysis(min_radius, max_radius, threshold);

        VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
        if (vr_frame)
            vr_frame->RefreshVRenderViews();
    }
}

void BrushToolDlg::OnMaskAlphaChange(wxScrollEvent& event)
{
    int ival = (int)(event.GetPosition() + 0.5);
    wxString str = wxString::Format("%d", ival);
    m_mask_overlay_alpha_text->SetValue(str);
}

void BrushToolDlg::OnMaskAlphaText(wxCommandEvent& event)
{
    wxString str = m_mask_overlay_alpha_text->GetValue();
    double val;
    str.ToDouble(&val);
    m_dft_mask_alpha = val;
    m_mask_overlay_alpha_sldr->SetValue(int(val + 0.5));

    if (m_cur_view && m_cur_view->GetVolumeA())
    {
        VolumeData* vd = m_cur_view->GetVolumeA();
        vd->SetMaskAlpha(m_dft_mask_alpha / 255.0);
        m_cur_view->RefreshGL();
    }
}

//help button
void BrushToolDlg::OnHelpBtn(wxCommandEvent &event)
{
   ::wxLaunchDefaultBrowser(HELP_PAINT);
}

//save default
void BrushToolDlg::SaveDefault()
{
   wxFileConfig fconfig("FluoRender default brush settings");
   double val;
   wxString str;
   //brush properties
   fconfig.Write("brush_ini_thresh", m_dft_ini_thresh);
   fconfig.Write("brush_gm_falloff", m_dft_gm_falloff);
   fconfig.Write("brush_scl_falloff", m_dft_scl_falloff);
   fconfig.Write("brush_scl_translate", m_dft_scl_translate);
   //edge detect
   fconfig.Write("edge_detect", m_edge_detect_chk->GetValue());
   //hidden removal
   fconfig.Write("hidden_removal", m_hidden_removal_chk->GetValue());
   //select group
   fconfig.Write("select_group", m_select_group_chk->GetValue());
   //2d influence
   str = m_brush_2dinfl_text->GetValue();
   str.ToDouble(&val);
   fconfig.Write("brush_2dinfl", val);
   //size 1
   str = m_brush_size1_text->GetValue();
   str.ToDouble(&val);
   fconfig.Write("brush_size1", val);
   //size2 link
   fconfig.Write("use_brush_size2", m_brush_size2_chk->GetValue());
   //size 2
   str = m_brush_size2_text->GetValue();
   str.ToDouble(&val);
   fconfig.Write("brush_size2", val);
   //iterations
   int ival = m_brush_iterw_rb->GetValue()?1:
      m_brush_iters_rb->GetValue()?2:
      m_brush_iterss_rb->GetValue()?3:0;
   fconfig.Write("brush_iters", ival);

   fconfig.Write("mask_alpha", m_dft_mask_alpha);

   fconfig.Write("use_dslt", m_dslt_chk->GetValue());
   str = m_dslt_r_text->GetValue();
   str.ToDouble(&val);
   fconfig.Write("dslt_r", val);
   str = m_dslt_q_text->GetValue();
   str.ToDouble(&val);
   fconfig.Write("dslt_q", val);
   fconfig.Write("dslt_c", m_dft_dslt_c);

   //component analyzer
   //selected only
   fconfig.Write("ca_select_only", m_ca_select_only_chk->GetValue());
   //min voxel
   str = m_ca_min_text->GetValue();
   str.ToDouble(&val);
   fconfig.Write("ca_min", val);
   //max voxel
   str = m_ca_max_text->GetValue();
   str.ToDouble(&val);
   fconfig.Write("ca_max", val);
   //ignore max
   fconfig.Write("ca_ignore_max", m_ca_ignore_max_chk->GetValue());
   //thresh
   fconfig.Write("ca_thresh", m_dft_ca_thresh);
   //noise removal
   //nr thresh
   fconfig.Write("nr_thresh", m_dft_nr_thresh);
   //nr_size
   fconfig.Write("nr_size", m_dft_nr_size);
   //EVE
   //eve thresh
   fconfig.Write("eve_thresh", m_dft_eve_thresh);
   //eve radius
   fconfig.Write("eve_min_radius", m_dft_eve_min_radius);
   fconfig.Write("eve_max_radius", m_dft_eve_max_radius);
	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/default_brush_settings.dft";
#else
	wxString dft = expath + GETSLASHS() + "default_brush_settings.dft";
	wxString dft2 = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "default_brush_settings.dft";
	if (!wxFileExists(dft) && wxFileExists(dft2))
		dft = dft2;
#endif
   wxFileOutputStream os(dft);
   fconfig.Save(os);
}

//load default
void BrushToolDlg::LoadDefault()
{
	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/default_brush_settings.dft";
#else
	wxString dft = expath + GETSLASHS() + "default_brush_settings.dft";
	if (!wxFileExists(dft))
		dft = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "default_brush_settings.dft";
#endif
   wxFileInputStream is(dft);
   if (!is.IsOk())
      return;
   wxFileConfig fconfig(is);

   wxString str;
   double val;
   int ival;
   bool bval;

   //brush properties
   fconfig.Read("brush_ini_thresh", &m_dft_ini_thresh);
   fconfig.Read("brush_gm_falloff", &m_dft_gm_falloff);
   fconfig.Read("brush_scl_falloff", &m_dft_scl_falloff);
   if (fconfig.Read("brush_scl_translate", &m_dft_scl_translate))
   {
      str = wxString::Format("%.1f", m_dft_scl_translate*m_max_value);
      m_brush_scl_translate_sldr->SetRange(0, int(m_max_value*10.0));
      m_brush_scl_translate_text->SetValue(str);
   }
   //edge detect
   if (fconfig.Read("edge_detect", &bval))
      m_edge_detect_chk->SetValue(bval);
   //hidden removal
   if (fconfig.Read("hidden_removal", &bval))
      m_hidden_removal_chk->SetValue(bval);
   //select group
   if (fconfig.Read("select_group", &bval))
      m_select_group_chk->SetValue(bval);
   //2d influence
   if (fconfig.Read("brush_2dinfl", &val))
   {
      str = wxString::Format("%.2f", val);
      m_brush_2dinfl_text->SetValue(str);
   }
   //size 1
   if (fconfig.Read("brush_size1", &val) && val>0.0)
   {
      str = wxString::Format("%d", (int)val);
      m_brush_size1_text->SetValue(str);
   }
   //size 2 link
   if (fconfig.Read("use_brush_size2", &bval))
   {
      m_brush_size2_chk->SetValue(bval);
      if (bval)
      {
         m_brush_size2_sldr->Enable();
         m_brush_size2_text->Enable();
      }
      else
      {
         m_brush_size2_sldr->Disable();
         m_brush_size2_text->Disable();
      }
   }
   //size 2
   if (fconfig.Read("brush_size2", &val) && val>0.0)
   {
      str = wxString::Format("%d", (int)val);
      m_brush_size2_text->SetValue(str);
   }
   //iterations
   if (fconfig.Read("brush_iters", &ival))
   {
      switch (ival)
      {
      case 1:
         m_brush_iterw_rb->SetValue(true);
         m_brush_iters_rb->SetValue(false);
         m_brush_iterss_rb->SetValue(false);
         break;
      case 2:
         m_brush_iterw_rb->SetValue(false);
         m_brush_iters_rb->SetValue(true);
         m_brush_iterss_rb->SetValue(false);
         break;
      case 3:
         m_brush_iterw_rb->SetValue(false);
         m_brush_iters_rb->SetValue(false);
         m_brush_iterss_rb->SetValue(true);
         break;
      }
   }
   if (fconfig.Read("mask_alpha", &m_dft_mask_alpha))
   {
       str = wxString::Format("%d", (int)val);
       m_mask_overlay_alpha_text->SetValue(str);
   }

   if (fconfig.Read("use_dslt", &bval))
   {
	   m_dslt_chk->SetValue(bval);
	   if (bval)
	   {
		   st_dslt_r->Enable();
		   m_dslt_r_sldr->Enable();
		   m_dslt_r_text->Enable();
		   st_dslt_q->Enable();
		   m_dslt_q_sldr->Enable();
		   m_dslt_q_text->Enable();
		   st_dslt_c->Enable();
		   m_dslt_c_sldr->Enable();
		   m_dslt_c_text->Enable();
	   }
	   else
	   {
		   st_dslt_r->Disable();
		   m_dslt_r_sldr->Disable();
		   m_dslt_r_text->Disable();
		   st_dslt_q->Disable();
		   m_dslt_q_sldr->Disable();
		   m_dslt_q_text->Disable();
		   st_dslt_c->Disable();
		   m_dslt_c_sldr->Disable();
		   m_dslt_c_text->Disable();
	   }
   }
   if (fconfig.Read("dslt_r", &val) && val>0.0)
   {
      str = wxString::Format("%d", (int)val);
      m_dslt_r_text->SetValue(str);
   }
   if (fconfig.Read("dslt_q", &val) && val>0.0)
   {
      str = wxString::Format("%d", (int)val);
      m_dslt_q_text->SetValue(str);
   }
   if (fconfig.Read("dslt_c", &m_dft_dslt_c))
   {
      str = wxString::Format("%.1f", m_dft_dslt_c*m_max_value);
      m_dslt_c_sldr->SetRange(0, int(m_max_value*10.0));
      m_dslt_c_text->SetValue(str);
   }

   //component analyzer
   //selected only
   if (fconfig.Read("ca_select_only", &bval))
      m_ca_select_only_chk->SetValue(bval);
   //min voxel
   if (fconfig.Read("ca_min", &ival))
   {
      m_dft_ca_min = ival;
      str = wxString::Format("%d", ival);
      m_ca_min_text->SetValue(str);
   }
   //max voxel
   if (fconfig.Read("ca_max", &ival))
   {
      m_dft_ca_max = ival;
      str = wxString::Format("%d", ival);
      m_ca_max_text->SetValue(str);
   }
   //ignore max
   if (fconfig.Read("ca_ignore_max", &bval))
   {
      m_ca_ignore_max_chk->SetValue(bval);
      if (bval)
         m_ca_max_text->Disable();
      else
         m_ca_max_text->Enable();
   }
   //thresh
   if (fconfig.Read("ca_thresh", &m_dft_ca_thresh))
   {
      str = wxString::Format("%.1f", m_dft_ca_thresh*m_max_value);
      m_ca_thresh_sldr->SetRange(0, int(m_max_value*10.0));
      m_ca_thresh_text->SetValue(str);
   }
   //nr thresh
   fconfig.Read("nr_thresh", &m_dft_nr_thresh);
   //nr size
   if (fconfig.Read("nr_size", &m_dft_nr_size))
   {
      str = wxString::Format("%d", (int)val);
      m_nr_size_text->SetValue(str);
   }

   //eve thresh
   fconfig.Read("eve_thresh", &m_dft_eve_thresh);
   //eve radius
   if (fconfig.Read("eve_min_radius", &m_dft_eve_min_radius))
   {
       str = wxString::Format("%d", (int)val);
       m_eve_min_radius_text->SetValue(str);
   }
   if (fconfig.Read("eve_max_radius", &m_dft_eve_max_radius))
   {
       str = wxString::Format("%d", (int)val);
       m_eve_max_radius_text->SetValue(str);
   }
}

//calculations
//operands
void BrushToolDlg::OnLoadA(wxCommandEvent &event)
{
   VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
   if (vr_frame)
   {
      m_vol1 = vr_frame->GetCurSelVol();
      if (m_vol1)
         m_calc_a_text->SetValue(m_vol1->GetName());
   }
}

void BrushToolDlg::OnLoadB(wxCommandEvent &event)
{
   VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
   if (vr_frame)
   {
      m_vol2 = vr_frame->GetCurSelVol();
      if (m_vol2)
         m_calc_b_text->SetValue(m_vol2->GetName());
   }
}

void BrushToolDlg::LoadVolumes()
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame) return;

	DataManager* mgr = vr_frame->GetDataManager();
    if (!mgr) return;

	m_vol1 = mgr->GetVolumeData(m_calc_a_text->GetValue());
	m_vol2 = mgr->GetVolumeData(m_calc_b_text->GetValue());
}

//operators
void BrushToolDlg::OnCalcSub(wxCommandEvent &event)
{
	LoadVolumes();

   if (!m_vol1 || !m_vol2)
      return;

   VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
   if (vr_frame)
   {
      m_cur_view = 0;
      for (int i=0; i<vr_frame->GetViewNum(); i++)
      {
         VRenderView* vrv = vr_frame->GetView(i);
         wxString str = m_vol1->GetName();
         if (vrv && vrv->GetVolumeData(str))
         {
            m_cur_view = vrv;
            break;
         }
      }

      if (!m_cur_view)
         m_cur_view = vr_frame->GetView(0);

      if (m_cur_view)
      {
         m_cur_view->SetVolumeA(m_vol1);
         m_cur_view->SetVolumeB(m_vol2);
         m_cur_view->Calculate(1);
		 m_vol1 = m_cur_view->GetVolumeA();
		 m_vol2 = m_cur_view->GetVolumeB();
		 m_calc_a_text->SetValue(m_vol1 ? m_vol1->GetName() : wxT(""));
		 m_calc_b_text->SetValue(m_vol2 ? m_vol2->GetName() : wxT(""));
	  }
   }
}

void BrushToolDlg::OnCalcAdd(wxCommandEvent &event)
{
	LoadVolumes();

   if (!m_vol1 || !m_vol2)
      return;

   VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
   if (vr_frame)
   {
      m_cur_view = 0;
      for (int i=0; i<vr_frame->GetViewNum(); i++)
      {
         VRenderView* vrv = vr_frame->GetView(i);
         wxString str = m_vol1->GetName();
         if (vrv && vrv->GetVolumeData(str))
         {
            m_cur_view = vrv;
            break;
         }
      }

      if (!m_cur_view)
         m_cur_view = vr_frame->GetView(0);

      if (m_cur_view)
      {
         m_cur_view->SetVolumeA(m_vol1);
         m_cur_view->SetVolumeB(m_vol2);
         m_cur_view->Calculate(2);
		 m_vol1 = m_cur_view->GetVolumeA();
		 m_vol2 = m_cur_view->GetVolumeB();
		 m_calc_a_text->SetValue(m_vol1 ? m_vol1->GetName() : wxT(""));
		 m_calc_b_text->SetValue(m_vol2 ? m_vol2->GetName() : wxT(""));
      }
   }
}

void BrushToolDlg::OnCalcDiv(wxCommandEvent &event)
{
	LoadVolumes();

   if (!m_vol1 || !m_vol2)
      return;

   VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
   if (vr_frame)
   {
      m_cur_view = 0;
      for (int i=0; i<vr_frame->GetViewNum(); i++)
      {
         VRenderView* vrv = vr_frame->GetView(i);
         wxString str = m_vol1->GetName();
         if (vrv && vrv->GetVolumeData(str))
         {
            m_cur_view = vrv;
            break;
         }
      }

      if (!m_cur_view)
         m_cur_view = vr_frame->GetView(0);

      if (m_cur_view)
      {
         m_cur_view->SetVolumeA(m_vol1);
         m_cur_view->SetVolumeB(m_vol2);
         m_cur_view->Calculate(3);
		 m_vol1 = m_cur_view->GetVolumeA();
		 m_vol2 = m_cur_view->GetVolumeB();
		 m_calc_a_text->SetValue(m_vol1 ? m_vol1->GetName() : wxT(""));
		 m_calc_b_text->SetValue(m_vol2 ? m_vol2->GetName() : wxT(""));
      }
   }
}

void BrushToolDlg::OnCalcIsc(wxCommandEvent &event)
{
	LoadVolumes();

   if (!m_vol1 || !m_vol2)
      return;

   VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
   if (vr_frame)
   {
      m_cur_view = 0;
      for (int i=0; i<vr_frame->GetViewNum(); i++)
      {
         VRenderView* vrv = vr_frame->GetView(i);
         wxString str = m_vol1->GetName();
         if (vrv && vrv->GetVolumeData(str))
         {
            m_cur_view = vrv;
            break;
         }
      }

      if (!m_cur_view)
         m_cur_view = vr_frame->GetView(0);

      if (m_cur_view)
      {
         m_cur_view->SetVolumeA(m_vol1);
         m_cur_view->SetVolumeB(m_vol2);
         m_cur_view->Calculate(4);
		 m_vol1 = m_cur_view->GetVolumeA();
		 m_vol2 = m_cur_view->GetVolumeB();
		 m_calc_a_text->SetValue(m_vol1 ? m_vol1->GetName() : wxT(""));
		 m_calc_b_text->SetValue(m_vol2 ? m_vol2->GetName() : wxT(""));
      }
   }
}

//one-operators
void BrushToolDlg::OnCalcFill(wxCommandEvent &event)
{
	LoadVolumes();

   if (!m_vol1)
      return;

   VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
   if (vr_frame)
   {
      m_cur_view = 0;
      for (int i=0; i<vr_frame->GetViewNum(); i++)
      {
         VRenderView* vrv = vr_frame->GetView(i);
         wxString str = m_vol1->GetName();
         if (vrv && vrv->GetVolumeData(str))
         {
            m_cur_view = vrv;
            break;
         }
      }

      if (!m_cur_view)
         m_cur_view = vr_frame->GetView(0);

      if (m_cur_view)
      {
         m_cur_view->SetVolumeA(m_vol1);
         m_vol2 = 0;
         m_cur_view->SetVolumeB(0);
         m_calc_b_text->Clear();
         m_cur_view->Calculate(9);
		 m_vol1 = m_cur_view->GetVolumeA();
		 m_vol2 = m_cur_view->GetVolumeB();
		 m_calc_a_text->SetValue(m_vol1 ? m_vol1->GetName() : wxT(""));
		 m_calc_b_text->SetValue(m_vol2 ? m_vol2->GetName() : wxT(""));
      }
   }
}

void BrushToolDlg::OnIdle(wxTimerEvent &event)
{
   if (!IsShownOnScreen())
      return;

    VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
    if (!vr_frame) return;

    if (!m_cur_view || !m_cur_view->m_glview || m_cur_view->m_glview->m_capture )
        return;
    
    VolumeData *vd = m_cur_view->GetVolumeA();
    if (!vd)
        return;

    wxPoint pos = wxGetMousePosition();
    wxRect ca_reg = m_ca_thresh_sldr->GetScreenRect();
    wxRect eve_reg = m_eve_threshold_sldr->GetScreenRect();
    
    bool cur_mode = vd->GetHighlightingMode();
    
    bool focused =  this->HasFocus();
    int page = m_notebook->GetSelection();
    
    if ((m_notebook && m_notebook->GetSelection() == 2) && (ca_reg.Contains(pos) || eve_reg.Contains(pos)) )
    {
        if (!cur_mode)
        {
            if (Texture::mask_undo_num_ == 0)
            {
                Texture::mask_undo_num_ = 1;
                mask_undo_num_modified = true;
            }
            vd->AddEmptyMask();
            if (vd->GetVR())
                vd->GetVR()->return_mask();
            if (vd->GetTexture())
                vd->GetTexture()->push_mask();
            auto blank = std::shared_ptr<vks::VTexture>();
            vd->Set2DWeight(blank, blank);
            vd->DrawMask(0, 5, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false, NULL, true, false, false);
            if (vd->GetVR())
            {
                vd->GetVR()->return_mask();
                vd->GetVR()->clear_tex_current_mask();
            }
            vd->SetUseMaskThreshold(true);
            if (ca_reg.Contains(pos))
                vd->SetMaskThreshold(m_dft_ca_thresh);
            else
                vd->SetMaskThreshold(m_dft_eve_thresh);
            vd->SetHighlightingMode(true);
            m_cur_view->RefreshGL();
        }
    }
    else
    {
        if (cur_mode)
        {
            if (vd->GetTexture())
            {
                vd->GetTexture()->mask_undos_backward();
                if (mask_undo_num_modified)
                {
                    mask_undo_num_modified = false;
                    Texture::mask_undo_num_ = 0;
                    vd->GetTexture()->trim_mask_undos_tail();
                }
            }
            if (vd->GetVR())
                vd->GetVR()->clear_tex_current_mask();
            vd->SetUseMaskThreshold(false);
            vd->SetHighlightingMode(false);
            m_cur_view->RefreshGL();
        }
    }
}
