// Testing aid: VVD_PAINT_TIMING=1 (or VVD_QT_PAINT_TIMING=1) prints
// "[ptime] ..." lines on stderr with the cost of every stage of a brush
// stroke (stroke recording, paint buffer flush, VolumeSelector::Select and
// its diffusion passes, mask readback, the redraw that follows). Shared by
// the wx and Qt front ends; toolkit-neutral (no wx / Qt headers).
#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace vvd
{
namespace ptime
{
	inline bool On()
	{
		static const bool on = []() {
			const char* a = std::getenv("VVD_PAINT_TIMING");
			const char* b = std::getenv("VVD_QT_PAINT_TIMING");
			return (a && std::atoi(a) != 0) || (b && std::atoi(b) != 0);
		}();
		return on;
	}

	inline double NowMs()
	{
		using namespace std::chrono;
		return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
	}

	inline void Log(const char* fmt, ...)
	{
		if (!On()) return;
		std::fprintf(stderr, "[ptime] ");
		va_list ap;
		va_start(ap, fmt);
		std::vfprintf(stderr, fmt, ap);
		va_end(ap);
		std::fprintf(stderr, "\n");
		std::fflush(stderr);
	}

	// per-Select accumulators of VolumeRenderer::draw_mask (reset by Select)
	struct MaskPassStats
	{
		int calls = 0;
		long long bricks = 0;
		long long uploads = 0;      // brick textures (re)uploaded to the GPU
		double wait_slots_ms = 0.0; // WaitIdleAllFrameSlots at entry
		double load_ms = 0.0;       // load_brick / load_brick_mask / load_brick_stroke
		double gpu_ms = 0.0;        // record + submit + fence wait
		long long submits = 0;      // command buffer submits (fence waits)
		long long skipped = 0;      // brick dispatches skipped (outside the 2D mask)
		long long mask_unchanged = 0; // dispatched (stroke clear) but the mask is unchanged
		long long verify_bricks = 0;  // VVD_SEG_VERIFY_SKIP: skippable bricks run anyway
		long long verify_changed = 0; // ... texels their dispatch changed (must be 0)
		double setup_ms = 0.0;      // pipeline / uniforms / brick sorting
		// texture pool (VulkanDevice::check_swap_memory) during the passes
		long long swaps = 0;        // loads that needed memory
		long long overwrites = 0;   // ... and reused a texture in place
		long long overwrite_hot = 0;// ... a texture of the volume being segmented
		long long evictions = 0;    // textures released
		long long evict_hot = 0;    // ... of the volume being segmented
		const void* hot_tex = nullptr; // FLIVR::Texture being segmented
		void Reset() { const void* h = hot_tex; *this = MaskPassStats(); hot_tex = h; }
	};
	inline MaskPassStats& Mask()
	{
		static MaskPassStats s;
		return s;
	}

	// stroke window: set at the button press, the release time once the
	// stroke has been segmented; frames are logged while it is open and it
	// closes when the redraw after the release has finished its brick loop
	struct StrokeWindow
	{
		bool active = false;
		double press_ms = 0.0;
		double release_ms = 0.0;   // 0 while the button is held
		int frames = 0;
		double frame_ms = 0.0;
	};
	inline StrokeWindow& Stroke()
	{
		static StrokeWindow w;
		return w;
	}

	// RAII: log "<what> <ms>" when it goes out of scope
	struct Scope
	{
		const char* what;
		double t0;
		explicit Scope(const char* w) : what(w), t0(On() ? NowMs() : 0.0) {}
		double Elapsed() const { return NowMs() - t0; }
		~Scope() { if (On()) Log("%s %.1f ms", what, NowMs() - t0); }
	};
}
}
