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
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/clrpicker.h>
#include "FLIVR/Color.h"
#include "DLLExport.h"

#ifndef _MEASUREDLG_H_
#define _MEASUREDLG_H_

using namespace std;
using namespace FLIVR;

class VRenderView;

class EXPORT_API RulerListCtrl : public wxListCtrl
{
	enum
	{
		ID_RulerNameDispText = wxID_HIGHEST+2351,
		ID_ColorPicker,
		ID_RulerDescriptionText
	};

public:
	RulerListCtrl(wxWindow *frame,
		wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style=wxLC_REPORT|wxLC_SINGLE_SEL);
	~RulerListCtrl();

	void Append(wxString name, wxString &color, double length, wxString &unit,
		double angle, wxString &points, bool time_dep, int time, wxString extra, int type);
	void UpdateRulers(VRenderView* vrv=0, bool update_annotaions=true);

	void DeleteSelection();
	void DeleteAll(bool cur_time=true);

	void Export(wxString filename);

	wxString GetText(long item, int col);
	void SetText(long item, int col, const wxString &str);
	void UpdateText(VRenderView* vrv=0);

	friend class MeasureDlg;

private:
	//wxWindow* m_frame;
	VRenderView *m_view;
	wxImageList *m_images;

	wxTextCtrl *m_name_disp;
	wxColourPickerCtrl *m_color_picker;
	wxTextCtrl *m_description_text;

	long m_editing_item;
	long m_dragging_to_item;
	long m_dragging_item;

	bool m_show_anno;

	long m_ruler_count;

private:
	void EndEdit();
	void OnAct(wxListEvent &event);
	void OnEndSelection(wxListEvent &event);
	void OnNameDispText(wxCommandEvent& event);
	void OnDescriptionText(wxCommandEvent& event);
	void OnEnterInTextCtrl(wxCommandEvent& event);
	void OnColorChange(wxColourPickerEvent& event);
	void OnScroll(wxScrollWinEvent& event);
	void OnScroll(wxMouseEvent& event);
	void OnBeginDrag(wxListEvent& event);
	void OnDragging(wxMouseEvent& event);
	void OnEndDrag(wxMouseEvent& event);
	void OnLeftDClick(wxMouseEvent& event);

	void OnColumnSizeChanged(wxListEvent &event);
	void OnColBeginDrag(wxListEvent& event);

	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);

	void ShowTextCtrls(long item);

	DECLARE_EVENT_TABLE()
protected: //Possible TODO
	wxSize GetSizeAvailableForScrollTarget(const wxSize& size) {
		return size - GetEffectiveMinSize();
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////
class EXPORT_API MeasureDlg : public wxPanel
{
public:
	enum
	{
		ID_LocatorBtn = wxID_HIGHEST+2101,
		ID_RulerBtn,
		ID_RulerMPBtn,
		ID_RulerEditBtn,
		ID_DeleteBtn,
		ID_DeleteAllBtn,
		ID_ExportBtn,
		ID_IntensityMethodsCombo,
		ID_UseTransferChk,
		ID_TransientChk
	};

	MeasureDlg(wxWindow* frame,
		wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxString& name = "MeasureDlg");
	~MeasureDlg();

	void GetSettings(VRenderView* vrv, bool update_annotaions = true);
	VRenderView* GetView();
	void UpdateList(bool update_annotaions=true);

private:
	wxWindow* m_frame;
	//current view
	VRenderView* m_view;

	//list ctrl
	RulerListCtrl *m_rulerlist;
	//tool bar
	wxToolBar *m_toolbar;
	//options
	wxComboBox *m_int_method_combo;
	wxCheckBox *m_use_transfer_chk;
	wxCheckBox *m_transient_chk;

private:
	void OnNewLocator(wxCommandEvent& event);
	void OnNewRuler(wxCommandEvent& event);
	void OnNewRulerMP(wxCommandEvent& event);
	void OnRulerEdit(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	void OnDeleteAll(wxCommandEvent& event);
	void OnExport(wxCommandEvent& event);
	void OnIntensityMethodsCombo(wxCommandEvent& event);
	void OnUseTransferCheck(wxCommandEvent& event);
	void OnTransientCheck(wxCommandEvent& event);

	DECLARE_EVENT_TABLE();
};

#endif//_MEASUREDLG_H_
