#ifndef VVD_QT_QTPLUGINMANAGER_H
#define VVD_QT_QTPLUGINMANAGER_H

// Plugin manager of the Qt front end (wx PluginManager +
// PluginControllerBase).
//
// LoadAllPlugins: the built-in plugins first, in the order the wx loader
// finds their libraries (wxDir::GetAllFiles of PlugIns/gui:
// libFijiInterfacePlugin, libNBLASTGuiPlugin, libNeuronAnnotatorPlugin ->
// "Fiji Interface", "NBLAST Search", "Neuron Annotator Plugin"), then the
// shared libraries in PluginsPath() (vvdCreateQtPlugin / vvdDeleteQtPlugin,
// PluginApi.h).
//
// Plugins folder: the wx one (wx GetPluginsPath: <bundle>/Contents/PlugIns on
// macOS -- wxStandardPaths::GetPluginsDir --, <exe dir>/plugins elsewhere)
// with the subfolder "qt" instead of wx's "gui" / "nongui", so neither
// front end ever loads the other one's libraries.

#include <QObject>
#include <QString>
#include <vector>
#include <map>

#include "PluginApi.h"

class QLibrary;

class QtPluginManager : public QObject
{
	Q_OBJECT
public:
	explicit QtPluginManager(vvd::PluginHost* host, QObject* parent = nullptr);
	~QtPluginManager() override;

	bool LoadAllPlugins();
	bool UnloadAllPlugins();
	void InitPlugins();       // wx InitPlugins (OnInit of every GUI plugin)
	void OnTreeUpdate();      // wx OnTreeUpdate
	void FinalizePlugins();   // wx FinalizePligins (OnDestroy)

	const std::vector<vvd::QtGuiPlugin*>& GetGuiPlugins() const { return m_gui; }
	const std::vector<vvd::Plugin*>& GetNonGuiPlugins() const { return m_nongui; }
	vvd::QtGuiPlugin* GetGuiPlugin(const QString& name) const;
	vvd::Plugin* GetNonGuiPlugin(const QString& name) const;

	// wx GetPluginsPath(true) + "qt", GetPluginExt
	static QString GetPluginsPath();
	static QString GetPluginExt();

	// wx wxEVT_GUI_PLUGIN_INTEROP: emitted from the event loop
	void PostInterop(const QString& message);

Q_SIGNALS:
	void interop(const QString& message);

private:
	vvd::PluginHost* m_host = nullptr;
	std::vector<vvd::QtGuiPlugin*> m_gui;
	std::vector<vvd::Plugin*> m_nongui;
	// plugins created by a library (wx m_MapGuiPluginsDll / m_DllList)
	std::map<vvd::Plugin*, QLibrary*> m_libs;
	bool m_finalized = false;
	void Register(vvd::Plugin* p, QLibrary* lib);
};

#endif // VVD_QT_QTPLUGINMANAGER_H
