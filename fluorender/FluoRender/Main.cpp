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

#include "Main.h"
#include <cstdio>
#include <iostream>
#include <wx/cmdline.h>
#include <wx/stdpaths.h>
#include <wx/filefn.h>
#include <wx/tokenzr.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include "VRenderFrame.h"
#include "compatibility.h"
// -- application --

#ifdef __WXGTK__
#include <libgen.h>
#endif

bool m_open_by_web_browser = false;

IMPLEMENT_APP(VRenderApp)

static const wxCmdLineEntryDesc g_cmdLineDesc [] =
{
   { wxCMD_LINE_OPTION, "p", "plugin", NULL, wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
   { wxCMD_LINE_OPTION, "d", "desc", NULL, wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
   { wxCMD_LINE_OPTION, "m", "macro", NULL, wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
   { wxCMD_LINE_PARAM, NULL, NULL, "input file", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL|wxCMD_LINE_PARAM_MULTIPLE },
   { wxCMD_LINE_NONE }
};

#ifdef __WXGTK__
int VRenderApp::FilterEvent(wxEvent& event)
{
    if (event.GetEventType() == wxEVT_KEY_DOWN || event.GetEventType() == wxEVT_KEY_UP)
    {
        //std::cerr << (event.GetEventType() == wxEVT_KEY_DOWN ? "KEY_DOWN" : "KEY_UP") << std::endl;
        VRenderFrame::m_key_state[((wxKeyEvent&)event).GetKeyCode()] = (event.GetEventType() == wxEVT_KEY_DOWN ? true : false);
    }

    return -1;
}
#endif


bool VRenderApp::OnInit()
{
	//_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF|_CRTDBG_LEAK_CHECK_DF);

#ifdef __WXGTK__
	char exepath[PATH_MAX];
	ssize_t count = readlink("/proc/self/exe", exepath, PATH_MAX);
	const char *exedir;
	if (count != -1) {
    	exedir = dirname(exepath);
	}
	wxString wxs_exedir((const char*)exedir);
	wxString cachepath = wxT("") + wxs_exedir + wxT("/loaders.cache");
	wxString cachedirpath = wxT("") + wxs_exedir;
	cerr << cachepath.ToStdString() << endl;
	setenv("GDK_PIXBUF_MODULE_FILE", cachepath.ToStdString().c_str(), true);
#endif

   char cpath[FILENAME_MAX];
   GETCURRENTDIR(cpath, sizeof(cpath));
   ::wxSetWorkingDirectory(wxString(s2ws(std::string(cpath))));
   // call default behaviour (mandatory)
   if (!wxApp::OnInit())
      return false;
   //add png handler
   wxImage::AddHandler(new wxPNGHandler);
   //the frame
/*   std::string title =  std::string(FLUORENDER_TITLE) + std::string(" ") +
      std::string(VERSION_MAJOR_TAG) +  std::string(".") +
      std::string(VERSION_MINOR_TAG);
*/   
   std::string title =  std::string(FLUORENDER_TITLE) + "1.6.5";

   m_frame = new VRenderFrame(
	     this,
         (wxFrame*) NULL,
         wxString(title),
         50,50,1024,768);
   
   if(m_server)
	   m_server->SetFrame((VRenderFrame *)m_frame);

#ifdef WITH_DATABASE
#ifdef _WIN32
/*
   bool registered = true;

   wxString install = wxStandardPaths::Get().GetLocalDataDir() + wxFileName::GetPathSeparator() + "install";
   if (wxFileName::FileExists(install))
   {
	   wxFile tmpif(install, wxFile::read);
	   wxString data;
	   tmpif.ReadAll(&data);
	   if(data != install) registered = false;
   }
   else registered = false;

   if (!registered)
   {

	   wxString regfile = wxStandardPaths::Get().GetLocalDataDir() + wxFileName::GetPathSeparator() + "regist.reg";
	   
	   wxString exepath = wxStandardPaths::Get().GetExecutablePath();
	   exepath.Replace(wxT("\\"), wxT("\\\\"));

	   wxString reg("Windows Registry Editor Version 5.00\n"\
					"[HKEY_CLASSES_ROOT\\vvd]\n"\
					"@=\"URL:VVD Protocol\"\n"\
					"\"URL Protocol\"=\"\"\n"\
					"[HKEY_CLASSES_ROOT\\vvd\\DefaultIcon]\n"\
					"[HKEY_CLASSES_ROOT\\vvd\\shell]\n"\
					"[HKEY_CLASSES_ROOT\\vvd\\shell\\open]\n"\
					"[HKEY_CLASSES_ROOT\\vvd\\shell\\open\\command]\n"\
					"@=\"\\\"");
	   reg += exepath;
	   reg += wxT("\\\" \\\"%1\\\"\"");

	   wxFile tmpof(regfile, wxFile::write);
	   tmpof.Write(reg);
	   tmpof.Close();

	   wxString regfilearg = wxT("/s ") + regfile;
	   TCHAR args[1024];
	   const wxChar* regfileargChars = regfilearg.c_str();  
	   for (int i = 0; i < regfilearg.Len(); i++) {
		   args[i] = regfileargChars [i];
	   }
	   args[regfilearg.Len()] = _T('\0');
	   ShellExecute(NULL, NULL, _T("regedit.exe"), args, NULL, SW_HIDE);

	   wxFile of(install, wxFile::write);
	   of.Write(install);
	   of.Close();
   }
*/
#endif
#endif
   
   m_files.Add("/home/kawaset@hhmi.org/test/aws-test-14.vrp");
   m_tasks = "hideui,advanced,record,~/output.mov";

   SetTopWindow(m_frame);
   m_frame->Show();

   SettingDlg *setting_dlg = ((VRenderFrame *)m_frame)->GetSettingDlg();
   if (setting_dlg)
	   ((VRenderFrame *)m_frame)->SetRealtimeCompression(setting_dlg->GetRealtimeCompress());

   if (m_files.Count()>0)
      ((VRenderFrame*)m_frame)->StartupLoad(m_files, 0LL, m_descs);

   if (!m_plugin_name.IsEmpty())
	   ((VRenderFrame*)m_frame)->RunPlugin(m_plugin_name, m_plugin_params, true);

   if (setting_dlg)
   {
	   setting_dlg->SetRealtimeCompress(((VRenderFrame *)m_frame)->GetRealtimeCompression());
	   setting_dlg->UpdateUI();
   }
    
   if (!m_tasks.IsEmpty())
      ((VRenderFrame*)m_frame)->SetTasks(m_tasks);
    
   return true;
}

int VRenderApp::OnExit()
{
	if (m_server) delete m_server;

	return 0;
}

void VRenderApp::OnInitCmdLine(wxCmdLineParser& parser)
{
   parser.SetDesc(g_cmdLineDesc);
   parser.SetSwitchChars(wxT("-"));
}

bool VRenderApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
    int i=0;
    wxString params;
    
    
    for (wxCmdLineArgs::const_iterator itarg=parser.GetArguments().begin();
         itarg!=parser.GetArguments().end();
         ++itarg)
    {
        wxString optionName;
        switch (itarg->GetKind())
        {
            case wxCMD_LINE_SWITCH:
                if (itarg->IsNegated()) {
                }
                else {
                }
                break;
            case wxCMD_LINE_OPTION:
                // assuming that all the options have a short name
                optionName = itarg->GetShortName();
                if (optionName == wxT("p")) {
                    wxString val = itarg->GetStrVal();
                    m_plugin_name = val.BeforeFirst(' ');
                    m_plugin_params = val.AfterFirst(' ');
                }
                else if (optionName == wxT("d")) {
                    wxString val = itarg->GetStrVal();
                    m_descs.Add(val);
                    if (!m_plugin_name.IsEmpty())
                        m_plugin_params += wxT(" -d ") + val;
                }
                else if (optionName == wxT("m")) {
                    m_tasks = itarg->GetStrVal();
                }
                break;
            case wxCMD_LINE_PARAM:
                {
                    wxString file = itarg->GetStrVal();
                    if (file.StartsWith(wxT("vvd:")))
                    {
                        m_open_by_web_browser = true;
                        if (file.Length() >= 5)
                        {
                            params = file.SubString(4, file.Length() - 1);
                            wxStringTokenizer tkz(params, wxT(","));
                            while (tkz.HasMoreTokens())
                            {
                                wxString path = tkz.GetNextToken();
                                m_files.Add(path);
                            }
                        }
                    }
                    else
                    {
                        m_files.Add(file);
                    }
                }
                break;
            case wxCMD_LINE_NONE:
                switch (itarg->GetType()) {
                    case wxCMD_LINE_VAL_NUMBER:
                        // do something with itarg->GetLongVal();
                        break;
                    case wxCMD_LINE_VAL_DOUBLE:
                        // do something with itarg->GetDoubleVal();
                        break;
                    case wxCMD_LINE_VAL_DATE:
                        // do something with itarg->GetDateVal();
                        break;
                    case wxCMD_LINE_VAL_STRING:
                        // do something with itarg->GetStrVal();
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
    }
    
    /*
    wxString list;
    for (i = 0; i < (int)parser.GetParamCount(); i++)
    {
        list += parser.GetParam(i);
    }

   wxString line;
   for (i = 0; i < (int)parser.GetParamCount(); i++)
   {
	   line += parser.GetParam(i);
	   line += " ";
   }

   wxString val;
   int count = 0;
   if (parser.Found(wxT("p"), &val))
   {
	   m_plugin_name = val.BeforeFirst(' ');
	   m_plugin_params = val.AfterFirst(' ');
	   count++;
   }

   for (i = count; i < (int)parser.GetParamCount(); i++)
   {
	   wxString file = parser.GetParam(i);

	   if (file.StartsWith(wxT("vvd:")))
	   {
		   m_open_by_web_browser = true;
		   if (file.Length() >= 5)
		   {
			   params = file.SubString(4, file.Length() - 1);
			   wxStringTokenizer tkz(params, wxT(","));
			   while (tkz.HasMoreTokens())
			   {
				   wxString path = tkz.GetNextToken();
				   m_files.Add(path);
			   }
		   }
		   break;
	   }
	   else
	   {
		   m_files.Add(file);
	   }
   }
    */
    
#ifdef _WIN32
/*   {
	   wxString message;

	   int fnum = m_files.GetCount();
	   for (i = 0; i < fnum; i++) message += m_files[i] + (i == fnum - 1 ? wxT("") : wxT(","));

	   wxLogNull lognull;

	   wxString server = "8001";
	   wxString hostName = "localhost";

	   MyClient *client = new MyClient;
	   ClientConnection *connection = (ClientConnection *)client->MakeConnection(hostName, server, "IPC TEST");
//	   ClientConnection *connection = (ClientConnection *)client->MakeConnection(hostName, "50001", "IPC TEST");
//	   connection->StartAdvise(wxString("test"));

	   if (!connection)
	   {
		   //wxMessageBox("Failed to make connection to server", "Client Demo");
		   m_server = new MyServer;
		   m_server->Create(server);
	   }
	   else
	   {
		   connection->StartAdvise(message);
		   connection->Disconnect();
		   wxExit();
		   return true;
	   }
	   delete client;
   }*/
#endif
    
   return true;
}


#ifdef _DARWIN
void VRenderApp::MacOpenFiles(const wxArrayString& fileNames)
{
    if (m_frame)
        ((VRenderFrame*)m_frame)->StartupLoad(fileNames);
}
#endif
