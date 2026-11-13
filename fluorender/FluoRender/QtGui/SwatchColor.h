#ifndef VVD_QT_SWATCH_COLOR_H
#define VVD_QT_SWATCH_COLOR_H

#include "FLIVR/Color.h"
#include <algorithm>

// The colour of a volume's or mesh's swatch (its bulb in Workspace, its
// square in the legend of the view): the data colour at full brightness,
// its largest component at 1, hue and saturation kept; a grey or black one
// is white. Not in wx, where a dim channel colour (low luminance) gave a
// dark swatch, hard to tell apart.
inline FLIVR::Color VVDSwatchColor(const FLIVR::Color& c)
{
	const double m = std::max(c.r(), std::max(c.g(), c.b()));
	if (!(m > 0.0))
		return FLIVR::Color(1.0, 1.0, 1.0);
	auto u = [m](double v) { return std::min(1.0, std::max(0.0, v / m)); };
	return FLIVR::Color(u(c.r()), u(c.g()), u(c.b()));
}

#endif
