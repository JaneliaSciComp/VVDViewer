#ifndef VVD_CORE_UICALLBACKS_H
#define VVD_CORE_UICALLBACKS_H

// The engine and data layer occasionally need to talk to the user: show a
// message, or report progress while loading a large volume. They must not
// depend on a GUI toolkit to do that, so they go through this tiny interface.
// The GUI (wx today, Qt tomorrow) installs an implementation at start-up with
// vvd::UI::SetBackend(); with no backend installed messages go to stderr and
// progress is silent, which is what a headless/unit-test build wants.

#include <memory>
#include "VVDString.h"

namespace vvd
{
	// Mirrors the wxProgressDialog usage pattern: create, Update() a few
	// times, destroy. Update() returns false if the user pressed Cancel.
	class EXPORT_API Progress
	{
	public:
		virtual ~Progress() {}
		virtual bool Update(int value, const String& message = String()) = 0;
		virtual bool Pulse(const String& message = String()) = 0;
		virtual void SetRange(int maximum) = 0;
		virtual bool WasCancelled() const = 0;
	};

	enum ProgressFlags
	{
		PROGRESS_NONE        = 0,
		PROGRESS_APP_MODAL   = 1 << 0,
		PROGRESS_CAN_ABORT   = 1 << 1,
		PROGRESS_AUTO_HIDE   = 1 << 2,
		PROGRESS_ELAPSED     = 1 << 3,
		PROGRESS_REMAINING   = 1 << 4,
		PROGRESS_SMOOTH      = 1 << 5,
		PROGRESS_DEFAULT     = PROGRESS_APP_MODAL | PROGRESS_SMOOTH | PROGRESS_ELAPSED | PROGRESS_AUTO_HIDE
	};

	enum MessageFlags
	{
		MSG_OK       = 1 << 0,
		MSG_CANCEL   = 1 << 1,
		MSG_YES_NO   = 1 << 2,
		MSG_ICON_INFO    = 1 << 8,
		MSG_ICON_WARNING = 1 << 9,
		MSG_ICON_ERROR   = 1 << 10,
		MSG_ICON_QUESTION= 1 << 11,
		MSG_NO_DEFAULT   = 1 << 16   // "No" is the default button of a yes/no box
	};
	enum MessageResult { MSG_RESULT_OK = 1, MSG_RESULT_CANCEL, MSG_RESULT_YES, MSG_RESULT_NO };

	class EXPORT_API UIBackend
	{
	public:
		virtual ~UIBackend() {}
		virtual int MessageBox(const String& message, const String& caption, int flags) = 0;
		virtual std::unique_ptr<Progress> CreateProgress(const String& title, const String& message,
			int maximum, int flags) = 0;
		// Give the GUI a chance to process events during a long operation
		// (wxYield / QCoreApplication::processEvents). May be a no-op.
		virtual void Yield() {}
	};

	class EXPORT_API UI
	{
	public:
		// The engine does not own the backend; the GUI keeps it alive for the
		// lifetime of the application. Passing nullptr restores the default.
		static void SetBackend(UIBackend* backend);
		static UIBackend* GetBackend();

		static int MessageBox(const String& message,
			const String& caption = String(L"VVDViewer"), int flags = MSG_OK | MSG_ICON_INFO);
		static std::unique_ptr<Progress> CreateProgress(const String& title, const String& message,
			int maximum = 100, int flags = PROGRESS_DEFAULT);
		static void Yield();
	};
}

#endif // VVD_CORE_UICALLBACKS_H
