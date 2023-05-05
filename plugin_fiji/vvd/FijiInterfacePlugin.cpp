#include "stdwx.h"
#include "FijiInterfacePlugin.h"
#include "FijiInterfacePluginWindow.h"
#include "VRenderFrame.h"
#include "wx/process.h"
#include "wx/mstream.h"
#include "compatibility.h"

IMPLEMENT_DYNAMIC_CLASS(SampleGuiPlugin1, wxObject)


FijiServer::~FijiServer(void)
{

}

void FijiServer::DeleteConnection()
{
    if (m_connection)
    {
        if (m_connection->GetConnected()) m_connection->Disconnect();
        delete m_connection;
        m_connection = NULL;
    }
}

wxConnectionBase* FijiServer::OnAcceptConnection(const wxString &topic)
{
	if (topic == _("FijiVVDPlugin"))
	{
		m_connection = new FijiServerConnection;
		m_connection->SetFrame(m_vframe);
		notifyAll(FI_CONNECT);
		return m_connection;
	}
	else
		return NULL;
}

void FijiServerConnection::SetTimeout(long seconds)
{
	if (m_sock)
		m_sock->SetTimeout(seconds);
}

long FijiServerConnection::GetTimeout()
{
	if (m_sock)
		return m_sock->GetTimeout();
	else
		return -1L;
}

void FijiServerConnection::SetSndBufSize(size_t size)
{
    if (m_sock)
        m_sock->SetOption(SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
}

bool FijiServerConnection::OnAdvise(const wxString& topic, const wxString& item, char *data,
				                int size, wxIPCFormat format)
{
	wxMessageBox(topic, data);

	return true;
}

bool FijiServerConnection::OnStartAdvise(const wxString& topic, const wxString& item)
{
	wxMessageBox(wxString::Format("OnStartAdvise(\"%s\",\"%s\")", topic.c_str(), item.c_str()));
	return true;
}

bool FijiServerConnection::OnStopAdvise(const wxString& topic, const wxString& item)
{
	wxMessageBox(wxString::Format("OnStopAdvise(\"%s\",\"%s\")", topic.c_str(), item.c_str()));
	return true;
}

bool FijiServerConnection::OnPoke(const wxString& topic, const wxString& item, const void *data, size_t size, wxIPCFormat format)
{
	char ret[1] = {10};
	switch (format)
	{
	case wxIPC_TEXT:
		//wxMessageBox(wxString::Format("OnPoke(\"%s\",\"%s\",\"%s\")", topic.c_str(), item.c_str(), (const char*)data));
		if (item == "version")
		{
			notifyAll(FI_VERSION_CHECK, data, size);
			//m_sock->Write(ret, 1);
            int newbuffersize = 1024 * 1024 * 128;
            m_sock->SetOption(SOL_SOCKET, SO_RCVBUF, &newbuffersize, sizeof(newbuffersize));
            m_sock->SetOption(SOL_SOCKET, SO_SNDBUF, &newbuffersize, sizeof(newbuffersize));
            m_sock->SetFlags(wxSOCKET_BLOCK|wxSOCKET_WAITALL);
		}
		if (item == "confirm")
		{
			Poke(item, data, size, wxIPC_TEXT);
			notifyAll(FI_CONFIRM, data, size);
            int newbuffersize = 1024 * 1024 * 128;
            m_sock->SetOption(SOL_SOCKET, SO_RCVBUF, &newbuffersize, sizeof(newbuffersize));
            m_sock->SetOption(SOL_SOCKET, SO_SNDBUF, &newbuffersize, sizeof(newbuffersize));
            SetTimeout(30);
            
		}
		if (item == "pid")
		{
			notifyAll(FI_PID, data, size);
		}
		if (item == "com_finish")
			notifyAll(FI_COMMAND_FINISHED, data, size);
		break;
	case wxIPC_PRIVATE:
        //wxMessageBox(wxString::Format("OnPoke(\"%s\",\"%s\",\"%s\")", topic.c_str(), item.c_str(), (const char*)data));
		if (item == "settimeout" && size == 4)
		{
			int sec = *((const int32_t *)data);
			if (sec > 0)
				SetTimeout(sec);
		}
        if (item == "setrcvbufsize")
        {
            int newbuffersize = *((const int32_t *)data);
            if (newbuffersize > 0)
			{
                m_sock->SetOption(SOL_SOCKET, SO_RCVBUF, &newbuffersize, sizeof(newbuffersize));
				wxMilliSleep(300);
			}
        }
        if (item == "setsndbufsize")
        {
            int newbuffersize = *((const int32_t *)data);
            if (newbuffersize > 0)
			{
                m_sock->SetOption(SOL_SOCKET, SO_SNDBUF, &newbuffersize, sizeof(newbuffersize));
				wxMilliSleep(300);
			}
        }
		if (item == "volume")
		{
			notifyAll(FI_VOLUMEDATA, data, size);
//			Poke("volume_received", "recv", 4, wxIPC_TEXT);
		}
	}
	return true;
}

bool FijiServerConnection::OnExec(const wxString &topic, const wxString &data)
{
	return true;
}




SampleGuiPlugin1::SampleGuiPlugin1()
	: wxGuiPluginBase(NULL, NULL), m_server(NULL), m_initialized(false), m_booting(false), m_fijiprocess(NULL), m_sendmask(false), m_launch_fiji_startup(false), m_tmp_ovvox(TMP_OVOX_NONE)
{

}

SampleGuiPlugin1::SampleGuiPlugin1(wxEvtHandler * handler, wxWindow * vvd)
	: wxGuiPluginBase(handler, vvd), m_server(NULL), m_initialized(false), m_booting(false), m_fijiprocess(NULL), m_sendmask(false), m_launch_fiji_startup(false), m_tmp_ovvox(TMP_OVOX_NONE)
{
}

SampleGuiPlugin1::~SampleGuiPlugin1()
{
	wxDELETE(m_server);
}

void SampleGuiPlugin1::doAction(ActionInfo *info)
{
	if (!info)
		return;
	int evid = info->id;

	switch (evid)
	{
	case FI_CONNECT:
		if (m_server && m_server->GetConnection())
			m_server->GetConnection()->addObserver(this);
		m_booting = false;
		break;
	case FI_VERSION_CHECK:
		//wxMessageBox(wxString::Format("VVD plugin ver. %s     Fiji plugin ver. %s", FI_PLUGIN_VERSION, wxString((char *)info->data)));
		m_fiji_plugin_ver = wxString((char *)info->data);
		if (wxString((char *)info->data) != FI_PLUGIN_VERSION)
		{
			//do something
		}
		notifyAll(FI_VERSION_CHECK, info->data, info->size);
		break;
	case FI_PID:
		m_pid = wxString((char *)info->data);
		//notifyAll(FI_PID, info->data, info->size);
		break;
	case FI_CONFIRM:
		m_initialized = true;
		m_timer.Stop();
		m_watch.Pause();
		break;
	case FI_VOLUMEDATA:
		if (m_vvd)
		{
			int idx = 0;
			const char *ptr = (const char *)info->data;
			size_t chk = info->size;

			VRenderFrame *vframe = (VRenderFrame *)m_vvd;
            VolumeData* cur_vol = vframe->GetCurSelVol();
						
			int name_len = *((const int32_t *)ptr);
			ptr += 4;
			chk -= 4;

			char *name = new char[name_len];
			memcpy(name, ptr, name_len);
			ptr += name_len;
			chk -= name_len;

			int nx = *((const int32_t *)ptr);
			ptr += 4;
			chk -= 4;

			int ny = *((const int32_t *)ptr);
			ptr += 4;
			chk -= 4;

			int nz = *((const int32_t *)ptr);
			ptr += 4;
			chk -= 4;

			int bd = *((const int32_t *)ptr);
			ptr += 4;
			chk -= 4;

			int r = *((const int32_t *)ptr);
			ptr += 4;
			chk -= 4;

			int g = *((const int32_t *)ptr);
			ptr += 4;
			chk -= 4;

			int b = *((const int32_t *)ptr);
			ptr += 4;
			chk -= 4;

			double spcx = *((const double *)ptr);
			ptr += 8;
			chk -= 8;

			double spcy = *((const double *)ptr);
			ptr += 8;
			chk -= 8;

			double spcz = *((const double *)ptr);
			ptr += 8;
			chk -= 8;

			if (chk == nx*ny*nz*(bd/8))
			{
				bool set_as_mask = false;
				DataManager *dm = vframe->GetDataManager();
                    
				if (dm && !m_sent_mask.IsEmpty() && m_sent_mask == name && bd == 8)
				{
					VolumeData *src = dm->GetVolumeData(name);
					if (src)
					{
						int resx, resy, resz;
						src->GetResolution(resx, resy, resz);
						if (resx == nx && resy == ny && resz == nz)
							set_as_mask = true;
					}
					m_sent_mask = "";
				}

				if (set_as_mask)
				{
					VolumeData *vd = VolumeData::DeepCopy(*dm->GetVolumeData(name), false, dm);
					if (!vd->GetMask(false)) vd->AddEmptyMask();
					
					auto vlnrrd = vd->GetMask(false);
					memcpy(vlnrrd->getNrrd()->data, ptr, nx*ny*nz*(bd/8));
					vframe->AddVolume(vd, NULL);
				}
				else
				{
					VolumeData *vd = new VolumeData();
					vd->AddEmptyData(bd, nx, ny, nz, spcx, spcy, spcz);
					FLIVR::Color col((double)r/255.0, (double)g/255.0, (double)b/255.0);
					vd->SetName(wxString(name));
					vd->SetColor(col);
					vd->SetBaseSpacings(spcx, spcy, spcz);
					vd->SetSpcFromFile(true);

					if (dm) dm->SetVolumeDefault(vd);

					auto vlnrrd = vd->GetVolume(false);
					memcpy(vlnrrd->getNrrd()->data, ptr, nx*ny*nz*(bd/8));

					if (bd == 16)
					{
						const char *p = ptr;
						int maxval = 0;
						for (int i = 0; i < chk; i+=2)
						{
							int ival = *((unsigned short *)p);
							if (maxval < ival)
								maxval = ival;
							p += 2;
						}
						vd->SetMaxValue((double)maxval);
                        double scalar_scale = 1.0;
                        if (maxval > 0.0)
                            scalar_scale = 65535.0 / maxval;
                        vd->SetScalarScale(scalar_scale);
					}
                    wxString name_wx(name);
                    int vid = dm->GetVolumeIndex(name_wx);
                    if (vid >= 0)
                    {
                        vframe->GetView(0)->ReplaceVolumeData(name_wx, vd);
                        //vframe->GetPropView()->SetVolumeData(vd);
                    }
                    else
                        vframe->AddVolume(vd, NULL);
                    vframe->OnSelection(2, 0, 0, cur_vol);
				}
			}
			delete [] name;
		}
		break;
	case FI_COMMAND_FINISHED:
        {
            VRenderFrame *vframe = (VRenderFrame *)m_vvd;
            DataManager *dm = vframe ? vframe->GetDataManager() : NULL;
            if (dm && vframe->GetView(0))
			{
				dm->SetOverrideVox(m_ovvox);
				DataGroup* group = vframe->GetView(0)->GetCurrentVolGroup();
				if (group)
				{
					group->SetVolumeSyncSpc(m_gpsyncspc);
					group->SetVolumeSyncProp(m_gpsync);
				}
			}
            m_tmp_ovvox = TMP_OVOX_NONE;
        }
		notifyAll(FI_COMMAND_FINISHED, info->data, info->size);
		break;
	}
    delete info;
}

bool SampleGuiPlugin1::SendCommand(wxString command, bool send_mask)
{
	if (!m_server || !m_server->GetConnection())
		return false;
    SendCurrentVolume(send_mask);
    
    VRenderFrame *vframe = (VRenderFrame *)m_vvd;
    DataManager *dm = vframe ? vframe->GetDataManager() : NULL;
    if (dm && vframe->GetView(0))
    {
        m_ovvox = dm->GetOverrideVox();

		DataGroup* group = vframe->GetView(0)->GetCurrentVolGroup();
		if (group)
		{
			m_gpsync = group->GetVolumeSyncProp();
			m_gpsyncspc = group->GetVolumeSyncSpc();
			if (m_tmp_ovvox == TMP_OVOX_TRUE)
			{
				dm->SetOverrideVox(true);
				group->SetVolumeSyncSpc(true);
			}
			else if (m_tmp_ovvox == TMP_OVOX_FALSE)
			{
				dm->SetOverrideVox(false);
				group->SetVolumeSyncSpc(false);
				group->SetVolumeSyncProp(false);
			}
		}
    }
    
	return m_server->GetConnection()->Poke(_("com"), command);
}

bool SampleGuiPlugin1::SendCurrentVolume(bool send_mask)
{
    VRenderFrame *vframe = (VRenderFrame *)m_vvd;
    if (!vframe || !m_server || !m_server->GetConnection() || !m_server->GetConnection()) return false;
    
    VolumeData *vd = vframe->GetCurSelVol();
    if (!vd) return false;
    
    shared_ptr<VL_Nrrd> vol;
	if (send_mask)
	{
		vol = vd->GetMask(true);
		if (!vol)
			vol = vd->GetVolume(true);
	}
	else
		vol = vd->GetVolume(true);
    
    if (!vol || !vol->getNrrd() || !vol->getNrrd()->data) return false;
    
    const wxString name = vd->GetName();
    size_t basesize = 4+name.Len()+4+4+4+4+4+4+4+8+8+8;
    int resx, resy, resz;
    vd->GetResolution(resx, resy, resz);
    double spcx, spcy, spcz;
    vd->GetSpacings(spcx, spcy, spcz);
    int bd;
    if (vol->getNrrd()->type == nrrdTypeChar || vol->getNrrd()->type == nrrdTypeUChar)
        bd = 8;
    else
        bd = 16;
    size_t imagesize = (size_t)resx*(size_t)resy*(size_t)resz*(size_t)(bd/8);
    char *buf = new char[basesize+imagesize];
    FLIVR::Color col = vd->GetColor();
    
    int32_t tmp32;
    wxMemoryOutputStream mos(buf, basesize+imagesize);
    tmp32 = (int32_t)name.Len();
    mos.Write(&tmp32, sizeof(int32_t));
    mos.Write(name.ToStdString().c_str(), tmp32);
    tmp32 = (int32_t)resx;
    mos.Write(&tmp32, sizeof(int32_t));
    tmp32 = (int32_t)resy;
    mos.Write(&tmp32, sizeof(int32_t));
    tmp32 = (int32_t)resz;
    mos.Write(&tmp32, sizeof(int32_t));
    tmp32 = (int32_t)bd;
    mos.Write(&tmp32, sizeof(int32_t));
    tmp32 = (int32_t)(col.r()*255);
    mos.Write(&tmp32, sizeof(int32_t));
    tmp32 = (int32_t)(col.g()*255);
    mos.Write(&tmp32, sizeof(int32_t));
    tmp32 = (int32_t)(col.b()*255);
    mos.Write(&tmp32, sizeof(int32_t));
    mos.Write(&spcx, sizeof(double));
    mos.Write(&spcy, sizeof(double));
    mos.Write(&spcz, sizeof(double));
    mos.Write(vol->getNrrd()->data, imagesize);
    
    tmp32 = basesize+imagesize;
    if (tmp32 > 0)
    {
        m_server->GetConnection()->SetSndBufSize(basesize+imagesize);
        m_server->GetConnection()->Poke("setrcvbufsize", &tmp32, sizeof(int32_t), wxIPC_PRIVATE);
        wxMilliSleep(500);
        m_server->GetConnection()->Poke("volume", buf, tmp32, wxIPC_PRIVATE);

		if (send_mask) m_sent_mask = name;
    }
    
    delete[] buf;
    
    return true;
}

wxString SampleGuiPlugin1::GetName() const
{
	return _("Fiji Interface");
}


wxString SampleGuiPlugin1::GetDisplayName() const
{
    return _("Fiji Interface");
}

wxString SampleGuiPlugin1::GetId() const
{
	return wxT("{4E97DF66-5FBB-4719-AF17-76C1C82D3FE1}");
}

wxWindow * SampleGuiPlugin1::CreatePanel(wxWindow * parent)
{
	return new SampleGuiPluginWindow1(this, parent);
}

bool SampleGuiPlugin1::StartFiji()
{
	if (m_initialized || m_booting)
		return true;

    wxString fjpath = m_fiji_path;
#ifdef _DARWIN
    fjpath += _("/Contents/MacOS/ImageJ-macosx");
#endif

	if (fjpath.IsEmpty() || !wxFileExists(fjpath))
	{
		wxMessageBox("Error: Could not found Fiji", "Fiji Interface");
		VRenderFrame *vframe = (VRenderFrame *)m_vvd;
		
		if (vframe)
		{
			if (!vframe->IsCreatedPluginWindow(GetName()))
				vframe->CreatePluginWindow(GetName());
			vframe->ToggleVisibilityPluginWindow(GetName(), true);
		}
		return false;
	}

	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(wxFILE_SEP_PATH, NULL);
#ifdef _DARWIN
	wxString rootdir = m_fiji_path;
	wxString rdir = expath + _("/../Resources") + wxFILE_SEP_PATH;
#else
    wxString rootdir = m_fiji_path.BeforeLast(wxFILE_SEP_PATH, NULL);
	wxString rdir = expath + wxFILE_SEP_PATH;
#endif
	wxString macrodir = rootdir + wxFILE_SEP_PATH + _("macros") + wxFILE_SEP_PATH;
	wxString plugindir = rootdir + wxFILE_SEP_PATH + _("plugins") + wxFILE_SEP_PATH;

	if (wxDirExists(macrodir) && wxFileExists(rdir+"vvd_listener.txt"))
		wxCopyFile(rdir+"vvd_listener.txt", macrodir+"vvd_listener.txt");
	if (wxDirExists(macrodir) && wxFileExists(rdir+"vvd_run.txt"))
		wxCopyFile(rdir+"vvd_run.txt", macrodir+"vvd_run.txt");
	if (wxDirExists(macrodir) && wxFileExists(rdir+"vvd_quit.txt"))
		wxCopyFile(rdir+"vvd_quit.txt", macrodir+"vvd_quit.txt");
	if (wxDirExists(plugindir) && wxFileExists(rdir+"vvd_listener.jar"))
		wxCopyFile(rdir+"vvd_listener.jar", plugindir+"vvd_listener.jar");
	if (wxDirExists(plugindir) && wxFileExists(rdir+"NBLAST_Skeletonize.jar"))
		wxCopyFile(rdir+"NBLAST_Skeletonize.jar", plugindir+"NBLAST_Skeletonize.jar");
    if (wxDirExists(plugindir) && wxFileExists(rdir+"Apply_Bigwarp_Filter.jar"))
        wxCopyFile(rdir+"Apply_Bigwarp_Filter.jar", plugindir+"Apply_Bigwarp_Filter.jar");

	m_initialized = false;
#ifdef _WIN32
	wxString command = fjpath + " -macro vvd_listener.txt";
#else
    wxString hcom = "defaults write " + m_fiji_path + "/Contents/Info LSUIElement 1";
    wxExecute(hcom, wxEXEC_HIDE_CONSOLE|wxEXEC_SYNC);
    wxString command = fjpath + " -macro vvd_listener.txt";
#endif
	m_booting = true;
	m_fijiprocess = new wxProcess();
	wxExecute(command, wxEXEC_HIDE_CONSOLE|wxEXEC_ASYNC, m_fijiprocess);
	if (m_fijiprocess) m_pid = wxString::Format("%ld", m_fijiprocess->GetPid());

	return true;
}

void SampleGuiPlugin1::CloseFiji()
{
	if (m_fiji_path.IsEmpty())
		return;
	
	if (m_fijiprocess) m_fijiprocess->Detach();

#ifdef _WIN32
	//wxString command = m_fiji_path + " -port3 -macro vvd_quit.ijm";
    //wxExecute(command);
    if (!m_pid.IsEmpty()) wxExecute("taskkill /pid "+m_pid+" /f", wxEXEC_HIDE_CONSOLE|wxEXEC_SYNC);
#else
    if (!m_pid.IsEmpty())
    {
        wxString hcom = "defaults write " + m_fiji_path + "/Contents/Info LSUIElement 0";
        if (!m_fiji_path.IsEmpty())
            wxExecute(hcom, wxEXEC_HIDE_CONSOLE|wxEXEC_SYNC);
        wxExecute("kill "+m_pid, wxEXEC_HIDE_CONSOLE|wxEXEC_SYNC);
    }
#endif

	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(wxFILE_SEP_PATH, NULL);
#ifdef _DARWIN
	wxString rootdir = m_fiji_path;
	wxString rdir = expath + _("/../Resources") + wxFILE_SEP_PATH;
#else
    wxString rootdir = m_fiji_path.BeforeLast(wxFILE_SEP_PATH, NULL);
	wxString rdir = expath + wxFILE_SEP_PATH;
#endif
	wxString macrodir = rootdir + wxFILE_SEP_PATH + _("macros") + wxFILE_SEP_PATH;
	wxString plugindir = rootdir + wxFILE_SEP_PATH + _("plugins") + wxFILE_SEP_PATH;

	if (wxFileExists(macrodir+"vvd_listener.txt"))
		wxRemoveFile(macrodir+"vvd_listener.txt");
	if (wxFileExists(macrodir+"vvd_run.txt"))
		wxRemoveFile(macrodir+"vvd_run.txt");
	if (wxFileExists(macrodir+"vvd_quit.txt"))
		wxRemoveFile(macrodir+"vvd_quit.txt");
//	if (wxFileExists(plugindir+"vvd_listener.jar"))
//		wxRemoveFile(plugindir+"vvd_listener.jar");
//	if (wxFileExists(plugindir+"NBLAST_Skeletonize.jar"))
//		wxRemoveFile(plugindir+"NBLAST_Skeletonize.jar");

	m_initialized = false;
	m_booting = false;
}

bool SampleGuiPlugin1::isReady()
{
	if (!m_server || !m_server->GetConnection() || !m_server->GetConnection()->GetConnected())
		return false;

	return m_initialized;
}

void SampleGuiPlugin1::OnInit()
{
	LoadConfigFile();
	m_server = new FijiServer;
	m_server->Create("8002");
	m_server->addObserver(this);

	if (m_launch_fiji_startup)
		StartFiji();
}

void SampleGuiPlugin1::OnDestroy()
{
	CloseFiji();
}

bool SampleGuiPlugin1::OnRun(wxString options)
{
	notifyAll(FI_RUN, options.ToStdString().c_str(), options.Len()+1);
	return true;
}

void SampleGuiPlugin1::OnTimer(wxTimerEvent& event)
{
	if (m_watch.Time() >= 15000)
	{
		CloseFiji();
		m_timer.Stop();
		m_watch.Pause();
	}
}

void SampleGuiPlugin1::LoadConfigFile()
{
	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(), NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/fiji_interface_settings.dft";
#else
	wxString dft = expath + GETSLASHS() + "fiji_interface_settings.dft";
	if (!wxFileExists(dft))
		dft = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "fiji_interface_settings.dft";
#endif
	if (wxFileExists(dft))
	{
		wxFileInputStream is(dft);
		if (is.IsOk())
		{
			wxFileConfig fconfig(is);
			wxString str;
			bool bVal;
			if (fconfig.Read("fiji_path", &str))
				m_fiji_path = str;
			if (fconfig.Read("launch_fiji", &bVal))
				m_launch_fiji_startup = bVal;
			if (fconfig.Read("send_mask", &bVal))
				m_sendmask = bVal;
		}
	}
}

void SampleGuiPlugin1::SaveConfigFile()
{
	wxFileConfig fconfig("Fiji interface default settings");

	fconfig.Write("fiji_path", m_fiji_path);
	fconfig.Write("launch_fiji", m_launch_fiji_startup);
	fconfig.Write("send_mask", m_sendmask);

	wxString expath = wxStandardPaths::Get().GetExecutablePath();
	expath = expath.BeforeLast(GETSLASH(),NULL);
#ifdef _DARWIN
	wxString dft = expath + "/../Resources/fiji_interface_settings.dft";
#else
	wxString dft = expath + GETSLASHS() + "fiji_interface_settings.dft";
	wxString dft2 = wxStandardPaths::Get().GetUserConfigDir() + GETSLASHS() + "fiji_interface_settings.dft";
	if (!wxFileExists(dft) && wxFileExists(dft2))
		dft = dft2;
#endif
	wxFileOutputStream os(dft);
	fconfig.Save(os);
}
