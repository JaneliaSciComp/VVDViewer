#include <wx/wx.h>
#include <wx/treectrl.h>
#include <wx/listctrl.h>
#include <wx/thread.h>
#include <wx/event.h>

#include <tinyxml2.h>

#include <vector>
#include <map>
#include "DLLExport.h"

#ifndef _DATABASEDLG_H_
#define _DATABASEDLG_H_

class VRenderView;

wxDECLARE_EVENT(wxEVT_MYTHREAD_COMPLETED, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MYTHREAD_PAUSED, wxCommandEvent);

#include "AnnotationDB.h"

//tree item data
class EXPORT_API DBItemInfo : public wxTreeItemData
{
public:
	DBItemInfo()
	{
		data_url = "";
		annotation_url = "";
		desc = "";
		sample_id = 0;
	}
	DBItemInfo(const DBItemInfo &obj)
	{
		type = obj.type;
		name = obj.name;
		cat_name = obj.cat_name;
		data_url = obj.data_url;
		annotation_url = obj.annotation_url;
		desc = obj.desc;
		annotations = obj.annotations;
		sample_id = obj.sample_id;
	}

	int type;	//0-root; 1-group; 2-data; 3-catalog;
	wxString data_url;
	wxString annotation_url;
	wxString desc;
	wxString name;
	wxString cat_name;
	std::vector<AnnotationDB> annotations;
	int sample_id;
};

struct DBCatalog
{
	wxString name;
	int id;
	wxString path;
	std::vector<DBItemInfo*> items;
};

class EXPORT_API DBTreeCtrl : public wxTreeCtrl
{
	enum
	{
		ID_Expand = wxID_HIGHEST+2001
	};
	
   public:
      DBTreeCtrl(wxWindow *frame,
            wxWindow* parent,
            wxWindowID id,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
			long style=wxTR_HAS_BUTTONS|
			wxTR_TWIST_BUTTONS|
			wxTR_LINES_AT_ROOT|
			wxTR_NO_LINES|
			wxTR_FULL_ROW_HIGHLIGHT|
			wxTR_HIDE_ROOT);
      ~DBTreeCtrl();

      friend class DatabaseDlg;

	  bool LoadDatabase(wxString path);
	  std::vector<DBCatalog> *GetCatalogDataIndex() { return &cats; }

   private:
      wxWindow* m_frame;
      VRenderView *m_view;
	  
	  wxArrayString urls;

	  std::vector<DBCatalog> cats;

      std::map<wxString, std::vector<AnnotationDB>> ann_buf;

	  static int catid;
	  
   private:

	  void OnAct(wxTreeEvent &event);

	  void OnExpand(wxCommandEvent& event);
	   
	  void OnKeyDown(wxKeyEvent& event);
	  void OnKeyUp(wxKeyEvent& event);

	  void ReadNode(tinyxml2::XMLElement *lvRootNode, wxTreeItemId parent, DBCatalog *cat = NULL);

	  void ReadAnnotations(wxString url, std::vector<AnnotationDB> &annotations);

	  void DeleteSelectedCatalog();

	  DECLARE_EVENT_TABLE()
   protected: //Possible TODO
         wxSize GetSizeAvailableForScrollTarget(const wxSize& size) {
            return size - GetEffectiveMinSize();
         }
};

class EXPORT_API DBListCtrl : public wxListCtrl
{
	
   public:
      DBListCtrl(wxWindow *frame,
            wxWindow* parent,
            wxWindowID id,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            long style=wxLC_REPORT|wxLC_SINGLE_SEL);
      ~DBListCtrl();

      void Append(wxString name, wxString url, wxString desc);
     
      wxString GetText(long item, int col);
	  
      friend class DatabaseDlg;

   private:
      wxWindow* m_frame;
      VRenderView *m_view;
	  
	  wxArrayString urls;
      
	  long m_editing_item;
	  long m_dragging_to_item;

   private:
	  void OnAct(wxListEvent &event);
	   
	  void OnKeyDown(wxKeyEvent& event);
	  void OnKeyUp(wxKeyEvent& event);

	  DECLARE_EVENT_TABLE()
   protected: //Possible TODO
         wxSize GetSizeAvailableForScrollTarget(const wxSize& size) {
            return size - GetEffectiveMinSize();
         }
};


class EXPORT_API DBSearcher : public wxTextCtrl
{
	public:
      DBSearcher(wxWindow *frame,
		    wxWindow* parent,
            wxWindowID id,
			const wxString& text = wxT(""),
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            long style=wxTE_PROCESS_ENTER);
      ~DBSearcher();

	  void setList(DBTreeCtrl *list){m_list = list;}

   private:
      wxWindow* m_frame;
      VRenderView *m_view;
	  
	  wxArrayString urls;

	  DBTreeCtrl *m_list;
     
   private:
	   
	   void OnKeyDown(wxKeyEvent& event);
	   void OnKeyUp(wxKeyEvent& event);

      DECLARE_EVENT_TABLE()
};


struct MyThreadData
{
	int m_cur_cat;
	int m_cur_item;
	int m_progress;
	std::vector<DBItemInfo*> m_results;
};

class EXPORT_API MyThread : public wxThread
{
    public:
		MyThread(wxEvtHandler* pParent, std::vector<DBCatalog> *cats, int st_cat, int st_item, wxString schtxts, unsigned int time_limit, wxGauge *gauge = NULL);
		~MyThread();
		void SetTimeLimit(unsigned int time_limit) { m_time_limit = time_limit; }
    private:
        int m_cur_cat;
		int m_cur_item;
		int m_progress;
		unsigned int m_time_limit;
		std::vector<DBCatalog> *m_cats;
		wxGauge *m_gauge;
		std::vector<wxString> m_schtxts;
		std::vector<DBItemInfo*> m_results;
    protected:
		virtual ExitCode Entry();
        wxEvtHandler* m_pParent;
};

class EXPORT_API MyThread_deleter : public wxThread
{
    public:
		MyThread_deleter(wxEvtHandler* pParent);
		~MyThread_deleter();
    protected:
		virtual ExitCode Entry();
        wxEvtHandler* m_pParent;
};

class EXPORT_API DatabaseDlg : public wxPanel
{
public:
	enum
	{
		ID_SearchTextCtrl,
		ID_DownloadBtn,
		ID_DBTextCtrl,
		ID_BrowseBtn,
		ID_AddDBBtn
	};

	DatabaseDlg(wxWindow* frame,
		wxWindow* parent);
	~DatabaseDlg();

	void SaveDefault();

	void StopSearch();

private:
	wxWindow* m_frame;
	//current view
	VRenderView* m_view;

	//enter URL
	wxTextCtrl *m_url_text;
	//download volume from the database
	wxButton *m_download_btn;

	wxGauge *m_sch_gauge;

	wxTextCtrl *m_db_text;
	wxButton *m_browse_btn;
	wxButton *m_add_btn;
	
	//list ctrl
	DBListCtrl *m_dblist;

	//tree ctrl
	DBTreeCtrl *m_dbtree;
	DBTreeCtrl *m_searchResult;

protected:
	MyThread *m_thread;
	MyThread_deleter *m_th_del;
	wxCriticalSection m_pThreadCS;

	friend class MyThread;
	friend class MyThread_deleter;

private:
	void OnAddDB(wxCommandEvent &event);
	void OnBrowse(wxCommandEvent &event);
	void OnSearch(wxCommandEvent &event);
	void OnSearchText(wxCommandEvent &event);
	void OnSearchPause(wxCommandEvent &event);
	void OnSearchCompletion(wxCommandEvent &event);

	void AppendItemsToSearchResultTree(std::vector<DBItemInfo*> *items);

	DECLARE_EVENT_TABLE();
};

#endif//_DATABASEDLG_H_