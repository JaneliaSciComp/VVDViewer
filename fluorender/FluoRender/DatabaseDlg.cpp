#include "DatabaseDlg.h"
#include "VRenderFrame.h"
#include <wx/valnum.h>
#include <wx/tokenzr.h>
#include <wx/msgdlg.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <curl/curl.h>
#include <sstream>
#include <wx/stdpaths.h>

#include "cube.xpm"
#include "folder.xpm"
#include "database.xpm"

int DBTreeCtrl::catid = 0;

size_t callbackWrite(char *ptr, size_t size, size_t nmemb, string *stream)
{
	int dataLength = size * nmemb;
	stream->append(ptr, dataLength);
	return dataLength;
}


BEGIN_EVENT_TABLE(DBTreeCtrl, wxTreeCtrl)
EVT_MENU(ID_Expand, DBTreeCtrl::OnExpand)
EVT_TREE_ITEM_ACTIVATED(wxID_ANY, DBTreeCtrl::OnAct)
EVT_KEY_DOWN(DBTreeCtrl::OnKeyDown)
EVT_KEY_UP(DBTreeCtrl::OnKeyUp)
END_EVENT_TABLE()

DBTreeCtrl::DBTreeCtrl(
		wxWindow* frame,
		wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos,
		const wxSize& size,
		long style) :
	wxTreeCtrl(parent, id, pos, size, style),
	m_frame(frame)
{
	wxImageList *images = new wxImageList(24, 24, true);
	wxIcon icons[3];
	icons[0] = wxIcon(cube_xpm);
	icons[1] = wxIcon(folder_xpm);
	icons[2] = wxIcon(database_xpm);
	images->Add(icons[0]);
	images->Add(icons[1]);
	images->Add(icons[2]);
	AssignImageList(images);
	SetDoubleBuffered(true); 

//	ExpandAll();
//	ScrollTo(item);
}

bool DBTreeCtrl::LoadDatabase(wxString path)
{
	tinyxml2::XMLDocument doc;
	if (doc.LoadFile(path.ToStdString().c_str()) != 0)
	{
		extern CURL *_g_curl;
		CURLcode ret;
		string catalogdata;
		if (_g_curl == NULL) {
			cerr << "curl_easy_init() failed" << endl;
			return false;
		}
		curl_easy_reset(_g_curl);
		curl_easy_setopt(_g_curl, CURLOPT_URL, path.ToStdString().c_str());
		curl_easy_setopt(_g_curl, CURLOPT_TIMEOUT, 10L);
		curl_easy_setopt(_g_curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
		curl_easy_setopt(_g_curl, CURLOPT_WRITEFUNCTION, callbackWrite);
		curl_easy_setopt(_g_curl, CURLOPT_WRITEDATA, &catalogdata);
		//�s�A�ؖ������؂Ȃ�
		curl_easy_setopt(_g_curl, CURLOPT_SSL_VERIFYPEER, 0);
		ret = curl_easy_perform(_g_curl);
		if (ret != CURLE_OK) {
			cerr << "curl_easy_perform() failed." << curl_easy_strerror(ret) << endl;
			return false;
		}
		
		if (doc.Parse(catalogdata.c_str()) != 0) {
			return false;
		}
	}
	
	tinyxml2::XMLElement *root = doc.RootElement();
	if (!root || strcmp(root->Name(), "Catalog"))
		return false;

	wxTreeItemId rootitem = GetRootItem();
	if (!rootitem.IsOk())
	{
		rootitem = AddRoot(wxT("Root"));
		DBItemInfo* item_data = new DBItemInfo;
		item_data->type = 0;//root;
		SetItemData(rootitem, item_data);
	}

	wxString str;
	str = root->Attribute("Name");
	if(str.IsEmpty()) str = path;

	DBCatalog cat;
	cat.name = str;
	cat.path = path;

	wxTreeItemId item = AppendItem(rootitem, str, 2);
	DBItemInfo* item_data = new DBItemInfo;
	item_data->type = 3;//catalog;
	item_data->data_url = path;
	item_data->sample_id = catid++;
	cat.id = item_data->sample_id;
	SetItemData(item, item_data);

	if (!ann_buf.empty()) ann_buf.clear();

	ReadNode(root, item, &cat);

	cats.push_back(cat);

	return true;
}

void DBTreeCtrl::ReadNode(tinyxml2::XMLElement *lvRootNode, wxTreeItemId parent, DBCatalog *cat)
{
	wxString str, str2;
	long ival;
	int level;

	tinyxml2::XMLElement *child = lvRootNode->FirstChildElement();
	while (child)
	{
		if (strcmp(child->Name(), "Group") == 0)
		{
			str = child->Attribute("Name");
			wxTreeItemId item = AppendItem(parent, str, 1);
			DBItemInfo* item_data = new DBItemInfo;
			item_data->type = 1;//group;
			SetItemData(item, item_data);

			ReadNode(child, item, cat);
		}
		if (strcmp(child->Name(), "Data") == 0)
		{
			DBItemInfo* item_data = new DBItemInfo;
			str = child->Attribute("Name");
			str2 = child->Attribute("Description");
			item_data->desc = str + wxT(" ") + str2;
			item_data->desc.MakeLower();
			item_data->name = str + wxT("      ") + str2;
			if (cat) item_data->cat_name = cat->name;
			wxTreeItemId item = AppendItem(parent, item_data->name, 0);
			item_data->type = 2;//data;
			str = child->Attribute("DataURL");
			item_data->data_url = str;
			str = child->Attribute("SampleID");
			str.ToLong(&ival);
			item_data->sample_id = ival;

			str = child->Attribute("AnnotationURL");
			item_data->annotation_url = str;
			ReadAnnotations(str, item_data->annotations);

			SetItemData(item, item_data);

			if (cat) cat->items.push_back(item_data);
		}
		child = child->NextSiblingElement();
	}
}

void DBTreeCtrl::ReadAnnotations(wxString url, vector<AnnotationDB> &annotations)
{
	if(url.IsEmpty()) return;

	if (ann_buf.find(url) != ann_buf.end())
	{
		annotations = ann_buf[url];
		return;
	}
	else
	{
		if (!annotations.empty()) annotations.clear();
		ann_buf[url] = annotations;
	}

	wxString tmp_path = url;
	tmp_path.Replace(wxT("/"), wxT("\\"));

	tinyxml2::XMLDocument doc;
	if (doc.LoadFile(tmp_path.ToStdString().c_str()) != 0)
	{
		extern CURL *_g_curl;
		CURLcode ret;
		string anndata;
		if (_g_curl == NULL) {
			cerr << "curl_easy_init() failed" << endl;
			return;
		}
		curl_easy_reset(_g_curl);
		curl_easy_setopt(_g_curl, CURLOPT_URL, url.ToStdString().c_str());
		curl_easy_setopt(_g_curl, CURLOPT_TIMEOUT, 10L);
		curl_easy_setopt(_g_curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
		curl_easy_setopt(_g_curl, CURLOPT_WRITEFUNCTION, callbackWrite);
		curl_easy_setopt(_g_curl, CURLOPT_WRITEDATA, &anndata);
		//�s�A�ؖ������؂Ȃ�
		curl_easy_setopt(_g_curl, CURLOPT_SSL_VERIFYPEER, 0);
		ret = curl_easy_perform(_g_curl);
		if (ret != CURLE_OK) {
			cerr << "curl_easy_perform() failed." << curl_easy_strerror(ret) << endl;
			return;
		}

		if (doc.Parse(anndata.c_str()) != 0) {
			return;
		}
	}

	tinyxml2::XMLElement *root = doc.RootElement();
	if (!root || strcmp(root->Name(), "Annotations"))
		return;

	wxString str;
	tinyxml2::XMLElement *child = root->FirstChildElement();
	while (child)
	{
		if (strcmp(child->Name(), "Annotation") == 0)
		{
			AnnotationDB ann;
			str = child->Attribute("URL");
			ann.url = str;
			str = child->Attribute("Contents");
			wxStringTokenizer tkz(str, wxT(","));
			while(tkz.HasMoreTokens())
			{
				ann.contents.Add(tkz.GetNextToken());
			}
			annotations.push_back(ann);
		}
		child = child->NextSiblingElement();
	}

	ann_buf[url] = annotations;
}

void DBTreeCtrl::OnExpand(wxCommandEvent &event)
{
	wxTreeItemId sel_item = GetSelection();
	if (IsExpanded(sel_item))
		Collapse(sel_item);
	else
		Expand(sel_item);
}

void DBTreeCtrl::OnAct(wxTreeEvent &event)
{
	wxTreeItemId sel_item = GetSelection();
	if(sel_item.IsOk())
	{
		DBItemInfo* item_data = (DBItemInfo *)GetItemData(sel_item);
		if(item_data && item_data->type == 2)
		{
			VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
			if (!vr_frame)
				return;

			SettingDlg *setting_dlg = vr_frame->GetSettingDlg();
			if (setting_dlg)
				vr_frame->SetRealtimeCompression(setting_dlg->GetRealtimeCompress());

			VRenderView* vrv = vr_frame->GetView(0);

			wxArrayString durl;
			durl.Add(item_data->data_url);
			vector<vector<AnnotationDB>> anns;
			anns.push_back(item_data->annotations);
			vr_frame->LoadVolumes(durl, vrv, anns);

			if (setting_dlg)
			{
				setting_dlg->SetRealtimeCompress(vr_frame->GetRealtimeCompression());
				setting_dlg->UpdateUI();
			}

		}
	}
}

void DBTreeCtrl::DeleteSelectedCatalog()
{
	wxTreeItemId sel_item = GetSelection();
	
	if (sel_item.IsOk())
	{
		DBItemInfo* item_data = (DBItemInfo *)GetItemData(sel_item);
		if (!item_data || item_data->type != 3) return;

		int result = wxMessageBox("Are you sure you want to remove the selected catalog?",
									"Remove Catalog Data", wxYES_NO);
		if (result == wxNO) return;

		vector<DBCatalog>::iterator ite = cats.begin();
		while (ite != cats.end())
		{
			if (ite->id == item_data->sample_id)
			{
				ite = cats.erase(ite);
			}
			else ite++;
		}
		
		Delete(sel_item);
	}
}

void DBTreeCtrl::OnKeyDown(wxKeyEvent& event)
{
	if ( event.GetKeyCode() == WXK_DELETE ||
		event.GetKeyCode() == WXK_BACK)
	{
		DeleteSelectedCatalog();

		VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
		if (!vr_frame)
			return;

		DatabaseDlg *db_dlg = vr_frame->GetDatabaseDlg();
		if (db_dlg)
			db_dlg->SaveDefault();
	}
	//event.Skip();
}

void DBTreeCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

DBTreeCtrl::~DBTreeCtrl()
{
	if (!IsEmpty())
	{
		DeleteAllItems();
	}
}



BEGIN_EVENT_TABLE(DBListCtrl, wxListCtrl)
	EVT_LIST_ITEM_ACTIVATED(wxID_ANY, DBListCtrl::OnAct)
	EVT_KEY_DOWN(DBListCtrl::OnKeyDown)
	EVT_KEY_UP(DBListCtrl::OnKeyUp)
END_EVENT_TABLE()

DBListCtrl::DBListCtrl(
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
   wxListItem itemCol;
   itemCol.SetText("Name");
    this->InsertColumn(0, itemCol);
    SetColumnWidth(0, 200);
	itemCol.SetText("Description");
    this->InsertColumn(1, itemCol);
	SetColumnWidth(1, 200);

	/*
	const std::string file_name = "test.txt";
	std::ifstream ifs(file_name.c_str());
	if (ifs.fail())
    {
        std::cerr << "list file open error" << std::endl;
        return;
    }
    std::string str((std::istreambuf_iterator<char>(ifs)),
        std::istreambuf_iterator<char>());
	*/

	extern CURL *_g_curl;
	CURLcode ret;
	string chunk;
	if (_g_curl == NULL) {
		cerr << "curl_easy_init() failed" << endl;
		return;
	}
	curl_easy_reset(_g_curl);
	curl_easy_setopt(_g_curl, CURLOPT_URL, "http://srpbsg04.unit.oist.jp/flydb/catalog.txt");
	curl_easy_setopt(_g_curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	curl_easy_setopt(_g_curl, CURLOPT_WRITEFUNCTION, callbackWrite);
	curl_easy_setopt(_g_curl, CURLOPT_WRITEDATA, &chunk);
	//�s�A�ؖ������؂Ȃ�
	curl_easy_setopt(_g_curl, CURLOPT_SSL_VERIFYPEER, 0);
	ret = curl_easy_perform(_g_curl);
	if (ret != CURLE_OK) {
		cerr << "curl_easy_perform() failed." << curl_easy_strerror(ret) << endl;
		return;
	}
	
	std::stringstream ss(chunk);
	std::string name, description, url, temp;
	while (getline(ss, temp))
	{
		std::stringstream ls(temp);
		if(getline(ls, name, '\t') && getline(ls, url, '\t'))
		{
			description = "";
			getline(ls, description, '\t');
			Append(name, url, description);
		}
	}
/*
	while (getline(ss, temp))
	{
		std::stringstream ls(temp);
		if((ls >> name) && (ls >> url))
		{
			description = "";
			ls >> description;
			Append(name, url, description);
		}
	}
*/
}

DBListCtrl::~DBListCtrl()
{
}

void DBListCtrl::Append(wxString name, wxString url, wxString desc)
{
   long tmp = InsertItem(GetItemCount(), name, 0);
   SetItem(tmp, 1, desc);
   urls.Add(url);
}

wxString DBListCtrl::GetText(long item, int col)
{
	wxListItem info;
	info.SetId(item);
	info.SetColumn(col);
	info.SetMask(wxLIST_MASK_TEXT);
	GetItem(info);
	return info.GetText();
}

void DBListCtrl::OnAct(wxListEvent &event)
{
	long item = GetNextItem(-1,
		wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (item == -1)
		return;
	
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	SettingDlg *setting_dlg = vr_frame->GetSettingDlg();
	if (setting_dlg)
		vr_frame->SetRealtimeCompression(setting_dlg->GetRealtimeCompress());

	VRenderView* vrv = vr_frame->GetView(0);

	wxArrayString url;
	url.Add(urls.Item(item));
	vr_frame->LoadVolumes(url, vrv);

	if (setting_dlg)
	{
		setting_dlg->SetRealtimeCompress(vr_frame->GetRealtimeCompression());
		setting_dlg->UpdateUI();
	}
	
}

void DBListCtrl::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void DBListCtrl::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}


BEGIN_EVENT_TABLE(DBSearcher, wxTextCtrl)

//EVT_CHILD_FOCUS(DBSearcher::OnSetChildFocus)
EVT_KEY_DOWN(DBSearcher::OnKeyDown)
EVT_KEY_UP(DBSearcher::OnKeyUp)
END_EVENT_TABLE()

DBSearcher::DBSearcher(
			wxWindow *frame,
	        wxWindow* parent,
            wxWindowID id,
			const wxString& text,
            const wxPoint& pos,
            const wxSize& size,
			long style) :
		wxTextCtrl(parent, id, "", pos, size, style),
		m_frame(frame)
{
	SetHint(text);
}

DBSearcher::~DBSearcher()
{

}

void DBSearcher::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}

void DBSearcher::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

wxDEFINE_EVENT(wxEVT_MYTHREAD_COMPLETED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MYTHREAD_PAUSED, wxCommandEvent);

MyThread::MyThread(wxEvtHandler* pParent, std::vector<DBCatalog> *cats, int st_cat, int st_item, wxString schtxt, unsigned int time_limit, wxGauge *gauge)
	: wxThread(wxTHREAD_DETACHED), m_pParent(pParent), m_cats(cats), m_cur_cat(st_cat), m_cur_item(st_item), m_time_limit(time_limit), m_gauge(gauge)
{
	wxStringTokenizer tkz(schtxt, wxT(" "));
	while ( tkz.HasMoreTokens() )
	{
		m_schtxts.push_back(tkz.GetNextToken().MakeLower());
	}
	
	if (!m_cats) return;
			
	m_progress = 1;
	for (int i = 0; i < m_cur_cat && i < m_cats->size(); i++) m_progress += (*m_cats)[i].items.size();
	m_progress += m_cur_item;

}

MyThread::~MyThread()
{
	wxCriticalSectionLocker enter(((DatabaseDlg *)m_pParent)->m_pThreadCS);
    // the thread is being destroyed; make sure not to leave dangling pointers around
    ((DatabaseDlg *)m_pParent)->m_thread = NULL;
}

wxThread::ExitCode MyThread::Entry()
{
	unsigned int st_time = GET_TICK_COUNT();

	DatabaseDlg *dd = (DatabaseDlg *)m_pParent;
	wxTreeItemId rootitem = dd->m_searchResult->GetRootItem();

	if (!m_results.empty()) m_results.clear();

	if (m_cats)
	{
		int catnum = m_cats->size();
		int schtxtnum = m_schtxts.size();

		for (int i = m_cur_cat; i < catnum; i++)
		{
			int itemnum = (*m_cats)[i].items.size();
			for (int j = m_cur_item; j < itemnum; j++)
			{
				bool found = false;
				for (int k = 0; k < schtxtnum; k++)
				{
					if (!m_schtxts[k].IsEmpty() && (*m_cats)[i].items[j]->desc.Find(m_schtxts[k]) != -1) found = true;
				}

				//if(found) m_results.push_back((*m_cats)[i].items[j]);

				if(found)
				{
					//dd->SetEvtHandlerEnabled(false);
					wxTreeItemId item = dd->m_searchResult->AppendItem(rootitem, (*m_cats)[i].items[j]->name + wxT(" (") + (*m_cats)[i].items[j]->cat_name + wxT(")"), 0);
					DBItemInfo* item_data = new DBItemInfo( *((*m_cats)[i].items[j]) );
					dd->m_searchResult->SetItemData(item, item_data);
					//dd->SetEvtHandlerEnabled(true);
				}
				//wxUsleep(10);

				m_progress++;

				unsigned int rn_time = GET_TICK_COUNT();
				if (rn_time - st_time > m_time_limit || TestDestroy())
				{
					wxCommandEvent evt(TestDestroy() ? wxEVT_MYTHREAD_COMPLETED: wxEVT_MYTHREAD_PAUSED, GetId());
					MyThreadData *data = new MyThreadData;
					data->m_cur_cat = i;
					data->m_cur_item = j + 1;
					data->m_progress = m_progress;
					data->m_results = m_results;
					evt.SetClientData(data);
					wxPostEvent(m_pParent, evt);
					if (m_gauge) m_gauge->SetValue(m_progress);
					return 0;
				}
			}
			m_cur_item = 0;
		}
	}

	wxCommandEvent evt(wxEVT_MYTHREAD_COMPLETED, GetId());
	MyThreadData *data = new MyThreadData;
	data->m_cur_cat = m_cur_cat;
	data->m_cur_item = m_cur_item;
	data->m_progress = m_progress;
	data->m_results = m_results;
	evt.SetClientData(data);
	wxPostEvent(m_pParent, evt);

	return 0;
}


MyThread_deleter::MyThread_deleter(wxEvtHandler* pParent) : wxThread(wxTHREAD_DETACHED), m_pParent(pParent)
{

}

MyThread_deleter::~MyThread_deleter()
{
	wxCriticalSectionLocker enter(((DatabaseDlg *)m_pParent)->m_pThreadCS);
    // the thread is being destroyed; make sure not to leave dangling pointers around
    ((DatabaseDlg *)m_pParent)->m_th_del = NULL;
}

wxThread::ExitCode MyThread_deleter::Entry()
{
	if( !m_pParent || !((DatabaseDlg *)m_pParent)->m_searchResult) return 0;

	((DatabaseDlg *)m_pParent)->m_searchResult->DeleteAllItems();

	return 0;
}

BEGIN_EVENT_TABLE(DatabaseDlg, wxPanel)
	EVT_TEXT(ID_SearchTextCtrl, DatabaseDlg::OnSearchText)
	EVT_TEXT_ENTER(ID_SearchTextCtrl, DatabaseDlg::OnSearch)
	EVT_BUTTON(ID_DownloadBtn, DatabaseDlg::OnSearch)
	EVT_BUTTON(ID_BrowseBtn, DatabaseDlg::OnBrowse)
	EVT_BUTTON(ID_AddDBBtn, DatabaseDlg::OnAddDB)
	EVT_COMMAND(wxID_ANY, wxEVT_MYTHREAD_COMPLETED, DatabaseDlg::OnSearchCompletion)
	EVT_COMMAND(wxID_ANY, wxEVT_MYTHREAD_PAUSED, DatabaseDlg::OnSearchPause)
END_EVENT_TABLE()

DatabaseDlg::DatabaseDlg(wxWindow* frame, wxWindow* parent)
: wxPanel(parent, wxID_ANY,
wxPoint(500, 150), wxSize(440, 700),
0, "DatabaseDlg"),
m_frame(parent),
m_view(0)
{
	SetEvtHandlerEnabled(false);
	Freeze();

	wxBoxSizer *group1 = new wxBoxSizer(wxHORIZONTAL);
	m_url_text = new DBSearcher(parent, this, ID_SearchTextCtrl, wxT("Search"),
		wxDefaultPosition, wxSize(300, 23), wxTE_PROCESS_ENTER);
	m_download_btn = new wxButton(this, ID_DownloadBtn, wxT("Search"),
		wxDefaultPosition, wxSize(70, 23));
	group1->Add(5, 5);
	group1->Add(m_url_text, 1, wxEXPAND);
	group1->Add(5, 5);
	group1->Add(m_download_btn, 0, wxALIGN_CENTER);

	wxBoxSizer *group_g = new wxBoxSizer(wxHORIZONTAL);
	m_sch_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(300, 10));
	group_g->Add(5, 10);
	group_g->Add(m_sch_gauge, 1, wxEXPAND);
	group_g->Add(5, 10);
	
	//list
	m_dbtree = new DBTreeCtrl(frame, this, wxID_ANY);
	m_searchResult = new DBTreeCtrl(frame, this, wxID_ANY);

    wxString expath = wxStandardPaths::Get().GetExecutablePath();
    expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
    wxString dft = expath + "/../Resources/" + "catalog_list.set";
#else
    wxString dft = expath + GETSLASHS() + "catalog_list.set";
	if (!wxFileExists(dft))
		dft = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "catalog_list.set";
#endif
    wxFileInputStream is(dft);
	if (is.IsOk())
	{
		wxTextInputStream tis(is);
		wxString str;

		while (!is.Eof())
		{
			wxString sline = tis.ReadLine();
			m_dbtree->LoadDatabase(sline);
		}
	}

	wxBoxSizer *group2 = new wxBoxSizer(wxHORIZONTAL);
	m_db_text = new DBSearcher(parent, this, ID_DBTextCtrl, wxT("URL or Path"),
		wxDefaultPosition, wxSize(330, 23), wxTE_PROCESS_ENTER);
	m_browse_btn = new wxButton(this, ID_BrowseBtn, wxT("Browse..."),
		wxDefaultPosition, wxSize(90, 23));
	group2->Add(5, 5);
	group2->Add(m_db_text, 1, wxEXPAND);
	group2->Add(5, 5);
	group2->Add(m_browse_btn, 0, wxALIGN_CENTER);
	group2->Add(5, 5);

	wxBoxSizer *group3 = new wxBoxSizer(wxHORIZONTAL);
	m_add_btn = new wxButton(this, ID_AddDBBtn, wxT("Add"),
		wxDefaultPosition, wxSize(90, 28));
	group3->AddStretchSpacer(1);
	group3->Add(5, 5);
	group3->Add(m_add_btn, 0, wxALIGN_CENTER);
	group3->Add(5, 5);

	//all controls
	wxBoxSizer *sizerV = new wxBoxSizer(wxVERTICAL);
	sizerV->Add(10, 10);
	sizerV->Add(group1, 0, wxEXPAND);
	sizerV->Add(10, 5);
	sizerV->Add(group_g, 0, wxEXPAND);
	sizerV->Add(10, 10);
	sizerV->Add(m_dbtree, 1, wxEXPAND);
	sizerV->Add(m_searchResult, 1, wxEXPAND);
	sizerV->Add(10, 20);
	sizerV->Add(group2, 0, wxEXPAND);
	sizerV->Add(10, 5);
	sizerV->Add(group3, 0, wxEXPAND);
	m_searchResult->Hide();
	m_sch_gauge->Hide();
	
	SetSizer(sizerV);
	Layout();

	m_thread = NULL;
	m_th_del = NULL;

	Thaw();
	SetEvtHandlerEnabled(true);
}

DatabaseDlg::~DatabaseDlg()
{
	if (m_thread) m_thread->Delete();

	//SaveDefault();
}

void DatabaseDlg::SaveDefault()
{	
	if (!m_dbtree) return;

	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
    wxString dft = expath + "/../Resources/" + "catalog_list.set";
#else
	wxString dft = expath + GETSLASHS() + "catalog_list.set";
	wxString dft2 = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "catalog_list.set";
	if (!wxFileExists(dft) && wxFileExists(dft2))
		dft = dft2;
#endif
	wxFileOutputStream os(dft);
	if (os.IsOk())
	{
		wxTextOutputStream tout(os);

		std::vector<DBCatalog> *cat = m_dbtree->GetCatalogDataIndex();
		if (cat && !cat->empty())
		{
			for (int i = 0; i < cat->size(); i++)
				tout << (*cat)[i].path << "\n";
		}
	}
}

void DatabaseDlg::StopSearch()
{
	if(m_searchResult->IsShown())
	{
		if (m_thread)
		{
			m_thread->Delete();
		}

		m_th_del = new MyThread_deleter(this);
		m_th_del->Create();
		m_th_del->Run();

		m_dbtree->Show();
		m_searchResult->Hide();
		Layout();
	}
}

void DatabaseDlg::OnSearchText(wxCommandEvent &event)
{
	StopSearch();
}

void DatabaseDlg::OnSearch(wxCommandEvent &event)
{
	if(m_url_text->GetValue().IsEmpty()) return;

	std::vector<DBCatalog> *cat = m_dbtree->GetCatalogDataIndex();
	int catnum = cat->size();
	int sch_range = 0;
	for (int i = 0; i < catnum; i++) sch_range += (*cat)[i].items.size();
	m_sch_gauge->SetRange(sch_range);
	m_sch_gauge->SetValue(1);
	m_sch_gauge->Show();
	m_dbtree->Hide();
	m_searchResult->Show();
	Layout();

	if (m_th_del) m_th_del->Delete();
	m_searchResult->DeleteAllItems();

	wxTreeItemId rootitem = m_searchResult->AddRoot(wxT("Root"));
	DBItemInfo* item_data = new DBItemInfo;
	item_data->type = 0;//root;
	m_searchResult->SetItemData(rootitem, item_data);

	if (m_thread && m_thread->IsAlive()) m_thread->Delete();
	m_thread = new MyThread(this, m_dbtree->GetCatalogDataIndex(), 0, 0, m_url_text->GetValue(), 100);
	m_thread->Create();
	m_thread->Run();

/*
	std::vector<DBCatalog> *cat = m_dbtree->GetCatalogDataIndex();
	int catnum = cat->size();
	wxString str = m_url_text->GetValue();
	vector<wxString> schtxts;

	wxStringTokenizer tkz(str, wxT(" "));
	while ( tkz.HasMoreTokens() )
	{
		schtxts.push_back(tkz.GetNextToken().MakeLower());
	}
	if (schtxts.empty()) return;
	int schtxtnum = schtxts.size();

	int sch_range = 0;
	for (int i = 0; i < catnum; i++) sch_range += (*cat)[i].items.size();
	m_sch_gauge->SetRange(sch_range);
	m_sch_gauge->SetValue(1);
	m_sch_gauge->Show();
	Layout();

	SetEvtHandlerEnabled(false);
	//Freeze();
	m_searchResult->DeleteAllItems();

	wxTreeItemId rootitem = m_searchResult->AddRoot(wxT("Root"));
	DBItemInfo* item_data = new DBItemInfo;
	item_data->type = 0;//root;
	m_searchResult->SetItemData(rootitem, item_data);

	int cur = 1;
	for (int i = 0; i < catnum; i++)
	{
		int itemnum = (*cat)[i].items.size();
		for (int j = 0; j < itemnum; j++)
		{
			bool found = true;
			for (int k = 0; k < schtxtnum; k++)
			{
				if ((*cat)[i].items[j]->desc.Find(schtxts[k]) == -1) found = false;
			}
			
			if(found)
			{
				wxTreeItemId item = m_searchResult->AppendItem(rootitem, (*cat)[i].items[j]->name + wxT(" (") + (*cat)[i].name + wxT(")"), 0);
				DBItemInfo* item_data = new DBItemInfo( *((*cat)[i].items[j]) );
				m_searchResult->SetItemData(item, item_data);
			}
			cur++;
			m_sch_gauge->SetValue(cur);
		}
	}
	m_sch_gauge->Hide();
	
	wxTreeItemIdValue cookie2;
	wxTreeItemId topitem = m_searchResult->GetFirstChild(m_searchResult->GetRootItem(), cookie2);
	if (!topitem.IsOk())
	{
		m_searchResult->AppendItem(rootitem, wxT("Not Found!"));
	}

	//Thaw();
	SetEvtHandlerEnabled(true);

	m_searchResult->ExpandAll();

	m_dbtree->Hide();
	m_searchResult->Show();
	Layout();
*/
}

void DatabaseDlg::OnSearchCompletion(wxCommandEvent &event)
{
	MyThreadData *data = (MyThreadData *)event.GetClientData();
	if (!data) return;

	m_sch_gauge->SetValue(data->m_progress);

	//AppendItemsToSearchResultTree(&(data->m_results));
	m_sch_gauge->Hide();
	Layout();
	
	delete data;
}

void DatabaseDlg::OnSearchPause(wxCommandEvent &event)
{
	MyThreadData *data = (MyThreadData *)event.GetClientData();
	if (!data) return;
	//AppendItemsToSearchResultTree(&(data->m_results));
	m_sch_gauge->SetValue(data->m_progress);
	Refresh();
		
	if (m_thread && m_thread->IsAlive()) m_thread->Delete();
	m_thread = new MyThread(this, m_dbtree->GetCatalogDataIndex(), data->m_cur_cat, data->m_cur_item, m_url_text->GetValue(), 100);
	m_thread->Create();
	m_thread->Run();

	delete data;
}

void DatabaseDlg::AppendItemsToSearchResultTree(vector<DBItemInfo*> *items)
{
	if (!items) return;

	SetEvtHandlerEnabled(false);
	Freeze();

	int itemnum = items->size();
	wxTreeItemId rootitem = m_searchResult->GetRootItem();
	if (!rootitem.IsOk())
	{
		m_searchResult->DeleteAllItems();
		rootitem = m_searchResult->AddRoot(wxT("Root"));
		DBItemInfo* item_data = new DBItemInfo;
		item_data->type = 0;//root;
		m_searchResult->SetItemData(rootitem, item_data);
	}

	for (int i = 0; i < itemnum; i++)
	{
		wxTreeItemId item = m_searchResult->AppendItem(rootitem, (*items)[i]->name + wxT(" (") + (*items)[i]->cat_name + wxT(")"), 0);
		DBItemInfo* item_data = new DBItemInfo( *((*items)[i]) );
		m_searchResult->SetItemData(item, item_data);
	}

	Thaw();
	SetEvtHandlerEnabled(true);
	
	m_searchResult->ExpandAll();
}

void DatabaseDlg::OnBrowse(wxCommandEvent &event)
{
	VRenderFrame* vr_frame = (VRenderFrame*)m_frame;
	if (!vr_frame)
		return;

	wxFileDialog *fopendlg = new wxFileDialog(
		this, "Choose the catalog file", "", "",
		"Catalog files (*.xml)|*.xml", wxFD_OPEN);

	int rval = fopendlg->ShowModal();
	if (rval == wxID_OK)
	{
		wxArrayString paths;
		fopendlg->GetPaths(paths);
		if (paths.size() > 0) m_db_text->SetValue(paths[0]);
	}
}

void DatabaseDlg::OnAddDB(wxCommandEvent &event)
{
	StopSearch();

	wxProgressDialog *prg_diag = new wxProgressDialog(
            "VVDViewer: Loading catalog data...",
            "Loading selected catalog data. Please wait.",
            100, 0, wxPD_APP_MODAL | wxPD_SMOOTH);
	
	prg_diag->Update(95);

	m_dbtree->LoadDatabase(m_db_text->GetValue());
	m_db_text->Clear();
	
	prg_diag->Destroy();

	SaveDefault();
}
