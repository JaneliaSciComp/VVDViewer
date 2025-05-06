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
#include "TreePanel.h"
#include "ListPanel.h"
#include "VRenderView.h"
#include "VPropView.h"
#include "MPropView.h"
#include "APropView.h"
#include "MManipulator.h"
#include "VMovieView.h"
#include "VAnnoView.h"
#include "ClippingView.h"
#include "AdjustView.h"
#include "SettingDlg.h"
#include "HelpDlg.h"
#include "BrushToolDlg.h"
#include "NoiseCancellingDlg.h"
#include "CountingDlg.h"
#include "ConvertDlg.h"
#include "ColocalizationDlg.h"
#include "RecorderDlg.h"
#include "MeasureDlg.h"
#include "TraceDlg.h"
#include "DatabaseDlg.h"
#include "Tester.h"
#include "Animator/Interpolator.h"
#include "TextRenderer.h"
#include "PluginManager.h"
#include "compatibility.h"

#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/aui/aui.h>

#include <vector>

#ifndef _VRENDERFRAME_H_
#define _VRENDERFRAME_H_

using namespace std;

//#define WITH_DATABASE

#define VERSION_CONTACT "http://www.sci.utah.edu/software/fluorender.html"
#define VERSION_AUTHORS "    Yong Wan, Hideo Otsuna,\nChuck Hansen, Chi-Bin Chien,\nBrig Bagley\nTakashi Kawase\n      @The University of Utah"
#define VERSION_UPDATES "http://www.sci.utah.edu/releases/fluorender_v" \
	               VERSION_MAJOR_TAG \
				   "." \
				   VERSION_MINOR_TAG \
				   "/"
#define HELP_MANUAL "http://www.sci.utah.edu/releases/fluorender_v"\
	               VERSION_MAJOR_TAG \
				   "." \
				   VERSION_MINOR_TAG \
				   "/FluoRender" \
	               VERSION_MAJOR_TAG \
				   "." \
				   VERSION_MINOR_TAG \
				   "_Manual.pdf"
#define HELP_TUTORIAL "http://www.sci.utah.edu/releases/fluorender_v"\
	               VERSION_MAJOR_TAG \
				   "." \
				   VERSION_MINOR_TAG \
				   "/FluoRender" \
	               VERSION_MAJOR_TAG \
				   "." \
				   VERSION_MINOR_TAG \
				   "_Tutorials.pdf"
#define BATCH_INFO HELP_MANUAL
#define HELP_PAINT HELP_MANUAL

#define UITEXT_DATAVIEW		"Datasets"
#define UITEXT_TREEVIEW		"Workspace"
#define UITEXT_MAKEMOVIE	"Record/Export"
#define UITEXT_MEASUREMENT	"Measurement"
#define UITEXT_ANNOTATION	"Info"
#define UITEXT_ADJUST		"Output Adjustments"
#define UITEXT_CLIPPING		"Clipping Planes"
#define UITEXT_PROPERTIES	"Properties"
#define UITEXT_PLUGINS		"Plugins"

 
class DatasetListCtrl : public wxListCtrl
{
    
public:
    DatasetListCtrl(wxWindow* parent,
                   wxWindowID id,
                   const wxArrayString& list,
                   const wxPoint& pos=wxDefaultPosition,
                   const wxSize& size=wxDefaultSize,
                   long style=wxLC_REPORT|wxLC_SINGLE_SEL|wxLC_NO_HEADER);
    ~DatasetListCtrl();
    
    wxString GetText(long item, int col);
    void SelectAllDataset();
    void DeselectAllDataset();
    bool isDatasetSelected(int id);
    wxString GetDatasetName(int id);
    
private:
    void OnItemSelected(wxListEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnScroll(wxScrollWinEvent& event);
    void OnScroll(wxMouseEvent& event);
    
    DECLARE_EVENT_TABLE()
protected:
    wxSize GetSizeAvailableForScrollTarget(const wxSize& size) {
        return size - GetEffectiveMinSize();
    }
};

class DatasetSelectionDialog : public wxDialog
{
    enum
    {
        ID_SelectAllButton,
        ID_DeselectAllButton
    };
    
public:
    DatasetSelectionDialog(wxWindow* parent, wxWindowID id, const wxString &title, const wxArrayString& list,
                   const wxPoint &pos = wxDefaultPosition,
                   const wxSize &size = wxDefaultSize,
                   long style = wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER );
    
    int GetDatasetNum();
    bool isDatasetSelected(int id);
    wxString GetDatasetName(int id);
    
private:
    DatasetListCtrl *m_list;
    wxButton* m_sel_all_button;
    
public:
    void OnSelectAllButtonClick( wxCommandEvent& event );
    void OnDeselectAllButtonClick( wxCommandEvent& event );
    void OnOk( wxCommandEvent& event );
    
    DECLARE_EVENT_TABLE();
};


class EXPORT_API VRenderFrame: public wxFrame
{
	enum
	{
		//file
		//file\new
		ID_Save = wxID_HIGHEST+901,
		ID_SaveProject,
		//file\open
		ID_Open,
		ID_OpenProject,
		ID_OpenVolume,
		ID_DownloadVolume,
		ID_OpenMesh,
		ID_OpenURL,
		//Mesh
		//Mesh\Create
		ID_Create,
		ID_CreateCube,
		ID_CreateSphere,
		ID_CreateCone,
		//view
		ID_FullScreen,
		ID_ViewNew,
		ID_ShowHideUI,
		ID_PaintTool,
		ID_NoiseCancelling,
		ID_Counting,
		ID_Colocalization,
		ID_Convert,
		ID_ViewOrganize,
		ID_Recorder,
		ID_Measure,
		ID_Trace,
		ID_InfoDlg,
		ID_Settings,
		//UI menu
        ID_UIToggleAllViews,
		ID_UIListView,
		ID_UITreeView,
		ID_UIMeasureView,
		ID_UIMovieView,
		ID_UIAdjView,
		ID_UIClipView,
		ID_UIPropView,
		//right aligned items
		ID_CheckUpdates,
		ID_Facebook,
		ID_Twitter,
		ID_Info,
		ID_ShowHideToolbar,
		ID_Timer,
		ID_Plugins,
		ID_Plugin = wxID_HIGHEST+10001
	};

public:
	VRenderFrame(wxApp *app, wxFrame* frame,
                 const wxString& title,
                 int x, int y,
                 int w, int h);
	~VRenderFrame();

	wxApp* GetApp() {return m_app;}

	TreePanel *GetTree();
	void UpdateTree(wxString name = "", int type=-1, bool set_calc=true);
	void UpdateROITree(VolumeData *vd, bool set_calc=true);
	void UpdateTreeColors();
	void UpdateTreeIcons();
	void UpdateTreeFrames();
	
	//data manager
	DataManager* GetDataManager();
	
	//views
	int GetViewNum();
	vector <VRenderView*>* GetViewList();
	VRenderView* GetView(int index);
	VRenderView* GetView(wxString& name);
	void RefreshVRenderViews(bool tree=false, bool interactive=false);
	void RefreshVRenderViewsOverlay(bool tree);
	void DeleteVRenderView(int i);
	void DeleteVRenderView(wxString &name);

	//organize render views
	void OrganizeVRenderViews(int mode);
	//hide/show tools
	void ToggleAllTools();
	//show/hide panes
	void ShowPane(wxPanel* pane, bool show=true);

	//on selections
	void OnSelection(int type,	//0: nothing; 1:view; 2: volume; 3:mesh; 4:annotations; 5:group; 6:mesh manip
		VRenderView* vrv=0,
		DataGroup* group=0,
		VolumeData* vd=0,
		MeshData* md=0,
		Annotations* ann=0);

	//settings
	//make movie settings
	int m_mov_view;
	int m_mov_axis;	//1 for x; 2 for y
	bool m_mov_rewind;
	wxString m_mov_angle_start;
	wxString m_mov_angle_end;
	wxString m_mov_step;
	wxString m_mov_frames;

	//prop view
	AdjustView* GetAdjustView();
	//tool views
	VPropView* GetPropView()
	{ return m_volume_prop; }
    MPropView* GetMPropView()
    { return m_mesh_prop; }
	//movie view
	VMovieView* GetMovieView()
	{ return m_movie_view; }
	//movie view
	VAnnoView* GetAnnoView()
	{ return m_anno_view; }
	//system settings
	SettingDlg* GetSettingDlg()
	{ return m_setting_dlg; }
	//help dialog
	HelpDlg* GetHelpDlg()
	{ return m_help_dlg; }
	//clipping view
	ClippingView* GetClippingView()
	{ return m_clip_view; }
	//brush dialog
	BrushToolDlg* GetBrushToolDlg()
	{ return m_brush_tool_dlg; }
	//noise cancelling dialog
	NoiseCancellingDlg* GetNoiseCancellingDlg()
	{ return m_noise_cancelling_dlg; }
	//counting dialog
	CountingDlg* GetCountingDlg()
	{ return m_counting_dlg; }
	//convert dialog
	ConvertDlg* GetConvertDlg()
	{ return m_convert_dlg; }
	ColocalizationDlg* GetColocalizationDlg()
	{ return m_colocalization_dlg; }
	//recorder dialog
	RecorderDlg* GetRecorderDlg()
	{ return m_recorder_dlg; }
	//measure dialog
	MeasureDlg* GetMeasureDlg()
	{ return m_measure_dlg; }
	//trace dialog
	TraceDlg* GetTraceDlg()
	{ return m_trace_dlg; }
	//database dialog
	DatabaseDlg* GetDatabaseDlg()
	{ return m_database_dlg; }
    //legend panel
    LegendPanel* GetLegendPanel()
    { return m_legend_panel; }

	//selection
	int GetCurSelType()
	{ return m_cur_sel_type; }
	//get current selected volume
	VolumeData* GetCurSelVol()
	{ return m_data_mgr.GetVolumeData(m_cur_sel_vol); }
	//get current selected mesh
	MeshData* GetCurSelMesh()
	{ return m_data_mgr.GetMeshData(m_cur_sel_mesh); }

	void StartupLoad(wxArrayString files, size_t datasize = 0LL, wxArrayString descs = wxArrayString());
	VolumeData* OpenVolumeFromProject(wxString name, wxFileConfig &fconfig);
    void OpenVolumesFromProjectMT(wxFileConfig &fconfig, bool join);
    void SetVolumePropertiesFromProject(wxFileConfig &fconfig);
	MeshData* OpenMeshFromProject(wxString name, wxFileConfig &fconfig);
	void SaveClippingLayerProperties(wxFileConfig& fconfig, ClippingLayer* layer);
	void LoadClippingLayerProperties(wxFileConfig& fconfig, ClippingLayer* layer);
	void OpenProject(wxString& filename);
	void SaveProject(wxString& filename);
	void LoadVolumes(wxArrayString files, VRenderView* view = 0, vector<vector<AnnotationDB>> annotations = vector<vector<AnnotationDB>>(), size_t datasize = 0LL, wxArrayString descs = wxArrayString());
	void LoadMeshes(wxArrayString files, VRenderView* view = 0, wxArrayString descs = wxArrayString(), wxString group_name=wxString());
    void ConvertCSV2SWC(wxString filename, VRenderView* view = NULL);
    void ConvertNML2SWC(wxString filename, VRenderView* view = NULL);

	void AddVolume(VolumeData *vd, VRenderView* view);

	//compression
	static void SetCompression(bool value)
	{ m_save_compress = value; }
	static bool GetCompression()
	{ return m_save_compress; }
	//realtime compression
	static void SetRealtimeCompression(bool value)
	{ m_compression = value; }
	static bool GetRealtimeCompression()
	{ return m_compression; }
	//skip brick
	static void SetSkipBrick(bool value)
	{ m_skip_brick = value; }
	static bool GetSkipBrick()
	{ return m_skip_brick;}
	//save project
	static void SetSaveProject(bool value)
	{ m_save_project = value; }
	static bool GetSaveProject()
	{ return m_save_project; }
	//embed project
	static void SetEmbedProject(bool value)
	{ m_vrp_embed = value; }
	static bool GetEmbedProject()
	{ return m_vrp_embed; }

	//show dialogs
	void ShowPaintTool();
	void ShowInfoDlg();
	void ShowTraceDlg();
	void ShowNoiseCancellingDlg();
	void ShowCountingDlg();
	void ShowColocalizationDlg();
	void ShowConvertDlg();
	void ShowRecorderDlg();
	void ShowMeasureDlg();
	
	//get interpolator
	Interpolator* GetInterpolator()
	{ return &m_interpolator; }

	//tex renderer settings
	void SetTextureRendererSettings();
	void SetTextureUndos();

	//quit option
	void OnQuit(wxCommandEvent& WXUNUSED(event))
	{ Close(true); }
	//show info
	void OnInfo(wxCommandEvent& WXUNUSED(event));

	void SetKeyLock(bool lock);

	PluginManager* GetPluginManager() const { return m_plugin_manager ; }

	void ToggleVisibilityPluginWindow(wxString name, bool show, int docking = 0);
	void CreatePluginWindow(wxString name, bool show=true);
	bool IsCreatedPluginWindow(wxString name);
	bool IsShownPluginWindow(wxString name);
	bool RunPlugin(wxString name, wxString options, bool show=false);
	bool PluginExists(wxString name);
	int UploadFileRemote(wxString url, wxString upfname, wxString loc_fpath, wxString usr, wxString pwd=wxString());
	int DownloadFileRemote(wxString url, wxString dir, wxString usr=wxString(), wxString pwd=wxString());
    
    void ShowLegendPanel(VRenderView* view, const wxPoint pos, const wxSize size)
    {
        if (m_legend_panel)
        {
            m_aui_mgr.GetPane(m_legend_panel).FloatingSize(size).FloatingPosition(pos).Show().Float();
            m_aui_mgr.Update();
            m_legend_panel->SetSize(size);
            m_legend_panel->Layout();
        }
    }
    void HideLegendPanel()
    {
        if (m_legend_panel)
        {
            m_aui_mgr.GetPane(m_legend_panel).Hide();
            m_aui_mgr.Update();
        }
    }
    bool IsShownLegendPanel()
    {
        if (m_legend_panel)
            return m_aui_mgr.GetPane(m_legend_panel).IsShown();
        return false;
    }
    
    void SetTasks(wxString comma_separated_tasks);

#ifdef __WXGTK__
	static bool m_is_wayland;
	static std::unordered_map<int, bool> m_key_state;
#endif
	static bool GetKeyState(wxKeyCode key);
	
public: //public so export window can see it and set it. 
	RecorderDlg* m_recorder_dlg;
	VMovieView* m_movie_view;

private:
	wxAuiManager m_aui_mgr;
	wxMenu* m_tb_menu_ui;
	wxMenu* m_tb_menu_edit;
	wxMenu* m_tb_menu_plugin;
	wxToolBar* m_main_tb;
	//main top menu
	wxMenuBar* m_top_menu;
	wxMenu* m_top_file;
	wxMenu* m_top_tools;
	wxMenu* m_top_window;
	wxMenu* m_top_help;

	TreePanel *m_tree_panel;
	//ListPanel *m_list_panel;
	vector <VRenderView*> m_vrv_list;
	DataManager m_data_mgr;
	wxPanel *m_prop_panel;
	ClippingView *m_clip_view;
	AdjustView* m_adjust_view;
	SettingDlg* m_setting_dlg;
	HelpDlg* m_help_dlg;
	BrushToolDlg* m_brush_tool_dlg;
	NoiseCancellingDlg* m_noise_cancelling_dlg;
	CountingDlg* m_counting_dlg;
	ConvertDlg* m_convert_dlg;
	ColocalizationDlg* m_colocalization_dlg;
	MeasureDlg* m_measure_dlg;
	TraceDlg* m_trace_dlg;
	DatabaseDlg *m_database_dlg;
    LegendPanel *m_legend_panel;
	VAnnoView *m_anno_view;
	//prop panel children
	wxBoxSizer* m_prop_sizer;
	VPropView* m_volume_prop;
	MPropView* m_mesh_prop;
	MManipulator* m_mesh_manip;
	APropView* m_annotation_prop;
	//tester
	TesterDlg* m_tester;
	//flag for show/hide views
	bool m_ui_state;
	//interpolator
	//it stores all keys
	//and does interpolaions too
	Interpolator m_interpolator;

	//current selection (allow only one)
	//selection type
	int m_cur_sel_type; //0:root; 1:view; 2:volume; 3:mesh; 5:volume group; 6:mesh group
	//current selected volume index
	int m_cur_sel_vol;
	//mesh index
	int m_cur_sel_mesh;

	//if slices are sequence
	static bool m_sliceSequence;
	static bool m_timeSequence;
	//compression
	static bool m_compression;
	//brick skipping
	static bool m_skip_brick;
	//string for time id
	static wxString m_time_id;
	//load volume mask
	static bool m_load_mask;
	//save compressed
	static bool m_save_compress;
	//embed files in project
	static bool m_vrp_embed;
	//save project
	static bool m_save_project;

	//mac address
	wxString m_address;

	map<wxString, bool> m_ui_state_cache;

	double m_gpu_max_mem;

	wxTimer *m_timer;

	PluginManager* m_plugin_manager;
	wxArrayString m_plugin_list;

	wxApp* m_app; 

	static wxCriticalSection ms_criticalSection;
    
    ProjectDataLoader m_project_data_loader;
    
    wxArrayString m_tasks;
    bool m_run_tasks;
    bool m_waiting_for_task;

private:
	//views
	wxString CreateView(int row=1);
	VRenderView* GetLastView() {return m_vrv_list[m_vrv_list.size()-1];}
	static wxWindow* CreateExtraControlVolume(wxWindow* parent);
	static wxWindow* CreateExtraControlProjectSave(wxWindow* parent);

	//open dialog options
	void OnCh1Check(wxCommandEvent &event);
	void OnCh4Check(wxCommandEvent &event);
	void OnCh1Click(wxEvent &event);
	void OnCh4Click(wxEvent &event);
	void OnTxt1Change(wxCommandEvent &event);
	void OnCh2Check(wxCommandEvent &event);
	void OnCh3Check(wxCommandEvent &event);
	void OnChEmbedCheck(wxCommandEvent &event);
	void OnChSaveCmpCheck(wxCommandEvent &event);

	void OnClose(wxCloseEvent &event);
	void OnExit(wxCommandEvent& WXUNUSED(event));
	void OnNewView(wxCommandEvent& WXUNUSED(event));
	void OnFullScreen(wxCommandEvent& WXUNUSED(event));
	void OnOpenVolume(wxCommandEvent& WXUNUSED(event));
	void OnDownloadVolume(wxCommandEvent& WXUNUSED(event));
	void OnOpenMesh(wxCommandEvent& WXUNUSED(event));
	void OnOrganize(wxCommandEvent& WXUNUSED(event));
	void OnCheckUpdates(wxCommandEvent& WXUNUSED(event));
	void OnFacebook(wxCommandEvent& WXUNUSED(event));
	void OnTwitter(wxCommandEvent& WXUNUSED(event));
	void OnShowHideUI(wxCommandEvent& WXUNUSED(event));
	void OnShowHideToolbar(wxCommandEvent& WXUNUSED(event));
    void OnToggleAllUIs(wxCommandEvent& WXUNUSED(event));
	void OnShowHideView(wxCommandEvent &event);
	void OnPlugins(wxCommandEvent& WXUNUSED(event));
	void OnPluginMenuSelect(wxCommandEvent& event);

	//panes
	void OnPaneClose(wxAuiManagerEvent& event);

	//test
	void OnCreateCube(wxCommandEvent& WXUNUSED(event));
	void OnCreateSphere(wxCommandEvent& WXUNUSED(event));
	void OnCreateCone(wxCommandEvent& WXUNUSED(event));

	void OnSaveProject(wxCommandEvent& WXUNUSED(event));
	void OnOpenProject(wxCommandEvent& WXUNUSED(event));

	void OnOpenURL(wxCommandEvent& WXUNUSED(event));

	void OnSettings(wxCommandEvent& WXUNUSED(event));
	void OnPaintTool(wxCommandEvent& WXUNUSED(event));
	void OnNoiseCancelling(wxCommandEvent& WXUNUSED(event));
	void OnCounting(wxCommandEvent& WXUNUSED(event));
	void OnConvert(wxCommandEvent& WXUNUSED(event));
	void OnRecorder(wxCommandEvent& WXUNUSED(event));
	void OnColocalization(wxCommandEvent& WXUNUSED(event));
	void OnInfoDlg(wxCommandEvent& WXUNUSED(event));
	void OnTrace(wxCommandEvent& WXUNUSED(event));
	void OnMeasure(wxCommandEvent& WXUNUSED(event));

	void OnDraw(wxPaintEvent& event);
	void OnKeyDown(wxKeyEvent& event);

	void OnTimer(wxTimerEvent& event);

	DECLARE_EVENT_TABLE()
};

#endif//_VRENDERFRAME_H_
