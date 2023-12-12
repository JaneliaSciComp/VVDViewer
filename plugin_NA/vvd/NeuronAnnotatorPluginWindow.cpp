
// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "NeuronAnnotatorPlugin.h"
#include "NeuronAnnotatorPluginWindow.h"
//#include "VRenderFrame.h"
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/tokenzr.h>
#include <wx/dcbuffer.h>
#include <wx/valnum.h>
#include <wx/filename.h>
#include <wx/statline.h>
#include "Formats/png_resource.h"
#include "img/icons.h"
#include "tick.xpm"
#include "cross.xpm"


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

///////////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(wxImagePanel, wxPanel)
EVT_PAINT(wxImagePanel::OnDraw)
EVT_SIZE(wxImagePanel::OnSize)
END_EVENT_TABLE()

wxImagePanel::wxImagePanel(wxWindow* parent, int w, int h) :
wxPanel(parent)
{
	m_resized = NULL;
	m_w = -1;
	m_h = -1;
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

wxImagePanel::~wxImagePanel()
{
	wxDELETE(m_resized);
	if (m_orgimage.IsOk())
		m_orgimage.Destroy();
	if (m_image.IsOk())
		m_image.Destroy();
}

void wxImagePanel::SetImage(const wxImage* img)
{
	wxDELETE(m_resized);
	m_image.Destroy();
	if (m_orgimage.IsOk())
		m_orgimage.Destroy();
	if (m_image.IsOk())
		m_image.Destroy();

	if (!img)
		return;

	m_orgimage = img->Copy();
	if (m_orgimage.IsOk())
	{
		m_resized = new wxBitmap();
		m_image = m_orgimage.Copy();
	}
	else
		m_orgimage.Destroy();
}

void wxImagePanel::SetOverlayImage(wxString file, wxBitmapType format, bool show)
{
	/*if (!wxFileExists(file) || !m_orgimage || !m_orgimage->IsOk())
		return;

	m_olimage.Destroy();
	m_olimage.LoadFile(file, format);
	if (!m_olimage.IsOk())
		return;*/
}

void wxImagePanel::SetBackgroundImage(wxString file, wxBitmapType format)
{
	/*if (!wxFileExists(file) || !m_orgimage || !m_orgimage->IsOk())
		return;

	m_bgimage.Destroy();
	m_bgimage.LoadFile(file, format);
	if (!m_bgimage.IsOk())
		return;*/
}

void wxImagePanel::UpdateImage(bool ov_show)
{
	if (!m_orgimage.IsOk())
		return;

	int w = m_orgimage.GetSize().GetWidth();
	int h = m_orgimage.GetSize().GetHeight();

	//m_image = m_orgimage.Copy();

	//if (m_bgimage.IsOk())
	//{
	//	unsigned char *bgdata = m_bgimage.GetData();
	//	unsigned char *imdata = m_image.GetData();

	//	double alpha = 0.6;
	//	for (int y = 0; y < h; y++)
	//	{
	//		for(int x = 0; x < w; x++)
	//		{
	//			if (imdata[(y*w+x)*3] == 0 && imdata[(y*w+x)*3+1] == 0 && imdata[(y*w+x)*3+2] == 0)
	//			{
	//				imdata[(y*w+x)*3]   = bgdata[(y*w+x)*3];
	//				imdata[(y*w+x)*3+1] = bgdata[(y*w+x)*3+1];
	//				imdata[(y*w+x)*3+2] = bgdata[(y*w+x)*3+2];
	//			}
	//		}
	//	}
	//}
	//
	//if (m_olimage.IsOk() && ov_show)
	//{
	//	unsigned char *oldata = m_olimage.GetData();
	//	unsigned char *imdata = m_image.GetData();

	//	double alpha = 0.6;
	//	double alpha2 = 0.8;

	//	m_olimage.Blur(1);

	//	for (int y = 0; y < h; y++)
	//	{
	//		for(int x = 0; x < w; x++)
	//		{
	//			if (oldata[(y*w+x)*3] >= 64)
	//			{
	//				imdata[(y*w+x)*3]   = (unsigned char)(alpha*255.0 + (1.0-alpha)*(double)imdata[(y*w+x)*3]);
	//				imdata[(y*w+x)*3+1] = (unsigned char)(alpha*0.0   + (1.0-alpha)*(double)imdata[(y*w+x)*3+1]);
	//				imdata[(y*w+x)*3+2] = (unsigned char)(alpha*0.0   + (1.0-alpha)*(double)imdata[(y*w+x)*3+2]);
	//			}
	//			else if (oldata[(y*w+x)*3] > 0)
	//			{
	//				imdata[(y*w+x)*3]   = (unsigned char)(alpha2*0.0 + (1.0-alpha2)*(double)imdata[(y*w+x)*3]);
	//				imdata[(y*w+x)*3+1] = (unsigned char)(alpha2*0.0 + (1.0-alpha2)*(double)imdata[(y*w+x)*3+1]);
	//				imdata[(y*w+x)*3+2] = (unsigned char)(alpha2*0.0 + (1.0-alpha2)*(double)imdata[(y*w+x)*3+2]);
	//			}
	//		}
	//	}
	//}
}

void wxImagePanel::ResetImage()
{
	if (!m_orgimage.IsOk())
		return;

	m_image = m_orgimage.Copy();
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
	if (!m_orgimage.IsOk())
		return wxSize(-1, -1);

	int orgw = m_orgimage.GetWidth();
	int orgh = m_orgimage.GetHeight();

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
	if (!m_orgimage.IsOk())
		return 0.0;
	
	int orgw = m_orgimage.GetWidth();
	int orgh = m_orgimage.GetHeight();

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


BEGIN_EVENT_TABLE(NAListCtrl, wxListCtrl)
EVT_LIST_ITEM_SELECTED(wxID_ANY, NAListCtrl::OnSelect)
EVT_LIST_ITEM_DESELECTED(wxID_ANY, NAListCtrl::OnDeselect)
EVT_LIST_COL_BEGIN_DRAG(wxID_ANY, NAListCtrl::OnColBeginDrag)
EVT_LIST_ITEM_CHECKED(wxID_ANY, NAListCtrl::OnItemChecked)
EVT_LIST_ITEM_UNCHECKED(wxID_ANY, NAListCtrl::OnItemUnchecked)
EVT_KEY_DOWN(NAListCtrl::OnKeyDown)
EVT_KEY_UP(NAListCtrl::OnKeyUp)
EVT_MOUSE_EVENTS(NAListCtrl::OnMouse)
EVT_LEFT_DCLICK(NAListCtrl::OnLeftDClick)
EVT_SIZE(NAListCtrl::OnSize)
EVT_TIMER(NAL_SelTimer ,NAListCtrl::OnTimer)
END_EVENT_TABLE()

NAListCtrl::NAListCtrl(
	wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style) :
	wxListCtrl(parent, id, pos, size, style)
{
	m_plugin = NULL;
	m_images = NULL;
	m_vis_images = NULL;
	m_history_pos = 0;

	wxListItem itemCol;
	itemCol.SetText("");
	this->InsertColumn(0, itemCol);

	itemCol;
	itemCol.SetText("");
	this->InsertColumn(1, itemCol);

	itemCol.SetText("Name");
	this->InsertColumn(2, itemCol);

	itemCol.SetText("Thumbnail");
	this->InsertColumn(3, itemCol);

	SetColumnWidth(0, 25);
	SetColumnWidth(1, 0);
	SetColumnWidth(2, 110);
	SetColumnWidth(3, 300);

	bool chk = EnableCheckBoxes();

	//m_vis_images = new wxImageList(20, 20, false);
	//wxIcon icon0 = wxIcon(cross_xpm);
	//wxIcon icon1 = wxIcon(tick_xpm);
	//m_vis_images->Add(icon0);
	//m_vis_images->Add(icon1);
	//SetImageList(m_vis_images, wxIMAGE_LIST_SMALL);
    
    m_selTimer = new wxTimer(this, NAL_SelTimer);
}

NAListCtrl::~NAListCtrl()
{
    m_selTimer->Stop();
    wxDELETE(m_selTimer);
	wxDELETE(m_images);
	wxDELETE(m_vis_images);
}

void NAListCtrl::LoadResults(wxString idpath, wxString volpath, wxString chspec, wxString prefix)
{
	if (!m_plugin) return;

	m_plugin->runNALoader(idpath, volpath, chspec, prefix);

	m_plugin->SetSegmentVisibility(IMG_ID_REF, false);

	for (int i = 0; i < m_plugin->getSegCount(); i++)
		m_plugin->SetSegmentVisibility(i, true);

	UpdateResults(true);
}

void NAListCtrl::UpdateResults(bool refresh_items)
{
	if (!m_plugin) return;

	if (m_plugin->getSegCount() <= 0) return;

	SetEvtHandlerEnabled(false);

	bool ref_selected = false;
	bool bg_selected = false;
    int id_offset = m_plugin->isRefExists() ? 1 : 0;
	if (GetItemCount() > 0 && m_plugin->isRefExists())
		ref_selected = (GetItemState(0, wxLIST_STATE_SELECTED) != 0);
	if (GetItemCount() > id_offset)
		bg_selected = (GetItemState(id_offset, wxLIST_STATE_SELECTED) != 0);

	if (refresh_items)
	{
		DeleteAllItems();

		if (!m_listdata.empty()) m_listdata.clear();

		wxSize s = GetSize();
		int namew = GetColumnWidth(2) + GetColumnWidth(1) + GetColumnWidth(0) + 20;
		if (s.x - namew > 0)
			m_plugin->ResizeThumbnails(s.x - namew);

		int w = m_plugin->getSegMIPThumbnail(0)->GetWidth();
		int h = m_plugin->getSegMIPThumbnail(0)->GetHeight();

		if (m_images) wxDELETE(m_images);
		int img_count = 0;
		m_images = new wxImageList(w, h, false);
		SetImageList(m_images, wxIMAGE_LIST_SMALL);

		if (m_plugin->isRefExists())
		{
			NAListItemData ref_data;
			ref_data.name = "Reference";
			ref_data.mipid = img_count;
			ref_data.imgid = IMG_ID_REF;
			ref_data.visibility = m_plugin->GetSegmentVisibility(IMG_ID_REF);
			wxBitmap refbmp = wxBitmap(*m_plugin->getRefMIPThumbnail());
			m_images->Add(refbmp, wxBITMAP_TYPE_ANY);
			m_listdata.push_back(ref_data);
			Append(IMG_ID_REF, m_listdata[img_count].name, m_listdata[img_count].mipid, m_listdata[img_count].visibility > 0 ? true : false);
			img_count++;
		}

		for (int i = 0; i < m_plugin->getSegCount(); i++)
		{
			NAListItemData data;
			if (i == 0)
				data.name = "Background";
			else
				data.name = wxString::Format("Fragment %d", i);
			data.mipid = img_count;
			data.imgid = i;
			data.visibility = m_plugin->GetSegmentVisibility(i);
			wxBitmap bmp = wxBitmap(*m_plugin->getSegMIPThumbnail(i));
			m_images->Add(bmp, wxBITMAP_TYPE_ANY);
			m_listdata.push_back(data);
			Append(m_listdata[img_count].imgid, m_listdata[img_count].name, m_listdata[img_count].mipid, m_listdata[img_count].visibility > 0 ? true : false);
			img_count++;
		}
        
        m_plugin->setReloaded(false);

#ifdef _DARWIN
		SetColumnWidth(3, w + 8);
#else
		SetColumnWidth(3, w + 2);
#endif
	}
	else
	{
		if (m_plugin->isRefExists() && m_listdata.size() > 0)
			m_listdata[0].visibility = m_plugin->GetSegmentVisibility(IMG_ID_REF);

		int offset = m_plugin->isRefExists() ? 1 : 0;
		for (int i = 0; i < m_plugin->getSegCount() && i+offset < m_listdata.size(); i++)
			m_listdata[offset + i].visibility = m_plugin->GetSegmentVisibility(i);
	}

	if (ref_selected)
		SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	if (GetItemCount() > 0 && m_plugin->isRefExists())
		CheckItem(0, m_listdata[0].visibility > 0 ? true : false);
	if (bg_selected)
		SetItemState(1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	if (GetItemCount() > id_offset)
		CheckItem(id_offset, m_listdata[id_offset].visibility > 0 ? true : false);

	for (long i = id_offset + 1; i < GetItemCount() && i < m_listdata.size(); i++)
	{
		if (m_listdata[i].visibility == 2)
			SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		else
			SetItemState(i, 0, wxLIST_STATE_SELECTED);
		CheckItem(i, m_listdata[i].visibility > 0 ? true : false);
	}

	SetEvtHandlerEnabled(true);
	Update();

	m_plugin->setDirty(false);
}

void NAListCtrl::DeleteSelection()
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item != -1)
	{
		long sel = item;
		if (GetNextItem(item, wxLIST_NEXT_BELOW) == -1)
			sel = GetNextItem(item, wxLIST_NEXT_ABOVE);

		NAListItemData hdata;
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
			SetItemState(sel, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	}
}

void NAListCtrl::UpdateSelections()
{
    if (!m_plugin) return;
    
    if (m_plugin->getSegCount() <= 0) return;
    
    SetEvtHandlerEnabled(false);
    
    bool ref_selected = false;
    bool bg_selected = false;
    if (GetItemCount() > 0)
        ref_selected = (GetItemState(0, wxLIST_STATE_SELECTED) != 0);
    if (GetItemCount() > 1)
        bg_selected = (GetItemState(1, wxLIST_STATE_SELECTED) != 0);

    if (m_plugin->isRefExists() && m_listdata.size() > 0)
        m_listdata[0].visibility = m_plugin->GetSegmentVisibility(IMG_ID_REF);
    int offset = m_plugin->isRefExists() ? 1 : 0;
    for (int i = 0; i < m_plugin->getSegCount() && i+offset < m_listdata.size(); i++)
        m_listdata[offset + i].visibility = m_plugin->GetSegmentVisibility(i);
    
    if (ref_selected)
        SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    if (GetItemCount() > 0)
        CheckItem(0, m_listdata[0].visibility > 0 ? true : false);
    if (bg_selected)
        SetItemState(1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    if (GetItemCount() > 1)
        CheckItem(1, m_listdata[1].visibility > 0 ? true : false);
    
    for (long i = 2; i < GetItemCount() && i < m_listdata.size(); i++)
    {
        if (m_listdata[i].visibility == 2)
            SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        else
            SetItemState(i, 0, wxLIST_STATE_SELECTED);
        CheckItem(i, m_listdata[i].visibility > 0 ? true : false);
    }
    
    SetEvtHandlerEnabled(true);
    Update();
    
    m_plugin->setDirty(false);
}

void NAListCtrl::DeleteAll()
{
	DeleteAllItems();
	if (!m_history.empty()) m_history.clear();
	m_history_pos = 0;
}

void NAListCtrl::AddHistory(const NAListItemData& data)
{
	if (m_history_pos < m_history.size())
	{
		if (m_history_pos > 0)
			m_history = vector<NAListItemData>(m_history.begin(), m_history.begin() + m_history_pos);
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

void NAListCtrl::Undo()
{
	if (m_history_pos <= 0)
		return;

	m_history_pos--;
	NAListItemData comdata = m_history[m_history_pos];

	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	long sel = -1;
	if (item != -1)
	{
		if (item >= comdata.itemid)
			sel = item + 1;
	}

	Append(comdata.imgid, comdata.name, comdata.mipid, comdata.itemid);
}

void NAListCtrl::Redo()
{
	if (m_history_pos >= m_history.size())
		return;
	NAListItemData comdata = m_history[m_history_pos];

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

void NAListCtrl::OnColBeginDrag(wxListEvent& event)
{
	if (event.GetColumn() == 0 || event.GetColumn() == 1)
	{
		event.Veto();
	}
}

void NAListCtrl::Append(int imgid, wxString name, int mipid, bool visibility)
{
	wxString dbidstr = wxT("0");
	int itemid = GetItemCount();
	long tmp = InsertItem(itemid, wxString::Format("%d", imgid), -1);
	SetItem(tmp, 1, visibility ? _("+") : _(""));
	SetItem(tmp, 2, name);
	SetItem(tmp, 3, _(""), mipid);
}

wxString NAListCtrl::GetText(long item, int col)
{
	wxListItem info;
	info.SetId(item);
	info.SetColumn(col);
	info.SetMask(wxLIST_MASK_TEXT);
	GetItem(info);
	return info.GetText();
}

int NAListCtrl::GetImageId(long item, int col)
{
	wxListItem info;
	info.SetId(item);
	info.SetColumn(col);
	info.SetMask(wxLIST_MASK_IMAGE);
	GetItem(info);
	return info.GetImage();
}

void NAListCtrl::OnSelect(wxListEvent& event)
{
	if (event.GetIndex() != -1)
	{
		int id = wxAtoi(GetText(event.GetIndex(), 0));
        if (id <= 0 && IsItemChecked(event.GetIndex()))
        {
            notifyAll(NA_SWITCH_PROPPANEL, &id, sizeof(int));
        }
		else if (id > 0 && IsItemChecked(event.GetIndex()))
        {
            if (m_selTimer->IsRunning())
                m_selTimer->Stop();
            m_selTimer->StartOnce(30);
            NAListItemSel sel;
            sel.itemid = id;
            sel.visibility = 2;
            m_selbuf.push_back(sel);
			notifyAll(NA_SWITCH_PROPPANEL, &id, sizeof(int));
        }
	}
}

void NAListCtrl::OnDeselect(wxListEvent& event)
{
	if (event.GetIndex() != -1)
	{
		int id = wxAtoi(GetText(event.GetIndex(), 0));
		if (id > 0 && IsItemChecked(event.GetIndex()) && IsItemChecked(event.GetIndex()))
        {
            if (m_selTimer->IsRunning())
                m_selTimer->Stop();
            m_selTimer->StartOnce(30);
            NAListItemSel sel;
            sel.itemid = id;
            sel.visibility = 1;
            m_selbuf.push_back(sel);
			//notifyAll(NA_SET_VISIBLE, &id, sizeof(int));
        }
	}
}

void NAListCtrl::OnItemChecked(wxListEvent& event)
{
	if (event.GetIndex() != -1)
	{
		int id = wxAtoi(GetText(event.GetIndex(), 0));
		if (id > 0)
			notifyAll(NA_SET_ACTIVE, &id, sizeof(int));
		else
			notifyAll(NA_SET_VISIBLE, &id, sizeof(int));
	}
}

void NAListCtrl::OnItemUnchecked(wxListEvent& event)
{
	if (event.GetIndex() != -1)
	{
		int id = wxAtoi(GetText(event.GetIndex(), 0));
		notifyAll(NA_SET_INVISIBLE, &id, sizeof(int));
	}
}

void NAListCtrl::OnMouse(wxMouseEvent& event)
{
	event.Skip();
}

void NAListCtrl::OnScroll(wxScrollWinEvent& event)
{
	event.Skip(true);
}

void NAListCtrl::OnScroll(wxMouseEvent& event)
{
	event.Skip(true);
}

void NAListCtrl::OnKeyDown(wxKeyEvent& event)
{
	/*
	if ( event.GetKeyCode() == WXK_DELETE ||
		event.GetKeyCode() == WXK_BACK)
		DeleteSelection();
	*/

	if (event.GetKeyCode() == wxKeyCode('Z') && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
		Undo();

	if (event.GetKeyCode() == wxKeyCode('Y') && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
		Redo();

	if (event.GetKeyCode() == wxKeyCode('Z') && VRenderFrame::GetKeyState(WXK_CONTROL) && VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
		Redo();

	event.Skip();
}

void NAListCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void NAListCtrl::OnLeftDClick(wxMouseEvent& event)
{
	wxPoint pos = event.GetPosition();
	int flags = wxLIST_HITTEST_ONITEM;
	long item = HitTest(pos, flags, NULL);
	if (item != -1)
	{
		int id = wxAtoi(GetText(item, 0));
		if (IsItemChecked(item))
			CheckItem(item, false);
		else
			CheckItem(item, true);
		//notifyAll(NA_TOGGLE_VISIBILITY, &id, sizeof(int));
	}
}

void NAListCtrl::OnSize(wxSizeEvent& event)
{
	if (!m_plugin) return;

	if (m_plugin->isRefExists() || m_plugin->isSegExists())
		UpdateResults(true);

	event.Skip();
}

void NAListCtrl::OnTimer(wxTimerEvent& event)
{
    if (m_selbuf.size() > 0)
    {
        notifyAll(NA_PUSH_HISTORY, NULL, 0);
        for (auto d : m_selbuf)
        {
            if (d.visibility == 1)
                notifyAll(NA_SET_VISIBLE_WITHOUT_REFRESH, &(d.itemid), sizeof(int));
            else if (d.visibility == 2)
                notifyAll(NA_SET_ACTIVE_WITHOUT_REFRESH, &(d.itemid), sizeof(int));
        }
        notifyAll(NA_REFRESH_RENDERVIEWS, NULL, 0);
    }
    
    m_selbuf.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE( wxNASettingDialog, wxDialog )
	EVT_BUTTON( wxID_OK, wxNASettingDialog::OnOk )
END_EVENT_TABLE()

wxNASettingDialog::wxNASettingDialog(NAGuiPlugin *plugin, wxWindow* parent, wxWindowID id, const wxString &title,
												const wxPoint &pos, const wxSize &size, long style)
: wxDialog (parent, id, title, pos, size, style)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	wxString rpath, tmpdir, rnum;
	m_plugin = NULL;
	if (plugin)
	{
		m_plugin = plugin;
	}

	wxBoxSizer* itemBoxSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *sizer1 = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText *st = new wxStaticText(this, 0, "Rscript:", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_RPickCtrl = new wxFilePickerCtrl( this, ID_NAS_RPicker, rpath, _("Set a path to Rscript"), wxFileSelectorDefaultWildcardStr, wxDefaultPosition, wxSize(400, -1));
	sizer1->Add(5, 10);
	sizer1->Add(st, 0, wxALIGN_CENTER_VERTICAL);
	sizer1->Add(5, 10);
	sizer1->Add(m_RPickCtrl, 1, wxRIGHT|wxEXPAND);
	sizer1->Add(10, 10);
	itemBoxSizer->Add(5, 10);
	itemBoxSizer->Add(sizer1, 0, wxEXPAND);
		
	wxBoxSizer *sizer3 = new wxBoxSizer(wxHORIZONTAL);
	st = new wxStaticText(this, 0, "Temporary directory:", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_tmpdirPickCtrl = new wxDirPickerCtrl( this, ID_NAS_TempDirPicker, tmpdir, _("Choose a temp directory"), wxDefaultPosition, wxSize(400, -1));
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
    m_rnumTextCtrl = new wxTextCtrl( this, ID_NAS_ResultNumText, rnum, wxDefaultPosition, wxSize(30, -1), wxTE_RIGHT, vald_int);
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

void wxNASettingDialog::LoadSettings()
{

}

void wxNASettingDialog::SaveSettings()
{
	if (m_plugin)
	{
		
	}
}

void wxNASettingDialog::OnOk( wxCommandEvent& event )
{
	SaveSettings();
	event.Skip();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * NAGuiPluginWindow type definition
 */

IMPLEMENT_DYNAMIC_CLASS( NAGuiPluginWindow, wxGuiPluginWindowBase )


/*
 * NAGuiPluginWindow event table definition
 */

BEGIN_EVENT_TABLE( NAGuiPluginWindow, wxGuiPluginWindowBase )

////@begin NAGuiPluginWindow event table entries
    EVT_BUTTON( ID_SEND_EVENT_BUTTON, NAGuiPluginWindow::OnSENDEVENTBUTTONClick )
	EVT_MENU( ID_SAVE_BUTTON, NAGuiPluginWindow::OnSaveButtonClick )
	EVT_MENU( ID_EDIT_DB_BUTTON, NAGuiPluginWindow::OnEditDBButtonClick )
	EVT_MENU( ID_IMPORT_RESULTS_BUTTON, NAGuiPluginWindow::OnImportResultsButtonClick )
	EVT_MENU( ID_SETTING, NAGuiPluginWindow::OnSettingButtonClick )
    EVT_MENU( ID_NA_Export, NAGuiPluginWindow::OnExport )
	EVT_RADIOBUTTON(ID_SC_FWD, NAGuiPluginWindow::OnScoringMethodCheck)
	EVT_RADIOBUTTON(ID_SC_MEAN, NAGuiPluginWindow::OnScoringMethodCheck)
	EVT_CHECKBOX(ID_NA_OverlayCheckBox, NAGuiPluginWindow::OnOverlayCheck)
	EVT_CLOSE(NAGuiPluginWindow::OnClose)
	EVT_KEY_DOWN(NAGuiPluginWindow::OnKeyDown)
	EVT_SHOW(NAGuiPluginWindow::OnShowHide)
	EVT_KEY_UP(NAGuiPluginWindow::OnKeyUp)
	EVT_TIMER(ID_IdleTimer ,NAGuiPluginWindow::OnIdle)
////@end NAGuiPluginWindow event table entries

END_EVENT_TABLE()


/*
 * NAGuiPluginWindow constructors
 */

NAGuiPluginWindow::NAGuiPluginWindow()
{
    Init();
}

NAGuiPluginWindow::NAGuiPluginWindow( wxGuiPluginBase * plugin,
											   wxWindow* parent, wxWindowID id, 
											   const wxPoint& pos, const wxSize& size, 
											   long style )
{
    Init();
    Create(plugin, parent, id, pos, size, style);
}


/*
 * NAGuiPluginWindow creator
 */

bool NAGuiPluginWindow::Create(wxGuiPluginBase * plugin,
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

	((NAGuiPlugin *)GetPlugin())->addObserver(this);

    return true;
}


/*
 * NAGuiPluginWindow destructor
 */

NAGuiPluginWindow::~NAGuiPluginWindow()
{
	m_idleTimer->Stop();
	NAGuiPlugin* plugin = (NAGuiPlugin *)GetPlugin();
	if (plugin)
		plugin->SaveConfigFile();
}


/*
 * Member initialisation
 */

void NAGuiPluginWindow::Init()
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

	m_nbpanel = NULL;
	m_imgpanel = NULL;
}


/*
 * Control creation for NAGuiPluginWindow
 */

void NAGuiPluginWindow::CreateControls()
{    
	wxString rpath, nlibpath, outdir, rnum, scmtd;
	NAGuiPlugin* plugin = (NAGuiPlugin *)GetPlugin();

	SetEvtHandlerEnabled(false);
	Freeze();

	wxPanel* nbpanel = new wxPanel(this, wxID_ANY);
	nbpanel->SetWindowStyle(wxBORDER_SIMPLE);
	
	////@begin NAGuiPluginWindow content construction
	wxBoxSizer* itemBoxSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* itemBoxSizer2 = new wxBoxSizer(wxVERTICAL);

	wxIntegerValidator<unsigned int> vald_int;
	vald_int.SetMin(1);

#ifdef _WIN32
	int stsize = 120;
#else
	int stsize = 130;
#endif

	wxBoxSizer* sizert = new wxBoxSizer(wxHORIZONTAL);
	m_tb = new wxToolBar(nbpanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_FLAT | wxTB_TOP | wxTB_NODIVIDER | wxTB_TEXT | wxTB_NOICONS | wxTB_HORZ_LAYOUT);
	//m_tb->AddTool(ID_SAVE_BUTTON, "Save", wxNullBitmap, "Save search results");
	m_tb->AddTool(ID_IMPORT_RESULTS_BUTTON, "Import", wxNullBitmap, "Import search results");
    m_tb->AddTool(ID_NA_Export, "export", wxNullBitmap, "Export combined fragments");
	//m_tb->AddTool(ID_EDIT_DB_BUTTON, "Database", wxNullBitmap, "Edit NBLAST databases");
	//m_tb->AddTool(ID_SETTING, "Setting", wxNullBitmap, "Setting");
	m_tb->SetToolSeparation(20);
	m_tb->Realize();
	sizert->Add(m_tb, 1, wxEXPAND);
	itemBoxSizer2->Add(sizert, 0, wxEXPAND);
	wxStaticLine* stl = new wxStaticLine(nbpanel);
	itemBoxSizer2->Add(stl, 0, wxEXPAND);

	wxBoxSizer* sizerl = new wxBoxSizer(wxHORIZONTAL);
	m_results = new NAListCtrl(nbpanel, wxID_ANY, wxDefaultPosition, wxSize(500, 500));
	m_results->addObserver(this);
	m_results->SetPlugin(plugin);
	sizerl->Add(5, 10);
	sizerl->Add(m_results, 1, wxEXPAND);
	sizerl->Add(5, 10);
	itemBoxSizer2->Add(5, 3);
	itemBoxSizer2->Add(sizerl, 1, wxEXPAND);

	nbpanel->SetSizer(itemBoxSizer2);
	
	nbpanel->SetMinSize(wxSize(400, 700));
	
	itemBoxSizer->Add(nbpanel, 1, wxEXPAND);
	this->SetSizer(itemBoxSizer);
	this->Layout();

	////@end NAGuiPluginWindow content construction

	plugin->GetEventHandler()->Bind(wxEVT_GUI_PLUGIN_INTEROP,
		wxCommandEventHandler(NAGuiPluginWindow::OnInteropMessageReceived), this);

	m_nbpanel = nbpanel;
	
	Thaw();
	SetEvtHandlerEnabled(true);

	nbpanel->SetMinSize(wxSize(300, 400));

	m_idleTimer->Start(100);
	//m_wtimer->Start(50);
}

void NAGuiPluginWindow::EnableControls(bool enable)
{
	if (enable)
	{
		if (m_results) m_results->Enable();
	}
	else 
	{
		if (m_results) m_results->Disable();
	}
}

bool NAGuiPluginWindow::ShowToolTips()
{
    return true;
}

/*
 * Get bitmap resources
 */

wxBitmap NAGuiPluginWindow::GetBitmapResource( const wxString& name )
{
    // Bitmap retrieval
////@begin NAGuiPluginWindow bitmap retrieval
    wxUnusedVar(name);
    return wxNullBitmap;
////@end NAGuiPluginWindow bitmap retrieval
}

/*
 * Get icon resources
 */

wxIcon NAGuiPluginWindow::GetIconResource( const wxString& name )
{
    // Icon retrieval
////@begin NAGuiPluginWindow icon retrieval
    wxUnusedVar(name);
    return wxNullIcon;
////@end NAGuiPluginWindow icon retrieval
}

void NAGuiPluginWindow::doAction(ActionInfo *info)
{
	if (!info)
		return;
	int evid = info->id;
	
	NAGuiPlugin* plugin = (NAGuiPlugin *)GetPlugin();

	switch (evid)
	{
	case NA_TOGGLE_VISIBILITY:
		if (plugin)
		{
			plugin->PushVisHistory();
			int id = *(int*)(info->data);
			plugin->ToggleSegmentVisibility(id);
		}
		break;
	case NA_SET_VISIBLE:
		if (plugin)
		{
			plugin->PushVisHistory();
			int id = *(int*)(info->data);
			plugin->SetSegmentVisibility(id, 1);
		}
		break;
	case NA_SET_INVISIBLE:
		if (plugin)
		{
			plugin->PushVisHistory();
			int id = *(int*)(info->data);
			plugin->SetSegmentVisibility(id, 0);
		}
		break;
	case NA_SET_ACTIVE:
		if (plugin)
		{
			plugin->PushVisHistory();
			int id = *(int*)(info->data);
			plugin->SetSegmentVisibility(id, 2);
		}
		break;
    case NA_SET_VISIBLE_WITHOUT_REFRESH:
        if (plugin)
        {
            int id = *(int*)(info->data);
            plugin->SetSegmentVisibility(id, 1, false);
        }
        break;
    case NA_SET_ACTIVE_WITHOUT_REFRESH:
        if (plugin)
        {
            int id = *(int*)(info->data);
            plugin->SetSegmentVisibility(id, 2, false);
        }
        break;
    case NA_PUSH_HISTORY:
        if (plugin)
        {
            plugin->PushVisHistory();
        }
        break;
    case NA_REFRESH_RENDERVIEWS:
        if (plugin)
        {
            plugin->RefreshRenderViews();
        }
        break;
    case NA_SWITCH_PROPPANEL:
        if (plugin)
        {
            int id = *(int*)(info->data);
            bool isref = id == -2 ? true : false;
            plugin->SwitchPropPanel(isref);
        }
        break;
	default:
		break;
	}
    delete info;
}


void NAGuiPluginWindow::OnSENDEVENTBUTTONClick( wxCommandEvent& event )
{

    event.Skip();
}

wxWindow* NAGuiPluginWindow::CreateExtraNBLASTControl(wxWindow* parent)
{
#ifdef _WIN32
    wxPanel* panel = new wxPanel(parent, 0, wxDefaultPosition, wxSize(500, 90));
#else
    wxPanel* panel = new wxPanel(parent, 0, wxDefaultPosition, wxSize(530, 90));
#endif

	return panel;
}

void NAGuiPluginWindow::OnSWCExportCheck(wxCommandEvent& event)
{
}

void NAGuiPluginWindow::OnMIPImageExportCheck(wxCommandEvent& event)
{

}

void NAGuiPluginWindow::OnSWCImageExportCheck(wxCommandEvent& event)
{
}

void NAGuiPluginWindow::OnVolumeExportCheck(wxCommandEvent& event)
{
}

void NAGuiPluginWindow::OnScorePrefixCheck(wxCommandEvent& event)
{
}

void NAGuiPluginWindow::OnDatabasePrefixCheck(wxCommandEvent& event)
{
}

void NAGuiPluginWindow::OnSaveButtonClick( wxCommandEvent& event )
{
}

void NAGuiPluginWindow::OnEditDBButtonClick( wxCommandEvent& event )
{
}

void NAGuiPluginWindow::OnImportResultsButtonClick( wxCommandEvent& event )
{
	NAGuiPlugin* plugin = (NAGuiPlugin*)GetPlugin();

	if (!m_results || !plugin)
		return;

	wxFileDialog file_dlg(this, "Choose a label", "", "", "*.v3dpbd", wxFD_OPEN);
	int rval = file_dlg.ShowModal();
	if (rval != wxID_OK)
		return;
	wxString idpath = file_dlg.GetPath();
    
    wxString volpath = idpath.BeforeLast('.') + wxT(".h5j");

    if (!wxFileExists(volpath))
    {
        wxFileDialog file_dlg2(this, "Choose a confocal image file (h5j)", "", "", "All Supported|*.h5j", wxFD_OPEN);
        int rval2 = file_dlg2.ShowModal();
        if (rval2 != wxID_OK)
            return;
        volpath = file_dlg2.GetPath();
    }

	m_results->LoadResults(idpath, volpath, "sssr", "");

	if (m_results->GetItemCount() > 0)
	{
		m_nbpanel->SetMinSize(wxSize(1, 1));
	}
	else
	{
		m_nbpanel->SetMinSize(wxSize(400, 700));
	}
}

void NAGuiPluginWindow::OnExport( wxCommandEvent& event )
{
    NAGuiPlugin* plugin = (NAGuiPlugin*)GetPlugin();
    
    if (!m_results || !plugin)
        return;
    
    wxString vname = plugin->getVolumePath();
    
    vname = vname.AfterLast(GETSLASH());
    vname = vname.BeforeLast('.', NULL);
    
    wxFileDialog saveFileDialog(this, _("Save Combined Fragments"), "", vname, "Nrrd files (*.nrrd)|*.nrrd", wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    
    if (saveFileDialog.ShowModal() == wxID_CANCEL)
        return;
    
    wxString path = saveFileDialog.GetPath();
    
    plugin->SaveCombinedFragment(path);
}

void NAGuiPluginWindow::OnSettingButtonClick( wxCommandEvent& event )
{
	wxNASettingDialog sdlg((NAGuiPlugin *)GetPlugin() ,this, wxID_ANY, "NBLAST Settings", wxDefaultPosition, wxSize(450, 200));
	
	if (sdlg.ShowModal() == wxID_OK)
	{

	}
}

void NAGuiPluginWindow::OnOverlayCheck( wxCommandEvent& event )
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

void NAGuiPluginWindow::OnScoringMethodCheck( wxCommandEvent& event )
{
}

void NAGuiPluginWindow::OnClose(wxCloseEvent& event)
{
	event.Skip();
}

void NAGuiPluginWindow::OnShowHide(wxShowEvent& event)
{
	event.Skip();
}

void NAGuiPluginWindow::OnInteropMessageReceived(wxCommandEvent & event)
{
}

void NAGuiPluginWindow::OnIdle(wxTimerEvent& event)
{
	if (!IsShown())
		return;
/*	if (m_results)
	{
		if (VRenderFrame::GetKeyState(wxKeyCode('z')) && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
			m_results->Undo();

		if (VRenderFrame::GetKeyState(wxKeyCode('y')) && VRenderFrame::GetKeyState(WXK_CONTROL) && !VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
			m_results->Redo();

		if (VRenderFrame::GetKeyState(wxKeyCode('z')) && VRenderFrame::GetKeyState(WXK_CONTROL) && VRenderFrame::GetKeyState(WXK_SHIFT) && !VRenderFrame::GetKeyState(WXK_ALT))
			m_results->Redo();
	}
*/
	NAGuiPlugin* plugin = (NAGuiPlugin*)GetPlugin();

	if (!m_results || !plugin)
		return;

	if (plugin->isDirty())
	{
		m_results->UpdateResults(plugin->isReloaded());
	}
}

void NAGuiPluginWindow::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void NAGuiPluginWindow::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}
