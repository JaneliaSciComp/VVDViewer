#include "UICallbacks.h"

#include <iostream>

namespace vvd
{
	namespace
	{
		class NullProgress : public Progress
		{
		public:
			bool Update(int, const String&) override { return true; }
			bool Pulse(const String&) override { return true; }
			void SetRange(int) override {}
			bool WasCancelled() const override { return false; }
		};

		class ConsoleBackend : public UIBackend
		{
		public:
			int MessageBox(const String& message, const String& caption, int flags) override
			{
				std::cerr << "[" << caption.ToStdString() << "] " << message.ToStdString() << std::endl;
				// Headless: never answer "yes" to a question (a retry prompt
				// that always says yes would loop forever).
				if (flags & MSG_YES_NO) return MSG_RESULT_NO;
				if (flags & MSG_CANCEL) return MSG_RESULT_CANCEL;
				return MSG_RESULT_OK;
			}
			std::unique_ptr<Progress> CreateProgress(const String&, const String&, int, int) override
			{
				return std::unique_ptr<Progress>(new NullProgress());
			}
		};

		ConsoleBackend& DefaultBackend()
		{
			static ConsoleBackend backend;
			return backend;
		}

		UIBackend* g_backend = nullptr;
	}

	void UI::SetBackend(UIBackend* backend) { g_backend = backend; }
	UIBackend* UI::GetBackend() { return g_backend ? g_backend : &DefaultBackend(); }

	int UI::MessageBox(const String& message, const String& caption, int flags)
	{
		return GetBackend()->MessageBox(message, caption, flags);
	}

	std::unique_ptr<Progress> UI::CreateProgress(const String& title, const String& message, int maximum, int flags)
	{
		std::unique_ptr<Progress> p = GetBackend()->CreateProgress(title, message, maximum, flags);
		if (!p) p.reset(new NullProgress());
		return p;
	}

	void UI::Yield() { GetBackend()->Yield(); }
}
