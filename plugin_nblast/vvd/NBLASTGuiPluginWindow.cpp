
// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "NBLASTGuiPlugin.h"
#include "NBLASTGuiPluginWindow.h"
#include "VRenderFrame.h"
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/tokenzr.h>
#include <wx/dcbuffer.h>
#include <wx/valnum.h>
#include <wx/filename.h>
#include <wx/statline.h>
#include "Formats/png_resource.h"
#include "img/icons.h"


wxUsrPwdDialog::wxUsrPwdDialog(wxWindow* parent, wxWindowID id, const wxString &title,
								const wxPoint &pos, const wxSize &size, long style)
: wxDialog (parent, id, title, pos, size, style)
{
	#ifdef _WIN32
    int stsize = 60;
#else
    int stsize = 70;
#endif

	SetEvtHandlerEnabled(false);
	Freeze();

	wxBoxSizer* itemBoxSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer *sizer1 = new wxBoxSizer(wxHORIZONTAL);

	sizer1->Add(5, 5);
	wxStaticText* st = new wxStaticText( this, wxID_STATIC, _("User:"), wxDefaultPosition, wxSize(stsize, -1), 0 );
	sizer1->Add(st, 0, wxALIGN_LEFT|wxLEFT|wxRIGHT|wxTOP, 5);
	m_usrtxt = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxSize(200, -1));
	sizer1->Add(m_usrtxt, 0, wxALIGN_LEFT|wxLEFT|wxRIGHT|wxTOP, 5);

	itemBoxSizer->Add(10, 5);
	itemBoxSizer->Add(sizer1, 0, wxALIGN_CENTER);

	wxBoxSizer *sizer2 = new wxBoxSizer(wxHORIZONTAL);
	sizer2->Add(5, 5);
	st = new wxStaticText( this, wxID_STATIC, _("Password:"), wxDefaultPosition, wxSize(stsize, -1), 0 );
	sizer2->Add(st, 0, wxALIGN_LEFT|wxLEFT|wxRIGHT|wxTOP, 5);
	m_pwdtxt = new wxTextCtrl( this, wxID_ANY, "", wxDefaultPosition, wxSize(200, -1), wxTE_PASSWORD);
	sizer2->Add(m_pwdtxt, 0, wxALIGN_LEFT|wxLEFT|wxRIGHT|wxTOP, 5);

	itemBoxSizer->Add(5, 5);
	itemBoxSizer->Add(sizer2, 1, wxALIGN_CENTER);

	wxBoxSizer *sizerb = new wxBoxSizer(wxHORIZONTAL);
	wxButton *b = new wxButton(this, wxID_OK, _("OK"), wxDefaultPosition, wxDefaultSize);
	wxButton *c = new wxButton(this, wxID_CANCEL, _("Cancel"), wxDefaultPosition, wxDefaultSize);
	sizerb->Add(5,10);
    sizerb->Add(b);
    sizerb->Add(5,10);
	sizerb->Add(c);
	sizerb->Add(10,10);
	itemBoxSizer->Add(10, 10);
	itemBoxSizer->Add(sizerb, 0, wxALIGN_RIGHT);
	itemBoxSizer->Add(10, 10);
	
	SetSizer(itemBoxSizer);

	Thaw();
	SetEvtHandlerEnabled(true);
}

wxString wxUsrPwdDialog::GetUserNameText()
{
	return m_usrtxt ? m_usrtxt->GetValue() : wxString();
}

wxString wxUsrPwdDialog::GetPasswordText()
{
	return m_pwdtxt ? m_pwdtxt->GetValue() : wxString();
}

void wxUsrPwdDialog::SetUserNameText(wxString usr)
{
	m_usrtxt->SetValue(usr);
}

void wxUsrPwdDialog::SetPasswordText(wxString pwd)
{
	m_pwdtxt->SetValue(pwd);
}

BEGIN_EVENT_TABLE(NBLASTDatabaseListCtrl, wxListCtrl)
	EVT_LIST_ITEM_DESELECTED(wxID_ANY, NBLASTDatabaseListCtrl::OnEndSelection)
	EVT_LIST_ITEM_SELECTED(wxID_ANY, NBLASTDatabaseListCtrl::OnSelection)
	EVT_LIST_ITEM_ACTIVATED(wxID_ANY, NBLASTDatabaseListCtrl::OnAct)
	EVT_TEXT(ID_NameDispText, NBLASTDatabaseListCtrl::OnNameDispText)
	EVT_TEXT_ENTER(ID_NameDispText, NBLASTDatabaseListCtrl::OnEnterInTextCtrl)
	EVT_TEXT(ID_DescriptionText, NBLASTDatabaseListCtrl::OnPathText)
	EVT_TEXT_ENTER(ID_DescriptionText, NBLASTDatabaseListCtrl::OnEnterInTextCtrl)
	EVT_KEY_DOWN(NBLASTDatabaseListCtrl::OnKeyDown)
	EVT_KEY_UP(NBLASTDatabaseListCtrl::OnKeyUp)
	EVT_LIST_BEGIN_DRAG(wxID_ANY, NBLASTDatabaseListCtrl::OnBeginDrag)
	EVT_LIST_COL_DRAGGING(wxID_ANY, NBLASTDatabaseListCtrl::OnColumnSizeChanged)
	EVT_SCROLLWIN(NBLASTDatabaseListCtrl::OnScroll)
	EVT_MOUSEWHEEL(NBLASTDatabaseListCtrl::OnScroll)
	EVT_LEFT_DCLICK(NBLASTDatabaseListCtrl::OnLeftDClick)
	EVT_SIZE(NBLASTDatabaseListCtrl::OnResize)
	EVT_BUTTON(ID_Setting, NBLASTDatabaseListCtrl::OnSettingButton)
END_EVENT_TABLE()

NBLASTDatabaseListCtrl::NBLASTDatabaseListCtrl(
	wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style) :
wxListCtrl(parent, id, pos, size, style),
	//m_frame(frame),
	m_editing_item(-1),
	m_dragging_to_item(-1),
	m_dragging_item(-1)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	wxListItem itemCol;
	itemCol.SetText("Name");
	this->InsertColumn(0, itemCol);
	SetColumnWidth(0, 150);
	itemCol.SetText("Path");
	this->InsertColumn(1, itemCol);
	SetColumnWidth(1, 300);
	itemCol.SetText("");
	this->InsertColumn(2, itemCol);
	SetColumnWidth(2, 50);
	
	//frame edit
	m_name_disp = new wxTextCtrl(this, ID_NameDispText, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_name_disp->Hide();

	m_path_disp = new wxFilePickerCtrl(this, ID_DescriptionText, "", _("Choose a target neuron library"), "*.rds", wxDefaultPosition, wxDefaultSize, wxFLP_DEFAULT_STYLE|wxFLP_SMALL);
	m_path_disp->Hide();

	m_setting_b = new wxButton(this, ID_Setting, _("Setting"), wxDefaultPosition, wxDefaultSize);
	m_setting_b->Hide();

	Thaw();
	SetEvtHandlerEnabled(true);
}

NBLASTDatabaseListCtrl::~NBLASTDatabaseListCtrl()
{
}

void NBLASTDatabaseListCtrl::Append(wxString path, wxString name)
{
	wxString nstr;
	if (name.IsEmpty())
	{
		wxFileName fn(path);
		nstr = fn.GetName();
	}
	else
		nstr = name;
	long tmp = InsertItem(GetItemCount(), nstr);
	SetItem(tmp, 1, path);
}

void NBLASTDatabaseListCtrl::Add(wxString path, wxString name, bool selection)
{
	Append(path, name);
	wxString newname = GetText(GetItemCount()-1, 0);
	NBLASTDBListItemData data;
	data.name = newname;
	data.path = path;
	data.state = false;
	m_list.push_back(data);
	if (selection) SetItemState(GetItemCount()-1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	SetColumnWidthAuto();
}

void NBLASTDatabaseListCtrl::SetColumnWidthAuto()
{
	wxSize sz = GetSize();
	int w1 = GetColumnWidth(0);
	int w3 = GetColumnWidth(2);
	SetColumnWidth(1, sz.GetWidth()-5-w1-w3);
}

void NBLASTDatabaseListCtrl::OnResize(wxSizeEvent& event)
{
	SetColumnWidthAuto();
	event.Skip();
}

void NBLASTDatabaseListCtrl::UpdateList()
{
	m_name_disp->Hide();
	m_path_disp->Hide();
	m_setting_b->Hide();
    m_editing_item = -1;

	DeleteAllItems();

	for (int i=0; i<(int)m_list.size(); i++)
		Append(m_list[i].path, m_list[i].name);
	SetColumnWidthAuto();
}

void NBLASTDatabaseListCtrl::UpdateText()
{
	m_name_disp->Hide();
	m_path_disp->Hide();
	m_setting_b->Hide();
	m_editing_item = -1;

	for (int i=0; i<(int)m_list.size(); i++)
	{
		SetText(i, 0, m_list[i].name);
		SetText(i, 1, m_list[i].path);
	}
	SetColumnWidthAuto();
}

void NBLASTDatabaseListCtrl::DeleteSelection()
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item != -1 && item < m_list.size())
	{
		m_list.erase(m_list.begin()+item);
		UpdateList();
	}
}

void NBLASTDatabaseListCtrl::DeleteAll()
{
	m_list.clear();
	UpdateList();
}

wxString NBLASTDatabaseListCtrl::GetText(long item, int col)
{
	wxListItem info;
	info.SetId(item);
	info.SetColumn(col);
	info.SetMask(wxLIST_MASK_TEXT);
	GetItem(info);
	return info.GetText();
}

void NBLASTDatabaseListCtrl::SetText(long item, int col, const wxString &str)
{
	wxListItem info;
	info.SetId(item);
	info.SetColumn(col);
	info.SetMask(wxLIST_MASK_TEXT);
	GetItem(info);
	info.SetText(str);
	SetItem(info);
}

void NBLASTDatabaseListCtrl::ShowTextCtrls(long item)
{
	if (item != -1)
	{
		wxRect rect;
		wxString str;
		//add frame text
		GetSubItemRect(item, 0, rect);
		str = GetText(item, 0);
		rect.SetLeft(rect.GetLeft());
		rect.SetRight(rect.GetRight());
		m_name_disp->SetPosition(rect.GetTopLeft());
		m_name_disp->SetSize(rect.GetSize());
		m_name_disp->SetValue(str);
		m_name_disp->Show();

		int c0w = rect.GetWidth();
		GetSubItemRect(item, 2, rect);
		int c2w = rect.GetWidth();
		//add description text
		GetSubItemRect(item, 1, rect);
		str = GetText(item, 1);
		wxSize sz = rect.GetSize();
		int win_w = GetSize().GetWidth();
		sz.SetWidth(win_w-c0w-c2w-5); 
		m_path_disp->SetPosition(rect.GetTopLeft());
		m_path_disp->SetSize(sz);
		m_path_disp->SetPath(str);
		m_path_disp->Show();

		GetSubItemRect(item, 2, rect);
		rect.SetLeft(rect.GetLeft());
		rect.SetRight(rect.GetRight());
		m_setting_b->SetPosition(rect.GetTopLeft());
		m_setting_b->SetSize(rect.GetSize());
		m_setting_b->Show();
	}
}

void NBLASTDatabaseListCtrl::OnSelection(wxListEvent &event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	m_editing_item = item;
	if (item != -1 && !m_name_disp->IsShown())
	{
		wxRect rect;
		GetSubItemRect(item, 2, rect);
		rect.SetLeft(rect.GetLeft());
		rect.SetRight(rect.GetRight());
		m_setting_b->SetPosition(rect.GetTopLeft());
		m_setting_b->SetSize(rect.GetSize());
		m_setting_b->Show();
	}
}

void NBLASTDatabaseListCtrl::OnAct(wxListEvent &event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	m_editing_item = item;
	if (item != -1 && !m_name_disp->IsShown())
	{
		//wxPoint pos = this->ScreenToClient(::wxGetMousePosition());
		ShowTextCtrls(item);
		m_name_disp->SetFocus();
	}
}

void NBLASTDatabaseListCtrl::OnLeftDClick(wxMouseEvent& event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	m_editing_item = item;
	if (item != -1)
	{
		wxPoint pos = event.GetPosition();
		ShowTextCtrls(item);
		wxRect rect;
		GetSubItemRect(item, 1, rect);
		if (rect.Contains(pos))
			m_path_disp->SetFocus();
		else
			m_name_disp->SetFocus();
	}
}

void NBLASTDatabaseListCtrl::EndEdit()
{
	if (m_name_disp->IsShown())
	{
		m_name_disp->Hide();
		
		if (m_editing_item >= 0 && m_dragging_to_item == -1){
			
			if(m_editing_item < m_list.size())
			{
				wxString str = m_name_disp->GetValue();
				m_list[m_editing_item].name = str;
				str = m_path_disp->GetPath();
				m_list[m_editing_item].path = str;
				SetItemState(m_editing_item, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED);
				m_editing_item = -1;
			}
		}

		UpdateText();
	}
}

void NBLASTDatabaseListCtrl::OnEndSelection(wxListEvent &event)
{
	EndEdit();
}

void NBLASTDatabaseListCtrl::OnEnterInTextCtrl(wxCommandEvent& event)
{
	if (m_editing_item >= 0) SetItemState(m_editing_item, 0, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED);
	EndEdit();
}

void NBLASTDatabaseListCtrl::OnNameDispText(wxCommandEvent& event)
{
	if (m_editing_item == -1)
		return;

	wxString str = m_name_disp->GetValue();

	SetText(m_editing_item, 0, str);
}

void NBLASTDatabaseListCtrl::OnPathText(wxCommandEvent& event)
{
	if (m_editing_item == -1)
		return;

	wxString str = m_path_disp->GetPath();

	SetText(m_editing_item, 1, str);
}

void NBLASTDatabaseListCtrl::OnBeginDrag(wxListEvent& event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item ==-1)
		return;

	m_editing_item = -1;
	m_dragging_item = item;
	m_dragging_to_item = -1;
	// trigger when user releases left button (drop)
	Connect(wxEVT_MOTION, wxMouseEventHandler(NBLASTDatabaseListCtrl::OnDragging), NULL, this);
	Connect(wxEVT_LEFT_UP, wxMouseEventHandler(NBLASTDatabaseListCtrl::OnEndDrag), NULL, this);
	Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(NBLASTDatabaseListCtrl::OnEndDrag), NULL,this);
	SetCursor(wxCursor(wxCURSOR_WATCH));

	m_name_disp->Hide();
	m_path_disp->Hide();
}

void NBLASTDatabaseListCtrl::OnDragging(wxMouseEvent& event)
{
	wxPoint pos = event.GetPosition();
	int flags = wxLIST_HITTEST_ONITEM;
	long index = HitTest(pos, flags, NULL); // got to use it at last
	if (index >=0 && index != m_dragging_item && index != m_dragging_to_item)
	{
		m_dragging_to_item = index;

		//change the content in the ruler list
		swap(m_list[m_dragging_item], m_list[m_dragging_to_item]);
		
		DeleteItem(m_dragging_item);
		InsertItem(m_dragging_to_item, "", 0);

		//Update();
		UpdateText();
		m_dragging_item = m_dragging_to_item;
		
		SetEvtHandlerEnabled(false);
		Freeze();
		SetItemState(m_dragging_item, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED);
		Thaw();
		SetEvtHandlerEnabled(true);
	}
}

void NBLASTDatabaseListCtrl::OnEndDrag(wxMouseEvent& event)
{
	SetCursor(wxCursor(*wxSTANDARD_CURSOR));
	Disconnect(wxEVT_MOTION, wxMouseEventHandler(NBLASTDatabaseListCtrl::OnDragging));
	Disconnect(wxEVT_LEFT_UP, wxMouseEventHandler(NBLASTDatabaseListCtrl::OnEndDrag));
	Disconnect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(NBLASTDatabaseListCtrl::OnEndDrag));
	m_dragging_to_item = -1;
}

void NBLASTDatabaseListCtrl::OnScroll(wxScrollWinEvent& event)
{
	EndEdit();
	event.Skip(true);
}

void NBLASTDatabaseListCtrl::OnScroll(wxMouseEvent& event)
{
	EndEdit();
	event.Skip(true);
}

void NBLASTDatabaseListCtrl::OnKeyDown(wxKeyEvent& event)
{
	if ( event.GetKeyCode() == WXK_DELETE ||
		event.GetKeyCode() == WXK_BACK)
		DeleteSelection();
	event.Skip();
}

void NBLASTDatabaseListCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void NBLASTDatabaseListCtrl::OnColumnSizeChanged(wxListEvent &event)
{
	if (m_editing_item == -1)
		return;

	wxRect rect;
	wxString str;
	GetSubItemRect(m_editing_item, 0, rect);
	rect.SetLeft(rect.GetLeft());
	rect.SetRight(rect.GetRight());
	m_name_disp->SetPosition(rect.GetTopLeft());
	m_name_disp->SetSize(rect.GetSize());
	
	int c0w = rect.GetWidth();
	GetSubItemRect(m_editing_item, 2, rect);
	int c2w = rect.GetWidth();
	GetSubItemRect(m_editing_item, 1, rect);
	wxSize sz = rect.GetSize();
	int win_w = GetSize().GetWidth();
	sz.SetWidth(win_w-c0w-c2w-5);
	m_path_disp->SetPosition(rect.GetTopLeft());
	m_path_disp->SetSize(sz);

	GetSubItemRect(m_editing_item, 2, rect);
	rect.SetLeft(rect.GetLeft());
	rect.SetRight(rect.GetRight());
	m_setting_b->SetPosition(rect.GetTopLeft());
	m_setting_b->SetSize(rect.GetSize());
}

void NBLASTDatabaseListCtrl::OnSettingButton(wxCommandEvent &event)
{
	wxUsrPwdDialog updlg(this, wxID_ANY, "Set User/Password", wxDefaultPosition, wxSize(350, 150));
	long item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item >= 0 && item < m_list.size())
	{
		updlg.SetUserNameText(m_list[item].usr);
		updlg.SetPasswordText(m_list[item].pwd);
	}
	
	if (updlg.ShowModal() == wxID_OK)
	{
		if (item >= 0 && item < m_list.size())
		{
			wxString usr = updlg.GetUserNameText();
			wxString pwd = updlg.GetPasswordText();
			m_list[item].usr = usr;
			m_list[item].pwd = pwd;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE( wxDBListDialog, wxDialog )
    EVT_BUTTON( ID_AddButton, wxDBListDialog::OnAddButtonClick )
	EVT_BUTTON( wxID_OK, wxDBListDialog::OnOk )
END_EVENT_TABLE()

wxDBListDialog::wxDBListDialog(wxWindow* parent, wxWindowID id, const wxString &title,
								const wxPoint &pos, const wxSize &size, long style)
: wxDialog (parent, id, title, pos, size, style)
{
	#ifdef _WIN32
    int stsize = 120;
#else
    int stsize = 130;
#endif

	SetEvtHandlerEnabled(false);
	Freeze();

	wxBoxSizer* itemBoxSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *sizer1 = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText *st = new wxStaticText(this, 0, "NBLAST Database (.rds):", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	m_new_db_pick = new wxFilePickerCtrl(this, ID_NewDBPick, "", _("Choose a NBLAST database"), "*.rds", wxDefaultPosition, wxSize(400, -1));
	sizer1->Add(5, 10);
	sizer1->Add(st, 0, wxALIGN_CENTER_VERTICAL);
	sizer1->Add(5, 10);
	sizer1->Add(m_new_db_pick, 1, wxRIGHT|wxEXPAND);
	sizer1->Add(10, 10);
	itemBoxSizer->Add(5, 5);
	itemBoxSizer->Add(sizer1, 0, wxEXPAND);
		
	m_add_button = new wxButton( this, ID_AddButton, _("Add Database"), wxDefaultPosition, wxDefaultSize, 0 );
	itemBoxSizer->Add(10, 5);
	itemBoxSizer->Add(m_add_button, 0, wxALIGN_CENTER_HORIZONTAL, 5);

	wxBoxSizer *sizerl = new wxBoxSizer(wxHORIZONTAL);
	m_list = new NBLASTDatabaseListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(590, 500));
    sizerl->Add(5,10);
    sizerl->Add(m_list, 1, wxEXPAND);
    sizerl->Add(5,10);
	itemBoxSizer->Add(5, 5);
	itemBoxSizer->Add(sizerl, 1, wxEXPAND);

	wxBoxSizer *sizerb = new wxBoxSizer(wxHORIZONTAL);
	wxButton *b = new wxButton(this, wxID_OK, _("OK"), wxDefaultPosition, wxDefaultSize);
	wxButton *c = new wxButton(this, wxID_CANCEL, _("Cancel"), wxDefaultPosition, wxDefaultSize);
	sizerb->Add(5,10);
    sizerb->Add(b);
    sizerb->Add(5,10);
	sizerb->Add(c);
	sizerb->Add(10,10);
	itemBoxSizer->Add(10, 10);
	itemBoxSizer->Add(sizerb, 0, wxALIGN_RIGHT);
	itemBoxSizer->Add(10, 10);
	
	SetSizer(itemBoxSizer);

	LoadList();
	
	Thaw();
	SetEvtHandlerEnabled(true);
}

void wxDBListDialog::LoadList()
{
	if (!m_list)
		return;

	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(), NULL);
#ifdef _DARWIN
	wxString listpath = expath + "/../Resources/NBLAST_database_list.txt";
#else
	wxString listpath = expath + GETSLASHS() + "NBLAST_database_list.txt";
	if (!wxFileExists(listpath))
		listpath = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "NBLAST_database_list.txt";
#endif
	if (wxFileExists(listpath))
	{
		m_list->DeleteAll();
		wxFileInputStream is(listpath);
		wxTextInputStream text(is);
		while(is.IsOk() && !is.Eof() )
		{
			wxString line = text.ReadLine();
			wxStringTokenizer tkz(line, wxT("\t"));

			wxArrayString tokens;
			while(tkz.HasMoreTokens())
				tokens.Add(tkz.GetNextToken());

			if (tokens.Count() == 1)
				m_list->Add(tokens[0]);
			if (tokens.Count() >= 2)
				m_list->Add(tokens[1], tokens[0]);
			if (tokens.Count() >= 3 && tokens[2] == "true")
				m_list->SetState(m_list->GetItemCount()-1, true);
			if (tokens.Count() >= 5)
				m_list->SetUsrPwd(m_list->GetItemCount()-1, tokens[3], tokens[4]);
		}
	}
}

void wxDBListDialog::SaveList()
{
	if (!m_list)
		return;

	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
	wxString listpath = expath + "/../Resources/NBLAST_database_list.txt";
#else
	wxString listpath = expath + GETSLASHS() + "NBLAST_database_list.txt";
	wxString listpath2 = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "NBLAST_database_list.txt";
	if (!wxFileExists(listpath) && wxFileExists(listpath2))
		listpath = listpath2;
#endif

	wxFileOutputStream os(listpath);
	wxTextOutputStream tos(os);
	vector<NBLASTDBListItemData> list = m_list->getList();
	if (list.size() > 0)
	{
		for (int i = 0; i < list.size()-1; i++)
			tos << list[i].name << "\t" << list[i].path << "\t" << (list[i].state ? "true" : "false") << "\t" << list[i].usr << "\t" << list[i].pwd << endl;
		tos << list[list.size()-1].name << "\t" << list[list.size()-1].path << "\t" << (list[list.size()-1].state ? "true" : "false") << "\t" << list[list.size()-1].usr << "\t" << list[list.size()-1].pwd;
	}
}

void wxDBListDialog::OnAddButtonClick( wxCommandEvent& event )
{
	if (m_list && m_new_db_pick)
		m_list->Add(m_new_db_pick->GetPath(), wxString(), true);
}

void wxDBListDialog::OnOk( wxCommandEvent& event )
{
	SaveList();
	event.Skip();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(wxImagePanel, wxPanel)
EVT_PAINT(wxImagePanel::OnDraw)
EVT_SIZE(wxImagePanel::OnSize)
END_EVENT_TABLE()

wxImagePanel::wxImagePanel(wxWindow* parent, int w, int h) :
wxPanel(parent)
{
	m_orgimage = NULL;
	m_resized = NULL;
	m_w = -1;
	m_h = -1;
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

wxImagePanel::~wxImagePanel()
{
	wxDELETE(m_orgimage);
	wxDELETE(m_resized);
	m_image.Destroy();
	m_olimage.Destroy();
}

void wxImagePanel::SetImage(wxString file, wxBitmapType format)
{
	wxDELETE(m_orgimage);
	wxDELETE(m_resized);
	m_image.Destroy();
	if (m_olimage.IsOk())
		m_olimage.Destroy();
	if (m_bgimage.IsOk())
		m_bgimage.Destroy();

	if (!wxFileExists(file))
		return;

	m_orgimage = new wxImage(file, format);
	if (m_orgimage && m_orgimage->IsOk())
	{
		m_resized = new wxBitmap();
		m_image = m_orgimage->Copy();
	}
	else
		wxDELETE(m_orgimage);
}

void wxImagePanel::SetOverlayImage(wxString file, wxBitmapType format, bool show)
{
	if (!wxFileExists(file) || !m_orgimage || !m_orgimage->IsOk())
		return;

	m_olimage.Destroy();
	m_olimage.LoadFile(file, format);
	if (!m_olimage.IsOk())
		return;
}

void wxImagePanel::SetBackgroundImage(wxString file, wxBitmapType format)
{
	if (!wxFileExists(file) || !m_orgimage || !m_orgimage->IsOk())
		return;

	m_bgimage.Destroy();
	m_bgimage.LoadFile(file, format);
	if (!m_bgimage.IsOk())
		return;
}

void wxImagePanel::UpdateImage(bool ov_show)
{
	if (!m_orgimage || !m_orgimage->IsOk())
		return;

	if (!m_olimage.IsOk())
		return;

	int w = m_orgimage->GetSize().GetWidth();
	int h = m_orgimage->GetSize().GetHeight();

	if (m_olimage.IsOk() && m_olimage.GetSize() != m_orgimage->GetSize())
		m_olimage = m_olimage.Scale( w, h, wxIMAGE_QUALITY_HIGH);
	if (m_bgimage.IsOk() && m_bgimage.GetSize() != m_orgimage->GetSize())
		m_bgimage = m_bgimage.Scale( w, h, wxIMAGE_QUALITY_HIGH);

	m_image = m_orgimage->Copy();

	if (m_bgimage.IsOk())
	{
		unsigned char *bgdata = m_bgimage.GetData();
		unsigned char *imdata = m_image.GetData();

		double alpha = 0.6;
		for (int y = 0; y < h; y++)
		{
			for(int x = 0; x < w; x++)
			{
				if (imdata[(y*w+x)*3] == 0 && imdata[(y*w+x)*3+1] == 0 && imdata[(y*w+x)*3+2] == 0)
				{
					imdata[(y*w+x)*3]   = bgdata[(y*w+x)*3];
					imdata[(y*w+x)*3+1] = bgdata[(y*w+x)*3+1];
					imdata[(y*w+x)*3+2] = bgdata[(y*w+x)*3+2];
				}
			}
		}
	}
	
	if (m_olimage.IsOk() && ov_show)
	{
		unsigned char *oldata = m_olimage.GetData();
		unsigned char *imdata = m_image.GetData();

		double alpha = 0.6;
		double alpha2 = 0.8;

		m_olimage.Blur(1);

		for (int y = 0; y < h; y++)
		{
			for(int x = 0; x < w; x++)
			{
				if (oldata[(y*w+x)*3] >= 64)
				{
					imdata[(y*w+x)*3]   = (unsigned char)(alpha*255.0 + (1.0-alpha)*(double)imdata[(y*w+x)*3]);
					imdata[(y*w+x)*3+1] = (unsigned char)(alpha*0.0   + (1.0-alpha)*(double)imdata[(y*w+x)*3+1]);
					imdata[(y*w+x)*3+2] = (unsigned char)(alpha*0.0   + (1.0-alpha)*(double)imdata[(y*w+x)*3+2]);
				}
				else if (oldata[(y*w+x)*3] > 0)
				{
					imdata[(y*w+x)*3]   = (unsigned char)(alpha2*0.0 + (1.0-alpha2)*(double)imdata[(y*w+x)*3]);
					imdata[(y*w+x)*3+1] = (unsigned char)(alpha2*0.0 + (1.0-alpha2)*(double)imdata[(y*w+x)*3+1]);
					imdata[(y*w+x)*3+2] = (unsigned char)(alpha2*0.0 + (1.0-alpha2)*(double)imdata[(y*w+x)*3+2]);
				}
			}
		}
	}
}

void wxImagePanel::ResetImage()
{
	if (!m_orgimage || !m_orgimage->IsOk())
		return;

	m_image = m_orgimage->Copy();
}

void wxImagePanel::OnDraw(wxPaintEvent & evt)
{
	wxAutoBufferedPaintDC dc(this);
	Render(dc);
}

void wxImagePanel::PaintNow()
{
	wxClientDC dc(this);
	Render(dc);
}

wxSize wxImagePanel::CalcImageSizeKeepAspectRatio(int w, int h)
{
	if (!m_orgimage || !m_orgimage->IsOk())
		return wxSize(-1, -1);

	int orgw = m_orgimage->GetWidth();
	int orgh = m_orgimage->GetHeight();

	double nw = w;
	double nh = (double)w*((double)orgh/(double)orgw);
	if (nh <= h)
		return wxSize((int)nw, (int)nh);

	nw = (double)h*((double)orgw/(double)orgh);
	nh = h;
	return wxSize((int)nw, (int)nh);
}

double wxImagePanel::GetAspectRatio()
{
	if (!m_orgimage || !m_orgimage->IsOk())
		return 0.0;
	
	int orgw = m_orgimage->GetWidth();
	int orgh = m_orgimage->GetHeight();

	return orgh != 0.0 ? orgw/orgh : 0.0;
}

void wxImagePanel::Render(wxDC& dc)
{
	dc.Clear();

	if (!m_image.IsOk())
		return;

	int neww, newh;
	this->GetSize( &neww, &newh );

	wxSize ns = CalcImageSizeKeepAspectRatio(neww, newh);
	if (ns.GetWidth() <= 0 || ns.GetHeight() <= 0)
		return;

	int posx = neww > ns.GetWidth() ? (neww-ns.GetWidth())/2 : 0;
	int posy = newh > ns.GetHeight() ? (newh-ns.GetHeight())/2 : 0;

	wxDELETE(m_resized);
	m_resized = new wxBitmap( m_image.Scale( ns.GetWidth(), ns.GetHeight(), wxIMAGE_QUALITY_HIGH));
	if (m_resized && m_resized->IsOk())
	{
		m_w = ns.GetWidth();
		m_h = ns.GetHeight();
		dc.DrawBitmap(*m_resized, posx, posy, false);
	}
}

void wxImagePanel::OnEraseBackground(wxEraseEvent& event)
{
	event.Skip();
}

void wxImagePanel::OnSize(wxSizeEvent& event)
{
	Refresh(true);
	event.Skip();
}


BEGIN_EVENT_TABLE(NBLASTListCtrl, wxListCtrl)
	EVT_LIST_ITEM_SELECTED(wxID_ANY, NBLASTListCtrl::OnSelect)
	EVT_LIST_COL_BEGIN_DRAG(wxID_ANY, NBLASTListCtrl::OnColBeginDrag)
	EVT_KEY_DOWN(NBLASTListCtrl::OnKeyDown)
	EVT_KEY_UP(NBLASTListCtrl::OnKeyUp)
	EVT_MOUSE_EVENTS(NBLASTListCtrl::OnMouse)
	EVT_LEFT_DCLICK(NBLASTListCtrl::OnLeftDClick)
END_EVENT_TABLE()

NBLASTListCtrl::NBLASTListCtrl(
	wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style) :
	wxListCtrl(parent, id, pos, size, style)
{
	m_images = NULL;
	m_history_pos = 0;

	wxListItem itemCol;
	itemCol.SetText("");
	this->InsertColumn(0, itemCol);

	itemCol.SetText("");
	this->InsertColumn(1, itemCol);

	itemCol.SetText("Name");
	this->InsertColumn(2, itemCol);

	itemCol.SetText("Database");
	this->InsertColumn(3, itemCol);

	itemCol.SetText("Skeleton");
	this->InsertColumn(4, itemCol);

	itemCol.SetText("Volume Data");
	this->InsertColumn(5, itemCol);
	
	itemCol.SetText("Score");
	itemCol.SetAlign(wxLIST_FORMAT_RIGHT);
	this->InsertColumn(6, itemCol);

	SetColumnWidth(0, 0);
	SetColumnWidth(1, 0);
	SetColumnWidth(2, 110);
	SetColumnWidth(3, 100);
	SetColumnWidth(4, 150);
	SetColumnWidth(5, 150);
	SetColumnWidth(6, 90);
}

NBLASTListCtrl::~NBLASTListCtrl()
{
	wxDELETE(m_images);
}

void NBLASTListCtrl::LoadResults(wxString csvfilepath)
{
	wxFileInputStream input(csvfilepath);
	wxTextInputStream text(input, wxT("\t"), wxConvUTF8 );
	SetEvtHandlerEnabled(false);

	DeleteAllItems();
	if (!m_dbdirs.IsEmpty()) m_dbdirs.Clear();
	if (!m_dbpaths.IsEmpty()) m_dbpaths.Clear();
	if (!m_dbnames.IsEmpty()) m_dbnames.Clear();
	if (!m_listdata.empty()) m_listdata.clear();
	m_rfpath = csvfilepath;

	if ( input.IsOk() && !input.Eof() )
	{
		wxString line = text.ReadLine();
		wxStringTokenizer tkz(line, wxT(","));

		while(tkz.HasMoreTokens())
		{
			wxString tk = tkz.GetNextToken();
			m_dbnames.Add(tk);
		}
	}

	if ( input.IsOk() && !input.Eof() )
	{
		wxString line = text.ReadLine();
		wxStringTokenizer tkz(line, wxT(","));

		while(tkz.HasMoreTokens())
		{
			wxString tk = tkz.GetNextToken();
			m_dbpaths.Add(tk);
			m_dbdirs.Add(tk.BeforeLast(wxFILE_SEP_PATH, NULL));
		}
	}

	while(input.IsOk() && !input.Eof() )
	{
		wxString line = text.ReadLine();
		wxStringTokenizer tkz(line, wxT(","));
		
		wxArrayString con;
		while(tkz.HasMoreTokens())
			con.Add(tkz.GetNextToken());

		if (con.Count() >= 2)
		{
			NBLASTListItemData data;
			data.name = con[0];

			int tp = wxAtoi(con[1]);
			if (tp < 0 || tp >= m_dbdirs.GetCount())
				tp = -1;
			data.dbid = tp;
			data.score = con[2];
			m_listdata.push_back(data);
		}
	}

	bool show_image = false;
	int w = 0, h = 0;

	if (!m_dbdirs.IsEmpty())
	{
		wxString thumbdir = m_dbdirs[0] + wxFILE_SEP_PATH + _("MIP_thumb");
		wxString prevdir = m_dbdirs[0] + wxFILE_SEP_PATH + _("swc_thumb");

		wxDir dir1(thumbdir);
		if (dir1.IsOpened())
		{
			wxString fimgpath;
			if (dir1.GetFirst(&fimgpath, "*.png"))
			{
				wxBitmap img(_(thumbdir+wxFILE_SEP_PATH+fimgpath), wxBITMAP_TYPE_PNG);
				if (img.IsOk())
				{
					w = img.GetWidth();
					h = img.GetHeight();
#ifdef _DARWIN
					SetColumnWidth(4, w+8);
					SetColumnWidth(5, w+8);
#else
					SetColumnWidth(4, w+2);
					SetColumnWidth(5, w+2);
#endif
					show_image = true;
				}
			}
		}

		if (!show_image)
		{
			wxDir dir2(prevdir);
			if (dir2.IsOpened())
			{
				wxString fimgpath;
				if (dir2.GetFirst(&fimgpath, "*.png"))
				{
					wxBitmap img(_(prevdir+wxFILE_SEP_PATH+fimgpath), wxBITMAP_TYPE_PNG);
					if (img.IsOk())
					{
						w = img.GetWidth();
						h = img.GetHeight();
#ifdef _DARWIN
						SetColumnWidth(4, w+8);
						SetColumnWidth(5, w+8);
#else
						SetColumnWidth(4, w+2);
						SetColumnWidth(5, w+2);
#endif
						show_image = true;
					}
				}
			}
		}
	}

	if (show_image)
	{
		if (m_images) wxDELETE(m_images);
		m_images = new wxImageList(w, h, false);
        SetImageList(m_images, wxIMAGE_LIST_SMALL);
		
        wxString imgpath;
		
		char *dummy8 = new (std::nothrow) char[w*h];
		memset((void*)dummy8, 0, sizeof(char)*w*h);
		wxBitmap dummy(dummy8, w, h);
		m_images->Add(dummy, wxBITMAP_TYPE_BMP);

		int imgcount = 0;
		for (int i = 0; i < m_listdata.size(); i++)
		{
			int mipid = 0;
			int swcid = 0;
			if (m_listdata[i].dbid >= 0 && m_listdata[i].dbid < m_dbdirs.GetCount())
			{
				wxString thumbdir = m_dbdirs[m_listdata[i].dbid] + wxFILE_SEP_PATH + _("MIP_thumb");
				wxString prevdir = m_dbdirs[m_listdata[i].dbid] + wxFILE_SEP_PATH + _("swc_thumb");
				if (wxFileExists(thumbdir+wxFILE_SEP_PATH+m_listdata[i].name+_(".png")))
				{
					wxBitmap img(thumbdir+wxFILE_SEP_PATH+m_listdata[i].name+_(".png"), wxBITMAP_TYPE_PNG);
					if (img.IsOk())
					{
						imgcount++;
						m_images->Add(img, wxBITMAP_TYPE_PNG);
						mipid = imgcount;
					}
				}
				if (wxFileExists(prevdir+wxFILE_SEP_PATH+m_listdata[i].name+_(".png")))
				{
					wxBitmap img(prevdir+wxFILE_SEP_PATH+m_listdata[i].name+_(".png"), wxBITMAP_TYPE_PNG);
					if (img.IsOk())
					{
						imgcount++;
						m_images->Add(img, wxBITMAP_TYPE_PNG);
						swcid = imgcount;
					}
				}
			}
			wxString dbname;
			if (m_listdata[i].dbid >= 0 && m_listdata[i].dbid < m_dbnames.GetCount())
				dbname = m_dbnames[m_listdata[i].dbid];
			else if (m_listdata[i].dbid >= 0 && m_listdata[i].dbid < m_dbpaths.GetCount())
			{
				wxFileName fn(m_dbpaths[m_listdata[i].dbid]);
				dbname = fn.GetName();
			}
			m_listdata[i].swcid = swcid;
			m_listdata[i].mipid = mipid;
			m_listdata[i].dbname = dbname;
			Append(m_listdata[i].name, m_listdata[i].dbname, m_listdata[i].score, m_listdata[i].mipid, m_listdata[i].swcid, m_listdata[i].dbid);
		}
	}
	else
	{
		for (int i = 0; i < m_listdata.size(); i++)
		{
			wxString dbname;
			if (m_listdata[i].dbid >= 0 && m_listdata[i].dbid < m_dbnames.GetCount())
				dbname = m_dbnames[m_listdata[i].dbid];
			else if (m_listdata[i].dbid >= 0 && m_listdata[i].dbid < m_dbpaths.GetCount())
			{
				wxFileName fn(m_dbpaths[m_listdata[i].dbid]);
				dbname = fn.GetName();
			}
			m_listdata[i].swcid = -1;
			m_listdata[i].mipid = -1;
			m_listdata[i].dbname = dbname;
			Append(m_listdata[i].name, m_listdata[i].dbname, m_listdata[i].score, m_listdata[i].mipid, m_listdata[i].swcid, m_listdata[i].dbid);
		}
	}
/*
	SetColumnWidth(1, wxLIST_AUTOSIZE);
	int cw = GetColumnWidth(1);
	if (cw < 100)
		SetColumnWidth(1, 100);
	SetColumnWidth(2, wxLIST_AUTOSIZE);
	cw = GetColumnWidth(2);
	if (cw < 100)
		SetColumnWidth(2, 100);
*/
	SetEvtHandlerEnabled(true);
    Update();

	long item = GetNextItem(-1);
	if (item != -1)
		SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}

void NBLASTListCtrl::SaveResults(wxString txtpath, bool export_swc, bool export_swcprev, bool export_mip, bool export_vol, bool pfx_score, bool pfx_db, bool zip)
{
	wxFileOutputStream os(txtpath);
	wxTextOutputStream tos(os);

	if (m_dbnames.GetCount() > 0)
	{
		for (int i = 0; i < m_dbnames.GetCount()-1; i++)
			tos << m_dbnames[i] << ",";
		tos << m_dbnames[m_dbnames.GetCount()-1] << endl;
	}
	if (m_dbpaths.GetCount() > 0)
	{
		for (int i = 0; i < m_dbpaths.GetCount()-1; i++)
			tos << m_dbpaths[i] << ",";
		tos << m_dbpaths[m_dbpaths.GetCount()-1] << endl;
	}

	long item = GetNextItem(-1);
	if (item != -1)
	{
		do{
			tos << GetText(item, 2) << "," << GetText(item, 1) << "," << GetText(item, 6) << endl;
		} while ((item = GetNextItem(item, wxLIST_NEXT_BELOW)) != -1);
	}

	os.Close();

	wxString outdir = txtpath.BeforeLast(wxFILE_SEP_PATH, NULL);
	wxString prjimg = m_rfpath.BeforeLast(L'.', NULL) + _(".png");
	wxFileName fn(txtpath);
	wxString new_prjimg = outdir + wxFILE_SEP_PATH + fn.GetName() + _(".png");
	wxCopyFile(prjimg, new_prjimg);

	if ((export_swc || export_swcprev || export_mip || export_vol) && !zip)
	{
		long item = GetNextItem(-1);
		if (item != -1)
		{
			wxProgressDialog *prg_diag = new wxProgressDialog(
				"NBLAST Plugin: Exporting search results...",
				"Please wait.",
			GetItemCount(), 0, wxPD_APP_MODAL|wxPD_SMOOTH|wxPD_ELAPSED_TIME|wxPD_AUTO_HIDE);
			int count = 0;
			do
			{
				wxString dbidstr = GetText(item, 1);
				int dbid = wxAtoi(dbidstr);
				if (dbid >= 0 && dbid < m_dbdirs.GetCount())
				{
					wxString name = GetText(item, 2);
					wxString prefix;
					if (pfx_score)
					{
						wxString scstr = GetText(item, 6);
						double sc = 1.0;
						if(scstr.ToDouble(&sc))
						{
							int pfsc = (int)(floor(sc * 100000.0 + 0.5));
							prefix += wxString::Format(wxT("%i"), pfsc) + _(" ");
						}
					}
					if (pfx_db)
					{
						prefix += m_dbnames[dbid] + _(" ");
					}

					if (export_swcprev)
					{
						wxString imgpath1 = m_dbdirs[dbid] + wxFILE_SEP_PATH + _("swc_prev") + wxFILE_SEP_PATH + name + _(".png");
						wxString new_imgpath1 = outdir + wxFILE_SEP_PATH + prefix + name + _("_swc.png");
						if (wxFileExists(imgpath1)) wxCopyFile(imgpath1, new_imgpath1);
					}
					if (export_mip)
					{
						wxString imgpath2 = m_dbdirs[dbid] + wxFILE_SEP_PATH + _("MIP") + wxFILE_SEP_PATH + name + _(".png");
						wxString new_imgpath2 = outdir + wxFILE_SEP_PATH + prefix + name + _("_mip.png");
						if (wxFileExists(imgpath2)) wxCopyFile(imgpath2, new_imgpath2);
					}
					if (export_swc)
					{
						wxString swcpath = m_dbdirs[dbid] + wxFILE_SEP_PATH + _("swc") + wxFILE_SEP_PATH + name + _(".swc");
						wxString new_swcpath = outdir + wxFILE_SEP_PATH + prefix + name + _(".swc");
						if (wxFileExists(swcpath)) wxCopyFile(swcpath, new_swcpath);
					}
					if (export_vol)
					{
						wxString swcpath = m_dbdirs[dbid] + wxFILE_SEP_PATH + _("volume") + wxFILE_SEP_PATH + name + _(".nrrd");
						wxString new_swcpath = outdir + wxFILE_SEP_PATH + prefix + name + _(".nrrd");
						if (wxFileExists(swcpath)) wxCopyFile(swcpath, new_swcpath);
					}
				}
				count++;
				prg_diag->Update(count, "NBLAST Plugin: Exporting search results...");
			} while ((item = GetNextItem(item, wxLIST_NEXT_BELOW)) != -1);
			delete prg_diag;
		}
	}

}

void NBLASTListCtrl::DeleteSelection()
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item != -1)
	{
		long sel = item;
		if (GetNextItem(item, wxLIST_NEXT_BELOW) == -1)
			sel = GetNextItem(item, wxLIST_NEXT_ABOVE);
		
		NBLASTListItemData hdata;
		wxString dbidstr = GetText(item, 1);
		hdata.dbid = wxAtoi(dbidstr);
		hdata.name = GetText(item, 2);
		hdata.dbname = GetText(item, 3);
		hdata.swcid = GetImageId(item, 4);
		hdata.mipid = GetImageId(item, 5);
		hdata.score = GetText(item, 6);
		hdata.itemid = item;
		AddHistory(hdata);

		DeleteItem(item);

		if (sel != -1)
			SetItemState(sel, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED);
	}
}

void NBLASTListCtrl::DeleteAll()
{
	DeleteAllItems();
	if (!m_history.empty()) m_history.clear();
	m_history_pos = 0;
}

void NBLASTListCtrl::AddHistory(const NBLASTListItemData &data)
{
	if (m_history_pos < m_history.size())
	{
		if (m_history_pos > 0)
			m_history = vector<NBLASTListItemData>(m_history.begin(), m_history.begin()+m_history_pos);
		else
		{
			if (!m_history.empty()) m_history.clear();
			m_history_pos = 0;
		}
	}

	if (m_history_pos >= 300)
	{
		m_history.erase(m_history.begin());
		m_history.push_back(data);
	}
	else
	{
		m_history.push_back(data);
		m_history_pos++;
	}
}

void NBLASTListCtrl::Undo()
{
	if (m_history_pos <= 0)
		return;

	m_history_pos--;
	NBLASTListItemData comdata = m_history[m_history_pos];

	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	long sel = -1;
	if (item != -1)
	{
		if (item >= comdata.itemid)
			sel = item + 1;
	}

	Append(comdata.name, comdata.dbname, comdata.score, comdata.mipid, comdata.swcid, comdata.dbid, comdata.itemid);
}

void NBLASTListCtrl::Redo()
{
	if (m_history_pos >= m_history.size())
		return;
	NBLASTListItemData comdata = m_history[m_history_pos];

	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	long sel = -1;
	if (item != -1)
	{
		if (item >= comdata.itemid)
			sel = item - 1;
	}

	DeleteItem(comdata.itemid);
	m_history_pos++;
}

void NBLASTListCtrl::OnColBeginDrag(wxListEvent& event)
{
	if ( event.GetColumn() == 0 || event.GetColumn() == 1 )
    {
        event.Veto();
    }
}

void NBLASTListCtrl::Append(wxString name, wxString dbname, wxString score, int mipid, int swcid, int dbid, int index)
{
	wxString dbidstr = wxString::Format(wxT("%i"), dbid);
	int itemid = index >= 0 ? index : GetItemCount();
	long tmp = InsertItem(itemid, name);
	SetItem(tmp, 1, dbidstr);
	SetItem(tmp, 2, name);
	SetItem(tmp, 3, dbname);
	SetItem(tmp, 4, _(""), swcid);
	SetItem(tmp, 5, _(""), mipid);
	SetItem(tmp, 6, score);
}

wxString NBLASTListCtrl::GetText(long item, int col)
{
	wxListItem info;
	info.SetId(item);
	info.SetColumn(col);
	info.SetMask(wxLIST_MASK_TEXT);
	GetItem(info);
	return info.GetText();
}

int NBLASTListCtrl::GetImageId(long item, int col)
{
	wxListItem info;
	info.SetId(item);
	info.SetColumn(col);
	info.SetMask(wxLIST_MASK_IMAGE);
	GetItem(info);
	return info.GetImage();
}

void NBLASTListCtrl::OnSelect(wxListEvent &event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);

	if (item != -1)
	{
		wxString dbidstr = GetText(item, 1);
		int dbid = wxAtoi(dbidstr);
		if (dbid >= 0 && dbid < m_dbdirs.GetCount())
		{
			wxString name = GetText(item, 2);
			wxString imgpath1 = m_dbdirs[dbid] + wxFILE_SEP_PATH + _("swc_prev") + wxFILE_SEP_PATH + name + _(".png");
			wxString imgpath2 = m_dbdirs[dbid] + wxFILE_SEP_PATH + _("MIP") + wxFILE_SEP_PATH + name + _(".png");
			wxString imgpaths = imgpath1 + _(",") + imgpath2;
			notifyAll(NB_SET_IMAGE, imgpaths.ToStdString().c_str(), imgpaths.ToStdString().length()+1);
		}
	}
}
/*
void NBLASTListCtrl::OnAct(wxListEvent &event)
{
	int index = 0;
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item != -1)
	{
		wxString dbidstr = GetText(item, 1);
		int dbid = wxAtoi(dbidstr);
		if (dbid >= 0 && dbid < m_dbdirs.GetCount())
		{
			wxString name = GetText(item, 2);
			wxString swcpath = m_dbdirs[dbid] + wxFILE_SEP_PATH + _("swc") + wxFILE_SEP_PATH + name + _(".swc");
			notifyAll(NB_OPEN_FILE, swcpath.ToStdString().c_str(), swcpath.ToStdString().length()+1);
		}
	}
}
*/
void NBLASTListCtrl::OnMouse(wxMouseEvent &event)
{
	event.Skip();
}

void NBLASTListCtrl::OnScroll(wxScrollWinEvent& event)
{
	event.Skip(true);
}

void NBLASTListCtrl::OnScroll(wxMouseEvent& event)
{
	event.Skip(true);
}

void NBLASTListCtrl::OnKeyDown(wxKeyEvent& event)
{
	if ( event.GetKeyCode() == WXK_DELETE ||
		event.GetKeyCode() == WXK_BACK)
		DeleteSelection();

	if (event.GetKeyCode() == wxKeyCode('Z') && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
		Undo();
	
	if (event.GetKeyCode() == wxKeyCode('Y') && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
		Redo();

	if (event.GetKeyCode() == wxKeyCode('Z') && VRenderFrame::GetKeyState(WXK_CONTROL) && VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
		Redo();

	event.Skip();
}

void NBLASTListCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void NBLASTListCtrl::OnLeftDClick(wxMouseEvent& event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item != -1)
	{
		wxPoint pos = event.GetPosition();
		wxRect rect_swc, rect_vol;
		GetSubItemRect(item, 4, rect_swc);
		GetSubItemRect(item, 5, rect_vol);

		wxString dbidstr = GetText(item, 1);
		int dbid = wxAtoi(dbidstr);
		if (dbid >= 0 && dbid < m_dbdirs.GetCount())
		{
			if (rect_swc.Contains(pos))
			{
				wxString name = GetText(item, 2);
				wxString swcpath = m_dbdirs[dbid] + wxFILE_SEP_PATH + _("swc") + wxFILE_SEP_PATH + name + _(".swc");
				if (wxFileExists(swcpath))
					notifyAll(NB_OPEN_FILE, swcpath.ToStdString().c_str(), swcpath.ToStdString().length()+1);
			}
			else if (rect_vol.Contains(pos))
			{
				wxString name = GetText(item, 2);
				wxString volpath = m_dbdirs[dbid] + wxFILE_SEP_PATH + _("volume") + wxFILE_SEP_PATH + name + _(".nrrd");
				if (wxFileExists(volpath))
					notifyAll(NB_OPEN_FILE, volpath.ToStdString().c_str(), volpath.ToStdString().length()+1);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE( wxNBLASTSettingDialog, wxDialog )
	EVT_BUTTON( wxID_OK, wxNBLASTSettingDialog::OnOk )
END_EVENT_TABLE()

wxNBLASTSettingDialog::wxNBLASTSettingDialog(NBLASTGuiPlugin *plugin, wxWindow* parent, wxWindowID id, const wxString &title,
												const wxPoint &pos, const wxSize &size, long style)
: wxDialog (parent, id, title, pos, size, style)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	wxString rpath, tmpdir, rnum;
	m_plugin = NULL;
	if (plugin)
	{
		rpath = plugin->GetRPath();
		tmpdir = plugin->GetOutDir();
		rnum = plugin->GetResultNum();
		m_plugin = plugin;
	}

	wxBoxSizer* itemBoxSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *sizer1 = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText *st = new wxStaticText(this, 0, "Rscript:", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	m_RPickCtrl = new wxFilePickerCtrl( this, ID_NBS_RPicker, rpath, _("Set a path to Rscript"), wxFileSelectorDefaultWildcardStr, wxDefaultPosition, wxSize(400, -1));
	sizer1->Add(5, 10);
	sizer1->Add(st, 0, wxALIGN_CENTER_VERTICAL);
	sizer1->Add(5, 10);
	sizer1->Add(m_RPickCtrl, 1, wxRIGHT|wxEXPAND);
	sizer1->Add(10, 10);
	itemBoxSizer->Add(5, 10);
	itemBoxSizer->Add(sizer1, 0, wxEXPAND);
		
	wxBoxSizer *sizer3 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(this, 0, "Temporary directory:", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	m_tmpdirPickCtrl = new wxDirPickerCtrl( this, ID_NBS_TempDirPicker, tmpdir, _("Choose a temp directory"), wxDefaultPosition, wxSize(400, -1));
	sizer3->Add(5, 10);
	sizer3->Add(st, 0, wxALIGN_CENTER_VERTICAL);
	sizer3->Add(5, 10);
	sizer3->Add(m_tmpdirPickCtrl, 1, wxRIGHT|wxEXPAND);
	sizer3->Add(10, 10);
	itemBoxSizer->Add(5, 10);
	itemBoxSizer->Add(sizer3, 0, wxEXPAND);

	wxIntegerValidator<unsigned int> vald_int;
	vald_int.SetMin(1);
	wxBoxSizer *sizer4 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(this, 0, "Show Top", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
	m_rnumTextCtrl = new wxTextCtrl( this, ID_NBS_ResultNumText, rnum, wxDefaultPosition, wxSize(30, -1), wxTE_RIGHT, vald_int);
	sizer4->Add(st, 0, wxALIGN_CENTER_VERTICAL);
	sizer4->Add(5, 10);
	sizer4->Add(m_rnumTextCtrl, 0, wxRIGHT);
	sizer4->Add(5, 10);
	st = new wxStaticText(this, 0, "Results", wxDefaultPosition, wxSize(70, -1), wxALIGN_LEFT);
	sizer4->Add(st, 0, wxALIGN_CENTER_VERTICAL);

	itemBoxSizer->Add(5, 10);
	itemBoxSizer->Add(sizer4, 0, wxALIGN_CENTER_HORIZONTAL);

	wxBoxSizer *sizerb = new wxBoxSizer(wxHORIZONTAL);
	wxButton *b = new wxButton(this, wxID_OK, _("OK"), wxDefaultPosition, wxDefaultSize);
	wxButton *c = new wxButton(this, wxID_CANCEL, _("Cancel"), wxDefaultPosition, wxDefaultSize);
	sizerb->Add(5,10);
    sizerb->Add(b);
    sizerb->Add(5,10);
	sizerb->Add(c);
	sizerb->Add(10,10);
	itemBoxSizer->Add(10, 20);
	itemBoxSizer->Add(sizerb, 0, wxALIGN_RIGHT);
	itemBoxSizer->Add(10, 10);
	
	SetSizer(itemBoxSizer);
	
	Thaw();
	SetEvtHandlerEnabled(true);
}

void wxNBLASTSettingDialog::LoadSettings()
{

}

void wxNBLASTSettingDialog::SaveSettings()
{
	if (m_plugin)
	{
		m_plugin->SetRPath(m_RPickCtrl->GetPath());
		m_plugin->SetOutDir(m_tmpdirPickCtrl->GetPath());
		m_plugin->SetResultNum(m_rnumTextCtrl->GetValue());
	}
}

void wxNBLASTSettingDialog::OnOk( wxCommandEvent& event )
{
	SaveSettings();
	event.Skip();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * NBLASTGuiPluginWindow type definition
 */

IMPLEMENT_DYNAMIC_CLASS( NBLASTGuiPluginWindow, wxGuiPluginWindowBase )


/*
 * NBLASTGuiPluginWindow event table definition
 */

BEGIN_EVENT_TABLE( NBLASTGuiPluginWindow, wxGuiPluginWindowBase )

////@begin NBLASTGuiPluginWindow event table entries
    EVT_BUTTON( ID_SEND_EVENT_BUTTON, NBLASTGuiPluginWindow::OnSENDEVENTBUTTONClick )
	EVT_MENU( ID_SAVE_BUTTON, NBLASTGuiPluginWindow::OnSaveButtonClick )
	EVT_MENU( ID_EDIT_DB_BUTTON, NBLASTGuiPluginWindow::OnEditDBButtonClick )
	EVT_MENU( ID_IMPORT_RESULTS_BUTTON, NBLASTGuiPluginWindow::OnImportResultsButtonClick )
	EVT_MENU( ID_SETTING, NBLASTGuiPluginWindow::OnSettingButtonClick )
	EVT_RADIOBUTTON(ID_SC_FWD, NBLASTGuiPluginWindow::OnScoringMethodCheck)
	EVT_RADIOBUTTON(ID_SC_MEAN, NBLASTGuiPluginWindow::OnScoringMethodCheck)
	EVT_CHECKBOX(ID_NB_OverlayCheckBox, NBLASTGuiPluginWindow::OnOverlayCheck)
	EVT_CLOSE(NBLASTGuiPluginWindow::OnClose)
	EVT_KEY_DOWN(NBLASTGuiPluginWindow::OnKeyDown)
	EVT_SHOW(NBLASTGuiPluginWindow::OnShowHide)
	EVT_KEY_UP(NBLASTGuiPluginWindow::OnKeyUp)
	EVT_TIMER(ID_IdleTimer ,NBLASTGuiPluginWindow::OnIdle)
////@end NBLASTGuiPluginWindow event table entries

END_EVENT_TABLE()


/*
 * NBLASTGuiPluginWindow constructors
 */

NBLASTGuiPluginWindow::NBLASTGuiPluginWindow()
{
    Init();
}

NBLASTGuiPluginWindow::NBLASTGuiPluginWindow( wxGuiPluginBase * plugin, 
											   wxWindow* parent, wxWindowID id, 
											   const wxPoint& pos, const wxSize& size, 
											   long style )
{
    Init();
    Create(plugin, parent, id, pos, size, style);
}


/*
 * NBLASTGuiPluginWindow creator
 */

bool NBLASTGuiPluginWindow::Create(wxGuiPluginBase * plugin, 
									wxWindow* parent, wxWindowID id, 
									const wxPoint& pos, const wxSize& size, 
									long style )
{
    wxGuiPluginWindowBase::Create(plugin, parent, id, pos, size, style );

    CreateControls();
    if (GetSizer())
    {
        GetSizer()->SetSizeHints(this);
    }
    Centre();

	((NBLASTGuiPlugin *)GetPlugin())->addObserver(this);

    return true;
}


/*
 * NBLASTGuiPluginWindow destructor
 */

NBLASTGuiPluginWindow::~NBLASTGuiPluginWindow()
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	if (plugin)
		plugin->SaveConfigFile();

	wxDBListDialog dbdlg(this, wxID_ANY, "Edit NBLAST Database", wxDefaultPosition, wxSize(500, 600));
	for (int i = 0; i < m_nlib_chks.size(); i++)
		dbdlg.setState(i, m_nlib_chks[i]->GetValue());
	dbdlg.SaveList();
}


/*
 * Member initialisation
 */

void NBLASTGuiPluginWindow::Init()
{
	m_splitterWindow = NULL;
	m_tb = NULL;
	m_CommandButton = NULL;
	m_swcImagePanel = NULL;
	m_mipImagePanel = NULL;
	m_overlayChk = NULL;
	m_prg_diag = NULL;
	m_waitingforR = false;
	m_waitingforFiji = false;
	m_wtimer = new wxTimer(this, ID_WaitTimer);
	m_idleTimer = new wxTimer(this, ID_IdleTimer);
}


/*
 * Control creation for NBLASTGuiPluginWindow
 */

void NBLASTGuiPluginWindow::CreateControls()
{    
	wxString rpath, nlibpath, outdir, rnum, scmtd;
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	if (plugin)
	{
		rpath = plugin->GetRPath();
		nlibpath = plugin->GetNlibPath();
		outdir = plugin->GetOutDir();
		rnum = plugin->GetResultNum();
		scmtd = plugin->GetScoringMethod();
	}

	SetEvtHandlerEnabled(false);
	Freeze();

	m_splitterWindow = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxSize(-1, -1));
	wxPanel* nbpanel = new wxPanel(m_splitterWindow, wxID_ANY);
	nbpanel->SetWindowStyle(wxBORDER_SIMPLE);
	wxPanel* imgpanel = new wxPanel(m_splitterWindow, wxID_ANY);
	imgpanel->SetWindowStyle(wxBORDER_SIMPLE);

	////@begin NBLASTGuiPluginWindow content construction
	wxBoxSizer* itemBoxSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* itemBoxSizer2 = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* itemBoxSizer2_1 = new wxBoxSizer(wxVERTICAL);

	wxIntegerValidator<unsigned int> vald_int;
	vald_int.SetMin(1);

#ifdef _WIN32
    int stsize = 120;
#else
    int stsize = 130;
#endif

	wxBoxSizer *sizert = new wxBoxSizer(wxHORIZONTAL);
	m_tb = new wxToolBar(nbpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_FLAT|wxTB_TOP|wxTB_NODIVIDER|wxTB_TEXT|wxTB_NOICONS|wxTB_HORZ_LAYOUT);
	m_tb->AddTool(ID_SAVE_BUTTON, "Save", wxNullBitmap, "Save search results");
	m_tb->AddTool(ID_IMPORT_RESULTS_BUTTON, "Import", wxNullBitmap, "Import search results");
	m_tb->AddTool(ID_EDIT_DB_BUTTON, "Database", wxNullBitmap, "Edit NBLAST databases");
	m_tb->AddTool(ID_SETTING, "Setting", wxNullBitmap, "Setting");
	m_tb->SetToolSeparation(20);
	m_tb->Realize();
	sizert->Add(m_tb, 1, wxEXPAND);
	itemBoxSizer2->Add(sizert, 0, wxEXPAND);
	wxStaticLine *stl = new wxStaticLine(nbpanel);
	itemBoxSizer2->Add(stl, 0, wxEXPAND);

	wxDBListDialog dbdlg(this, wxID_ANY, "Edit NBLAST Database", wxDefaultPosition, wxSize(500, 600));
	m_nlib_list = dbdlg.getList();

	wxBoxSizer *sizer1 = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText *st1 = new wxStaticText(nbpanel, 0, "Scoring Method:");
	m_sc_forward = new wxRadioButton(nbpanel, ID_SC_FWD, "Forward",
		wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_sc_mean = new wxRadioButton(nbpanel, ID_SC_MEAN, "Mean",
		wxDefaultPosition, wxDefaultSize);
	if (scmtd == "mean") {
		m_sc_forward->SetValue(false);
		m_sc_mean->SetValue(true);
	} else {
		m_sc_forward->SetValue(true);
		m_sc_mean->SetValue(false);
	}
	sizer1->Add(st1, 0, wxALIGN_CENTER, 0);
	sizer1->Add(20, 5, 0);
	sizer1->Add(m_sc_forward, 0, wxALIGN_CENTER);
	sizer1->Add(m_sc_mean, 0, wxALIGN_CENTER);
	itemBoxSizer2->Add(5, 3);
	itemBoxSizer2->Add(sizer1, 0, wxALIGN_CENTER_HORIZONTAL|wxALL);
	
	int cols = 5;
	int rows = 1;
	if (m_nlib_list.size() > 0)
		rows = (m_nlib_list.size() / cols) + 1;
	wxFlexGridSizer *gs = new wxFlexGridSizer(rows, cols, 20, 10);
	m_nlib_box = new wxStaticBoxSizer(wxVERTICAL, nbpanel, "Target Neurons");
	//m_nlib_box->GetStaticBox()->SetWindowStyleFlag(wxSIMPLE_BORDER);
	for (int i = 0; i < m_nlib_list.size(); i++)
	{
		m_nlib_chks.push_back(new wxCheckBox(nbpanel, wxID_ANY, m_nlib_list[i].name));
		gs->Add(m_nlib_chks[i], 0, wxALIGN_CENTER);
		m_nlib_chks[i]->SetValue(m_nlib_list[i].state);
	}
	m_nlib_box->Add(gs, 0, wxALIGN_CENTER|wxALL, 5);
	//itemBoxSizer2->Add(m_nlib_box, 0, wxALIGN_CENTER);
	
	wxBoxSizer *sizer2_2 = new wxBoxSizer(wxHORIZONTAL);
	sizer2_2->Add(m_nlib_box, 0, wxALIGN_CENTER_VERTICAL);
	itemBoxSizer2->Add(5, 3);
	itemBoxSizer2->Add(sizer2_2, 0, wxALIGN_CENTER_HORIZONTAL|wxALL);
    
	wxBoxSizer *sizerb = new wxBoxSizer(wxHORIZONTAL);
	m_CommandButton = new wxButton( nbpanel, ID_SEND_EVENT_BUTTON, _("Run NBLAST"), wxDefaultPosition, wxDefaultSize, 0 );
	sizerb->Add(m_CommandButton);
	itemBoxSizer2->Add(sizerb, 0, wxALIGN_CENTER_HORIZONTAL|wxALL, 5);
	
    wxBoxSizer *sizerl = new wxBoxSizer(wxHORIZONTAL);
	m_results = new NBLASTListCtrl(nbpanel, wxID_ANY, wxDefaultPosition, wxSize(590, 500));
	m_results->addObserver(this);
    sizerl->Add(5,10);
    sizerl->Add(m_results, 1, wxEXPAND);
    sizerl->Add(5,10);
	itemBoxSizer2->Add(5, 3);
	itemBoxSizer2->Add(sizerl, 1, wxEXPAND);

	wxBoxSizer *sizerchk = new wxBoxSizer(wxHORIZONTAL);
	m_overlayChk = new wxCheckBox(imgpanel, ID_NB_OverlayCheckBox, "Overlay search query");
	m_overlayChk->SetValue(true);
	sizerchk->Add(20, 10);
	sizerchk->Add(m_overlayChk, 0, wxALIGN_CENTER_VERTICAL);
	m_swcImagePanel = new wxImagePanel( imgpanel, 500, 250);
	m_mipImagePanel = new wxImagePanel( imgpanel, 500, 250);
	itemBoxSizer2_1->Add(5, 5);
	itemBoxSizer2_1->Add(sizerchk, 0, wxLEFT);
	itemBoxSizer2_1->Add(5, 5);
	itemBoxSizer2_1->Add(m_swcImagePanel, 1, wxEXPAND);
	itemBoxSizer2_1->Add(5, 5);
	itemBoxSizer2_1->Add(m_mipImagePanel, 1, wxEXPAND);

	nbpanel->SetSizer(itemBoxSizer2);
	imgpanel->SetSizer(itemBoxSizer2_1);

	nbpanel->SetMinSize(wxSize(640,750));
	imgpanel->SetMinSize(wxSize(510,750));
	
	m_splitterWindow->SplitVertically(nbpanel, imgpanel);

	itemBoxSizer->Add(m_splitterWindow, 1, wxEXPAND); 
	this->SetSizer(itemBoxSizer);
	this->Layout();

	////@end NBLASTGuiPluginWindow content construction

	plugin->GetEventHandler()->Bind(wxEVT_GUI_PLUGIN_INTEROP, 
		wxCommandEventHandler(NBLASTGuiPluginWindow::OnInteropMessageReceived), this);

	m_nbpanel = nbpanel;

	Thaw();
	SetEvtHandlerEnabled(true);
	//m_idleTimer->Start(50);
	//m_wtimer->Start(50);
}

void NBLASTGuiPluginWindow::EnableControls(bool enable)
{
	if (enable)
	{
		if (m_results) m_results->Enable();
		if (m_CommandButton) m_CommandButton->Enable();
	}
	else 
	{
		if (m_results) m_results->Disable();
		if (m_CommandButton) m_CommandButton->Disable();
	}
}

bool NBLASTGuiPluginWindow::ShowToolTips()
{
    return true;
}

/*
 * Get bitmap resources
 */

wxBitmap NBLASTGuiPluginWindow::GetBitmapResource( const wxString& name )
{
    // Bitmap retrieval
////@begin NBLASTGuiPluginWindow bitmap retrieval
    wxUnusedVar(name);
    return wxNullBitmap;
////@end NBLASTGuiPluginWindow bitmap retrieval
}

/*
 * Get icon resources
 */

wxIcon NBLASTGuiPluginWindow::GetIconResource( const wxString& name )
{
    // Icon retrieval
////@begin NBLASTGuiPluginWindow icon retrieval
    wxUnusedVar(name);
    return wxNullIcon;
////@end NBLASTGuiPluginWindow icon retrieval
}

void NBLASTGuiPluginWindow::doAction(ActionInfo *info)
{
	if (!info)
		return;
	int evid = info->id;
	
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();

	switch (evid)
	{
	case NB_OPEN_FILE:
		if (plugin)
		{
			wxString str = wxString((char *)info->data);
			plugin->LoadFiles(str);
		}
		break;
	case NB_SET_IMAGE:
		if (plugin && m_results && m_swcImagePanel && m_mipImagePanel)
		{
			wxString prjimg = m_results->GetListFilePath().BeforeLast(L'.', NULL) + _(".png");
			
			wxString str = wxString((char *)info->data);
			wxStringTokenizer tkz(str, wxT(","));

			wxArrayString con;
			while(tkz.HasMoreTokens())
				con.Add(tkz.GetNextToken());

			double aspect = 0.0;

			if (con.GetCount() >= 1)
			{
				wxString imgpath1 = con[0];
				wxString bkimgpath = imgpath1.BeforeLast(wxFILE_SEP_PATH, NULL) + wxFILE_SEP_PATH + _("bg.png");
				m_swcImagePanel->SetImage(imgpath1, wxBITMAP_TYPE_PNG);
				m_swcImagePanel->SetOverlayImage(prjimg, wxBITMAP_TYPE_PNG);
				m_swcImagePanel->SetBackgroundImage(bkimgpath, wxBITMAP_TYPE_PNG);
				m_swcImagePanel->UpdateImage(m_overlayChk->GetValue());
				m_swcImagePanel->Refresh();
				aspect = m_swcImagePanel->GetAspectRatio();
			}

			if (con.GetCount() >= 2)
			{
				wxString imgpath2 = con[1];
				wxString bkimgpath = imgpath2.BeforeLast(wxFILE_SEP_PATH, NULL) + wxFILE_SEP_PATH + _("bg.png");
				m_mipImagePanel->SetImage(imgpath2, wxBITMAP_TYPE_PNG);
				double a2 = m_mipImagePanel->GetAspectRatio();
				if (fabsl(aspect - a2) < 0.00001)
					m_mipImagePanel->SetOverlayImage(prjimg, wxBITMAP_TYPE_PNG);
				m_mipImagePanel->SetBackgroundImage(bkimgpath, wxBITMAP_TYPE_PNG);
				m_mipImagePanel->UpdateImage(m_overlayChk->GetValue());
				m_mipImagePanel->Refresh();
			}
		}
	default:
		break;
	}
    delete info;
}


void NBLASTGuiPluginWindow::OnSENDEVENTBUTTONClick( wxCommandEvent& event )
{
	wxArrayString nlibs;
	wxArrayString nlibnames;
	for (int i=0; i < m_nlib_list.size(); i++)
	{
		if (m_nlib_chks[i]->GetValue())
		{
			nlibs.Add(m_nlib_list[i].path);
			nlibnames.Add(m_nlib_list[i].name);
		}
	}

	wxString nlibpath, nlibname;
	if (nlibs.Count() > 0)
	{
		for (int i=0; i < nlibs.Count()-1; i++)
		{
			nlibpath += nlibs[i] + wxT(",");
			nlibname += nlibnames[i] + wxT(",");
		}
		nlibpath += nlibs[nlibs.Count()-1];
		nlibname += nlibnames[nlibs.Count()-1];
	}
	
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	long lval = -1;
	if (plugin)
	{
		wxString rpath = plugin->GetRPath();
		wxString outdir = plugin->GetOutDir();;
		wxString ofname = "vvd_nblast_tmp_result";
		wxString rnum = plugin->GetResultNum();
		wxString scmtd = plugin->GetScoringMethod();
		if (!wxFileExists(rpath))
			{wxMessageBox("Could not find Rscript binary", "NBLAST Plugin"); event.Skip(); return;}
		if (nlibs.IsEmpty())
			{wxMessageBox("Choose target neurons", "NBLAST Plugin"); event.Skip(); return;}
		for (int i=0; i < nlibs.Count()-1; i++)
		{
			if (!wxFileExists(nlibs[i]))
			{
				wxMessageBox(wxT("Could not find a NBLAST database (")+nlibs[i]+wxT(")"), wxT("NBLAST Plugin"));
				event.Skip();
				return;
			}
		}
		if (outdir.IsEmpty())
			{wxMessageBox("Set a temp directory", "NBLAST Plugin"); event.Skip(); return;}
		if (ofname.IsEmpty())
			{wxMessageBox("Set a project name", "NBLAST Plugin"); event.Skip(); return;}
		if (!rnum.ToLong(&lval))
			{wxMessageBox("Invalid number", "NBLAST Plugin"); event.Skip(); return;}

		VRenderFrame *vframe = (VRenderFrame *)plugin->GetVVDMainFrame();
		if (vframe)
		{
			int type = vframe->GetCurSelType();
			VolumeData *vd = vframe->GetCurSelVol();
			MeshData *md = vframe->GetCurSelMesh();

			if (vd && type == 2)
			{
				if (vd->GetName().Find("skeleton") == wxNOT_FOUND)
				{
					if (plugin->skeletonizeMask())
					{
						plugin->SetRPath(rpath);
						plugin->SetNlibPath(nlibpath);
						plugin->SetOutDir(outdir);
						plugin->SetFileName(ofname);
						plugin->SetResultNum(rnum);
						plugin->SetDatabaseNames(nlibname);
						plugin->SetScoringMethod(scmtd);
						m_waitingforFiji = true;
					}
				}
				else if (!m_waitingforFiji)
				{
					plugin->runNBLAST(rpath, nlibpath, outdir, ofname, rnum, nlibname, scmtd);

					wxString respath = outdir + wxFILE_SEP_PATH + ofname + _(".txt");
					if (wxFileExists(respath) && m_results)
						m_results->LoadResults(respath);
				}
			}
			if (md && type == 3)
			{
				plugin->runNBLAST(rpath, nlibpath, outdir, ofname, rnum, nlibname, scmtd);
				wxString respath = outdir + wxFILE_SEP_PATH + ofname + _(".txt");
				if (wxFileExists(respath) && m_results)
					m_results->LoadResults(respath);
			}
		}

	}

//	wxCommandEvent e(wxEVT_GUI_PLUGIN_INTEROP);
//	e.SetString(m_RPickCtrl->GetPath());
//	GetPlugin()->GetEventHandler()->AddPendingEvent(e);

    event.Skip();
}

wxWindow* NBLASTGuiPluginWindow::CreateExtraNBLASTControl(wxWindow* parent)
{
#ifdef _WIN32
    wxPanel* panel = new wxPanel(parent, 0, wxDefaultPosition, wxSize(500, 90));
#else
    wxPanel* panel = new wxPanel(parent, 0, wxDefaultPosition, wxSize(530, 90));
#endif

	wxBoxSizer *sizer = new wxStaticBoxSizer(
		new wxStaticBox(panel, wxID_ANY, "Additional Options"), wxVERTICAL);

	wxBoxSizer *group1 = new wxBoxSizer(wxHORIZONTAL);

	wxStaticText *st = new wxStaticText(panel, 0, "Export:", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	
	wxCheckBox* ch1 = new wxCheckBox(panel, wxID_HIGHEST+10051, "Skeletones");
	ch1->Connect(ch1->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
		wxCommandEventHandler(NBLASTGuiPluginWindow::OnSWCExportCheck), NULL, panel);
	if (ch1)
		ch1->SetValue(NBLASTGuiPlugin::GetExportSWCs());

	wxCheckBox* ch2 = new wxCheckBox(panel, wxID_HIGHEST+10052, "MIPs");
	ch2->Connect(ch2->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
		wxCommandEventHandler(NBLASTGuiPluginWindow::OnMIPImageExportCheck), NULL, panel);
	if (ch2)
		ch2->SetValue(NBLASTGuiPlugin::GetExportMIPs());

	wxCheckBox* ch3 = new wxCheckBox(panel, wxID_HIGHEST+10053, "Skeletone Previews");
	ch3->Connect(ch3->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
		wxCommandEventHandler(NBLASTGuiPluginWindow::OnSWCImageExportCheck), NULL, panel);
	if (ch3)
		ch3->SetValue(NBLASTGuiPlugin::GetExportSWCPrevImgs());

	wxCheckBox* chv = new wxCheckBox(panel, wxID_HIGHEST+10054, "Volumes");
	chv->Connect(chv->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
		wxCommandEventHandler(NBLASTGuiPluginWindow::OnVolumeExportCheck), NULL, panel);
	if (chv)
		chv->SetValue(NBLASTGuiPlugin::GetExportVolumes());

	//group
	group1->Add(10, 10);
	group1->Add(st);
	group1->Add(20, 10);
	group1->Add(ch1);
	group1->Add(20, 10);
	group1->Add(ch2);
	group1->Add(20, 10);
	group1->Add(ch3);
	group1->Add(20, 10);
	group1->Add(chv);
	group1->Add(20, 10);

	wxBoxSizer *group2 = new wxBoxSizer(wxHORIZONTAL);

	st = new wxStaticText(panel, 0, "File Name Prefix:", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	
	wxCheckBox* ch4 = new wxCheckBox(panel, wxID_HIGHEST+10054, "Score");
	ch4->Connect(ch4->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
		wxCommandEventHandler(NBLASTGuiPluginWindow::OnScorePrefixCheck), NULL, panel);
	if (ch4)
		ch4->SetValue(NBLASTGuiPlugin::GetPrefixScore());
	
	wxCheckBox* ch5 = new wxCheckBox(panel, wxID_HIGHEST+10055, "Database");
	ch5->Connect(ch5->GetId(), wxEVT_COMMAND_CHECKBOX_CLICKED,
		wxCommandEventHandler(NBLASTGuiPluginWindow::OnDatabasePrefixCheck), NULL, panel);
	if (ch5)
		ch5->SetValue(NBLASTGuiPlugin::GetPrefixDatabase());

	group2->Add(10, 10);
	group2->Add(st);
	group2->Add(20, 10);
	group2->Add(ch4);
	group2->Add(20, 10);
	group2->Add(ch5);
	group2->Add(20, 10);

	sizer->Add(10, 10);
	sizer->Add(group1);
	sizer->Add(15, 15);
	sizer->Add(group2);
	
	panel->SetSizer(sizer);
	panel->Layout();

	return panel;
}

void NBLASTGuiPluginWindow::OnSWCExportCheck(wxCommandEvent& event)
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	wxCheckBox* ch1 = (wxCheckBox*)event.GetEventObject();

	if (ch1 && plugin)
		plugin->SetExportSWCs(ch1->GetValue());
}

void NBLASTGuiPluginWindow::OnMIPImageExportCheck(wxCommandEvent& event)
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	wxCheckBox* ch2 = (wxCheckBox*)event.GetEventObject();

	if (ch2 && plugin)
		plugin->SetExportMIPs(ch2->GetValue());
}

void NBLASTGuiPluginWindow::OnSWCImageExportCheck(wxCommandEvent& event)
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	wxCheckBox* ch3 = (wxCheckBox*)event.GetEventObject();

	if (ch3 && plugin)
		plugin->SetExportSWCPrevImgs(ch3->GetValue());
}

void NBLASTGuiPluginWindow::OnVolumeExportCheck(wxCommandEvent& event)
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	wxCheckBox* chv = (wxCheckBox*)event.GetEventObject();

	if (chv && plugin)
		plugin->SetExportVolumes(chv->GetValue());
}

void NBLASTGuiPluginWindow::OnScorePrefixCheck(wxCommandEvent& event)
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	wxCheckBox* ch4 = (wxCheckBox*)event.GetEventObject();

	if (ch4 && plugin)
		plugin->SetPrefixScore(ch4->GetValue());
}

void NBLASTGuiPluginWindow::OnDatabasePrefixCheck(wxCommandEvent& event)
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	wxCheckBox* ch5 = (wxCheckBox*)event.GetEventObject();

	if (ch5 && plugin)
		plugin->SetPrefixDatabase(ch5->GetValue());
}

void NBLASTGuiPluginWindow::OnSaveButtonClick( wxCommandEvent& event )
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
//	if (plugin) plugin->skeletonizeMask();

	if (!m_results || !plugin)
		return;

	wxFileDialog file_dlg(this, "Save Search Results", "", "", "*.txt", wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
	file_dlg.SetExtraControlCreator(CreateExtraNBLASTControl);
	int rval = file_dlg.ShowModal();
	if (rval == wxID_OK)
		m_results->SaveResults(file_dlg.GetPath(), plugin->GetExportSWCs(), plugin->GetExportSWCPrevImgs(), plugin->GetExportMIPs(), plugin->GetExportVolumes(), plugin->GetPrefixScore(), plugin->GetPrefixDatabase());
}

void NBLASTGuiPluginWindow::OnEditDBButtonClick( wxCommandEvent& event )
{
	wxDBListDialog dbdlg(this, wxID_ANY, "Edit NBLAST Database", wxDefaultPosition, wxSize(500, 600));
	for (int i = 0; i < m_nlib_chks.size(); i++)
		dbdlg.setState(i, m_nlib_chks[i]->GetValue());

	if (dbdlg.ShowModal() == wxID_OK)
	{
		vector<bool> chk_state;
		for (int i = 0; i < m_nlib_chks.size(); i++)
			chk_state.push_back(m_nlib_chks[i]->GetValue());
		m_nlib_list = dbdlg.getList();
		
		m_nlib_chks.clear();
		m_nlib_box->Clear(true);
		
		int cols = 5;
		int rows = 1;
		if (m_nlib_list.size() > 0)
			rows = (m_nlib_list.size() / cols) + 1;
		wxFlexGridSizer *gs = new wxFlexGridSizer(rows, cols, 20, 10);
		for (int i = 0; i < m_nlib_list.size(); i++)
		{
			m_nlib_chks.push_back(new wxCheckBox(m_nbpanel, wxID_ANY, m_nlib_list[i].name));
			gs->Add(m_nlib_chks[i], 0, wxALIGN_CENTER);
		}
		m_nlib_box->Add(gs, 0, wxALIGN_CENTER|wxALL, 5);

		for (int i = 0; i < m_nlib_chks.size(); i++)
			m_nlib_chks[i]->SetValue(m_nlib_list[i].state);
		
		m_nbpanel->Layout();
	}
}

void NBLASTGuiPluginWindow::OnImportResultsButtonClick( wxCommandEvent& event )
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
//	if (plugin) plugin->skeletonizeMask();

	if (!m_results || !plugin)
		return;

	wxFileDialog file_dlg(this, "Import Search Results", "", "", "*.txt", wxFD_OPEN);
	int rval = file_dlg.ShowModal();
	if (rval == wxID_OK)
		m_results->LoadResults(file_dlg.GetPath());
}

void NBLASTGuiPluginWindow::OnSettingButtonClick( wxCommandEvent& event )
{
	wxNBLASTSettingDialog sdlg((NBLASTGuiPlugin *)GetPlugin() ,this, wxID_ANY, "NBLAST Settings", wxDefaultPosition, wxSize(450, 200));
	
	if (sdlg.ShowModal() == wxID_OK)
	{

	}
}

void NBLASTGuiPluginWindow::OnOverlayCheck( wxCommandEvent& event )
{
	if (m_swcImagePanel && m_mipImagePanel)
	{
		m_swcImagePanel->UpdateImage(m_overlayChk->GetValue());
		double a1 = m_swcImagePanel->GetAspectRatio();
		double a2 = m_mipImagePanel->GetAspectRatio();
		m_mipImagePanel->UpdateImage(m_overlayChk->GetValue() && fabsl(a1 - a2) < 0.00001);
		m_swcImagePanel->Refresh();
		m_mipImagePanel->Refresh();
	}
}

void NBLASTGuiPluginWindow::OnScoringMethodCheck( wxCommandEvent& event )
{
	NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
	if (!plugin) return;

	int sender_id = event.GetId();
	switch (sender_id)
	{
	case ID_SC_FWD:
		plugin->SetScoringMethod(wxT("forward"));
		break;
	case ID_SC_MEAN:
		plugin->SetScoringMethod(wxT("mean"));
		break;
	}
}

void NBLASTGuiPluginWindow::OnClose(wxCloseEvent& event)
{
	event.Skip();
}

void NBLASTGuiPluginWindow::OnShowHide(wxShowEvent& event)
{
	event.Skip();
}

void NBLASTGuiPluginWindow::OnInteropMessageReceived(wxCommandEvent & event)
{
	if (m_waitingforFiji)
	{
		NBLASTGuiPlugin* plugin = (NBLASTGuiPlugin *)GetPlugin();
		if (!plugin) return;

		wxString message = event.GetString();
		wxStringTokenizer tkz(message, wxT(","));

		wxArrayString args;
		while(tkz.HasMoreTokens())
			args.Add(tkz.GetNextToken());

		if (args[0] == _("Fiji Interface") && args[1] == _("finished") && args[2] == _("NBLAST Skeletonize"))
		{
			m_waitingforFiji = false;
			plugin->runNBLAST();

			wxString respath = plugin->GetOutDir() + wxFILE_SEP_PATH + plugin->GetFileName() + _(".txt");
			if (wxFileExists(respath) && m_results)
				m_results->LoadResults(respath);
		}
	}
}

void NBLASTGuiPluginWindow::OnIdle(wxTimerEvent& event)
{
	if (!IsShown())
		return;
	if (m_results)
	{
		if (VRenderFrame::GetKeyState(wxKeyCode('z')) && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
			m_results->Undo();

		if (VRenderFrame::GetKeyState(wxKeyCode('y')) && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
			m_results->Redo();

		if (VRenderFrame::GetKeyState(wxKeyCode('z')) && VRenderFrame::GetKeyState(WXK_CONTROL) && VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
			m_results->Redo();
	}
}

void NBLASTGuiPluginWindow::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void NBLASTGuiPluginWindow::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}
