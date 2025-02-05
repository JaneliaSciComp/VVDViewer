#include "DataManager.h"
#include <wx/wx.h>
#include <wx/panel.h>
#include <wx/glcanvas.h>
#include <wx/clrpicker.h>
#include <wx/slider.h>

#include "DLLExport.h"
#include "FLIVR/Color.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/BBox.h"
#include "FLIVR/Point.h"
#include "FLIVR/MultiVolumeRenderer.h"


#ifndef _VPROPVIEW_H_
#define _VPROPVIEW_H_

using namespace std;

class VRenderView;

class EXPORT_API vpTextCtrl : public wxTextCtrl
{
public:
	vpTextCtrl(wxWindow* frame,
		wxWindow* parent,
		wxWindowID id,
		const wxString& text = wxT(""),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxValidator& valid = wxDefaultValidator);
	~vpTextCtrl();

private:
	wxWindow* m_frame;
	wxButton* m_dummy;
	long m_style;

private:
	void OnSetChildFocus(wxChildFocusEvent& event);
	void OnSetFocus(wxFocusEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);

	void OnText(wxCommandEvent& event);
	void OnEnter(wxCommandEvent& event);

	DECLARE_EVENT_TABLE()
};

class EXPORT_API VPropView: public wxPanel
{
	enum
	{
		ID_Load = wxID_HIGHEST+801,
		ID_Save,
		ID_AddRect,
		ID_AddTri,
		ID_Del,
		ID_ColorText,
		ID_ColorBtn,
		ID_AlphaChk,
		ID_AlphaSldr,
		ID_Alpha_Text,
		ID_SampleSldr,
		ID_SampleText,
		ID_BoundarySldr,
		ID_BoundaryText,
		ID_MaxText,
		ID_MinText,
		ID_GammaSldr,
		ID_GammaText,
		ID_ContrastSldr,
		ID_ContrastText,
		ID_LeftThreshSldr,
		ID_LeftThreshText,
		ID_RightThreshSldr,
		ID_RightThreshText,
		ID_LowShadingSldr,
		ID_LowShadingText,
		ID_ShadingEnableChk,
		ID_HiShadingSldr,
		ID_HiShadingText,
		ID_ShadowChk,
		ID_ShadowSldr,
		ID_ShadowText,
		ID_SpaceXText,
		ID_SpaceYText,
		ID_SpaceZText,
		ID_LuminanceSldr,
		ID_LuminanceText,
		ID_ScaleChk,
		ID_ScaleText,
		ID_ScaleCmb,
		ID_ScaleTextChk,
		ID_ScaleLenFixChk,
		ID_ScaleDigitCombo,
		ID_LegendChk,
		ID_SyncGroupChk,
		ID_SyncGroupSpcChk,
		ID_SaveDefault,
		ID_ResetDefault,
		ID_ColormapEnableChk,
		ID_ColormapHighValueSldr,
		ID_ColormapHighValueText,
		ID_ColormapLowValueSldr,
		ID_ColormapLowValueText,
		ID_ROINameText,
		ID_ROIColorBtn,
		ID_ROIDispModesCombo,
		ID_InvChk,
		ID_IDCLChk,
		ID_MipChk,
		ID_NRChk,
		ID_DepthChk,
		ID_MaskHideOutside,
		ID_MaskHideInside
};

public:
	VPropView(wxWindow* frame, wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxString& name = "VPropView");
	~VPropView();

	void SetVolumeData(VolumeData* vd);
	VolumeData* GetVolumeData();
	void RefreshVRenderViews(bool tree=false, bool interactive=false);
	void InitVRenderViews(unsigned int type);

	//sync group
	void SetGroup(DataGroup* group);
	DataGroup* GetGroup();

	//sync view in depth mode
	void SetView(VRenderView* view);
	VRenderView* GetView();

	void SaveROIName();
	void SetROIindex(int id);
	void ShowUIsROI();
	void HideUIsROI();
	void UpdateUIsROI();

	void UpdateMaxValue();

private:
	wxWindow* m_frame;
	VolumeData* m_vd;

	bool m_lumi_change;
	bool m_sync_group;
	bool m_sync_group_spc;
	DataGroup* m_group;
	VRenderView* m_vrv;
	double m_max_val;
	bool m_is_float;

	wxBoxSizer* m_sizer_sl_righ;
	wxBoxSizer* m_sizer_r5;
	wxBoxSizer* m_sizer_r6;

	//1st line
	//wxTextCtrl* m_min_text;
	wxBoxSizer* m_sizer_0;
	wxTextCtrl* m_max_text;
	//gamma
	wxSlider *m_gamma_sldr;
	wxTextCtrl *m_gamma_text;
	//boundary
	wxSlider *m_boundary_sldr;
	wxTextCtrl *m_boundary_text;

	//2nd line
	//saturation point
	wxSlider *m_contrast_sldr;
	wxTextCtrl *m_contrast_text;
	//thresholds
	wxStaticText *m_threh_st;
	wxSlider *m_left_thresh_sldr;
	wxTextCtrl *m_left_thresh_text;
	wxSlider *m_right_thresh_sldr;
	wxTextCtrl *m_right_thresh_text;

	//3rd line
	//luminance
	wxSlider *m_luminance_sldr;
	wxTextCtrl* m_luminance_text;
	//highlight
	wxSlider *m_hi_shading_sldr;
	wxTextCtrl *m_hi_shading_text;

	//4th line
	//alpha
	wxCheckBox *m_alpha_chk;
	wxSlider *m_alpha_sldr;
	wxTextCtrl* m_alpha_text;
	//sample rate
	wxSlider *m_sample_sldr;
	wxTextCtrl *m_sample_text;

	//5th line
	//shading
	wxSlider *m_low_shading_sldr;
	wxTextCtrl *m_low_shading_text;
	wxCheckBox *m_shading_enable_chk;
	//shadow
	wxSlider *m_shadow_sldr;
	wxTextCtrl *m_shadow_text;
	wxCheckBox *m_shadow_chk;

	//6th line
	//color
	//wxColourPickerCtrl *m_color_picker;
	wxTextCtrl *m_color_text;
	wxColourPickerCtrl *m_color_btn;
	//space
	wxTextCtrl *m_space_x_text;
	wxTextCtrl *m_space_y_text;
	wxTextCtrl *m_space_z_text;
	//colormap
	wxCheckBox *m_colormap_enable_chk;
	wxSlider *m_colormap_high_value_sldr;
	wxTextCtrl *m_colormap_high_value_text;
	wxSlider *m_colormap_low_value_sldr;
	wxTextCtrl *m_colormap_low_value_text;
	//roi
	int m_roi_id;
	wxStaticText *m_roi_st;
	wxTextCtrl *m_roi_text;
	wxColourPickerCtrl *m_roi_color_btn;
	wxComboBox *m_roi_disp_mode_combo;


	//bottom line
	//invert
	wxCheckBox *m_inv_chk;
	//Index color
	wxCheckBox *m_idcl_chk;
	//MIP
	wxCheckBox *m_mip_chk;
	//Noise reduction
	wxCheckBox *m_nr_chk;
	//Depth
	wxCheckBox *m_depth_chk;
	//scale bar
	wxCheckBox *m_scale_chk;
	wxTextCtrl *m_scale_text;
	wxComboBox *m_scale_cmb;
	wxCheckBox *m_scale_te_chk;
	wxCheckBox *m_scale_lenfix_chk;
	wxComboBox *m_scale_digit_cmb;
	//legend
	wxCheckBox *m_legend_chk;
	//sync
	wxCheckBox *m_sync_group_chk;
	wxCheckBox *m_sync_g_spc_chk;
	//default
	wxButton *m_save_default;
	wxButton *m_reset_default;

	wxCheckBox *m_mask_outside_chk;
	wxCheckBox *m_mask_inside_chk;

private:
	void GetSettings();
	bool SetSpacings();

	void OnEnterInMaxText(wxCommandEvent& event);
	//1
	void OnGammaChange(wxScrollEvent &event);
	void OnGammaText(wxCommandEvent &event);
	void OnBoundaryChange(wxScrollEvent &event);
	void OnBoundaryText(wxCommandEvent &event);
	//2
	void OnContrastChange(wxScrollEvent &event);
	void OnContrastText(wxCommandEvent &event);
	void OnLeftThreshChange(wxScrollEvent &event);
	void OnLeftThreshText(wxCommandEvent &event);
	void OnRightThreshChange(wxScrollEvent &event);
	void OnRightThreshText(wxCommandEvent &event);
	//3
	void OnLuminanceChange(wxScrollEvent &event);
	void OnLuminanceText(wxCommandEvent &event);
	//shadow
	void OnShadowEnable(wxCommandEvent &event);
	void OnShadowChange(wxScrollEvent &event);
	void OnShadowText(wxCommandEvent &event);
	void OnHiShadingChange(wxScrollEvent &event);
	void OnHiShadingText(wxCommandEvent &event);
	//4
	void OnAlphaCheck(wxCommandEvent &event);
	void OnAlphaChange(wxScrollEvent & event);
	void OnAlphaText(wxCommandEvent& event);
	void OnSampleChange(wxScrollEvent &event);
	void OnSampleText(wxCommandEvent &event);
	//5
	void OnLowShadingChange(wxScrollEvent &event);
	void OnLowShadingText(wxCommandEvent &event);
	void OnShadingEnable(wxCommandEvent &event);
	//colormap
	void OnEnableColormap(wxCommandEvent &event);
	void OnColormapHighValueChange(wxScrollEvent &event);
	void OnColormapHighValueText(wxCommandEvent &event);
	void OnColormapLowValueChange(wxScrollEvent &event);
	void OnColormapLowValueText(wxCommandEvent &event);
	//roi
	void OnEnterInROINameText(wxCommandEvent& event);
	void OnROIColorBtn(wxColourPickerEvent& event);
	void OnROIDispModesCombo(wxCommandEvent &event);
	//6
	void OnColorChange(wxColor c);
	void OnColorTextChange(wxCommandEvent& event);
	void OnColorTextFocus(wxCommandEvent& event);
	void OnColorBtn(wxColourPickerEvent& event);
	//spacings
	void OnSpaceText(wxCommandEvent& event);
	//scale bar
	void OnScaleCheck(wxCommandEvent& event);
	void OnScaleTextCheck(wxCommandEvent& event);
	void OnScaleTextEditing(wxCommandEvent& event);
	void OnScaleUnitSelected(wxCommandEvent& event);
	void OnScaleLenFixCheck(wxCommandEvent& event);
	void OnScaleDigitSelected(wxCommandEvent& event);
	//legend
	void OnLegendCheck(wxCommandEvent& event);
	//sync within group
	void OnSyncGroupCheck(wxCommandEvent& event);
	void OnSyncGroupSpcCheck(wxCommandEvent& event);
	//save as default
	void OnSaveDefault(wxCommandEvent& event);
	void OnResetDefault(wxCommandEvent& event);
	//inversion
	void OnInvCheck(wxCommandEvent &event);
	//Index color
	void OnIDCLCheck(wxCommandEvent &event);
	//MIP
	void OnMIPCheck(wxCommandEvent &event);
	//noise reduction
	void OnNRCheck(wxCommandEvent &event);
	//depth omde
	void OnDepthCheck(wxCommandEvent &event);

	void OnMaskHideOutsideCheck(wxCommandEvent &event);
	void OnMaskHideInsideCheck(wxCommandEvent &event);

	DECLARE_EVENT_TABLE();
};

#endif//_VPROPVIEW_H_
