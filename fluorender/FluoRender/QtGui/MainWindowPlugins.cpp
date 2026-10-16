// MainWindow: plugins (wx VRenderFrame plugin UI, batch 4b).
//
//   * the plugin manager (QtPluginManager: the built-in Fiji Interface,
//     NBLAST Search and Neuron Annotator plugins + shared libraries from the
//     plugins folder), created at start-up; InitPlugins at the end of the
//     constructor and FinalizePlugins on close, as wx
//   * the toolbar's Plugins button: a click pops up one entry per plugin
//     (display name; wx m_tb_menu_plugin / OnPlugins / OnPluginMenuSelect)
//   * one dock pane per GUI plugin, created when it is first shown (wx
//     ToggleVisibilityPluginWindow: an AUI pane, Dockable, close button,
//     left side); floatable
//   * RunPlugin (command line -p "<name> <options>", plugins calling each
//     other), PluginExists, Is{Created,Shown}PluginWindow
//   * OnTreeUpdate: TreePanel::treeUpdated (wx UpdateTree / UpdateTreeIcons /
//     UpdateTreeColors -> m_plugin_manager->OnTreeUpdate())
//   * projects: /ui_layout/<display name>, <display name>_float,
//     <display name>_setting (+ <project>_files/<display name>.set) as wx
//     SaveProject / OpenProject -- written for the plugins whose pane exists
//     (wx writes them for every plugin; this keeps projects saved without
//     using a plugin identical to before)
//   * the PluginHost interface (PluginApi.h) the plugins use

#include "MainWindow.h"
#include "TestRun.h"

#include <QAction>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QDockWidget>
#include "PaneScrollArea.h"
#include <QFileInfo>
#include <QDir>
#include <QCursor>
#include <QTimer>

#include <curl/curl.h>
#include <sys/stat.h>

#include "QtPluginManager.h"
#include "RenderCanvas.h"
#include "TreePanel.h"
#include "AdjustPanel.h"
#include "VolumePropPanel.h"
#include "ViewPanel.h"
#include "BrushTool.h"
#include "BrushToolDialog.h"
#include "ClipPanel.h"
#include "MoviePanel.h"
#include "InfoDialog.h"
#include "TraceDialog.h"
#include "DataManager.h"
#include "Core/FileUtil.h"
#include "FLIVR/VolumeRenderer.h"

// ---------------------------------------------------------------------------
// start-up / shut-down
// ---------------------------------------------------------------------------

void MainWindow::InitPluginSystem()
{
	m_plugin_manager = new QtPluginManager(this, this);
	m_plugin_manager->LoadAllPlugins();

	// wx VRenderFrame ctor: m_tb_menu_plugin, one entry per plugin (GUI
	// plugins first, display names), m_plugin_list = the plugin names
	m_tb_menu_plugin = new QMenu(this);
	m_tb_menu_plugin->setObjectName("PluginMenu");
	m_plugin_list.clear();
	QStringList disp;
	for (vvd::QtGuiPlugin* gp : m_plugin_manager->GetGuiPlugins())
	{
		if (!gp) continue;
		const QString name = gp->GetName().ToQt();
		const QString dname = gp->GetDisplayName().ToQt();
		if (name.isEmpty()) continue;
		m_plugin_list << name;
		disp << (dname.isEmpty() ? name : dname);
	}
	for (vvd::Plugin* ngp : m_plugin_manager->GetNonGuiPlugins())
	{
		if (!ngp) continue;
		const QString name = ngp->GetName().ToQt();
		if (name.isEmpty()) continue;
		m_plugin_list << name;
		disp << name;
	}
	for (int i = 0; i < m_plugin_list.size(); i++)
	{
		QAction* a = m_tb_menu_plugin->addAction(disp[i]);
		connect(a, &QAction::triggered, this, [this, i]() { OnPluginMenuSelect(i); });
	}
	if (m_plugins_act)
	{
		m_plugins_act->setEnabled(true);
		m_plugins_act->setToolTip(tr("Plugins"));
		// wx OnPlugins: PopupMenu(m_tb_menu_plugin) (a plain tool)
		connect(m_plugins_act, &QAction::triggered, this, [this]() {
			QWidget* btn = m_main_tb ? m_main_tb->widgetForAction(m_plugins_act) : nullptr;
			const QPoint at = btn ? btn->mapToGlobal(QPoint(0, btn->height())) : QCursor::pos();
			m_tb_menu_plugin->popup(at);
		});
		if (QWidget* btn = m_main_tb ? m_main_tb->widgetForAction(m_plugins_act) : nullptr)
			btn->setObjectName("PluginsButton");
	}
	// wx UpdateTree / UpdateTreeIcons / UpdateTreeColors -> OnTreeUpdate
	connect(m_tree, &TreePanel::treeUpdated, m_plugin_manager, &QtPluginManager::OnTreeUpdate);
	// wx ctor end: m_plugin_manager->InitPlugins()
	m_plugin_manager->InitPlugins();
}

void MainWindow::FinalizePluginSystem()
{
	// wx ~VRenderFrame: m_plugin_manager->FinalizePligins()
	if (m_plugin_manager)
		m_plugin_manager->FinalizePlugins();
}

// ---------------------------------------------------------------------------
// wx OnPluginMenuSelect / ToggleVisibilityPluginWindow / ...
// ---------------------------------------------------------------------------

void MainWindow::OnPluginMenuSelect(int pid)
{
	if (pid < 0 || pid >= m_plugin_list.size()) return;
	if (vvd::QtGuiPlugin* gp = m_plugin_manager->GetGuiPlugin(m_plugin_list[pid]))
		ToggleVisibilityPluginWindow(gp->GetName().ToQt(), true);
	else if (vvd::Plugin* ngp = m_plugin_manager->GetNonGuiPlugin(m_plugin_list[pid]))
		ngp->Work();
}

QDockWidget* MainWindow::PluginDock(const QString& name) const
{
	vvd::QtGuiPlugin* gp = m_plugin_manager ? m_plugin_manager->GetGuiPlugin(name) : nullptr;
	if (!gp) return nullptr;
	auto it = m_plugin_docks.find(gp->GetDisplayName().ToQt());
	return it == m_plugin_docks.end() ? nullptr : it->second;
}

void MainWindow::ToggleVisibilityPluginWindow(const QString& name, bool show, int docking)
{
	(void)docking;   // unused in wx as well
	vvd::QtGuiPlugin* gp = m_plugin_manager ? m_plugin_manager->GetGuiPlugin(name) : nullptr;
	if (!gp) return;
	const QString dname = gp->GetDisplayName().ToQt();
	QDockWidget* dock = PluginDock(name);
	if (show)
	{
		if (!dock)
		{
			// wx: AddPane(pp, Name(disp).Caption(disp).Dockable(true)
			// .CloseButton(true)), Left().Layer(3) -- the left column. Here
			// the pane goes to the left dock area, next to the Workspace
			// column (wx stacks it under the Workspace / Record panes of
			// layer 3, which squeezes both at the plugins' sizes)
			// The panels' own minimum sizes (wx values: NBLAST 940 px wide,
			// Fiji 500, NA up to 400 x 700) do not widen the window: the
			// pane scrolls below them (PaneScrollArea.h). A further plugin
			// pane is tabbed with a docked one already shown, so the panes
			// take one column next to the Workspace column
			QWidget* panel = gp->CreatePanel(this);
			dock = new QDockWidget(dname, this);
			dock->setObjectName("PluginDock_" + gp->GetName().ToQt().replace(' ', '_'));
			dock->setWidget(new PaneScrollArea(panel, Qt::Horizontal | Qt::Vertical, QSize(150, 100)));
			dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
			QDockWidget* tab_with = nullptr;
			for (const auto& kv : m_plugin_docks)
				if (kv.second && !kv.second->isHidden() && !kv.second->isFloating() &&
					dockWidgetArea(kv.second) == Qt::LeftDockWidgetArea)
					tab_with = kv.second;
			addDockWidget(Qt::LeftDockWidgetArea, dock);
			if (tab_with)
				tabifyDockWidget(tab_with, dock);
			else if (m_tree_dock && !m_tree_dock->isFloating() && !m_tree_dock->isHidden() &&
				dockWidgetArea(m_tree_dock) == Qt::LeftDockWidgetArea)
			{
				splitDockWidget(m_tree_dock, dock, Qt::Horizontal);
				// its preferred width, but at most a third of the window
				const int w = qMax(150, qMin(dock->sizeHint().width(), width() / 3));
				resizeDocks({ dock }, { w }, Qt::Horizontal);
			}
			m_plugin_docks[dname] = dock;
			dock->show();
			if (tab_with) dock->raise();
		}
		else
		{
			dock->show();
			dock->raise();
		}
	}
	else if (dock)
		dock->hide();
}

void MainWindow::CreatePluginWindow(const QString& name, bool show)
{
	ToggleVisibilityPluginWindow(name, show);
}

bool MainWindow::IsCreatedPluginWindow(const QString& name)
{
	return PluginDock(name) != nullptr;
}

bool MainWindow::IsShownPluginWindow(const QString& name)
{
	QDockWidget* dock = PluginDock(name);
	return dock && !dock->isHidden();
}

bool MainWindow::PluginExists(const QString& name)
{
	if (!m_plugin_manager) return false;
	return m_plugin_manager->GetGuiPlugin(name) || m_plugin_manager->GetNonGuiPlugin(name);
}

bool MainWindow::RunPlugin(const QString& name, const QString& options, bool show)
{
	if (!m_plugin_manager) return false;
	if (vvd::QtGuiPlugin* gp = m_plugin_manager->GetGuiPlugin(name))
	{
		if (show)
			ToggleVisibilityPluginWindow(gp->GetName().ToQt(), true);
		return gp->OnRun(VVDString(options));
	}
	if (vvd::Plugin* ngp = m_plugin_manager->GetNonGuiPlugin(name))
	{
		ngp->Work();
		return true;
	}
	return false;
}

void MainWindow::PostPluginInterop(const QString& message)
{
	if (m_plugin_manager) m_plugin_manager->PostInterop(message);
}

// ---------------------------------------------------------------------------
// projects (wx SaveProject 4779-4794 / OpenProject 7345-7366)
// ---------------------------------------------------------------------------

namespace
{
	const char* kPluginUILayout = "/ui_layout/";
}

void MainWindow::StoreProjectPluginLayout(const QString& filename)
{
	if (!m_plugin_manager) return;
	auto put = [this](const QString& key, const QString& v) {
		const QString full = QString(kPluginUILayout) + key;
		for (auto& kv : m_prj_passthrough)
			if (kv.first == full) { kv.second = v; return; }
		m_prj_passthrough.emplace_back(full, v);
	};
	for (vvd::QtGuiPlugin* gp : m_plugin_manager->GetGuiPlugins())
	{
		if (!gp) continue;
		QDockWidget* dock = PluginDock(gp->GetName().ToQt());
		if (!dock) continue;   // see the file comment
		const QString dname = gp->GetDisplayName().ToQt();
		put(dname, !dock->isHidden() ? "1" : "0");
		put(dname + "_float", dock->isFloating() ? "1" : "0");
		// wx: new_folder = filename + "_files"; CREATE_DIR; str = new_folder /
		// <display name>.set; SaveProjectSettingFile(str); key when it exists
		const QString new_folder = filename + "_files";
		QDir().mkpath(new_folder);
		const QString str = new_folder + "/" + dname + ".set";
		gp->SaveProjectSettingFile(VVDString(str));
		if (QFileInfo::exists(str))
			put(dname + "_setting", str);
	}
}

void MainWindow::ApplyProjectPluginLayout()
{
	if (!m_plugin_manager) return;
	auto find = [this](const QString& key, QString& v) {
		const QString full = QString(kPluginUILayout) + key;
		for (const auto& kv : m_prj_passthrough)
			if (kv.first == full) { v = kv.second; return true; }
		return false;
	};
	auto to_bool = [](const QString& s, bool& b) {
		bool ok = false;
		const long l = s.trimmed().toLong(&ok);
		if (ok) { b = l != 0; return true; }
		const QString t = s.trimmed().toLower();
		if (t == "true" || t == "yes" || t == "on") { b = true; return true; }
		if (t == "false" || t == "no" || t == "off") { b = false; return true; }
		return false;
	};
	for (vvd::QtGuiPlugin* gp : m_plugin_manager->GetGuiPlugins())
	{
		if (!gp) continue;
		const QString dname = gp->GetDisplayName().ToQt();
		const QString name = gp->GetName().ToQt();
		QString v;
		bool bval = false;
		if (!find(dname, v) || !to_bool(v, bval)) continue;
		ToggleVisibilityPluginWindow(name, bval);
		bool fl = false;
		if (find(dname + "_float", v) && to_bool(v, fl))
			if (QDockWidget* dock = PluginDock(name))
				dock->setFloating(fl);
		if (find(dname + "_setting", v))
			gp->LoadProjectSettingFile(VVDString(v));
	}
}

// ---------------------------------------------------------------------------
// PluginHost
// ---------------------------------------------------------------------------

QWidget* MainWindow::PluginParentWidget() { return this; }
QtPluginManager* MainWindow::GetPluginManager() { return m_plugin_manager; }
DataManager* MainWindow::PluginDataManager() { return m_data_mgr; }
int MainWindow::GetCurSelType() { return m_cur_sel_type; }

VolumeData* MainWindow::GetCurSelVol()
{
	// wx: m_data_mgr.GetVolumeData(m_cur_sel_vol)
	return m_data_mgr->GetVolumeData(m_cur_sel_vol);
}

MeshData* MainWindow::GetCurSelMesh()
{
	return m_data_mgr->GetMeshData(m_cur_sel_mesh);
}

int MainWindow::PluginViewNum() { return GetViewNum(); }
RenderCanvas* MainWindow::PluginView(int i) { return GetViewCanvas(i); }
TreePanel* MainWindow::PluginTree() { return m_tree; }
BrushToolDialog* MainWindow::PluginBrushDialog() { return m_brush_dlg; }
BrushTool* MainWindow::PluginBrushTool(int view) { return GetViewBrushTool(view); }
AdjustPanel* MainWindow::PluginAdjustPanel() { return m_adjust; }
VolumePropPanel* MainWindow::PluginPropPanel() { return m_prop; }

void MainWindow::PluginAddVolume(VolumeData* vd, RenderCanvas* view)
{
	if (!vd) return;
	RenderCanvas* c = view ? view : GetViewCanvas(0);
	if (!c) return;
	// wx AddVolume: m_data_mgr.AddVolumeData(vd), vrv->AddVolumeData(vd)
	// (the volume keeps its colour), UpdateTree(name, 2), InitView(BOUNDS |
	// CENTER), a 4D series switches Record/Export to the time sequence
	m_data_mgr->AddVolumeData(vd);
	m_adjust->ApplyDefaults(vd);
	c->AddVolume(vd);
	if (vd->GetReader() && vd->GetReader()->GetTimeNum() > 1)
	{
		c->SetTSeqCurNum(vd->GetReader()->GetCurTime());
		m_movie->EnableTime();
		m_movie->DisableRot();
		m_movie->SetCurrentTime(c->GetTSeqCurNum());
	}
	m_tree->UpdateTree(vd->GetName().ToQt(), TreePanel::TypeVolume);
	if (m_tree->CurrentVolume() != vd) m_tree->SelectVolume(vd);
	c->RecomputeBounds();
	c->RefreshView();
}

bool MainWindow::PluginReplaceVolume(const QString& name, VolumeData* dst, RenderCanvas* view)
{
	RenderCanvas* c = view ? view : GetViewCanvas(0);
	if (!c || !dst) return false;
	VolumeData* old = c->GetVolumeData(VVDString(name));
	if (!old || old == dst) return false;
	// panes / tools that point at the old volume (ForgetVolume without
	// taking it out of the view)
	if (m_info_dlg) m_info_dlg->SetData(nullptr);
	if (m_adjust->GetVolume() == old) m_adjust->SetVolume(nullptr);
	for (const auto& v : m_views)
	{
		if (v->sel->GetVolume() == old) v->sel->SetVolume(nullptr);
		if (v->calc->GetVolumeA() == old) v->calc->SetVolumeA(nullptr);
		if (v->calc->GetVolumeB() == old) v->calc->SetVolumeB(nullptr);
		if (v->calc->GetVolumeC() == old) v->calc->SetVolumeC(nullptr);
		if (v->canvas != c) v->canvas->RemoveVolume(old);
	}
	if (m_trace_dlg) m_trace_dlg->VolumeRemoved(old);
	if (m_prop->GetVolume() == old) { m_prop->SetVolume(nullptr); m_view->SetCurrentVolume(nullptr); }
	if (m_clip->GetVolume() == old) m_clip->SetVolume(nullptr);
	// wx VRenderVulkanView::ReplaceVolumeData: the layer (or the group
	// member) becomes dst, then m_data_mgr->ReplaceVolumeData(vid, dst)
	// (deletes the old volume), AdjustView follows dst, InitView
	if (!c->ReplaceVolumeData(VVDString(name), dst))
		return false;
	const int vid = m_data_mgr->GetVolumeIndex(dst->GetName());
	const int oid = m_data_mgr->GetVolumeIndex(old->GetName());
	if (vid >= 0 && m_data_mgr->GetVolumeData(vid) == old)
		m_data_mgr->ReplaceVolumeData(vid, dst);
	else if (oid >= 0)
		m_data_mgr->ReplaceVolumeData(oid, dst);
	DataGroup* group = c->GetParentGroup(dst);
	m_adjust->SetVolume(dst);
	if (!group) m_adjust->SetGroupLink(group);
	m_adjust->UpdateSync();
	c->RecomputeBounds();
	return true;
}

void MainWindow::PluginRemoveGroup(const QString& name, RenderCanvas* view)
{
	RenderCanvas* c = view ? view : GetViewCanvas(0);
	if (!c) return;
	std::vector<VolumeData*> vols;
	std::vector<MeshData*> meshes;
	c->RemoveGroup(VVDString(name), &vols, &meshes);
	for (VolumeData* vd : vols) ForgetVolume(vd);
	for (MeshData* md : meshes) ForgetMesh(md);
	c->RefreshView();
}

void MainWindow::PluginUpdateTree(const QString& name, int type)
{
	m_tree->UpdateTree(name, type);
	for (const auto& v : m_views) v->view->ReloadLegend();
}

void MainWindow::PluginUpdateTreeIcons()
{
	m_tree->UpdateTreeIcons();
}

void MainWindow::PluginRefreshViews(bool tree)
{
	// wx RefreshVRenderViews(tree): every view (+ UpdateTreeColors)
	for (const auto& v : m_views) v->canvas->RefreshView();
	if (tree) m_tree->UpdateTreeColors();
}

void MainWindow::PluginSelect(int type, RenderCanvas* view, DataGroup* group, VolumeData* vd)
{
	(void)group;
	// wx OnSelection(type, vrv, group, vd): in the Qt app the tree selection
	// drives the panes (OnTreeSelection)
	if (view) ActivateCanvas(view);
	if (type == 2 && vd)
	{
		if (m_tree->CurrentVolume() != vd)
			m_tree->SelectVolume(vd);
		else
			OnTreeSelection(2, view ? view->GetParentGroup(vd) : GetViewCanvas(0)->GetParentGroup(vd), vd, nullptr);
	}
}

void MainWindow::PluginStartupLoad(const QStringList& files, size_t datasize, const QStringList& descs)
{
	// wx StartupLoad(files, datasize, descs): datasize is the memory budget
	// of DataManager::LoadVolumeData (downsampled when a file is larger)
	const size_t keep = m_load_datasize;
	m_load_datasize = datasize;
	StartupLoad(files, descs);
	m_load_datasize = keep;
}

void MainWindow::PluginLoadMeshes(const QStringList& files)
{
	LoadMeshFiles(files);
}

namespace
{
	size_t upload_read_callback(void* ptr, size_t size, size_t nmemb, void* stream)
	{
		// wx my_read_callback
		const size_t retcode = fread(ptr, size, nmemb, (FILE*)stream);
		fprintf(stderr, "*** We read %zu bytes from file\n", retcode);
		return retcode;
	}
}

int MainWindow::PluginUploadFileRemote(const QString& url, const QString& upfname, const QString& loc_fpath,
	const QString& usr, const QString& pwd)
{
	// wx VRenderFrame::UploadFileRemote: upload to <url><name>.uploading,
	// then RNFR / RNTO it to <name>. wx re-initialised the process-wide
	// _g_curl handle (the brick loader's) and freed it; a handle of its own here
	const QByteArray buf1 = ("RNFR " + upfname + ".uploading").toUtf8();
	const QByteArray buf2 = ("RNTO " + upfname).toUtf8();
	const QByteArray usrpwd = (usr + ":" + pwd).toUtf8();
	const QByteArray fullurl = (url + upfname + ".uploading").toUtf8();
	const QByteArray lpath = QFile::encodeName(loc_fpath);
	struct stat file_info;
	if (stat(lpath.constData(), &file_info))
	{
		fprintf(stderr, "Couldnt open '%s': %s\n", lpath.constData(), strerror(errno));
		return 1;
	}
	const curl_off_t fsize = (curl_off_t)file_info.st_size;
	FILE* fl = fopen(lpath.constData(), "rb");
	if (!fl) return 1;
	CURL* curl = curl_easy_init();
	if (curl)
	{
		struct curl_slist* headerlist = nullptr;
		headerlist = curl_slist_append(headerlist, buf1.constData());
		headerlist = curl_slist_append(headerlist, buf2.constData());
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_read_callback);
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_URL, fullurl.constData());
		curl_easy_setopt(curl, CURLOPT_POSTQUOTE, headerlist);
		curl_easy_setopt(curl, CURLOPT_READDATA, fl);
		curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, fsize);
		curl_easy_setopt(curl, CURLOPT_USERPWD, usrpwd.constData());
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		const CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK)
			fprintf(stderr, "[upload] %s: %s\n", fullurl.constData(), curl_easy_strerror(res));
		curl_slist_free_all(headerlist);
		curl_easy_cleanup(curl);
	}
	fclose(fl);
	return 0;
}
