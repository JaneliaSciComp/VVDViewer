// See VolumePrefetcher.h.

#include "VolumePrefetcher.h"

#include <chrono>
#include <thread>

#include "DataManager.h"

namespace
{
	// ProjectDataLoader guards its queues with a static critical section that
	// the GUI hands it (wx: VRenderFrame::ms_criticalSection). In prefetch
	// mode the workers touch no renderer state, so a private one is enough.
	vvd::CriticalSection& LoaderCriticalSection()
	{
		static vvd::CriticalSection cs;
		return cs;
	}
}

VolumePrefetcher::VolumePrefetcher(DataManager* dm, const std::vector<Request>& requests)
	: m_count(requests.size())
{
	if (!dm || requests.empty())
		return;
	ProjectDataLoader::setCriticalSection(&LoaderCriticalSection());
	m_loader = std::make_unique<ProjectDataLoader>();
	// thread count: the ProjectDataLoader default (CPU count - 1), as in wx
	if (m_loader->GetMaxThreadNum() <= 0)
	{
		m_loader.reset();
		return;
	}
	m_loader->SetDataManager(dm);
	m_loader->SetPrefetchMode(true);
	std::vector<ProjectDataLoaderQueue> queues;
	queues.reserve(requests.size());
	for (const Request& r : requests)
	{
		ProjectDataLoaderQueue q(r.filename, r.ch_num, r.t_num, VVDString(), false, false,
			r.slice_seq, r.time_seq, r.time_id);
		q.type = r.type;   // the caller's suffix table (the queue's own one misses .czi/.zarr)
		queues.push_back(q);
	}
	m_loader->Set(queues);
	m_running = m_loader->Run();
}

VolumePrefetcher::~VolumePrefetcher()
{
	if (m_loader)
	{
		m_loader->ClearQueues();   // requests nobody waits for any more
		m_loader->Join();
	}
}

std::unique_ptr<VolumePrefetch> VolumePrefetcher::Wait(size_t index, const std::function<void()>& idle)
{
	if (!m_loader || !m_running || index >= m_count)
		return nullptr;
	using clock = std::chrono::steady_clock;
	auto last_idle = clock::now();
	while (!m_loader->IsPrefetchReady(index))
	{
		if (!m_loader->IsRunning() && !m_loader->IsPrefetchReady(index))
			return nullptr;   // the workers are gone (queue cleared)
		const auto now = clock::now();
		if (idle && now - last_idle >= std::chrono::milliseconds(30))
		{
			idle();
			last_idle = now;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return m_loader->TakePrefetch(index);
}

int VolumePrefetcher::Decoded() const
{
	return m_loader ? m_loader->GetProgress() : 0;
}

int VolumePrefetcher::Threads() const
{
	return m_loader ? m_loader->GetMaxThreadNum() : 0;
}
