#ifndef VVD_QT_UIBACKEND_H
#define VVD_QT_UIBACKEND_H

// Qt implementation of the engine's vvd::UIBackend (message boxes and
// progress dialogs). Installed once from QtMain.cpp.

#include "Core/UICallbacks.h"

class QtUIBackend : public vvd::UIBackend
{
public:
	int MessageBox(const VVDString& message, const VVDString& caption, int flags) override;
	std::unique_ptr<vvd::Progress> CreateProgress(const VVDString& title, const VVDString& message,
		int maximum, int flags) override;
	void Yield() override;

	static void Install();
};

#endif // VVD_QT_UIBACKEND_H
