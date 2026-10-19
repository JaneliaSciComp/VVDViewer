#ifndef VVD_CORE_THREAD_H
#define VVD_CORE_THREAD_H

// vvd::Thread -- a small std::thread based replacement for the subset of the
// wxThread API used by the engine (VolumeLoader, NrrdScaler, ...). The
// semantics deliberately mirror wxThread so the port stays mechanical:
//
//   * kind = THREAD_DETACHED : the object deletes itself when Entry() returns
//                              (never delete it yourself, never Wait() on it).
//   * kind = THREAD_JOINABLE : the creator owns the object and must Wait()
//                              (or Delete()) before deleting it.
//   * Create() then Run() starts the thread; Entry() is the thread body.
//   * TestDestroy() returns true once Delete() was requested -- poll it.
//   * Delete() asks the thread to stop and waits until it has terminated.
//
// vvd::CriticalSection / CriticalSectionLocker mirror the wx classes. The
// mutex is recursive (like the Win32 CRITICAL_SECTION wx wraps), which is the
// behaviour the existing code was written against.

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

#include "../DLLExport.h"
#include "WinCompat.h"

namespace vvd
{
	enum ThreadKind { THREAD_DETACHED, THREAD_JOINABLE };
	enum ThreadError { THREAD_NO_ERROR = 0, THREAD_NO_RESOURCE, THREAD_RUNNING, THREAD_NOT_RUNNING, THREAD_KILLED, THREAD_MISC_ERROR };

	class EXPORT_API CriticalSection
	{
	public:
		CriticalSection() = default;
		CriticalSection(const CriticalSection&) = delete;
		CriticalSection& operator=(const CriticalSection&) = delete;
		void Enter() { m_mutex.lock(); }
		bool TryEnter() { return m_mutex.try_lock(); }
		void Leave() { m_mutex.unlock(); }
	private:
		std::recursive_mutex m_mutex;
	};

	class CriticalSectionLocker
	{
	public:
		explicit CriticalSectionLocker(CriticalSection& cs) : m_cs(cs) { m_cs.Enter(); }
		~CriticalSectionLocker() { m_cs.Leave(); }
		CriticalSectionLocker(const CriticalSectionLocker&) = delete;
		CriticalSectionLocker& operator=(const CriticalSectionLocker&) = delete;
	private:
		CriticalSection& m_cs;
	};

	inline void MilliSleep(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
	inline void MicroSleep(unsigned long us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }

	class EXPORT_API Thread
	{
	public:
		typedef void* ExitCode;

		explicit Thread(ThreadKind kind = THREAD_DETACHED);
		virtual ~Thread();

		Thread(const Thread&) = delete;
		Thread& operator=(const Thread&) = delete;

		// wxThread-compatible surface
		ThreadError Create(unsigned int stackSize = 0);
		ThreadError Run();
		ThreadError Delete(ExitCode* rc = nullptr);
		ExitCode Wait();
		bool IsAlive() const;
		bool IsRunning() const;
		bool IsDetached() const { return m_kind == THREAD_DETACHED; }
		bool TestDestroy() const { return m_state->stopRequested.load(); }
		unsigned long GetId() const { return m_state->id.load(); }

		static int GetCPUCount();
		static void Sleep(unsigned long ms) { MilliSleep(ms); }
		static void Yield() { std::this_thread::yield(); }

	protected:
		// The thread body. Return an exit code (cast an int to ExitCode).
		virtual ExitCode Entry() = 0;
		// Called in the thread context right after Entry() returns.
		virtual void OnExit() {}

	private:
		struct State
		{
			std::atomic<bool> created{ false };
			std::atomic<bool> started{ false };
			std::atomic<bool> finished{ false };
			std::atomic<bool> stopRequested{ false };
			std::atomic<unsigned long> id{ 0 };
			ExitCode exitCode = nullptr;
		};
		ThreadKind m_kind;
		std::shared_ptr<State> m_state;   // outlives a detached thread object
		std::thread m_thread;             // only used for joinable threads
	};
}

#endif // VVD_CORE_THREAD_H
