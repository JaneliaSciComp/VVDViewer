#include "VPropView.h"
#include "VAnnoView.h"
#include "VRenderFrame.h"
#include <wx/wfstream.h>
#include <wx/fileconf.h>
#include <wx/colordlg.h>
#include <wx/valnum.h>
#include <wx/stdpaths.h>
#include <wx/statline.h>

////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(vpTextCtrl, wxTextCtrl)
EVT_SET_FOCUS(vpTextCtrl::OnSetFocus)
EVT_KILL_FOCUS(vpTextCtrl::OnKillFocus)
EVT_TEXT(wxID_ANY, vpTextCtrl::OnText)
EVT_TEXT_ENTER(wxID_ANY, vpTextCtrl::OnEnter)
EVT_KEY_DOWN(vpTextCtrl::OnKeyDown)
EVT_KEY_UP(vpTextCtrl::OnKeyUp)
END_EVENT_TABLE()

vpTextCtrl::vpTextCtrl(
	wxWindow* frame,
	wxWindow* parent,
	wxWindowID id,
	const wxString& text,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxValidator& valid) :
	wxTextCtrl(parent, id, text, pos, size, style, valid),
	m_frame(frame),
	m_style(style)
{
	SetHint(text);

	m_dummy = new wxButton(parent, id, text, pos, size);
	m_dummy->Hide();
}

vpTextCtrl::~vpTextCtrl()
{

}

void vpTextCtrl::OnSetChildFocus(wxChildFocusEvent& event)
{
	VRenderFrame* vrf = (VRenderFrame*)m_frame;
	if (vrf) vrf->SetKeyLock(true);
	event.Skip();
}

void vpTextCtrl::OnSetFocus(wxFocusEvent& event)
{
	VRenderFrame* vrf = (VRenderFrame*)m_frame;
	if (vrf) vrf->SetKeyLock(true);
	event.Skip();
}

void vpTextCtrl::OnKillFocus(wxFocusEvent& event)
{
	VRenderFrame* vrf = (VRenderFrame*)m_frame;
	if (vrf) vrf->SetKeyLock(false);
	wxCommandEvent ev(wxEVT_COMMAND_TEXT_ENTER);
	if (this->GetParent())
		((VPropView*)this->GetParent())->UpdateMaxValue();
	event.Skip();
}

void vpTextCtrl::OnText(wxCommandEvent& event)
{
	VRenderFrame* vrf = (VRenderFrame*)m_frame;
	if (vrf) vrf->SetKeyLock(true);
}

void vpTextCtrl::OnEnter(wxCommandEvent& event)
{
	if (m_style & wxTE_PROCESS_ENTER)
		m_dummy->SetFocus();

	//	event.Skip();
}

void vpTextCtrl::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void vpTextCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

/////////////////////////////////////////////////////////////////////////


BEGIN_EVENT_TABLE(VPropView, wxPanel)
	EVT_TEXT_ENTER(ID_MaxText, VPropView::OnEnterInMaxText)
	//1
	EVT_COMMAND_SCROLL(ID_GammaSldr, VPropView::OnGammaChange)
	EVT_TEXT(ID_GammaText, VPropView::OnGammaText)
	EVT_COMMAND_SCROLL(ID_BoundarySldr, VPropView::OnBoundaryChange)
	EVT_TEXT(ID_BoundaryText, VPropView::OnBoundaryText)
	//2
	EVT_COMMAND_SCROLL(ID_ContrastSldr, VPropView::OnContrastChange)
	EVT_TEXT(ID_ContrastText, VPropView::OnContrastText)
	EVT_COMMAND_SCROLL(ID_LeftThreshSldr, VPropView::OnLeftThreshChange)
	EVT_TEXT(ID_LeftThreshText, VPropView::OnLeftThreshText)
	EVT_COMMAND_SCROLL(ID_RightThreshSldr, VPropView::OnRightThreshChange)
	EVT_TEXT(ID_RightThreshText, VPropView::OnRightThreshText)
	//3
	EVT_COMMAND_SCROLL(ID_LuminanceSldr, VPropView::OnLuminanceChange)
	EVT_TEXT(ID_LuminanceText, VPropView::OnLuminanceText)
	EVT_CHECKBOX(ID_ShadowChk, VPropView::OnShadowEnable)
	EVT_COMMAND_SCROLL(ID_ShadowSldr, VPropView::OnShadowChange)
	EVT_TEXT(ID_ShadowText, VPropView::OnShadowText)
	EVT_COMMAND_SCROLL(ID_HiShadingSldr, VPropView::OnHiShadingChange)
	EVT_TEXT(ID_HiShadingText, VPropView::OnHiShadingText)
	//4
	EVT_CHECKBOX(ID_AlphaChk, VPropView::OnAlphaCheck)
	EVT_COMMAND_SCROLL(ID_AlphaSldr, VPropView::OnAlphaChange)
	EVT_TEXT(ID_Alpha_Text, VPropView::OnAlphaText)
	EVT_COMMAND_SCROLL(ID_SampleSldr, VPropView::OnSampleChange)
	EVT_TEXT(ID_SampleText, VPropView::OnSampleText)
	//5
	EVT_COMMAND_SCROLL(ID_LowShadingSldr, VPropView::OnLowShadingChange)
	EVT_TEXT(ID_LowShadingText, VPropView::OnLowShadingText)
	EVT_CHECKBOX(ID_ShadingEnableChk, VPropView::OnShadingEnable)
	//colormap
	EVT_CHECKBOX(ID_ColormapEnableChk, VPropView::OnEnableColormap)
	EVT_COMMAND_SCROLL(ID_ColormapHighValueSldr, VPropView::OnColormapHighValueChange)
	EVT_TEXT(ID_ColormapHighValueText, VPropView::OnColormapHighValueText)
	EVT_COMMAND_SCROLL(ID_ColormapLowValueSldr, VPropView::OnColormapLowValueChange)
	EVT_TEXT(ID_ColormapLowValueText, VPropView::OnColormapLowValueText)
	//roi
	EVT_TEXT_ENTER(ID_ROINameText, VPropView::OnEnterInROINameText)
	EVT_COLOURPICKER_CHANGED(ID_ROIColorBtn, VPropView::OnROIColorBtn)
	EVT_COMBOBOX(ID_ROIDispModesCombo, VPropView::OnROIDispModesCombo)
	//6
	//color
	EVT_TEXT(ID_ColorText, VPropView::OnColorTextChange)
	EVT_COLOURPICKER_CHANGED(ID_ColorBtn, VPropView::OnColorBtn)
	//spacings
	EVT_TEXT(ID_SpaceXText, VPropView::OnSpaceText)
	EVT_TEXT(ID_SpaceYText, VPropView::OnSpaceText)
	EVT_TEXT(ID_SpaceZText, VPropView::OnSpaceText)
	//scale bar
	EVT_CHECKBOX(ID_ScaleChk, VPropView::OnScaleCheck)
	EVT_CHECKBOX(ID_ScaleTextChk, VPropView::OnScaleTextCheck)
	EVT_TEXT(ID_ScaleText, VPropView::OnScaleTextEditing)
	EVT_COMBOBOX(ID_ScaleCmb, VPropView::OnScaleUnitSelected)
	EVT_CHECKBOX(ID_ScaleLenFixChk, VPropView::OnScaleLenFixCheck)
	EVT_COMBOBOX(ID_ScaleDigitCombo, VPropView::OnScaleDigitSelected)
	//legend
	EVT_CHECKBOX(ID_LegendChk, VPropView::OnLegendCheck)
	//sync within group
	EVT_CHECKBOX(ID_SyncGroupChk, VPropView::OnSyncGroupCheck)
	EVT_CHECKBOX(ID_SyncGroupSpcChk, VPropView::OnSyncGroupSpcCheck)
	//save default
	EVT_BUTTON(ID_SaveDefault, VPropView::OnSaveDefault)
	EVT_BUTTON(ID_ResetDefault, VPropView::OnResetDefault)
	//inversion
	EVT_CHECKBOX(ID_InvChk, VPropView::OnInvCheck)
	//Index color
	EVT_CHECKBOX(ID_IDCLChk, VPropView::OnIDCLCheck)
	//MIP
	EVT_CHECKBOX(ID_MipChk, VPropView::OnMIPCheck)
	//noise reduction
	EVT_CHECKBOX(ID_NRChk, VPropView::OnNRCheck)
	//depth mode
	EVT_CHECKBOX(ID_DepthChk, VPropView::OnDepthCheck)
	EVT_CHECKBOX(ID_MaskHideOutside, VPropView::OnMaskHideOutsideCheck)
	EVT_CHECKBOX(ID_MaskHideInside, VPropView::OnMaskHideInsideCheck)
	END_EVENT_TABLE()

	VPropView::VPropView(wxWindow* frame,
	wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name):
wxPanel(parent, id, pos, size,style, name),
	m_frame(frame),
	m_vd(0),
	m_lumi_change(false),
	m_sync_group(false),
	m_sync_group_spc(false),
	m_group(0),
	m_vrv(0),
	m_max_val(255.0),
	m_is_float(false),
	m_space_x_text(0),
	m_space_y_text(0),
	m_space_z_text(0),
	m_sizer_sl_righ(0),
	m_sizer_r5(0),
	m_sizer_r6(0),
	m_roi_id(-1)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	wxBoxSizer* sizer_all = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizer_sl_b = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* sizer_sliders = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizer_sl_left = new wxBoxSizer(wxVERTICAL);
	m_sizer_sl_righ = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* sizer_checks = new wxBoxSizer(wxVERTICAL);


	wxBoxSizer* sizer_l1 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizer_l2 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizer_l3 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizer_l4 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizer_l5 = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* sizer_r1 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizer_r2 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizer_r3 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizer_r4 = new wxBoxSizer(wxHORIZONTAL);
	m_sizer_r5 = new wxBoxSizer(wxHORIZONTAL);
	m_sizer_r6 = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* sizer_b = new wxBoxSizer(wxHORIZONTAL);

	wxStaticText* st = 0;

	//validator: floating point 1
	wxFloatingPointValidator<double> vald_fp1(1);
	//validator: floating point 2
	wxFloatingPointValidator<double> vald_fp2(2);
	//validator: floating point 3
	wxFloatingPointValidator<double> vald_fp3(3);
	//validator: floating point 4
	wxFloatingPointValidator<double> vald_fp4(4);
	//validator: floating point 6
	wxFloatingPointValidator<double> vald_fp6(6);
	//validator: floating point 8
	wxFloatingPointValidator<double> vald_fp8(8);
	//validator: integer
	wxIntegerValidator<unsigned int> vald_int;

#if defined(__WXGTK__)
	int col1_text_w = 60;
	int col2_text_w = 70;
	int unit_comb_w = 90;
	int digit_comb_w = 80;
#else
	int col1_text_w = 50;
	int col2_text_w = 60;
	int unit_comb_w = 50;
	int digit_comb_w = 40;
#endif

#ifdef _DARWIN
	int spc_formsize = 50;
	int sb_formsize = 55;
#else
	int spc_formsize = 60;
	int sb_formsize = 60;
#endif

	//1st line	
	//gamma
	st = new wxStaticText(this, 0, ":Gamma",
		wxDefaultPosition, wxSize(110, 20));
	m_gamma_sldr = new wxSlider(this, ID_GammaSldr, 100, 10, 400,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL|wxSL_INVERSE);
	m_gamma_text = new wxTextCtrl(this, ID_GammaText, "1.00",
		wxDefaultPosition, wxSize(col1_text_w, 20), 0, vald_fp2);
	sizer_l1->Add(m_gamma_sldr, 1, wxEXPAND);
	sizer_l1->Add(m_gamma_text, 0, wxALIGN_CENTER);
	sizer_l1->Add(st, 0, wxALIGN_CENTER);
	//extract boundary
	st = new wxStaticText(this, 0, "Extract Boundary:",
		wxDefaultPosition, wxSize(140, 20), wxALIGN_RIGHT);
	m_boundary_sldr = new wxSlider(this, ID_BoundarySldr, 0, 0, 1000,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_boundary_text = new wxTextCtrl(this, ID_BoundaryText, "0.0000",
		wxDefaultPosition, wxSize(col2_text_w, 20), 0, vald_fp4);
	sizer_r1->Add(st, 0, wxALIGN_CENTER);
	sizer_r1->Add(m_boundary_text, 0, wxALIGN_CENTER);
	sizer_r1->Add(m_boundary_sldr, 1, wxEXPAND);

	//2nd line
	//saturation point
	st = new wxStaticText(this, 0, ":Saturation Point",
		wxDefaultPosition, wxSize(110, 20));
	m_contrast_sldr = new wxSlider(this, ID_ContrastSldr, 255, 0, 255,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_contrast_text = new wxTextCtrl(this, ID_ContrastText, "50",
		wxDefaultPosition, wxSize(col1_text_w, 20), 0, vald_int);
	sizer_l2->Add(m_contrast_sldr, 1, wxEXPAND);
	sizer_l2->Add(m_contrast_text, 0, wxALIGN_CENTER);
	sizer_l2->Add(st, 0, wxALIGN_CENTER);
	//thresholds
	m_threh_st = new wxStaticText(this, 0, "Threshold:",
		wxDefaultPosition, wxSize(140, 20), wxALIGN_RIGHT);
	m_left_thresh_sldr = new wxSlider(this, ID_LeftThreshSldr, 5, 0, 255,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_left_thresh_text = new wxTextCtrl(this, ID_LeftThreshText, "5",
		wxDefaultPosition, wxSize(col2_text_w, 20), 0, vald_int);
	m_right_thresh_sldr = new wxSlider(this, ID_RightThreshSldr, 230, 0, 255,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_right_thresh_text = new wxTextCtrl(this, ID_RightThreshText, "230",
		wxDefaultPosition, wxSize(col2_text_w, 20), 0, vald_int);
	sizer_r2->Add(m_threh_st, 0, wxALIGN_CENTER);
	sizer_r2->Add(m_left_thresh_text, 0, wxALIGN_CENTER);
	sizer_r2->Add(m_left_thresh_sldr, 1, wxEXPAND);
	sizer_r2->Add(m_right_thresh_text, 0, wxALIGN_CENTER);
	sizer_r2->Add(m_right_thresh_sldr,1, wxEXPAND);

	//3rd line
	//luminance
	st = new wxStaticText(this, 0, ":Luminance",
		wxDefaultPosition, wxSize(110, 20));
	m_luminance_sldr = new wxSlider(this, ID_LuminanceSldr, 128, 0, 255,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_luminance_text = new wxTextCtrl(this, ID_LuminanceText, "128",
		wxDefaultPosition, wxSize(col1_text_w, 20), 0, vald_int);
	sizer_l3->Add(m_luminance_sldr, 1, wxEXPAND, 0);
	sizer_l3->Add(m_luminance_text, 0, wxALIGN_CENTER, 0);
	sizer_l3->Add(st, 0, wxALIGN_CENTER, 0);
	//shadow
	sizer_r3->Add(10,5,0);
	m_shadow_chk = new wxCheckBox(this, ID_ShadowChk, "Shadow / Light:",
		wxDefaultPosition, wxSize(130, 20), wxALIGN_RIGHT);
	m_shadow_sldr = new wxSlider(this, ID_ShadowSldr, 0, 0, 100,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_shadow_text = new wxTextCtrl(this, ID_ShadowText, "0.00",
		wxDefaultPosition, wxSize(col2_text_w, 20), 0, vald_fp2);
	sizer_r3->Add(m_shadow_chk, 0, wxALIGN_CENTER);
	sizer_r3->Add(m_shadow_text, 0, wxALIGN_CENTER);
	sizer_r3->Add(m_shadow_sldr, 1, wxEXPAND);
	//highlight
	m_hi_shading_sldr = new wxSlider(this, ID_HiShadingSldr, 0, 0, 100,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_hi_shading_text = new wxTextCtrl(this, ID_HiShadingText, "0.00",
		wxDefaultPosition, wxSize(col2_text_w, 20), 0, vald_fp2);
    m_hi_shading_sldr->Hide();
    m_hi_shading_text->Hide();
    //sizer_r3->Add(m_hi_shading_text, 0, wxALIGN_CENTER);
	//sizer_r3->Add(m_hi_shading_sldr, 1, wxEXPAND);

	//4th line
	//alpha
	st = new wxStaticText(this, 0, ":",
		wxDefaultPosition, wxSize(5, 20));
	m_alpha_chk = new wxCheckBox(this, ID_AlphaChk, "Alpha",
		wxDefaultPosition, wxSize(105, 20));
	m_alpha_sldr = new wxSlider(this, ID_AlphaSldr, 127, 0, 255,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_alpha_text = new wxTextCtrl(this, ID_Alpha_Text, "127",
		wxDefaultPosition, wxSize(col1_text_w, 20), 0, vald_int);
	sizer_l4->Add(m_alpha_sldr, 1, wxEXPAND);
	sizer_l4->Add(m_alpha_text, 0, wxALIGN_CENTER);
	sizer_l4->Add(st, 0, wxALIGN_CENTER);
	sizer_l4->Add(m_alpha_chk, 0, wxALIGN_CENTER);
	//sample rate
	st = new wxStaticText(this, 0, "Sample Rate:",
		wxDefaultPosition, wxSize(140, 20), wxALIGN_RIGHT);
	m_sample_sldr = new wxSlider(this, ID_SampleSldr, 50, 0, 100,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_sample_text = new wxTextCtrl(this, ID_SampleText, "1.0",
		wxDefaultPosition, wxSize(col2_text_w, 20), 0, vald_fp2);
	sizer_r4->Add(st, 0, wxALIGN_CENTER);
	sizer_r4->Add(m_sample_text, 0, wxALIGN_CENTER);
	sizer_r4->Add(m_sample_sldr, 1, wxEXPAND);

	//5th line
	//shading
	m_low_shading_sldr = new wxSlider(this, ID_LowShadingSldr, 0, 0, 200,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_low_shading_text = new wxTextCtrl(this, ID_LowShadingText, "0.00",
		wxDefaultPosition, wxSize(col1_text_w, 20), 0, vald_fp2);
	st = new wxStaticText(this, 0, ":",
		wxDefaultPosition, wxSize(5, 20));
	m_shading_enable_chk = new wxCheckBox(this, ID_ShadingEnableChk, "Shading",
		wxDefaultPosition, wxSize(105, 20));
	sizer_l5->Add(m_low_shading_sldr, 1, wxEXPAND);
	sizer_l5->Add(m_low_shading_text, 0, wxALIGN_CENTER);
	sizer_l5->Add(st, 0, wxALIGN_CENTER);
	sizer_l5->Add(m_shading_enable_chk, 0, wxALIGN_CENTER);
	//colormap
	m_sizer_r5->Add(10,5,0);
	m_colormap_enable_chk = new wxCheckBox(this, ID_ColormapEnableChk, "Colormap: Low (B)",
		wxDefaultPosition, wxSize(140, 20), wxALIGN_RIGHT);
	m_sizer_r5->Add(m_colormap_enable_chk, 0, wxALIGN_CENTER);
	m_colormap_low_value_text = new wxTextCtrl(this, ID_ColormapLowValueText, "0",
		wxDefaultPosition, wxSize(col1_text_w, 20), 0, vald_int);
	m_sizer_r5->Add(m_colormap_low_value_text, 0, wxALIGN_CENTER);
	m_colormap_low_value_sldr = new wxSlider(this, ID_ColormapLowValueSldr, 0, 0, 255,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_sizer_r5->Add(m_colormap_low_value_sldr, 1, wxEXPAND);
	m_colormap_high_value_text = new wxTextCtrl(this, ID_ColormapHighValueText, "255",
		wxDefaultPosition + wxPoint(10,0), wxSize(col1_text_w, 20), 0, vald_int);
	m_sizer_r5->Add(m_colormap_high_value_text, 0, wxALIGN_CENTER);
	m_colormap_high_value_sldr = new wxSlider(this, ID_ColormapHighValueSldr, 255, 0, 255,
		wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	m_sizer_r5->Add(m_colormap_high_value_sldr, 1, wxEXPAND);
	st = new wxStaticText(this, 0, "High (R)");
	m_sizer_r5->Add(st, 0, wxALIGN_CENTER);

	//roi
	m_roi_st = new wxStaticText(this, 0, "Segment:", wxDefaultPosition,  wxSize(140, 20), wxALIGN_RIGHT);
#ifdef _DARWIN
    m_roi_text = new myTextCtrl(frame, this, ID_ROINameText, "",
        wxDefaultPosition, wxSize(150, 22), wxTE_PROCESS_ENTER);
    m_roi_color_btn = new wxColourPickerCtrl(this, ID_ROIColorBtn, *wxRED,
        wxDefaultPosition, wxDefaultSize);
    m_roi_disp_mode_combo = new wxComboBox(this, ID_ROIDispModesCombo, "",
        wxDefaultPosition, wxSize(145, 30), 0, NULL, wxCB_READONLY);
#else
	m_roi_text = new myTextCtrl(frame, this, ID_ROINameText, "",
		wxDefaultPosition, wxSize(150, 20), wxTE_PROCESS_ENTER);
	m_roi_color_btn = new wxColourPickerCtrl(this, ID_ROIColorBtn, *wxRED,
		wxDefaultPosition, wxDefaultSize);
	m_roi_disp_mode_combo = new wxComboBox(this, ID_ROIDispModesCombo, "",
		wxDefaultPosition, wxSize(140, 24), 0, NULL, wxCB_READONLY);
#endif
	vector<string>dispmode_list;
	dispmode_list.push_back("Highlight/Dark");
	dispmode_list.push_back("Highlight/DarkGray");
	dispmode_list.push_back("Highlight/Invisible");
	for (size_t i=0; i<dispmode_list.size(); ++i)
		m_roi_disp_mode_combo->Append(dispmode_list[i]);
	m_roi_disp_mode_combo->SetSelection(0);
	m_sizer_r5->Add(10,5,0);
	m_sizer_r6->Add(m_roi_st, 0, wxALIGN_CENTER);
	m_sizer_r6->Add(m_roi_text, 0, wxALIGN_CENTER);
	m_sizer_r6->Add(10, 10);
	m_sizer_r6->Add(m_roi_color_btn, 0, wxALIGN_CENTER);
	m_sizer_r6->Add(10, 10);
	m_sizer_r6->Add(m_roi_disp_mode_combo, 0, wxALIGN_CENTER);

	//6th line
	//left sliders
	sizer_sl_left->Add(sizer_l1, 0, wxEXPAND);
	sizer_sl_left->Add(sizer_l2, 0, wxEXPAND);
	sizer_sl_left->Add(sizer_l3, 0, wxEXPAND);
	sizer_sl_left->Add(sizer_l4, 0, wxEXPAND);
	sizer_sl_left->Add(sizer_l5, 0, wxEXPAND);

	//right sliders
	m_sizer_sl_righ->Add(sizer_r1, 0, wxEXPAND);
	m_sizer_sl_righ->Add(sizer_r2, 0, wxEXPAND);
	m_sizer_sl_righ->Add(sizer_r3, 0, wxEXPAND);
	m_sizer_sl_righ->Add(sizer_r4, 0, wxEXPAND);
	m_sizer_sl_righ->Add(m_sizer_r5, 0, wxEXPAND);
	m_sizer_sl_righ->Add(m_sizer_r6, 0, wxEXPAND);

	//all sliders
	sizer_sliders->Add(sizer_sl_left, 4, wxEXPAND);
	sizer_sliders->Add(m_sizer_sl_righ, 5, wxEXPAND);

	//bottom line
	st = new wxStaticText(this, 0, "Max Value:");
	m_max_text = new vpTextCtrl(m_frame, this, ID_MaxText, "1.00",
		wxDefaultPosition, wxSize(80, 20), wxTE_PROCESS_ENTER, vald_fp2);

	sizer_b->Add(10, 5, 0);
	sizer_b->Add(st, 0, wxALIGN_CENTER, 0);
	sizer_b->Add(m_max_text, 0, wxALIGN_CENTER, 0);

	//color
	st = new wxStaticText(this, 0, "Color:");
	m_color_text = new wxTextCtrl(this, ID_ColorText, "255 , 255 , 255",
		wxDefaultPosition, wxSize(110, 20));
	m_color_text->Connect(ID_ColorText, wxEVT_LEFT_DCLICK,
		wxCommandEventHandler(VPropView::OnColorTextFocus),
		NULL, this);
	m_color_btn = new wxColourPickerCtrl(this, ID_ColorBtn, *wxRED,
		wxDefaultPosition, wxDefaultSize);

	sizer_b->Add(10, 5, 0);
	sizer_b->Add(st, 0, wxALIGN_CENTER, 0);
	sizer_b->Add(m_color_text, 0, wxALIGN_CENTER, 0);
	sizer_b->Add(m_color_btn, 0, wxALIGN_CENTER, 0);
	//m_color_text->Hide();

	//spaceings
	//x
	st = new wxStaticText(this, 0, "X:");
	m_space_x_text = new wxTextCtrl(this, ID_SpaceXText, "1.000000",
		wxDefaultPosition, wxSize(spc_formsize, 20), 0, vald_fp6);
	sizer_b->Add(10, 5, 0);
	sizer_b->Add(st, 0, wxALIGN_CENTER);
	sizer_b->Add(m_space_x_text, 0, wxALIGN_CENTER);
	//y
	st = new wxStaticText(this, 0, "Y:");
	m_space_y_text = new wxTextCtrl(this, ID_SpaceYText, "1.000000",
		wxDefaultPosition, wxSize(spc_formsize, 20), 0, vald_fp6);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(st, 0, wxALIGN_CENTER);
	sizer_b->Add(m_space_y_text, 0, wxALIGN_CENTER);
	//z
	st = new wxStaticText(this, 0, "Z:");
	m_space_z_text = new wxTextCtrl(this, ID_SpaceZText, "1.000000",
		wxDefaultPosition, wxSize(spc_formsize, 20), 0, vald_fp6);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(st, 0, wxALIGN_CENTER);
	sizer_b->Add(m_space_z_text, 0, wxALIGN_CENTER);

	//scale bar
	m_scale_chk = new wxCheckBox(this, ID_ScaleChk, "SclBar:",
		wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	m_scale_te_chk = new wxCheckBox(this, ID_ScaleTextChk, "Text:",
		wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	m_scale_text = new wxTextCtrl(this, ID_ScaleText, "",
		wxDefaultPosition, wxSize(sb_formsize, 20), 0, vald_fp3);
	m_scale_cmb = new wxComboBox(this, ID_ScaleCmb, "",
		wxDefaultPosition, wxSize(unit_comb_w, 30), 0, NULL, wxCB_READONLY);
	m_scale_cmb->Append("nm");
	m_scale_cmb->Append(L"\u03BCm");
	m_scale_cmb->Append("mm");
	m_scale_cmb->Select(1);
	
	st = new wxStaticText(this, 0, "Digit:");
	m_scale_digit_cmb = new wxComboBox(this, ID_ScaleDigitCombo, "",
		wxDefaultPosition, wxSize(digit_comb_w, 30), 0, NULL, wxCB_READONLY);
	for (int c = 0; c <= 8; c++)
		m_scale_digit_cmb->Append(wxString::Format(wxT(" %i"), c));
	m_scale_digit_cmb->Select(0);
	
	m_scale_lenfix_chk = new wxCheckBox(this, ID_ScaleLenFixChk, "Fix length:",
		wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);

	sizer_b->Add(10, 5, 0);
	sizer_b->Add(m_scale_chk, 0, wxALIGN_CENTER);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(m_scale_te_chk, 0, wxALIGN_CENTER);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(m_scale_text, 0, wxALIGN_CENTER);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(m_scale_cmb, 0, wxALIGN_CENTER);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(st, 0, wxALIGN_CENTER);
	sizer_b->Add(m_scale_digit_cmb, 0, wxALIGN_CENTER_VERTICAL);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(m_scale_lenfix_chk, 0, wxALIGN_CENTER);

	//legend
	m_legend_chk = new wxCheckBox(this, ID_LegendChk, "Legend:",
		wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_legend_chk->Hide();
	//sizer_b->Add(10, 5, 0);
	//sizer_b->Add(m_legend_chk, 0, wxALIGN_CENTER);

	//mask
	st = new wxStaticText(this, 0, "Mask:");
	m_mask_outside_chk = new wxCheckBox(this, ID_MaskHideOutside, "Hide Outside");
	sizer_b->Add(15, 5, 0);
	sizer_b->Add(st, 0, wxALIGN_CENTER);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(m_mask_outside_chk, 0, wxALIGN_CENTER);
	m_mask_inside_chk = new wxCheckBox(this, ID_MaskHideInside, "Hide Inside");
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(m_mask_inside_chk, 0, wxALIGN_CENTER);

	//stretcher
	sizer_b->AddStretchSpacer(1);

	//group
	st = new wxStaticText(this, 0, "Group:");
	//sync group
	m_sync_group_chk = new wxCheckBox(this, ID_SyncGroupChk, "Sync");
	sizer_b->Add(10, 5, 0);
	sizer_b->Add(st, 0, wxALIGN_CENTER);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(m_sync_group_chk, 0, wxALIGN_CENTER);
	m_sync_g_spc_chk = new wxCheckBox(this, ID_SyncGroupSpcChk, "Sync Spacings");
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(m_sync_g_spc_chk, 0, wxALIGN_CENTER);
	//depth mode
	m_depth_chk = new wxCheckBox(this, ID_DepthChk, "Depth",
		wxDefaultPosition, wxDefaultSize);
	sizer_b->Add(5, 5, 0);
	sizer_b->Add(m_depth_chk, 0, wxALIGN_CENTER, 0);

	wxSize csize = wxSize(90, -1);
	//inversion
	m_inv_chk = new wxCheckBox(this, ID_InvChk, ":Inv",
		wxDefaultPosition, csize);
	sizer_checks->Add(5,7,0);
	sizer_checks->Add(m_inv_chk, 0, wxALIGN_LEFT, 0);
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	//Index color
	m_idcl_chk = new wxCheckBox(this, ID_IDCLChk, ":Index",
		wxDefaultPosition, csize);
	sizer_checks->Add(5,7,0);
	sizer_checks->Add(m_idcl_chk, 0, wxALIGN_LEFT, 0);
	//MIP
	m_mip_chk = new wxCheckBox(this, ID_MipChk, ":MIP",
		wxDefaultPosition, csize);
	sizer_checks->Add(5,7,0);
	sizer_checks->Add(m_mip_chk, 0, wxALIGN_LEFT, 0);
	//noise reduction
	m_nr_chk = new wxCheckBox(this, ID_NRChk, ":Smoothing",
		wxDefaultPosition, csize);
	sizer_checks->Add(5,7,0);
	sizer_checks->Add(m_nr_chk, 0, wxALIGN_LEFT, 0);
	sizer_checks->AddStretchSpacer(1);
	//buttons
	m_reset_default = new wxButton(this, ID_ResetDefault, "Reset",
		wxDefaultPosition, wxSize(115, 20));
	m_save_default = new wxButton(this, ID_SaveDefault, "Save as Default",
		wxDefaultPosition, wxSize(115, 20));
	sizer_checks->Add(10,5,0);
	sizer_checks->Add(m_reset_default, 0, wxALIGN_CENTER);
	sizer_checks->Add(10,5,0);
	sizer_checks->Add(m_save_default, 0, wxALIGN_CENTER);
	sizer_checks->Add(10,5,0);

	sizer_sl_b->Add(sizer_sliders, 0, wxEXPAND);
	sizer_sl_b->Add(sizer_b, 0, wxEXPAND);

	sizer_all->Add(sizer_sl_b, 1, wxEXPAND);
	sizer_all->AddSpacer(20);
	sizer_all->Add(sizer_checks, 0, wxEXPAND);

	SetSizer(sizer_all);
	Layout();

	Thaw();
	SetEvtHandlerEnabled(true);
}

VPropView::~VPropView()
{
}

void VPropView::GetSettings()
{
	if (!m_vd)
		return;

	if (m_vd->GetTexture() && m_vd->GetTexture()->get_nrrd(0) &&
		(m_vd->GetTexture()->get_nrrd(0)->getNrrdDataType() == nrrdTypeFloat || m_vd->GetTexture()->get_nrrd(0)->getNrrdDataType() == nrrdTypeDouble))
		m_is_float = true;
	else
		m_is_float = false;

	wxString str;
	double dval = 0.0;
	int ival = 0;

	SetEvtHandlerEnabled(false);

	//maximum value
	m_max_val = m_vd->GetMaxValue();
	if (!m_is_float)
		m_max_val = Max(255.0, m_max_val);
	else
		m_max_val = Max(1.0, m_max_val);
	m_max_text->ChangeValue(wxString::Format("%.2f", m_max_val));

	int sldr_max = m_is_float ? (m_max_val * 100 + 0.5) : m_max_val;

	//set range
	wxFloatingPointValidator<double>* vald_fp;
	wxIntegerValidator<unsigned int>* vald_i;

	//validator: floating point 1
	wxFloatingPointValidator<double> vald_fp1(1);
	//validator: floating point 2
	wxFloatingPointValidator<double> vald_fp2(2);
	//validator: floating point 3
	wxFloatingPointValidator<double> vald_fp3(3);
	//validator: floating point 4
	wxFloatingPointValidator<double> vald_fp4(4);
	//validator: floating point 6
	wxFloatingPointValidator<double> vald_fp6(6);
	//validator: floating point 8
	wxFloatingPointValidator<double> vald_fp8(8);
	//validator: integer
	wxIntegerValidator<unsigned int> vald_int;


	//volume properties
	//transfer function
	//gamma
	if ((vald_fp = (wxFloatingPointValidator<double>*)m_gamma_text->GetValidator()))
		vald_fp->SetRange(0.0, 10.0);
	dval = m_vd->Get3DGamma();
	m_gamma_sldr->SetValue(int(dval*100.0+0.5));
	str = wxString::Format("%.2f", dval);
	m_gamma_text->ChangeValue(str);
	//boundary
	if ((vald_fp = (wxFloatingPointValidator<double>*)m_boundary_text->GetValidator()))
		vald_fp->SetRange(0.0, 1.0);
	dval = m_vd->GetBoundary();
	m_boundary_sldr->SetValue(int(dval*2000.0+0.5));
	str = wxString::Format("%.4f", dval);
	m_boundary_text->ChangeValue(str);
	//contrast
	if (!m_is_float)
	{
		m_contrast_text->SetValidator(vald_int);
		if ((vald_i = (wxIntegerValidator<unsigned int>*)m_contrast_text->GetValidator()))
			vald_i->SetRange(0, int(m_max_val) * 10);
		dval = m_vd->GetOffset();
		ival = int(dval * m_max_val + 0.5);
		m_contrast_sldr->SetRange(0, int(sldr_max));
		str = wxString::Format("%d", ival);
		m_contrast_sldr->SetValue(ival);
		m_contrast_text->ChangeValue(str);
	}
	else
	{
		m_contrast_text->SetValidator(vald_fp2);
		if ((vald_fp = (wxFloatingPointValidator<double>*)m_contrast_text->GetValidator()))
			vald_fp->SetRange(0, m_max_val * 10);
		dval = m_vd->GetOffset() * m_max_val;
		m_contrast_sldr->SetRange(0, sldr_max);
		str = wxString::Format("%.2f", dval);
		m_contrast_sldr->SetValue((int)(dval*100.0 + 0.5));
		m_contrast_text->ChangeValue(str);
	}
	//left threshold
	if (!m_is_float)
	{
		m_left_thresh_text->SetValidator(vald_int);
		if ((vald_i = (wxIntegerValidator<unsigned int>*)m_left_thresh_text->GetValidator()))
			vald_i->SetRange(0, int(m_max_val));
		dval = m_vd->GetLeftThresh();
		ival = int(dval * m_max_val + 0.5);
		m_left_thresh_sldr->SetRange(0, int(sldr_max));
		str = wxString::Format("%d", ival);
		m_left_thresh_sldr->SetValue(ival);
		m_left_thresh_text->ChangeValue(str);
	}
	else
	{
		m_left_thresh_text->SetValidator(vald_fp2);
		if ((vald_fp = (wxFloatingPointValidator<double>*)m_left_thresh_text->GetValidator()))
			vald_fp->SetRange(0, m_max_val);
		dval = m_vd->GetLeftThresh() * m_max_val;
		m_left_thresh_sldr->SetRange(0, sldr_max);
		str = wxString::Format("%.2f", dval);
		m_left_thresh_sldr->SetValue((int)(dval * 100.0 + 0.5));
		m_left_thresh_text->ChangeValue(str);
	}
	//right threshold
	if (!m_is_float)
	{
		m_right_thresh_text->SetValidator(vald_int);
		if ((vald_i = (wxIntegerValidator<unsigned int>*)m_right_thresh_text->GetValidator()))
			vald_i->SetRange(0, int(m_max_val));
		dval = m_vd->GetRightThresh();
		ival = int(dval * m_max_val + 0.5);
		m_right_thresh_sldr->SetRange(0, int(sldr_max));
		str = wxString::Format("%d", ival);
		m_right_thresh_sldr->SetValue(ival);
		m_right_thresh_text->ChangeValue(str);
	}
	else
	{
		m_right_thresh_text->SetValidator(vald_fp2);
		if ((vald_fp = (wxFloatingPointValidator<double>*)m_right_thresh_text->GetValidator()))
			vald_fp->SetRange(0, m_max_val);
		dval = m_vd->GetRightThresh() * m_max_val;
		m_right_thresh_sldr->SetRange(0, sldr_max);
		str = wxString::Format("%.2f", dval);
		m_right_thresh_sldr->SetValue((int)(dval * 100.0 + 0.5));
		m_right_thresh_text->ChangeValue(str);
	}
	//luminance
	if (!m_is_float)
	{
		m_luminance_text->SetValidator(vald_int);
		if ((vald_i = (wxIntegerValidator<unsigned int>*)m_luminance_text->GetValidator()))
			vald_i->SetRange(0, int(m_max_val));
		dval = m_vd->GetLuminance();
		ival = int(dval * m_max_val + 0.5);
		m_luminance_sldr->SetRange(0, int(sldr_max));
		str = wxString::Format("%d", ival);
		m_luminance_sldr->SetValue(ival);
		m_luminance_text->ChangeValue(str);
	}
	else
	{
		m_luminance_text->SetValidator(vald_fp2);
		if ((vald_fp = (wxFloatingPointValidator<double>*)m_luminance_text->GetValidator()))
			vald_fp->SetRange(0, m_max_val);
		dval = m_vd->GetLuminance() * m_max_val;
		m_luminance_sldr->SetRange(0, sldr_max);
		str = wxString::Format("%.2f", dval);
		m_luminance_sldr->SetValue((int)(dval * 100.0 + 0.5));
		m_luminance_text->ChangeValue(str);
	}
	//color
	Color c = m_vd->GetColor();
	wxColor wxc((unsigned char)(c.r()*255+0.5),
		(unsigned char)(c.g()*255+0.5),
		(unsigned char)(c.b()*255+0.5));
	m_color_text->ChangeValue(wxString::Format("%d , %d , %d",
		wxc.Red(), wxc.Green(), wxc.Blue()));
	m_color_btn->SetColour(wxc);
	//alpha
	if (!m_is_float)
	{
		m_alpha_text->SetValidator(vald_int);
		if ((vald_i = (wxIntegerValidator<unsigned int>*)m_alpha_text->GetValidator()))
			vald_i->SetRange(0, int(m_max_val));
		dval = m_vd->GetAlpha();
		ival = int(dval * m_max_val + 0.5);
		m_alpha_sldr->SetRange(0, int(sldr_max));
		str = wxString::Format("%d", ival);
		m_alpha_sldr->SetValue(ival);
		m_alpha_text->ChangeValue(str);
	}
	else
	{
		m_alpha_text->SetValidator(vald_fp2);
		if ((vald_fp = (wxFloatingPointValidator<double>*)m_alpha_text->GetValidator()))
			vald_fp->SetRange(0, m_max_val);
		dval = m_vd->GetAlpha() * m_max_val;
		m_alpha_sldr->SetRange(0, sldr_max);
		str = wxString::Format("%.2f", dval);
		m_alpha_sldr->SetValue((int)(dval * 100.0 + 0.5));
		m_alpha_text->ChangeValue(str);
	}
	bool alpha = m_vd->GetEnableAlpha();
	m_alpha_chk->SetValue(alpha);
	if (alpha)
	{
		m_alpha_sldr->Enable();
		m_alpha_text->Enable();
	}
	else
	{
		m_alpha_sldr->Disable();
		m_alpha_text->Disable();
	}

	//shadings
	if ((vald_fp = (wxFloatingPointValidator<double>*)m_low_shading_text->GetValidator()))
		vald_fp->SetRange(0.0, 10.0);
	if ((vald_fp = (wxFloatingPointValidator<double>*)m_hi_shading_text->GetValidator()))
		vald_fp->SetRange(0.0, 100.0);
	double amb, diff, spec, shine;
	m_vd->GetMaterial(amb, diff, spec, shine);
	m_low_shading_sldr->SetValue(amb*100.0);
	str = wxString::Format("%.2f", amb);
	m_low_shading_text->ChangeValue(str);
	m_hi_shading_sldr->SetValue(shine*10.0);
	str = wxString::Format("%.2f", shine);
	m_hi_shading_text->ChangeValue(str);
	bool shading = m_vd->GetVR()->get_shading();
	m_shading_enable_chk->SetValue(shading);

	if (!alpha || !shading)
	{
		m_low_shading_sldr->Disable();
		m_low_shading_text->Disable();
	}
	else
	{
		m_low_shading_sldr->Enable();
		m_low_shading_text->Enable();
	}

	//shadow
	if ((vald_fp = (wxFloatingPointValidator<double>*)m_shadow_text->GetValidator()))
		vald_fp->SetRange(0.0, 100.0);
	m_shadow_chk->SetValue(m_vd->GetShadow());
	double shadow_int;
	m_vd->GetShadowParams(shadow_int);
	m_shadow_sldr->SetValue(int(shadow_int*100.0+0.5));
	str = wxString::Format("%.2f", shadow_int);
	m_shadow_text->ChangeValue(str);

	//smaple rate
	if ((vald_fp = (wxFloatingPointValidator<double>*)m_sample_text->GetValidator()))
		vald_fp->SetRange(0.0, 100.0);
	double sr = m_vd->GetSampleRate();
	m_sample_sldr->SetValue(int(sr*100.0+0.5));
	str = wxString::Format("%.2f", sr);
	m_sample_text->ChangeValue(str);

	//spacings
	double spcx, spcy, spcz;
	m_vd->GetSpacings(spcx, spcy, spcz, 0);
	int pr = 6;
/*	double minspclog10 = log10(min(min(spcx, spcy), spcz));
	if (minspclog10 <= -2.0)
		pr = 3 - ((int)minspclog10 + 1.0);
*/	wxString fmstr = wxT("%.") + wxString::Format(wxT("%i"), pr) + wxT("f");

	if ((vald_fp = (wxFloatingPointValidator<double>*)m_space_x_text->GetValidator()))
	{
		vald_fp->SetMin(0.0);
		vald_fp->SetPrecision(pr);
	}
	str = wxString::Format(fmstr, spcx);
	m_space_x_text->ChangeValue(str);
	if ((vald_fp = (wxFloatingPointValidator<double>*)m_space_y_text->GetValidator()))
	{
		vald_fp->SetMin(0.0);
		vald_fp->SetPrecision(pr);
	}
	str = wxString::Format(fmstr, spcy);
	m_space_y_text->ChangeValue(str);
	if ((vald_fp = (wxFloatingPointValidator<double>*)m_space_z_text->GetValidator()))
	{
		vald_fp->SetMin(0.0);
		vald_fp->SetPrecision(pr);
	}
	str = wxString::Format(fmstr, spcz);
	m_space_z_text->ChangeValue(str);

	//scale bar
	VRenderFrame *frame = (VRenderFrame*)m_frame;
	if (frame && frame->GetViewList()->size() > 0)
	{
		VRenderView* vrv = (*frame->GetViewList())[0];
		if (vrv && vrv->m_glview)
		{
			m_scale_cmb->Select(vrv->m_glview->m_sb_unit);
			dval = vrv->GetScaleBarLen();
			m_scale_text->ChangeValue(wxString::Format("%.3f", dval));
			bool scale_check = vrv->m_glview->m_disp_scale_bar;
			m_scale_chk->SetValue(scale_check);
			m_scale_te_chk->SetValue(vrv->m_glview->m_disp_scale_bar_text);
			int dig = vrv->m_glview->GetScaleBarDigit();
			if (dig >= 0 && dig <= 8)
				m_scale_digit_cmb->Select(dig);
			bool fixed = false;
			vrv->m_glview->GetScaleBarFixed(fixed, dval, ival);
			m_scale_lenfix_chk->SetValue(fixed);
			if (scale_check)
			{
				m_scale_te_chk->Enable();
				if (!fixed)
				{
					m_scale_text->Enable();
					m_scale_cmb->Enable();
				}
				else
				{
					m_scale_text->Disable();
					m_scale_cmb->Disable();
				}
			}
			else
			{
				m_scale_te_chk->Disable();
				m_scale_text->Disable();
				m_scale_cmb->Disable();
			}
		}
	}

	//legend
	m_legend_chk->SetValue(m_vd->GetLegend());

	//mask
	int mask_hide_mode = m_vd->GetMaskHideMode();
	m_mask_outside_chk->SetValue(false);
	m_mask_inside_chk->SetValue(false);
	if (mask_hide_mode == VOL_MASK_HIDE_OUTSIDE)
		m_mask_outside_chk->SetValue(true);
	if (mask_hide_mode == VOL_MASK_HIDE_INSIDE)
		m_mask_inside_chk->SetValue(true);

	//sync group
	if (m_group)
		m_sync_group = m_group->GetVolumeSyncProp();
	m_sync_group_chk->SetValue(m_sync_group);
	if (m_group)
		m_sync_group_spc = m_group->GetVolumeSyncSpc();
	m_sync_g_spc_chk->SetValue(m_sync_group_spc);

	//colormap
	double low, high;
	m_vd->GetColormapValues(low, high);
	//low
	if (!m_is_float)
	{
		m_colormap_low_value_text->SetValidator(vald_int);
		if ((vald_i = (wxIntegerValidator<unsigned int>*)m_colormap_low_value_text->GetValidator()))
			vald_i->SetRange(0, int(m_max_val));
		ival = int(low * m_max_val + 0.5);
		m_colormap_low_value_sldr->SetRange(0, int(sldr_max));
		str = wxString::Format("%d", ival);
		m_colormap_low_value_sldr->SetValue(ival);
		m_colormap_low_value_text->ChangeValue(str);
	}
	else
	{
		m_colormap_low_value_text->SetValidator(vald_fp2);
		if ((vald_fp = (wxFloatingPointValidator<double>*)m_colormap_low_value_text->GetValidator()))
			vald_fp->SetRange(0, m_max_val);
		m_colormap_low_value_sldr->SetRange(0, sldr_max);
		str = wxString::Format("%.2f", low * m_max_val);
		m_colormap_low_value_sldr->SetValue((int)(low * m_max_val * 100.0 + 0.5));
		m_colormap_low_value_text->ChangeValue(str);
	}
	//high
	if (!m_is_float)
	{
		m_colormap_high_value_text->SetValidator(vald_int);
		if ((vald_i = (wxIntegerValidator<unsigned int>*)m_colormap_high_value_text->GetValidator()))
			vald_i->SetRange(0, int(m_max_val));
		ival = int(high * m_max_val + 0.5);
		m_colormap_high_value_sldr->SetRange(0, int(sldr_max));
		str = wxString::Format("%d", ival);
		m_colormap_high_value_sldr->SetValue(ival);
		m_colormap_high_value_text->ChangeValue(str);
	}
	else
	{
		m_colormap_high_value_text->SetValidator(vald_fp2);
		if ((vald_fp = (wxFloatingPointValidator<double>*)m_colormap_high_value_text->GetValidator()))
			vald_fp->SetRange(0, m_max_val);
		m_colormap_high_value_sldr->SetRange(0, sldr_max);
		str = wxString::Format("%.2f", high * m_max_val);
		m_colormap_high_value_sldr->SetValue((int)(high* m_max_val * 100.0 + 0.5));
		m_colormap_high_value_text->ChangeValue(str);
	}
	//mode
	if (m_vd->GetColormapMode() == 1)
		m_colormap_enable_chk->SetValue(true);
	else
		m_colormap_enable_chk->SetValue(false);

	//inversion
	bool inv = m_vd->GetInvert();
	m_inv_chk->SetValue(inv);

	//MIP
	int mode = m_vd->GetMode();
	if (mode == 1)
	{
		m_mip_chk->SetValue(true);
		m_alpha_sldr->Disable();
		m_alpha_text->Disable();
		m_right_thresh_sldr->Disable();
		m_right_thresh_text->Disable();
		m_boundary_sldr->Disable();
		m_boundary_text->Disable();
		m_luminance_sldr->Disable();
		m_luminance_text->Disable();
		if (m_vd->GetColormapMode() == 1)
		{
			m_gamma_sldr->Disable();
			m_gamma_text->Disable();
			m_contrast_sldr->Disable();
			m_contrast_text->Disable();
		}
		else
		{
			m_gamma_sldr->Enable();
			m_gamma_text->Enable();
			m_contrast_sldr->Enable();
			m_contrast_text->Enable();
		}
		if (m_threh_st)
			m_threh_st->SetLabel("Shade Threshold:");
	}
	else
	{
		m_mip_chk->SetValue(false);
		if (alpha)
		{
			m_alpha_sldr->Enable();
			m_alpha_text->Enable();
		}
		m_right_thresh_sldr->Enable();
		m_right_thresh_text->Enable();
		m_boundary_sldr->Enable();
		m_boundary_text->Enable();
		m_gamma_sldr->Enable();
		m_gamma_text->Enable();
		m_contrast_sldr->Enable();
		m_contrast_text->Enable();
		m_luminance_sldr->Enable();
		m_luminance_text->Enable();
		if (m_threh_st)
			m_threh_st->SetLabel("Threshold:");
	}

	//Indexed color
	if (m_vd->GetColormapMode() == 3)
	{
		m_idcl_chk->SetValue(true);
		m_colormap_enable_chk->Disable();
		m_colormap_high_value_text->Disable();
		m_colormap_high_value_sldr->Disable();
		m_colormap_low_value_text->Disable();
		m_colormap_low_value_sldr->Disable();
	}
	else
	{
		m_idcl_chk->SetValue(false);
		m_colormap_enable_chk->Enable();
		m_colormap_high_value_text->Enable();
		m_colormap_high_value_sldr->Enable();
		m_colormap_low_value_text->Enable();
		m_colormap_low_value_sldr->Enable();
	}

	//noise reduction
	bool nr = m_vd->GetNR();
	m_nr_chk->SetValue(nr);

	//blend mode
	int blend_mode = m_vd->GetBlendMode();
	if (blend_mode == 2)
		m_depth_chk->SetValue(true);
	else
		m_depth_chk->SetValue(false);

	SetEvtHandlerEnabled(true);

	UpdateUIsROI();

	Layout();
}

void VPropView::SetVolumeData(VolumeData* vd)
{
	m_vd = vd;
	GetSettings();
}

VolumeData* VPropView::GetVolumeData()
{
	return m_vd;
}

void VPropView::RefreshVRenderViews(bool tree, bool interactive)
{
	VRenderFrame* vrender_frame = (VRenderFrame*)m_frame;
	if (vrender_frame)
		vrender_frame->RefreshVRenderViews(tree, interactive);
}

void VPropView::InitVRenderViews(unsigned int type)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		for (int i=0 ; i<vr_frame->GetViewNum(); i++)
		{
			VRenderView* vrv = vr_frame->GetView(i);
			if (vrv)
			{
				vrv->InitView(type);
			}
		}
	}
}

void VPropView::SetGroup(DataGroup* group)
{
	m_group = group;
	if (m_group)
	{
		m_sync_group = m_group->GetVolumeSyncProp();
		m_sync_group_chk->SetValue(m_sync_group);
	}
}

DataGroup* VPropView::GetGroup()
{
	return m_group;
}

void VPropView::SetView(VRenderView *view)
{
	m_vrv = view;
}

VRenderView* VPropView::GetView()
{
	return m_vrv;
}

void VPropView::SaveROIName()
{
	if (m_vd)
	{
		wxString parent(wxT(""));
		int parent_id = -1;
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (!vr_frame) return;
		TreePanel* tree_panel = vr_frame->GetTree();
		if (!tree_panel) return;
		
		if (m_vd->GetROIName(m_vd->GetEditSelID()).empty())
		{
			DataTreeCtrl* tree_ctrl = tree_panel->GetTreeCtrl();
			if (!tree_ctrl) return;

			wxTreeItemId sel_item = tree_ctrl->GetSelection();
			if (sel_item.IsOk())
			{
				LayerInfo* item_data = (LayerInfo*)tree_ctrl->GetItemData(sel_item);
				if (item_data && item_data->type == 8)
				{
					wxTreeItemId vitem = tree_ctrl->GetParentVolItem(sel_item);
					if (vitem.IsOk() && tree_ctrl->GetItemText(vitem) == m_vd->GetName())
					{
						parent = tree_ctrl->GetItemText(sel_item);
						parent_id = item_data->id;
					}
				}
			}
		}

		wxString name = m_roi_text->GetValue();
		m_vd->SetROIName(name.ToStdWstring(), -1, parent.ToStdWstring());
		
		vr_frame->UpdateTree();
		tree_panel->SelectROI(m_vd, parent_id);
	}
}

void VPropView::ShowUIsROI()
{
	if (m_sizer_sl_righ && m_sizer_r5 && m_sizer_r6)
	{
		SetEvtHandlerEnabled(false);
		Freeze();

		m_sizer_sl_righ->Hide(m_sizer_r5);
		m_sizer_sl_righ->Show(m_sizer_r6);
		if (m_vd->GetEditSelID() < -1)
		{
			m_roi_color_btn->Hide();
			m_roi_text->Show();
			m_roi_st->SetLabel(wxT("Segment Group:"));
		}
		else if (m_vd->GetEditSelID() >= 0) 
		{
			m_roi_color_btn->Show();
			m_roi_text->Show();
			m_roi_st->SetLabel(wxT("Segment:"));
		}
		else
		{
			m_roi_color_btn->Hide();
			m_roi_text->Hide();
			m_roi_st->SetLabel(wxT("Segment:"));
		}
		Layout();

		Thaw();
		SetEvtHandlerEnabled(true);
	}
}

void VPropView::HideUIsROI()
{
	if (m_sizer_sl_righ && m_sizer_r5 && m_sizer_r6)
	{
		SetEvtHandlerEnabled(false);
		Freeze();

		m_sizer_sl_righ->Hide(m_sizer_r6);
		m_sizer_sl_righ->Show(m_sizer_r5);
		Layout();

		Thaw();
		SetEvtHandlerEnabled(true);
	}
}

void VPropView::UpdateUIsROI()
{
	if (!m_vd) return;

	if (m_vd->GetColormapMode() == 3)
	{
		SetROIindex(m_vd->GetEditSelID());
		m_roi_disp_mode_combo->SetSelection(m_vd->GetIDColDispMode());
		ShowUIsROI();
	}
	else 
		HideUIsROI();
}

void VPropView::SetROIindex(int id)
{
	if (!m_vd) return; 

	unsigned char r = 0, g = 0, b = 0;
	
	m_roi_id = id;
	m_roi_text->SetValue(wxT(""));

	if (m_roi_id > 0)
	{
		m_vd->GetIDColor(r, g, b);

		wxColor wxc(r, g, b);
		m_roi_color_btn->SetColour(wxc);

		wxString inistr = wxT("Segment ") + wxString::Format("%d", m_roi_id);
		m_roi_text->SetHint(inistr);
	}

	m_roi_text->SetValue(m_vd->GetROIName());
}

void VPropView::UpdateMaxValue()
{
	wxString str = m_max_text->GetValue();
	double val = 0.0;
	str.ToDouble(&val);

	//set max value
	if (m_vd && m_vd->GetTexture() && m_vd->GetTexture()->get_nrrd(0) && val > 0.0) {
		m_vd->SetMaxValue(val);
		double scalar_scale = 1.0;
		if (m_vd->GetTexture()->get_nrrd(0)->getNrrdDataType() == nrrdTypeUShort)
			scalar_scale = 65535.0 / val;
		else if (m_vd->GetTexture()->get_nrrd(0)->getNrrdDataType() == nrrdTypeFloat || m_vd->GetTexture()->get_nrrd(0)->getNrrdDataType() == nrrdTypeDouble)
			scalar_scale = 1.0 / val;
		m_vd->SetScalarScale(scalar_scale);

		GetSettings();
	}

	RefreshVRenderViews(false, true);
}

void VPropView::OnEnterInMaxText(wxCommandEvent& event)
{
	UpdateMaxValue();
}

//1
void VPropView::OnGammaChange(wxScrollEvent & event)
{
	double val = (double)event.GetPosition() / 100.0;
	wxString str = wxString::Format("%.2f", val);
	m_gamma_text->SetValue(str);
}

void VPropView::OnGammaText(wxCommandEvent& event)
{
	wxString str = m_gamma_text->GetValue();
	double val = 0.0;
	str.ToDouble(&val);
	int ival = int(val*100.0+0.5);
	m_gamma_sldr->SetValue(ival);

	//set gamma value
	if (m_sync_group && m_group)
		m_group->Set3DGamma(val);
	else if (m_vd)
		m_vd->Set3DGamma(val);

	RefreshVRenderViews(false, true);
}

void VPropView::OnBoundaryChange(wxScrollEvent & event)
{
	double val = (double)event.GetPosition() / 2000.0;
	wxString str = wxString::Format("%.4f", val);
	m_boundary_text->SetValue(str);
}

void VPropView::OnBoundaryText(wxCommandEvent& event)
{
	wxString str = m_boundary_text->GetValue();
	double val = 0.0;
	str.ToDouble(&val);
	int ival = int(val*2000.0+0.5);
	m_boundary_sldr->SetValue(ival);

	//set boundary value
	if (m_sync_group && m_group)
		m_group->SetBoundary(val);
	else if (m_vd)
		m_vd->SetBoundary(val);

	RefreshVRenderViews(false, true);
}

//2
void VPropView::OnContrastChange(wxScrollEvent & event)
{
	int ival = event.GetPosition();
	if (m_is_float)
	{
		wxString str = wxString::Format("%.2f", ival * 0.01);
		m_contrast_text->SetValue(str);
	}
	else
	{
		wxString str = wxString::Format("%d", ival);
		m_contrast_text->SetValue(str);
	}
}

void VPropView::OnContrastText(wxCommandEvent& event)
{
	wxString str = m_contrast_text->GetValue();
	double val = 0.0;
	if (m_is_float)
	{
		str.ToDouble(&val);
		long ival = (long)(val * 100.0 + 0.5);
		val = val / m_max_val;
		m_contrast_sldr->SetValue(ival);
	}
	else
	{
		long ival = 0;
		str.ToLong(&ival);
		val = double(ival) / m_max_val;
		m_contrast_sldr->SetValue(ival);
	}

	//set contrast value
	if (m_sync_group && m_group)
		m_group->SetOffset(val);
	else if (m_vd)
		m_vd->SetOffset(val);

	RefreshVRenderViews(false, true);
}

void VPropView::OnLeftThreshChange(wxScrollEvent &event)
{
	int ival = event.GetPosition();
	if (m_is_float)
	{
		wxString str = wxString::Format("%.2f", ival * 0.01);
		m_left_thresh_text->SetValue(str);
	}
	else
	{
		wxString str = wxString::Format("%d", ival);
		m_left_thresh_text->SetValue(str);
	}
}

void VPropView::OnLeftThreshText(wxCommandEvent &event)
{
	wxString str = m_left_thresh_text->GetValue();
	long ival = 0;
	double val = 0;
	double right_val = 0;
	if (m_is_float)
	{
		str.ToDouble(&val);
		ival = (long)(val * 100.0 + 0.5);
		val = val / m_max_val;
		right_val = (double)m_right_thresh_sldr->GetValue() * 0.01 / m_max_val;
	}
	else
	{
		str.ToLong(&ival);
		val = double(ival) / m_max_val;
		right_val = (double)m_right_thresh_sldr->GetValue() / m_max_val;
	}

	if (val > right_val)
	{
		val = right_val;

		if (m_is_float)
		{
			ival = int(val * m_max_val * 100.0 + 0.5);
			wxString str = wxString::Format("%.2f", val * m_max_val);
			m_left_thresh_text->SetValue(str);
		}
		else
		{
			ival = int(val * m_max_val + 0.5);
			wxString str2 = wxString::Format("%d", ival);
			m_left_thresh_text->ChangeValue(str2);
		}
	}
	m_left_thresh_sldr->SetValue(ival);

	//set left threshold value
	if (m_sync_group && m_group)
		m_group->SetLeftThresh(val);
	else if (m_vd)
		m_vd->SetLeftThresh(val);

	RefreshVRenderViews(false, true);
}

void VPropView::OnRightThreshChange(wxScrollEvent & event)
{
	int ival = event.GetPosition();
	int ival2 = m_left_thresh_sldr->GetValue();

	if (ival < ival2)
	{
		ival = ival2;
		m_right_thresh_sldr->SetValue(ival);
	}

	if (m_is_float)
	{
		wxString str = wxString::Format("%.2f", ival * 0.01);
		m_right_thresh_text->SetValue(str);
	}
	else
	{
		wxString str = wxString::Format("%d", ival);
		m_right_thresh_text->SetValue(str);
	}
}

void VPropView::OnRightThreshText(wxCommandEvent &event)
{
	wxString str = m_right_thresh_text->GetValue();
	long ival = 0;
	double val = 0;
	double left_val = 0;
	
	if (m_is_float)
	{
		str.ToDouble(&val);
		ival = (long)(val * 100.0 + 0.5);
		val = val / m_max_val;
		left_val = (double)m_left_thresh_sldr->GetValue() * 0.01 / m_max_val;
	}
	else
	{
		str.ToLong(&ival);
		val = double(ival) / m_max_val;
		left_val = (double)m_left_thresh_sldr->GetValue() / m_max_val;
	}
	

	if (val >= left_val)
	{
		m_right_thresh_sldr->SetValue(ival);

		//set right threshold value
		if (m_sync_group && m_group)
			m_group->SetRightThresh(val);
		else if (m_vd)
			m_vd->SetRightThresh(val);

		RefreshVRenderViews(false, true);
	}
}

//3
void VPropView::OnLuminanceChange(wxScrollEvent &event)
{
	int ival = event.GetPosition();

	if (m_is_float)
	{
		wxString str = wxString::Format("%.2f", ival * 0.01);
		m_luminance_text->SetValue(str);
	}
	else
	{
		wxString str = wxString::Format("%d", ival);
		m_luminance_text->SetValue(str);
	}
}

void VPropView::OnLuminanceText(wxCommandEvent &event)
{
	wxString str = m_luminance_text->GetValue();
	long ival = 0;
	double val = 0;

	if (m_is_float)
	{
		str.ToDouble(&val);
		long ival = (long)(val * 100.0 + 0.5);
		val = val / m_max_val;
		m_luminance_sldr->SetValue(ival);
	}
	else
	{
		str.ToLong(&ival);
		val = double(ival) / m_max_val;
		m_luminance_sldr->SetValue(ival);
	}

	if (m_sync_group && m_group)
		m_group->SetLuminance(val);
	else if (m_vd)
		m_vd->SetLuminance(val);

	if (m_vd)
	{
		Color color = m_vd->GetColor();
		wxColor wxc((unsigned char)(color.r()*255),
			(unsigned char)(color.g()*255),
			(unsigned char)(color.b()*255));
		m_color_text->ChangeValue(wxString::Format("%d , %d , %d",
			wxc.Red(), wxc.Green(), wxc.Blue()));
		m_color_btn->SetBackgroundColour(wxc);
		m_lumi_change = true;
	}

	RefreshVRenderViews(true, true);
}

//shadow
void VPropView::OnShadowEnable(wxCommandEvent &event)
{
	bool shadow = m_shadow_chk->GetValue();

	if (m_vrv && m_vrv->GetVolMethod()==VOL_METHOD_MULTI)
	{
		for (int i=0; i<m_vrv->GetAllVolumeNum(); i++)
		{
			VolumeData* vd = m_vrv->GetAllVolumeData(i);
			if (vd)
				vd->SetShadow(shadow);
		}
	}
	else
	{
		if (m_sync_group && m_group)
			m_group->SetShadow(shadow);
		else if (m_group && m_group->GetBlendMode()==2)
			m_vd->SetShadow(shadow);
		else if (m_vd)
			m_vd->SetShadow(shadow);
	}

	RefreshVRenderViews();
}

void VPropView::OnShadowChange(wxScrollEvent &event)
{
	double val = (double)event.GetPosition() / 100.0;
	wxString str = wxString::Format("%.2f", val);
	m_shadow_text->SetValue(str);
}

void VPropView::OnShadowText(wxCommandEvent &event)
{
	wxString str = m_shadow_text->GetValue();
	double val = 0.0;
	str.ToDouble(&val);
	m_shadow_sldr->SetValue(int(val*100.0+0.5));

	//set shadow darkness
	if (m_vrv && m_vrv->GetVolMethod()==VOL_METHOD_MULTI)
	{
		for (int i=0; i<m_vrv->GetAllVolumeNum(); i++)
		{
			VolumeData* vd = m_vrv->GetAllVolumeData(i);
			if (vd)
				vd->SetShadowParams(val);
		}
	}
	else
	{
		if (m_sync_group && m_group)
			m_group->SetShadowParams(val);
		else if (m_group && m_group->GetBlendMode()==2)
			m_group->SetShadowParams(val);
		else if (m_vd)
			m_vd->SetShadowParams(val);
	}

	RefreshVRenderViews(false, true);
}

void VPropView::OnHiShadingChange(wxScrollEvent &event)
{
	double val = (double)event.GetPosition() / 10.0;
	wxString str = wxString::Format("%.2f", val);
	m_hi_shading_text->SetValue(str);
}

void VPropView::OnHiShadingText(wxCommandEvent &event)
{
	wxString str = m_hi_shading_text->GetValue();
	double val = 0.0;
	str.ToDouble(&val);
	m_hi_shading_sldr->SetValue(int(val*10.0+0.5));

	//set high shading value
	if (m_sync_group && m_group)
		m_group->SetHiShading(val);
	else if (m_vd)
		m_vd->SetHiShading(val);

	RefreshVRenderViews(false, true);
}

//4
void VPropView::OnAlphaCheck(wxCommandEvent &event)
{
	bool alpha = m_alpha_chk->GetValue();

	if (alpha)
	{
		if (m_vd && m_vd->GetMode()==0)
		{
			m_alpha_sldr->Enable();
			m_alpha_text->Enable();
		}
		//shading
		if (m_vd->GetShading())
		{
			m_low_shading_sldr->Enable();
			m_low_shading_text->Enable();
			m_shading_enable_chk->SetValue(true);
			m_vd->GetVR()->set_shading(true);
		}
	}
	else
	{
		m_alpha_sldr->Disable();
		m_alpha_text->Disable();
		//shading
		if (m_vd->GetShading())
		{
			m_low_shading_sldr->Disable();
			m_low_shading_text->Disable();
			m_shading_enable_chk->SetValue(false);
			m_vd->GetVR()->set_shading(false);
		}
	}

	if (m_sync_group && m_group)
		m_group->SetEnableAlpha(alpha);
	else if (m_vd)
		m_vd->SetEnableAlpha(alpha);

	RefreshVRenderViews();
}

void VPropView::OnAlphaChange(wxScrollEvent &event)
{
	int ival = event.GetPosition();
	if (m_is_float)
	{
		wxString str = wxString::Format("%.2f", ival * 0.01);
		m_alpha_text->SetValue(str);
	}
	else
	{
		wxString str = wxString::Format("%d", ival);
		m_alpha_text->SetValue(str);
	}
}

void VPropView::OnAlphaText(wxCommandEvent& event)
{
	wxString str = m_alpha_text->GetValue();
	long ival = 0;
	double val = 0;

	if (m_is_float)
	{
		str.ToDouble(&val);
		long ival = (long)(val * 100.0 + 0.5);
		val = val / m_max_val;
		m_alpha_sldr->SetValue(ival);
	}
	else
	{
		str.ToLong(&ival);
		val = double(ival) / m_max_val;
		m_alpha_sldr->SetValue(ival);
	}

	//set alpha value
	if (m_sync_group && m_group)
		m_group->SetAlpha(val);
	else if (m_vd)
		m_vd->SetAlpha(val);

	RefreshVRenderViews(false, true);
}

void VPropView::OnSampleChange(wxScrollEvent & event)
{
	double val = event.GetPosition() / 100.0;
	wxString str = wxString::Format("%.2f", val);
	m_sample_text->SetValue(str);
}

void VPropView::OnSampleText(wxCommandEvent& event)
{
	wxString str = m_sample_text->GetValue();
	double srate = 0.0;
	str.ToDouble(&srate);
	double val = srate*100.0;
	m_sample_sldr->SetValue(int(val));

	//set sample rate value
	if (m_vrv && m_vrv->GetVolMethod()==VOL_METHOD_MULTI)
	{
		for (int i=0; i<m_vrv->GetAllVolumeNum(); i++)
		{
			VolumeData* vd = m_vrv->GetAllVolumeData(i);
			if (vd)
				vd->SetSampleRate(srate);
		}
	}
	else
	{
		if (m_sync_group && m_group)
			m_group->SetSampleRate(srate);
		else if (m_group && m_group->GetBlendMode()==2)
			m_group->SetSampleRate(srate);
		else if (m_vd)
			m_vd->SetSampleRate(srate);
	}

	RefreshVRenderViews(false, true);
}

//5
void VPropView::OnLowShadingChange(wxScrollEvent &event)
{
	double val = (double)event.GetPosition() / 100.0;
	wxString str = wxString::Format("%.2f", val);
	m_low_shading_text->SetValue(str);
}

void VPropView::OnLowShadingText(wxCommandEvent &event)
{
	wxString str = m_low_shading_text->GetValue();
	double val = 0.0;
	str.ToDouble(&val);
	m_low_shading_sldr->SetValue(int(val*100.0+0.5));

	//set low shading value
	if (m_sync_group && m_group)
		m_group->SetLowShading(val);
	else if (m_vd)
		m_vd->SetLowShading(val);

	RefreshVRenderViews(false, true);
}

void VPropView::OnShadingEnable(wxCommandEvent &event)
{
	bool shading = m_shading_enable_chk->GetValue();

	if (m_sync_group && m_group)
		m_group->SetShading(shading);
	else if (m_vd)
		m_vd->SetShading(shading);

	if (shading)
	{
		if (m_vd && m_vd->GetEnableAlpha())
		{
			m_low_shading_sldr->Enable();
			m_low_shading_text->Enable();
		}
	}
	else
	{
		m_low_shading_sldr->Disable();
		m_low_shading_text->Disable();
	}

	RefreshVRenderViews();
}

//colormap controls
void VPropView::OnEnableColormap(wxCommandEvent &event)
{
	bool colormap = m_colormap_enable_chk->GetValue();

	if (m_sync_group && m_group)
	{
		m_group->SetColormapMode(colormap?1:0);
		m_group->SetColormapDisp(colormap);
	}
	else if (m_vd)
	{
		m_vd->SetColormapMode(colormap?1:0);
		m_vd->SetColormapDisp(colormap);
	}

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		AdjustView *adjust_view = vr_frame->GetAdjustView();
		if (adjust_view)
			adjust_view->UpdateSync();
	}

	if (m_vd && m_vd->GetMode()==1)
	{
		if (colormap)
		{
			m_gamma_sldr->Disable();
			m_gamma_text->Disable();
			m_contrast_sldr->Disable();
			m_contrast_text->Disable();
			m_luminance_sldr->Disable();
			m_luminance_text->Disable();
		}
		else
		{
			m_gamma_sldr->Enable();
			m_gamma_text->Enable();
			m_contrast_sldr->Enable();
			m_contrast_text->Enable();
			//m_luminance_sldr->Enable();
			//m_luminance_text->Enable();
		}
	}

	RefreshVRenderViews();
}

void VPropView::OnColormapHighValueChange(wxScrollEvent &event)
{
	int iVal = m_colormap_high_value_sldr->GetValue();
	int iVal2 = m_colormap_low_value_sldr->GetValue();

	if (iVal < iVal2)
	{
		iVal = iVal2;
		m_colormap_high_value_sldr->SetValue(iVal);
	}

	if (m_is_float)
	{
		wxString str = wxString::Format("%.2f", iVal * 0.01);
		m_colormap_high_value_text->SetValue(str);
	}
	else
	{
		wxString str = wxString::Format("%d", iVal);
		m_colormap_high_value_text->SetValue(str);
	}
	
}

void VPropView::OnColormapHighValueText(wxCommandEvent &event)
{
	wxString str = m_colormap_high_value_text->GetValue();
	long iVal = 0;
	long iVal2 = 0;
	double val = 0;

	if (m_is_float)
	{
		str.ToDouble(&val);
		iVal = (long)(val * 100.0 + 0.5);
		val = val / m_max_val;
		iVal2 = m_colormap_low_value_sldr->GetValue() * 0.01;
	}
	else
	{
		str.ToLong(&iVal);
		iVal2 = m_colormap_low_value_sldr->GetValue();
		val = double(iVal) / m_max_val;
	}

	if (iVal >= iVal2)
	{
		m_colormap_high_value_sldr->SetValue(iVal);

		if (m_sync_group && m_group)
			m_group->SetColormapValues(-1, val);
		else if (m_vd)
		{
			double low, high;
			m_vd->GetColormapValues(low, high);
			m_vd->SetColormapValues(low, val);
		}

		RefreshVRenderViews(false, true);
	}
}

void VPropView::OnColormapLowValueChange(wxScrollEvent &event)
{
	int iVal = m_colormap_low_value_sldr->GetValue();

	if (m_is_float)
	{
		wxString str = wxString::Format("%.2f", iVal * 0.01);
		m_colormap_low_value_text->SetValue(str);
	}
	else
	{
		wxString str = wxString::Format("%d", iVal);
		m_colormap_low_value_text->SetValue(str);
	}
}

void VPropView::OnColormapLowValueText(wxCommandEvent &event)
{
	wxString str = m_colormap_low_value_text->GetValue();
	long iVal = 0;
	long iVal2 = 0;
	double val = 0;

	if (m_is_float)
	{
		str.ToDouble(&val);
		iVal = (long)(val * 100.0 + 0.5);
		val = val / m_max_val;
	}
	else
	{
		str.ToLong(&iVal);
		val = double(iVal) / m_max_val;
	}

	iVal2 = m_colormap_high_value_sldr->GetValue();

	if (iVal > iVal2)
	{
		iVal = iVal2;

		if (m_is_float)
		{
			val = iVal * 0.01;
			str = wxString::Format("%.2f", val);
			m_colormap_low_value_text->ChangeValue(str);
			val = val / m_max_val;
		}
		else
		{
			val = double(iVal) / m_max_val;
			str = wxString::Format("%d", iVal);
			m_colormap_low_value_text->ChangeValue(str);
		}
	}
	m_colormap_low_value_sldr->SetValue(iVal);

	if (m_sync_group && m_group)
		m_group->SetColormapValues(val, -1);
	else if (m_vd)
	{
		double low, high;
		m_vd->GetColormapValues(low, high);
		m_vd->SetColormapValues(val, high);
	}

	RefreshVRenderViews(false, true);
}

void VPropView::OnEnterInROINameText(wxCommandEvent& event)
{
	SaveROIName();
}

void VPropView::OnROIColorBtn(wxColourPickerEvent& event)
{
	wxColor wxc = event.GetColour();

	if (m_vd)
	{
		m_vd->SetIDColor(wxc.Red(), wxc.Green(), wxc.Blue());
		
		RefreshVRenderViews(true);
	}
}

void VPropView::OnROIDispModesCombo(wxCommandEvent &event)
{
	if (m_vd)
	{
		int mode = m_roi_disp_mode_combo->GetCurrentSelection();
		m_vd->SetIDColDispMode(mode);
		
		RefreshVRenderViews();
	}
}

//6
void VPropView::OnColorChange(wxColor c)
{
	Color color(c.Red()/255.0, c.Green()/255.0, c.Blue()/255.0);
	if (m_vd)
	{
		if (m_lumi_change)
		{
			m_vd->SetColor(color, true);
			m_lumi_change = false;
		}
		else
			m_vd->SetColor(color);

		double lum = m_vd->GetLuminance();
		int ilum = int(lum*m_max_val+0.5);
		m_luminance_sldr->SetValue(ilum);
		wxString str = wxString::Format("%d", ilum);
		m_luminance_text->ChangeValue(str);

		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;

		if (vr_frame)
		{
			AdjustView *adjust_view = vr_frame->GetAdjustView();
			if (adjust_view)
				adjust_view->UpdateSync();
		}

		RefreshVRenderViews(true);
	}
}

void VPropView::OnColorTextChange(wxCommandEvent& event)
{
	wxColor wxc;
	int filled = 0;
	wxString str = m_color_text->GetValue();
	if (str == "r" || str == "R")
	{
		wxc = wxColor(255, 0, 0);
		filled = 3;
	}
	else if (str == "g" || str == "G")
	{
		wxc = wxColor(0, 255, 0);
		filled = 3;
	}
	else if (str == "b" || str == "B")
	{
		wxc = wxColor(0, 0, 255);
		filled = 3;
	}
	else if (str == "w" || str == "W")
	{
		wxc = wxColor(255, 255, 255);
		filled = 3;
	}
	else if (str == "p" || str == "P")
	{
		wxc = wxColor(255, 0, 255);
		filled = 3;
	}
	else
	{
		int index = 0;//1-red; 2-green; 3-blue;
		int state = 0;//0-idle; 1-reading digit; 3-finished
		wxString sColor;
		long r = 255;
		long g = 255;
		long b = 255;
		for (unsigned int i=0; i<str.length(); i++)
		{
			wxChar c = str[i];
			if (isdigit(c) || c=='.')
			{
				if (state == 0 || state == 3)
				{
					sColor += c;
					index++;
					state = 1;
				}
				else if (state == 1)
				{
					sColor += c;
				}

				if (i == str.length()-1)  //last one
				{
					switch (index)
					{
					case 1:
						sColor.ToLong(&r);
						filled = 1;
						break;
					case 2:
						sColor.ToLong(&g);
						filled = 2;
						break;
					case 3:
						sColor.ToLong(&b);
						filled = 3;
						break;
					}
				}
			}
			else
			{
				if (state == 1)
				{
					switch (index)
					{
					case 1:
						sColor.ToLong(&r);
						filled = 1;
						break;
					case 2:
						sColor.ToLong(&g);
						filled = 2;
						break;
					case 3:
						sColor.ToLong(&b);
						filled = 3;
						break;
					}
					state = 3;
					sColor = "";
				}
			}
		}
		wxc = wxColor(Clamp(r,0,255), Clamp(g,0,255), Clamp(b,0,255));
	}

	if (filled == 3)
	{
		wxString new_str = wxString::Format("%d , %d , %d",
			wxc.Red(), wxc.Green(), wxc.Blue());
		if (str != new_str)
			m_color_text->ChangeValue(new_str);
		m_color_btn->SetColour(wxc);

		OnColorChange(wxc);
	}
}

void VPropView::OnColorBtn(wxColourPickerEvent& event)
{
	wxColor wxc = event.GetColour();

	m_color_text->ChangeValue(wxString::Format("%d , %d , %d",
		wxc.Red(), wxc.Green(), wxc.Blue()));

	OnColorChange(wxc);
}

void VPropView::OnColorTextFocus(wxCommandEvent& event)
{
	m_color_text->SetSelection(0, -1);
}

void VPropView::OnInvCheck(wxCommandEvent &event)
{
	bool inv = m_inv_chk->GetValue();
	if (m_sync_group && m_group)
		m_group->SetInvert(inv);
	else if (m_vd)
		m_vd->SetInvert(inv);

	RefreshVRenderViews();
}

void VPropView::OnIDCLCheck(wxCommandEvent &event)
{
	bool idcolor = m_idcl_chk->GetValue();
	bool colormap = m_colormap_enable_chk->GetValue();

	int colormode;

	if (idcolor) colormode = 3;
	else if (colormap) colormode = 1;
	else colormode = 0;

	/*   if (m_sync_group && m_group)
	{
	m_group->SetColormapMode(mode?3:0);
	}
	else*/ if (m_vd)
	{
		m_vd->SetColormapMode(colormode);
	}

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
		AdjustView *adjust_view = vr_frame->GetAdjustView();
		if (adjust_view)
			adjust_view->UpdateSync();
		
		UpdateUIsROI();
		vr_frame->UpdateTree(m_vd->GetName(), 2);
		if (vr_frame->GetTree())
		{
			vr_frame->GetTree()->CollapseDataTreeItem(m_vd->GetName(), true);
			vr_frame->GetTree()->ExpandDataTreeItem(m_vd->GetName());
		}
	}

	if (m_vd)
	{
		if (colormode == 3)
		{
			m_colormap_enable_chk->Disable();
			m_colormap_high_value_text->Disable();
			m_colormap_high_value_sldr->Disable();
			m_colormap_low_value_text->Disable();
			m_colormap_low_value_sldr->Disable();
		}
		else
		{
			m_colormap_enable_chk->Enable();
			m_colormap_high_value_text->Enable();
			m_colormap_high_value_sldr->Enable();
			m_colormap_low_value_text->Enable();
			m_colormap_low_value_sldr->Enable();
		}
	}

	if (m_vd && m_vd->GetMode()==1)
	{
		if (colormode == 1)
		{
			m_gamma_sldr->Disable();
			m_gamma_text->Disable();
			m_contrast_sldr->Disable();
			m_contrast_text->Disable();
			m_luminance_sldr->Disable();
			m_luminance_text->Disable();
		}
		else
		{
			m_gamma_sldr->Enable();
			m_gamma_text->Enable();
			m_contrast_sldr->Enable();
			m_contrast_text->Enable();
			m_luminance_sldr->Enable();
			m_luminance_text->Enable();
		}
	}

	RefreshVRenderViews();
}


void VPropView::OnMIPCheck(wxCommandEvent &event)
{
	int val = m_mip_chk->GetValue()?1:0;

	if (val==1)
	{
		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (vr_frame)
		{
			for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
			{
				VRenderView *vrv = (*vr_frame->GetViewList())[i];
				if (vrv && vrv->GetVolMethod()==VOL_METHOD_MULTI)
				{
					::wxMessageBox("MIP is not supported in Depth mode.");
					m_mip_chk->SetValue(false);
					return;
				}
			}
		}
		m_alpha_sldr->Disable();
		m_alpha_text->Disable();
		m_right_thresh_sldr->Disable();
		m_right_thresh_text->Disable();
		m_boundary_sldr->Disable();
		m_boundary_text->Disable();
		m_luminance_sldr->Disable();
		m_luminance_text->Disable();
		if (m_vd && m_vd->GetColormapMode() == 1)
		{
			m_gamma_sldr->Disable();
			m_gamma_text->Disable();
			m_contrast_sldr->Disable();
			m_contrast_text->Disable();
		}
		if (m_threh_st)
			m_threh_st->SetLabel("Shade Threshold:");
	}
	else
	{
		if (m_vd && m_vd->GetEnableAlpha())
		{
			m_alpha_sldr->Enable();
			m_alpha_text->Enable();
		}
		m_right_thresh_sldr->Enable();
		m_right_thresh_text->Enable();
		m_boundary_sldr->Enable();
		m_boundary_text->Enable();
		m_gamma_sldr->Enable();
		m_gamma_text->Enable();
		m_contrast_sldr->Enable();
		m_contrast_text->Enable();
		m_luminance_sldr->Enable();
		m_luminance_text->Enable();
		if (m_threh_st)
			m_threh_st->SetLabel("Threshold:");
	}

	if (m_sync_group && m_group)
		m_group->SetMode(val);
	else if (m_vd)
		m_vd->SetMode(val);

	Layout();

	RefreshVRenderViews();
}

//noise reduction
void VPropView::OnNRCheck(wxCommandEvent &event)
{
	bool val = m_nr_chk->GetValue();

	if (m_vrv && m_vrv->GetVolMethod()==VOL_METHOD_MULTI)
	{
		for (int i=0; i<m_vrv->GetAllVolumeNum(); i++)
		{
			VolumeData* vd = m_vrv->GetAllVolumeData(i);
			if (vd)
				vd->SetNR(val);
		}
	}
	else
	{
		if (m_sync_group && m_group)
			m_group->SetNR(val);
		else if (m_group && m_group->GetBlendMode()==2)
			m_group->SetNR(val);
		else if (m_vd)
			m_vd->SetNR(val);
	}

	RefreshVRenderViews();
}

//depth mode
void VPropView::OnDepthCheck(wxCommandEvent &event)
{
	bool val = m_depth_chk->GetValue();

	if (val)
	{
		if (m_group)
		{
			m_group->SetBlendMode(2);
			if (m_vd)
			{
				m_group->SetNR(m_vd->GetNR());
				m_group->SetSampleRate(m_vd->GetSampleRate());
				m_group->SetShadow(m_vd->GetShadow());
				double sp;
				m_vd->GetShadowParams(sp);
				m_group->SetShadowParams(sp);
			}
		}
	}
	else
	{
		if (m_group)
			m_group->SetBlendMode(0);
	}

	RefreshVRenderViews();
}

bool VPropView::SetSpacings()
{
	if (!m_space_x_text || !m_space_y_text || !m_space_z_text)
		return false;

	wxString str, str_new;
	double spcx = 0.0;
	double spcy = 0.0;
	double spcz = 0.0;

	str = m_space_x_text->GetValue();
	str.ToDouble(&spcx);
	if (spcx<=0.0)
		return false;

	str = m_space_y_text->GetValue();
	str.ToDouble(&spcy);
	if (spcy<=0.0)
		return false;

	str = m_space_z_text->GetValue();
	str.ToDouble(&spcz);
	if (spcz<=0.0)
		return false;

	if ( spcz / Min(spcx,spcy) >= 500.0)
		return false;

	if ((m_sync_group || m_sync_group_spc) && m_group)
	{
		for (int i=0; i<m_group->GetVolumeNum(); i++)
		{
			if (m_group->GetVolumeData(i)->GetName().Find("skeleton") == -1)
				m_group->GetVolumeData(i)->SetSpacings(spcx, spcy, spcz);
		}
	}
	else if (m_vd)
	{
		m_vd->SetSpacings(spcx, spcy, spcz);
	}
	else return false;
	/*
	wxString v_name;
	if (m_vd)
	v_name = m_vd->GetName();
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (vr_frame)
	{
	for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
	{
	VRenderView *vrv = (*vr_frame->GetViewList())[i];
	if (vrv)
	if (vrv->GetVolumeData(v_name))
	for (int j=0; j<vrv->GetAllVolumeNum(); j++)
	vrv->GetAllVolumeData(j)->SetSpacings(spcx, spcy, spcz);
	}
	}
	*/
	return true;
}

void VPropView::OnSpaceText(wxCommandEvent& event)
{
	if (SetSpacings())
		InitVRenderViews(INIT_BOUNDS|INIT_CENTER);
}

void VPropView::OnScaleCheck(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	if (m_scale_chk->GetValue())
	{
		wxString num_text, unit_text;
		num_text = m_scale_text->GetValue();
		double len;
		num_text.ToDouble(&len);
		switch (m_scale_cmb->GetSelection())
		{
		case 0:
			unit_text = "nm";
			break;
		case 1:
		default:
			unit_text = wxString::Format("%c%c", 181, 'm');
			break;
		case 2:
			unit_text = "mm";
			break;
		}
		for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
		{
			VRenderView *vrv = (*vr_frame->GetViewList())[i];
			if (vrv)
			{
				vrv->SetScaleBarLen(len);
				vrv->SetSBText(unit_text);
				vrv->EnableScaleBar();
				vrv->SetSbUnitSel(m_scale_cmb->GetSelection());
				vrv->FixScaleBarLen(m_scale_lenfix_chk->GetValue());
			}
		}
		m_scale_te_chk->Enable();
		m_scale_lenfix_chk->Enable();
		if (!m_scale_lenfix_chk->GetValue())
		{
			m_scale_text->Enable();
			m_scale_cmb->Enable();
		}
	}
	else
	{
		for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
		{
			VRenderView *vrv = (*vr_frame->GetViewList())[i];
			if (vrv)
			{
				vrv->DisableScaleBar();
			}
		}
		m_scale_te_chk->Disable();
		m_scale_lenfix_chk->Disable();
		m_scale_text->Disable();
		m_scale_cmb->Disable();
	}

	RefreshVRenderViews();
}

void VPropView::OnScaleTextCheck(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	if (m_scale_te_chk->GetValue())
	{
		wxString unit_text;
		switch (m_scale_cmb->GetSelection())
		{
		case 0:
			unit_text = "nm";
			break;
		case 1:
		default:
			unit_text = wxString::Format("%c%c", 181, 'm');
			break;
		case 2:
			unit_text = "mm";
			break;
		}
		for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
		{
			VRenderView *vrv = (*vr_frame->GetViewList())[i];
			if (vrv)
			{
				vrv->SetSBText(unit_text);
				vrv->EnableSBText();
				vrv->SetSbUnitSel(m_scale_cmb->GetSelection());
			}
		}
	}
	else
	{
		for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
		{
			VRenderView *vrv = (*vr_frame->GetViewList())[i];
			if (vrv)
			{
				vrv->DisableSBText();
			}
		}
	}

	RefreshVRenderViews();
}

void VPropView::OnScaleTextEditing(wxCommandEvent& event)
{
	if (!m_scale_text)
		return;
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	wxString num_text, unit_text;
	num_text = m_scale_text->GetValue();
	double len;
	num_text.ToDouble(&len);
	switch (m_scale_cmb->GetSelection())
	{
	case 0:
		unit_text = "nm";
		break;
	case 1:
	default:
		unit_text = wxString::Format("%c%c", 181, 'm');
		break;
	case 2:
		unit_text = "mm";
		break;
	}
	for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
	{
		VRenderView *vrv = (*vr_frame->GetViewList())[i];
		if (vrv)
		{
			vrv->SetScaleBarLen(len);
			vrv->SetSBText(unit_text);
			vrv->SetSbUnitSel(m_scale_cmb->GetSelection());
		}
	}

	RefreshVRenderViews();
}

void VPropView::OnScaleUnitSelected(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	wxString unit_text;
	switch (m_scale_cmb->GetSelection())
	{
	case 0:
		unit_text = "nm";
		break;
	case 1:
	default:
		unit_text = wxString::Format("%c%c", 181, 'm');
		break;
	case 2:
		unit_text = "mm";
		break;
	}
	for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
	{
		VRenderView *vrv = (*vr_frame->GetViewList())[i];
		if (vrv)
		{
			vrv->SetSBText(unit_text);
			vrv->SetSbUnitSel(m_scale_cmb->GetSelection());
		}
	}

	RefreshVRenderViews();
}

void VPropView::OnScaleDigitSelected(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	int digit = m_scale_digit_cmb->GetSelection();
	
	for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
	{
		VRenderView *vrv = (*vr_frame->GetViewList())[i];
		if (vrv)
			vrv->SetScaleBarDigit(digit);
	}

	RefreshVRenderViews();
}

void VPropView::OnScaleLenFixCheck(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	if(m_scale_lenfix_chk->GetValue())
	{
		for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
		{
			VRenderView *vrv = (*vr_frame->GetViewList())[i];
			if (vrv)
				vrv->FixScaleBarLen(true);
		}
		m_scale_text->Disable();
		m_scale_cmb->Disable();
	}
	else
	{
		if ( vr_frame->GetViewList()->size() != 0 && (*vr_frame->GetViewList())[0] )
		{
			VRenderView *vrv = (*vr_frame->GetViewList())[0];
			bool fixed;
			double len;
			int unitid;

			vrv->GetScaleBarFixed(fixed, len, unitid);
			wxString unit_text;
			switch (m_scale_cmb->GetSelection())
			{
			case 0:
				unit_text = "nm";
				break;
			case 1:
			default:
				unit_text = wxString::Format("%c%c", 181, 'm');
				break;
			case 2:
				unit_text = "mm";
				break;
			}

			for (int i=0; i<(int)vr_frame->GetViewList()->size(); i++)
			{
				vrv = (*vr_frame->GetViewList())[i];
				if (vrv)
				{
					vrv->FixScaleBarLen(false);
					vrv->SetScaleBarLen(len);
					vrv->SetSBText(unit_text);
					vrv->SetSbUnitSel(unitid);
					vrv->FixScaleBarLen(m_scale_lenfix_chk->GetValue());
				}
			}

			m_scale_cmb->Select(unitid);
			m_scale_text->ChangeValue(wxString::Format("%.3f", len));
		}
		m_scale_text->Enable();
		m_scale_cmb->Enable();
	}
}

//legend
void VPropView::OnLegendCheck(wxCommandEvent& event)
{
	if (m_vd)
		m_vd->SetLegend(m_legend_chk->GetValue());

	RefreshVRenderViews();
}

//sync within group
void VPropView::OnSyncGroupCheck(wxCommandEvent& event)
{
	m_sync_group = m_sync_group_chk->GetValue();
	m_sync_group_spc = m_sync_g_spc_chk->GetValue();
	if (m_group)
	{
		m_group->SetVolumeSyncProp(m_sync_group);
		m_group->SetVolumeSyncSpc(m_sync_group|m_sync_group_spc);
	}

	if (m_sync_group && m_group)
	{
		wxString str;
		double dVal;
		long iVal;
		bool bVal;

		//gamma
		str = m_gamma_text->GetValue();
		str.ToDouble(&dVal);
		m_group->Set3DGamma(dVal);
		//boundary
		str = m_boundary_text->GetValue();
		str.ToDouble(&dVal);
		m_group->SetBoundary(dVal);
		//contrast
		str = m_contrast_text->GetValue();
		str.ToLong(&iVal);
		dVal = double(iVal) / m_max_val;
		m_group->SetOffset(dVal);
		//left threshold
		str = m_left_thresh_text->GetValue();
		str.ToLong(&iVal);
		dVal = double(iVal) / m_max_val;
		m_group->SetLeftThresh(dVal);
		//right thresh
		str = m_right_thresh_text->GetValue();
		str.ToLong(&iVal);
		dVal = double(iVal) / m_max_val;
		m_group->SetRightThresh(dVal);
		//luminance
		str = m_luminance_text->GetValue();
		str.ToLong(&iVal);
		dVal = double(iVal)/m_max_val;
		m_group->SetLuminance(dVal);
		//shadow
		bVal = m_shadow_chk->GetValue();
		m_group->SetShadow(bVal);
		str = m_shadow_text->GetValue();
		str.ToDouble(&dVal);
		m_group->SetShadowParams(dVal);
		//high shading
		str = m_hi_shading_text->GetValue();
		str.ToDouble(&dVal);
		m_group->SetHiShading(dVal);
		//alpha
		str = m_alpha_text->GetValue();
		str.ToLong(&iVal);
		dVal = double(iVal) / m_max_val;
		m_group->SetAlpha(dVal);
		//sample rate
		str = m_sample_text->GetValue();
		str.ToDouble(&dVal);
		m_group->SetSampleRate(dVal);
		//shading
		bVal = m_shading_enable_chk->GetValue();
		m_group->SetShading(bVal);
		str = m_low_shading_text->GetValue();
		str.ToDouble(&dVal);
		m_group->SetLowShading(dVal);
		//colormap low
		str = m_colormap_low_value_text->GetValue();
		str.ToLong(&iVal);
		dVal = double(iVal)/m_max_val;
		m_group->SetColormapValues(dVal, -1);
		//colormap high
		str = m_colormap_high_value_text->GetValue();
		str.ToLong(&iVal);
		dVal = double(iVal)/m_max_val;
		m_group->SetColormapValues(-1, dVal);
		//inversion
		bVal = m_inv_chk->GetValue();
		m_group->SetInvert(bVal);
		//MIP
		bVal = m_mip_chk->GetValue();
		m_group->SetMode(bVal?1:0);
		//noise reduction
		bVal = m_nr_chk->GetValue();
		m_group->SetNR(bVal);
	}

	if ((m_sync_group_spc|m_sync_group) && m_group)
	{
		SetSpacings();
	}

	RefreshVRenderViews();
}

//sync within group
void VPropView::OnSyncGroupSpcCheck(wxCommandEvent& event)
{
	m_sync_group_spc = m_sync_g_spc_chk->GetValue();
	if (m_group)
		m_group->SetVolumeSyncSpc(m_sync_group_spc);

	if (m_sync_group_spc && m_group)
	{
		SetSpacings();
	}

	RefreshVRenderViews();
}

//hide volume outside of mask
void VPropView::OnMaskHideOutsideCheck(wxCommandEvent& event)
{
	bool outside = m_mask_outside_chk->GetValue();
	bool inside = m_mask_inside_chk->GetValue();
	if (outside)
		m_mask_inside_chk->SetValue(false);
    
    if (m_sync_group && m_group)
    {
        if (outside)
            m_group->SetMaskHideMode(VOL_MASK_HIDE_OUTSIDE);
        else
            m_group->SetMaskHideMode(VOL_MASK_HIDE_NONE);
    }
    else
    {
        if (outside)
            m_vd->SetMaskHideMode(VOL_MASK_HIDE_OUTSIDE);
        else
            m_vd->SetMaskHideMode(VOL_MASK_HIDE_NONE);
    }

	RefreshVRenderViews();
}

//hide volume inside of mask
void VPropView::OnMaskHideInsideCheck(wxCommandEvent& event)
{
	bool outside = m_mask_outside_chk->GetValue();
	bool inside = m_mask_inside_chk->GetValue();
	if (inside)
		m_mask_outside_chk->SetValue(false);

    if (m_sync_group && m_group)
    {
        if (inside)
            m_group->SetMaskHideMode(VOL_MASK_HIDE_INSIDE);
        else
            m_group->SetMaskHideMode(VOL_MASK_HIDE_NONE);
    }
    else
    {
        if (inside)
            m_vd->SetMaskHideMode(VOL_MASK_HIDE_INSIDE);
        else
            m_vd->SetMaskHideMode(VOL_MASK_HIDE_NONE);
    }

	RefreshVRenderViews();
}

void VPropView::OnSaveDefault(wxCommandEvent& event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	DataManager *mgr = vr_frame->GetDataManager();
	if (!mgr)
		return;

	wxFileConfig fconfig("FluoRender default volume settings");
	wxString str;
	double val;
	//extract boundary
	str = m_boundary_text->GetValue();
	str.ToDouble(&val);
	fconfig.Write("extract_boundary", val);
	mgr->m_vol_exb = val;
	//gamma
	str = m_gamma_text->GetValue();
	str.ToDouble(&val);
	fconfig.Write("gamma", val);
	mgr->m_vol_gam = val;
	//low offset
	str = m_contrast_text->GetValue();
	str.ToDouble(&val);
	val /= m_max_val;
	fconfig.Write("low_offset", val);
	mgr->m_vol_of1 = val;
	//high offset
	val = 1.0;
	fconfig.Write("high_offset", val);
	mgr->m_vol_of2 = val;
	//low thresholding
	str = m_left_thresh_text->GetValue();
	str.ToDouble(&val);
	val /= m_max_val;
	fconfig.Write("low_thresholding", val);
	mgr->m_vol_lth = val;
	//high thresholding
	str = m_right_thresh_text->GetValue();
	str.ToDouble(&val);
	val /= m_max_val;
	fconfig.Write("high_thresholding", val);
	mgr->m_vol_hth = val;
	//low shading
	str = m_low_shading_text->GetValue();
	str.ToDouble(&val);
	fconfig.Write("low_shading", val);
	mgr->m_vol_lsh = val;
	//high shading
	str = m_hi_shading_text->GetValue();
	str.ToDouble(&val);
	fconfig.Write("high_shading", val);
	mgr->m_vol_hsh = val;
	//alpha
	str = m_alpha_text->GetValue();
	str.ToDouble(&val);
	val /= m_max_val;
	fconfig.Write("alpha", val);
	mgr->m_vol_alf = val;
	//sample rate
	str = m_sample_text->GetValue();
	str.ToDouble(&val);
	fconfig.Write("sample_rate", val);
	mgr->m_vol_spr = val;
	//x spacing
	str = m_space_x_text->GetValue();
	str.ToDouble(&val);
	fconfig.Write("x_spacing", val);
	mgr->m_vol_xsp = val;
	//y spacing
	str = m_space_y_text->GetValue();
	str.ToDouble(&val);
	fconfig.Write("y_spacing", val);
	mgr->m_vol_ysp = val;
	//z spacing
	str = m_space_z_text->GetValue();
	str.ToDouble(&val);
	fconfig.Write("z_spacing", val);
	mgr->m_vol_zsp = val;
	//luminance
	str = m_luminance_text->GetValue();
	str.ToDouble(&val);
	val /= m_max_val;
	fconfig.Write("luminance", val);
	mgr->m_vol_lum = val;
	//colormap low value
	str = m_colormap_low_value_text->GetValue();
	str.ToDouble(&val);
	val /= m_max_val;
	fconfig.Write("colormap_low", val);
	mgr->m_vol_lcm = val;
	//colormap high value
	str = m_colormap_high_value_text->GetValue();
	str.ToDouble(&val);
	val /= m_max_val;
	fconfig.Write("colormap_hi", val);
	mgr->m_vol_hcm = val;
	//alpha
	bool alpha = m_alpha_chk->GetValue();
	fconfig.Write("enable_alpha", alpha);
	mgr->m_vol_eap = alpha;
	//enable shading
	bool shading = m_shading_enable_chk->GetValue();
	fconfig.Write("enable_shading", shading);
	mgr->m_vol_esh = shading;
	//inversion
	bool inv = m_inv_chk->GetValue();
	fconfig.Write("enable_inv", inv);
	mgr->m_vol_inv = inv;
	//enable mip
	bool mip = m_mip_chk->GetValue();
	fconfig.Write("enable_mip", mip);
	mgr->m_vol_mip = mip;
	//noise reduction
	bool nrd = m_nr_chk->GetValue();
	fconfig.Write("noise_rd", nrd);
	mgr->m_vol_nrd = nrd;
	//shadow
	bool shw = m_shadow_chk->GetValue();
	fconfig.Write("enable_shadow", shw);
	mgr->m_vol_shw = shw;
	//shadow intensity
	str = m_shadow_text->GetValue();
	str.ToDouble(&val);
	double swi = val;
	fconfig.Write("shadow_intensity", swi);
	mgr->m_vol_swi = swi;

	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/default_volume_settings.dft";
#else
	wxString dft = expath + GETSLASHS() + "default_volume_settings.dft";
	wxString dft2 = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "default_volume_settings.dft";
	if (!wxFileExists(dft) && wxFileExists(dft2))
		dft = dft2;
#endif

	wxFileOutputStream os(dft);
	fconfig.Save(os);
}

void VPropView::OnResetDefault(wxCommandEvent &event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	DataManager *mgr = vr_frame->GetDataManager();
	if (!mgr)
		return;
	if (!m_vd)
		return;

	wxString str;
	double dval;
	int ival;
	bool bval;

	//gamma
	dval = mgr->m_vol_gam;
	str = wxString::Format("%.2f", dval);
	m_gamma_text->ChangeValue(str);
	ival = int(dval*100.0+0.5);
	m_gamma_sldr->SetValue(ival);
	m_vd->Set3DGamma(dval);
	//extract boundary
	dval = mgr->m_vol_exb;
	str = wxString::Format("%.4f", dval);
	m_boundary_text->ChangeValue(str);
	ival = int(dval*2000.0+0.5);
	m_boundary_sldr->SetValue(ival);
	m_vd->SetBoundary(dval);
	//low offset
	dval = mgr->m_vol_of1;
	ival = int(dval*m_max_val+0.5);
	str = wxString::Format("%d", ival);
	m_contrast_text->ChangeValue(str);
	m_contrast_sldr->SetValue(ival);
	m_vd->SetOffset(dval);
	//low thresholding
	dval = mgr->m_vol_lth;
	ival = int(dval*m_max_val+0.5);
	str = wxString::Format("%d", ival);
	m_left_thresh_text->ChangeValue(str);
	m_left_thresh_sldr->SetValue(ival);
	m_vd->SetLeftThresh(dval);
	//high thresholding
	dval = mgr->m_vol_hth;
	ival = int(dval*m_max_val+0.5);
	str = wxString::Format("%d", ival);
	m_right_thresh_text->ChangeValue(str);
	m_right_thresh_sldr->SetValue(ival);
	m_vd->SetRightThresh(dval);
	//low shading
	dval = mgr->m_vol_lsh;
	str = wxString::Format("%.2f", dval);
	m_low_shading_text->ChangeValue(str);
	ival = int(dval*100.0+0.5);
	m_low_shading_sldr->SetValue(ival);
	double amb, diff, spec, shine;
	m_vd->GetMaterial(amb, diff, spec, shine);
	m_vd->SetMaterial(dval, diff, spec, shine);
	//high shading
	dval = mgr->m_vol_hsh;
	str = wxString::Format("%.2f", dval);
	m_hi_shading_text->ChangeValue(str);
	ival = int(dval*10.0+0.5);
	m_hi_shading_sldr->SetValue(ival);
	m_vd->GetMaterial(amb, diff, spec, shine);
	m_vd->SetMaterial(amb, diff, spec, dval);
	//alpha
	dval = mgr->m_vol_alf;
	ival = int(dval*m_max_val+0.5);
	str = wxString::Format("%d", ival);
	m_alpha_text->ChangeValue(str);
	m_alpha_sldr->SetValue(ival);
	m_vd->SetAlpha(dval);
	//sample rate
	dval = mgr->m_vol_spr;
	str = wxString::Format("%.2f", dval);
	m_sample_text->ChangeValue(str);
	ival = int(dval*100.0+0.5);
	m_sample_sldr->SetValue(ival);
	m_vd->SetSampleRate(dval);
	////x spacing
	//dval = mgr->m_vol_xsp;
	//str = wxString::Format("%.3f", dval);
	//m_space_x_text->ChangeValue(str);
	////y spacing
	//dval = mgr->m_vol_ysp;
	//str = wxString::Format("%.3f", dval);
	//m_space_y_text->ChangeValue(str);
	////z spacing
	//dval = mgr->m_vol_zsp;
	//str = wxString::Format("%.3f", dval);
	//m_space_z_text->ChangeValue(str);
	//SetSpacings();
	//luminance
	dval = mgr->m_vol_lum;
	ival = int(dval*m_max_val+0.5);
	str = wxString::Format("%d", ival);
	m_luminance_text->ChangeValue(str);
	m_luminance_sldr->SetValue(ival);
	double h, s, v;
	m_vd->GetHSV(h, s, v);
	HSVColor hsv(h, s, dval);
	Color color(hsv);
	m_vd->SetColor(color);
	wxColor wxc((unsigned char)(color.r()*255),
		(unsigned char)(color.g()*255),
		(unsigned char)(color.b()*255));
	m_color_text->ChangeValue(wxString::Format("%d , %d , %d",
		wxc.Red(), wxc.Green(), wxc.Blue()));
	m_color_btn->SetColour(wxc);
	//colormap low value
	dval = mgr->m_vol_lcm;
	ival = int(dval*m_max_val+0.5);
	str = wxString::Format("%d", ival);
	m_colormap_low_value_text->ChangeValue(str);
	m_colormap_low_value_sldr->SetValue(ival);
	double lcm = dval;
	dval = mgr->m_vol_hcm;
	ival = int(dval*m_max_val+0.5);
	str = wxString::Format("%d", ival);
	m_colormap_high_value_text->ChangeValue(str);
	m_colormap_high_value_sldr->SetValue(ival);
	m_vd->SetColormapValues(lcm, dval);
	//shadow intensity
	dval = mgr->m_vol_swi;
	str = wxString::Format("%.2f", dval);
	ival = int(dval*100.0+0.5);
	m_shadow_text->ChangeValue(str);
	m_shadow_sldr->SetValue(ival);
	m_vd->SetShadowParams(dval);

	//enable alpha
	bval = mgr->m_vol_eap;
	m_alpha_chk->SetValue(bval);
	if (m_sync_group && m_group)
		m_group->SetEnableAlpha(bval);
	else
		m_vd->SetEnableAlpha(bval);
	//enable shading
	bval = mgr->m_vol_esh;
	m_shading_enable_chk->SetValue(bval);
	if (m_sync_group && m_group)
		m_group->SetShading(bval);
	else
		m_vd->SetShading(bval);
	//inversion
	bval = mgr->m_vol_inv;
	m_inv_chk->SetValue(bval);
	if (m_sync_group && m_group)
		m_group->SetInvert(bval);
	else
		m_vd->SetInvert(bval);
	//enable mip
	bval = mgr->m_vol_mip;
	m_mip_chk->SetValue(bval);
	if (m_sync_group && m_group)
		m_group->SetMode(bval?1:0);
	else
		m_vd->SetMode(bval?1:0);
	//noise reduction
	bval = mgr->m_vol_nrd;
	m_nr_chk->SetValue(bval);
	if (m_sync_group && m_group)
		m_group->SetNR(bval);
	else
		m_vd->SetNR(bval);
	//shadow
	bval = mgr->m_vol_shw;
	m_shadow_chk->SetValue(bval);
	if (m_sync_group && m_group)
		m_group->SetShadow(bval);
	else
		m_vd->SetShadow(bval);

	//apply all
	RefreshVRenderViews();
}
