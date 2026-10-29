#include "Thread.h"

#include <functional>

namespace vvd
{
	Thread::Thread(ThreadKind kind)
		: m_kind(kind), m_state(std::make_shared<State>())
	{
	}

	Thread::~Thread()
	{
		// A joinable thread that was started but never Wait()ed on must not
		// leave a joinable std::thread behind (that would terminate()). Match
		// wx by detaching in that case.
		if (m_thread.joinable())
			m_thread.detach();
	}

	ThreadError Thread::Create(unsigned int /*stackSize*/)
	{
		if (m_state->created.exchange(true))
			return THREAD_RUNNING;
		return THREAD_NO_ERROR;
	}

	ThreadError Thread::Run()
	{
		if (!m_state->created.load())
			Create();
		if (m_state->started.exchange(true))
			return THREAD_RUNNING;

		std::shared_ptr<State> state = m_state;
		Thread* self = this;
		ThreadKind kind = m_kind;

		auto body = [self, state, kind]()
		{
			state->id.store((unsigned long)std::hash<std::thread::id>()(std::this_thread::get_id()));
			ExitCode rc = self->Entry();
			self->OnExit();
			state->exitCode = rc;
			if (kind == THREAD_DETACHED)
			{
				// wxThread semantics: a detached thread object deletes itself.
				delete self;
			}
			state->finished.store(true);
		};

		try
		{
			if (m_kind == THREAD_DETACHED)
				std::thread(body).detach();
			else
				m_thread = std::thread(body);
		}
		catch (...)
		{
			m_state->started.store(false);
			return THREAD_NO_RESOURCE;
		}
		return THREAD_NO_ERROR;
	}

	ThreadError Thread::Delete(ExitCode* rc)
	{
		// Keep a reference: for detached threads *this may be deleted while
		// we are waiting.
		std::shared_ptr<State> state = m_state;
		ThreadKind kind = m_kind;

		if (!state->started.load())
		{
			// Never ran. wx deletes a detached object here; mirror that.
			if (kind == THREAD_DETACHED)
				delete this;
			return THREAD_NOT_RUNNING;
		}

		state->stopRequested.store(true);

		if (kind == THREAD_JOINABLE)
		{
			if (m_thread.joinable())
				m_thread.join();
		}
		else
		{
			while (!state->finished.load())
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (rc) *rc = state->exitCode;
		return THREAD_NO_ERROR;
	}

	Thread::ExitCode Thread::Wait()
	{
		if (m_kind != THREAD_JOINABLE)
			return nullptr;
		if (m_thread.joinable())
			m_thread.join();
		return m_state->exitCode;
	}

	bool Thread::IsAlive() const
	{
		return m_state->started.load() && !m_state->finished.load();
	}

	bool Thread::IsRunning() const
	{
		return IsAlive();
	}

	int Thread::GetCPUCount()
	{
		unsigned int n = std::thread::hardware_concurrency();
		return n == 0 ? 1 : (int)n;
	}
}
