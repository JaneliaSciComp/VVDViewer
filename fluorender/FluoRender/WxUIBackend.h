#ifndef _WXUIBACKEND_H_
#define _WXUIBACKEND_H_

// wxWidgets implementation of the engine's vvd::UIBackend: routes engine
// message boxes and progress reports to wxMessageBox / wxProgressDialog.
// Installed once by VRenderApp::OnInit().

#include "Core/UICallbacks.h"

class WxUIBackend : public vvd::UIBackend
{
public:
	int MessageBox(const VVDString& message, const VVDString& caption, int flags) override;
	std::unique_ptr<vvd::Progress> CreateProgress(const VVDString& title, const VVDString& message,
		int maximum, int flags) override;
	void Yield() override;

	// Convenience: install a process-wide instance.
	static void Install();
};

#endif // _WXUIBACKEND_H_
