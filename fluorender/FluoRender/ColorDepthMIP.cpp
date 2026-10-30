// ColorDepthMIP: native port of the single-channel pipeline of the Fiji macro
// Color_Depth_MIP_batch_0308_2021.ijm (Hideo Otsuna, HHMI Janelia) and of
// every plugin / ImageJ 1.54p built-in it runs. See ColorDepthMIP.h and
// docs/QT_PORT.md ("Color Depth MIP").
//
// Every step follows the installed Java code (javap -c of the classes Fiji
// actually loads) in evaluation order and arithmetic type (int / float /
// double), so the image comes out identical to the macro's. Comments
// "macro NNNN" refer to lines of Color_Depth_MIP_batch_0308_2021.ijm.

// Java never contracts a*b+c into a fused multiply-add; neither may we.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#if defined(_MSC_VER)
#pragma fp_contract(off)
#endif

#include "ColorDepthMIP.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <tiffio.h>
#include <zlib.h>

#ifndef CDM_STANDALONE
#include "DataManager.h"
#include "Core/FileUtil.h"
#endif

namespace cdm
{

// ===========================================================================
// Java arithmetic helpers
// ===========================================================================

// Java (int) cast of a double / float: NaN -> 0, saturating.
static inline int jd2i(double v)
{
	if (v != v) return 0;
	if (v >= 2147483647.0) return INT_MAX;
	if (v <= -2147483648.0) return INT_MIN;
	return (int)v;
}
static inline int jf2i(float v) { return jd2i((double)v); }

// Java Math.round(double) (long result; values here fit in int)
static inline long long jround(double a)
{
	if (a != a) return 0;
	if (a == 0.49999999999999994) return 0;
	double r = std::floor(a + 0.5);
	if (r >= 9.2233720368547758e18) return LLONG_MAX;
	if (r <= -9.2233720368547758e18) return LLONG_MIN;
	return (long long)r;
}
// Java Math.round(float) (JDK 8)
static inline int jroundf(float a)
{
	if (a != a) return 0;
	if (a == 0x1.fffffep-2f) return 0;
	return jd2i(std::floor((double)(a + 0.5f)));
}
// macro round(): Math.floor(x + 0.5)
static inline double mround(double x) { return std::floor(x + 0.5); }

static inline double u2d(uint64_t u) { double d; std::memcpy(&d, &u, 8); return d; }

// Exact Math.sin / Math.cos / Math.exp results of Fiji's JVM (Zulu 8u322,
// x86_64 under Rosetta) for the fixed arguments of DSLT3D_LINE2_Multi
// (sin/cos(k*PI/16)), DSLT_ (cos/sin(toRadians(d*11.25))) and gaussian1D(r)
// (the normalised filters), dumped with Double.doubleToRawLongBits.
#include "ColorDepthMIPTables.inc"

// ===========================================================================
// threads
// ===========================================================================

// per calling thread, so that concurrent Compute calls do not share them
static thread_local int g_threads = 0;
static int NThreads()
{
	if (g_threads > 0) return g_threads;
	unsigned n = std::thread::hardware_concurrency();
	return n ? (int)n : 4;
}

// f(begin, end) on [0, n) split into contiguous chunks
template <class F> static void ParallelFor(int n, F f)
{
	int nt = std::min(NThreads(), std::max(1, n));
	if (nt <= 1 || n < 64) { f(0, n); return; }
	std::vector<std::thread> th;
	std::atomic<int> next(0);
	const int chunk = std::max(1, n / (nt * 8));
	for (int t = 0; t < nt; t++)
		th.emplace_back([&]() {
			for (;;) {
				int b = next.fetch_add(chunk);
				if (b >= n) break;
				f(b, std::min(n, b + chunk));
			}
		});
	for (auto& t : th) t.join();
}

// ===========================================================================
// images (ImageJ processors) and the macro's log
// ===========================================================================

struct Img16	// ShortProcessor (one slice) or a 16-bit stack
{
	int w = 0, h = 0, n = 1;
	std::vector<uint16_t> p;
	int dmin = 0, dmax = 0;		// display range (ints, as ShortProcessor)
	uint16_t* s(int z) { return p.data() + (size_t)z * w * h; }
	const uint16_t* s(int z) const { return p.data() + (size_t)z * w * h; }
	size_t plane() const { return (size_t)w * h; }
};
struct Img8	// ByteProcessor (one slice) or an 8-bit stack
{
	int w = 0, h = 0, n = 1;
	std::vector<uint8_t> p;
	int dmin = 0, dmax = 255;
	uint8_t* s(int z) { return p.data() + (size_t)z * w * h; }
	const uint8_t* s(int z) const { return p.data() + (size_t)z * w * h; }
	size_t plane() const { return (size_t)w * h; }
};
struct ImgRGB	// ColorProcessor, 0x00RRGGBB
{
	int w = 0, h = 0, n = 1;
	std::vector<uint32_t> p;
	uint32_t* s(int z) { return p.data() + (size_t)z * w * h; }
	const uint32_t* s(int z) const { return p.data() + (size_t)z * w * h; }
};

// ImageJ macro number to string (Interpreter.toString: IJ.d2s(x,0) for
// integers, else IJ.d2s(x, 4, 9) without trailing zeros); log text only
static std::string n2s(double x)
{
	char buf[64];
	if (x != x) return "NaN";
	if (x == INFINITY) return "Infinity";
	if (x == -INFINITY) return "-Infinity";
	if ((double)jd2i(x) == x)
	{
		snprintf(buf, sizeof(buf), "%d", jd2i(x));
		return buf;
	}
	double log10 = std::log10(std::fabs(x));
	double roundErrorAtMax = 0.223 * std::pow(10, -9);
	int magnitude = (int)std::ceil(log10 + roundErrorAtMax);
	int decimals = x == 0 ? 0 : 9 - magnitude;
	if (decimals < 0 || magnitude < 4 + 1 - 9)
		snprintf(buf, sizeof(buf), "%.3E", x);
	else
	{
		if (decimals > 4) decimals = std::max(4, decimals - 9 + 4);
		snprintf(buf, sizeof(buf), "%.*f", decimals, x);
	}
	std::string s(buf);
	if (s.find('.') != std::string::npos && s.find('E') == std::string::npos)
	{
		while (!s.empty() && s.back() == '0') s.pop_back();
		if (!s.empty() && s.back() == '.') s.pop_back();
	}
	return s;
}

struct Log
{
	std::string text;
	bool echo = false;
	void operator()(const std::string& s)
	{
		text += s;
		text += '\n';
		if (echo) { fprintf(stderr, "[cdm] %s\n", s.c_str()); fflush(stderr); }
	}
};

// ---- debug dumps (VVD_CDM_DUMP=<dir>): same tags as the Fiji harness ----
static thread_local std::string g_dump_dir;
static void DumpRaw(const std::string& tag, const void* data, size_t bytes, const std::string& hdr)
{
	if (g_dump_dir.empty()) return;
	std::string path = g_dump_dir + "/" + tag;
	if (FILE* f = fopen((path + ".raw").c_str(), "wb")) { fwrite(data, 1, bytes, f); fclose(f); }
	if (FILE* f = fopen((path + ".txt").c_str(), "w")) { fprintf(f, "%s\n", hdr.c_str()); fclose(f); }
}
static void Dump(const std::string& tag, const Img16& im)
{ DumpRaw(tag, im.p.data(), im.p.size() * 2, "c++|16|" + std::to_string(im.w) + "|" + std::to_string(im.h) + "|" + std::to_string(im.n) + "|" + std::to_string(im.dmin) + "|" + std::to_string(im.dmax)); }
static void Dump(const std::string& tag, const Img8& im)
{ DumpRaw(tag, im.p.data(), im.p.size(), "c++|8|" + std::to_string(im.w) + "|" + std::to_string(im.h) + "|" + std::to_string(im.n) + "|" + std::to_string(im.dmin) + "|" + std::to_string(im.dmax)); }
static void Dump(const std::string& tag, const ImgRGB& im)
{ DumpRaw(tag, im.p.data(), im.p.size() * 4, "c++|24|" + std::to_string(im.w) + "|" + std::to_string(im.h) + "|" + std::to_string(im.n) + "|0|255"); }

// ---- display range (ShortProcessor / ByteProcessor) ----
static void ResetMinAndMax(Img16& im)		// findMinAndMax over the current slice
{
	const uint16_t* q = im.s(0);
	size_t n = im.plane();
	int mn = q[0], mx = q[0];
	for (size_t i = 1; i < n; i++) { int v = q[i]; if (v < mn) mn = v; else if (v > mx) mx = v; }
	im.dmin = mn; im.dmax = mx;
}
static void SetMinAndMax(Img16& im, double mn, double mx)
{
	if (mn == 0.0 && mx == 0.0) { ResetMinAndMax(im); return; }
	im.dmin = (int)jround(mn);
	im.dmax = (int)jround(mx);
}
static void SetMinAndMax(Img8& im, double mn, double mx)
{
	if (mx < mn) return;
	im.dmin = (int)jround(mn);
	im.dmax = (int)jround(mx);
}

// ---- Z Project (ZProjector, Max Intensity); start/stop as setStart/StopSlice ----
static void ZRange(int nz, int start, int stop, int& s0, int& s1)
{
	s0 = 1; s1 = nz;
	if (start >= 1 && start <= nz) s0 = start;
	if (stop >= 1 && stop <= nz) s1 = stop;
}
static Img16 ZProjectMax(const Img16& st, int start = 0, int stop = 0)
{
	int s0, s1;
	ZRange(st.n, start, stop, s0, s1);
	Img16 out; out.w = st.w; out.h = st.h; out.n = 1;
	size_t n = st.plane();
	out.p.assign(n, 0);
	if (s0 <= s1)
	{
		ParallelFor((int)st.h, [&](int y0, int y1) {
			for (int z = s0 - 1; z <= s1 - 1; z++)
			{
				const uint16_t* q = st.s(z);
				for (size_t i = (size_t)y0 * st.w; i < (size_t)y1 * st.w; i++)
					if (q[i] > out.p[i]) out.p[i] = q[i];
			}
		});
	}
	ResetMinAndMax(out);
	return out;
}
static Img8 ZProjectMax(const Img8& st, int start = 0, int stop = 0)
{
	int s0, s1;
	ZRange(st.n, start, stop, s0, s1);
	Img8 out; out.w = st.w; out.h = st.h; out.n = 1;
	size_t n = st.plane();
	out.p.assign(n, 0);
	if (s0 <= s1)
		for (int z = s0 - 1; z <= s1 - 1; z++)
		{
			const uint8_t* q = st.s(z);
			for (size_t i = 0; i < n; i++)
				if (q[i] > out.p[i]) out.p[i] = q[i];
		}
	out.dmin = 0; out.dmax = 255;	// ByteProcessor.resetMinAndMax
	return out;
}

// ---- Enhance Contrast "saturated=0.3" (ContrastEnhancer.stretchHistogram) ----
static void HistMinMax(const std::vector<long long>& hist, long long pixelCount, double saturated, int& hmin, int& hmax)
{
	int hsize = (int)hist.size();
	int threshold = saturated > 0.0 ? jd2i(pixelCount * saturated / 200.0) : 0;
	int i = -1; bool found = false; long long count = 0; int maxindex = hsize - 1;
	do { i++; count += hist[i]; found = count > threshold; } while (!found && i < maxindex);
	hmin = i;
	i = hsize; count = 0;
	do { i--; count += hist[i]; found = count > threshold; } while (!found && i > 0);
	hmax = i;
}
static void EnhanceContrast(Img16& im, double saturated)
{
	std::vector<long long> hist(65536, 0);
	const uint16_t* q = im.s(0);
	size_t n = im.plane();
	for (size_t i = 0; i < n; i++) hist[q[i]]++;
	int hmin, hmax;
	HistMinMax(hist, (long long)n, saturated, hmin, hmax);
	if (hmax > hmin) SetMinAndMax(im, hmin, hmax);
}
static void EnhanceContrast(Img8& im, double saturated)
{
	std::vector<long long> hist(256, 0);
	const uint8_t* q = im.s(0);
	size_t n = im.plane();
	for (size_t i = 0; i < n; i++) hist[q[i]]++;
	int hmin, hmax;
	HistMinMax(hist, (long long)n, saturated, hmin, hmax);
	if (hmax > hmin) SetMinAndMax(im, hmin, hmax);
}

// ---- Apply LUT (LutApplier) on every slice ----
static void ApplyLut(Img16& im)
{
	int mn = im.dmin, mx = im.dmax;
	std::vector<uint16_t> table(65536);
	const int range = 65536;
	for (int i = 0; i < 65536; i++)
	{
		if (i <= mn) table[i] = 0;
		else if (i >= mx) table[i] = (uint16_t)(range - 1);
		else table[i] = (uint16_t)jd2i(((double)(i - mn) / (mx - mn)) * range);
	}
	size_t total = im.p.size();
	ParallelFor((int)((total + 65535) / 65536), [&](int b, int e) {
		for (size_t i = (size_t)b * 65536; i < std::min(total, (size_t)e * 65536); i++) im.p[i] = table[im.p[i]];
	});
	im.dmin = 0; im.dmax = range - 1;
}
static void ApplyLut(Img8& im)
{
	int mn = im.dmin, mx = im.dmax;
	uint8_t table[256];
	for (int i = 0; i < 256; i++)
	{
		if (i <= mn) table[i] = 0;
		else if (i >= mx) table[i] = 255;
		else table[i] = (uint8_t)jd2i(((double)(i - mn) / (mx - mn)) * 256);
	}
	for (auto& v : im.p) v = table[v];
	im.dmin = 0; im.dmax = 255;
}

// ---- type conversions (TypeConverter / StackConverter) ----
static Img8 To8(const Img16& im)	// "8-bit" with the display range
{
	Img8 out; out.w = im.w; out.h = im.h; out.n = im.n;
	out.p.resize(im.p.size());
	int mn = im.dmin, mx = im.dmax;
	double scale = 256.0 / (mx - mn + 1);
	uint8_t table[65536];
	for (int v = 0; v < 65536; v++)
	{
		int value = v - mn;
		if (value < 0) value = 0;
		value = jd2i(value * scale + 0.5);
		if (value > 255) value = 255;
		table[v] = (uint8_t)value;
	}
	size_t total = im.p.size();
	ParallelFor((int)((total + 65535) / 65536), [&](int b, int e) {
		for (size_t i = (size_t)b * 65536; i < std::min(total, (size_t)e * 65536); i++) out.p[i] = table[im.p[i]];
	});
	out.dmin = 0; out.dmax = 255;
	return out;
}
static Img16 To16(const Img8& im)	// "16-bit" (no scaling)
{
	Img16 out; out.w = im.w; out.h = im.h; out.n = im.n;
	out.p.assign(im.p.begin(), im.p.end());
	ResetMinAndMax(out);
	return out;
}

// ---- auto threshold (ImageProcessor.setAutoThreshold(method+" dark")) ----
enum class ThMethod { Triangle, Moments, Mean };

static int Bilevel(const std::vector<int>& data)
{
	int firstNonZero = -1, secondNonZero = -1, nonZeroCount = 0;
	for (int i = 0; i < (int)data.size(); i++)
		if (data[i] > 0)
		{
			nonZeroCount++;
			if (nonZeroCount > 2) return -1;
			if (firstNonZero == -1) firstNonZero = i; else secondNonZero = i;
		}
	if (nonZeroCount == 1) return firstNonZero - 1;
	if (nonZeroCount == 2) return secondNonZero - 1;
	return -1;
}
static int TriangleTh(std::vector<int> data)
{
	int len = (int)data.size();
	int min = 0, dmax = 0, max = 0, min2 = 0;
	for (int i = 0; i < len; i++) if (data[i] > 0) { min = i; break; }
	if (min > 0) min--;
	for (int i = len - 1; i > 0; i--) if (data[i] > 0) { min2 = i; break; }
	if (min2 < len - 1) min2++;
	for (int i = 0; i < len; i++) if (data[i] > dmax) { max = i; dmax = data[i]; }
	bool inverted = false;
	if ((max - min) < (min2 - max))
	{
		inverted = true;
		std::reverse(data.begin(), data.end());
		min = len - 1 - min2;
		max = len - 1 - max;
	}
	if (min == max) return min;
	double nx, ny, d;
	nx = data[max];
	ny = min - max;
	d = std::sqrt(nx * nx + ny * ny);
	nx /= d;
	ny /= d;
	d = nx * min + ny * data[min];
	int split = min;
	double splitDistance = 0;
	for (int i = min + 1; i <= max; i++)
	{
		double newDistance = nx * i + ny * data[i] - d;
		if (newDistance > splitDistance) { split = i; splitDistance = newDistance; }
	}
	split--;
	if (inverted) return len - 1 - split;
	return split;
}
static int MomentsTh(const std::vector<int>& data)
{
	int len = (int)data.size();
	double total = 0;
	double m0 = 1.0, m1 = 0.0, m2 = 0.0, m3 = 0.0, sum = 0.0, p0 = 0.0;
	double cd, c0, c1, z0, z1;
	int threshold = -1;
	std::vector<double> histo(len);
	for (int i = 0; i < len; i++) total += data[i];
	for (int i = 0; i < len; i++) histo[i] = (double)(data[i] / total);
	for (int i = 0; i < len; i++)
	{
		m1 += i * histo[i];
		m2 += i * i * histo[i];
		m3 += i * i * i * histo[i];
	}
	cd = m0 * m2 - m1 * m1;
	c0 = (-m2 * m2 + m1 * m3) / cd;
	c1 = (m0 * -m3 + m2 * m1) / cd;
	z0 = 0.5 * (-c1 - std::sqrt(c1 * c1 - 4.0 * c0));
	z1 = 0.5 * (-c1 + std::sqrt(c1 * c1 - 4.0 * c0));
	p0 = (z1 - m1) / (z1 - z0);
	sum = 0;
	for (int i = 0; i < len; i++)
	{
		sum += histo[i];
		if (sum > p0) { threshold = i; break; }
	}
	return threshold;
}
static int MeanTh(const std::vector<int>& data)
{
	long long tot = 0, sum = 0;
	for (int i = 0; i < (int)data.size(); i++) { tot += data[i]; sum += ((long long)i * (long long)data[i]); }
	if (tot == 0) return 0;
	return (int)(sum / tot);
}
static int GetThreshold(ThMethod m, const std::vector<int>& hist)
{
	int t = Bilevel(hist);
	if (t >= 0) return t;
	switch (m)
	{
	case ThMethod::Triangle: t = TriangleTh(hist); break;
	case ThMethod::Moments: t = MomentsTh(hist); break;
	case ThMethod::Mean: t = MeanTh(hist); break;
	}
	if (t == -1) t = 0;
	return t;
}
// setAutoThreshold("<m> dark") on a 16-bit image (resets its display range,
// as ImageJ does), returns the raw threshold [lower, upper] it sets.
static void AutoThreshold16(Img16& im, ThMethod m, double& lower, double& upper)
{
	ResetMinAndMax(im);
	int mn = im.dmin, mx = im.dmax;
	double scale = 256.0 / (mx - mn + 1);
	std::vector<int> hist(256, 0);
	const uint16_t* q = im.s(0);
	size_t n = im.plane();
	for (size_t i = 0; i < n; i++)
	{
		int value = q[i] - mn;
		if (value < 0) value = 0;
		value = jd2i(value * scale + 0.5);
		if (value > 255) value = 255;
		hist[value]++;
	}
	int threshold = GetThreshold(m, hist);
	double lo = threshold + 1, up = 255.0;
	if (lo > 255) lo = 255;
	// scaleAndSetThreshold
	double dmin = mn, dmax = mx;
	if (dmax > dmin)
	{
		lo = dmin + (lo / 255.0) * (dmax - dmin);
		up = 65535;
	}
	else lo = up = dmin;
	// ShortProcessor.setThreshold
	if (lo < 0.0) lo = 0.0;
	if (up > 65535.0) up = 65535.0;
	lower = (double)jround(lo);
	upper = (double)jround(up);
}
// "Convert to Mask" of a thresholded 16-bit image: 255 where lower<=v<=upper
static Img8 ConvertToMask16(const Img16& im, double lower, double upper)
{
	float t1 = (float)lower, t2 = (float)upper;
	Img8 out; out.w = im.w; out.h = im.h; out.n = 1;
	out.p.resize(im.plane());
	const uint16_t* q = im.s(0);
	for (size_t i = 0; i < out.p.size(); i++)
	{
		float v = (float)q[i];
		out.p[i] = (v >= t1 && v <= t2) ? 255 : 0;
	}
	return out;
}
// "Create Selection" on a binary mask + getStatistics(area, mean) and the
// macro's "if(area!=totalpix && mean<200) Make Inverse" (Make Inverse of no
// selection = Select All)
static void SelectionStats(const Img8& m, long long totalpix, double& area, double& mean)
{
	long long cnt = 0;
	for (uint8_t v : m.p) if (v == 255) cnt++;
	long long all = (long long)m.w * m.h;
	if (cnt > 0) { area = (double)cnt; mean = 255.0; }
	else { area = (double)all; mean = 0.0; }	// no selection: whole image (all 0)
	if (area != (double)totalpix && mean < 200)
	{
		if (cnt > 0) { area = (double)(all - cnt); mean = 0.0; }
		else { area = (double)all; mean = 0.0; }
	}
}

// ---- Canvas Size (CanvasResizer, zero fill) ----
enum CanvasPos { TopLeft = 0, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight };
static void CanvasOffsets(int wOld, int hOld, int wNew, int hNew, int pos, int& xOff, int& yOff)
{
	int xC = (wNew - wOld) / 2, xR = (wNew - wOld), yC = (hNew - hOld) / 2, yB = (hNew - hOld);
	switch (pos)
	{
	case TopLeft: xOff = 0; yOff = 0; break;
	case TopCenter: xOff = xC; yOff = 0; break;
	case TopRight: xOff = xR; yOff = 0; break;
	case CenterLeft: xOff = 0; yOff = yC; break;
	case CenterRight: xOff = xR; yOff = yC; break;
	case BottomLeft: xOff = 0; yOff = yB; break;
	case BottomCenter: xOff = xC; yOff = yB; break;
	case BottomRight: xOff = xR; yOff = yB; break;
	default: xOff = xC; yOff = yC; break;
	}
}
template <class IM> static IM CanvasSize(const IM& im, int wNew, int hNew, int pos)
{
	int xOff, yOff;
	CanvasOffsets(im.w, im.h, wNew, hNew, pos, xOff, yOff);
	IM out = im;
	out.w = wNew; out.h = hNew; out.n = 1;
	out.p.assign((size_t)wNew * hNew, 0);
	for (int y = 0; y < hNew; y++)
	{
		int ys = y - yOff;
		if (ys < 0 || ys >= im.h) continue;
		for (int x = 0; x < wNew; x++)
		{
			int xs = x - xOff;
			if (xs < 0 || xs >= im.w) continue;
			out.p[(size_t)y * wNew + x] = im.p[(size_t)ys * im.w + xs];
		}
	}
	return out;
}

// ---- Fill (rectangle / polygon ROI) ----
// makeRectangle(x, y, w, h) with doubles: Roi((int)x, (int)y, ceil(w), ceil(h))
static void RectRoi(double x, double y, double w, double h, int& rx, int& ry, int& rw, int& rh)
{
	rx = jd2i(x); ry = jd2i(y); rw = jd2i(std::ceil(w)); rh = jd2i(std::ceil(h));
}
static void FillRect(Img16& im, int rx, int ry, int rw, int rh, uint16_t value)
{
	int x0 = std::max(0, rx), y0 = std::max(0, ry), x1 = std::min(im.w, rx + rw), y1 = std::min(im.h, ry + rh);
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++) im.p[(size_t)y * im.w + x] = value;
}
// outside of the rectangle (Make Inverse of a rectangle + Fill)
static void FillOutsideRect(Img16& im, int rx, int ry, int rw, int rh, uint16_t value)
{
	for (int y = 0; y < im.h; y++)
		for (int x = 0; x < im.w; x++)
			if (!(x >= rx && x < rx + rw && y >= ry && y < ry + rh)) im.p[(size_t)y * im.w + x] = value;
}
// PolygonRoi(int polygon) mask (PolygonFiller, integer coordinates) + Fill
static void FillPolygon(Img16& im, const std::vector<int>& px, const std::vector<int>& py, uint16_t value)
{
	int n = (int)px.size();
	int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
	for (int i = 0; i < n; i++)
	{
		minx = std::min(minx, px[i]); maxx = std::max(maxx, px[i]);
		miny = std::min(miny, py[i]); maxy = std::max(maxy, py[i]);
	}
	int bw = maxx - minx, bh = maxy - miny;	// java.awt.Polygon.getBounds
	std::vector<int> x(n), y(n);
	for (int i = 0; i < n; i++) { x[i] = px[i] - minx; y[i] = py[i] - miny; }
	// buildEdgeTable
	std::vector<double> ex(n), eslope(n);
	std::vector<int> ey1(n), ey2(n), aedge(n + 1);
	int yMin = INT_MAX, yMax = INT_MIN, edges = 0;
	for (int i = 0; i < n; i++)
	{
		int iplus1 = i == n - 1 ? 0 : i + 1;
		int y1 = y[i], y2 = y[iplus1], x1 = x[i], x2 = x[iplus1];
		if (y1 == y2) continue;
		if (y1 > y2) { std::swap(y1, y2); std::swap(x1, x2); }
		double slope = (double)(x2 - x1) / (y2 - y1);
		ex[edges] = x1 + 0.5 * slope + 1e-8;
		ey1[edges] = y1; ey2[edges] = y2; eslope[edges] = slope;
		if (y1 < yMin) yMin = y1;
		if (y2 > yMax) yMax = y2;
		edges++;
	}
	int activeEdges = 0;
	auto sortActive = [&]() {
		for (int i = 0; i < activeEdges; i++)
		{
			int mi = i;
			for (int j = i; j < activeEdges; j++) if (ex[aedge[j]] < ex[aedge[mi]]) mi = j;
			std::swap(aedge[mi], aedge[i]);
		}
	};
	std::vector<uint8_t> mask((size_t)bw * bh, 0);
	int yStart = yMin > 0 ? yMin : 0;
	if (yMin != 0)
	{
		for (int i = 0; i < edges; i++)
			if (ey1[i] < yStart && ey2[i] >= yStart) { ex[i] += eslope[i] * (yStart - ey1[i]); aedge[activeEdges++] = i; }
		sortActive();
	}
	for (int yy = yStart; yy < std::min(bh, yMax + 1); yy++)
	{
		// removeInactiveEdges
		int i = 0;
		while (i < activeEdges)
		{
			int idx = aedge[i];
			if (yy < ey1[idx] || yy >= ey2[idx]) { for (int j = i; j < activeEdges - 1; j++) aedge[j] = aedge[j + 1]; activeEdges--; }
			else i++;
		}
		// activateEdges
		for (int e = 0; e < edges; e++)
			if (yy == ey1[e])
			{
				int index = 0;
				while (index < activeEdges && ex[e] > ex[aedge[index]]) index++;
				for (int j = activeEdges - 1; j >= index; j--) aedge[j + 1] = aedge[j];
				aedge[index] = e;
				activeEdges++;
			}
		for (int k = 0; k + 1 < activeEdges; k += 2)
		{
			int x1 = jd2i(ex[aedge[k]] + 0.5); if (x1 < 0) x1 = 0; if (x1 > bw) x1 = bw;
			int x2 = jd2i(ex[aedge[k + 1]] + 0.5); if (x2 < 0) x2 = 0; if (x2 > bw) x2 = bw;
			for (int xx = x1; xx < x2; xx++) mask[(size_t)yy * bw + xx] = 255;
		}
		// updateXCoordinates
		double xa = -1.7976931348623157e308; bool sorted = true;
		for (int k = 0; k < activeEdges; k++)
		{
			int idx = aedge[k];
			double xb = ex[idx] + eslope[idx];
			ex[idx] = xb;
			if (xb < xa) sorted = false;
			xa = xb;
		}
		if (!sorted) sortActive();
	}
	for (int yy = 0; yy < bh; yy++)
		for (int xx = 0; xx < bw; xx++)
			if (mask[(size_t)yy * bw + xx])
			{
				int X = xx + minx, Y = yy + miny;
				if (X >= 0 && X < im.w && Y >= 0 && Y < im.h) im.p[(size_t)Y * im.w + X] = value;
			}
}

// ---- imageCalculator (ShortBlitter, in place, then resetMinAndMax) ----
enum class Calc { Subtract, Min, Max };
static void ImageCalculator(Img16& a, const Img16& b, Calc op)
{
	int w = std::min(a.w, b.w), h = std::min(a.h, b.h);
	for (int y = 0; y < h; y++)
	{
		uint16_t* d = a.p.data() + (size_t)y * a.w;
		const uint16_t* s = b.p.data() + (size_t)y * b.w;
		for (int x = 0; x < w; x++)
		{
			int dv = d[x], sv = s[x];
			switch (op)
			{
			case Calc::Subtract: dv = dv - sv; if (dv < 0) dv = 0; break;
			case Calc::Min: if (sv < dv) dv = sv; break;
			case Calc::Max: if (sv > dv) dv = sv; break;
			}
			d[x] = (uint16_t)dv;
		}
	}
	ResetMinAndMax(a);
}

// ---- Remove Outliers (RankFilters OUTLIERS, radius=1 -> 3x3, dark, threshold 90) ----
static void RemoveDarkOutliers(Img8& im, float threshold)
{
	int w = im.w, h = im.h;
	std::vector<uint8_t> src = im.p;
	ParallelFor(h, [&](int y0, int y1) {
		float v9[9];
		for (int y = y0; y < y1; y++)
			for (int x = 0; x < w; x++)
			{
				int k = 0; float mx = -3.4028234663852886e38f;
				for (int dy = -1; dy <= 1; dy++)
				{
					int yy = std::min(h - 1, std::max(0, y + dy));
					for (int dx = -1; dx <= 1; dx++)
					{
						int xx = std::min(w - 1, std::max(0, x + dx));
						float v = src[(size_t)yy * w + xx];
						v9[k++] = v;
						if (mx < v) mx = v;
					}
				}
				float v = src[(size_t)y * w + x];
				if (v + threshold < mx)
				{
					std::nth_element(v9, v9 + 4, v9 + 9);
					float median = v9[4];
					if (v + threshold < median) v = median;
				}
				im.p[(size_t)y * w + x] = (uint8_t)(jf2i(v + 0.5f) & 0xff);
			}
	});
}

// ===========================================================================
// the user's plugins
// ===========================================================================

// Max_value: largest pixel of the stack
static int MaxValue(const Img16& st)
{
	int mx = 0;
	for (uint16_t v : st.p) if (v > mx) mx = v;
	return mx;
}
static int MaxValue(const Img8& st)
{
	int mx = 0;
	for (uint8_t v : st.p) if (v > mx) mx = v;
	return mx;
}

// Mask255_to_4095 (installed class): >200 -> 65535, then resetMinAndMax
static void Mask255To4095(Img16& im)
{
	for (auto& v : im.p) if (v > 200) v = 65535;
	ResetMinAndMax(im);
}

// A4095_normalizer "subtraction=0 start=1 end=1" (in place)
static void A4095Normalizer(Img16& im)
{
	double maxval = 0;
	for (uint16_t v : im.p) if (v > maxval) maxval = v;
	double divideval = maxval / 4096;
	for (auto& v : im.p)
	{
		double pix1 = v;
		double pix1sub = pix1 / divideval;
		int finalpix = jd2i(pix1sub) - 0;
		if (finalpix < 0) finalpix = 0;
		if (finalpix > 4095) finalpix = 4095;
		v = (uint16_t)finalpix;
	}
}

// Three_D_Ave (installed class): sum of the non-zero pixels / (w*h*n),
// BigDecimal HALF_UP to 2 decimals; the macro reads parseFloat(title) and
// rounds it: returns round(parseFloat(title)) and the title text
static double ThreeDAve(const Img16& im, std::string* title = nullptr)
{
	unsigned long long sum = 0;
	for (uint16_t v : im.p) if (v != 0) sum += v;
	unsigned long long total = (unsigned long long)im.w * im.h * im.n;
	unsigned long long k = 0;	// round(sum*100/total) half up
	if (sum != 0) k = (2 * sum * 100 + total) / (2 * total);
	if (title)
	{
		char buf[64]; snprintf(buf, sizeof(buf), "%llu.%02llu", k / 100, k % 100);
		*title = buf;
	}
	double v = (double)k / 100.0;	// Double.parseDouble of "k/100"
	return mround(v);
}

// Mask_MIP_Brightness_Measure: mean of the data inside mask>200 (zeros
// counted), BigDecimal HALF_UP 3 decimals. Returns false for "No signal".
static bool MaskMIPBrightness(const Img16& mask, const Img16& data, double& mean)
{
	int maxvalue = 0;
	size_t n = (size_t)mask.w * mask.h;
	for (size_t i = 0; i < n; i++) if (data.p[i] > maxvalue) maxvalue = data.p[i];
	unsigned long long cnt = 0, sum = 0;
	for (size_t i = 0; i < n; i++)
		if (mask.p[i] > 200) { cnt++; if (data.p[i] > 0) sum += data.p[i]; }
	if (maxvalue <= 0) return false;
	if (cnt == 0) { mean = NAN; return true; }	// BigDecimal division by zero (not reached with real masks)
	unsigned long long k = (2 * sum * 1000 + cnt) / (2 * cnt);
	mean = (double)k / 1000.0;
	return true;
}

// Mask_Brightness_Measure (the INSTALLED class, which differs from the
// source in Plugins Sorce): returns the title it gives the data image.
// mask: test.tif (16-bit 0/65535), data: MIP.tif
static std::string MaskBrightnessMeasure(const Img16& mask, const Img16& data, int desiremean1, Log& log)
{
	size_t sumpx = (size_t)mask.w * mask.h;
	double maxvalue = 0;
	for (size_t i = 0; i < sumpx; i++) if (maxvalue < data.p[i]) maxvalue = data.p[i];
	double desiremean = (double)desiremean1;
	// the mask is 16-bit (GRAY16)
	if (maxvalue < 4096) { desiremean = desiremean * 16.0; log("12 bit"); }
	else { desiremean = desiremean * 16.0 * 16.0; log("16 bit"); }
	unsigned long long sumVX = 0, value1 = 0;
	for (size_t n = 0; n < sumpx; n++)
		if (mask.p[n] > 200) { sumVX++; if (data.p[n] > 0) value1 += data.p[n]; }
	auto div3 = [](unsigned long long a, unsigned long long b) {	// BigDecimal a/b, 3 decimals, HALF_UP
		unsigned long long k = (2 * a * 1000 + b) / (2 * b);
		return (double)k / 1000.0;
	};
	if (!(maxvalue > 0)) { log("No signal"); return "0"; }
	if (sumVX == 0) return "MIP.tif";	// ArithmeticException in Java: title unchanged
	double mean3 = div3(value1, sumVX);
	std::string title = "MIP.tif";
	if (mean3 >= desiremean)
	{
		char buf[64]; snprintf(buf, sizeof(buf), "%.1f", maxvalue);	// String.valueOf(maxvalue) of an integer value
		title = buf;
	}
	double totalmax = 0;
	double prev = -1;
	while (mean3 < desiremean)
	{
		double gap = desiremean - mean3;
		double maxvalue2 = 2.0;
		if (gap > 10000.0) maxvalue2 = 1000.0;
		else if (gap > 1000.0) maxvalue2 = 500.0;
		else if (gap > 100.0) maxvalue2 = 30.0;
		maxvalue2 = (double)jround(maxvalue2);
		totalmax = totalmax + maxvalue2;
		unsigned long long v1 = 0;
		for (size_t n = 0; n < sumpx; n++)
			if (mask.p[n] > 200)
			{
				double pix3 = data.p[n];
				pix3 = pix3 + totalmax;
				if (maxvalue < 4096) { if (pix3 > 4095.0) pix3 = 4095.0; }
				else if (maxvalue > 4095) { if (pix3 > 65535.0) pix3 = 65535.0; }
				v1 += (unsigned long long)pix3;
			}
		mean3 = div3(v1, sumVX);
		char buf[64]; snprintf(buf, sizeof(buf), "%.1f", totalmax);	// String.valueOf(totalmax)
		title = buf;
		if (mean3 <= prev) break;	// Fiji loops forever here (desired mean above the clamp); stop instead
		prev = mean3;
	}
	return title;
}

// Mask_Median_Subtraction "%=<pct> subtract histogram=<avg>" (16-bit data,
// 8-bit or 16-bit mask stack)
template <class MASK> static void MaskMedianSubtraction(const MASK& mask, Img16& data, int medianPercent, int averaging, Log& log)
{
	log("averaging; " + std::to_string(averaging));
	double medianPercent2 = (double)((double)medianPercent / (double)100);
	{
		char buf[64]; snprintf(buf, sizeof(buf), "%s", n2s(medianPercent2).c_str());
		log(std::string("medianPercent2; ") + buf);
	}
	int Fnslice = mask.n, Fidatanslice = data.n;
	if (Fnslice != Fidatanslice)
	{
		log("Slice number is different!!  Mask; " + std::to_string(Fnslice) + "   Data; " + std::to_string(Fidatanslice));
		if (Fnslice > Fidatanslice) Fnslice = Fidatanslice;
	}
	size_t sumpx = (size_t)mask.w * mask.h;
	int MaxPix3 = 0, MinPix3 = 100000;
	for (int s = 0; s < Fnslice; s++)
	{
		auto mk = mask.s(s); const uint16_t* dt = data.s(s);
		for (size_t nn = 0; nn < sumpx; nn++)
			if (mk[nn] > 200)
			{
				float pix3 = dt[nn];
				if (pix3 > 0)
				{
					if (pix3 > MaxPix3) MaxPix3 = jroundf(pix3);
					if (pix3 < MinPix3) MinPix3 = jroundf(pix3);
				}
			}
	}
	int maxVal = 65536;
	std::vector<long long> histog(maxVal, 0);
	log("maxVal; " + std::to_string(maxVal));
	long long MaxPointSum = 0;
	int maxNum = 0, MaxPoint = 0, aveVal = 0;
	std::vector<int> MedianArray(Fnslice + 1, 0);
	for (int s = 0; s < Fnslice; s++)
	{
		auto mk = mask.s(s); const uint16_t* dt = data.s(s);
		for (size_t n = 0; n < sumpx; n++)
			if (mk[n] > 200) histog[jroundf((float)dt[n])]++;
		maxNum = 0; MaxPoint = 0; aveVal = 0;
		if (averaging < 2)
		{
			for (int i = 1; i < (int)histog.size(); i++)
			{
				int numVal = (int)histog[i];
				if (numVal > maxNum) { maxNum = numVal; MaxPoint = i; }
			}
		}
		else
		{
			for (int i = 1; i < (int)histog.size() - averaging - 1; i++)
			{
				int sumCountval = 0;
				for (int a = 0; a < averaging; a++) sumCountval = (int)histog[i + a] + sumCountval;
				aveVal = sumCountval / averaging;
				if (aveVal > maxNum)
				{
					maxNum = aveVal;
					MaxPoint = i + jroundf((float)(averaging / 2));
					if (MaxPoint == jroundf((float)(averaging / 2))) MaxPoint = i;
				}
			}
		}
		MaxPointSum = MaxPointSum + MaxPoint;
		MaxPoint = jd2i((double)MaxPoint * medianPercent2);
		MedianArray[s] = MaxPoint;	// subave
	}
	log("3Dave median; " + std::to_string(MaxPoint) + "  but it will subtract larger number of median in each slice");
	for (int s = 0; s < Fnslice; s++)
	{
		MaxPoint = (int)(MaxPointSum / (long long)Fnslice);
		MaxPoint = jd2i((double)MaxPoint * medianPercent2);
		if (MedianArray[s] > MaxPoint) MaxPoint = MedianArray[s];
		double SliceGapValue = MaxPix3 - MaxPoint;
		uint16_t* dt = data.s(s);
		const int MP = MaxPoint, MX = MaxPix3;
		for (size_t npix = 0; npix < sumpx; npix++)
		{
			float pix3 = dt[npix];
			double ratio = ((double)pix3 - (double)MP) / SliceGapValue;
			int NewPix3 = 0;
			if (pix3 >= MP) NewPix3 = (jroundf(pix3) - MP) + jd2i(MP * ratio);
			else NewPix3 = 0;
			if (NewPix3 > MX) NewPix3 = MX;
			dt[npix] = (uint16_t)NewPix3;
		}
	}
}

// Gamma 1.4 (Gamma_ and Gamma_samewindow_noswing): out = (int)(sv*pow(v/sv, 1/1.4))
static void GammaTable(int bits, std::vector<int>& table)
{
	double Dgamma = (double)jd2i(1.40 * 10.0) / 10.0;
	double sv = bits == 8 ? 255.0 : 65535.0;
	int n = (int)sv + 1;
	table.resize(n);
	for (int pix = 0; pix < n; pix++)
	{
		double out = sv * std::pow((double)pix / sv, (double)1 / Dgamma);
		if (out > sv) out = (int)sv;
		if (out < 0) out = 0;
		table[pix] = jd2i(out);
	}
}
static void Gamma(Img16& im)
{
	std::vector<int> t; GammaTable(16, t);
	for (auto& v : im.p) v = (uint16_t)t[v];
}
static void Gamma(Img8& im)
{
	std::vector<int> t; GammaTable(8, t);
	for (auto& v : im.p) v = (uint8_t)t[v];
}

// ---- Anisotropic Diffusion 2D "number=6 smoothings=7 keep=20 a1=0.50
// a2=0.90 dt=20 edge=2" (Fiji Anisotropic_Diffusion_2D 2.0.1, one 16-bit
// slice): float/double arithmetic exactly as the Java code ----
static Img16 AnisotropicDiffusion2D(const Img16& in)
{
	const int nb_iter = 6, nb_smoothings = 7;
	const double dt = 20.0;
	const float a1 = (float)0.5, a2 = (float)0.9, edgeheight = (float)2.0;
	const int width = in.w, height = in.h;
	const size_t N = (size_t)width * height;
	// arrays indexed [x][y] in Java; here x*height + y
	auto idx = [height](int x, int y) { return (size_t)x * height + y; };
	std::vector<float> ipf(N), grad0(N), grad1(N), G0(N), G1(N), G2(N), T0(N), T1(N), T2(N), veloc(N);
	for (int x = width; x-- > 0;)
		for (int y = height; y-- > 0;)
			ipf[idx(x, y)] = in.p[(size_t)y * width + x];
	double average = 0;
	float initial_max = ipf[idx(0, 0)], initial_min = ipf[idx(0, 0)], pix;
	for (int x = width; x-- > 0;)
		for (int y = height; y-- > 0;)
		{
			pix = ipf[idx(x, y)];
			if (pix > initial_max) initial_max = pix;
			if (pix < initial_min) initial_min = pix;
			average += pix;
		}
	average /= (width * height * 1);
	double stddev = 0;
	for (int x = width; x-- > 0;)
		for (int y = height; y-- > 0;)
		{
			pix = ipf[idx(x, y)];
			stddev += (pix - average) * (pix - average);
		}
	stddev = std::sqrt(stddev / (width * height * 1));
	double drange = (edgeheight * stddev) / 256.0;
	double drange0 = (6 * stddev) / 256.0;
	double drange2 = drange * drange;
	const float c1 = (float)(0.25 * (2 - std::sqrt(2.0))), c2 = (float)(0.5f * (std::sqrt(2.0) - 1));

	for (int iter = 0; iter < nb_iter; iter++)
	{
		// gradients
		ParallelFor(width, [&](int xb, int xe) {
			for (int x = xb; x < xe; x++)
			{
				int px = x - 1; if (px < 0) px = 0;
				int nx = x + 1; if (nx == width) nx--;
				for (int y = height; y-- > 0;)
				{
					int py = y - 1; if (py < 0) py = 0;
					int ny = y + 1; if (ny == height) ny--;
					float Ipp = ipf[idx(px, py)], Ipc = ipf[idx(px, y)], Ipn = ipf[idx(px, ny)];
					float Icp = ipf[idx(x, py)], Icn = ipf[idx(x, ny)];
					float Inp = ipf[idx(nx, py)], Inc = ipf[idx(nx, y)], Inn = ipf[idx(nx, ny)];
					float IppInn = c1 * (Inn - Ipp);
					float IpnInp = c1 * (Ipn - Inp);
					grad0[idx(x, y)] = (float)(IppInn - IpnInp - c2 * Ipc + c2 * Inc);
					grad1[idx(x, y)] = (float)(IppInn + IpnInp - c2 * Icp + c2 * Icn);
				}
			}
		});
		// structure tensor G and diffusion tensor T
		ParallelFor(width, [&](int xb, int xe) {
			for (int x = xb; x < xe; x++)
				for (int y = height; y-- > 0;)
				{
					size_t i = idx(x, y);
					float g0 = 0.0f, g1 = 0.0f, g2 = 0.0f;
					float fx = grad0[i], fy = grad1[i];
					g0 += fx * fx; g1 += fx * fy; g2 += fy * fy;
					G0[i] = g0; G1[i] = g1; G2[i] = g2;
					double a = g0, b = g1, c = g1, d = g2, e = a + d;
					double f = std::sqrt(e * e - 4 * (a * d - b * c));
					double l1 = 0.5 * (e - f), l2 = 0.5 * (e + f);
					if (e > 0) { if (l1 != 0) l2 = (a * d - b * c) / l1; }
					else { if (l2 != 0) l1 = (a * d - b * c) / l2; }
					float val1 = (float)(l2 / drange2);
					float val2 = (float)(l1 / drange2);
					float f1 = (a1 == .5) ? (float)(1 / std::sqrt((double)(1.0f + val1 + val2))) : (float)(std::pow((double)(1.0f + val1 + val2), (double)-a1));
					float f2 = (float)(std::pow((double)(1.0f + val1 + val2), (double)-a2));
					double u, v, nn;
					if (std::fabs(b) > std::fabs(a - l1)) { u = 1; v = (l1 - a) / b; }
					else
					{
						if (a - l1 != 0) { u = -b / (a - l1); v = 1; }
						else { u = 1; v = 0; }
					}
					nn = std::sqrt(u * u + v * v); u /= nn; v /= nn;
					float vec1 = (float)u, vec2 = (float)v;
					float vec11 = vec1 * vec1, vec12 = vec1 * vec2, vec22 = vec2 * vec2;
					T0[i] = f1 * vec11 + f2 * vec22;
					T1[i] = (f1 - f2) * vec12;
					T2[i] = f1 * vec22 + f2 * vec11;
				}
		});
		for (int sit = 0; sit < nb_smoothings; sit++)
		{
			ParallelFor(width, [&](int xb, int xe) {
				for (int x = xb; x < xe; x++)
				{
					int px = x - 1; if (px < 0) px = 0;
					int nx = x + 1; if (nx == width) nx--;
					for (int y = height; y-- > 0;)
					{
						int py = y - 1; if (py < 0) py = 0;
						int ny = y + 1; if (ny == height) ny--;
						float Ipp = ipf[idx(px, py)], Ipc = ipf[idx(px, y)], Ipn = ipf[idx(px, ny)];
						float Icp = ipf[idx(x, py)], Icc = ipf[idx(x, y)], Icn = ipf[idx(x, ny)];
						float Inp = ipf[idx(nx, py)], Inc = ipf[idx(nx, y)], Inn = ipf[idx(nx, ny)];
						float ixx = Inc + Ipc - 2 * Icc,
							iyy = Icn + Icp - 2 * Icc,
							ixy = 0.5f * (Ipp + Inn - Ipn - Inp);
						size_t i = idx(x, y);
						veloc[i] = T0[i] * ixx + T1[i] * ixy + T2[i] * iyy;
					}
				}
			});
			double xdt;
			if (dt > 0)
			{
				float max = veloc[idx(0, 0)], min = veloc[idx(0, 0)];
				for (int x = width; x-- > 0;)
					for (int y = height; y-- > 0;)
					{
						float v = veloc[idx(x, y)];
						if (v > max) max = v;
						if (v < min) min = v;
					}
				xdt = dt / std::max(std::fabs((double)max), std::fabs((double)min)) * drange0;
			}
			else xdt = -dt;
			ParallelFor(width, [&](int xb, int xe) {
				for (int x = xb; x < xe; x++)
					for (int y = height; y-- > 0;)
					{
						size_t i = idx(x, y);
						double ipfnew = ipf[i] + veloc[i] * xdt;
						ipf[i] = (float)ipfnew;
						if (ipf[i] < initial_min) ipf[i] = initial_min;
						if (ipf[i] > initial_max) ipf[i] = initial_max;
					}
			});
		}
	}
	Img16 out; out.w = width; out.h = height; out.n = 1;
	out.p.resize(N);
	for (int x = 0; x < width; x++)
		for (int y = 0; y < height; y++)
		{
			int v = jf2i(ipf[idx(x, y)]);
			if (v < 0) v = 0; if (v > 65535) v = 65535;	// ShortProcessor.putPixel
			out.p[(size_t)y * width + x] = (uint16_t)v;
		}
	out.dmin = in.dmin; out.dmax = in.dmax;	// createProcessor copies the display range
	return out;
}

// ---- DSLT noise filters (sequential, in place, exactly the Java loops) ----
// DSLT3D_LINE2_Multi.noise with noisenum 0 ("1px"; the radio button is
// ignored in macro mode, see below), no branch deletion / close gap /
// skeleton
static void DsltNoise1px(Img8& out)
{
	int width = out.w, height = out.h;
	int WWnoise = width - 3, HHnoise = height - 3;
	auto get = [&](int x, int y) { return (int)out.p[(size_t)y * width + x]; };
	for (int noisetime = 1; noisetime <= 2; noisetime++)
		for (int nn = 0; nn < WWnoise; nn++)
			for (int vvv = 0; vvv < HHnoise; vvv++)
			{
				int posi5 = 0, center1 = 0;
				for (int nnx = nn; nnx <= nn + 2; nnx++)
					for (int vvvy = vvv; vvvy <= vvv + 2; vvvy++)
					{
						int pixnnx = get(nnx, vvvy);
						if (pixnnx == 0) posi5 = posi5 + 1;
						if (pixnnx == 255 && nnx == nn + 1 && vvvy == vvv + 1) center1 = 1;
					}
				if (posi5 == 8 && center1 == 1) out.p[(size_t)(vvv + 1) * width + (nn + 1)] = 0;
			}
}
// DSLT3D_LINE2_Multi.noise with noisenum 1 ("5px")
static void DsltNoise5px(Img8& out)
{
	int width = out.w, height = out.h;
	int WWnoise = width - 5, HHnoise = height - 5;
	auto get = [&](int x, int y) { return (int)out.p[(size_t)y * width + x]; };
	auto set0 = [&](int x, int y) { out.p[(size_t)y * width + x] = 0; };
	for (int noisetime = 1; noisetime <= 2; noisetime++)
		for (int nn = 0; nn < WWnoise; nn++)
			for (int vvv = 0; vvv < HHnoise; vvv++)
			{
				int posi5 = 0;
				int pixnnx = get(nn, vvv);
				int pixnnx0 = get(nn + 1, vvv), pixnnx1 = get(nn + 2, vvv), pixnnx2 = get(nn + 3, vvv);
				int pixnnx3 = get(nn, vvv + 1), pixnnx4 = get(nn + 1, vvv + 1), pixnnx5 = get(nn + 3, vvv + 1), pixnnx6 = get(nn + 4, vvv + 1);
				int pixnnx7 = get(nn, vvv + 2), pixnnx8 = get(nn + 4, vvv + 2);
				int pixnnx9 = get(nn, vvv + 3), pixnnx10 = get(nn + 1, vvv + 3), pixnnx11 = get(nn + 3, vvv + 3), pixnnx12 = get(nn + 4, vvv + 3);
				int pixnnx13 = get(nn + 1, vvv + 4), pixnnx14 = get(nn + 2, vvv + 4), pixnnx15 = get(nn + 3, vvv + 4);
				int pixnnx20 = get(nn + 2, vvv + 1), pixnnx21 = get(nn + 1, vvv + 2), pixnnx22 = get(nn + 2, vvv + 2);
				int pixnnx23 = get(nn + 3, vvv + 2), pixnnx24 = get(nn + 2, vvv + 3);
				int background = (pixnnx0 + pixnnx1 + pixnnx2 + pixnnx3 + pixnnx4 + pixnnx5 + pixnnx6 + pixnnx7 + pixnnx8 + pixnnx9 + pixnnx10 + pixnnx11 + pixnnx12 + pixnnx13 + pixnnx14 + pixnnx15);
				int onepxback = (pixnnx + pixnnx0 + pixnnx1 + pixnnx3 + pixnnx20 + pixnnx7 + pixnnx21 + pixnnx22);
				if (background == 0)
				{
					if (pixnnx20 == 255) posi5++;
					if (pixnnx21 == 255) posi5++;
					if (pixnnx22 == 255) posi5++;
					if (pixnnx23 == 255) posi5++;
					if (pixnnx24 == 255) posi5++;
				}
				if (posi5 > 0)
				{
					set0(nn + 1, vvv + 2); set0(nn + 2, vvv + 3); set0(nn + 2, vvv + 2); set0(nn + 2, vvv + 1); set0(nn + 3, vvv + 2);
				}
				if (onepxback == 0 && pixnnx4 == 255) set0(nn + 1, vvv + 1);
			}
}
// DSLT_.noise with noiseC=1 and "less than this value is the noise" = measuresize
static void DsltNoiseC(Img8& out, int measuresize)
{
	int width = out.w;
	int sumpx = out.w * out.h;
	for (int noisetime = 1; noisetime <= 2; noisetime++)
		for (int xx = 0; xx < sumpx - (4 * width); xx += width)
			for (int i = xx; i < xx + width - 4; i++)
			{
				int pixsum = 0, background1 = 0, signal1 = 0;
				for (int ii = i; ii <= i + 4; ii++)
					for (int iv = ii; iv <= ii + (4 * width); iv += width)
					{
						int pix = out.p[iv];
						pixsum = pixsum + 1;
						if (pix >= 200)
						{
							if (pixsum < 7 || pixsum == 10 || pixsum == 11 || pixsum == 15 || pixsum == 16 || pixsum > 19) background1++;
							else signal1++;
						}
					}
				if (background1 == 0)
				{
					if (signal1 > 0 && signal1 <= measuresize)
						for (int iii = i + 1; iii <= i + 3; iii++)
							for (int ivv = iii + width; ivv <= iii + (3 * width); ivv += width) out.p[ivv] = 0;
				}
				else
				{
					pixsum = 0; background1 = 0; signal1 = 0;
					for (int ii = i; ii <= i + 2; ii++)
						for (int iv = ii; iv <= ii + (2 * width); iv += width)
						{
							int pix = out.p[iv];
							pixsum = pixsum + 1;
							if (pix >= 200)
							{
								if (pixsum < 4 || pixsum > 6) background1++;
								else signal1++;
							}
						}
					if (background1 == 0 && signal1 > 0 && signal1 <= measuresize)
						out.p[i + 1 + width] = 0;
				}
			}
}

static const double* GaussFilter(int r)	// gaussian1D(r) of the plugins (Java values)
{
	static double tab[sizeof(kGauss) / sizeof(kGauss[0])];
	static std::once_flag once;
	std::call_once(once, []() { for (size_t i = 0; i < sizeof(kGauss) / sizeof(kGauss[0]); i++) tab[i] = u2d(kGauss[i]); });
	return tab + kGaussOff[r];
}
static std::vector<double> MeanFilter(int r)
{
	int ksize = 2 * r + 1;
	return std::vector<double>(ksize, 1.0 / (double)ksize);
}

// DSLT3D_LINE2_Multi on one 2D image (depth 1): radii r_max..r_min step,
// rotation rd, weight -> c = weight/500*255, filter 0 Gaussian / 1 mean.
// The noise method is the IJ_Prefs value (radio buttons are compared with
// == in the plugin, so macro options never change it): noisenum = 0 here.
static Img8 DSLT3D(const Img16& in, int r_max, int r_min, int r_step, int rd, double weight, int filtertype, int noisenum)
{
	const int width = in.w, height = in.h;
	Img8 out; out.w = width; out.h = height; out.n = 1;
	out.p.assign((size_t)width * height, 0);
	std::vector<float> src(in.p.begin(), in.p.end());	// getf
	const double c = weight / 500.0 * 255.0;
	const int knum = rd * 2 * (rd * 2 - 1) + 1;
	std::vector<double> slat(knum), clat(knum), slon(knum), clon(knum), sintable(rd * 2), costable(rd * 2);
	slat[0] = 0.0; clat[0] = 1.0; slon[0] = 0.0; clon[0] = 0.0;
	for (int b = 0; b < rd * 2; b++)
		for (int a = 1; a < rd * 2; a++)
		{
			slat[b * (rd * 2 - 1) + (a - 1) + 1] = u2d(kSinA[a]);
			clat[b * (rd * 2 - 1) + (a - 1) + 1] = u2d(kCosA[a]);
			slon[b * (rd * 2 - 1) + (a - 1) + 1] = u2d(kSinA[b]);
			clon[b * (rd * 2 - 1) + (a - 1) + 1] = u2d(kCosA[b]);
		}
	for (int a = 0; a < rd * 2; a++) { sintable[a] = u2d(kSinA[a]); costable[a] = u2d(kCosA[a]); }
	auto run = [&](int r) {
		std::vector<double> mean;
		const double* filter;
		if (filtertype == 1) { mean = MeanFilter(r); filter = mean.data(); }
		else filter = GaussFilter(r);
		ParallelFor(height, [&](int yb, int ye) {
			for (int y = yb; y < ye; y++)
				for (int x = 0; x < width; x++)
				{
					const int z = 0, depth = 1;
					double bval = -1.0, sum;
					double rx, ry, rz, val;
					int ix, iy, iz;
					double sinlongi = 0.0, sinlati = 0.0, coslongi = 0.0, coslati = 0.0;
					double srcv = src[(size_t)y * width + x];
					for (int n = 0; n < knum; n++)
					{
						sum = 0.0;
						double drx = slat[n] * clon[n], dry = slat[n] * slon[n], drz = clat[n];
						for (int k = -r; k <= r; k++)
						{
							rx = x + k * drx; ry = y + k * dry; rz = z + k * drz;
							ix = jd2i(rx + 0.5); iy = jd2i(ry + 0.5); iz = jd2i(rz + 0.5);
							if (ix < 0) ix = 0; else if (ix >= width) ix = width - 1;
							if (iy < 0) iy = 0; else if (iy >= height) iy = height - 1;
							if (iz < 0) iz = 0; else if (iz >= depth) iz = depth - 1;
							val = src[(size_t)iy * width + ix];
							sum += (double)val * filter[r + k];
						}
						if (bval < sum)
						{
							bval = sum;
							sinlati = slat[n]; sinlongi = slon[n];
							coslati = clat[n]; coslongi = clon[n];
						}
					}
					sum = 0.0;
					for (int a = 0; a < rd * 2; a++)
					{
						double bx = costable[a], by = sintable[a], bz = 0.0;
						double drx2 = bx * coslati * coslongi - by * sinlongi + bz * sinlati * coslongi;
						double dry2 = bx * coslati * sinlongi + by * coslongi + bz * sinlati * sinlongi;
						double drz2 = -bx * sinlati + bz * coslati;
						for (int k = -r; k <= r; k++)
						{
							rx = x + k * drx2; ry = y + k * dry2; rz = z + k * drz2;
							ix = jd2i(rx + 0.5); iy = jd2i(ry + 0.5); iz = jd2i(rz + 0.5);
							if (ix < 0) ix = 0; else if (ix >= width) ix = width - 1;
							if (iy < 0) iy = 0; else if (iy >= height) iy = height - 1;
							if (iz < 0) iz = 0; else if (iz >= depth) iz = depth - 1;
							val = src[(size_t)iy * width + ix];
							sum += (double)val * filter[r + k];
						}
					}
					sum /= rd * 2;
					if (srcv > sum + c) out.p[(size_t)y * width + x] = 255;
				}
		});
	};
	for (int r = r_max; r >= r_min && r > 0; r -= r_step)
	{
		run(r);
		if (r_min > 0 && r != r_min && r - r_step < r_min) run(r_min);
	}
	if (noisenum == 1) DsltNoise5px(out); else DsltNoise1px(out);
	return out;
}

// ImageProcessor.getInterpolatedValue (bilinear; getPixelValue of 8-bit or
// 16-bit data without calibration)
template <class T> static inline double InterpValue(const T* p, int width, int height, double x, double y)
{
	if (x < 0.0 || x >= width - 1.0 || y < 0.0 || y >= height - 1.0)
	{
		if (x < -1.0 || x >= width || y < -1.0 || y >= height) return 0.0;
		// getInterpolatedEdgeValue
		int xbase = jd2i(x), ybase = jd2i(y);
		double xFraction = x - xbase, yFraction = y - ybase;
		if (xFraction < 0.0) xFraction = 0.0;
		if (yFraction < 0.0) yFraction = 0.0;
		auto edge = [&](int xx, int yy) -> float {
			if (xx <= 0) xx = 0;
			if (xx >= width) xx = width - 1;
			if (yy <= 0) yy = 0;
			if (yy >= height) yy = height - 1;
			return (float)p[(size_t)yy * width + xx];
		};
		double lowerLeft = edge(xbase, ybase), lowerRight = edge(xbase + 1, ybase);
		double upperRight = edge(xbase + 1, ybase + 1), upperLeft = edge(xbase, ybase + 1);
		double upperAverage = upperLeft + xFraction * (upperRight - upperLeft);
		double lowerAverage = lowerLeft + xFraction * (lowerRight - lowerLeft);
		return lowerAverage + yFraction * (upperAverage - lowerAverage);
	}
	int xbase = (int)x, ybase = (int)y;
	double xFraction = x - xbase, yFraction = y - ybase;
	if (xFraction < 0.0) xFraction = 0.0;
	if (yFraction < 0.0) yFraction = 0.0;
	double lowerLeft = (float)p[(size_t)ybase * width + xbase];
	double lowerRight = (float)p[(size_t)ybase * width + xbase + 1];
	double upperRight = (float)p[(size_t)(ybase + 1) * width + xbase + 1];
	double upperLeft = (float)p[(size_t)(ybase + 1) * width + xbase];
	double upperAverage = upperLeft + xFraction * (upperRight - upperLeft);
	double lowerAverage = lowerLeft + xFraction * (lowerRight - lowerLeft);
	return lowerAverage + yFraction * (upperAverage - lowerAverage);
}

// DSLT_ on one image (8-bit or 16-bit); then its noise filter (noiseC=1,
// "less than this value is the noise" = measuresize)
template <class T> static Img8 DSLT2D(const T* p, int width, int height, int r_max, int r_min, int r_step, int rd, double weight, int filtertype, int measuresize)
{
	Img8 out; out.w = width; out.h = height; out.n = 1;
	out.p.assign((size_t)width * height, 0);
	const double c = weight / 500.0 * 255.0;
	auto run = [&](int r) {
		std::vector<double> mean;
		const double* filter;
		if (filtertype == 1) { mean = MeanFilter(r); filter = mean.data(); }
		else filter = GaussFilter(r);
		ParallelFor(width, [&](int xb, int xe) {
			for (int x = xb; x < xe; x++)
				for (int y = 0; y < height; y++)
				{
					for (int d = 0; d < 2 * rd - 1; d++)
					{
						double ex = u2d(kDsltEx[d]), ey = u2d(kDsltEy[d]);
						double src = InterpValue(p, width, height, x, y);
						double sum = src * filter[r];
						for (int i = 1; i <= r; i++)
						{
							double x1 = (double)x + ex * (double)i;
							if (x1 > width) x1 = width; else if (x1 < 0) x1 = 0;
							double y1 = (double)y + ey * (double)i;
							if (y1 > height) y1 = height; else if (y1 < 0) y1 = 0;
							double x2 = (double)x - ex * (double)i;
							if (x2 > width) x2 = width; else if (x2 < 0) x2 = 0;
							double y2 = (double)y - ey * (double)i;
							if (y2 > height) y2 = height; else if (y2 < 0) y2 = 0;
							sum += InterpValue(p, width, height, x1, y1) * filter[r + i];
							sum += InterpValue(p, width, height, x2, y2) * filter[r - i];
						}
						if (src > sum + c) out.p[(size_t)y * width + x] = 255;
					}
				}
		});
	};
	for (int r = r_max; r >= r_min && r > 0; r -= r_step)
	{
		run(r);
		if (r_min > 0 && r != r_min && r - r_step < r_min) run(r_min);
	}
	DsltNoiseC(out, measuresize);
	return out;
}

// ---- Z_Code_Stack_HO: depth colour coding of the 8-bit stack ----
static ImgRGB ZCodeStack(const Img8& st, const std::vector<uint32_t>& lutrow)
{
	ImgRGB out; out.w = st.w; out.h = st.h; out.n = st.n;
	size_t plane = st.plane();
	out.p.assign(plane * st.n, 0);
	ParallelFor(st.n, [&](int zb, int ze) {
		for (int lateral = zb; lateral < ze; lateral++)
		{
			uint32_t colorpix = lutrow[lateral];
			int red = (colorpix >> 16) & 0xff, green = (colorpix >> 8) & 0xff, blue = colorpix & 0xff;
			const uint8_t* q = st.s(lateral);
			uint32_t* o = out.s(lateral);
			for (size_t kk = 0; kk < plane; kk++)
			{
				int pix = q[kk];
				int blue1 = pix & 0xff;
				if (blue1 > 1)
				{
					int R = jd2i(((double)pix / (double)255) * (double)red);
					int G = jd2i(((double)pix / (double)255) * (double)green);
					int B = jd2i(((double)pix / (double)255) * (double)blue);
					int rgb1 = R; rgb1 = (rgb1 << 8) + G; rgb1 = (rgb1 << 8) + B;
					o[kk] = (uint32_t)rgb1;
				}
			}
		}
	});
	return out;
}

// ---- MIP_right_color: colour MIP keeping the hue of the brightest voxel.
// max2 and MIPtwoST are state carried from pixel to pixel in the Java loop
// (they keep their value when the current MIP pixel has no strict maximum
// channel), so each slice is a sequential pass. ----
static ImgRGB MIPRightColor(const ImgRGB& st, int startMIP, int endMIP)
{
	int width = st.w, height = st.h, stackSize = st.n;
	if (endMIP > stackSize) endMIP = stackSize;
	if (startMIP == 0) startMIP = 1;
	ImgRGB ip2; ip2.w = width; ip2.h = height; ip2.n = 1;
	size_t sumpx = (size_t)width * height;
	ip2.p.assign(sumpx, 0);
	enum { ST_NONE = 0, RB2, RG2, GB2, GR2, BR2, BG2 };
	int MIPtwoST = ST_NONE;
	int max1 = 0, max2 = 0;
	auto MIPpix = [&](uint32_t* o, int red1, int red2, int green1, int green2, int blue1, int blue2, int st2) {
		int rgb1 = 0;
		if (st2 == RB2)
		{
			rgb1 = red2;
			if (green2 > green1) rgb1 = (rgb1 << 8) + green2;
			else { if (green1 < blue2) rgb1 = (rgb1 << 8) + green1; else rgb1 = (rgb1 << 8) + green2; }
			rgb1 = (rgb1 << 8) + blue2; *o = (uint32_t)rgb1;
		}
		else if (st2 == RG2)
		{
			rgb1 = red2; rgb1 = (rgb1 << 8) + green2;
			if (blue2 > blue1) rgb1 = (rgb1 << 8) + blue2;
			else { if (blue1 < green2) rgb1 = (rgb1 << 8) + blue1; else rgb1 = (rgb1 << 8) + blue2; }
			*o = (uint32_t)rgb1;
		}
		else if (st2 == GB2)
		{
			if (red2 > red1) rgb1 = red2;
			else { if (red1 < blue2) rgb1 = red1; else rgb1 = red2; }
			rgb1 = (rgb1 << 8) + green2; rgb1 = (rgb1 << 8) + blue2; *o = (uint32_t)rgb1;
		}
		else if (st2 == GR2)
		{
			rgb1 = red2; rgb1 = (rgb1 << 8) + green2;
			if (blue2 > blue1) rgb1 = (rgb1 << 8) + blue2;
			else { if (blue1 < red2) rgb1 = (rgb1 << 8) + blue1; else rgb1 = (rgb1 << 8) + blue2; }
			*o = (uint32_t)rgb1;
		}
		else if (st2 == BR2)
		{
			rgb1 = red2;
			if (green2 > green1) rgb1 = (rgb1 << 8) + green2;
			else { if (green1 < red2) rgb1 = (rgb1 << 8) + green1; else rgb1 = (rgb1 << 8) + green2; }
			rgb1 = (rgb1 << 8) + blue2; *o = (uint32_t)rgb1;
		}
		else if (st2 == BG2)
		{
			if (red2 > red1) rgb1 = red2;
			else { if (red1 < green2) rgb1 = red1; else rgb1 = red2; }
			rgb1 = (rgb1 << 8) + green2; rgb1 = (rgb1 << 8) + blue2; *o = (uint32_t)rgb1;
		}
	};
	for (int lateral = startMIP - 1; lateral < endMIP; lateral++)
	{
		const uint32_t* q = st.s(lateral);
		for (size_t kk = 0; kk < sumpx; kk++)
		{
			int RG1 = 0, BG1 = 0, GR1 = 0, GB1 = 0, RB1 = 0, BR1 = 0;
			int pix = (int)q[kk];
			int red1 = (pix >> 16) & 0xff, green1 = (pix >> 8) & 0xff, blue1 = pix & 0xff;
			if (red1 > blue1 && red1 > green1)
			{
				max1 = red1;
				if (blue1 > green1) RB1 = red1 + blue1; else RG1 = red1 + green1;
			}
			else if (green1 > blue1 && green1 > red1)
			{
				max1 = green1;
				if (blue1 > red1) GB1 = green1 + blue1; else GR1 = green1 + red1;
			}
			else if (blue1 > red1 && blue1 > green1)
			{
				max1 = blue1;
				if (red1 > green1) BR1 = blue1 + red1; else BG1 = blue1 + green1;
			}
			uint32_t* o = &ip2.p[kk];
			int pix2 = (int)*o;
			int red2 = (pix2 >> 16) & 0xff, green2 = (pix2 >> 8) & 0xff, blue2 = pix2 & 0xff;
			if (red2 > 0 || green2 > 0 || blue2 > 0)
			{
				if (red2 > blue2 && red2 > green2)
				{
					max2 = red2;
					MIPtwoST = blue2 > green2 ? RB2 : RG2;
				}
				else if (green2 > blue2 && green2 > red2)
				{
					max2 = green2;
					MIPtwoST = blue2 > red2 ? GB2 : GR2;
				}
				else if (blue2 > red2 && blue2 > green2)
				{
					max2 = blue2;
					MIPtwoST = red2 > green2 ? BR2 : BG2;
				}
				if ((max1 == 255) & (max2 == 255)) {}
				else
				{
					int rgb1;
					if (RB1 > 0)
					{
						if (max1 > max2)
						{
							rgb1 = red1;
							if (green2 < green1) rgb1 = (rgb1 << 8) + green1;
							else { if (green2 < blue1) rgb1 = (rgb1 << 8) + green2; else rgb1 = (rgb1 << 8) + green1; }
							rgb1 = (rgb1 << 8) + blue1; *o = (uint32_t)rgb1;
						}
						else MIPpix(o, red1, red2, green1, green2, blue1, blue2, MIPtwoST);
					}
					else if (RG1 > 0)
					{
						if (max1 > max2)
						{
							rgb1 = red1; rgb1 = (rgb1 << 8) + green1;
							if (blue2 < blue1) rgb1 = (rgb1 << 8) + blue1;
							else { if (blue2 < green1) rgb1 = (rgb1 << 8) + blue2; else rgb1 = (rgb1 << 8) + blue1; }
							*o = (uint32_t)rgb1;
						}
						else MIPpix(o, red1, red2, green1, green2, blue1, blue2, MIPtwoST);
					}
					else if (GB1 > 0)
					{
						if (max1 > max2)
						{
							if (red2 < red1) rgb1 = red1;
							else { if (red2 < blue1) rgb1 = red2; else rgb1 = red1; }
							rgb1 = (rgb1 << 8) + green1; rgb1 = (rgb1 << 8) + blue1; *o = (uint32_t)rgb1;
						}
						else MIPpix(o, red1, red2, green1, green2, blue1, blue2, MIPtwoST);
					}
					else if (GR1 > 0)
					{
						if (max1 > max2)
						{
							rgb1 = red1; rgb1 = (rgb1 << 8) + green1;
							if (blue2 < blue1) rgb1 = (rgb1 << 8) + blue1;
							else { if (blue2 < red1) rgb1 = (rgb1 << 8) + blue2; else rgb1 = (rgb1 << 8) + blue1; }
							*o = (uint32_t)rgb1;
						}
						else MIPpix(o, red1, red2, green1, green2, blue1, blue2, MIPtwoST);
					}
					else if (BR1 > 0)
					{
						if (max1 > max2)
						{
							rgb1 = red1;
							if (green2 < green1) rgb1 = (rgb1 << 8) + green1;
							else { if (green2 < red1) rgb1 = (rgb1 << 8) + green2; else rgb1 = (rgb1 << 8) + green1; }
							rgb1 = (rgb1 << 8) + blue1; *o = (uint32_t)rgb1;
						}
						else MIPpix(o, red1, red2, green1, green2, blue1, blue2, MIPtwoST);
					}
					else if (BG1 > 0)
					{
						if (max1 > max2)
						{
							if (red2 < red1) rgb1 = red1;
							else { if (red2 < green1) rgb1 = red2; else rgb1 = red1; }
							rgb1 = (rgb1 << 8) + green1; rgb1 = (rgb1 << 8) + blue1; *o = (uint32_t)rgb1;
						}
						else MIPpix(o, red1, red2, green1, green2, blue1, blue2, MIPtwoST);
					}
				}
			}
			else
			{
				if (red2 == 0 && green2 == 0 && blue2 == 0) *o = (uint32_t)pix;
			}
		}
	}
	return ip2;
}

// ===========================================================================
// data files: masks (16-bit TIFF), 3D mask (gzip NRRD), LUT (text)
// ===========================================================================

static std::string Utf8(const std::wstring& w)
{
	std::string out;
	for (size_t i = 0; i < w.size(); i++)
	{
		uint32_t c = (uint32_t)w[i];
		if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size())
		{
			uint32_t c2 = (uint32_t)w[i + 1];
			if (c2 >= 0xDC00 && c2 <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00); i++; }
		}
		if (c < 0x80) out += (char)c;
		else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
		else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
	}
	return out;
}
static std::wstring Join(const std::wstring& dir, const std::wstring& name)
{
	if (dir.empty()) return name;
	wchar_t last = dir.back();
	if (last == L'/' || last == L'\\') return dir + name;
#ifdef _WIN32
	return dir + L"\\" + name;
#else
	return dir + L"/" + name;
#endif
}
static FILE* OpenFile(const std::wstring& path, const char* mode)
{
#ifdef _WIN32
	std::wstring wm(mode, mode + strlen(mode));
	return _wfopen(path.c_str(), wm.c_str());
#else
	return fopen(Utf8(path).c_str(), mode);
#endif
}
static TIFF* OpenTiff(const std::wstring& path, const char* mode)
{
#ifdef _WIN32
	return TIFFOpenW(path.c_str(), mode);
#else
	return TIFFOpen(Utf8(path).c_str(), mode);
#endif
}

static bool ReadMaskTiff(const std::wstring& path, Img16& im, std::string& err)
{
	TIFF* tif = OpenTiff(path, "r");
	if (!tif) { err = "cannot open " + Utf8(path); return false; }
	uint32_t w = 0, h = 0; uint16_t bps = 0, spp = 1;
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
	TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps);
	TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
	if (bps != 16 || spp != 1 || !w || !h) { TIFFClose(tif); err = "unexpected mask format " + Utf8(path); return false; }
	im.w = (int)w; im.h = (int)h; im.n = 1;
	im.p.resize((size_t)w * h);
	for (uint32_t y = 0; y < h; y++)
		if (TIFFReadScanline(tif, im.p.data() + (size_t)y * w, y) < 0) { TIFFClose(tif); err = "read error " + Utf8(path); return false; }
	TIFFClose(tif);
	ResetMinAndMax(im);
	return true;
}

// minimal NRRD reader (uint8, raw or gzip) for JFRC2010_3D_Mask.nrrd
static bool ReadMaskNrrd8(const std::wstring& path, Img8& im, std::string& err)
{
	FILE* f = OpenFile(path, "rb");
	if (!f) { err = "cannot open " + Utf8(path); return false; }
	std::string all;
	char buf[65536];
	size_t r;
	while ((r = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, r);
	fclose(f);
	size_t he = all.find("\n\n");
	if (all.compare(0, 4, "NRRD") != 0 || he == std::string::npos) { err = "not a NRRD file " + Utf8(path); return false; }
	std::istringstream hs(all.substr(0, he));
	std::string line, type, enc;
	int sx = 0, sy = 0, sz = 0;
	while (std::getline(hs, line))
	{
		if (line.empty() || line[0] == '#') continue;
		size_t c = line.find(':');
		if (c == std::string::npos) continue;
		std::string k = line.substr(0, c), v = line.substr(c + 1);
		while (!v.empty() && v[0] == ' ') v.erase(0, 1);
		if (k == "type") type = v;
		else if (k == "encoding") enc = v;
		else if (k == "sizes") { std::istringstream ss(v); ss >> sx >> sy >> sz; }
	}
	if (!(type == "unsigned char" || type == "uchar" || type == "uint8" || type == "uint8_t") || sx <= 0 || sy <= 0 || sz <= 0)
	{ err = "unexpected 3D mask format"; return false; }
	size_t bytes = (size_t)sx * sy * sz;
	im.w = sx; im.h = sy; im.n = sz;
	im.p.resize(bytes);
	const unsigned char* src = (const unsigned char*)all.data() + he + 2;
	size_t srclen = all.size() - he - 2;
	if (enc == "raw") { if (srclen < bytes) { err = "short 3D mask"; return false; } memcpy(im.p.data(), src, bytes); }
	else if (enc == "gzip" || enc == "gz")
	{
		z_stream zs; memset(&zs, 0, sizeof(zs));
		if (inflateInit2(&zs, 15 + 32) != Z_OK) { err = "zlib"; return false; }
		zs.next_in = (Bytef*)src; zs.avail_in = (uInt)srclen;
		zs.next_out = im.p.data(); zs.avail_out = (uInt)bytes;
		int ret = inflate(&zs, Z_FINISH);
		size_t got = bytes - zs.avail_out;
		inflateEnd(&zs);
		if ((ret != Z_STREAM_END && ret != Z_OK && ret != Z_BUF_ERROR) || got != bytes) { err = "cannot decompress 3D mask"; return false; }
	}
	else { err = "unexpected 3D mask encoding " + enc; return false; }
	return true;
}

static bool ReadLut(const std::wstring& path, uint32_t lut[256], std::string& err)
{
	FILE* f = OpenFile(path, "rb");
	if (!f) { err = "cannot open " + Utf8(path); return false; }
	std::vector<std::vector<double>> rows;
	char line[512];
	while (fgets(line, sizeof(line), f))
	{
		std::vector<double> v;
		char* p = line;
		while (*p)
		{
			while (*p == ' ' || *p == '\t' || *p == ',' || *p == '\r' || *p == '\n') p++;
			if (!*p) break;
			char* e; double d = strtod(p, &e);
			if (e == p) break;
			v.push_back(d); p = e;
		}
		if (!v.empty()) rows.push_back(v);
	}
	fclose(f);
	if (rows.size() < 256 || rows.size() > 258) { err = "unexpected LUT " + Utf8(path); return false; }
	size_t y = rows.size() > 256 ? 1 : 0;
	for (int i = 0; i < 256; i++)
	{
		const auto& r = rows[y + i];
		size_t x = r.size() == 4 ? 1 : 0;
		if (r.size() < 3 + x) { err = "unexpected LUT row"; return false; }
		uint32_t R = (uint8_t)jd2i(r[x]), G = (uint8_t)jd2i(r[x + 1]), B = (uint8_t)jd2i(r[x + 2]);
		lut[i] = (R << 16) | (G << 8) | B;
	}
	return true;
}

// ===========================================================================
// text (drawString with Java2D LCD antialiasing) - see ColorDepthMIPText.inc
// ===========================================================================
#include "ColorDepthMIPText.inc"

// ===========================================================================
// the pipeline: mipfunction for one channel (imageNum == 1)
// ===========================================================================

const char* ModeLabel(int mode)
{
	switch (mode)
	{
	case Options::SegmentationAuto: return "Segmentation based automatic";
	case Options::Saturation15: return "Automatic 1.5% saturation (good for segmented neuron)";
	case Options::NoAdjustment: return "No adjustment";
	case Options::Max1: return "Max1";
	case Options::SegmentationNoLowerCut: return "Segmentation based no lower value cut";
	}
	return "Segmentation based automatic";
}

namespace
{
struct Canceled {};
struct Failed { std::string msg; };

struct Pipeline
{
	const Options& opt;
	std::wstring dataDir;
	Log log;
	Progress progress;
	double prog = 0;
	void step(double p, const char* what)
	{
		prog = p;
		if (progress && !progress(p, what)) throw Canceled();
	}
	Pipeline(const Options& o) : opt(o) {}

	bool loadMask(const wchar_t* name, Img16& m)
	{
		std::string err;
		if (!ReadMaskTiff(Join(dataDir, name), m, err)) throw Failed{ err };
		return true;
	}
};
}

bool Compute(const Volume& vol, const Options& opt, const std::wstring& dataDir, Result& res, const Progress& progress)
{
	res = Result();
	const char* dbg = getenv("VVD_CDM_DEBUG");
	const char* dump = getenv("VVD_CDM_DUMP");
	g_dump_dir = dump ? dump : "";
	g_threads = opt.threads;
	Pipeline P(opt);
	P.dataDir = dataDir;
	P.progress = progress;
	P.log.echo = dbg && *dbg && strcmp(dbg, "0") != 0;
	Log& log = P.log;
	auto t0 = std::chrono::steady_clock::now();
	try
	{
		if (!vol.data || vol.nx <= 0 || vol.ny <= 0) throw Failed{ "no voxel data" };
		if (vol.nz < 2) throw Failed{ "the volume has a single slice; a stack is required (the macro skips single images)" };
		if (vol.bits != 8 && vol.bits != 16 && vol.bits != 32) throw Failed{ "unsupported voxel type" };
		const size_t plane = (size_t)vol.nx * vol.ny;

		// ---- dialog values (macro 176-313) ----
		const std::string AutoBRVST = ModeLabel(opt.mode);
		bool easyADJ = false; int AutoBRV = 0;
		if (AutoBRVST == "Automatic 1.5% saturation (good for segmented neuron)") easyADJ = true;
		if (AutoBRVST == "Segmentation based automatic" || AutoBRVST == "Segmentation based no lower value cut") { AutoBRV = 1; easyADJ = true; }
		log("easyADJ; " + std::to_string(easyADJ ? 1 : 0) + "  AutoBRV; " + std::to_string(AutoBRV));
		double desiredmean = opt.desiredMean;
		if (AutoBRV == 1) log("Desired mean; " + n2s(desiredmean));
		double lowerweight = 0.0;
		const std::string lowthreM = "Peak Histogram";
		double secondjump = 210;
		const int multiDSLT = 1; (void)multiDSLT;
		const int autothre = 0;
		std::string unsharp = opt.skeletonBolded ? "Max" : "NA";
		const bool colorscale = opt.colorScale, reverse0 = opt.reversed, GammaON = opt.gamma14;
		const std::string DSLTver = opt.dsltLine ? "Line" : "Normal";
		double startMIP = opt.startMIP, endMIP = opt.endMIP;
		const int width = vol.nx, height = vol.ny, slices = vol.nz;

		// ---- mipfunction ----
		int bitd = vol.bits;
		log("bitd; " + std::to_string(bitd) + "  width; " + std::to_string(width) + "  height; " + std::to_string(height) + "  channels; 1  slices; " + std::to_string(slices) + "  frames; 1");
		P.step(0.01, "Preparing");
		Img16 stack;		// the stack window (16-bit from line 1357 on)
		Img8 stack8;		// the stack while it is 8-bit
		stack8.w = stack.w = width; stack8.h = stack.h = height; stack8.n = stack.n = slices;
		if (bitd == 16)
		{
			stack.p.assign((const uint16_t*)vol.data, (const uint16_t*)vol.data + plane * slices);
			ResetMinAndMax(stack);
		}
		else if (bitd == 8)
			stack8.p.assign((const uint8_t*)vol.data, (const uint8_t*)vol.data + plane * slices);

		double DefMaxValue = 0, Inimax = 0, Inimin = 0, RealInimax = 0, maxvalue = 0, applyV = 0, RealapplyV = 0;
		int eightbit = 0;
		double MedianSub = 70;
		// Entries of the stack image in ImageJ's batch-mode image list. Every
		// ImagePlus.show() in batch mode appends one (Interpreter.addBatchModeImage
		// has no duplicate check) and close() removes only one: open() = 1, and
		// "Mask Median Subtraction" and "Gamma samewindow noswing" each call
		// imp.show() on the stack once more. ColorCoder's gamma depends on it.
		int stackListed = 1;
		if (bitd == 32 || unsharp == "Max")	// macro 1313
		{
			if (bitd == 32)
			{
				// setMinAndMax(0, 1); run("8-bit") (FloatProcessor.create8BitImage)
				const float* fv = (const float*)vol.data;
				stack8.p.resize(plane * slices);
				const double min2 = 0, max2 = 1, scale = 255.0 / (max2 - min2);
				for (size_t i = 0; i < plane * slices; i++)
				{
					double value = fv[i] - min2;
					if (value < 0.0) value = 0.0;
					int ivalue = jd2i(value * scale + 0.5);
					if (ivalue > 255) ivalue = 255;
					stack8.p[i] = (uint8_t)ivalue;
				}
			}
			else if (bitd == 16)
			{
				SetMinAndMax(stack, 0, 1);
				stack8 = To8(stack);
				stack.p.clear(); stack.p.shrink_to_fit();
			}
			else
				SetMinAndMax(stack8, 0, 1);	// "8-bit" of an 8-bit stack does nothing
			bitd = 8;
			unsharp = "Max";
			DefMaxValue = 1;
			AutoBRV = 0;
		}
		log("Channel number; 1");
		if (bitd == 8)	// macro 1329
		{
			log("8bit file");
			desiredmean = 200; lowerweight = 0.3; secondjump = 245; MedianSub = 0; DefMaxValue = 255;
			Img8 mip = ZProjectMax(stack8);
			EnhanceContrast(mip, 0.3);
			double min = mip.dmin;
			Inimax = mip.dmax;
			if (min != 0 && Inimax != 255)
			{
				SetMinAndMax(stack8, min, Inimax);
				ApplyLut(stack8);
			}
			maxvalue = MaxValue(stack8);
			log("Maxvalue; " + std::to_string((int)maxvalue) + "  Minvalue; " + std::to_string((int)*std::min_element(stack8.p.begin(), stack8.p.end())));
			maxvalue = mround(maxvalue);
			log("3D stack brightness adjusted; maxvalue; " + n2s(maxvalue) + "  Inimax; " + n2s(Inimax));
			stack = To16(stack8);
			stack8.p.clear(); stack8.p.shrink_to_fit();
			bitd = 16;
			eightbit = 1;
		}
		P.step(0.05, "Brightness pre-adjustment");
		if (bitd == 16)	// macro 1362
		{
			log("stack dimension; width= " + std::to_string(width) + "  height= " + std::to_string(height) + "  slices= " + std::to_string(slices));
			Img16 MIPt = ZProjectMax(stack);
			Dump("d01_MIPin", MIPt);
			lowerweight = 0.3;
			ResetMinAndMax(MIPt);
			Inimin = MIPt.dmin;
			double max = MIPt.dmax;
			if (max > 255 && max < 4096) DefMaxValue = 4095;
			else if (max > 4095) DefMaxValue = 65535;
			else if (max < 256) DefMaxValue = 255;
			if (eightbit == 1) DefMaxValue = 255;
			if (max <= 255)
			{
				desiredmean = 205; lowerweight = 0; MedianSub = 100;
				log("desiremean 205  16bit file with 8bit data");
			}
			EnhanceContrast(MIPt, 0.3);
			Inimax = MIPt.dmax;
			RealInimax = Inimax;
			log("Inimax; " + n2s(Inimax));
			if (DefMaxValue == 255) log("DefMaxValue = 255, 16 bit");
			if (easyADJ == false)
			{
				if (DefMaxValue == 4095)
				{
					if (Inimax < 200 && Inimax > 100) Inimax = Inimax * 3.5;
					else if (Inimax >= 200 && Inimax < 300) Inimax = mround(Inimax * 3);
					else if (Inimax < 100) Inimax = Inimax * 4;
					else if (Inimax >= 300 && Inimax < 500) Inimax = Inimax * 2;
				}
				if (DefMaxValue == 65535)
				{
					if (Inimax < 3200 && Inimax > 1600) Inimax = mround(Inimax * 1.5);
					else if (Inimax >= 3200 && Inimax < 4800) Inimax = mround(Inimax * 1.2);
					else if (Inimax < 1600) Inimax = mround(Inimax * 2);
					else if (Inimax >= 4800 && Inimax < 8000) Inimax = mround(Inimax * 1.1);
				}
			}
			if (easyADJ == true)
			{
				if (DefMaxValue == 4095)
				{
					if (Inimax < 200 && Inimax > 100) Inimax = mround(Inimax * 1.5);
					else if (Inimax >= 200 && Inimax < 300) Inimax = mround(Inimax * 1.2);
					else if (Inimax < 100) Inimax = mround(Inimax * 2);
					else if (Inimax < 2000 && Inimax > 1000) Inimax = mround(Inimax * 0.9);
					else if (Inimax >= 2000) Inimax = mround(Inimax * 0.8);
				}
				if (DefMaxValue == 65535)
				{
					if (Inimax < 3200 && Inimax > 1600) Inimax = mround(Inimax * 1.5);
					else if (Inimax >= 3200 && Inimax < 4800) Inimax = mround(Inimax * 1.2);
					else if (Inimax < 1600) Inimax = mround(Inimax * 2);
					else if (Inimax >= 4800 && Inimax < 8000) Inimax = mround(Inimax * 1.1);
				}
			}
			log("After adjusted Inimax; " + n2s(Inimax));
			SetMinAndMax(MIPt, 0, Inimax);
			ApplyLut(MIPt);
			Dump("d02_MIPlut", MIPt);
			applyV = mround(Inimax);
			if (easyADJ == true)
			{
				double sumval = 0, sumnumpx = 0;
				for (int ixx = 0; ixx < width; ixx++)
					for (int iyy = 0; iyy < height; iyy++)
					{
						double pixn = MIPt.p[(size_t)iyy * width + ixx];
						if (pixn > 1) { sumval = sumval + pixn; sumnumpx = sumnumpx + 1; }
					}
				double aveval = mround((sumval / sumnumpx) / 16);
				log("aveval; " + n2s(aveval) + "   applyV; " + n2s(applyV));
			}
			log("applyV 336; " + n2s(applyV));
			// mask subtraction / central fill (macro 1491-1522)
			bool vncMask = false;
			if (width == 512)
			{
				if (height == 1024 || height == 1100)
				{
					Img16 m;
					P.loadMask(height == 1024 ? L"Mask_VNC_Female.tif" : L"Mask_VNC_Male.tif", m);
					ImageCalculator(MIPt, m, Calc::Subtract);
					vncMask = true;
				}
			}
			else if (width == 1024)
			{
				if (height == 512)
				{
					Img16 m;
					P.loadMask(L"JFRC2010_Mask.tif", m);
					ImageCalculator(MIPt, m, Calc::Subtract);
				}
			}
			else
			{
				SetMinAndMax(MIPt, 0, 65535);
				int rx, ry, rw, rh;
				RectRoi(width * 0.1, height * 0.1, width * 0.8, height * 0.8, rx, ry, rw, rh);
				FillRect(MIPt, rx, ry, rw, rh, 0);	// black with display range 0..65535 -> 0
			}
			if (AutoBRVST == "Segmentation based no lower value cut") lowerweight = 0;
			Dump("d03_MIPmasked", MIPt);
			double total = 0, zerovalue = 0;
			for (int ix = 0; ix < width; ix++)
				for (int iy = 0; iy < height; iy++)
				{
					double pxv = MIPt.p[(size_t)iy * width + ix];
					total = total + pxv;
					if (pxv == 0 && !vncMask) zerovalue = zerovalue + 1;
				}
			Inimin = mround((total / (((double)height * width) - zerovalue)) * 0.8);
			log("Initial Bri adjustment; Inimin; " + n2s(Inimin) + "   max; " + n2s(Inimax) + "   RealInimax; " + n2s(RealInimax) + "   DefMaxValue; " + n2s(DefMaxValue));
			if (Inimin != 0 || Inimax != 65535)
			{
				if (easyADJ == true || AutoBRV == 1)
				{
					SetMinAndMax(stack, 0, applyV);
					ApplyLut(stack);
					Dump("d04_stack_applyV", stack);
				}
				if (AutoBRVST == "Segmentation based no lower value cut") Inimin = 0;
				if (AutoBRV == 1)
				{
					SetMinAndMax(stack, Inimin, 65535);
					ApplyLut(stack);
					Dump("d05_stack_inimin", stack);
					// macro 1574-1592: the "Mask MIP Zerovalue Measure" check reads the
					// undefined variables MaskName2D / zeronumberpxPre (the macro stops
					// there with "Undefined variable"); implemented with zero gap: no
					// change of Inimin / lowerweight / AutoBRVST.
					if (AutoBRVST != "Segmentation based no lower value cut")
						log("zeronumberpxPost; 0  zeronumberpxPre; 0  gap; 0");
					maxvalue = MaxValue(stack);
					log("Maxvalue; " + std::to_string((int)maxvalue) + "  Minvalue; " + std::to_string((int)*std::min_element(stack.p.begin(), stack.p.end())));
					maxvalue = mround(maxvalue);
					log("3D stack brightness adjusted; maxvalue; " + n2s(maxvalue));
				}
			}
			else
				maxvalue = 65535;
		}
		P.step(0.15, "Background median subtraction");
		// macro 1623-1700: median subtraction
		if (unsharp != "Max" && AutoBRV == 1)
		{
			if (lowerweight > 0)
			{
				if (height == 512 && width == 1024)
				{
					if (MedianSub != 0)
					{
						Img16 mp = ZProjectMax(stack);
						double max = mp.dmax;
						if (max <= 255)
						{
							desiredmean = 200; lowerweight = 0; MedianSub = 100;
							log("16bit file with 8bit data");
						}
						Img8 m3;
						std::string err;
						if (!ReadMaskNrrd8(Join(dataDir, L"JFRC2010_3D_Mask.nrrd"), m3, err)) throw Failed{ err };
						double histave;
						if (bitd == 16 && max > 255) { histave = 50; MedianSub = 60; }
						else histave = 1;
						MaskMedianSubtraction(m3, stack, jd2i(MedianSub), jd2i(histave), log);
						stackListed++;	// the plugin ends with imp.show() of its data image
						Dump("d06_stack_median3D", stack);
					}
				}
				else
				{
					int OrigiSlice = slices;
					if (MedianSub != 0 && lowerweight > 0)
					{
						Img16 mm = ZProjectMax(stack, 10, OrigiSlice - 10);
						double lo, up;
						AutoThreshold16(mm, ThMethod::Mean, lo, up);
						Img8 mask1 = ConvertToMask16(mm, lo, up);
						Dump("d06m_MIPmask", mask1);
						double histave = bitd == 16 ? 50 : 5;
						Img8 mstack; mstack.w = width; mstack.h = height; mstack.n = OrigiSlice;
						mstack.p.resize(plane * OrigiSlice);
						for (int s = 0; s < OrigiSlice; s++) memcpy(mstack.s(s), mask1.p.data(), plane);
						log("1278 lowerweight*0.3; " + n2s(lowerweight * 0.3));
						// "%="+lowerweight*30: the dialog reads (int) of the number
						MaskMedianSubtraction(mstack, stack, jd2i(lowerweight * 30), jd2i(histave), log);
						stackListed++;	// the plugin ends with imp.show() of its data image
						Dump("d06_stack_medianMIP", stack);
					}
				}
			}
		}

		// ---- basicoperation (macro 3104) ----
		P.step(0.25, "MIP");
		{
			if (bitd == 16)
			{
				Img16 m = ZProjectMax(stack);
				double max = m.dmax;
				SetMinAndMax(stack, 0, max);
			}
		}
		Img16 MIP;		// "MIP.tif"
		Img8 MIP8;		// only for bitd == 8 (unsharp=Max path)
		if (bitd == 16)
		{
			MIP = ZProjectMax(stack, 15, slices);
			ResetMinAndMax(MIP);
			MIP = CanvasSize(MIP, (int)mround(MIP.w * 0.95), (int)mround(MIP.h * 0.95), Center);
		}
		log("basicoperation done");
		if (bitd == 16) Dump("d07_MIP", MIP);

		double sigsize = 0, sigsizethre = 0;
		// ---- autobradjustment (macro 1943) ----
		if (AutoBRV == 1)
		{
			P.step(0.3, "Signal size");
			applyV = 1;
			const int width2 = MIP.w, height2 = MIP.h;
			const double totalpix = (double)width2 * height2;
			SetMinAndMax(MIP, 0, maxvalue);
			double areathre = 0, mean = 0;
			{
				Img16 t2 = MIP;
				double lower, upper;
				AutoThreshold16(t2, ThMethod::Triangle, lower, upper);
				// setThreshold(lower, maxvalue)
				double up = maxvalue; if (up > 65535.0) up = 65535.0;
				double lo = lower; if (lo < 0) lo = 0;
				Img8 m = ConvertToMask16(t2, (double)jround(lo), (double)jround(up));
				Dump("d08_test2_tri", m);
				SelectionStats(m, (long long)totalpix, areathre, mean);
			}
			if (areathre / totalpix > 0.4)
			{
				Img16 t2 = MIP;
				double lower, upper;
				AutoThreshold16(t2, ThMethod::Moments, lower, upper);
				double up = maxvalue; if (up > 65535.0) up = 65535.0;
				double lo = lower; if (lo < 0) lo = 0;
				Img8 m = ConvertToMask16(t2, (double)jround(lo), (double)jround(up));
				Dump("d08b_test2_mom", m);
				SelectionStats(m, (long long)totalpix, areathre, mean);
			}
			// ---- DSLTfun ----
			P.step(0.35, "Anisotropic diffusion");
			Img16 ad = AnisotropicDiffusion2D(MIP);
			log("Anisotropic diffusion ON");
			Dump("d09_AD", ad);
			ResetMinAndMax(ad);
			double admax = ad.dmax;
			A4095Normalizer(ad);
			{
				char buf[64]; snprintf(buf, sizeof(buf), "start; 1  end; 1  maxval; %.1f", admax);
				log(buf);
			}
			log("A4095 normalizer max; " + n2s(admax));
			Dump("d10_A4095", ad);
			P.step(0.45, "DSLT");
			Img8 first;
			// DSLT3D_LINE2_Multi "noise=5px" and DSLT_: radio buttons are compared
			// with == in the plugins, so macro options never change them: the noise
			// method stays at the IJ_Prefs value (0 = 1px) and DSLT_'s
			// "less than this value is the noise" slider at measuresize.int (9)
			if (DSLTver == "Line") first = DSLT3D(ad, 15, 2, 3, 8, 140, 0, 0);
			else first = DSLT2D(ad.p.data(), ad.w, ad.h, 15, 2, 3, 8, 140, 1, 9);
			Dump("d11_DSLT", first);
			RemoveDarkOutliers(first, 90.0f);
			Dump("d12_RO1", first);
			Img8 second = DSLT2D(first.p.data(), first.w, first.h, 5, 1, 2, 8, 1, 0, 9);
			Dump("d13_DSLT2", second);
			RemoveDarkOutliers(second, 90.0f);
			Dump("d14_RO2", second);
			Img16 test = To16(second);
			Mask255To4095(test);
			Dump("d15_test", test);
			double area1, mean1;
			{
				Img8 t2 = To8(test);
				t2 = CanvasSize(t2, (int)mround(t2.w * 0.95), (int)mround(t2.h * 0.95), Center);
				Dump("d16_test2c", t2);
				SelectionStats(t2, (long long)totalpix, area1, mean1);
			}
			double presize = area1 / totalpix;
			if (area1 == totalpix) { presize = 0.0001; log("Equal"); }
			log("Area 1st time;  " + n2s(area1) + "   mean; " + n2s(mean1) + "  totalpix; " + n2s(totalpix) + "   presize; " + n2s(presize * 100) + " %   bitd; " + std::to_string(bitd));
			double realArea = area1;
			double area2 = realArea;
			// Mask Brightness Measure + applyVcalculation
			ResetMinAndMax(MIP);
			std::string fff = MaskBrightnessMeasure(test, MIP, jd2i(desiredmean), log);
			log("fff 1202; " + fff + "   desiredmean; " + n2s(desiredmean));
			{
				double av;
				if (fff == "MIP.tif") av = 200;
				else av = std::strtod(fff.c_str(), nullptr);
				av = mround(av);
				double max = MIP.dmax;
				if (max <= 4095) av = 4095 - av;
				if (max > 4095) av = 65535 - av;
				if (av == 0) av = max;
				else if (av < 100) av = 100;
				applyV = av;
			}
			if (fff == "MIP.tif") applyV = 150;	// bitd == 16
			sigsize = area2 / totalpix;
			if (sigsize == 1) sigsize = 0;
			sigsizethre = areathre / totalpix;
			log("Signal brightness; after 65535 applyV;\t" + n2s(applyV) + "\t Signal Size DSLT; \t" + n2s(sigsize) + "\t Sig size threshold; \t" + n2s(sigsizethre));
			sigsize = sigsize * 100;
			sigsizethre = sigsizethre * 100;
			log("Auto-bri finished");
		}
		// macro 1740-1774
		if (AutoBRV == 1)
		{
			log("1704 DefMaxValue" + n2s(DefMaxValue) + "  Inimax; " + n2s(Inimax) + "  applyV" + n2s(applyV));
			if (DefMaxValue == 4095 && applyV > 4095) DefMaxValue = 65535;
			if (bitd == 16)
			{
				if (DefMaxValue == 4095) RealapplyV = mround((applyV / 16) * (Inimax / DefMaxValue));
				else if (DefMaxValue == 255) RealapplyV = ((applyV / (16 * 16)) * (Inimax / DefMaxValue));
				else if (DefMaxValue == 65535) RealapplyV = mround(applyV * (Inimax / DefMaxValue));
			}
			log("After +Inimax RealapplyV; " + n2s(RealapplyV) + "   applyV; " + n2s(applyV));
		}
		P.step(0.6, "Brightness");
		bool stackIs8 = (bitd == 8);
		Img8 stackOut;	// the 8-bit stack for the colour coder
		if (sigsize < 30)
		{
			if (AutoBRV == 1)
			{
				// ---- brightnessapply (macro 2448), bitd == 16 ----
				log("brightnessapply start, RealapplyV; " + n2s(RealapplyV) + "   applyV; " + n2s(applyV) + "  lowerweight; " + n2s(lowerweight) + "   sigsize; " + n2s(sigsize) + "  sigsizethre; " + n2s(sigsizethre));
				double brightnessNeed = 0, changelower = 0;
				double applyV2 = applyV;
				if (applyV == 65535) applyV2 = 65534;
				Img16 MIP2 = ZProjectMax(stack);
				double min = MIP2.dmin, max = MIP2.dmax;
				log("Line 1225 min; " + n2s(min) + "   max; " + n2s(max));
				if (min != 0 && max != 65535) ApplyLut(MIP2);
				Img16 MIPDUP = MIP2;
				Dump("d17_MIPDUP", MIPDUP);
				log("Line 1252;  MIPthresholding; MIPDUP.tif");
				log("getHeight; " + std::to_string(height));
				std::string tissue = "UNKNOWN";
				// BackgroundMask (macro 3516)
				auto backgroundMask = [&](Img16& MIPapply, const std::string& tis) -> double {
					log("tissue; " + tis);
					std::wstring maskName;
					double BrightnessAdjustMaskValue = 1;
					if (tis == "Brain")
					{
						if (height == 512) { maskName = L"JFRC2010_Mask.tif"; BrightnessAdjustMaskValue = 65535.0 / 46447; }
						if (height == 592) { maskName = L"JFRC2013_20x_Mask.tif"; BrightnessAdjustMaskValue = 65535.0 / 40972; }
					}
					else if (tis == "VNC")
					{
						if (height == 1024) { maskName = L"Mask_VNC_Female.tif"; BrightnessAdjustMaskValue = 65535.0 / 29988; }
						if (height == 1100) { maskName = L"Mask_VNC_Male.tif"; BrightnessAdjustMaskValue = 65535.0 / 29700; }
					}
					// macro 3533: the brain masks are opened from JFRCpath (JFRC2010_Mask.tif)
					// for both sizes, so the 1184x592 path stops with "Image not found";
					// implemented as evidently intended: the JFRC2013_20x mask.
					log("Mask path; " + Utf8(Join(dataDir, maskName)));
					log("Used a Mask for background subtraction.");
					Img16 m;
					P.loadMask(maskName.c_str(), m);
					if (maskName == L"JFRC2010_Mask.tif")	// objective "40x": optic lobes removed
					{
						FillPolygon(m, { 813, 672, 840, 1022, 1020 }, { 90, 431, 503, 488, 113 }, 0);
						FillPolygon(m, { 177, 355, 227, 1, 12 }, { 56, 453, 507, 505, 89 }, 0);
						BrightnessAdjustMaskValue = 65535.0 / 25266;
					}
					Dump("d19_bgmask", m);
					Dump("d19b_bgdata", MIPapply);
					if (GammaON)
					{
						Gamma(MIPapply);
						log("GAMMA 1.4 applied to 2D");
					}
					ImageCalculator(MIPapply, m, Calc::Min);
					Dump("d20_bgmin", MIPapply);
					double bn = 0;
					if (!MaskMIPBrightness(m, MIPapply, bn)) { log("No signal"); bn = NAN; }
					bn = mround(bn * BrightnessAdjustMaskValue);
					log("brightnessNeed; 3405  " + n2s(bn));
					return bn;
				};
				if (height == 512 || height == 592)
				{
					if (width == 1024 || width == 1184) { tissue = "Brain"; brightnessNeed = backgroundMask(MIPDUP, tissue); }
				}
				else if (height == 1024 || height == 1100)
				{
					if (width == 512) { tissue = "VNC"; brightnessNeed = backgroundMask(MIPDUP, tissue); }
				}
				if (tissue == "UNKNOWN")
				{
					Img16 mm; mm.w = width; mm.h = height; mm.n = 1;
					mm.p.assign(plane, 0);
					// newImage 16-bit (display 0..65535), white fill = 65535, then
					// setMinAndMax(0,255) + Apply LUT
					FillRect(mm, (int)mround(width * (150.0 / 1500)), (int)mround(height * (100.0 / 833)), (int)mround(width * (1200.0 / 1500)), (int)mround(height * (600.0 / 833)), 65535);
					mm.dmin = 0; mm.dmax = 255;
					ApplyLut(mm);
					std::string t;
					double MaskBri = ThreeDAve(mm, &t);
					log("AVEsamp; " + t);
					ImageCalculator(MIPDUP, mm, Calc::Min);
					if (GammaON)
					{
						Gamma(MIPDUP);
						log("GAMMA 1.4 applied to 2D");
					}
					brightnessNeed = ThreeDAve(MIPDUP, &t);
					log("AVEsamp; " + t);
					brightnessNeed = mround(brightnessNeed);
					brightnessNeed = mround(brightnessNeed * (65535 / MaskBri));
					log("brightnessNeed; " + n2s(brightnessNeed));
				}
				const double MinSigSize = 0;
				log("MIPthresholding; MIPDUP.tif");
				Dump("d18_MIPthr", MIPDUP);
				double maxi = 0, countregion = 10000;
				// lowthreM == "Peak Histogram"
				{
					std::vector<long long> counts(65536, 0);
					for (uint16_t v : MIPDUP.p) counts[v]++;
					const int medianNum = 400;
					brightnessNeed = mround(brightnessNeed);
					if (brightnessNeed > 35000) countregion = 16000; else countregion = brightnessNeed;
					auto peak = [&](const std::vector<long long>& c, double& mx, bool roundIt) {
						double maxcounts = 0;
						// sum_{a=i3}^{i3+399} c[a]*a is exact in a double (< 2^53)
						long long lim = (long long)(countregion - medianNum);
						for (long long i3 = 5; (double)i3 < countregion - medianNum; i3++)
						{
							long long s = 0;
							for (long long a = i3; a < i3 + medianNum; a++) s += (a < (long long)c.size() ? c[a] : 0) * a;
							double AveVal20 = (double)s / medianNum;
							if (AveVal20 > maxcounts)
							{
								maxcounts = AveVal20;
								mx = roundIt ? mround(i3 + (medianNum / 2.0)) : i3 + (medianNum / 2.0);
							}
						}
						(void)lim;
					};
					peak(counts, maxi, false);
					if (maxi == 0) maxi = brightnessNeed;
					log("Avebrightness; " + n2s(brightnessNeed) + "  maxi" + n2s(maxi) + "   countregion; " + n2s(countregion) + "  sigsize; " + n2s(sigsize) + "  MinSigSize;" + n2s(MinSigSize));
					if (brightnessNeed > 40000)
					{
						double applyV2Ori = applyV2;
						applyV2 = applyV2 * (brightnessNeed / 40000);
						if (applyV2 > 65534) applyV2 = 65534;
						log("Too bright, applyV2 decreased from " + n2s(applyV2Ori) + " to " + n2s(applyV2));
					}
					double minus2 = 0;
					if (sigsize >= MinSigSize)
					{
						if (maxi < 1000 && brightnessNeed < 2000)
						{
							log("Decreasing maxi; original maxi = " + n2s(maxi));
							double avebrightness = 0, previousmaxi = 0, preapplyV2 = 0;
							while (maxi < 400 && avebrightness < 2000)
							{
								minus2 = minus2 - mround(applyV2 * 0.03);
								Img16 d2 = MIP2;
								log("minus2; " + n2s(minus2) + "  applyV2; " + n2s(applyV2) + "  maxi; " + n2s(maxi));
								if (applyV2 + minus2 > 80)
								{
									log("applyV2+minus2; " + n2s(applyV2 + minus2) + "  min; " + n2s(min));
									SetMinAndMax(d2, min, applyV2 + minus2);
									ApplyLut(d2);
								}
								else break;
								if ((height == 512 || height == 592) && (width == 1024 || width == 1184))
									backgroundMask(d2, "Brain");
								else if ((height == 1024 || height == 1100) && width == 512)
									backgroundMask(d2, "VNC");
								else if (!(height == 512 || height == 592 || height == 1024 || height == 1100))
								{
									Img16 mm; mm.w = width; mm.h = height; mm.n = 1;
									mm.p.assign(plane, 0);
									int rx = (int)mround(width * (162.0 / 1499)), ry = (int)mround(height * (131.0 / 833));
									int rw = (int)mround(width * (1150.0 / 1499)), rh = (int)mround(height * (508.0 / 833));
									FillOutsideRect(mm, rx, ry, rw, rh, 65535);
									ImageCalculator(d2, mm, Calc::Max);
								}
								// getHistogram(values, counts, 65535): 65535 bins over [min, max]
								std::vector<long long> c2(65535, 0);
								{
									int hmn = 65535, hmx = 0;
									for (uint16_t v : d2.p) { if (v < hmn) hmn = v; if (v > hmx) hmx = v; }
									const int nBins = 65535;
									double histMin = hmn, histMax = hmx;
									double binSize = (histMax - histMin + 1) / nBins;
									double scale = 1.0 / binSize;
									int hMin = (int)histMin;
									for (uint16_t v : d2.p)
									{
										int index = jd2i(scale * (v - hMin));
										if (index >= nBins) index = nBins - 1;
										c2[index]++;
									}
								}
								double tot = 0;
								for (uint16_t v : d2.p) tot += v;
								avebrightness = mround(tot / ((double)height * width));
								log("avebrightness; " + n2s(avebrightness));
								peak(c2, maxi, true);
								if (previousmaxi == maxi && applyV2 == preapplyV2) break;
								previousmaxi = maxi;
								preapplyV2 = applyV2;
							}
							if (maxi < 3000 && DefMaxValue < 4096)
							{
								applyV2 = applyV2 + minus2;
								RealapplyV = mround((applyV2 / 16) * (Inimax / 4095));
							}
							changelower = maxi * 0.05;
						}
						else if (maxi > 9000)
						{
							changelower = maxi * 0.9;
							if (sigsize < 10)
							{
								changelower = maxi;
								double preRealapplyV = RealapplyV;
								applyV2 = applyV2 * 1.6;
								RealapplyV = mround((applyV2 / 16) * (Inimax / 4095));
								log("decreased RealapplyV; from " + n2s(preRealapplyV) + " to " + n2s(RealapplyV));
							}
						}
						else if (maxi > 3500 && maxi <= 5700)
						{
							changelower = maxi * 0.7;
							if (sigsize > 7.5) { changelower = maxi * 0.4; log("changelower=maxi*0.4  sigsize; " + n2s(sigsize)); }
							else applyV2 = applyV2 * 1.4;
							if (sigsize < 2.5)
							{
								changelower = maxi * 0.9;
								double preRealapplyV = RealapplyV;
								applyV2 = applyV2 * 1.6;
								RealapplyV = mround((applyV2 / 16) * (Inimax / 4095));
								log("decreased RealapplyV; from " + n2s(preRealapplyV) + " to " + n2s(RealapplyV));
								log("changelower=maxi*0.7  sigsize; " + n2s(sigsize));
							}
						}
						else if (maxi > 5700 && maxi <= 9000)
						{
							changelower = maxi * 0.8;
							if (sigsize > 9) { changelower = maxi * 0.5; log("changelower=maxi*0.5  sigsize; " + n2s(sigsize)); }
							else applyV2 = applyV2 * 1.4;
						}
						else if (maxi > 2500 && maxi <= 3500)
						{
							changelower = maxi * 0.2;
							if (sigsize < 2) { changelower = maxi * 0.4; log("changelower=maxi*0.4  sigsize; " + n2s(sigsize)); }
							else if (maxi > 3000) applyV2 = applyV2 * 1.2;
						}
						else if (maxi <= 2500)
							changelower = 0;
					}
					if ((sigsize / maxi) * 100 < 0.13 && maxi <= 2500) changelower = maxi * 0.2;
					log("lower threshold; \t" + n2s(changelower) + "   maxi; " + n2s(maxi));
				}
				if (GammaON)
				{
					// "Gamma samewindow noswing": in place, then imp.show()
					Gamma(stack);
					stackListed++;
					log("GAMMA 1.4 applied to 3D stack");
				}
				if (sigsize >= MinSigSize)
				{
					SetMinAndMax(stack, 0, applyV2);
					ApplyLut(stack);
				}
				maxvalue = MaxValue(stack);
				log("Maxvalue; " + std::to_string((int)maxvalue) + "  Minvalue; " + std::to_string((int)*std::min_element(stack.p.begin(), stack.p.end())));
				maxvalue = mround(maxvalue);
				log("stack maxvalue; " + n2s(maxvalue));
				changelower = mround(changelower);
				if (AutoBRVST == "Segmentation based no lower value cut") changelower = 0;
				if (changelower != 0)
				{
					SetMinAndMax(stack, changelower, maxvalue);
					ApplyLut(stack);
				}
				Dump("d21_stack_pre8", stack);
				SetMinAndMax(stack, 0, 65535);
				stackOut = To8(stack);
				Dump("d22_stack8", stackOut);
				stack.p.clear(); stack.p.shrink_to_fit();
				stackIs8 = true;
			}
		}
		else
		{
			// macro 1787: setMinAndMax(0, 65535); run("8-bit") on the stack
			if (!stackIs8)
			{
				SetMinAndMax(stack, 0, 65535);
				stackOut = To8(stack);
				stack.p.clear(); stack.p.shrink_to_fit();
				stackIs8 = true;
			}
		}
		if (AutoBRV == 0)	// macro 1800
		{
			if (easyADJ == false) applyV = 255;
			if (!stackIs8 && bitd == 16)
			{
				if (easyADJ == false) SetMinAndMax(stack, 0, DefMaxValue);
				stackOut = To8(stack);
				stack.p.clear(); stack.p.shrink_to_fit();
				stackIs8 = true;
			}
			log("line 1670 nImages; 2");	// open in Fiji: the stack and MIP.tif
		}
		if (!stackIs8)	// bitd == 8 (unsharp=Max path ends 16-bit only through the branches above)
		{
			stackOut = To8(stack);
			stackIs8 = true;
		}
		if (reverse0)	// macro 1790: Reverse + Flip Horizontally
		{
			Img8 r = stackOut;
			size_t pl = stackOut.plane();
			for (int z = 0; z < stackOut.n; z++)
			{
				const uint8_t* src = stackOut.s(stackOut.n - 1 - z);
				uint8_t* dst = r.s(z);
				for (int y = 0; y < height; y++)
					for (int x = 0; x < width; x++)
						dst[(size_t)y * width + x] = src[(size_t)y * width + (width - 1 - x)];
			}
			(void)pl;
			stackOut = std::move(r);
		}
		if (AutoBRV == 1)
		{
			applyV = mround(RealapplyV);
			log("Line 1518 RealapplyV; " + n2s(mround(RealapplyV)));
		}
		if (AutoBRVST == "Max1")
		{
			SetMinAndMax(stackOut, 0, 0);
			ApplyLut(stackOut);
		}

		// ---- ColorCoder (macro 3216) ----
		P.step(0.75, "Colour coding");
		Dump("d23_orig8", stackOut);
		if (GammaON)
		{
			// macro 3230-3241: "Gamma " (3d, in=InMacro) writes the corrected copy
			// "DUP_Original_Stack.tif"; the macro closes "Original_Stack.tif" and
			// renames the copy. close() drops one batch-mode list entry only, so
			// when the stack was listed more than once a stale entry of the
			// uncorrected stack stays ahead of the renamed copy, and the later
			// selectWindow("Original_Stack.tif") / "Z Code Stack HO
			// data=Original_Stack.tif" take that first match: Fiji colour-codes
			// the uncorrected stack. Verified with the Fiji plugins (docs/QT_PORT.md).
			if (stackListed == 1)
				Gamma(stackOut);
			log("GAMMA 1.4 applied");
		}
		uint32_t lut[256];
		{
			std::string err;
			if (!ReadLut(Join(dataDir, L"PsychedelicRainBow2.lut"), lut, err)) throw Failed{ err };
		}
		std::vector<uint32_t> lutrow(slices);
		for (int xxx = 0; xxx < slices; xxx++)
		{
			double per = (double)xxx / slices;
			double colv = mround(255 * per);
			lutrow[xxx] = lut[std::min(255, std::max(0, jd2i(colv)))];
		}
		{
			ImgRGB lt; lt.w = slices; lt.h = 1; lt.p = lutrow;
			Dump("d24_luttable", lt);
		}
		ImgRGB rgbmip;
		{
			ImgRGB depth = ZCodeStack(stackOut, lutrow);
			stackOut.p.clear(); stackOut.p.shrink_to_fit();
			P.step(0.85, "Colour MIP");
			if (endMIP > slices) endMIP = slices;
			rgbmip = MIPRightColor(depth, jd2i(startMIP), jd2i(endMIP));
		}
		Dump("d25_rgbmip", rgbmip);
		ImgRGB color = rgbmip;
		const bool expand = !(width > height);
		if (colorscale)
		{
			// CreateScale(Glut, 1, slicesOri, reverse0)
			const int ww = 256, hh = 32;
			ImgRGB sc; sc.w = ww; sc.h = hh + 16; sc.p.assign((size_t)sc.w * sc.h, 0);
			{
				std::vector<int> v8((size_t)ww * hh, 255);
				if (!reverse0)
					for (int j = 0; j < hh; j++) for (int i = 0; i < ww; i++) v8[(size_t)j * ww + i] = i;
				else
				{
					int valw = ww;
					for (int j = 0; j < hh; j++)
						for (int i = 0; i < ww; i++) { v8[(size_t)j * ww + i] = std::min(255, std::max(0, valw)); valw = ww - i; }
				}
				for (int j = 0; j < hh; j++) for (int i = 0; i < ww; i++) sc.p[(size_t)j * ww + i] = lut[v8[(size_t)j * ww + i]];
			}
			Dump("d26_scale", sc);
			DrawText(sc, kSansSerif12, "Slices", (int)mround(ww / 2.0) - 12, hh + 16, 0xffffff);
			char b[32];
			snprintf(b, sizeof(b), "%03d", 1);
			DrawText(sc, kSansSerif12, b, 10, hh + 16, 0xffffff);
			std::string endf = std::to_string(slices);
			while (endf.size() < 3) endf = "0" + endf;
			DrawText(sc, kSansSerif12, endf, ww - 30, hh + 16, 0xffffff);
			Dump("d27_scaletext", sc);
			Dump("d28_colorpre", color);
			if (expand) color = CanvasSize(color, width, height + 90, BottomLeft);
			// makeRectangle(width-257, 1, 256, 48); run("Paste")
			for (int y = 0; y < sc.h; y++)
				for (int x = 0; x < sc.w; x++)
				{
					int X = width - 257 + x, Y = 1 + y;
					if (X >= 0 && X < color.w && Y >= 0 && Y < color.h) color.p[(size_t)Y * color.w + X] = sc.p[(size_t)y * sc.w + x];
				}
			Dump("d29_colorpaste", color);
			if (AutoBRV == 1)
			{
				std::string s; int x = 0;
				char buf[64];
				const double av = applyV;
				auto num = [](double v) { return n2s(v); };
				bool draw = false;
				if (av > 999 && av < 10000)
				{
					if (bitd == 16 && DefMaxValue > 4095) { s = "Max: 0" + num(av) + " /65535"; x = width - 210; draw = true; }
					if (bitd == 16 && DefMaxValue <= 4095) { s = "Max: " + num(av) + " /4095"; x = width - 180; draw = true; }
				}
				else if (av > 99 && av < 1000)
				{
					if (bitd == 8) { s = "Max: " + num(av) + " /255"; x = width - 180; draw = true; }
					if (bitd == 16 && DefMaxValue > 4095) { s = "Max: 00" + num(av) + " /65535"; x = width - 210; draw = true; }
					if (bitd == 16 && DefMaxValue <= 4095) { s = "Max: 0" + num(av) + " /4095"; x = width - 180; draw = true; }
				}
				else if (av < 100)
				{
					if (bitd == 8) { s = "Max: 0" + num(av) + " /255"; x = width - 180; draw = true; }
					if (bitd == 16 && DefMaxValue <= 4095) { s = "Max: 00" + num(av) + " /4095"; x = width - 180; draw = true; }
					if (bitd == 16 && DefMaxValue > 4095) { s = "Max: 000" + num(av) + " /65535"; x = width - 210; draw = true; }
				}
				else if (av > 9999) { s = "Max: " + num(av) + " /65535"; x = width - 210; draw = true; }
				(void)buf;
				if (draw) DrawText(color, kArial20, s, x, 78, 0xffffff);
			}
			Dump("d30_colortext", color);
		}
		log("Line 1724");
		// result
		res.width = color.w; res.height = color.h;
		res.rgb.resize((size_t)color.w * color.h * 3);
		for (size_t i = 0; i < color.p.size(); i++)
		{
			uint32_t c = color.p[i];
			res.rgb[i * 3] = (uint8_t)((c >> 16) & 0xff);
			res.rgb[i * 3 + 1] = (uint8_t)((c >> 8) & 0xff);
			res.rgb[i * 3 + 2] = (uint8_t)(c & 0xff);
		}
		res.applyV = applyV;
		res.sigsize = sigsize;
		res.sigsizethre = sigsizethre;
		res.Inimax = Inimax; res.Inimin = Inimin; res.DefMaxValue = DefMaxValue;
		res.bitd = bitd;
		res.ok = true;
		P.step(1.0, "Done");
	}
	catch (Canceled&)
	{
		res.ok = false; res.canceled = true; res.error = "canceled";
	}
	catch (Failed& f)
	{
		res.ok = false; res.error = f.msg;
	}
	catch (std::bad_alloc&)
	{
		res.ok = false; res.error = "not enough memory";
	}
	// Compute runs on a worker thread, where an escaping exception would
	// terminate the application
	catch (std::exception& e)
	{
		res.ok = false; res.error = e.what();
	}
	catch (...)
	{
		res.ok = false; res.error = "unexpected error";
	}
	double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	{
		char b[64]; snprintf(b, sizeof(b), "C++ time %.2f s", secs);
		log(b);
	}
	res.log = log.text;
	return res.ok;
}

// ===========================================================================
// output
// ===========================================================================

bool WriteTiff(const std::wstring& path, const Result& res, std::string* error)
{
	if (!res.ok || res.width <= 0 || res.height <= 0) { if (error) *error = "no image"; return false; }
	TIFF* tif = OpenTiff(path, "w");
	if (!tif) { if (error) *error = "cannot create " + Utf8(path); return false; }
	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t)res.width);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t)res.height);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_PACKBITS);
	TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));
	TIFFSetField(tif, TIFFTAG_XRESOLUTION, 1.0);
	TIFFSetField(tif, TIFFTAG_YRESOLUTION, 1.0);
	TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_NONE);
	TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, "ImageJ=1.54p\nunit=pixel\n");
	TIFFSetField(tif, TIFFTAG_SOFTWARE, "VVDViewer Color Depth MIP");
	bool ok = true;
	for (int y = 0; y < res.height && ok; y++)
		if (TIFFWriteScanline(tif, (void*)(res.rgb.data() + (size_t)y * res.width * 3), (uint32_t)y, 0) < 0) ok = false;
	TIFFClose(tif);
	if (!ok && error) *error = "write error " + Utf8(path);
	return ok;
}

std::wstring DefaultFileName(const std::wstring& volumeName)
{
	std::wstring origi = volumeName;
	size_t slash = origi.find_last_of(L"/\\");
	if (slash != std::wstring::npos) origi = origi.substr(slash + 1);
	size_t dot = origi.rfind(L'.');
	std::wstring origiMIP = dot != std::wstring::npos ? origi.substr(0, dot) : origi;
	size_t qi = origiMIP.rfind(L"_QI");
	if (qi != std::wstring::npos) origiMIP = origiMIP.substr(0, qi);
	return origiMIP + L".tif";
}

std::wstring DataDir()
{
	if (const char* e = getenv("VVD_CDM_DATA"))
		if (*e)
		{
			std::string s(e);
			return std::wstring(s.begin(), s.end());
		}
#ifndef CDM_STANDALONE
	return vvd::JoinPath(vvd::GetResourcesDir(), vvd::String(L"ColorDepthMIP"));
#else
	return L"ColorDepthMIP";
#endif
}

#ifndef CDM_STANDALONE
bool GetVolume(VolumeData* vd, Volume& vol, std::string& error)
{
	vol = Volume();
	if (!vd) { error = "no volume selected"; return false; }
	if (vd->isBrxml())
	{
		error = "the full-resolution data of this volume are not in memory (multi-resolution / brick-streamed data); open the original stack to make a Color Depth MIP";
		return false;
	}
	auto vl = vd->GetVolume(false);
	Nrrd* nrrd = vl ? vl->getNrrd() : nullptr;
	if (!nrrd || !nrrd->data || nrrd->dim < 3)
	{
		error = "the voxel data of this volume are not in memory";
		return false;
	}
	vol.nx = (int)nrrd->axis[0].size;
	vol.ny = (int)nrrd->axis[1].size;
	vol.nz = (int)nrrd->axis[2].size;
	switch (nrrd->type)
	{
	case nrrdTypeUChar: case nrrdTypeChar: vol.bits = 8; break;
	case nrrdTypeUShort: case nrrdTypeShort: vol.bits = 16; break;
	case nrrdTypeFloat: vol.bits = 32; break;
	default:
		error = "unsupported voxel type (8-bit, 16-bit or 32-bit float required)";
		return false;
	}
	vol.data = nrrd->data;
	if (vol.nz < 2)
	{
		error = "the volume has a single slice; a stack is required";
		return false;
	}
	return true;
}

void LoadOptions(const vvd::IniFile& cfg, Options& o)
{
	int i; bool b;
	if (cfg.Read(vvd::String(L"/color depth mip/mode"), i) && i >= 0 && i <= 4) o.mode = i;
	if (cfg.Read(vvd::String(L"/color depth mip/color scale"), b)) o.colorScale = b;
	if (cfg.Read(vvd::String(L"/color depth mip/reversed"), b)) o.reversed = b;
	if (cfg.Read(vvd::String(L"/color depth mip/gamma"), b)) o.gamma14 = b;
	if (cfg.Read(vvd::String(L"/color depth mip/skeleton bolded"), b)) o.skeletonBolded = b;
	if (cfg.Read(vvd::String(L"/color depth mip/dslt line"), b)) o.dsltLine = b;
	if (cfg.Read(vvd::String(L"/color depth mip/start slice"), i)) o.startMIP = i;
	if (cfg.Read(vvd::String(L"/color depth mip/end slice"), i)) o.endMIP = i;
	if (cfg.Read(vvd::String(L"/color depth mip/desired mean"), i) && i > 0 && i < 256) o.desiredMean = i;
}

void SaveOptions(vvd::IniFile& cfg, const Options& o)
{
	cfg.Write(vvd::String(L"/color depth mip/mode"), o.mode);
	cfg.Write(vvd::String(L"/color depth mip/color scale"), o.colorScale);
	cfg.Write(vvd::String(L"/color depth mip/reversed"), o.reversed);
	cfg.Write(vvd::String(L"/color depth mip/gamma"), o.gamma14);
	cfg.Write(vvd::String(L"/color depth mip/skeleton bolded"), o.skeletonBolded);
	cfg.Write(vvd::String(L"/color depth mip/dslt line"), o.dsltLine);
	cfg.Write(vvd::String(L"/color depth mip/start slice"), o.startMIP);
	cfg.Write(vvd::String(L"/color depth mip/end slice"), o.endMIP);
	cfg.Write(vvd::String(L"/color depth mip/desired mean"), o.desiredMean);
}
#endif

}	// namespace cdm
