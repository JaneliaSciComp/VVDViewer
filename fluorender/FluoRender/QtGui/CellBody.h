#ifndef VVD_QT_CELLBODY_H
#define VVD_QT_CELLBODY_H

// Where the legend puts a volume's name with "Names on Objects" (not in
// wx): on its cell body, taken as the thickest part of the object -- the
// point deepest inside it, i.e. the centre of the largest sphere that fits
// in it. Voxels above the threshold are inside. The volume is reduced to
// blocks of d^3 voxels (d: about 1/320 of its longest side, so a block is
// inside when at least half of its voxels are); processes and sheets
// thinner than a block drop out, and the distance transform of the blocks
// (exact Euclidean, in physical units from the spacings) peaks in the soma.
// Of the blocks at the peak, the one nearest their mean is taken.

#include <atomic>
#include <cstddef>

struct VVDCellBody
{
	bool found = false;
	double x = 0.0, y = 0.0, z = 0.0;   // voxel coordinates (the block's centre)
	double depth = 0.0;                 // its distance to the background (physical units)
	int block = 1;                      // block size d (voxels)
};

// data: nx * ny * nz values, x fastest, of 1 (unsigned char), 2 (unsigned
// short) or 4 (float) bytes each; threshold in data units. cancel (may be
// null) stops it early (found false). Uses every core for the block counts.
VVDCellBody VVDFindCellBody(const void* data, int bytes, size_t nx, size_t ny, size_t nz,
	double spcx, double spcy, double spcz, double threshold,
	const std::atomic<bool>* cancel = nullptr);

#endif
