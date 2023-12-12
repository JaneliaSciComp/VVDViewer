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
#include "DLLExport.h"
#include "DataManager.h"
#include <wx/wx.h>
#include <wx/panel.h>
#include <wx/slider.h>
#include <wx/spinbutt.h>

#ifndef _CLIPPINGVIEW_H_
#define _CLIPPINGVIEW_H_

//plane modes

#define PM_NORMAL 0
#define PM_FRAME 1
#define PM_LOWTRANS 2
#define PM_LOWTRANSBACK 3
#define PM_NORMALBACK 4

class EXPORT_API ClippingView: public wxPanel
{
	enum
	{
		ID_LinkChannelsChk = wxID_HIGHEST+1,
		ID_PlaneModesCombo,
		ID_RotatePlanesChk,
		ID_ClipResetBtn,
		ID_SetZeroBtn,
		ID_RotResetBtn,
		//rotation sliders
		ID_XRotSldr,
		ID_YRotSldr,
		ID_ZRotSldr,
		ID_XRotText,
		ID_YRotText,
		ID_ZRotText,
		ID_XRotSpin,
		ID_YRotSpin,
		ID_ZRotSpin,
		//clipping sliders
		ID_X1ClipSldr,
		ID_X2ClipSldr,
		ID_Y1ClipSldr,
		ID_Y2ClipSldr,
		ID_Z1ClipSldr,
		ID_Z2ClipSldr,
		ID_X1ClipText,
		ID_X2ClipText,
		ID_Y1ClipText,
		ID_Y2ClipText,
		ID_Z1ClipText,
		ID_Z2ClipText,
		ID_LinkXChk,
		ID_LinkYChk,
		ID_LinkZChk,
		ID_YZClipBtn,
		ID_XZClipBtn,
		ID_XYClipBtn,
		ID_YZDistText,
		ID_XZDistText,
		ID_XYDistText,
		ID_CLTimer,
		ID_FixRotsChk,
        ID_HoldPlanesChk
	};

public:
	ClippingView(wxWindow* frame,
		wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos=wxDefaultPosition,
		const wxSize& size=wxDefaultSize,
		long style=0,
		const wxString& name="ClippingView");
	~ClippingView();

	void SetVolumeData(VolumeData* vd);
	void SetMeshData(MeshData* md);
	void SetDataManager(DataManager* mgr);
	int GetSelType();
	VolumeData* GetVolumeData();
	MeshData* GetMeshData();
	void RefreshVRenderViews(bool interactive=false);
	void RefreshVRenderViewsOverlay();
	void ClearData();

	bool GetChannLink()
	{
        return m_link_channels ? m_link_channels->GetValue() : false;
	}
	void SetChannLink(bool chann)
	{
		m_link_channels->SetValue(chann);
	}
	bool GetHoldPlanes()
	{ return m_hold_planes; }
	int GetPlaneMode()
	{ return m_plane_mode; }
	bool GetXLink()
	{
		return m_link_x_chk->GetValue();
	}
	bool GetYLink()
	{
		return m_link_y_chk->GetValue();
	}
	bool GetZLink()
	{
		return m_link_z_chk->GetValue();
	}
	void SetXLink(bool link)
	{
		m_link_x_chk->SetValue(link);
		m_link_x = link;
		wxCommandEvent ev;
		OnLinkXCheck(ev);
	}
	void SetYLink(bool link)
	{
		m_link_y_chk->SetValue(link);
		m_link_y = link;
		wxCommandEvent ev;
		OnLinkYCheck(ev);
	}
	void SetZLink(bool link)
	{
		m_link_z_chk->SetValue(link);
		m_link_z  = link;
		wxCommandEvent ev;
		OnLinkZCheck(ev);
	}

	void SetClippingPlaneRotations(double rotx, double roty, double rotz, bool update_only_ui=false)
	{
		m_update_only_ui = update_only_ui;

		m_x_rot_sldr->SetValue(int(rotx));
		m_y_rot_sldr->SetValue(int(roty));
		m_z_rot_sldr->SetValue(int(rotz));
		m_x_rot_text->SetValue(wxString::Format("%.1f", rotx));
		m_y_rot_text->SetValue(wxString::Format("%.1f", roty));
		m_z_rot_text->SetValue(wxString::Format("%.1f", rotz));

		m_update_only_ui = false;
	}

	void SetPlaneMode(int mode)
	{
		int elem_num = m_plane_mode_combo->GetStrings().size();
		if (mode < 0 || mode >= elem_num) return;
		
		m_plane_mode = mode;
		m_plane_mode_combo->SetSelection(mode);
	}

	//move linked clipping planes
	//dir: 0-lower; 1-higher
	void MoveLinkedClippingPlanes(int dir);

	void SaveDefault();
	void LoadDefault();

	void SetUpdateOnlyUIs(bool val) { m_update_only_ui = val; }
	bool GetUpdateOnlyUIs() { return m_update_only_ui; }

	void SetFixRotations(bool val) { m_fix_rots = val; }
	bool GetFixRotations() { return m_fix_rots; }
    
    int GetXdist();
    int GetYdist();
    int GetZdist();
    
    void SetLinkedX1Param(double p) { m_linked_plane_params[0] = p; }
    void SetLinkedX2Param(double p) { m_linked_plane_params[1] = p; }
    void SetLinkedY1Param(double p) { m_linked_plane_params[2] = p; }
    void SetLinkedY2Param(double p) { m_linked_plane_params[3] = p; }
    void SetLinkedZ1Param(double p) { m_linked_plane_params[4] = p; }
    void SetLinkedZ2Param(double p) { m_linked_plane_params[5] = p; }
    double GetLinkedX1Param() { return m_linked_plane_params[0]; }
    double GetLinkedX2Param() { return m_linked_plane_params[1]; }
    double GetLinkedY1Param() { return m_linked_plane_params[2]; }
    double GetLinkedY2Param() { return m_linked_plane_params[3]; }
    double GetLinkedZ1Param() { return m_linked_plane_params[4]; }
    double GetLinkedZ2Param() { return m_linked_plane_params[5]; }
    
    void CalcAndSetCombinedClippingPlanes();
    void CalcBoundingBoxDemensions(double &w, double &h, double &d);
    
    void SyncClippingPlanes();

private:
	wxWindow* m_frame;

	int m_sel_type;		//curent selection type
	VolumeData* m_vd;	//current volume data
	MeshData* m_md;		//current mesh data
	DataManager* m_mgr;	//manage all if clipping planes are synced
	bool m_draw_clip;
	bool m_hold_planes;
	int m_plane_mode;

	int m_x_sldr_dist;
	int m_y_sldr_dist;
	int m_z_sldr_dist;
	bool m_link_x;
	bool m_link_y;
	bool m_link_z;

	bool m_fix_rots;
	bool m_update_only_ui;
    bool m_mouse_in;
    
    double m_x_factor;
    double m_y_factor;
    double m_z_factor;

	//1st line
	wxCheckBox *m_link_channels;
	wxComboBox *m_plane_mode_combo;
	wxButton *m_clip_reset_btn;
	//fix plane rotations
	wxCheckBox* m_fix_rots_chk;
    wxCheckBox* m_hold_planes_chk;
	wxButton *m_set_zero_btn;
	wxButton *m_rot_reset_btn;

	//sliders for rotating clipping planes
	wxSlider *m_x_rot_sldr;
	wxSlider *m_y_rot_sldr;
	wxSlider *m_z_rot_sldr;
	wxTextCtrl *m_x_rot_text;
	wxTextCtrl *m_y_rot_text;
	wxTextCtrl *m_z_rot_text;
	wxSpinButton* m_x_rot_spin;
	wxSpinButton* m_y_rot_spin;
	wxSpinButton* m_z_rot_spin;

	//sliders for clipping planes
	//x1
	wxSlider *m_x1_clip_sldr;
	wxTextCtrl *m_x1_clip_text;
	//x2
	wxSlider *m_x2_clip_sldr;
	wxTextCtrl *m_x2_clip_text;
	//y1
	wxSlider *m_y1_clip_sldr;
	wxTextCtrl *m_y1_clip_text;
	//y2
	wxSlider *m_y2_clip_sldr;
	wxTextCtrl *m_y2_clip_text;
	//z1
	wxSlider *m_z1_clip_sldr;
	wxTextCtrl *m_z1_clip_text;
	//z2
	wxSlider *m_z2_clip_sldr;
	wxTextCtrl *m_z2_clip_text;
	//keep 1 panel for sizing reasons
	wxPanel * m_xpanel;
	//highlighters
	wxStaticText * m_xBar, * m_yBar, * m_zBar;

	//linkers
	wxCheckBox *m_link_x_chk;
	wxCheckBox *m_link_y_chk;
	wxCheckBox *m_link_z_chk;

	//buttons
	wxButton *m_yz_clip_btn;
	wxButton *m_xz_clip_btn;
	wxButton *m_xy_clip_btn;
	//distance text
	wxTextCtrl *m_yz_dist_text;
	wxTextCtrl *m_xz_dist_text;
	wxTextCtrl *m_xy_dist_text;

	wxTimer *m_timer;
    
    double m_linked_plane_params[6];

#if defined(__WXGTK__)
	static constexpr int spin_w = 70;
	static constexpr int disp_comb_w = 150;
	static constexpr int reset_button_w = 170;
	static constexpr int sldr_w = 33;
#else
	static constexpr int spin_w = 30;
	static constexpr int disp_comb_w = 115;
	static constexpr int reset_button_w = 150;
	static constexpr int sldr_w = 20;
#endif

private:
	void GetSettings();
	
	void OnIdle(wxTimerEvent& event);

	void OnLinkChannelsCheck(wxCommandEvent &event);
	void OnPlaneModesCombo(wxCommandEvent &event);
	void OnClipResetBtn(wxCommandEvent &event);

	void EnableAll();
	void DisableAll();

	void EnableRotations();
	void DisableRotations();

	void OnX1ClipChange(wxScrollEvent &event);
	void OnX2ClipChange(wxScrollEvent &event);
	void OnY1ClipChange(wxScrollEvent &event);
	void OnY2ClipChange(wxScrollEvent &event);
	void OnZ1ClipChange(wxScrollEvent &event);
	void OnZ2ClipChange(wxScrollEvent &event);
	void OnX1ClipEdit(wxCommandEvent &event);
	void OnX2ClipEdit(wxCommandEvent &event);
	void OnY1ClipEdit(wxCommandEvent &event);
	void OnY2ClipEdit(wxCommandEvent &event);
	void OnZ1ClipEdit(wxCommandEvent &event);
	void OnZ2ClipEdit(wxCommandEvent &event);

	void OnLinkXCheck(wxCommandEvent &event);
	void OnLinkYCheck(wxCommandEvent &event);
	void OnLinkZCheck(wxCommandEvent &event);

	void OnSetZeroBtn(wxCommandEvent &event);
	void OnRotResetBtn(wxCommandEvent &event);

	void OnFixRotsCheck(wxCommandEvent& event);
    void OnHoldPlanesCheck(wxCommandEvent& event);
	void OnXRotChange(wxScrollEvent &event);
	void OnYRotChange(wxScrollEvent &event);
	void OnZRotChange(wxScrollEvent &event);
	void OnXRotEdit(wxCommandEvent &event);
	void OnYRotEdit(wxCommandEvent &event);
	void OnZRotEdit(wxCommandEvent &event);

	//spin buttons
	void OnXRotSpinUp(wxSpinEvent& event);
	void OnXRotSpinDown(wxSpinEvent& event);
	void OnYRotSpinUp(wxSpinEvent& event);
	void OnYRotSpinDown(wxSpinEvent& event);
	void OnZRotSpinUp(wxSpinEvent& event);
	void OnZRotSpinDown(wxSpinEvent& event);

	//mouse
	void OnSliderRClick(wxCommandEvent& event);

	//clip buttons
	void OnYZClipBtn(wxCommandEvent& event);
	void OnXZClipBtn(wxCommandEvent& event);
	void OnXYClipBtn(wxCommandEvent& event);

	//key down
	void OnSliderKeyDown(wxKeyEvent& event);

	DECLARE_EVENT_TABLE();

};

#endif//_CLIPPINGVIEW_H_
