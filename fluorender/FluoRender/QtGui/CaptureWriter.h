#ifndef VVD_QT_CAPTUREWRITER_H
#define VVD_QT_CAPTUREWRITER_H

// TIFF writer of the wx "Capture" button (VRenderVulkanView::PostDraw), kept
// separate from RenderCanvas so the movie export (MoviePanel / wx
// VMovieView, which goes through the same PostDraw in wx) can reuse it.
//
// Output is what wx writes, tag for tag, through the same libtiff calls:
// TIFFOpen(path, "wb") (big-endian; "wb8" = BigTIFF when big_tiff is set),
// IMAGEWIDTH, IMAGELENGTH, SAMPLESPERPIXEL 3, BITSPERSAMPLE 8, ORIENTATION
// TOPLEFT, PLANARCONFIG CONTIG, PHOTOMETRIC RGB, COMPRESSION LZW when
// compress, ROWSPERSTRIP TIFFDefaultStripSize, one TIFFWriteScanline per row.
// No alpha, float, resolution (DPI) or predictor tags: wx writes none.

#include <QString>
#include <cstddef>

namespace CaptureWriter
{
	// rgb: 8-bit RGB pixels, top row first; row r of the image starts at
	// rgb + r * row_pitch (bytes).
	bool WriteRGB8Tiff(const QString& path, const unsigned char* rgb,
		size_t w, size_t h, size_t row_pitch, bool compress, bool big_tiff = false);

	// wx tiled path: BigTIFF ("wb8") when w*h exceeds 3758096384 pixels
	inline bool NeedsBigTiff(size_t w, size_t h) { return w * h > 3758096384ULL; }
}

#endif
