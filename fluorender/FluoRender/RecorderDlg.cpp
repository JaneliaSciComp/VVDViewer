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
#include "RecorderDlg.h"
#include "VRenderFrame.h"
#include <wx/artprov.h>
#include <wx/valnum.h>
#include "key.xpm"
#include "png_resource.h"
#include "img/icons.h"

BEGIN_EVENT_TABLE(KeyListCtrl, wxListCtrl)
	EVT_LIST_ITEM_ACTIVATED(wxID_ANY, KeyListCtrl::OnAct)
	EVT_LIST_ITEM_SELECTED(wxID_ANY, KeyListCtrl::OnSelection)
	EVT_LIST_ITEM_DESELECTED(wxID_ANY, KeyListCtrl::OnEndSelection)
	EVT_TEXT(ID_FrameText, KeyListCtrl::OnFrameText)
	EVT_TEXT(ID_DurationText, KeyListCtrl::OnDurationText)
	EVT_COMBOBOX(ID_InterpoCmb, KeyListCtrl::OnInterpoCmb)
	EVT_TEXT(ID_DescriptionText, KeyListCtrl::OnDescritionText)
	EVT_KEY_DOWN(KeyListCtrl::OnKeyDown)
	EVT_KEY_UP(KeyListCtrl::OnKeyUp)
	EVT_LIST_BEGIN_DRAG(wxID_ANY, KeyListCtrl::OnBeginDrag)
	EVT_SCROLLWIN(KeyListCtrl::OnScroll)
	EVT_MOUSEWHEEL(KeyListCtrl::OnScroll)
END_EVENT_TABLE()

KeyListCtrl::KeyListCtrl(
	wxWindow* frame,
	wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style) :
wxListCtrl(parent, id, pos, size, style),
m_frame(frame),
m_editing_item(-1),
m_dragging_to_item(-1)
{
	SetEvtHandlerEnabled(false);
	Freeze();

#if defined(__WXGTK__)
	int size_fix_w = 20;
#else
	int size_fix_w = 0;
#endif

	//validator: integer
	wxIntegerValidator<unsigned int> vald_int;

	wxListItem itemCol;
	itemCol.SetText("ID");
	this->InsertColumn(0, itemCol);
    SetColumnWidth(0, 40);
	itemCol.SetText("Frame");
	this->InsertColumn(1, itemCol);
    SetColumnWidth(1, 60);
	itemCol.SetText("Inbetweens");
	this->InsertColumn(2, itemCol);
    SetColumnWidth(2, 80+size_fix_w);
	itemCol.SetText("Interpolation");
	this->InsertColumn(3, itemCol);
    SetColumnWidth(3, 80+size_fix_w);
	itemCol.SetText("Description");
	this->InsertColumn(4, itemCol);
    SetColumnWidth(4, 80+size_fix_w);

	m_images = new wxImageList(16, 16, true);
	wxIcon icon = wxIcon(key_xpm);
	m_images->Add(icon);
	AssignImageList(m_images, wxIMAGE_LIST_SMALL);

	//frame edit
	m_frame_text = new wxTextCtrl(this, ID_FrameText, "",
		wxDefaultPosition, wxDefaultSize, 0, vald_int);
	m_frame_text->Hide();
	//duration edit
	m_duration_text = new wxTextCtrl(this, ID_DurationText, "",
		wxDefaultPosition, wxDefaultSize, 0, vald_int);
	m_duration_text->Hide();
	//interpolation combo box
	m_interpolation_cmb = new wxComboBox(this, ID_InterpoCmb, "",
		wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_READONLY);
	m_interpolation_cmb->Append("Linear");
	m_interpolation_cmb->Append("Smooth");
	m_interpolation_cmb->Hide();
	//description edit
	m_description_text = new wxTextCtrl(this, ID_DescriptionText, "",
		wxDefaultPosition, wxDefaultSize);
	m_description_text->Hide();

	//SetDoubleBuffered(true);
	Thaw();
	SetEvtHandlerEnabled(true);
}

KeyListCtrl::~KeyListCtrl()
{
}

void KeyListCtrl::Append(int id, int time, int duration, int interp, string &description)
{
	long tmp = InsertItem(GetItemCount(), wxString::Format("%d", id), 0);
	SetItem(tmp, 1, wxString::Format("%d", time));
	SetItem(tmp, 2, wxString::Format("%d", duration));
	SetItem(tmp, 3, interp==0?"Linear":"Smooth");
	SetItem(tmp, 4, description);
}

void KeyListCtrl::DeleteSel()
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item == -1)
		return;
	wxString str = GetItemText(item);
	long id;
	str.ToLong(&id);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;
	interpolator->RemoveKey(id);
	Update();
}

void KeyListCtrl::DeleteAll()
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;
	interpolator->Clear();
	Update();
}

void KeyListCtrl::Update()
{
	m_frame_text->Hide();
	m_duration_text->Hide();
	m_interpolation_cmb->Hide();
	m_description_text->Hide();
	m_editing_item = -1;

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;
	VMovieView* mov_view = vr_frame->GetMovieView();
	if (!mov_view)
		return;

	DeleteAllItems();
	for (int i=0; i<interpolator->GetKeyNum(); i++)
	{
		int id = interpolator->GetKeyID(i);
		int time = interpolator->GetKeyTime(i);
		int duration = interpolator->GetKeyDuration(i);
		int interp = interpolator->GetKeyType(i);
		string desc = interpolator->GetKeyDesc(i);
		Append(id, time, duration, interp, desc);
	}

	long fps = mov_view->GetFPS();
	int frames = int(interpolator->GetLastT());
	if (frames > 0 && fps > 0)
	{
		double runtime = (double)frames / (double)fps;
		mov_view->SetMovieTime(runtime);
	}
}

void KeyListCtrl::UpdateText()
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;
	VMovieView* mov_view = vr_frame->GetMovieView();
	if (!mov_view)
		return;

	wxString str;

	for (int i=0; i<interpolator->GetKeyNum(); i++)
	{
		int id = interpolator->GetKeyID(i);
		int time = interpolator->GetKeyTime(i);
		int duration = interpolator->GetKeyDuration(i);
		int interp = interpolator->GetKeyType(i);
		string desc = interpolator->GetKeyDesc(i);
		
        wxString wx_id = wxString::Format("%d", id);
        wxString wx_time = wxString::Format("%d", time);
        wxString wx_duration = wxString::Format("%d", duration);
		SetText(i, 0, wx_id);
		SetText(i, 1, wx_time);
		SetText(i, 2, wx_duration);
		str = interp==0?"Linear":"Smooth";
		SetText(i, 3, str);
		str = desc;
		SetText(i, 4, str);
	}

	long fps = mov_view->GetFPS();
	int frames = int(interpolator->GetLastT());
	if (frames > 0 && fps > 0)
	{
		double runtime = (double)frames / (double)fps;
		mov_view->SetMovieTime(runtime);
	}
}

void KeyListCtrl::OnAct(wxListEvent &event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item == -1)
		return;
	wxString str = GetItemText(item);
	long id;
	str.ToLong(&id);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;

	int index = interpolator->GetKeyIndex(int(id));
	double time = interpolator->GetKeyTime(index);
	double end_frame = interpolator->GetLastT();
	VMovieView *mov_view = vr_frame->GetMovieView();
	if (mov_view)
	{
		mov_view->SetProgress(time / end_frame);
		mov_view->SetRendering(time / end_frame);
	}

	/*
	VRenderView* view = vr_frame->GetRecorderDlg()->GetView();
	if (!view)
		view = vr_frame->GetView(0);
	if (view)
	{
		view->m_glview->SetParams(time);
		view->RefreshGL();
	}
	*/
}

wxString KeyListCtrl::GetText(long item, int col)
{
	wxListItem info;
	info.SetId(item);
	info.SetColumn(col);
	info.SetMask(wxLIST_MASK_TEXT);
	GetItem(info);
	return info.GetText();
}

void KeyListCtrl::SetText(long item, int col, wxString &str)
{
	wxListItem info;
	info.SetId(item);
	info.SetColumn(col);
	info.SetMask(wxLIST_MASK_TEXT);
	GetItem(info);
	info.SetText(str);
	SetItem(info);
}

void KeyListCtrl::OnSelection(wxListEvent &event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	m_editing_item = item;
	if (item != -1 && m_dragging_to_item==-1)
	{
		wxRect rect;
		wxString str;
		//add frame text
		GetSubItemRect(item, 1, rect);
		str = GetText(item, 1);
		m_frame_text->SetPosition(rect.GetTopLeft());
		m_frame_text->SetSize(rect.GetSize());
		m_frame_text->SetValue(str);
		//m_frame_text->Show();
		//add duration text
		GetSubItemRect(item, 2, rect);
		str = GetText(item, 2);
		m_duration_text->SetPosition(rect.GetTopLeft());
		m_duration_text->SetSize(rect.GetSize());
		m_duration_text->SetValue(str);
		m_duration_text->Show();
		//add interpolation combo
		GetSubItemRect(item, 3, rect);
		str = GetText(item, 3);
		m_interpolation_cmb->SetPosition(rect.GetTopLeft()-wxSize(0,5));
		m_interpolation_cmb->SetSize(wxSize(rect.GetSize().GetWidth(),-1));
		int sel = 0;
		if (str == "Linear")
			sel = 0;
		else if (str == "Smooth")
			sel = 1;
		m_interpolation_cmb->Select(sel);
		m_interpolation_cmb->Show();
		//add description text
		GetSubItemRect(item, 4, rect);
		str = GetText(item, 4);
		m_description_text->SetPosition(rect.GetTopLeft());
		m_description_text->SetSize(rect.GetSize());
		m_description_text->SetValue(str);
		m_description_text->Show();
	}
}

void KeyListCtrl::EndEdit(bool update)
{
	if (m_duration_text->IsShown())
	{
		m_frame_text->Hide();
		m_duration_text->Hide();
		m_interpolation_cmb->Hide();
		m_description_text->Hide();
		m_editing_item = -1;
		if (update) UpdateText();
	}
}

void KeyListCtrl::OnEndSelection(wxListEvent &event)
{
	EndEdit();
}

void KeyListCtrl::OnFrameText(wxCommandEvent& event)
{
	if (m_editing_item == -1)
		return;

	wxString str = GetItemText(m_editing_item);
	long id;
	str.ToLong(&id);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;

	int index = interpolator->GetKeyIndex(int(id));
	str = m_frame_text->GetValue();
	double time;
	if (str.ToDouble(&time))
	{
		interpolator->ChangeTime(index, time);
	}
}

void KeyListCtrl::OnDurationText(wxCommandEvent& event)
{
	if (m_editing_item == -1)
		return;

	wxString str = GetItemText(m_editing_item);
	long id;
	str.ToLong(&id);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;

	int index = interpolator->GetKeyIndex(int(id));
	str = m_duration_text->GetValue();
	double duration;
	if (str.ToDouble(&duration))
	{
		interpolator->ChangeDuration(index, duration);
		SetText(m_editing_item, 2, str);
	}
}

void KeyListCtrl::OnInterpoCmb(wxCommandEvent& event)
{
	if (m_editing_item == -1)
		return;

	wxString str = GetItemText(m_editing_item);
	long id;
	str.ToLong(&id);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;

	int index = interpolator->GetKeyIndex(int(id));
	FlKeyGroup* keygroup = interpolator->GetKeyGroup(index);
	if (keygroup)
	{
		int sel = m_interpolation_cmb->GetSelection();
		keygroup->type = sel;
		str = sel==0?"Linear":"Smooth";
		SetText(m_editing_item, 3, str);
	}
}

void KeyListCtrl::OnDescritionText(wxCommandEvent& event)
{
	if (m_editing_item == -1)
		return;

	wxString str = GetItemText(m_editing_item);
	long id;
	str.ToLong(&id);

	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;

	int index = interpolator->GetKeyIndex(int(id));
	FlKeyGroup* keygroup = interpolator->GetKeyGroup(index);
	if (keygroup)
	{
		str = m_description_text->GetValue();
		keygroup->desc = str.ToStdString();
		SetText(m_editing_item, 4, str);
	}
}

void KeyListCtrl::OnKeyDown(wxKeyEvent& event)
{
	if ( event.GetKeyCode() == WXK_DELETE ||
		event.GetKeyCode() == WXK_BACK)
		DeleteSel();
	event.Skip();
}

void KeyListCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void KeyListCtrl::OnBeginDrag(wxListEvent& event)
{
	if (m_editing_item == -1)
		return;

	m_dragging_to_item = -1;
	// trigger when user releases left button (drop)
	Connect(wxEVT_MOTION, wxMouseEventHandler(KeyListCtrl::OnDragging), NULL, this);
	Connect(wxEVT_LEFT_UP, wxMouseEventHandler(KeyListCtrl::OnEndDrag), NULL, this);
	Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(KeyListCtrl::OnEndDrag), NULL,this);
	SetCursor(wxCursor(wxCURSOR_WATCH));

	m_frame_text->Hide();
	m_duration_text->Hide();
	m_interpolation_cmb->Hide();
	m_description_text->Hide();
}

void KeyListCtrl::OnDragging(wxMouseEvent& event)
{
	wxPoint pos = event.GetPosition();
	int flags = wxLIST_HITTEST_ONITEM;
	long index = HitTest(pos, flags, NULL); // got to use it at last
	if (index >=0 && index != m_editing_item && index != m_dragging_to_item)
	{
		SetEvtHandlerEnabled(false);

		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (!vr_frame)
			return;
		Interpolator* interpolator = vr_frame->GetInterpolator();
		if (!interpolator)
			return;

		m_dragging_to_item = index;

		if (m_dragging_to_item - m_editing_item < -1)
			m_dragging_to_item = index;

		//change the content in the interpolator
		if (m_editing_item > m_dragging_to_item)
			interpolator->MoveKeyBefore(m_editing_item, m_dragging_to_item);
		else
			interpolator->MoveKeyAfter(m_editing_item, m_dragging_to_item);

		//DeleteItem(m_editing_item);
		//InsertItem(m_dragging_to_item, "", 0);
		UpdateText();

		m_editing_item = m_dragging_to_item;
		SetItemState(m_editing_item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

		SetEvtHandlerEnabled(true);
	}
}

void KeyListCtrl::OnEndDrag(wxMouseEvent& event)
{
	SetCursor(wxCursor(*wxSTANDARD_CURSOR));
	Disconnect(wxEVT_MOTION, wxMouseEventHandler(KeyListCtrl::OnDragging));
	Disconnect(wxEVT_LEFT_UP, wxMouseEventHandler(KeyListCtrl::OnEndDrag));
	Disconnect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(KeyListCtrl::OnEndDrag));
	m_dragging_to_item = -1;
}

void KeyListCtrl::OnScroll(wxScrollWinEvent& event)
{
	EndEdit(false);
	event.Skip(true);
}

void KeyListCtrl::OnScroll(wxMouseEvent& event)
{
	EndEdit(false);
	event.Skip(true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(RecorderDlg, wxPanel)
	EVT_BUTTON(ID_AutoKeyBtn, RecorderDlg::OnAutoKey)
	EVT_BUTTON(ID_SetKeyBtn, RecorderDlg::OnInsKey)
	EVT_BUTTON(ID_InsKeyBtn, RecorderDlg::OnInsKey)
	EVT_BUTTON(ID_DelKeyBtn, RecorderDlg::OnDelKey)
	EVT_BUTTON(ID_DelAllBtn, RecorderDlg::OnDelAll)
END_EVENT_TABLE()

RecorderDlg::RecorderDlg(wxWindow* frame, wxWindow* parent)
: wxPanel(parent, wxID_ANY,
wxPoint(500, 150), wxSize(450, 600),
0, "RecorderDlg"),
m_frame(frame),
m_view(0)
{
	SetEvtHandlerEnabled(false);
	Freeze();

#if defined(__WXGTK__)
	int duration_w = 38;
	int interp_w = 100;
#else
	int duration_w = 30;
	int interp_w = 65;
#endif

	//validator: integer
	wxIntegerValidator<unsigned int> vald_int;
	wxStaticText* st = 0;
	/*wxBoxSizer *group1 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(this, wxID_ANY, "Automatic Keys:");
	m_auto_key_cmb = new wxComboBox(this, ID_AutoKeyCmb, "",
		wxDefaultPosition, wxSize(180, 30), 0, NULL, wxCB_READONLY);
	m_auto_key_cmb->Append("Channel combination nC1");
	m_auto_key_cmb->Append("Channel combination nC2");
	m_auto_key_cmb->Append("Channel combination nC3");
	m_auto_key_cmb->Select(1);
	m_auto_key_btn = new wxButton(this, ID_AutoKeyBtn, "Generate",
		wxDefaultPosition, wxSize(75, 23));
	group1->Add(st, 0, wxALIGN_CENTER);
	group1->Add(5, 5);
	group1->Add(m_auto_key_cmb, 0, wxALIGN_CENTER);
	group1->Add(5, 5);
	group1->Add(m_auto_key_btn, 0, wxALIGN_CENTER);*/

	//list
	wxBoxSizer *group2 = new wxBoxSizer(wxVERTICAL);
	st = new wxStaticText(this, wxID_ANY, "Key Frames:");
	m_keylist = new KeyListCtrl(frame, this, wxID_ANY);
	group2->Add(st, 0, wxEXPAND);
	group2->Add(5, 5);
	group2->Add(m_keylist, 1, wxEXPAND);

	//default duration
	wxBoxSizer *group3 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(this, wxID_ANY, "Default:",wxDefaultPosition,wxSize(50,-1));
	m_duration_text = new wxTextCtrl(this, ID_DurationText, "30",
		wxDefaultPosition, wxSize(duration_w, 23), 0, vald_int);
	m_interpolation_cmb = new wxComboBox(this, ID_InterpolationCmb, "",
		wxDefaultPosition, wxSize(interp_w,-1), 0, NULL, wxCB_READONLY);
	m_interpolation_cmb->Append("Linear");
	m_interpolation_cmb->Append("Smooth");
	m_interpolation_cmb->Select(0);

	//key buttons
	//wxBoxSizer *group4 = new wxBoxSizer(wxHORIZONTAL);
	m_set_key_btn = new wxButton(this, ID_SetKeyBtn, "Add",
		wxDefaultPosition, wxSize(50, 23));
	m_del_key_btn = new wxButton(this, ID_DelKeyBtn, "Delete",
		wxDefaultPosition, wxSize(55, 23));
	m_del_all_btn = new wxButton(this, ID_DelAllBtn, "Del. All",
		wxDefaultPosition, wxSize(60, 23));

	group3->Add(st, 0, wxALIGN_CENTER);
	group3->Add(5, 5);
	group3->Add(m_duration_text, 0, wxALIGN_CENTER);
	group3->Add(5, 5);
	group3->Add(m_interpolation_cmb, 0, wxALIGN_CENTER);
	group3->AddStretchSpacer(1);
	group3->Add(m_set_key_btn, 0, wxALIGN_CENTER);
	group3->Add(5, 5);
	group3->Add(m_del_key_btn, 0, wxALIGN_CENTER);
	group3->Add(5, 5);
	group3->Add(m_del_all_btn, 0, wxALIGN_CENTER);

	//recoding
	wxBoxSizer* group4 = new wxBoxSizer(wxHORIZONTAL);
	m_vol_record_chk = new wxCheckBox(this, wxID_ANY, "Record volume properties");
	m_vol_record_chk->SetValue(false);
	group4->Add(m_vol_record_chk, 0, wxALIGN_CENTER);

/*
	m_seq_chk = new wxCheckBox(this,ID_SeqChk,
		"Time Sequence / Batch");
	m_time_start_text = new wxTextCtrl(this, ID_TimeStartText, "1",
		wxDefaultPosition,wxSize(35,-1));
	m_time_end_text = new wxTextCtrl(this, ID_TimeEndText, "10",
		wxDefaultPosition,wxSize(35,-1));
	st2 = new wxStaticText(this,wxID_ANY, "End: ",
		wxDefaultPosition,wxSize(-1,-1));
	//sizer 1
	sizer_1->Add(m_seq_chk, 0, wxALIGN_CENTER);
	sizer_1->AddStretchSpacer();
	st = new wxStaticText(this,wxID_ANY, "Current Time");
	sizer_1->Add(st, 0, wxALIGN_CENTER);
	sizer_1->Add(20,5,0);
	//sizer 2
	st = new wxStaticText(this,wxID_ANY, "Start: ",
		wxDefaultPosition,wxSize(-1,-1));
	sizer_2->Add(5,5,0);
	sizer_2->Add(st, 0, wxALIGN_CENTER);
	sizer_2->Add(m_time_start_text, 0, wxALIGN_CENTER);
	sizer_2->Add(15,5,0);
	sizer_2->Add(st2, 0, wxALIGN_CENTER);
	sizer_2->Add(m_time_end_text, 0, wxALIGN_CENTER);
	m_inc_time_btn = new wxButton(this, ID_IncTimeBtn, "",
		wxDefaultPosition, wxSize(30, 30));
	m_dec_time_btn = new wxButton(this, ID_DecTimeBtn, "",
		wxDefaultPosition, wxSize(30, 30));
	m_time_current_text = new wxTextCtrl(this, ID_CurrentTimeText, "0",
		wxDefaultPosition,wxSize(35,-1));
	m_inc_time_btn->SetBitmap(wxGetBitmapFromMemory(plus));
	m_dec_time_btn->SetBitmap(wxGetBitmapFromMemory(minus));
	sizer_2->AddStretchSpacer();
	sizer_2->Add(m_dec_time_btn, 0, wxALIGN_CENTER);
	sizer_2->Add(5,15,0);
	sizer_2->Add(m_time_current_text, 0, wxALIGN_CENTER);
	sizer_2->Add(5,15,0);
	sizer_2->Add(m_inc_time_btn, 0, wxALIGN_CENTER);
	sizer_2->Add(5,15,0);
*/

	//all controls
	wxBoxSizer *sizerV = new wxBoxSizer(wxVERTICAL);
	//sizerV->Add(10, 5);
	//sizerV->Add(group1, 0, wxEXPAND);
	sizerV->Add(10, 5);
	sizerV->Add(group2, 1, wxEXPAND);
	sizerV->Add(10, 5);
	sizerV->Add(group3, 0, wxEXPAND);
	sizerV->Add(5, 5);
	sizerV->Add(group4, 0, wxEXPAND);
	sizerV->Add(5, 5);

	SetSizer(sizerV);
	Layout();

	Thaw();
	SetEvtHandlerEnabled(true);
}

RecorderDlg::~RecorderDlg()
{
}

void RecorderDlg::GetSettings(VRenderView* vrv)
{
	m_view = vrv;
}

void RecorderDlg::SetSelection(int index)
{
	if (m_keylist)
	{
		long item = m_keylist->GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
		if (index != item && item != -1)
			m_keylist->SetItemState(index,
			wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	}
}

void RecorderDlg::OnAutoKey(wxCommandEvent &event)
{
	int sel = m_auto_key_cmb->GetSelection();

	switch (sel)
	{
	case 0://isolations
		AutoKeyChanComb(1);
		break;
	case 1://combination of two
		AutoKeyChanComb(2);
		break;
	case 2://combination of three
		AutoKeyChanComb(3);
		break;
	case 3://combination of all
		break;
	}

}

void RecorderDlg::OnSetKey(wxCommandEvent &event)
{
	wxString str = m_duration_text->GetValue();
	double duration;
	str.ToDouble(&duration);
	int interpolation = m_interpolation_cmb->GetSelection();
	InsertKey(-1, duration, interpolation);

	m_keylist->Update();
}

void RecorderDlg::OnInsKey(wxCommandEvent &event)
{
	wxString str;
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	Interpolator* interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;
	long item = m_keylist->GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	int index = -1;
	if (item != -1)
	{
		str = m_keylist->GetItemText(item);
		long id;
		str.ToLong(&id);
		index = interpolator->GetKeyIndex(id);
	}
	double duration = 0.0;
/*
	//check if 4D
	bool is_4d = false;
	bool is_3dbat = false;
	VolumeData* vd = 0;
	DataManager* mgr = vr_frame->GetDataManager();
	if (mgr)
	{
		for (int i = 0; i < mgr->GetVolumeNum(); i++)
		{
			vd = mgr->GetVolumeData(i);
			if (vd->GetReader() &&
				vd->GetReader()->GetTimeNum() > 1)
			{
				is_4d = true;
				break;
			}
			if (vd->GetReader() &&
				vd->GetReader()->GetBatchNum() > 1)
			{
				is_3dbat = true;
				break;
			}
		}
	}
	if (is_4d)
	{
		Interpolator *interpolator = vr_frame->GetInterpolator();
		if (interpolator && m_view)
		{
			double ct = vd->GetCurTime();
			KeyCode keycode;
			keycode.l0 = 1;
			keycode.l0_name = m_view->GetName();
			keycode.l1 = 2;
			keycode.l1_name = vd->GetName();
			keycode.l2 = 0;
			keycode.l2_name = "frame";
			double frame;
			if (interpolator->GetDouble(keycode, 
				interpolator->GetLastIndex(), frame))
				duration = fabs(ct - frame);
		}
	}
	if (is_3dbat)
	{
		Interpolator *interpolator = vr_frame->GetInterpolator();
		if (interpolator && m_view)
		{
			double ct = vd->GetCurTime();
			KeyCode keycode;
			keycode.l0 = 1;
			keycode.l0_name = m_view->GetName();
			keycode.l1 = 2;
			keycode.l1_name = vd->GetName();
			keycode.l2 = 0;
			keycode.l2_name = "batch";
			double batch;
			if (interpolator->GetDouble(keycode, 
				interpolator->GetLastIndex(), batch))
				duration = fabs(ct - batch);
		}
	}
	if (!is_4d && !is_3dbat)
	{*/
		str = m_duration_text->GetValue();
		str.ToDouble(&duration);
/*	}*/

	int interpolation = m_interpolation_cmb->GetSelection();
	InsertKey(index, duration, interpolation);

	m_keylist->Update();
	m_keylist->SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}

void RecorderDlg::InsertKey(int index, double duration, int interpolation)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	if (!m_view)
	{
		if (vr_frame && vr_frame->GetView(0))
			m_view = vr_frame->GetView(0);
		else
			return;
	}
    
	m_view->AddKeyFrame(duration, interpolation, m_vol_record_chk->GetValue());
}

bool RecorderDlg::MoveOne(vector<bool>& chan_mask, int lv)
{
	int i;
	int cur_lv = 0;
	int lv_pos = -1;
	for (i=(int)chan_mask.size()-1; i>=0; i--)
	{
		if (chan_mask[i])
		{
			cur_lv++;
			if (cur_lv == lv)
			{
				lv_pos = i;
				break;
			}
		}
	}
	if (lv_pos >= 0)
	{
		if (lv_pos == (int)chan_mask.size()-lv)
			return MoveOne(chan_mask, ++lv);
		else
		{
			if (!chan_mask[lv_pos+1])
			{
				for (i=lv_pos; i<(int)chan_mask.size(); i++)
				{
					if (i==lv_pos)
						chan_mask[i] = false;
					else if (i<=lv_pos+lv)
						chan_mask[i] = true;
					else
						chan_mask[i] = false;
				}
				return true;
			}
			else return false;//no space anymore
		}
	}
	else return false;
}

bool RecorderDlg::GetMask(vector<bool>& chan_mask)
{
	return MoveOne(chan_mask, 1);
}

void RecorderDlg::AutoKeyChanComb(int comb)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	if (!m_view)
	{
		if (vr_frame && vr_frame->GetView(0))
			m_view = vr_frame->GetView(0);
		else
			return;
	}

	DataManager* mgr = vr_frame->GetDataManager();
	if (!mgr)
		return;
	Interpolator *interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;

	wxString str = m_duration_text->GetValue();
	double duration;
	str.ToDouble(&duration);

	FLKeyCode keycode;
	FlKeyBoolean* flkeyB = 0;

	double t = interpolator->GetLastT();
	t = t<0.0?0.0:t;
	if (t>0.0) t += duration;

	int i;
	int numChan = m_view->GetAllVolumeNum();
	vector<bool> chan_mask;
	//initiate mask
	for (i=0; i<numChan; i++)
	{
		if (i < comb)
			chan_mask.push_back(true);
		else
			chan_mask.push_back(false);
	}

	do
	{
		interpolator->Begin(t);

		//for all volumes
		for (i=0; i<m_view->GetAllVolumeNum(); i++)
		{
			VolumeData* vd = m_view->GetAllVolumeData(i);
			keycode.l0 = 1;
			keycode.l0_name = m_view->GetName();
			keycode.l1 = 2;
			keycode.l1_name = vd->GetName();
			//display only
			keycode.l2 = 0;
			keycode.l2_name = "display";
			flkeyB = new FlKeyBoolean(keycode, chan_mask[i]);
			interpolator->AddKey(flkeyB);
		}

		interpolator->End();
		t += duration;
	} while (GetMask(chan_mask));

	m_keylist->Update();
}

void RecorderDlg::OnDelKey(wxCommandEvent &event)
{
	m_keylist->DeleteSel();
}

void RecorderDlg::OnDelAll(wxCommandEvent &event)
{
	m_keylist->DeleteAll();
}

//ch1
void RecorderDlg::OnCh1Check(wxCommandEvent &event)
{
	wxCheckBox* ch1 = (wxCheckBox*)event.GetEventObject();
	if (ch1)
		VRenderFrame::SetCompression(ch1->GetValue());
}

wxWindow* RecorderDlg::CreateExtraCaptureControl(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent, 0, wxDefaultPosition, wxSize(400, 90));

	wxBoxSizer *group1 = new wxStaticBoxSizer(
		new wxStaticBox(panel, wxID_ANY, "Additional Options"), wxVERTICAL);

	//compressed
	wxCheckBox* ch1 = new wxCheckBox(panel, wxID_HIGHEST+3004,
		"Lempel-Ziv-Welch Compression");
	ch1->Connect(ch1->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
		wxCommandEventHandler(RecorderDlg::OnCh1Check), NULL, panel);
	if (ch1)
		ch1->SetValue(VRenderFrame::GetCompression());

	//group
	group1->Add(10, 10);
	group1->Add(ch1);
	group1->Add(10, 10);

	panel->SetSizer(group1);
	panel->Layout();

	return panel;
}

void RecorderDlg::OnPreview(wxCommandEvent &event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	if (!m_view)
	{
		if (vr_frame && vr_frame->GetView(0))
			m_view = vr_frame->GetView(0);
		else
			return;
	}

	Interpolator *interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;

	wxString filename = "";
	int begin_frame = int(interpolator->GetFirstT());
	int end_frame = int(interpolator->GetLastT());
	m_view->SetParamCapture(filename, begin_frame, end_frame, true);

}

void RecorderDlg::OnReset(wxCommandEvent &event)
{
	if (m_view)
	{
		m_view->m_glview->SetParams(0);
		m_view->RefreshGL();
	}
}

void RecorderDlg::OnPlay(wxCommandEvent &event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	if (!m_view)
	{
		if (vr_frame && vr_frame->GetView(0))
			m_view = vr_frame->GetView(0);
		else
			return;
	}

	Interpolator *interpolator = vr_frame->GetInterpolator();
	if (!interpolator)
		return;

	wxFileDialog *fopendlg = new wxFileDialog(
		m_frame, "Save Movie Sequence", 
		"", "", "*.tif", wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
	fopendlg->SetExtraControlCreator(CreateExtraCaptureControl);

	int rval = fopendlg->ShowModal();
	if (rval == wxID_OK)
	{
		wxString filename = fopendlg->GetPath();
		int begin_frame = int(interpolator->GetFirstT());
		int end_frame = int(interpolator->GetLastT());
		m_view->SetParamCapture(filename, begin_frame, end_frame, false);

		if (vr_frame->GetSettingDlg() &&
			vr_frame->GetSettingDlg()->GetProjSave())
		{
			wxString new_folder;
			new_folder = filename + "_project";
			CREATE_DIR(new_folder.fn_str());
			wxString prop_file = new_folder + "/" + fopendlg->GetFilename() + "_project.vrp";
			vr_frame->SaveProject(prop_file);
		}
	}

	delete fopendlg;
}

void RecorderDlg::OnStop(wxCommandEvent &event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;
	if (!m_view)
	{
		if (vr_frame && vr_frame->GetView(0))
			m_view = vr_frame->GetView(0);
		else
			return;
	}
	m_view->StopMovie();
}
