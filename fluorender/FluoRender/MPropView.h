/*
For more information, please see: http://software.sci.utah.edu

The MIT License

Copyright (c) 2014 Scientific Computing and Imaging Institute,
University of Utah.


Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/
#include "DataManager.h"
#include "VRenderView.h"
#include <wx/wx.h>
#include <wx/panel.h>
#include <wx/glcanvas.h>
#include <wx/clrpicker.h>
#include <wx/slider.h>
#include "DLLExport.h"

#ifndef _MPROPVIEW_H_
#define _MPROPVIEW_H_

using namespace std;


class EXPORT_API mpTextCtrl : public wxTextCtrl
{
public:
	mpTextCtrl(wxWindow* frame,
		wxWindow* parent,
		wxWindowID id,
		const wxString& text = wxT(""),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style=0,
		const wxValidator& valid = wxDefaultValidator);
	~mpTextCtrl();

private:
	wxWindow *m_frame;
	wxButton *m_dummy;
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


class EXPORT_API MPropView: public wxPanel
{
	enum
	{
		ID_diff_picker = wxID_HIGHEST+401,
		ID_spec_picker,
		ID_shine_sldr,
		ID_shine_text,
		ID_light_chk,
		ID_alpha_sldr,
		ID_alpha_text,
		ID_scale_sldr,
		ID_scale_text,
		ID_shadow_chk,
		ID_shadow_sldr,
		ID_shadow_text,
		ID_size_chk,
		ID_size_sldr,
		ID_size_text,
		ID_r_text,
        ID_label_chk,
        ID_sync_group_chk,
        ID_sync_name_chk,
		ID_sync_chk
	};

public:
	MPropView(wxWindow* frame, wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxString& name = "MPropView");
	~MPropView();

	void SetMeshData(MeshData* md, VRenderView* vrv);
	MeshData* GetMeshData();
	void RefreshVRenderViews(bool tree=false);

	void GetSettings();

	void UpdateRadScale();
    
    bool GetSyncGroup() { return m_group_sync; }
    bool GetSyncName() { return m_name_sync; }

private:
	wxWindow* m_frame;
	MeshData* m_md;
	VRenderView* m_vrv;

	wxCheckBox *m_light_chk;
	wxColourPickerCtrl *m_diff_picker;
	wxColourPickerCtrl *m_spec_picker;
	wxSlider *m_shine_sldr;
	wxTextCtrl* m_shine_text;

	wxSlider *m_alpha_sldr;
	wxTextCtrl* m_alpha_text;
	wxCheckBox* m_shadow_chk;
	wxSlider* m_shadow_sldr;
	wxTextCtrl* m_shadow_text;

	wxSlider *m_scale_sldr;
	wxTextCtrl* m_scale_text;
	//size limiter
	wxCheckBox *m_size_chk;
	wxSlider *m_size_sldr;
	wxTextCtrl *m_size_text;

	wxStaticText *m_r_st;
	mpTextCtrl *m_r_text;
    
    wxCheckBox *m_label_chk;

    wxCheckBox *m_sync_group_chk;
    wxCheckBox *m_sync_name_chk;
	wxCheckBox *m_sync_chk;
	bool m_sync;
    bool m_group_sync;
    bool m_name_sync;

private:
	//lighting
	void OnLightingCheck(wxCommandEvent& event);
	void OnDiffChange(wxColourPickerEvent& event);
	void OnSpecChange(wxColourPickerEvent& event);
	void OnShineChange(wxScrollEvent & event);
	void OnShineText(wxCommandEvent& event);
	void OnAlphaChange(wxScrollEvent & event);
	void OnAlphaText(wxCommandEvent& event);
	void OnScaleChange(wxScrollEvent & event);
	void OnScaleText(wxCommandEvent& event);
	//shadow
	void OnShadowCheck(wxCommandEvent& event);
	void OnShadowChange(wxScrollEvent& event);
	void OnShadowText(wxCommandEvent& event);
	//size limiter
	void OnSizeCheck(wxCommandEvent& event);
	void OnSizeChange(wxScrollEvent& event);
	void OnSizeText(wxCommandEvent& event);

	void OnEnterInRadScaleText(wxCommandEvent& event);
    
    void OnLabelCheck(wxCommandEvent& event);

    void OnSyncGroupCheck(wxCommandEvent& event);
    void OnSyncNameCheck(wxCommandEvent& event);
	void OnSyncCheck(wxCommandEvent& event);

	void UpdateSync();

	DECLARE_EVENT_TABLE();
};

#endif//_MPROPVIEW_H_
