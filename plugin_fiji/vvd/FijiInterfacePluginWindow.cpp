
// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "FijiInterfacePlugin.h"
#include "FijiInterfacePluginWindow.h"
#include "VRenderFrame.h"
#include <wx/tokenzr.h>

/*
 * SampleGuiPluginWindow1 type definition
 */

IMPLEMENT_DYNAMIC_CLASS( SampleGuiPluginWindow1, wxGuiPluginWindowBase )


/*
 * SampleGuiPluginWindow1 event table definition
 */

BEGIN_EVENT_TABLE( SampleGuiPluginWindow1, wxGuiPluginWindowBase )

////@begin SampleGuiPluginWindow1 event table entries
    EVT_BUTTON( ID_SEND_EVENT_BUTTON, SampleGuiPluginWindow1::OnSENDEVENTBUTTONClick )
	EVT_TIMER(ID_PendingCommandTimer, SampleGuiPluginWindow1::OnPendingCommandTimer)
	EVT_TIMER(ID_WaitTimer, SampleGuiPluginWindow1::OnWaitTimer)
	EVT_CLOSE(SampleGuiPluginWindow1::OnClose)
////@end SampleGuiPluginWindow1 event table entries

END_EVENT_TABLE()


/*
 * SampleGuiPluginWindow1 constructors
 */

SampleGuiPluginWindow1::SampleGuiPluginWindow1()
{
    Init();
}

SampleGuiPluginWindow1::SampleGuiPluginWindow1( wxGuiPluginBase * plugin, 
											   wxWindow* parent, wxWindowID id, 
											   const wxPoint& pos, const wxSize& size, 
											   long style )
{
    Init();
    Create(plugin, parent, id, pos, size, style);
}


/*
 * SampleGuiPluginWindow1 creator
 */

bool SampleGuiPluginWindow1::Create(wxGuiPluginBase * plugin, 
									wxWindow* parent, wxWindowID id, 
									const wxPoint& pos, const wxSize& size, 
									long style )
{
    wxGuiPluginWindowBase::Create(plugin, parent, id, pos, size, style );

    CreateControls();
    if (GetSizer())
    {
        GetSizer()->SetSizeHints(this);
    }
    Centre();

	((SampleGuiPlugin1 *)GetPlugin())->addObserver(this);

    return true;
}


/*
 * SampleGuiPluginWindow1 destructor
 */

SampleGuiPluginWindow1::~SampleGuiPluginWindow1()
{
	SampleGuiPlugin1* plugin = (SampleGuiPlugin1 *)GetPlugin();
	if (plugin)
	{
		if (m_FijiPickCtrl) plugin->SetFijiPath(m_FijiPickCtrl->GetPath());
		if (m_LaunchChk)    plugin->SetLaunchFijiAtStartup(m_LaunchChk->GetValue());
		if (m_SendMaskChk)  plugin->SetSendMask(m_SendMaskChk->GetValue());
		plugin->SaveConfigFile();
	}
}


/*
 * Member initialisation
 */

void SampleGuiPluginWindow1::Init()
{
    m_VerTextCtrl = NULL;
	m_FijiPickCtrl = NULL;
	m_CommandTextCtrl = NULL;
	m_CommandButton = NULL;
	m_SendMaskChk = NULL;
	m_LaunchChk = NULL;
	m_prg_diag = NULL;
	m_pctimer = new wxTimer(this, ID_PendingCommandTimer);
	m_waitingforfiji = false;
	m_wtimer = new wxTimer(this, ID_WaitTimer);
}


/*
 * Control creation for SampleGuiPluginWindow1
 */

void SampleGuiPluginWindow1::CreateControls()
{    
	wxString fpath;
	bool sendmask = false;
	bool launch_fiji = false;
	SampleGuiPlugin1* plugin = (SampleGuiPlugin1 *)GetPlugin();
	if (plugin)
	{
		fpath = plugin->GetFijiPath();
		sendmask = plugin->GetSendMask();
		launch_fiji = plugin->GetLaunchFijiAtStartup();
	}

////@begin SampleGuiPluginWindow1 content construction
    SampleGuiPluginWindow1* itemGuiPluginWindowBase1 = this;

    wxBoxSizer* itemBoxSizer2 = new wxBoxSizer(wxVERTICAL);
    
    wxStaticText* itemStaticText3 = new wxStaticText( itemGuiPluginWindowBase1, wxID_STATIC, _("Info:"), wxDefaultPosition, wxDefaultSize, 0 );
    itemBoxSizer2->Add(itemStaticText3, 0, wxALIGN_LEFT|wxLEFT|wxRIGHT|wxTOP, 5);

	wxString str = "Connectiong to Fiji...";
	if (plugin->isReady()) str = wxString::Format("VVD plugin ver. %s   Fiji plugin ver. %s", FI_PLUGIN_VERSION, plugin->GetFijiPluginVer());
    m_VerTextCtrl = new wxTextCtrl( itemGuiPluginWindowBase1, ID_SAMPLE_TEXTCTRL, str, wxDefaultPosition, wxSize(500, -1), wxTE_READONLY);
    itemBoxSizer2->Add(m_VerTextCtrl, 0, wxGROW|wxLEFT|wxRIGHT|wxTOP, 5);

	m_FijiPickCtrl = new wxFilePickerCtrl( itemGuiPluginWindowBase1, ID_SAMPLE_FIJI, fpath, _("path"), wxFileSelectorDefaultWildcardStr, wxDefaultPosition, wxSize(500, -1));
    itemBoxSizer2->Add(m_FijiPickCtrl, 0, wxGROW|wxLEFT|wxRIGHT|wxTOP, 5);
	
	m_LaunchChk = new wxCheckBox(itemGuiPluginWindowBase1, ID_SAMPLE_STARTUP, "Launch Fiji at startup ");
	m_LaunchChk->SetValue(launch_fiji);
	itemBoxSizer2->Add(5, 5);
	itemBoxSizer2->Add(m_LaunchChk, 0, wxLEFT, 10);
	itemBoxSizer2->Add(5, 15);

	m_CommandTextCtrl = new wxTextCtrl( itemGuiPluginWindowBase1, ID_SAMPLE_COMMAND, _("Open..."), wxDefaultPosition, wxSize(500, -1), 0 );
    itemBoxSizer2->Add(m_CommandTextCtrl, 0, wxGROW|wxLEFT|wxRIGHT|wxTOP, 5);

	m_SendMaskChk = new wxCheckBox(itemGuiPluginWindowBase1, ID_SAMPLE_MASK, "Process mask ");
	m_SendMaskChk->SetValue(sendmask);
	itemBoxSizer2->Add(5, 5);
	itemBoxSizer2->Add(m_SendMaskChk, 0, wxLEFT, 10);

    m_CommandButton = new wxButton( itemGuiPluginWindowBase1, ID_SEND_EVENT_BUTTON, _("Send event"), wxDefaultPosition, wxDefaultSize, 0 );
    itemBoxSizer2->Add(m_CommandButton, 0, wxALIGN_CENTER_HORIZONTAL|wxALL, 5);

	itemGuiPluginWindowBase1->SetSizer(itemBoxSizer2);
	itemGuiPluginWindowBase1->Layout();
////@end SampleGuiPluginWindow1 content construction

	//m_wtimer->Start(50);
}

void SampleGuiPluginWindow1::EnableControls(bool enable)
{
	if (enable)
	{
		if (m_FijiPickCtrl) m_FijiPickCtrl->Enable();
		if (m_CommandTextCtrl) m_CommandTextCtrl->Enable();
		if (m_CommandButton) m_CommandButton->Enable();
		if (m_SendMaskChk) m_SendMaskChk->Enable();
		if (m_LaunchChk) m_LaunchChk->Enable();
	}
	else 
	{
		if (m_FijiPickCtrl) m_FijiPickCtrl->Disable();
		if (m_CommandTextCtrl) m_CommandTextCtrl->Disable();
		if (m_CommandButton) m_CommandButton->Disable();
		if (m_SendMaskChk) m_SendMaskChk->Disable();
		if (m_LaunchChk) m_LaunchChk->Disable();
	}
}

bool SampleGuiPluginWindow1::ShowToolTips()
{
    return true;
}

/*
 * Get bitmap resources
 */

wxBitmap SampleGuiPluginWindow1::GetBitmapResource( const wxString& name )
{
    // Bitmap retrieval
////@begin SampleGuiPluginWindow1 bitmap retrieval
    wxUnusedVar(name);
    return wxNullBitmap;
////@end SampleGuiPluginWindow1 bitmap retrieval
}

/*
 * Get icon resources
 */

wxIcon SampleGuiPluginWindow1::GetIconResource( const wxString& name )
{
    // Icon retrieval
////@begin SampleGuiPluginWindow1 icon retrieval
    wxUnusedVar(name);
    return wxNullIcon;
////@end SampleGuiPluginWindow1 icon retrieval
}

void SampleGuiPluginWindow1::doAction(ActionInfo *info)
{
	if (!info)
		return;
	int evid = info->id;

	switch (evid)
	{
	case FI_VOLUMEDATA:
		this->SetEvtHandlerEnabled(false);
		m_VerTextCtrl->SetValue(wxString((const char*)info->data));
		this->SetEvtHandlerEnabled(true);
		break;
	case FI_COMMAND_FINISHED:
		{
			m_VerTextCtrl->SetValue("finished");
			wxCommandEvent e(wxEVT_GUI_PLUGIN_INTEROP);
			e.SetString(_("Fiji Interface,finished,")+wxString((const char*)info->data));
			GetPlugin()->GetEventHandler()->AddPendingEvent(e);
		}
		if (m_prg_diag && m_waitingforfiji)
		{
			wxDELETE(m_prg_diag);
			EnableControls(true);
			m_waitingforfiji = false;
			m_wtimer->Stop();
#if defined(_DARWIN)
            wxString act = "osascript -e 'tell application \"System Events\" to set frontmost of the first process whose unix id is "+wxString::Format("%lu",wxGetProcessId())+" to true'";
            wxExecute(act, wxEXEC_HIDE_CONSOLE|wxEXEC_SYNC);
#elif defined(_WIN32)
            wchar_t slash = L'\\';
            wxString expath = wxStandardPaths::Get().GetExecutablePath();
            expath = expath.BeforeLast(slash, NULL);
            wxString act = expath + "\\ActivatePID.vbs";
            if (wxFileExists(act))
            {
                act = _("cscript //nologo ")+act+" "+wxString::Format("%lu",wxGetProcessId());
                wxExecute(act, wxEXEC_HIDE_CONSOLE|wxEXEC_SYNC);
			}
#else

#endif
		}
		break;
	case FI_VERSION_CHECK:
		if (m_VerTextCtrl)
			m_VerTextCtrl->SetValue(wxString::Format("VVD plugin ver. %s   Fiji plugin ver. %s", FI_PLUGIN_VERSION, wxString((char *)info->data)));
		break;
	case FI_RUN:
		if (info->data)
		{
			wxString options = wxString((char *)info->data);
			wxStringTokenizer tkz(options, wxT(";"));
			wxCommandEvent e;

			wxArrayString args;
			while(tkz.HasMoreTokens())
				args.Add(tkz.GetNextToken());

			if (args.Count() == 1)
				SendCommand(args[0], m_SendMaskChk->GetValue());
			else if (args.Count() >= 2)
            {
                SampleGuiPlugin1* plugin = (SampleGuiPlugin1 *)GetPlugin();
                if (plugin && args.Count() >= 3)
                {
                    if (args[2]==_("true"))  plugin->SetTempOverrideVox(TMP_OVOX_TRUE);
                    if (args[2]==_("false")) plugin->SetTempOverrideVox(TMP_OVOX_FALSE);
                }
				SendCommand(args[0], args[1]==_("true") ? true : false);
            }
			
		}
		break;
	}
    delete info;
}

void SampleGuiPluginWindow1::SendCommand(wxString com, bool send_mask)
{
	wxString fpath = m_FijiPickCtrl->GetPath();
	SampleGuiPlugin1* plugin = (SampleGuiPlugin1 *)GetPlugin();
	if (plugin)
	{
		plugin->SetFijiPath(fpath);
		if (!plugin->isReady())
		{
			if (plugin->StartFiji())
			{
				pendingcommand = com;
				pendingcom_msk = send_mask;
				m_pctimer->Start(50);
				m_pcwatch.Start();
				if (m_prg_diag) wxDELETE(m_prg_diag);
				m_prg_diag = new wxProgressDialog(
					"Connecting to Fiji...",
					"Please wait.",
					100, 0, wxPD_SMOOTH|wxPD_AUTO_HIDE|wxPD_CAN_ABORT);
				m_prg_diag->Pulse();
				if (m_Plugin->GetVVDMainFrame())
					m_Plugin->GetVVDMainFrame()->SetEvtHandlerEnabled(false);
			}
		}
		else
		{
#if defined(_WIN32)
			wchar_t slash = L'\\';
			wxString expath = wxStandardPaths::Get().GetExecutablePath();
			expath = expath.BeforeLast(slash, NULL);
			wxString act = expath + "\\ActivatePID.vbs";
			if (wxFileExists(act))
			{
				act = _("cscript //nologo ")+act+" "+plugin->GetPID();
				wxExecute(act, wxEXEC_HIDE_CONSOLE|wxEXEC_ASYNC);
			}
#elif defined(_DARWIN)
			wxString act = "osascript -e 'tell application \"System Events\" to set frontmost of the first process whose unix id is "+plugin->GetPID()+" to true'";
            wxExecute(act, wxEXEC_HIDE_CONSOLE|wxEXEC_SYNC);
#else

#endif
			plugin->SendCommand(com, send_mask);
			m_waitingforfiji = true;
			EnableControls(false);
			if (m_prg_diag) wxDELETE(m_prg_diag);
			m_prg_diag = new wxProgressDialog(
				"Waiting for Fiji...",
				"Please wait.",
				100, m_Plugin->GetVVDMainFrame(), wxPD_SMOOTH|wxPD_AUTO_HIDE|wxPD_CAN_ABORT);
			m_prg_diag->Pulse();
			m_wtimer->Start(50);
			
			pendingcommand = "";
			pendingcom_msk = m_SendMaskChk->GetValue();
		}
	}
}

void SampleGuiPluginWindow1::OnSENDEVENTBUTTONClick( wxCommandEvent& event )
{
	SendCommand(m_CommandTextCtrl->GetValue(), m_SendMaskChk->GetValue());

	wxCommandEvent e(wxEVT_GUI_PLUGIN_INTEROP);
	e.SetString(m_VerTextCtrl->GetValue());
	GetPlugin()->GetEventHandler()->AddPendingEvent(e);

    event.Skip();
}

void SampleGuiPluginWindow1::OnPendingCommandTimer(wxTimerEvent& event)
{
	SampleGuiPlugin1* plugin = (SampleGuiPlugin1 *)GetPlugin();

	if (m_prg_diag && m_prg_diag->WasCancelled())
	{
		m_pctimer->Stop();
		m_pcwatch.Pause();
		if (m_prg_diag) wxDELETE(m_prg_diag);
		if (m_Plugin->GetVVDMainFrame()) m_Plugin->GetVVDMainFrame()->SetEvtHandlerEnabled(true);
		if (plugin) plugin->CloseFiji();
	}

	if (!plugin || plugin->isReady() || m_pcwatch.Time() >= 35000)
	{
		m_pctimer->Stop();
		m_pcwatch.Pause();
		if (m_prg_diag) wxDELETE(m_prg_diag);
		if (m_Plugin->GetVVDMainFrame()) m_Plugin->GetVVDMainFrame()->SetEvtHandlerEnabled(true);
		if (m_pcwatch.Time() >= 15000)
		{
			plugin->CloseFiji();
			wxMessageBox("Time out");
		}

		if (plugin && plugin->isReady())
			SendCommand(pendingcommand, pendingcom_msk);
	}
}

void SampleGuiPluginWindow1::OnWaitTimer(wxTimerEvent& event)
{
	SampleGuiPlugin1* plugin = (SampleGuiPlugin1 *)GetPlugin();

	if (m_prg_diag && m_prg_diag->WasCancelled())
	{
		m_wtimer->Stop();
		if (m_prg_diag) wxDELETE(m_prg_diag);
		if (plugin) plugin->CloseFiji();
		EnableControls(true);
	}
}

void SampleGuiPluginWindow1::OnClose(wxCloseEvent& event)
{
	m_pctimer->Stop();
	m_wtimer->Stop();
	event.Skip();
}
