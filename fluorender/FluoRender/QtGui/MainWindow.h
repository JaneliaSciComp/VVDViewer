#ifndef VVD_QT_MAINWINDOW_H
#define VVD_QT_MAINWINDOW_H

// VVDViewer Qt main window (phase 1 of the Qt port).
//
// Pane layout mirrors the wx VRenderFrame's wxAUI layers:
//   layer 4  toolbar (top)
//   layer 3  Workspace over Record/Export (left, full height)
//   layer 2  Properties (bottom, right of layer 3)
//   layer 1  Output Adjustments (left) / Clipping Planes (right)
//   centre   Render View:1
// QMainWindow has no layers, so layers 1+centre live in an inner
// QMainWindow that is the outer window's central widget.

#include <QMainWindow>
#include <QUrl>
#include <QList>
#include <vector>
#include <memory>
#include <map>

#include "AnalysisContext.h"
#include "ProjectIO.h"
#include "VolumeSelector.h"
#include "VolumeCalculator.h"
#include "FileDialogs.h"
#include "PluginApi.h"

namespace vvd { class IniFile; }
class QDockWidget;
class QToolBar;
class QAction;
class QMenu;
class DataManager;
class VolumeData;
class RenderCanvas;
class TreePanel;
class VolumePropPanel;
class ViewPanel;
class AdjustPanel;
class ClipPanel;
class MoviePanel;
class Interpolator;

class NoiseReductionDialog;
class CountingDialog;
class ConvertDialog;
class ColocalizationDialog;
class InfoDialog;
class SettingsDialog;
class MeshPropPanel;
class MeshManipPanel;
class AnnotationPropPanel;
class RulerTool;
class ViewOverlays;
class BrushTool;
class BrushToolDialog;
class MeasureDialog;
class TraceDialog;
class QStackedWidget;
class MeshData;
class DataGroup;
class Annotations;
class QtPluginManager;
class AdjustPanel;

class MainWindow : public QMainWindow, public AnalysisContext, public vvd::PluginHost
{
	Q_OBJECT
public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override;

	// Load volume files (any format DataManager understands).
	// descs: wx -d/--desc name prefixes (LoadVolumeData prefix, by index)
	void LoadVolumes(const QStringList& files, const QStringList& descs = QStringList(), RenderCanvas* target = nullptr);
	// Load a .vrp project: phase 1 reads the volume entries only.
	bool OpenProject(const QString& path);
	// Write a .vrp (no dialog). Returns false and reports on failure.
	bool SaveProject(const QString& path);
	// Load mesh files (.obj .swc .ply .nml .csv).
	// wx LoadMeshes(files, vrv, descs, group_name)
	void LoadMeshFiles(const QStringList& files, const QString& group_name = QString(), const QStringList& descs = QStringList(),
		RenderCanvas* target = nullptr);
	// wx VRenderFrame::StartupLoad (command line, Finder open): a .vrp as the
	// first file opens that project only; otherwise volumes and meshes by
	// suffix (descs: -d/--desc prefixes, wx index rule)
	void StartupLoad(const QStringList& files, const QStringList& descs = QStringList());
	// wx VRenderFrame::SetTasks (-m/--macro "task,task,..."): hideui, showui,
	// basic, advanced, record <file>; run by a 100 ms timer (wx OnTimer), the
	// window closes when the list is done
	void SetTasks(const QString& comma_separated_tasks);
	RenderCanvas* Canvas() const { return m_canvas; }
	// wx DnDFile::OnDropFiles; on_view: dropped on a render view (target:
	// that view's canvas; the data are loaded into it, as wx DnDFile(frame,
	// vrv)); otherwise into the first view
	void OpenDroppedUrls(const QList<QUrl>& urls, bool on_view = false, RenderCanvas* target = nullptr);

	// ---- render views (wx VRenderFrame::m_vrv_list, MainWindowViews.cpp).
	// View 0 ("Render View:1") always exists; further views come from
	// projects with /views/num > 1 (wx OpenProject -> CreateView, the only
	// place wx creates one) and are closed with the tree's "Close" entry
	// (wx DataTreeCtrl::OnCloseView -> DeleteVRenderView). The panes follow
	// the active view: the view of the selected tree item (wx OnSelection
	// vrv), or the view last clicked; Canvas() / View() are the active ones.
	int GetViewNum() const { return (int)m_views.size(); }
	RenderCanvas* GetViewCanvas(int i) const;
	ViewPanel* GetViewPanel(int i) const;
	RulerTool* GetViewRulerTool(int i) const;
	BrushTool* GetViewBrushTool(int i) const;
	ViewOverlays* GetViewOverlays(int i) const;
	int ViewIndex(const RenderCanvas* c) const;
	RenderCanvas* FindViewCanvas(const QString& name) const;
	int ActiveView() const { return m_active_view; }
	void SetActiveView(int i);
	void ActivateCanvas(RenderCanvas* c) { const int i = ViewIndex(c); if (i >= 0) SetActiveView(i); }
	// wx CreateView: a new view right of the others (wxAUI Layer(0).Centre()
	// .Right(), then OrganizeVRenderViews(1)); returns its name
	QString CreateView();
	// wx DeleteVRenderView(name) (not the first view)
	void DeleteView(const QString& name);
	// wx OrganizeVRenderViews: 0 top-bottom, 1 left-right (equal sizes)
	void OrganizeViews(int mode);
	ViewPanel* View() const { return m_view; }
	MoviePanel* Movie() const { return m_movie; }
	// Testing aid (VVD_QT_TEST=ruler|brush): drive a tool with synthetic
	// mouse input; finishes within ~3 s.
	void RunTestScript(const QString& name);
	// Testing aid (VVD_QT_FILEDLG_RUN, see QtMain.cpp): run file dialogs and
	// file actions in order, print the loaded volumes, quit.
	void RunFileDialogTest(const QString& steps);
	// Testing aid (VVD_QT_TEST=paint|calc|seg, MainWindowPaintTest.cpp):
	// scripted brush strokes / calculations / analysis dialogs with mask
	// read-back and view captures; quits when done.
	void RunPaintTest(const QString& kind);
	// Testing aid (VVD_QT_TEST=b3_*, MainWindowBatch3Test.cpp): panels and
	// dialogs of the batch-3 wx parity work; quits when done.
	void RunBatch3Test(const QString& kind);

	// AnalysisContext (MainWindowAnalysis.cpp)
	VolumeData* CurrentVolume() override;
	DataManager* GetDataManager() override { return m_data_mgr; }
	VolumeSelector* GetSelector() override { return m_sel; }
	VolumeCalculator* GetCalculator() override { return m_calc; }
	void RefreshView() override;
	void AddVolume(VolumeData* vd) override;
	std::vector<VolumeData*> VisibleVolumes() override;
	int CompAnalysis(double min_voxels, double max_voxels, double thresh,
		bool select, bool gen_ann, int iter_limit = -1) override;
	void Calculate(int type) override { Calculate(type, true); }
	// wx Calculate(type, prev_group, add): add == false leaves the result in
	// the calculator (GetResult) without adding it (4D script noise_reduction)
	void Calculate(int type, bool add);
	void CalculateSingle(int type, const VVDString& prev_group, bool add = true);
	// wx VRenderVulkanView::Run4DScript() / Run4DScript(file, vd) and the task
	// handlers (MainWindowScript.cpp): noise_reduction, selection_tracking,
	// random_colors, separate_channels, external_exe (empty in wx), fetch_mask
	void Run4DScript();
	void Run4DScript(const QString& scriptname, VolumeData* vd);
	void CompExport(int mode, bool select) override;
	void ShowAnnotations() override;
	int NoiseAnalysis(double min_voxels, double max_voxels, double thresh) override;
	void NoiseRemoval(int iter, double thresh) override;
	void SetEnhanceSelection(bool enhance) override;
	bool GetEnhanceSelection() override;
	int GetScaleBarUnit() override;
	BrushToolDialog* GetBrushDialog() override { return m_brush_dlg; }
	void BrushErase() override;

	// ---- wx VRenderFrame UI management (MainWindowUI.cpp)
	// panes of the Show/Hide UI menu (wx m_tb_menu_ui / the Windows menu)
	enum UIPane { PaneTree = 0, PaneMeasure, PaneMovie, PaneAdjust, PaneClip, PaneProp, PaneCount };
	// wx ToggleAllTools: hide every pane (remembering which were shown) or
	// show the remembered ones again (all when nothing is remembered)
	void ToggleAllTools();
	bool IsPaneShown(int pane) const;
	void ShowPane(int pane, bool show);
	QMenu* UIMenu() const { return m_tb_menu_ui; }
	// wx VRenderVulkanView::OnContextMenu: the render view's right-click menu
	// (built for the current state; the caller owns it)
	QMenu* BuildViewContextMenu();
	QMenu* ViewContextMenu() const { return m_view_ctx_menu; }
	// wx VRenderFrame::ShowTraceDlg (a floating pane). wx shows it from
	// nowhere: its menu entries are commented out; the f / l keys and the
	// brush hooks use it hidden
	void ShowTraceDlg();
	TraceDialog* GetTraceDlg() const { return m_trace_dlg; }
	// Testing aid (VVD_QT_TEST=b4_views|b4_track, MainWindowBatch4Test.cpp)
	void RunBatch4Test(const QString& kind);
	// Testing aid (VVD_QT_TEST=b4b_*, MainWindowPluginTest.cpp): plugins
	void RunPluginTest(const QString& kind);
	// Testing aid (VVD_QT_TEST=layout, MainWindowLayout.cpp): window / pane
	// sizes against the screen
	void RunLayoutTest(const QString& kind);
	// keeps the window (and floating docks) inside the screen's available
	// area: restored geometry clamped, shrunk to fit (MainWindowLayout.cpp)
	void FitToScreen();

	// ---- plugins (MainWindowPlugins.cpp; wx VRenderFrame plugin UI)
	QtPluginManager* GetPluginManager() override;
	QMenu* PluginMenu() const { return m_tb_menu_plugin; }
	QDockWidget* PluginDock(const QString& name) const;
	// vvd::PluginHost (PluginApi.h)
	QWidget* PluginParentWidget() override;
	DataManager* PluginDataManager() override;
	int GetCurSelType() override;
	VolumeData* GetCurSelVol() override;
	MeshData* GetCurSelMesh() override;
	int PluginViewNum() override;
	RenderCanvas* PluginView(int i) override;
	TreePanel* PluginTree() override;
	BrushToolDialog* PluginBrushDialog() override;
	BrushTool* PluginBrushTool(int view) override;
	AdjustPanel* PluginAdjustPanel() override;
	VolumePropPanel* PluginPropPanel() override;
	void PluginAddVolume(VolumeData* vd, RenderCanvas* view = nullptr) override;
	bool PluginReplaceVolume(const QString& name, VolumeData* dst, RenderCanvas* view = nullptr) override;
	void PluginRemoveGroup(const QString& name, RenderCanvas* view = nullptr) override;
	void PluginUpdateTree(const QString& name = QString(), int type = -1) override;
	void PluginUpdateTreeIcons() override;
	void PluginRefreshViews(bool tree = false) override;
	void PluginSelect(int type, RenderCanvas* view, DataGroup* group, VolumeData* vd) override;
	void PluginStartupLoad(const QStringList& files, size_t datasize = 0, const QStringList& descs = QStringList()) override;
	void PluginLoadMeshes(const QStringList& files) override;
	int PluginUploadFileRemote(const QString& url, const QString& upfname, const QString& loc_fpath,
		const QString& usr, const QString& pwd = QString()) override;
	bool PluginExists(const QString& name) override;
	bool IsCreatedPluginWindow(const QString& name) override;
	bool IsShownPluginWindow(const QString& name) override;
	void CreatePluginWindow(const QString& name, bool show = true) override;
	void ToggleVisibilityPluginWindow(const QString& name, bool show, int docking = 0) override;
	bool RunPlugin(const QString& name, const QString& options, bool show = false) override;
	void PostPluginInterop(const QString& message) override;

protected:
	void closeEvent(QCloseEvent*) override;
	bool eventFilter(QObject* obj, QEvent* e) override;
	void dragEnterEvent(QDragEnterEvent*) override;
	void dropEvent(QDropEvent*) override;

private Q_SLOTS:
	void OnOpenVolume();
	void OnOpenProject();
	void OnResetView();
	void OnTogglePerspective(bool on);
	void OnBackgroundColor();
	void OnVolumeSelected(VolumeData* vd);
	void OnVolumeRemove(VolumeData* vd);
	void OnAbout();
	void OnSaveProject();
	void OnOpenMesh();
	void OnMeshSelected(MeshData* md);
	void OnMeshRemove(MeshData* md);
	void OnShowHideUI();
	void OnNewViewer();
	void OnOpenURL();

private:
	void BuildMenusAndToolbar();
	void BuildDocks();
	void AddVolumeToView(VolumeData* vd, RenderCanvas* canvas);
	void RefreshTree();
	void SetStatus(const QString& s);

	DataManager* m_data_mgr = nullptr;
	QMainWindow* m_inner = nullptr;
	ViewPanel* m_view = nullptr;
	RenderCanvas* m_canvas = nullptr;
	AdjustPanel* m_adjust = nullptr;
	ClipPanel* m_clip = nullptr;
	MoviePanel* m_movie = nullptr;
	QDockWidget* m_view_dock = nullptr;
	QDockWidget* m_adjust_dock = nullptr;
	QDockWidget* m_clip_dock = nullptr;
	QDockWidget* m_movie_dock = nullptr;
	// wx VRenderFrame::m_interpolator (Advanced-tab keyframes)
	std::unique_ptr<Interpolator> m_interpolator;
	TreePanel* m_tree = nullptr;
	VolumePropPanel* m_prop = nullptr;
	QDockWidget* m_tree_dock = nullptr;
	QDockWidget* m_prop_dock = nullptr;
	QAction* m_persp_action = nullptr;
	QString m_last_dir;
	bool m_vrp_embed = false;   // wx VRenderFrame::m_vrp_embed ("Embed all files in the project folder")
	// wx VRenderFrame::m_sliceSequence / m_timeSequence / m_time_id: the Open
	// Volume dialog options, applied to every volume load (LoadVolumes)
	FileDialogs::OpenVolumeOptions m_open_opts;

	// analysis (wx: owned by VRenderVulkanView): view 0's; further views own
	// theirs (ViewSlot); m_sel / m_calc = the active view's
	VolumeSelector m_selector;
	VolumeCalculator m_calculator;
	VolumeSelector* m_sel = &m_selector;
	VolumeCalculator* m_calc = &m_calculator;

	// render views (MainWindowViews.cpp)
	struct ViewSlot
	{
		QString name;                    // wx VRenderView::GetName ("Render View:<n>")
		int id = 1;                      // <n>
		ViewPanel* view = nullptr;
		RenderCanvas* canvas = nullptr;
		BrushTool* brush = nullptr;
		RulerTool* ruler = nullptr;
		ViewOverlays* overlays = nullptr;
		QWidget* frame = nullptr;        // caption + view (view 0: the view area's centre)
		QDockWidget* dock = nullptr;     // further views: their wxAUI-like pane
		std::unique_ptr<VolumeSelector> own_sel;
		std::unique_ptr<VolumeCalculator> own_calc;
		VolumeSelector* sel = nullptr;
		VolumeCalculator* calc = nullptr;
		std::vector<std::pair<QString, QString>> prj_props;   // /views/<i>/properties unmodelled keys (i > 0)
	};
	std::vector<std::unique_ptr<ViewSlot>> m_views;
	int m_active_view = 0;
	QMainWindow* m_view_area = nullptr;   // centre of m_inner: view 0 + the docks of further views
	ViewSlot* NewViewSlot(int id);        // ViewPanel + canvas in a frame (no tools yet)
	void WireView(ViewSlot& v);           // tools, overlays, providers, signal connections
	void ApplyViewSettings(RenderCanvas* c);   // SettingDlg values every view takes
	void DeleteView(int i);
	void ClearExtraViews();               // OpenProject: back to one view
	void SyncViewLists();                 // tree / movie panel view lists after a change
	void BuildAnalysisMenu(class QMenu* menu);
	void ShowTool(class QDialog* dlg);
	// wx floating AUI panes stay above the frame: make a tool window float
	// over the main window (also on macOS, without hiding when inactive)
	static void MakeFloatingTool(QWidget* w);
	NoiseReductionDialog* m_nr_dlg = nullptr;
	CountingDialog* m_count_dlg = nullptr;
	ConvertDialog* m_convert_dlg = nullptr;
	ColocalizationDialog* m_coloc_dlg = nullptr;
	InfoDialog* m_info_dlg = nullptr;
	SettingsDialog* m_settings = nullptr;
	MeshPropPanel* m_mesh_prop = nullptr;
	class QDialog* m_tester = nullptr;             // wx m_tester (TesterDlg)
	MeshManipPanel* m_mesh_manip = nullptr;        // wx m_mesh_manip (MManipulator)
	AnnotationPropPanel* m_ann_prop = nullptr;     // wx m_annotation_prop (APropView)
	// wx OnSelection(6, 0, 0, 0, md): the tree's "Manipulate"
	void ShowMeshManip(MeshData* md);
	RulerTool* m_ruler_tool = nullptr;
	ViewOverlays* m_overlays = nullptr;
	BrushTool* m_brush_tool = nullptr;
	BrushToolDialog* m_brush_dlg = nullptr;
	QMenu* m_analysis_menu = nullptr;
	MeasureDialog* m_measure_dlg = nullptr;
	// wx m_trace_dlg ("Components & Tracking"): created hidden at start-up
	TraceDialog* m_trace_dlg = nullptr;
	void ApplyRulerSettings();
	// 4D script tasks (MainWindowScript.cpp); prefix = "/tasks/task<i>/"
	void RunNoiseReduction(const vvd::IniFile& f, const VVDString& prefix);
	void RunSelectionTracking(const vvd::IniFile& f, const VVDString& prefix);
	void RunRandomColors(const vvd::IniFile& f, const VVDString& prefix);
	void RunSeparateChannels(const vvd::IniFile& f, const VVDString& prefix);
	void RunFetchMask(const vvd::IniFile& f, const VVDString& prefix);
	VolumeData* m_script_vol = nullptr;   // wx m_cur_vol while a script runs
	int m_fr_length = 0;                  // wx m_fr_length
	QStackedWidget* m_prop_stack = nullptr;
	// sections of the last opened project the Qt app does not use; written
	// back unchanged on save
	std::vector<std::pair<QString, QString>> m_prj_passthrough;
	void ClearScene();
	// wx m_tasks / m_run_tasks / m_waiting_for_task, OnTimer
	QStringList m_tasks;
	bool m_run_tasks = false;
	bool m_waiting_for_task = false;
	class QTimer* m_task_timer = nullptr;
	void OnTaskTimer();
	void ExpandZarrN5(const QStringList& files, QStringList& out, QStringList& meta);
	// wx VRenderFrame::SaveProject / OpenProject /views/0/properties and
	// /memory settings: the view state <-> ProjectIO::ProjectView
	void CaptureProjectView(ProjectIO::ProjectView& v, int view = 0);
	void ApplyProjectView(const ProjectIO::ProjectView& v, int view = 0);
	// wx SaveProject /views/<i>/layers of a view
	void CaptureProjectLayers(RenderCanvas* c, std::vector<ProjectIO::ProjectLayer>& layers);
	// wx m_cur_sel_type / m_cur_sel_vol / m_cur_sel_mesh (OnSelection)
	int m_cur_sel_type = -1, m_cur_sel_vol = -1, m_cur_sel_mesh = -1;
	void BuildLayersFromProject(const ProjectIO::ProjectView& view,
		const std::vector<VolumeData*>& vols, const std::vector<MeshData*>& meshes,
		RenderCanvas* canvas, bool reset_ids);
	// wx VRenderFrame::OnSelection (the tree's selectionChanged)
	void OnTreeSelection(int type, DataGroup* group, VolumeData* vd, MeshData* md);
	void OnTreeTool(int which);
	// wx VRenderVulkanView::Pick / PickVolume / SelSegVolume and
	// VPropView::SaveROIName (MainWindowAnalysis.cpp)
	void PickAt(const QPoint& pos);
	bool PickVolume(const QPoint& pos);
	bool SelSegVolume(int mode, const QPoint& pos);
	bool SelLabelSegVolumeMax(int mode, const std::vector<VolumeData*>& ref, const QPoint& pos);
	void SaveROIName(const QString& name);
	// wx VRenderFrame::OpenProject / SaveProject: /views/0/properties/brush_*
	// + w2d (view painting parameters) and /brush_diag (brush dialog
	// defaults), kept in m_prj_passthrough
	void ApplyProjectBrushSettings(int view = 0);
	void StoreProjectBrushSettings(int view = 0);
	// a volume / mesh left the view: drop every reference, delete it
	void ForgetVolume(VolumeData* vd);
	void ForgetMesh(MeshData* md);
	QWidget* m_prop_empty = nullptr;

	// wx VRenderFrame UI management (MainWindowUI.cpp)
	void BuildUIMenu();                       // m_tb_menu_ui + the pane actions
	void OnShowHideView(int pane);            // wx OnShowHideView
	void SyncUIChecks();                      // check marks = pane visibility
	MeasureDialog* EnsureMeasureDialog();
	void ApplyProjectUILayout();              // wx OpenProject /ui_layout
	void StoreProjectUILayout();              // wx SaveProject /ui_layout
	QToolBar* m_main_tb = nullptr;
	QMenu* m_tb_menu_ui = nullptr;
	QAction* m_ui_pane_act[PaneCount] = {};   // Show/Hide UI menu (wx m_tb_menu_ui)
	QAction* m_win_pane_act[PaneCount] = {};  // Windows menu (wx m_top_window)
	QAction* m_ui_toolbar_act = nullptr;      // Windows > Show/Hide Toolbar
	QAction* m_fullscreen_act = nullptr;      // Windows > Full Screen (not on macOS, as wx)
	bool m_ui_state = true;                   // wx m_ui_state
	std::map<int, bool> m_ui_state_cache;     // wx m_ui_state_cache
	bool m_syncing_ui = false;
	// wx VRenderVulkanView::OnContextMenu and its handlers
	void ShowViewContextMenu(const QPoint& pos);
	void OnShowAllFragments();
	void OnDeselectAllFragments();
	void OnHideOtherFragments();
	void OnHideSelectedFragments();
	void OnJoinSegments();
	void OnSplitSegments();
	std::vector<VolumeData*> NAVolumes(bool shown_groups_only);
	// wx VRenderVulkanView::PickMesh (after PickVolume in Pick)
	void PickMeshAt(const QPoint& pos);
	QMenu* m_view_ctx_menu = nullptr;

	// plugins (MainWindowPlugins.cpp)
	void InitPluginSystem();                  // wx ctor: LoadAllPlugins, m_tb_menu_plugin, InitPlugins
	void FinalizePluginSystem();              // wx ~VRenderFrame: FinalizePligins
	void OnPluginMenuSelect(int pid);
	void StoreProjectPluginLayout(const QString& filename);   // wx SaveProject /ui_layout/<plugin>*
	void ApplyProjectPluginLayout();          // wx OpenProject /ui_layout/<plugin>*
	QtPluginManager* m_plugin_manager = nullptr;
	QMenu* m_tb_menu_plugin = nullptr;
	QAction* m_plugins_act = nullptr;
	QStringList m_plugin_list;                // wx m_plugin_list (plugin names)
	std::map<QString, QDockWidget*> m_plugin_docks;   // display name -> pane
	// wx StartupLoad(files, datasize): DataManager::LoadVolumeData budget
	size_t m_load_datasize = 0;
};

#endif // VVD_QT_MAINWINDOW_H
