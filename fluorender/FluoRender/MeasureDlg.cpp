#include "MeasureDlg.h"
#include "VRenderFrame.h"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <wx/artprov.h>
#include <wx/valnum.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/statline.h>
#include <wx/tokenzr.h>
#include "Formats/png_resource.h"
#include "ruler.xpm"

//resources
#include "img/icons.h"

BEGIN_EVENT_TABLE(RulerListCtrl, wxListCtrl)
	EVT_LIST_ITEM_DESELECTED(wxID_ANY, RulerListCtrl::OnEndSelection)
	EVT_LIST_ITEM_ACTIVATED(wxID_ANY, RulerListCtrl::OnAct)
	EVT_TEXT(ID_RulerNameDispText, RulerListCtrl::OnNameDispText)
	EVT_TEXT_ENTER(ID_RulerNameDispText, RulerListCtrl::OnEnterInTextCtrl)
	EVT_TEXT(ID_RulerDescriptionText, RulerListCtrl::OnDescriptionText)
	EVT_COLOURPICKER_CHANGED(ID_ColorPicker, RulerListCtrl::OnColorChange)
	EVT_TEXT_ENTER(ID_RulerDescriptionText, RulerListCtrl::OnEnterInTextCtrl)
	EVT_KEY_DOWN(RulerListCtrl::OnKeyDown)
	EVT_KEY_UP(RulerListCtrl::OnKeyUp)
	EVT_LIST_BEGIN_DRAG(wxID_ANY, RulerListCtrl::OnBeginDrag)
	EVT_LIST_COL_DRAGGING(wxID_ANY, RulerListCtrl::OnColumnSizeChanged)
    EVT_SCROLLWIN(RulerListCtrl::OnScroll)
	EVT_MOUSEWHEEL(RulerListCtrl::OnScroll)
	EVT_LEFT_DCLICK(RulerListCtrl::OnLeftDClick)
	EVT_LIST_COL_BEGIN_DRAG(wxID_ANY, RulerListCtrl::OnColBeginDrag)
END_EVENT_TABLE()

RulerListCtrl::RulerListCtrl(
	wxWindow* frame,
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

	m_show_anno = true;
	m_ruler_count = 0;

	wxListItem itemCol;
	itemCol.SetText("");
	this->InsertColumn(0, itemCol);
	SetColumnWidth(0, 0);
	itemCol.SetText("");
	this->InsertColumn(1, itemCol);
	SetColumnWidth(1, 0);
	itemCol.SetText("Name");
	this->InsertColumn(2, itemCol);
	SetColumnWidth(2, 100);
	itemCol.SetText("Color");
	this->InsertColumn(3, itemCol);
	SetColumnWidth(3, wxLIST_AUTOSIZE_USEHEADER);
	itemCol.SetText("Length");
	this->InsertColumn(4, itemCol);
	SetColumnWidth(4, wxLIST_AUTOSIZE_USEHEADER);
	itemCol.SetText("Angle/Pitch");
	this->InsertColumn(5, itemCol);
	SetColumnWidth(5, wxLIST_AUTOSIZE_USEHEADER);
	itemCol.SetText("Start/End Points (X, Y, Z)");
	this->InsertColumn(6, itemCol);
	SetColumnWidth(6, wxLIST_AUTOSIZE_USEHEADER);
	itemCol.SetText("Time");
	this->InsertColumn(7, itemCol);
	SetColumnWidth(7, wxLIST_AUTOSIZE_USEHEADER);
	itemCol.SetText("Volumes/Score");
	this->InsertColumn(8, itemCol);
	SetColumnWidth(8, wxLIST_AUTOSIZE_USEHEADER);
	//itemCol.SetText("Description");
	//this->InsertColumn(6, itemCol);
	//SetColumnWidth(6, 200);

	//m_images = new wxImageList(16, 16, true);
	//wxIcon icon = wxIcon(ruler_xpm);
	//m_images->Add(icon);
	//AssignImageList(m_images, wxIMAGE_LIST_SMALL);

	//frame edit
	m_name_disp = new wxTextCtrl(this, ID_RulerNameDispText, "",
		wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_name_disp->Hide();
    m_color_picker = new wxColourPickerCtrl(this, ID_ColorPicker,
        *wxBLACK, wxDefaultPosition, wxDefaultSize);
	m_color_picker->Hide();
	//description edit
	m_description_text = new wxTextCtrl(this, ID_RulerDescriptionText, "",
		wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_description_text->Hide();

	Thaw();
	SetEvtHandlerEnabled(true);
}

RulerListCtrl::~RulerListCtrl()
{
}

wxString RulerListCtrl::OnGetItemText(long item, long column) const
{
    //if (item < 0 || item >= m_list_items.size() || column < 0 || column >= m_list_items[item].size())
    //    return wxT("");
    //return m_list_items[item][column];
    
    wxString ret_str;
    
    vector<Ruler*>* ruler_list = m_view->GetRulerList();
    if (!ruler_list) return ret_str;
    
    if (m_counts.size() == 0)
        return ret_str;
    
    long count = 0;
    int cid = 0;
    if (item < m_counts[cid])
    {
        for (int i=0; i<(int)ruler_list->size(); i++)
        {
            Ruler* ruler = (*ruler_list)[i];
            if (!ruler) continue;
            if (ruler->GetTimeDep() &&
                ruler->GetTime() != m_view->m_glview->m_tseq_cur_num)
                continue;
            if (count == item)
            {
                switch(column)
                {
                    case 0: //name
                        ret_str = ruler->GetNameDisp();
                        break;
                    case 1: //type
                        ret_str = wxString::Format("%d", 0);
                        break;
                    case 2: //name
                        ret_str = ruler->GetNameDisp();
                        break;
                    case 3: //color
                        if (ruler->GetUseColor())
                            ret_str = wxString::Format("RGB(%d, %d, %d)",
                            int(ruler->GetColor().r()*255),
                            int(ruler->GetColor().g()*255),
                            int(ruler->GetColor().b()*255));
                        break;
                    case 4: //length
                    {
                        wxString unit;
                        switch (m_view->m_glview->m_sb_unit)
                        {
                        case 0:
                            unit = "nm";
                            break;
                        case 1:
                        default:
                            unit = L"\u03BCm";
                            break;
                        case 2:
                            unit = "mm";
                            break;
                        }
                        ret_str = wxString::Format("%.2f", ruler->GetLength()) + unit;
                    }
                        break;
                    case 5: //angle
                        ret_str = wxString::Format("%.1f", ruler->GetAngle()) + "Deg";
                        break;
                    case 6: //points
                        if (ruler->GetNumPoint() > 0)
                        {
                            Point *p = ruler->GetPoint(0);
                            ret_str += wxString::Format("(%.2f, %.2f, %.2f)", p->x(), p->y(), -p->z());
                        }
                        if (ruler->GetNumPoint() > 1)
                        {
                            Point *p = ruler->GetPoint(ruler->GetNumPoint() - 1);
                            ret_str += ", ";
                            ret_str += wxString::Format("(%.2f, %.2f, %.2f)", p->x(), p->y(), -p->z());
                        }
                        break;
                    case 7: //time
                        if (ruler->GetTimeDep())
                            ret_str = wxString::Format("%d", ruler->GetTime());
                        else
                            ret_str = "N/A";
                        break;
                    case 8: //info
                        ret_str = ruler->GetDelInfoValues(", ");
                        break;
                }
                return ret_str;
            }
            count++;
        }
    }
    
    count = m_counts[cid];
    cid++;
    if (m_counts.size() <= cid)
        return ret_str;
    
    int lnum = m_view->GetLayerNum();
    for (int i = 0; i < lnum; i++)
    {
        auto layer = m_view->GetLayer(i);
        if (!layer)
            continue;
        switch (layer->IsA())
        {
            case 4://annotations
            {
                Annotations* ann = (Annotations*)layer;
                if (!ann || !ann->GetDisp()) continue;
                if (m_counts.size() <= cid)
                    return ret_str;
                int tnum = ann->GetTextNum();
                if (item < count + m_counts[cid])
                {
                    int id = item - count;
                    switch(column)
                    {
                        case 0: //name
                            ret_str = ann->GetTextText(id) + "  (" + ann->GetName() + ")";
                            break;
                        case 1: //type
                            ret_str = wxString::Format("%d", 1);
                            break;
                        case 2: //name
                            ret_str = ann->GetTextText(id) + "  (" + ann->GetName() + ")";
                            break;
                        case 3: //color
                            break;
                        case 4: //length
                            break;
                        case 5: //angle
                            break;
                        case 6: //points
                            {
                                Point p = ann->GetTextPos(id);
                                int resx = 1, resy = 1, resz = 1;
                                VolumeData *vd = ann->GetVolume();
                                if (vd)
                                    vd->GetResolution(resx, resy, resz);
                                ret_str = wxString::Format("(%.2f, %.2f, %.2f)", p.x()*resx, p.y()*resy, p.z()*resz);
                            }
                            break;
                        case 7: //time
                            break;
                        case 8: //info
                            {
                                wxString info = ann->GetTextInfo(id);
                                wxStringTokenizer tokenizer(info, "\t");
                                while (tokenizer.HasMoreTokens())
                                {
                                    wxString token = tokenizer.GetNextToken();
                                    ret_str = ann->GetMesh() ? token : token + " voxels";
                                    break;
                                }
                            }
                            break;
                    }
                    return ret_str;
                }
                count += m_counts[cid];
                cid++;
            }
        }
    }
    
    return ret_str;
}

void RulerListCtrl::OnColBeginDrag(wxListEvent& event)
{
	if (event.GetColumn() == 0 || event.GetColumn() == 1)
	{
		event.Veto();
	}
}

long RulerListCtrl::GetCount(Annotations* ann)
{
    if (!m_view)
        return 0;

    vector<Ruler*>* ruler_list = m_view->GetRulerList();
    if (!ruler_list) return 0;

    int id = 0;
    int count = 0;
    wxString points;
    Point *p;
    int num_points;
    for (int i=0; i<(int)ruler_list->size(); i++)
    {
        Ruler* ruler = (*ruler_list)[i];
        if (!ruler) continue;
        if (ruler->GetTimeDep() &&
            ruler->GetTime() != m_view->m_glview->m_tseq_cur_num)
            continue;
        count++;
    }
    
    if (!ann)
        return count;
    id++;

    int lnum = m_view->GetLayerNum();
    for (int i = 0; i < lnum; i++)
    {
        auto layer = m_view->GetLayer(i);
        if (!layer)
            continue;
        switch (layer->IsA())
        {
            case 4://annotations
            {
                Annotations* annotations = (Annotations*)layer;
                if (!annotations || !annotations->GetDisp()) continue;
                int tnum = annotations->GetTextNum();
                if (ann == annotations && id < m_counts.size())
                    return m_counts[id];
                id++;
            }
            break;
        }
    }
    
    return 0;
}

void RulerListCtrl::Append(wxString name, wxString &color, double length, wxString &unit,
	double angle, wxString &points, bool time_dep, int time, wxString desc, int type)
{
	long pos = m_ruler_count;

	if (type == 0) m_ruler_count++;
	else pos = m_list_items.size();
    
    vector<wxString> data;
    
    data.push_back(name);
    data.push_back(wxString::Format("%d", type));
    data.push_back(name);
    data.push_back(color);
    wxString str = wxString::Format("%.2f", length) + unit;
    data.push_back(str);
    str = wxString::Format("%.1f", angle) + "Deg";
    data.push_back(str);
    data.push_back(points);
    if (time_dep)
        str = wxString::Format("%d", time);
    else
        str = "N/A";
    data.push_back(str);
    data.push_back(desc);
    
    m_list_items.insert(m_list_items.begin()+pos, data);
}

void RulerListCtrl::UpdateRulers(VRenderView* vrv, bool update_annotaions)
{
	m_name_disp->Hide();
    m_color_picker->Hide();
	m_description_text->Hide();
	m_editing_item = -1;

	if (vrv)
		m_view = vrv;

	vector<Ruler*>* ruler_list = m_view->GetRulerList();
	if (!ruler_list) return;

	if (update_annotaions)
        m_counts.clear();

	SetEvtHandlerEnabled(false);
    int count = 0;
	wxString points;
	Point *p;
	int num_points;
	for (int i=0; i<(int)ruler_list->size(); i++)
	{
		Ruler* ruler = (*ruler_list)[i];
		if (!ruler) continue;
		if (ruler->GetTimeDep() &&
			ruler->GetTime() != m_view->m_glview->m_tseq_cur_num)
			continue;
        count++;
	}
    
    if (m_counts.size() > 0)
        m_counts[0] = count;
    else
        m_counts.push_back(count);

	if (m_show_anno && update_annotaions) {
		int i;
		int lnum = m_view->GetLayerNum();
		for (i = 0; i < lnum; i++)
		{
			auto layer = m_view->GetLayer(i);
			if (!layer)
				continue;
			switch (layer->IsA())
			{
				case 4://annotations
				{
					Annotations* ann = (Annotations*)layer;
					if (!ann || !ann->GetDisp()) continue;
					int tnum = ann->GetTextNum();
                    
                    if (ann->GetMesh())
                    {
                        count = 0;
                        int st = 0;
                        int ed = tnum;
                        if (tnum > 10000)
                        {
                            for (int j = 0; j < tnum; j += 10000) {
                                wxString info = ann->GetTextInfo(j);
                                wxString score;
                                wxStringTokenizer tokenizer(info, "\t");
                                while (tokenizer.HasMoreTokens())
                                {
                                    wxString token = tokenizer.GetNextToken();
                                    score = token;
                                    break;
                                }
                                if ( ann->GetThreshold() <  STOD(score.ToStdString().c_str()) )
                                    st = j;
                                else
                                {
                                    ed = j;
                                    break;
                                }
                            }
                        }
                        
                        count = st;
                        for (int j = st; j < ed; j++) {
                            wxString info = ann->GetTextInfo(j);
                            wxString score;
                            wxStringTokenizer tokenizer(info, "\t");
                            while (tokenizer.HasMoreTokens())
                            {
                                wxString token = tokenizer.GetNextToken();
                                score = token;
                                break;
                            }
                            if ( ann->GetThreshold() <  STOD(score.ToStdString().c_str()) )
                                count++;
                            else
                                break;
                        }
                    }
                    else
                        count = tnum;
                    m_counts.push_back(count);
				}
                break;
			}
		}
	}

	SetEvtHandlerEnabled(true);
    
    long total_count = 0;
    for (long c : m_counts)
        total_count += c;
    
    SetItemCount(total_count);
    if (total_count > 0)
    {
        //RefreshItems(0, m_list_items.size()-1);
        
        if (GetItemCount() > 0) {
            SetColumnWidth(3, wxLIST_AUTOSIZE);
            SetColumnWidth(4, wxLIST_AUTOSIZE);
            SetColumnWidth(5, wxLIST_AUTOSIZE);
            SetColumnWidth(6, wxLIST_AUTOSIZE);
            SetColumnWidth(7, wxLIST_AUTOSIZE_USEHEADER);
            SetColumnWidth(8, wxLIST_AUTOSIZE_USEHEADER);
        }
    }
}

void RulerListCtrl::UpdateText(VRenderView* vrv)
{
	m_name_disp->Hide();
	m_description_text->Hide();
	m_editing_item = -1;

	if (vrv)
		m_view = vrv;

	vector<Ruler*>* ruler_list = m_view->GetRulerList();
	if (!ruler_list) return;

	wxString points;
	Point *p;
	int num_points;
	for (int i=0; i<(int)ruler_list->size(); i++)
	{
		Ruler* ruler = (*ruler_list)[i];
		if (!ruler) continue;
		if (ruler->GetTimeDep() &&
			ruler->GetTime() != m_view->m_glview->m_tseq_cur_num)
			continue;

		wxString unit;
		switch (m_view->m_glview->m_sb_unit)
		{
		case 0:
			unit = "nm";
			break;
		case 1:
		default:
			unit = L"\u03BCm";
			break;
		case 2:
			unit = "mm";
			break;
		}

		points = "";
		num_points = ruler->GetNumPoint();
		if (num_points > 0)
		{
			p = ruler->GetPoint(0);
			points += wxString::Format("(%.2f, %.2f, %.2f)", p->x(), p->y(), -p->z());
		}
		if (num_points > 1)
		{
			p = ruler->GetPoint(num_points - 1);
			points += ", ";
			points += wxString::Format("(%.2f, %.2f, %.2f)", p->x(), p->y(), -p->z());
		}
		wxString color;
		if (ruler->GetUseColor())
			color = wxString::Format("RGB(%d, %d, %d)",
			int(ruler->GetColor().r()*255),
			int(ruler->GetColor().g()*255),
			int(ruler->GetColor().b()*255));
		else
			color = "N/A";
        
        if(i < m_list_items.size() && m_list_items[i].size() >= 9)
        {
            SetText(i, 0, ruler->GetNameDisp());
            SetText(i, 2, ruler->GetNameDisp());
            SetText(i, 3, color);
            wxString length = wxString::Format("%.2f", ruler->GetLength()) + unit;
            SetText(i, 4, length);
            wxString angle = wxString::Format("%.1f", ruler->GetAngle()) + "Deg";
            SetText(i, 5, angle);
            SetText(i, 6, points);
            SetColumnWidth(6, wxLIST_AUTOSIZE);
            wxString time;
            if (ruler->GetTimeDep())
                time = wxString::Format("%d", ruler->GetTime());
            else
                time = "N/A";
            SetText(i, 7, time);
            SetText(i, 8, ruler->GetDelInfoValues(", "));
        }
	}
    if (m_list_items.size() > 0)
    {
        RefreshItems(0, ruler_list->size());
        SetColumnWidth(6, wxLIST_AUTOSIZE);
    }
	/*
	if (m_show_anno) {
		int i;
		int lnum = m_view->GetLayerNum();
		for (i = 0; i < lnum; i++)
		{
			auto layer = m_view->GetLayer(i);
			if (!layer)
				continue;
			switch (layer->IsA())
			{
				case 4://annotations
				{
					Annotations* ann = (Annotations*)layer;
					if (!ann || !ann->GetDisp()) continue;
					int tnum = ann->GetTextNum();
					for (int j = 0; j < tnum; j++) {
						wxString name = ann->GetTextText(j) + "  (" + ann->GetName() + ")";
						wxString color = "N/A";
						wxString unit = m_view->GetSBText();
						wxString length = wxString::Format("%.2f", 0.0) + unit;
						Point p = ann->GetTextPos(j);
						int resx = 1, resy = 1, resz = 1;
						VolumeData *vd = ann->GetVolume();
						if (vd) 
							vd->GetResolution(resx, resy, resz);
						wxString points = wxString::Format("(%.2f, %.2f, %.2f)", p.x()*resx, p.y()*resy, p.z()*resz);
						wxString info = ann->GetTextInfo(j);
						wxString voxnum;
						wxStringTokenizer tokenizer(info, "\t");
						while (tokenizer.HasMoreTokens())
						{
							wxString token = tokenizer.GetNextToken();
							voxnum = token + " voxels";
						}

						SetText(i, 0, name);
						SetText(i, 2, name);
						SetText(i, 3, color);
						SetText(i, 4, length);
						wxString angle = wxString::Format("%.1f", 0.0) + "Deg";
						SetText(i, 5, angle);
						SetText(i, 6, points);
						SetColumnWidth(6, wxLIST_AUTOSIZE);
						wxString time = "N/A";
						SetText(i, 7, time);
						SetText(i, 8, voxnum);
					}
				}
			}
		}
	}
	*/
	/*
	long item = GetItemCount() - 1;
	if (item != -1)
	SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	*/
}

void RulerListCtrl::DeleteSelection()
{
	if (!m_view) return;

	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item != -1)
	{
		wxString name = GetItemText(item);
		vector<Ruler*>* ruler_list = m_view->GetRulerList();
		if (ruler_list)
		{
			for (int i=0; i<(int)ruler_list->size(); i++)
			{
				Ruler* ruler = (*ruler_list)[i];
				if (ruler && ruler->GetNameDisp()==name)
				{
					ruler_list->erase(ruler_list->begin()+i);
					delete ruler;
				}
			}
			UpdateRulers();
			m_view->RefreshGL();
		}
	}
}

void RulerListCtrl::DeleteAll(bool cur_time)
{
	if (!m_view) return;

	vector<Ruler*>* ruler_list = m_view->GetRulerList();
	if (ruler_list)
	{
		if (cur_time)
		{
			int tseq = m_view->m_glview->m_tseq_cur_num;
			for (int i=ruler_list->size()-1; i>=0; i--)
			{
				Ruler* ruler = (*ruler_list)[i];
				if (ruler &&
					((ruler->GetTimeDep() &&
					ruler->GetTime() == tseq) ||
					!ruler->GetTimeDep()))
				{
					ruler_list->erase(ruler_list->begin()+i);
					delete ruler;
				}
			}
		}
		else
		{
			for (int i=ruler_list->size()-1; i>=0; i--)
			{
				Ruler* ruler = (*ruler_list)[i];
				if (ruler)
					delete ruler;
			}
			ruler_list->clear();
		}

		UpdateRulers();
		m_view->RefreshGL();
	}
}

void RulerListCtrl::Export(wxString filename)
{
	if (!m_view) return;
	vector<Ruler*>* ruler_list = m_view->GetRulerList();
    
    wxFileOutputStream fos(filename);
    if (!fos.Ok())
        return;
    wxTextOutputStream tos(fos);
    
    wxString str;
    wxString unit;
    int num_points;
    Point *p;
    Ruler* ruler;
    switch (m_view->m_glview->m_sb_unit)
    {
        case 0:
            unit = "nm";
            break;
        case 1:
        default:
            unit = L"\u03BCm";
            break;
        case 2:
            unit = "mm";
            break;
    }
    
    tos << "Name\tColor\tLength(" << unit << ")\tAngle/Pitch(Deg)\tx1\ty1\tz1\txn\tyn\tzn\tTime\tvolumes/scores\n";
    
    Color color;
    for (size_t i=0; i<ruler_list->size(); i++)
    {
        ruler = (*ruler_list)[i];
        if (!ruler) continue;
        
        tos << ruler->GetName() << "\t";
        if (ruler->GetUseColor())
        {
            color = ruler->GetColor();
            str = wxString::Format("RGB(%d, %d, %d)",
                                   int(color.r()*255), int(color.g()*255), int(color.b()*255));
        }
        else
            str = "N/A";
        tos << str << "\t";
        str = wxString::Format("%.2f", ruler->GetLength());
        tos << str << "\t";
        str = wxString::Format("%.1f", ruler->GetAngle());
        tos << str << "\t";
        str = "";
        num_points = ruler->GetNumPoint();
        if (num_points > 0)
        {
            p = ruler->GetPoint(0);
            str += wxString::Format("%.2f\t%.2f\t%.2f", p->x(), p->y(), -p->z());
        }
        if (num_points > 1)
        {
            p = ruler->GetPoint(num_points - 1);
            str += "\t";
            str += wxString::Format("%.2f\t%.2f\t%.2f", p->x(), p->y(), -p->z());
        }
        else
            str += "\t\t\t";
        tos << str << "\t";
        if (ruler->GetTimeDep())
            str = wxString::Format("%d", ruler->GetTime());
        else
            str = "N/A";
        tos << str << "\t";
        tos << ruler->GetInfoValues() << "\n";
        
    }
    
    if (m_show_anno) {
        int cid = 1;
        int i;
        int lnum = m_view->GetLayerNum();
        for (i = 0; i < lnum; i++)
        {
            auto layer = m_view->GetLayer(i);
            if (!layer)
                continue;
            switch (layer->IsA())
            {
                case 4://annotations
                {
                    Annotations* ann = (Annotations*)layer;
                    if (!ann || !ann->GetDisp()) continue;
                    if (cid >= m_counts.size()) return;
                    int tnum = ann->GetTextNum();
                    for (int j = 0; j < tnum && j < m_counts[cid]; j++) {
                        wxString name = ann->GetTextText(j) + "  (" + ann->GetName() + ")";
                        wxString color = "N/A";
                        wxString unit = m_view->GetSBText();
                        wxString length = wxString::Format("%.2f", 0.0);
                        Point p = ann->GetTextPos(j);
                        int resx = 1, resy = 1, resz = 1;
                        VolumeData *vd = ann->GetVolume();
                        if (vd)
                            vd->GetResolution(resx, resy, resz);
                        wxString points = wxString::Format("%.2f\t%.2f\t%.2f", p.x()*resx, p.y()*resy, p.z()*resz);
                        wxString info = ann->GetTextInfo(j);
                        wxStringTokenizer tokenizer(info, "\t");
                        wxString vx_or_score;
                        while (tokenizer.HasMoreTokens())
                        {
                            wxString token = tokenizer.GetNextToken();
                            vx_or_score = ann->GetMesh() ? token : token + " voxels";
                            break;
                        }
                        
                        tos << name << "\t";
                        tos << color << "\t";
                        tos << length << "\t";
                        wxString angle = wxString::Format("%.1f", 0.0);
                        tos << angle << "\t";
                        tos << points << "\t";
                        tos << "\t\t" << "\t";
                        wxString time = "N/A";
                        tos << time << "\t";
                        tos << vx_or_score << "\n";
                    }
                    cid++;
                }
            }
        }
    }
}

wxString RulerListCtrl::GetText(long item, int col)
{
    if (item < 0 || item >= m_list_items.size() || col < 0 || col >= m_list_items[item].size())
        return wxT("");
    return m_list_items[item][col];
}

void RulerListCtrl::SetText(long item, int col, const wxString &str)
{
    if (item < 0 || item >= m_list_items.size() || col < 0 || col >= m_list_items[item].size())
        return;
    m_list_items[item][col] = str;
}

void RulerListCtrl::ShowTextCtrls(long item)
{
	if (item != -1)
	{
		wxRect rect;
		wxString str;
		//add frame text
		GetSubItemRect(item, 2, rect);
		str = GetText(item, 2);
		rect.SetLeft(rect.GetLeft());
		rect.SetRight(rect.GetRight());
		m_name_disp->SetPosition(rect.GetTopLeft());
		m_name_disp->SetSize(rect.GetSize());
		m_name_disp->SetValue(str);
		m_name_disp->Show();
		//add color picker
		GetSubItemRect(item, 3, rect);
		m_color_picker->SetPosition(rect.GetTopLeft());
		m_color_picker->SetSize(rect.GetSize());
		if (m_view)
		{
			vector<Ruler*>* ruler_list = m_view->GetRulerList();
			Ruler* ruler = (*ruler_list)[m_editing_item];
			if (ruler)
			{
				Color color;
				if (ruler->GetUseColor())
				{
					color = ruler->GetColor();
					wxColor c(int(color.r()*255.0), int(color.g()*255.0), int(color.b()*255.0));
					m_color_picker->SetColour(c);
				}
			}
		}
		m_color_picker->Show();
		//add description text
		GetSubItemRect(item, 8, rect);
		str = GetText(item, 8);
		m_description_text->SetPosition(rect.GetTopLeft());
		m_description_text->SetSize(rect.GetSize());
		m_description_text->SetValue(str);
		//m_description_text->Show();
	}
}

void RulerListCtrl::OnAct(wxListEvent &event)
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

void RulerListCtrl::OnLeftDClick(wxMouseEvent& event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	m_editing_item = item;
	if (item != -1 && item < m_ruler_count)
	{
		wxPoint pos = event.GetPosition();
		ShowTextCtrls(item);
		wxRect rect;
		GetSubItemRect(item, 8, rect);
		if (rect.Contains(pos))
			m_description_text->SetFocus();
		else
			m_name_disp->SetFocus();
	}
}

void RulerListCtrl::EndEdit()
{
	if (m_name_disp->IsShown())
	{
		m_name_disp->Hide();
		m_color_picker->Hide();
		m_description_text->Hide();

		if (m_editing_item >= 0 && m_view && m_dragging_to_item == -1){
			vector<Ruler*>* ruler_list = m_view->GetRulerList();
			if (!ruler_list) return;

			if(m_editing_item < ruler_list->size())
			{
				wxString str = m_name_disp->GetValue();
				(*ruler_list)[m_editing_item]->SetNameDisp(str);
				//wxColor c = m_color_picker->GetColour();
				//Color color(c.Red()/255.0, c.Green()/255.0, c.Blue()/255.0);
				//(*ruler_list)[m_editing_item]->SetColor(color);
				//str = m_description_text->GetValue();
				//(*ruler_list)[m_editing_item]->SetDesc(str);
				SetItemState(m_editing_item, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED);
				m_editing_item = -1;
			}
		}

		//Update();
		UpdateText();//Update�ɂ���ƃf�o�b�O�Ŏ~�܂�--�����s��
		if(m_dragging_to_item == -1)m_view->RefreshGL();
		m_view->SetKeyLock(false);
	}
}

void RulerListCtrl::OnEndSelection(wxListEvent &event)
{
	EndEdit();
}

void RulerListCtrl::OnEnterInTextCtrl(wxCommandEvent& event)
{
	if (m_editing_item >= 0) SetItemState(m_editing_item, 0, wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED);
	EndEdit();
}

void RulerListCtrl::OnNameDispText(wxCommandEvent& event)
{
	if (m_editing_item == -1)
		return;

	wxString str = m_name_disp->GetValue();

	if (!m_view) return;
	m_view->SetKeyLock(true);
	/*
	vector<Ruler*>* ruler_list = m_view->GetRulerList();
	if (!ruler_list) return;

	if(m_editing_item >= 0 && m_editing_item < ruler_list->size())
	{
	(*ruler_list)[m_editing_item]->SetNameDisp(str);
	}
	*/
	SetText(m_editing_item, 0, str);
	SetText(m_editing_item, 2, str);
}

void RulerListCtrl::OnDescriptionText(wxCommandEvent& event)
{
	if (m_editing_item == -1)
		return;

	wxString str = m_description_text->GetValue();

	if (!m_view) return;
	m_view->SetKeyLock(true);
	/*
	vector<Ruler*>* ruler_list = m_view->GetRulerList();
	if (!ruler_list) return;

	if(m_editing_item >= 0 && m_editing_item < ruler_list->size())
	{
	(*ruler_list)[m_editing_item]->SetDesc(str);
	}
	*/
	SetText(m_editing_item, 8, str);
	SetText(m_editing_item, 8, str);
}

void RulerListCtrl::OnColorChange(wxColourPickerEvent& event)
{
	if (!m_view)
		return;
	if (m_editing_item == -1)
		return;

	wxColor c = event.GetColour();
	Color color(c.Red()/255.0, c.Green()/255.0, c.Blue()/255.0);
	vector<Ruler*>* ruler_list = m_view->GetRulerList();
	if (!ruler_list) return;
	Ruler* ruler = (*ruler_list)[m_editing_item];
	if (!ruler) return;
	ruler->SetColor(color);
	wxString str_color;
	str_color = wxString::Format("RGB(%d, %d, %d)",
		int(color.r()*255),
		int(color.g()*255),
		int(color.b()*255));
	SetText(m_editing_item, 3, str_color);
	m_view->RefreshGL();
}

void RulerListCtrl::OnBeginDrag(wxListEvent& event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item ==-1)
		return;

	if (item >= m_ruler_count) return;

	m_editing_item = -1;
	m_dragging_item = item;
	m_dragging_to_item = -1;
	// trigger when user releases left button (drop)
	Connect(wxEVT_MOTION, wxMouseEventHandler(RulerListCtrl::OnDragging), NULL, this);
	Connect(wxEVT_LEFT_UP, wxMouseEventHandler(RulerListCtrl::OnEndDrag), NULL, this);
	Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(RulerListCtrl::OnEndDrag), NULL,this);
	SetCursor(wxCursor(wxCURSOR_WATCH));

	m_name_disp->Hide();
	m_description_text->Hide();
	m_color_picker->Hide();
}

void RulerListCtrl::OnDragging(wxMouseEvent& event)
{
	wxPoint pos = event.GetPosition();
	int flags = wxLIST_HITTEST_ONITEM;
	long index = HitTest(pos, flags, NULL); // got to use it at last
	if (index >=0 && index != m_dragging_item && index != m_dragging_to_item && index < m_ruler_count)
	{
		if (!m_view) return;
		vector<Ruler*>* ruler_list = m_view->GetRulerList();
		if (!ruler_list) return;

		m_dragging_to_item = index;

		//change the content in the ruler list
		swap((*ruler_list)[m_dragging_item], (*ruler_list)[m_dragging_to_item]);

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

void RulerListCtrl::OnEndDrag(wxMouseEvent& event)
{
	SetCursor(wxCursor(*wxSTANDARD_CURSOR));
	Disconnect(wxEVT_MOTION, wxMouseEventHandler(RulerListCtrl::OnDragging));
	Disconnect(wxEVT_LEFT_UP, wxMouseEventHandler(RulerListCtrl::OnEndDrag));
	Disconnect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(RulerListCtrl::OnEndDrag));
	m_dragging_to_item = -1;
}

void RulerListCtrl::OnScroll(wxScrollWinEvent& event)
{
	EndEdit();
	event.Skip(true);
}

void RulerListCtrl::OnScroll(wxMouseEvent& event)
{
	EndEdit();
	event.Skip(true);
}

void RulerListCtrl::OnKeyDown(wxKeyEvent& event)
{
	if ( event.GetKeyCode() == WXK_DELETE ||
		event.GetKeyCode() == WXK_BACK)
		DeleteSelection();
	event.Skip();
}

void RulerListCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void RulerListCtrl::OnColumnSizeChanged(wxListEvent &event)
{
	if (m_editing_item == -1)
		return;

	wxRect rect;
	wxString str;
	GetSubItemRect(m_editing_item, 2, rect);
	rect.SetLeft(rect.GetLeft());
	rect.SetRight(rect.GetRight());
	m_name_disp->SetPosition(rect.GetTopLeft());
	m_name_disp->SetSize(rect.GetSize());
	GetSubItemRect(m_editing_item, 3, rect);
	m_color_picker->SetPosition(rect.GetTopLeft());
	m_color_picker->SetSize(rect.GetSize());
	GetSubItemRect(m_editing_item, 8, rect);
	m_description_text->SetPosition(rect.GetTopLeft());
	m_description_text->SetSize(rect.GetSize());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
BEGIN_EVENT_TABLE(MeasureDlg, wxPanel)
	EVT_MENU(ID_LocatorBtn, MeasureDlg::OnNewLocator)
	EVT_MENU(ID_RulerBtn, MeasureDlg::OnNewRuler)
	EVT_MENU(ID_RulerMPBtn, MeasureDlg::OnNewRulerMP)
	EVT_MENU(ID_RulerEditBtn, MeasureDlg::OnRulerEdit)
	EVT_MENU(ID_DeleteBtn, MeasureDlg::OnDelete)
	EVT_MENU(ID_DeleteAllBtn, MeasureDlg::OnDeleteAll)
    EVT_MENU(ID_ImportBtn, MeasureDlg::OnImport)
	EVT_MENU(ID_ExportBtn, MeasureDlg::OnExport)
	EVT_COMBOBOX(ID_IntensityMethodsCombo, MeasureDlg::OnIntensityMethodsCombo)
	EVT_CHECKBOX(ID_UseTransferChk, MeasureDlg::OnUseTransferCheck)
	EVT_CHECKBOX(ID_TransientChk, MeasureDlg::OnTransientCheck)
    EVT_BUTTON(ID_WarpBtn, MeasureDlg::OnWarp)
	EVT_BUTTON(ID_ScatterBtn, MeasureDlg::OnScatterRulers)
	END_EVENT_TABLE()

MeasureDlg::MeasureDlg(wxWindow* frame, wxWindow* parent,
	wxWindowID id,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name) :
wxPanel(parent, id, pos, size, style, name),
	m_frame(parent),
	m_view(0)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	//validator: integer
	wxIntegerValidator<unsigned int> vald_int;

	//toolbar
	m_toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_FLAT|wxTB_TOP|wxTB_NODIVIDER);
    m_toolbar->SetToolBitmapSize(wxSize(20,20));
	m_toolbar->AddCheckTool(ID_LocatorBtn, "Locator",
		wxGetBitmapFromMemory(listicon_locator_24x24),
		wxNullBitmap,
		"Add locators to the render view by clicking");
	m_toolbar->AddCheckTool(ID_RulerBtn, "2pt Ruler",
		wxGetBitmapFromMemory(listicon_line_24x24),
		wxNullBitmap,
		"Add rulers to the render view by clicking two end points");
	m_toolbar->AddCheckTool(ID_RulerMPBtn, "2+pt Ruler",
		wxGetBitmapFromMemory(listicon_polyline_24x24),
		wxNullBitmap,
		"Add a polyline ruler to the render view by clicking its points");
	m_toolbar->AddCheckTool(ID_RulerEditBtn, "Edit",
		wxGetBitmapFromMemory(listicon_edit_24x24),
		wxNullBitmap,
		"Select and move ruler points");
	m_toolbar->AddTool(ID_DeleteBtn, "Delete",
		wxGetBitmapFromMemory(listicon_delete),
		"Delete a selected ruler");
	m_toolbar->AddTool(ID_DeleteAllBtn,"Delete All",
		wxGetBitmapFromMemory(listicon_delall),
		"Delete all rulers");
	m_toolbar->AddTool(ID_ExportBtn, "Export",
		wxGetBitmapFromMemory(listicon_save),
		"Export rulers to a text file");
    m_toolbar->AddTool(ID_ImportBtn, "Import",
        wxGetBitmapFromMemory(plus),
        "Import rulers from a BigWarp csv");
	m_toolbar->Realize();

	wxStaticLine *st_line = new wxStaticLine(this);

#ifdef _DARWIN
    wxStaticText *st = new wxStaticText(this, 0, "Pointing Method:",
        wxDefaultPosition, wxSize(110, -1), wxALIGN_CENTER);
#else
	wxStaticText *st = new wxStaticText(this, 0, "Pointing Method:",
		wxDefaultPosition, wxSize(100, -1), wxALIGN_CENTER);
#endif

	//options
#ifdef _DARWIN
    wxBoxSizer* sizer_1 = new wxBoxSizer(wxHORIZONTAL);
    m_int_method_combo = new wxComboBox(this, ID_IntensityMethodsCombo, "",
        wxDefaultPosition, wxSize(162, 30), 0, NULL, wxCB_READONLY);
#else
    wxBoxSizer* sizer_1 = new wxBoxSizer(wxHORIZONTAL);
    m_int_method_combo = new wxComboBox(this, ID_IntensityMethodsCombo, "",
        wxDefaultPosition, wxSize(155, 24), 0, NULL, wxCB_READONLY);
#endif
	vector<string> int_method_list;
	int_method_list.push_back("View Plane");
	int_method_list.push_back("Maximum Intensity");
	int_method_list.push_back("Accumulated Intensity");
	for (size_t i=0; i<int_method_list.size(); ++i)
		m_int_method_combo->Append(int_method_list[i]);
	m_int_method_combo->SetSelection(0);
	sizer_1->Add(st, 0, wxALIGN_CENTER);
	sizer_1->Add(10, 10);
	sizer_1->Add(m_int_method_combo, 0, wxALIGN_CENTER);

	//more options
	wxBoxSizer* sizer_2 = new wxBoxSizer(wxHORIZONTAL);
	m_transient_chk = new wxCheckBox(this, ID_TransientChk, "Transient",
		wxDefaultPosition, wxDefaultSize);
	m_use_transfer_chk = new wxCheckBox(this, ID_UseTransferChk, "Use Volume Properties",
		wxDefaultPosition, wxDefaultSize);
	m_scatter_btn = new wxButton(this, ID_ScatterBtn, "Scatter Rulers",
		wxDefaultPosition, wxSize(120, 20));
	wxStaticText* st2 = new wxStaticText(this, 0, "Density:",
		wxDefaultPosition, wxSize(50, -1), wxALIGN_CENTER);
	m_density_txt = new wxTextCtrl(this, ID_DensityText, "1000",
		wxDefaultPosition, wxSize(60, 20), 0, vald_int);
    m_warp_btn = new wxButton(this, ID_WarpBtn, "Apply Transform",
        wxDefaultPosition, wxSize(120, 20));
	sizer_2->Add(10, 10);
	sizer_2->Add(m_transient_chk, 0, wxALIGN_CENTER);
	sizer_2->Add(10, 10);
	sizer_2->Add(m_use_transfer_chk, 0, wxALIGN_CENTER);
	sizer_2->Add(10, 10);
	sizer_2->Add(m_scatter_btn, 0, wxALIGN_CENTER);
	sizer_2->Add(2, 10);
	sizer_2->Add(st2, 0, wxALIGN_CENTER);
	sizer_2->Add(1, 10);
	sizer_2->Add(m_density_txt, 0, wxALIGN_CENTER);
    sizer_2->Add(30, 10);
    sizer_2->Add(m_warp_btn, 0, wxALIGN_CENTER);
    //m_warp_btn->Hide();

	//list
	m_rulerlist = new RulerListCtrl(frame, this, wxID_ANY);

	//sizer
	wxBoxSizer *sizerV = new wxBoxSizer(wxVERTICAL);
	sizerV->Add(m_toolbar, 0, wxEXPAND);
	sizerV->Add(st_line, 0, wxEXPAND);
	sizerV->Add(10, 10);
	sizerV->Add(sizer_1, 0, wxEXPAND);
	sizerV->Add(10, 10);
	sizerV->Add(sizer_2, 0, wxEXPAND);
	sizerV->Add(10, 10);
	sizerV->Add(m_rulerlist, 1, wxEXPAND);

	SetSizer(sizerV);
	Layout();

	Thaw();
	SetEvtHandlerEnabled(true);
}

MeasureDlg::~MeasureDlg()
{
}

void MeasureDlg::GetSettings(VRenderView* vrv, bool update_annotaions)
{
	m_view = vrv;
	UpdateList(update_annotaions);
	if (m_view && m_view->m_glview)
	{
		m_toolbar->ToggleTool(ID_LocatorBtn, false);
		m_toolbar->ToggleTool(ID_RulerBtn, false);
		m_toolbar->ToggleTool(ID_RulerMPBtn, false);
		m_toolbar->ToggleTool(ID_RulerEditBtn, false);

		int int_mode = m_view->m_glview->GetIntMode();
		if (int_mode == 5 || int_mode == 7)
		{
			int ruler_type = m_view->GetRulerType();
			if (ruler_type == 0)
				m_toolbar->ToggleTool(ID_RulerBtn, true);
			else if (ruler_type == 1)
				m_toolbar->ToggleTool(ID_RulerMPBtn, true);
			else if (ruler_type == 2)
				m_toolbar->ToggleTool(ID_LocatorBtn, true);
		}
		else if (int_mode == 6)
			m_toolbar->ToggleTool(ID_RulerEditBtn, true);

		m_int_method_combo->SetSelection(m_view->m_glview->m_point_volume_mode);

		m_use_transfer_chk->SetValue(m_view->m_glview->m_ruler_use_transf);
		m_transient_chk->SetValue(m_view->m_glview->m_ruler_time_dep);
	}
}

VRenderView* MeasureDlg::GetView()
{
	return m_view;
}

void MeasureDlg::UpdateList(bool update_annotaions)
{
	if (!m_view) return;
	m_rulerlist->UpdateRulers(m_view, update_annotaions);
}

void MeasureDlg::OnNewLocator(wxCommandEvent& event)
{
	if (!m_view) return;

	if (m_toolbar->GetToolState(ID_RulerMPBtn))
		m_view->FinishRuler();

	m_toolbar->ToggleTool(ID_RulerBtn, false);
	m_toolbar->ToggleTool(ID_RulerMPBtn, false);
	m_toolbar->ToggleTool(ID_RulerEditBtn, false);

	if (m_toolbar->GetToolState(ID_LocatorBtn))
	{
		m_view->SetIntMode(5);
		m_view->SetRulerType(2);
	}
	else
	{
		m_view->SetIntMode(1);
	}
}

void MeasureDlg::OnNewRuler(wxCommandEvent& event)
{
	if (!m_view) return;

	if (m_toolbar->GetToolState(ID_RulerMPBtn))
		m_view->FinishRuler();

	m_toolbar->ToggleTool(ID_LocatorBtn, false);
	m_toolbar->ToggleTool(ID_RulerMPBtn, false);
	m_toolbar->ToggleTool(ID_RulerEditBtn, false);

	if (m_toolbar->GetToolState(ID_RulerBtn))
	{
		m_view->SetIntMode(5);
		m_view->SetRulerType(0);
	}
	else
	{
		m_view->SetIntMode(1);
	}
}

void MeasureDlg::OnNewRulerMP(wxCommandEvent& event)
{
	if (!m_view) return;

	m_toolbar->ToggleTool(ID_LocatorBtn, false);
	m_toolbar->ToggleTool(ID_RulerBtn, false);
	m_toolbar->ToggleTool(ID_RulerEditBtn, false);

	if (m_toolbar->GetToolState(ID_RulerMPBtn))
	{
		m_view->SetIntMode(5);
		m_view->SetRulerType(1);
	}
	else
	{
		m_view->SetIntMode(1);
		m_view->FinishRuler();
	}
}

void MeasureDlg::OnRulerEdit(wxCommandEvent& event)
{
	if (!m_view) return;

	if (m_toolbar->GetToolState(ID_RulerMPBtn))
		m_view->FinishRuler();

	m_toolbar->ToggleTool(ID_LocatorBtn, false);
	m_toolbar->ToggleTool(ID_RulerBtn, false);
	m_toolbar->ToggleTool(ID_RulerMPBtn, false);

	if (m_toolbar->GetToolState(ID_RulerEditBtn))
		m_view->SetIntMode(6);
	else
		m_view->SetIntMode(1);
}

void MeasureDlg::OnDelete(wxCommandEvent& event)
{
	m_rulerlist->DeleteSelection();
}

void MeasureDlg::OnDeleteAll(wxCommandEvent& event)
{
	m_rulerlist->DeleteAll();
}

void MeasureDlg::OnImport(wxCommandEvent& event)
{
    wxFileDialog *fopendlg = new wxFileDialog(
        m_frame, "Import rulers", "", "",
        "BigWarp landmarks (*.csv)|*.csv",
        wxFD_OPEN);

    int rval = fopendlg->ShowModal();

    if (rval == wxID_OK)
    {
        wxString filename = fopendlg->GetPath();
        wxString suffix = filename.Mid(filename.Find('.', true)).MakeLower();
        if (suffix == ".csv")
        {
            if (m_view)
                m_view->ImportBigWarpCSV(filename);
        }
    }

    if (fopendlg)
        delete fopendlg;
}

void MeasureDlg::OnExport(wxCommandEvent& event)
{
	wxFileDialog *fopendlg = new wxFileDialog(
		m_frame, "Export rulers", "", "",
        "Text file (*.txt)|*.txt|"\
        "BigWarp landmarks (*.csv)|*.csv",
		wxFD_SAVE|wxFD_OVERWRITE_PROMPT);

	int rval = fopendlg->ShowModal();

	if (rval == wxID_OK)
	{
		wxString filename = fopendlg->GetPath();
        wxString suffix = filename.Mid(filename.Find('.', true)).MakeLower();
        if (suffix == ".txt")
            m_rulerlist->Export(filename);
        else if (suffix == ".csv")
        {
            if (m_view)
                m_view->ExportBigWarpCSV(filename);
        }
	}

	if (fopendlg)
		delete fopendlg;
}

void MeasureDlg::OnIntensityMethodsCombo(wxCommandEvent& event)
{
	if (!m_view || !m_view->m_glview)
		return;

	int mode = m_int_method_combo->GetCurrentSelection();
	m_view->SetPointVolumeMode(mode);
	VRenderFrame* frame = (VRenderFrame*)m_frame;
	if (frame && frame->GetSettingDlg())
		frame->GetSettingDlg()->SetPointVolumeMode(mode);
}

void MeasureDlg::OnUseTransferCheck(wxCommandEvent& event)
{
	if (!m_view || !m_view->m_glview)
		return;

	bool use_transfer = m_use_transfer_chk->GetValue();
	m_view->SetRulerUseTransf(use_transfer);
	VRenderFrame* frame = (VRenderFrame*)m_frame;
	if (frame && frame->GetSettingDlg())
		frame->GetSettingDlg()->SetRulerUseTransf(use_transfer);
}

void MeasureDlg::OnTransientCheck(wxCommandEvent& event)
{
	if (!m_view || !m_view->m_glview)
		return;

	bool val = m_transient_chk->GetValue();
	m_view->SetRulerTimeDep(val);
	VRenderFrame* frame = (VRenderFrame*)m_frame;
	if (frame && frame->GetSettingDlg())
		frame->GetSettingDlg()->SetRulerTimeDep(val);
}

void MeasureDlg::OnWarp(wxCommandEvent& event)
{
    if (!m_view)
        return;

    m_view->WarpCurrentVolume();
}

void MeasureDlg::OnScatterRulers(wxCommandEvent& event)
{
	if (!m_view)
		return;

	wxString str = m_density_txt->GetValue();
	long ival = 0;
	str.ToLong(&ival);
	m_view->ScatterRulers(ival);
}
