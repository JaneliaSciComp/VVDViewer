#ifndef VVD_QT_VOLUMEPREFETCHER_H
#define VVD_QT_VOLUMEPREFETCHER_H

// Parallel volume loading for the Qt front end (Open Project, Open Volume
// with several files, drag & drop, command line).
//
// The wx app loads project volumes with ProjectDataLoader
// (VRenderFrame::OpenVolumesFromProjectMT): its worker threads read and
// decode the files and also create the VolumeData objects and add them to the
// DataManager in completion order. Here the same loader runs in prefetch mode
// (ProjectDataLoader::SetPrefetchMode, same thread count: CPU count - 1):
// the workers only run DataManager::PrefetchVolume (reader set-up,
// Preprocess, Convert = file I/O and decompression). The caller then takes
// the results in request order on the main thread and finishes each load
// with DataManager::LoadVolumeData(..., prefetch). So VolumeData::Load
// (Texture / VolumeRenderer, which create Vulkan objects), AddVolumeData and
// SetVolumeDefault stay on the GUI thread, the DataManager list order is the
// request order, and the result is the same as the sequential load.

#include <QString>
#include <functional>
#include <memory>
#include <vector>

#include "Core/VVDString.h"

class DataManager;
class ProjectDataLoader;
struct VolumePrefetch;

class VolumePrefetcher
{
public:
	struct Request
	{
		VVDString filename;
		int type = 0;
		int ch_num = -1;
		int t_num = -1;
		// DataManager reader settings LoadVolumeData will be called with
		bool slice_seq = false;
		bool time_seq = false;
		VVDString time_id;
	};

	// Starts decoding all requests in the background (nothing happens when
	// the machine has a single core: Wait then returns null and the caller
	// loads sequentially).
	VolumePrefetcher(DataManager* dm, const std::vector<Request>& requests);
	~VolumePrefetcher();   // waits for running workers; drops unused results

	// Blocks until request `index` is decoded, calling idle() every few ms
	// meanwhile (process events / update a progress dialog there). Returns
	// the result for DataManager::LoadVolumeData(..., result.get()), or null.
	std::unique_ptr<VolumePrefetch> Wait(size_t index, const std::function<void()>& idle);

	// number of requests decoded so far
	int Decoded() const;
	int Threads() const;

private:
	std::unique_ptr<ProjectDataLoader> m_loader;
	size_t m_count = 0;
	bool m_running = false;
};

#endif // VVD_QT_VOLUMEPREFETCHER_H
