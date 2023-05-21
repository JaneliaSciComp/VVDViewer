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

#include <wx/wx.h>
#include "InterprocessCommunication.h"

#ifndef _MAIN_H_
#define _MAIN_H_

class VRenderApp : public wxApp
{
   public:
      VRenderApp(void) : wxApp() { m_server = NULL; m_frame = NULL;}
	  virtual bool OnInit();
	  virtual int OnExit(); 
      void OnInitCmdLine(wxCmdLineParser& parser);
      bool OnCmdLineParsed(wxCmdLineParser& parser);

#ifdef _WIN32
	  bool Initialize( int& argc, wxChar **argv ) 
      { 
         ::CoUninitialize(); 
         return wxApp::Initialize( argc, argv ); 
      }
#endif
    
#ifdef _DARWIN
      virtual void MacOpenFiles(const wxArrayString& fileNames);
#endif

#ifdef __WXGTK__
      virtual int FilterEvent(wxEvent& event);
#endif

   private:
      wxArrayString m_files;
      wxArrayString m_descs;
      wxString m_plugin_params;
      wxString m_plugin_name;
      wxFrame *m_frame;
	  MyServer *m_server;
      wxString m_tasks;
};

DECLARE_APP(VRenderApp)

#endif//_MAIN_H_
