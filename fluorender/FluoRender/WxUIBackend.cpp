#include "WxUIBackend.h"

#include <wx/wx.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>

namespace
{
	class WxProgress : public vvd::Progress
	{
	public:
		WxProgress(const wxString& title, const wxString& message, int maximum, int style)
			: m_dlg(title, message, maximum, nullptr, style), m_cancelled(false) {}

		bool Update(int value, const VVDString& message) override
		{
			bool ok = message.empty() ? m_dlg.Update(value) : m_dlg.Update(value, wxString(message.c_str()));
			if (!ok) m_cancelled = true;
			return ok;
		}
		bool Pulse(const VVDString& message) override
		{
			bool ok = message.empty() ? m_dlg.Pulse() : m_dlg.Pulse(wxString(message.c_str()));
			if (!ok) m_cancelled = true;
			return ok;
		}
		void SetRange(int maximum) override { m_dlg.SetRange(maximum); }
		bool WasCancelled() const override { return m_cancelled || m_dlg.WasCancelled(); }

	private:
		wxProgressDialog m_dlg;
		bool m_cancelled;
	};

	int ToWxStyle(int flags)
	{
		int style = 0;
		if (flags & vvd::PROGRESS_APP_MODAL) style |= wxPD_APP_MODAL;
		if (flags & vvd::PROGRESS_CAN_ABORT) style |= wxPD_CAN_ABORT;
		if (flags & vvd::PROGRESS_AUTO_HIDE) style |= wxPD_AUTO_HIDE;
		if (flags & vvd::PROGRESS_ELAPSED)   style |= wxPD_ELAPSED_TIME;
		if (flags & vvd::PROGRESS_REMAINING) style |= wxPD_REMAINING_TIME;
		if (flags & vvd::PROGRESS_SMOOTH)    style |= wxPD_SMOOTH;
		return style;
	}
}

int WxUIBackend::MessageBox(const VVDString& message, const VVDString& caption, int flags)
{
	long style = 0;
	if (flags & vvd::MSG_YES_NO) style |= wxYES_NO;
	else style |= wxOK;
	if (flags & vvd::MSG_NO_DEFAULT) style |= wxNO_DEFAULT;
	if (flags & vvd::MSG_CANCEL) style |= wxCANCEL;
	if (flags & vvd::MSG_ICON_ERROR) style |= wxICON_ERROR;
	else if (flags & vvd::MSG_ICON_WARNING) style |= wxICON_WARNING;
	else if (flags & vvd::MSG_ICON_QUESTION) style |= wxICON_QUESTION;
	else if (flags & vvd::MSG_ICON_INFO) style |= wxICON_INFORMATION;

	int r = wxMessageBox(wxString(message.c_str()), wxString(caption.c_str()), style);
	switch (r)
	{
	case wxYES: return vvd::MSG_RESULT_YES;
	case wxNO: return vvd::MSG_RESULT_NO;
	case wxCANCEL: return vvd::MSG_RESULT_CANCEL;
	default: return vvd::MSG_RESULT_OK;
	}
}

std::unique_ptr<vvd::Progress> WxUIBackend::CreateProgress(const VVDString& title, const VVDString& message,
	int maximum, int flags)
{
	return std::unique_ptr<vvd::Progress>(
		new WxProgress(wxString(title.c_str()), wxString(message.c_str()), maximum, ToWxStyle(flags)));
}

void WxUIBackend::Yield()
{
	if (wxTheApp) wxTheApp->Yield(true);
}

void WxUIBackend::Install()
{
	static WxUIBackend backend;
	vvd::UI::SetBackend(&backend);
}
