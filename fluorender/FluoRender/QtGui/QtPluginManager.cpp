#include "QtPluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMetaObject>

// built-in plugins (plugin_fiji/qt, plugin_nblast/qt, plugin_NA/qt)
vvd::Plugin* CreateFijiInterfacePluginQt();
vvd::Plugin* CreateNBLASTPluginQt();
vvd::Plugin* CreateNAPluginQt();

QtPluginManager::QtPluginManager(vvd::PluginHost* host, QObject* parent)
	: QObject(parent), m_host(host)
{
}

QtPluginManager::~QtPluginManager()
{
	UnloadAllPlugins();
}

QString QtPluginManager::GetPluginsPath()
{
	const QString exe_dir = QCoreApplication::applicationDirPath();
#if defined(__APPLE__)
	// wxStandardPaths::GetPluginsDir(): <bundle>/Contents/PlugIns
	return QDir::cleanPath(exe_dir + "/../PlugIns/qt");
#else
	return QDir::cleanPath(exe_dir + "/plugins/qt");
#endif
}

QString QtPluginManager::GetPluginExt()
{
#if defined(_WIN32)
	return "dll";
#elif defined(__APPLE__)
	return "dylib";
#else
	return "so";
#endif
}

void QtPluginManager::Register(vvd::Plugin* p, QLibrary* lib)
{
	if (!p) return;
	p->SetHost(m_host);
	if (lib) m_libs[p] = lib;
	if (p->IsGui())
	{
		if (auto* g = dynamic_cast<vvd::QtGuiPlugin*>(p))
		{
			m_gui.push_back(g);
			return;
		}
	}
	m_nongui.push_back(p);
}

bool QtPluginManager::LoadAllPlugins()
{
	Register(CreateFijiInterfacePluginQt(), nullptr);
	Register(CreateNBLASTPluginQt(), nullptr);
	Register(CreateNAPluginQt(), nullptr);

	// wx LoadPlugins(<plugins>/gui, *.<ext>): every library with the create
	// function; others are unloaded again
	QDir dir(GetPluginsPath());
	if (dir.exists())
	{
		const QStringList libs = dir.entryList(QStringList() << ("*." + GetPluginExt()), QDir::Files, QDir::Name);
		for (const QString& f : libs)
		{
			auto* lib = new QLibrary(dir.filePath(f));
			if (!lib->load())
			{
				fprintf(stderr, "[plugins] cannot load %s: %s\n", f.toLocal8Bit().constData(), lib->errorString().toLocal8Bit().constData());
				delete lib;
				continue;
			}
			auto create = (vvdCreateQtPlugin_function)lib->resolve("vvdCreateQtPlugin");
			vvd::Plugin* p = create ? create() : nullptr;
			if (!p)
			{
				lib->unload();
				delete lib;
				continue;
			}
			Register(p, lib);
		}
	}
	return true;
}

bool QtPluginManager::UnloadAllPlugins()
{
	std::vector<vvd::Plugin*> all(m_gui.begin(), m_gui.end());
	all.insert(all.end(), m_nongui.begin(), m_nongui.end());
	m_gui.clear();
	m_nongui.clear();
	for (vvd::Plugin* p : all)
	{
		auto it = m_libs.find(p);
		if (it != m_libs.end())
		{
			QLibrary* lib = it->second;
			auto del = (vvdDeleteQtPlugin_function)lib->resolve("vvdDeleteQtPlugin");
			if (del) del(p);
			// plugin not deletable by its library: leak it rather than free
			// it with the wrong allocator (wx deletes it with wxDELETE)
			m_libs.erase(it);
			lib->unload();
			delete lib;
		}
		else
			delete p;
	}
	return true;
}

vvd::QtGuiPlugin* QtPluginManager::GetGuiPlugin(const QString& name) const
{
	for (vvd::QtGuiPlugin* p : m_gui)
		if (p && p->GetName().ToQt() == name)
			return p;
	return nullptr;
}

vvd::Plugin* QtPluginManager::GetNonGuiPlugin(const QString& name) const
{
	for (vvd::Plugin* p : m_nongui)
		if (p && p->GetName().ToQt() == name)
			return p;
	return nullptr;
}

void QtPluginManager::InitPlugins()
{
	// wx InitPlugins: the GUI plugins only
	for (vvd::QtGuiPlugin* p : m_gui)
		if (p) p->OnInit();
}

void QtPluginManager::OnTreeUpdate()
{
	for (vvd::QtGuiPlugin* p : m_gui)
		if (p) p->OnTreeUpdate();
}

void QtPluginManager::FinalizePlugins()
{
	if (m_finalized) return;
	m_finalized = true;
	for (vvd::QtGuiPlugin* p : m_gui)
		if (p) p->OnDestroy();
}

void QtPluginManager::PostInterop(const QString& message)
{
	QMetaObject::invokeMethod(this, [this, message]() { Q_EMIT interop(message); }, Qt::QueuedConnection);
}
