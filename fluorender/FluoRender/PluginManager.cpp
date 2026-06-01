#include "PluginManager.h"
#include <wx/listimpl.cpp>

WX_DEFINE_LIST(wxNonGuiPluginBaseList);
WX_DEFINE_LIST(wxGuiPluginBaseList);

PluginManager::~PluginManager()
{
	Clear();
}

bool PluginManager::LoadAllPlugins(bool forceProgramPath)
{
	wxString pluginsRootDir = GetPluginsPath(forceProgramPath);
	bool result = true;
	result &= LoadPlugins<wxNonGuiPluginBase,
		wxNonGuiPluginBaseList,
		wxNonGuiPluginToDllDictionary,
		CreatePlugin_function>(pluginsRootDir,
		m_NonGuiPlugins, 
		m_MapNonGuiPluginsDll,
		wxT("nongui"));
	result &= LoadPlugins<wxGuiPluginBase,
		wxGuiPluginBaseList,
		wxGuiPluginToDllDictionary,
		CreateGuiPlugin_function>(pluginsRootDir,
		m_GuiPlugins, 
		m_MapGuiPluginsDll,
		wxT("gui"));
	
	for(wxGuiPluginBaseList::compatibility_iterator node = m_GuiPlugins.GetFirst(); 
		node; node = node->GetNext())
	{
		wxGuiPluginBase * plugin = node->GetData();
		plugin->SetEventHandler(m_Handler);
		plugin->SetVVDMainFrame(m_MainFrame);
	}
	return true;
}

bool PluginManager::UnloadAllPlugins()
{
	return 
		UnloadPlugins<wxNonGuiPluginBase,
			wxNonGuiPluginBaseList,
			wxNonGuiPluginToDllDictionary,
			DeletePlugin_function>(m_NonGuiPlugins, 
			m_MapNonGuiPluginsDll) &&
		UnloadPlugins<wxGuiPluginBase,
			wxGuiPluginBaseList,
			wxGuiPluginToDllDictionary,
			DeleteGuiPlugin_function>(m_GuiPlugins, 
			m_MapGuiPluginsDll);
}

const wxNonGuiPluginBaseList & PluginManager::GetNonGuiPlugins() const
{
	return m_NonGuiPlugins;
}

const wxGuiPluginBaseList & PluginManager::GetGuiPlugins() const
{
	return m_GuiPlugins;
}

wxNonGuiPluginBase* PluginManager::GetNonGuiPlugin(wxString name) const
{
	for(wxNonGuiPluginBaseList::compatibility_iterator node = m_NonGuiPlugins.GetFirst(); 
		node; node = node->GetNext())
	{
		wxNonGuiPluginBase *plugin = node->GetData();
		if (plugin->GetName() == name)
			return plugin;
	}

	return NULL;
}

wxGuiPluginBase* PluginManager::GetGuiPlugin(wxString name) const
{
	for(wxGuiPluginBaseList::compatibility_iterator node = m_GuiPlugins.GetFirst(); 
		node; node = node->GetNext())
	{
		wxGuiPluginBase *plugin = node->GetData();
		if (plugin->GetName() == name)
			return plugin;
	}

	return NULL;
}

void PluginManager::InitPlugins()
{
	for(wxGuiPluginBaseList::compatibility_iterator node = m_GuiPlugins.GetFirst(); 
		node; node = node->GetNext())
	{
		wxGuiPluginBase *plugin = node->GetData();
		if (plugin)
			plugin->OnInit();
	}
}

void PluginManager::OnTreeUpdate()
{
	for (wxGuiPluginBaseList::compatibility_iterator node = m_GuiPlugins.GetFirst();
		node; node = node->GetNext())
	{
		wxGuiPluginBase* plugin = node->GetData();
		if (plugin)
			plugin->OnTreeUpdate();
	}
}

void PluginManager::FinalizePligins()
{
	for(wxGuiPluginBaseList::compatibility_iterator node = m_GuiPlugins.GetFirst(); 
		node; node = node->GetNext())
	{
		wxGuiPluginBase *plugin = node->GetData();
		if (plugin)
			plugin->OnDestroy();
	}
}
