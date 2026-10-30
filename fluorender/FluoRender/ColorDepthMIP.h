/*
Color Depth MIP (CDM) creation, a native port of the single-channel pipeline of
Hideo Otsuna's Fiji macro Color_Depth_MIP_batch_0308_2021.ijm (mipfunction and
everything it calls) together with the Fiji plugins it runs (DSLT3D_LINE2_Multi,
DSLT_, Anisotropic Diffusion 2D, A4095_normalizer, Mask_Median_Subtraction,
Mask_Brightness_Measure, Mask_MIP_Brightness_Measure, Max_value, Three_D_Ave,
Mask255_to_4095, Gamma_, Gamma_samewindow_noswing, Z_Code_Stack_HO,
MIP_right_color) and the ImageJ 1.54p built-ins on the way, reproduced voxel for
voxel. See docs/QT_PORT.md, "Color Depth MIP".

Toolkit-neutral (no wx, no Qt): used by both the wx and the Qt front end.
*/
#ifndef VVD_COLOR_DEPTH_MIP_H
#define VVD_COLOR_DEPTH_MIP_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class VolumeData;
namespace vvd { class IniFile; }

namespace cdm
{
	// The macro's dialog. Defaults = the macro's defaults (no saved MIP_batch.txt).
	struct Options
	{
		// "Automatic Brightness adjustment" radio group (macro order)
		enum Mode
		{
			SegmentationAuto = 0,     // "Segmentation based automatic" (default)
			Saturation15 = 1,         // "Automatic 1.5% saturation (good for segmented neuron)"
			NoAdjustment = 2,         // "No adjustment"
			Max1 = 3,                 // "Max1"
			SegmentationNoLowerCut = 4// "Segmentation based no lower value cut"
		};
		int mode = SegmentationAuto;
		bool colorScale = true;       // "Add color scale"
		bool reversed = false;        // "Reversed color"
		bool gamma14 = false;         // "Apply Gamma 1.4"
		bool skeletonBolded = false;  // "Skeleton bolded MIP"
		bool dsltLine = true;         // "DSLT version": Line (true) / Normal (false)
		int startMIP = 0;             // "Starting MIP slice"
		int endMIP = 1000;            // "Ending MIP slice"
		int desiredMean = 190;        // "Desired average value" (segmentation modes)
		int threads = 0;              // 0: all hardware threads
	};

	// The radio button labels of the macro, in Options::Mode order.
	const char* ModeLabel(int mode);

	// One channel of voxel data, x fastest, then y, then z (NRRD / VolumeData
	// order; Fiji opens the same file in the same order).
	struct Volume
	{
		int nx = 0, ny = 0, nz = 0;
		int bits = 0;             // 8 (uint8), 16 (uint16) or 32 (float)
		const void* data = nullptr;
	};

	struct Result
	{
		bool ok = false;
		bool canceled = false;
		std::string error;
		int width = 0, height = 0;
		std::vector<uint8_t> rgb;  // width*height*3, top row first
		// values the macro prints / uses for the file name
		double applyV = 0;         // the brightness written as "Max: ..." (segmentation modes)
		double sigsize = 0;        // DSLT signal size [% of the cropped MIP]
		double sigsizethre = 0;    // threshold signal size [%]
		double Inimax = 0, Inimin = 0, DefMaxValue = 0;
		int bitd = 0;
		std::string log;           // the macro's print() lines
	};

	// fraction 0..1 and a short status; return false to cancel
	typedef std::function<bool(double, const std::string&)> Progress;

	// Folder with the masks (JFRC2010_Mask.tif, JFRC2013_20x_Mask.tif,
	// Mask_VNC_Female.tif, Mask_VNC_Male.tif, JFRC2010_3D_Mask.nrrd) and the
	// LUT (PsychedelicRainBow2.lut): <Resources>/ColorDepthMIP (macOS bundle)
	// or ColorDepthMIP next to the executable. VVD_CDM_DATA overrides it.
	std::wstring DataDir();

	// Runs the pipeline. dataDir: see DataDir(). Thread-safe (per-call state).
	// The macro's print() lines go to res.log and, with VVD_CDM_DEBUG=1, to
	// stderr; VVD_CDM_DUMP=<dir> writes the intermediate images (raw).
	bool Compute(const Volume& vol, const Options& opt, const std::wstring& dataDir,
		Result& res, const Progress& progress = Progress());

	// RGB 8-bit, contiguous, PackBits compressed, with an ImageJ-style
	// ImageDescription (Fiji opens it as an RGB image).
	bool WriteTiff(const std::wstring& path, const Result& res, std::string* error = nullptr);

	// The macro's file name for a single-channel image: the volume name
	// without extension, cut at "_QI" (Color_Depth_MIP_batch line 838-844),
	// plus ".tif".
	std::wstring DefaultFileName(const std::wstring& volumeName);

	// The full-resolution voxels of a VolumeData that are in memory (current
	// time point). Returns false with a message for pyramid / brick-streamed
	// data, empty volumes, single slices or unsupported types.
	bool GetVolume(VolumeData* vd, Volume& vol, std::string& error);

	// The remembered dialog values in fluorender.set, group
	// "[color depth mip]" (written by the Qt app; the wx app keeps the keys
	// and reads them). Missing keys keep the values in o.
	void LoadOptions(const vvd::IniFile& cfg, Options& o);
	void SaveOptions(vvd::IniFile& cfg, const Options& o);
}

#endif
