#ifndef VVD_QT_PLUGINAPI_H
#define VVD_QT_PLUGINAPI_H

// Plugin interface of the Qt front end (batch 4b of the wx -> Qt port).
//
// vvd::Plugin is the toolkit-neutral part of the wx plugin ABI
// (PluginController/wxGuiPluginBase.h + wxNonGuiPluginBase.h): name, display
// name, id, OnInit (application start), OnDestroy (application close),
// OnTreeUpdate (wx PluginManager::OnTreeUpdate, called whenever the
// workspace tree is rebuilt / its icons or colours change), OnRun (the
// command line -p "<name> <options>" and VRenderFrame::RunPlugin), Work (a
// non-GUI plugin's action) and the per-project setting file (wx
// Load/SaveProjectSettingFile: <project>_files/<display name>.set).
// Nothing in it uses wx or Qt.
//
// vvd::QtGuiPlugin adds the widget factory of a GUI plugin (wx CreatePanel
// returned a wxWindow*; here a QWidget* that the host puts in a dock pane),
// and vvd::PluginHost is what a plugin may call in the application (wx
// plugins cast GetVVDMainFrame() to VRenderFrame*; MainWindow implements
// this interface, MainWindowPlugins.cpp).
//
// The three plugins of the wx build (plugin_fiji, plugin_nblast, plugin_NA)
// are compiled into VVDViewerQt (plugin_*/qt/*.cpp, registered in
// QtPluginManager.cpp); further plugins can be dropped into the plugins
// folder as shared libraries exporting the C functions below (see
// QtPluginManager.h for the folder).

#include "Core/VVDString.h"

#include <QString>
#include <QStringList>
#include <cstddef>

class QWidget;
class QObject;
class DataManager;
class VolumeData;
class MeshData;
class DataGroup;
class RenderCanvas;
class TreePanel;
class BrushTool;
class BrushToolDialog;
class AdjustPanel;
class VolumePropPanel;
class QtPluginManager;

namespace vvd
{
	class PluginHost;

	// wx wxGuiPluginBase / wxNonGuiPluginBase (toolkit-neutral)
	class Plugin
	{
	public:
		virtual ~Plugin() = default;

		// wx GetName: the id used by RunPlugin / -p / PluginExists
		virtual VVDString GetName() const = 0;
		// wx GetDisplayName: menu entry, pane caption, project keys
		virtual VVDString GetDisplayName() const { return GetName(); }
		virtual VVDString GetId() const = 0;
		// GUI plugin (a pane) or non-GUI plugin (Work() from the menu)
		virtual bool IsGui() const { return true; }

		// application start (wx PluginManager::InitPlugins -> OnInit)
		virtual void OnInit() {}
		// application close (wx FinalizePligins -> OnDestroy)
		virtual void OnDestroy() {}
		// the workspace tree changed (wx PluginManager::OnTreeUpdate)
		virtual void OnTreeUpdate() {}
		// command line / RunPlugin (wx OnRun)
		virtual bool OnRun(const VVDString& options) { (void)options; return true; }
		// non-GUI plugins (wx wxNonGuiPluginBase::Work)
		virtual int Work() { return 0; }
		// per-project settings (wx Load/SaveProjectSettingFile)
		virtual void LoadProjectSettingFile(const VVDString& path) { (void)path; }
		virtual void SaveProjectSettingFile(const VVDString& path) { (void)path; }

		// wx SetVVDMainFrame / GetVVDMainFrame
		void SetHost(PluginHost* host) { m_host = host; }
		PluginHost* GetHost() const { return m_host; }

	protected:
		PluginHost* m_host = nullptr;
	};

	// a GUI plugin of the Qt front end: the widget of its pane
	class QtGuiPlugin : public Plugin
	{
	public:
		// wx CreatePanel(parent); called once, when the pane is first shown
		virtual QWidget* CreatePanel(QWidget* parent) = 0;
	};

	// what plugins may call in the application (wx VRenderFrame methods the
	// wx plugins use). All views are canvases of MainWindow.
	class PluginHost
	{
	public:
		virtual ~PluginHost() = default;

		virtual QWidget* PluginParentWidget() = 0;              // the main window
		virtual QtPluginManager* GetPluginManager() = 0;

		// data / selection (wx GetDataManager, GetCurSelType, GetCurSelVol,
		// GetCurSelMesh, GetView(i), GetViewNum, GetTree, GetBrushToolDlg,
		// GetAdjustView, GetPropView)
		virtual DataManager* PluginDataManager() = 0;
		virtual int GetCurSelType() = 0;
		virtual VolumeData* GetCurSelVol() = 0;
		virtual MeshData* GetCurSelMesh() = 0;
		virtual int PluginViewNum() = 0;
		virtual RenderCanvas* PluginView(int i) = 0;
		virtual TreePanel* PluginTree() = 0;
		virtual BrushToolDialog* PluginBrushDialog() = 0;
		virtual BrushTool* PluginBrushTool(int view) = 0;
		virtual AdjustPanel* PluginAdjustPanel() = 0;
		virtual VolumePropPanel* PluginPropPanel() = 0;

		// wx VRenderFrame::AddVolume(vd, view): DataManager + the view (null:
		// view 0), UpdateTree(name, 2), InitView(INIT_BOUNDS | INIT_CENTER)
		virtual void PluginAddVolume(VolumeData* vd, RenderCanvas* view = nullptr) = 0;
		// wx VRenderVulkanView::ReplaceVolumeData(name, dst) of a view (null:
		// view 0): the volume called name is replaced by dst in the view's
		// layers and in the DataManager (which deletes the old one); false
		// when the view has no such volume
		virtual bool PluginReplaceVolume(const QString& name, VolumeData* dst, RenderCanvas* view = nullptr) = 0;
		// wx VRenderView::RemoveGroup (group and members out of the view and
		// the DataManager)
		virtual void PluginRemoveGroup(const QString& name, RenderCanvas* view = nullptr) = 0;
		// one volume out of every view and the DataManager (the panes let go
		// of it first), e.g. a load a plugin cannot use
		virtual void PluginRemoveVolume(VolumeData* vd) = 0;
		// wx UpdateTree(name, type) / UpdateTreeIcons / RefreshVRenderViews
		virtual void PluginUpdateTree(const QString& name = QString(), int type = -1) = 0;
		virtual void PluginUpdateTreeIcons() = 0;
		virtual void PluginRefreshViews(bool tree = false) = 0;
		// wx OnSelection(type, vrv, group, vd): the panes follow vd
		virtual void PluginSelect(int type, RenderCanvas* view, DataGroup* group, VolumeData* vd) = 0;
		// wx StartupLoad(files, datasize, descs) / LoadMeshes(files)
		virtual void PluginStartupLoad(const QStringList& files, size_t datasize = 0, const QStringList& descs = QStringList()) = 0;
		virtual void PluginLoadMeshes(const QStringList& files) = 0;
		// wx UploadFileRemote (curl upload + RNFR / RNTO rename)
		virtual int PluginUploadFileRemote(const QString& url, const QString& upfname, const QString& loc_fpath,
			const QString& usr, const QString& pwd = QString()) = 0;

		// wx plugin window management
		virtual bool PluginExists(const QString& name) = 0;
		virtual bool IsCreatedPluginWindow(const QString& name) = 0;
		virtual bool IsShownPluginWindow(const QString& name) = 0;
		virtual void CreatePluginWindow(const QString& name, bool show = true) = 0;
		virtual void ToggleVisibilityPluginWindow(const QString& name, bool show, int docking = 0) = 0;
		virtual bool RunPlugin(const QString& name, const QString& options, bool show = false) = 0;
		// wx wxEVT_GUI_PLUGIN_INTEROP (AddPendingEvent on the plugin manager's
		// handler): delivered asynchronously to QtPluginManager::interop
		virtual void PostPluginInterop(const QString& message) = 0;
	};
}

// shared-library plugins (not used by the built-in ones): a library in the
// plugins folder exports
//   extern "C" vvd::Plugin* vvdCreateQtPlugin();
//   extern "C" void vvdDeleteQtPlugin(vvd::Plugin*);
// (wx: CreatePlugin / DeletePlugin). Such a plugin may only rely on the
// virtual interfaces above and on Qt.
typedef vvd::Plugin* (*vvdCreateQtPlugin_function)();
typedef void (*vvdDeleteQtPlugin_function)(vvd::Plugin*);

#endif // VVD_QT_PLUGINAPI_H
