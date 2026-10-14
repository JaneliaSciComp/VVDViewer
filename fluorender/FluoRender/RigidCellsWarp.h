// RigidCellsWarp: shape-preserving warp of a segmented (label / binary)
// volume by BigWarp landmarks. C++ port of the ImageJ plugin
// Apply_BigWarp_RigidCells.java ("Apply BigWarp to Cells (shape preserving)").
//
// Why: an exact thin plate spline (TPS) through landmarks that are only a few
// micrometres apart but demand very different displacements stretches and
// folds compact cell bodies into streaks. Instead this
//   1. fits the same moving -> target TPS (optionally regularized with the
//      dimensionless stiffness of "Apply BigWarp with Stiffness":
//      lambda = stiffness * mean squared distance of the moving landmarks
//      from their centroid, on the kernel diagonal),
//   2. splits the moving label volume into objects (26-connected components
//      per label value, or one object per label value),
//   3. per object either fits ONE Translation / Rigid / Similarity transform
//      to the TPS sampled over the object's voxels and renders the object
//      with it (nearest neighbour, inverse mapping), or ("Adaptive") averages
//      the TPS displacement over the object's own voxels with a flat-top
//      kernel whose radius grows with the local thickness, so somata move as
//      rigid blocks while thin fibres follow the TPS (forward splatting),
//   4. reports per object how much the TPS wanted to deform it and per
//      landmark a leave-one-out error (inconsistent landmark pairs).
// Label values are kept; the first object written to a voxel wins.
//
// Conventions are the plugin's: physical = zero-based voxel index * spacing +
// origin per axis; landmarks are physical moving -> target pairs. The code
// follows the Java expression by expression (no FMA contraction, same
// summation orders), so results match the plugin up to the last bits of
// libm functions (log / exp / acos / cbrt).
//
// Toolkit-neutral engine code (no wx / Qt); used by both GUIs through
// RigidCellsWarpVolumeByRulers().

#ifndef VVD_RIGIDCELLSWARP_H
#define VVD_RIGIDCELLSWARP_H

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "DLLExport.h"
#ifndef RIGIDCELLS_CORE_ONLY
#include "Core/VVDString.h"
#endif

class VolumeData;
class Ruler;

namespace rigidcells
{
	enum LocalMode { ADAPTIVE = 0, TRANSLATION = 1, RIGID = 2, SIMILARITY = 3 };
	// the plugin's choice strings (LOCAL_MODES 0-3, OBJECT_MODES 0-1)
	EXPORT_API const char* LocalModeName(int mode);
	EXPORT_API const char* ObjectModeName(int mode);

	struct Params
	{
		int local_mode = ADAPTIVE;   // LocalMode
		bool per_label = false;      // false: 26-connected components per label; true: each label value is one object
		double stiffness = 0.0;      // dimensionless TPS stiffness (0 = exact TPS, as BigWarp)
		double beta = 4.0;           // Adaptive: rigidity factor (kernel radius = factor x local thickness)
		double sigma_min = 0.15;     // Adaptive: minimum smoothing (physical units)
		double neurite = 0.5;        // Adaptive: neurite radius (thinner structures follow the TPS)
		int samples = 5000;          // TPS samples per object
		int min_voxels = 1;          // minimum object voxels
		double radius = 1.0;         // landmark search radius (physical units)
		int device = 0;              // 0 = CPU (all cores), k > 0 = GPU k-1 of GpuDeviceNames()
		bool loo = true;             // leave-one-out landmark diagnostics
		int threads = 0;             // CPU threads, 0 = all cores
	};

	struct Grid
	{
		double spacing[3] = { 1, 1, 1 };
		double origin[3] = { 0, 0, 0 };
		int size[3] = { 0, 0, 0 };
		double phys(int axis, double index) const { return index * spacing[axis] + origin[axis]; }
		double index(int axis, double phys) const { return (phys - origin[axis]) / spacing[axis]; }
	};

	struct Landmarks
	{
		std::vector<std::string> names;
		std::vector<std::array<double, 3>> moving, target;
	};

	// one row of the plugin's "RigidCells objects" table
	struct ObjectRow
	{
		std::string note;   // the table's row label
		int label = 0;
		long long voxels = 0;
		double moving_c[3] = { 0, 0, 0 }, target_c[3] = { 0, 0, 0 };
		double rotation_deg = 0, scale = 1, tps_vs_local_rms = 0, tps_vs_local_max = 0;
		double tps_folded_fraction = 0, tps_jacobian_min = 0, tps_jacobian_max = 0, shape_anisotropy = 0;
		double max_thickness = 0, thick_part_nonrigid_rms = 0, thin_part_tps_deviation_rms = 0;
		int nearby_landmarks = 0;
		double nearby_landmark_rms = 0, nearby_landmark_max = 0;
		long long written = 0, clipped = 0, collisions = 0;
		double radius_of_gyration = 0;  // not a column (warning test only)
	};

	// one row of the plugin's "RigidCells landmarks" table
	struct LandmarkRow
	{
		std::string name;   // the table's row label
		double moving[3] = { 0, 0, 0 }, disp[3] = { 0, 0, 0 };
		double leave_one_out_error = 0, forward_jacobian_det = 0;
		std::string flags;
	};

	struct Report
	{
		std::vector<ObjectRow> objects;
		bool has_landmarks = false;
		std::vector<LandmarkRow> landmarks;
		std::vector<std::string> log;      // the plugin's IJ.log lines (warnings included)
		std::vector<std::string> warnings; // the WARNING lines only
		double lambda = 0, forward_rms = 0, seconds = 0;
		int components = 0, runs = 0;
		std::string device;                // what computed the adaptive field

		// the plugin's column names (row label first, named "Label" as in
		// ImageJ's results tables)
		static std::vector<std::string> ObjectColumns();
		static std::vector<std::string> LandmarkColumns();
		// cell texts of one row (full precision numbers), same order as the columns
		static std::vector<std::string> ObjectCells(const ObjectRow& r);
		static std::vector<std::string> LandmarkCells(const LandmarkRow& r);
		std::string ObjectsCsv() const;
		std::string LandmarksCsv() const;
	};

	// moving label volume, x fastest. bytes: 1 (uint8), 2 (uint16) or 4 (float,
	// non-negative integer values)
	struct Volume
	{
		const void* data = nullptr;
		int bytes = 1;
		int nx = 0, ny = 0, nz = 0;
	};

	// progress(done, total, what); return false to cancel
	using ProgressFn = std::function<bool(int, int, const std::string&)>;

	// Runs the plugin's apply(): out is the target grid tg (same voxel type as
	// the moving volume, zero-filled by the caller). Returns false with *error
	// set (the plugin's exception text; "Cancelled." when progress returned
	// false).
	EXPORT_API bool Apply(const Volume& moving, const Grid& mg, const Grid& tg, void* out,
		const Landmarks& landmarks, const Params& params, Report& report,
		std::string* error, const ProgressFn& progress = ProgressFn());

	// OpenCL GPUs (loaded at run time, no build dependency); empty when there
	// is no OpenCL runtime. Index k here is Params::device k+1.
	EXPORT_API std::vector<std::string> GpuDeviceNames();

	// The plugin's loadLandmarks (BigWarp CSV, 8 columns, no header, inactive rows skipped).
	EXPORT_API bool LoadLandmarksCsv(const std::string& path, Landmarks& lm, std::string* error);

	// Java's Double.toString (used in the plugin's notes and log lines)
	EXPORT_API std::string JavaDouble(double v);
}

// Measurement "Apply Transform" with the cell (shape preserving) model: the
// landmarks are the ruler list exactly as WarpVolumeByRulers (2-point ruler:
// point 0 = moving, point 1 = target; locator = zero-displacement anchor), in
// physical units (= the BigWarp CSV coordinates); the output grid is src's grid
// (moving and target are the same volume, as WarpVolumeByRulers). Label values
// and the bit depth are kept; name = src name + _CELLS_ADAPTIVE / _CELLS_TRANS
// / _CELLS_RIGID / _CELLS_SIM; display properties as WarpVolumeByRulers.
// Shows an engine progress dialog (vvd::UI) that can cancel. Returns the new
// volume (not added anywhere) or nullptr with *error set (empty: cancelled).
#ifndef RIGIDCELLS_CORE_ONLY
EXPORT_API VolumeData* RigidCellsWarpVolumeByRulers(VolumeData* src, const std::vector<Ruler*>& rulers,
	const rigidcells::Params& params, rigidcells::Report* report, VVDString* error = nullptr);
#endif

#endif // VVD_RIGIDCELLSWARP_H
