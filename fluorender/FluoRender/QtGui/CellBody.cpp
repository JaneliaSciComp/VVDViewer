#include "CellBody.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

namespace
{
	constexpr double kFar = 1.0e30;

	// voxels above thr per block; z slabs of blocks on every core
	template <typename T>
	bool CountBlocks(const T* data, size_t nx, size_t ny, size_t nz, size_t d, T thr,
		size_t gx, size_t gy, size_t gz, std::vector<uint32_t>& count, const std::atomic<bool>* cancel)
	{
		std::atomic<size_t> next{ 0 };
		std::atomic<bool> stopped{ false };
		auto work = [&]() {
			for (;;)
			{
				const size_t zb = next.fetch_add(1);
				if (zb >= gz)
					return;
				if (cancel && cancel->load())
				{
					stopped = true;
					return;
				}
				uint32_t* cz = count.data() + zb * gx * gy;
				const size_t z1 = std::min(nz, (zb + 1) * d);
				for (size_t z = zb * d; z < z1; z++)
					for (size_t y = 0; y < ny; y++)
					{
						const T* row = data + (z * ny + y) * nx;
						uint32_t* cy = cz + (y / d) * gx;
						for (size_t xb = 0; xb < gx; xb++)
						{
							const size_t x0 = xb * d, x1 = std::min(nx, x0 + d);
							uint32_t c = 0;
							for (size_t x = x0; x < x1; x++)
								c += row[x] > thr;
							cy[xb] += c;
						}
					}
			}
		};
		const size_t n = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), gz);
		std::vector<std::thread> pool;
		for (size_t i = 1; i < n; i++)
			pool.emplace_back(work);
		work();
		for (std::thread& t : pool)
			t.join();
		return !stopped;
	}

	// 1D squared distance transform (Felzenszwalb & Huttenlocher) of f, with
	// samples w apart (w2 = w^2): out[p] = min over q of w2 (p - q)^2 + f[q]
	void Edt1D(const double* f, double* out, int n, double w2, std::vector<int>& v, std::vector<double>& z)
	{
		v.resize(n);
		z.resize(n + 1);
		int k = 0;
		v[0] = 0;
		z[0] = -kFar;
		z[1] = kFar;
		auto cross = [&](int q, int r) {
			return ((f[q] + w2 * double(q) * q) - (f[r] + w2 * double(r) * r)) / (2.0 * w2 * (q - r));
		};
		for (int q = 1; q < n; q++)
		{
			double s = cross(q, v[k]);
			while (s <= z[k])
			{
				k--;
				s = cross(q, v[k]);
			}
			k++;
			v[k] = q;
			z[k] = s;
			z[k + 1] = kFar;
		}
		k = 0;
		for (int q = 0; q < n; q++)
		{
			while (z[k + 1] < q)
				k++;
			const double dq = double(q - v[k]);
			out[q] = w2 * dq * dq + f[v[k]];
		}
	}

	template <typename T>
	T Threshold(double thr)
	{
		if (std::numeric_limits<T>::is_integer)
		{
			if (!(thr > 0.0)) return T(0);
			if (thr >= double(std::numeric_limits<T>::max())) return std::numeric_limits<T>::max();
			return T(std::floor(thr));   // v > thr == v > floor(thr) for whole v
		}
		return T(thr);
	}

	template <typename T>
	VVDCellBody Find(const T* data, size_t nx, size_t ny, size_t nz, double spcx, double spcy, double spcz,
		double threshold, const std::atomic<bool>* cancel)
	{
		VVDCellBody r;
		const size_t longest = std::max(nx, std::max(ny, nz));
		const size_t d = std::max<size_t>(1, (longest + 319) / 320);
		r.block = int(d);
		const size_t gx = (nx + d - 1) / d, gy = (ny + d - 1) / d, gz = (nz + d - 1) / d;
		std::vector<uint32_t> count(gx * gy * gz, 0);
		if (!CountBlocks(data, nx, ny, nz, d, Threshold<T>(threshold), gx, gy, gz, count, cancel))
			return r;

		// inside: half of a block's voxels (the edge blocks have fewer); none
		// at all (a thin object): any voxel
		auto voxels = [&](size_t b, size_t n) { return std::min(n, (b + 1) * d) - b * d; };
		const size_t px = gx + 2, py = gy + 2, pz = gz + 2;   // a border of background
		std::vector<double> f(px * py * pz, 0.0);
		auto build = [&](bool half) {
			bool any = false;
			for (size_t zb = 0; zb < gz; zb++)
				for (size_t yb = 0; yb < gy; yb++)
					for (size_t xb = 0; xb < gx; xb++)
					{
						const uint32_t c = count[(zb * gy + yb) * gx + xb];
						const size_t full = voxels(xb, nx) * voxels(yb, ny) * voxels(zb, nz);
						const bool in = half ? 2 * size_t(c) >= full && c > 0 : c > 0;
						f[((zb + 1) * py + yb + 1) * px + xb + 1] = in ? kFar : 0.0;
						any = any || in;
					}
			return any;
		};
		if (!build(true) && !build(false))
			return r;

		// exact squared distances, one axis after the other
		const double w2[3] = { std::pow(d * spcx, 2.0), std::pow(d * spcy, 2.0), std::pow(d * spcz, 2.0) };
		std::vector<double> line, out;
		std::vector<int> v;
		std::vector<double> zz;
		auto pass = [&](size_t n, size_t stride, size_t outer1, size_t s1, size_t outer2, size_t s2, double ww) {
			line.resize(n);
			out.resize(n);
			for (size_t a = 0; a < outer1; a++)
				for (size_t b = 0; b < outer2; b++)
				{
					double* base = f.data() + a * s1 + b * s2;
					for (size_t i = 0; i < n; i++) line[i] = base[i * stride];
					Edt1D(line.data(), out.data(), int(n), ww, v, zz);
					for (size_t i = 0; i < n; i++) base[i * stride] = out[i];
				}
		};
		pass(px, 1, pz, px * py, py, px, w2[0]);        // along x
		if (cancel && cancel->load()) return r;
		pass(py, px, pz, px * py, px, 1, w2[1]);        // along y
		if (cancel && cancel->load()) return r;
		pass(pz, px * py, py, px, px, 1, w2[2]);        // along z
		if (cancel && cancel->load()) return r;

		double best = 0.0;
		for (double val : f) best = std::max(best, val);
		if (!(best > 0.0))
			return r;
		// the blocks at the peak (a long soma has several): the one nearest their mean
		const double tol = best * (1.0 - 1e-9);
		double mx = 0.0, my = 0.0, mz = 0.0;
		size_t m = 0;
		for (size_t zb = 1; zb + 1 < pz; zb++)
			for (size_t yb = 1; yb + 1 < py; yb++)
				for (size_t xb = 1; xb + 1 < px; xb++)
					if (f[(zb * py + yb) * px + xb] >= tol)
					{
						mx += double(xb); my += double(yb); mz += double(zb);
						m++;
					}
		mx /= double(m); my /= double(m); mz /= double(m);
		double nearest = std::numeric_limits<double>::max();
		size_t bx = 0, by = 0, bz = 0;
		for (size_t zb = 1; zb + 1 < pz; zb++)
			for (size_t yb = 1; yb + 1 < py; yb++)
				for (size_t xb = 1; xb + 1 < px; xb++)
					if (f[(zb * py + yb) * px + xb] >= tol)
					{
						const double dd = w2[0] * std::pow(xb - mx, 2.0) + w2[1] * std::pow(yb - my, 2.0) + w2[2] * std::pow(zb - mz, 2.0);
						if (dd < nearest) { nearest = dd; bx = xb - 1; by = yb - 1; bz = zb - 1; }
					}
		auto centre = [&](size_t b, size_t n) { return 0.5 * double(b * d + std::min(n, (b + 1) * d)); };
		r.found = true;
		r.x = centre(bx, nx);
		r.y = centre(by, ny);
		r.z = centre(bz, nz);
		r.depth = std::sqrt(best);
		return r;
	}
}

VVDCellBody VVDFindCellBody(const void* data, int bytes, size_t nx, size_t ny, size_t nz,
	double spcx, double spcy, double spcz, double threshold, const std::atomic<bool>* cancel)
{
	if (!data || !nx || !ny || !nz)
		return VVDCellBody();
	if (!(spcx > 0.0)) spcx = 1.0;
	if (!(spcy > 0.0)) spcy = 1.0;
	if (!(spcz > 0.0)) spcz = 1.0;
	switch (bytes)
	{
	case 1: return Find(static_cast<const uint8_t*>(data), nx, ny, nz, spcx, spcy, spcz, threshold, cancel);
	case 2: return Find(static_cast<const uint16_t*>(data), nx, ny, nz, spcx, spcy, spcz, threshold, cancel);
	case 4: return Find(static_cast<const float*>(data), nx, ny, nz, spcx, spcy, spcz, threshold, cancel);
	}
	return VVDCellBody();
}
