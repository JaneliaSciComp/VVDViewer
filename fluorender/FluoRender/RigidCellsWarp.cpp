// RigidCellsWarp: C++ port of the ImageJ plugin Apply_BigWarp_RigidCells.java.
// See RigidCellsWarp.h. Class and function names follow the Java code; every
// floating-point expression keeps the Java evaluation order so the results
// match the plugin.

// Java never contracts a*b+c into a fused multiply-add; neither may we.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#if defined(_MSC_VER)
#pragma fp_contract(off)
#endif

#include "RigidCellsWarp.h"
#ifndef RIGIDCELLS_CORE_ONLY   // defined by stand-alone test harnesses of the algorithm
#include "DataManager.h"
#include "Core/UICallbacks.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rigidcells
{
	const char* LocalModeName(int mode)
	{
		static const char* const names[4] = { "Adaptive (rigid soma, TPS fiber)", "Translation", "Rigid", "Similarity" };
		return mode >= 0 && mode < 4 ? names[mode] : "";
	}
	const char* ObjectModeName(int mode)
	{
		static const char* const names[2] = { "Connected components (26-connected, per label)", "Each label value is one object" };
		return mode >= 0 && mode < 2 ? names[mode] : "";
	}

	namespace
	{
		const char* const TITLE = "Apply BigWarp to Cells (shape preserving)";

		struct Cancelled {};
		struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };

		typedef std::array<double, 3> V3;

		// ---------------------------------------------------------- Java semantics
		int jint(double v)
		{
			if (v != v) return 0;
			if (v >= 2147483647.0) return std::numeric_limits<int>::max();
			if (v <= -2147483648.0) return std::numeric_limits<int>::min();
			return (int)v;
		}
		double jmax(double a, double b)
		{
			if (a != a) return a;
			if (a == 0.0 && b == 0.0) return std::signbit(a) ? b : a;
			return a >= b ? a : b;
		}
		double jmin(double a, double b)
		{
			if (a != a) return a;
			if (a == 0.0 && b == 0.0) return std::signbit(a) ? a : b;
			return a <= b ? a : b;
		}
		long long jround(double a)
		{
			// Math.round(double) (JDK 8): round half up
			double f = std::floor(a);
			double r = (a - f >= 0.5) ? f + 1.0 : f;
			if (r != r) return 0;
			if (r >= 9.2233720368547758e18) return std::numeric_limits<long long>::max();
			if (r <= -9.2233720368547758e18) return std::numeric_limits<long long>::min();
			return (long long)r;
		}
		// Double.compare
		int jcompare(double a, double b)
		{
			if (a < b) return -1;
			if (a > b) return 1;
			long long x, y;
			if (a != a) a = std::numeric_limits<double>::quiet_NaN();
			if (b != b) b = std::numeric_limits<double>::quiet_NaN();
			std::memcpy(&x, &a, 8); std::memcpy(&y, &b, 8);
			if (a != a) x = 0x7ff8000000000000LL;
			if (b != b) y = 0x7ff8000000000000LL;
			return x == y ? 0 : (x < y ? -1 : 1);
		}
		double toDegrees(double rad) { return rad * 180.0 / 3.14159265358979323846; }

		// String.format(Locale.ROOT, "%.Ng", v)
		std::string jfmtg(double v, int prec)
		{
			if (v != v) return "NaN";
			if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%#.*g", prec, v);
			std::string s(buf);
			if (!s.empty() && s.back() == '.') s.pop_back();
			return s;
		}
		std::string fmt(double v) { return jfmtg(v, 4); }
		std::string fmt1f(double v) { char b[64]; std::snprintf(b, sizeof(b), "%.1f", v); return b; }

		// shortest round-trip digits
		std::string shortest(double v)
		{
			char buf[64];
			for (int p = 1; p <= 17; ++p)
			{
				std::snprintf(buf, sizeof(buf), "%.*g", p, v);
				if (std::strtod(buf, nullptr) == v) break;
			}
			return buf;
		}

		int NumThreads(int req)
		{
			int n = req > 0 ? req : (int)std::thread::hardware_concurrency();
			return n < 1 ? 1 : n;
		}

		// fn(i) for i in [0, n) on all threads; deterministic as long as fn(i)
		// only writes what belongs to i
		template <class F>
		void ParallelFor(long long n, int threads, F fn)
		{
			if (n <= 0) return;
			if ((long long)threads > n) threads = (int)n;
			if (threads <= 1)
			{
				for (long long i = 0; i < n; ++i) fn(i);
				return;
			}
			std::atomic<long long> next(0);
			std::exception_ptr err;
			std::mutex m;
			std::vector<std::thread> pool;
			pool.reserve(threads);
			for (int t = 0; t < threads; ++t)
				pool.emplace_back([&]() {
					try
					{
						for (long long i; (i = next.fetch_add(1)) < n; )
							fn(i);
					}
					catch (...)
					{
						std::lock_guard<std::mutex> lk(m);
						if (!err) err = std::current_exception();
						next.store(n);
					}
				});
			for (auto& th : pool) th.join();
			if (err) std::rethrow_exception(err);
		}

		double distance(const double* a, const double* b)
		{
			double s = 0;
			for (int d = 0; d < 3; d++) s += (a[d] - b[d]) * (a[d] - b[d]);
			return std::sqrt(s);
		}
		double distance(const V3& a, const V3& b) { return distance(a.data(), b.data()); }

		// ---------------------------------------------------------- Linear
		typedef std::vector<std::vector<double>> Mat;

		struct Linear
		{
			// Gaussian elimination with partial pivoting; A is destroyed
			static Mat solve(Mat& A, const Mat& B)
			{
				int n = (int)A.size(), m = (int)B[0].size();
				Mat X(B);
				for (int k = 0; k < n; k++)
				{
					int piv = k;
					for (int i = k + 1; i < n; i++) if (std::fabs(A[i][k]) > std::fabs(A[piv][k])) piv = i;
					if (std::fabs(A[piv][k]) < 1e-300) throw Failure("Singular TPS system (degenerate landmarks).");
					std::swap(A[k], A[piv]); std::swap(X[k], X[piv]);
					for (int i = k + 1; i < n; i++)
					{
						double f = A[i][k] / A[k][k];
						if (f == 0) continue;
						for (int j = k; j < n; j++) A[i][j] -= f * A[k][j];
						for (int j = 0; j < m; j++) X[i][j] -= f * X[k][j];
					}
				}
				for (int k = n - 1; k >= 0; k--)
				{
					for (int j = 0; j < m; j++)
					{
						double s = X[k][j];
						for (int i = k + 1; i < n; i++) s -= A[k][i] * X[i][j];
						X[k][j] = s / A[k][k];
					}
				}
				return X;
			}
			static double det3(const double m[3][3])
			{
				return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
			}
			static double signum(double v) { return v > 0 ? 1.0 : v < 0 ? -1.0 : v; }
			// Jacobi eigen-decomposition of a symmetric matrix; eigenvalues
			// descending, V's columns the eigenvectors
			static std::vector<double> jacobi(const Mat& S, Mat& V)
			{
				int n = (int)S.size();
				Mat A(S);
				V.assign(n, std::vector<double>(n, 0.0));
				for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) V[i][j] = i == j ? 1 : 0;
				for (int sweep = 0; sweep < 100; sweep++)
				{
					double off = 0;
					for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) off += A[i][j] * A[i][j];
					if (off < 1e-30) break;
					for (int p = 0; p < n; p++) for (int q = p + 1; q < n; q++)
					{
						if (std::fabs(A[p][q]) < 1e-300) continue;
						double theta = (A[q][q] - A[p][p]) / (2 * A[p][q]);
						double t = signum(theta) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
						if (theta == 0) t = 1;
						double c = 1 / std::sqrt(t * t + 1), s = t * c;
						for (int k = 0; k < n; k++) { double akp = A[k][p], akq = A[k][q]; A[k][p] = c * akp - s * akq; A[k][q] = s * akp + c * akq; }
						for (int k = 0; k < n; k++) { double apk = A[p][k], aqk = A[q][k]; A[p][k] = c * apk - s * aqk; A[q][k] = s * apk + c * aqk; }
						for (int k = 0; k < n; k++) { double vkp = V[k][p], vkq = V[k][q]; V[k][p] = c * vkp - s * vkq; V[k][q] = s * vkp + c * vkq; }
					}
				}
				std::vector<double> ev(n);
				for (int i = 0; i < n; i++) ev[i] = A[i][i];
				std::vector<int> order(n);
				for (int i = 0; i < n; i++) order[i] = i;
				// Arrays.sort(Integer[], (x, y) -> Double.compare(ev[y], ev[x])) is a stable merge sort
				std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return jcompare(ev[y], ev[x]) < 0; });
				std::vector<double> sorted(n);
				Mat W(n, std::vector<double>(n, 0.0));
				for (int j = 0; j < n; j++) { sorted[j] = ev[order[j]]; for (int i = 0; i < n; i++) W[i][j] = V[i][order[j]]; }
				V = W;
				return sorted;
			}
			static std::vector<double> symmetricEigenvalues(const Mat& S) { Mat V; return jacobi(S, V); }
			static std::vector<double> largestEigenvector(const Mat& S)
			{
				Mat V; jacobi(S, V);
				int n = (int)S.size();
				std::vector<double> v(n);
				double norm = 0;
				for (int i = 0; i < n; i++) { v[i] = V[i][0]; norm += v[i] * v[i]; }
				norm = std::sqrt(norm);
				for (int i = 0; i < n; i++) v[i] /= norm;
				return v;
			}
		};

		// ---------------------------------------------------------- Tps
		// 3D thin plate spline, r^2 log r kernel (jitk / BigWarp), optional Tikhonov stiffness
		struct Tps
		{
			std::vector<V3> src;
			std::vector<V3> w;
			double affine[4][3];
			double lambda = 0, stiffness = 0;
			int n = 0;

			static double kernel(double r) { return r <= 0 ? 0 : r * r * std::log(r); }

			Tps(const std::vector<V3>& source, const std::vector<V3>& dest, double stiff)
			{
				if (!std::isfinite(stiff) || stiff < 0) throw Failure("Stiffness must be finite and non-negative.");
				n = (int)source.size(); stiffness = stiff;
				if (n < 4) throw Failure("At least 4 active landmarks are required for 3D.");
				src = source;
				double c[3] = { 0, 0, 0 };
				for (const V3& p : source) for (int d = 0; d < 3; d++) c[d] += p[d] / n;
				double r2 = 0;
				for (const V3& p : source) for (int d = 0; d < 3; d++) r2 += (p[d] - c[d]) * (p[d] - c[d]) / n;
				if (r2 <= 0) throw Failure("Moving landmarks have zero extent.");
				lambda = stiffness * r2;
				Mat L(n + 4, std::vector<double>(n + 4, 0.0)), Y(n + 4, std::vector<double>(3, 0.0));
				for (int i = 0; i < n; i++)
				{
					for (int j = 0; j < i; j++)
					{
						double r = distance(source[i], source[j]);
						if (r < 1e-10 * std::sqrt(r2))
							throw Failure("Duplicate moving landmarks: " + std::to_string(j + 1) + " and " + std::to_string(i + 1));
						L[i][j] = L[j][i] = kernel(r);
					}
					L[i][i] = lambda;
					L[i][n] = L[n][i] = 1;
					for (int d = 0; d < 3; d++) { L[i][n + 1 + d] = L[n + 1 + d][i] = source[i][d]; Y[i][d] = dest[i][d]; }
				}
				Mat X = Linear::solve(L, Y);
				w.resize(n);
				for (int i = 0; i < n; i++) for (int d = 0; d < 3; d++) w[i][d] = X[i][d];
				for (int i = 0; i < 4; i++) for (int d = 0; d < 3; d++) affine[i][d] = X[n + i][d];
				for (const auto& row : X) for (double v : row) if (!std::isfinite(v)) throw Failure("TPS fit failed (landmarks coplanar or degenerate?).");
			}
			V3 apply(const V3& p) const
			{
				V3 q;
				for (int d = 0; d < 3; d++) q[d] = affine[0][d] + affine[1][d] * p[0] + affine[2][d] * p[1] + affine[3][d] * p[2];
				for (int i = 0; i < n; i++)
				{
					double u = kernel(distance(p, src[i]));
					for (int d = 0; d < 3; d++) q[d] += w[i][d] * u;
				}
				return q;
			}
			// J[d][c] = d q_d / d p_c
			void jacobian(const V3& p, double J[3][3]) const
			{
				for (int d = 0; d < 3; d++) for (int c = 0; c < 3; c++) J[d][c] = affine[1 + c][d];
				for (int i = 0; i < n; i++)
				{
					double r2 = 0;
					for (int c = 0; c < 3; c++) r2 += (p[c] - src[i][c]) * (p[c] - src[i][c]);
					if (r2 <= 1e-300) continue;
					double f = std::log(r2) + 1;
					for (int d = 0; d < 3; d++) for (int c = 0; c < 3; c++) J[d][c] += w[i][d] * f * (p[c] - src[i][c]);
				}
			}
			double jacobianDeterminant(const V3& p) const { double J[3][3]; jacobian(p, J); return Linear::det3(J); }
			double rms(const std::vector<V3>& m, const std::vector<V3>& t) const
			{
				double s = 0;
				for (size_t i = 0; i < m.size(); i++) { double e = distance(apply(m[i]), t[i]); s += e * e; }
				return std::sqrt(s / m.size());
			}
		};

		// ---------------------------------------------------------- Procrustes
		struct Fit
		{
			double rotation[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
			double scale = 1;
			double translation[3] = { 0, 0, 0 };
			V3 apply(const V3& p) const
			{
				V3 q;
				for (int d = 0; d < 3; d++) q[d] = translation[d] + scale * (rotation[d][0] * p[0] + rotation[d][1] * p[1] + rotation[d][2] * p[2]);
				return q;
			}
			V3 inverse(const V3& q) const
			{
				V3 p;
				for (int d = 0; d < 3; d++) p[d] = (rotation[0][d] * (q[0] - translation[0]) + rotation[1][d] * (q[1] - translation[1]) + rotation[2][d] * (q[2] - translation[2])) / scale;
				return p;
			}
			double degrees() const
			{
				return toDegrees(std::acos(jmax(-1, jmin(1, (rotation[0][0] + rotation[1][1] + rotation[2][2] - 1) / 2))));
			}
		};

		struct Procrustes
		{
			// Horn 1987 (quaternion / 4x4 eigenvector) from M = sum a' b'^T
			static void rotationFromCrossCovariance(const double M[3][3], double R[3][3])
			{
				double Sxx = M[0][0], Sxy = M[0][1], Sxz = M[0][2], Syx = M[1][0], Syy = M[1][1], Syz = M[1][2], Szx = M[2][0], Szy = M[2][1], Szz = M[2][2];
				Mat N = {
					{ Sxx + Syy + Szz, Syz - Szy, Szx - Sxz, Sxy - Syx },
					{ Syz - Szy, Sxx - Syy - Szz, Sxy + Syx, Szx + Sxz },
					{ Szx - Sxz, Sxy + Syx, -Sxx + Syy - Szz, Syz + Szy },
					{ Sxy - Syx, Szx + Sxz, Syz + Szy, -Sxx - Syy + Szz } };
				std::vector<double> q = Linear::largestEigenvector(N);
				double q0 = q[0], qx = q[1], qy = q[2], qz = q[3];
				double r[3][3] = {
					{ q0 * q0 + qx * qx - qy * qy - qz * qz, 2 * (qx * qy - q0 * qz), 2 * (qx * qz + q0 * qy) },
					{ 2 * (qy * qx + q0 * qz), q0 * q0 - qx * qx + qy * qy - qz * qz, 2 * (qy * qz - q0 * qx) },
					{ 2 * (qz * qx - q0 * qy), 2 * (qz * qy + q0 * qx), q0 * q0 - qx * qx - qy * qy + qz * qz } };
				std::memcpy(R, r, sizeof(r));
			}
			static Fit fit(const std::vector<V3>& a, const std::vector<V3>& b, int mode)
			{
				int n = (int)a.size();
				double ca[3] = { 0, 0, 0 }, cb[3] = { 0, 0, 0 };
				for (int i = 0; i < n; i++) for (int d = 0; d < 3; d++) { ca[d] += a[i][d] / n; cb[d] += b[i][d] / n; }
				Fit f;
				if (mode > TRANSLATION)
				{
					double M[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
					double sa = 0;
					for (int i = 0; i < n; i++) for (int r = 0; r < 3; r++)
					{
						sa += (a[i][r] - ca[r]) * (a[i][r] - ca[r]);
						for (int c = 0; c < 3; c++) M[r][c] += (a[i][r] - ca[r]) * (b[i][c] - cb[c]);
					}
					rotationFromCrossCovariance(M, f.rotation);
					if (mode == SIMILARITY)
					{
						double sb = 0;
						for (int i = 0; i < n; i++)
						{
							double ra[3] = { 0, 0, 0 };
							for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) ra[r] += f.rotation[r][c] * (a[i][c] - ca[c]);
							for (int r = 0; r < 3; r++) sb += ra[r] * (b[i][r] - cb[r]);
						}
						f.scale = sa > 0 ? sb / sa : 1;
						if (!(f.scale > 0) || !std::isfinite(f.scale)) f.scale = 1;
					}
				}
				for (int d = 0; d < 3; d++) f.translation[d] = cb[d] - f.scale * (f.rotation[d][0] * ca[0] + f.rotation[d][1] * ca[1] + f.rotation[d][2] * ca[2]);
				return f;
			}
		};

		// ---------------------------------------------------------- Runs
		// run-length encoded label volume with 26-connected component labelling
		struct Runs
		{
			int width = 0, height = 0, depth = 0;
			int runCount = 0;
			std::vector<int> runStart, runEnd, runY, runZ, runLabel, runComponent;
			std::vector<int> rowFirst, rowCount;   // runs of row (z*height+y): [rowFirst, rowFirst+rowCount), ascending x
			int componentCount = 0;
			std::vector<int> componentLabel;
			std::vector<std::array<int, 3>> componentMin, componentMax;
			std::vector<long long> componentVoxels;

			static void readRow(const Volume& v, int y, int z, std::vector<int>& out)
			{
				const size_t base = ((size_t)z * v.ny + y) * v.nx;
				if (v.bytes == 1) { const uint8_t* b = (const uint8_t*)v.data + base; for (int x = 0; x < v.nx; x++) out[x] = b[x]; }
				else if (v.bytes == 2) { const uint16_t* s = (const uint16_t*)v.data + base; for (int x = 0; x < v.nx; x++) out[x] = s[x]; }
				else
				{
					const float* f = (const float*)v.data + base;
					for (int x = 0; x < v.nx; x++)
					{
						float fv = f[x];
						if (fv != std::rint(fv) || fv < 0 || fv > 2147483647.0)
							throw Failure("32-bit moving image must contain non-negative integer label values.");
						out[x] = (int)fv;
					}
				}
			}

			static int find(std::vector<int>& p, int i) { while (p[i] != i) { p[i] = p[p[i]]; i = p[i]; } return i; }
			static void unite(std::vector<int>& p, int a, int b) { a = find(p, a); b = find(p, b); if (a != b) p[std::max(a, b)] = std::min(a, b); }

			void unionRow(std::vector<int>& parent, int i, int y, int z)
			{
				if (y < 0 || y >= height || z < 0) return;
				int row = z * height + y;
				int s = runStart[i] - 1, e = runEnd[i] + 1;
				for (int j = rowFirst[row], je = rowFirst[row] + rowCount[row]; j < je; j++)
				{
					if (runStart[j] > e) break;
					if (runEnd[j] < s || runLabel[j] != runLabel[i]) continue;
					unite(parent, i, j);
				}
			}

			void build(const Volume& v, bool perLabel, int minVoxels)
			{
				width = v.nx; height = v.ny; depth = v.nz;
				rowFirst.assign((size_t)height * depth, 0);
				rowCount.assign((size_t)height * depth, 0);
				std::vector<int> row(width);
				for (int z = 0; z < depth; z++) for (int y = 0; y < height; y++)
				{
					readRow(v, y, z, row);
					int r = z * height + y;
					rowFirst[r] = runCount;
					int x = 0;
					while (x < width)
					{
						if (row[x] == 0) { x++; continue; }
						int start = x, label = row[x];
						while (x < width && row[x] == label) x++;
						if (runCount == std::numeric_limits<int>::max()) throw Failure("Too many runs.");
						runStart.push_back(start); runEnd.push_back(x - 1); runY.push_back(y); runZ.push_back(z); runLabel.push_back(label);
						runCount++;
					}
					rowCount[r] = runCount - rowFirst[r];
				}
				if (runCount == 0) throw Failure("The moving image contains no non-zero voxels.");
				std::vector<int> parent(runCount);
				for (int i = 0; i < runCount; i++) parent[i] = i;
				if (perLabel)
				{
					std::unordered_map<int, int> first;
					for (int i = 0; i < runCount; i++)
					{
						auto it = first.find(runLabel[i]);
						if (it == first.end()) first.emplace(runLabel[i], i); else parent[i] = it->second;
					}
				}
				else
				{
					for (int i = 0; i < runCount; i++)
					{
						int y = runY[i], z = runZ[i];
						unionRow(parent, i, y - 1, z);
						if (z > 0) { unionRow(parent, i, y - 1, z - 1); unionRow(parent, i, y, z - 1); unionRow(parent, i, y + 1, z - 1); }
					}
				}
				std::vector<int> root(runCount);
				for (int i = 0; i < runCount; i++) root[i] = find(parent, i);
				std::vector<long long> voxels(runCount, 0);
				for (int i = 0; i < runCount; i++) voxels[root[i]] += runEnd[i] - runStart[i] + 1;
				std::vector<int> compOf(runCount, -1);
				int count = 0;
				for (int i = 0; i < runCount; i++) if (root[i] == i && voxels[i] >= minVoxels) compOf[i] = count++;
				componentCount = count;
				componentLabel.assign(count, 0); componentVoxels.assign(count, 0);
				componentMin.assign(count, { 0, 0, 0 }); componentMax.assign(count, { 0, 0, 0 });
				std::vector<char> seen(count, 0);
				runComponent.assign(runCount, -1);
				for (int i = 0; i < runCount; i++)
				{
					int c = compOf[root[i]];
					runComponent[i] = c;
					if (c < 0) continue;
					componentLabel[c] = runLabel[i]; componentVoxels[c] = voxels[root[i]];
					if (!seen[c]) { seen[c] = 1; componentMin[c] = { runStart[i], runY[i], runZ[i] }; componentMax[c] = { runEnd[i], runY[i], runZ[i] }; }
					else
					{
						componentMin[c][0] = std::min(componentMin[c][0], runStart[i]); componentMax[c][0] = std::max(componentMax[c][0], runEnd[i]);
						componentMin[c][1] = std::min(componentMin[c][1], runY[i]); componentMax[c][1] = std::max(componentMax[c][1], runY[i]);
						componentMin[c][2] = std::min(componentMin[c][2], runZ[i]); componentMax[c][2] = std::max(componentMax[c][2], runZ[i]);
					}
				}
				if (count == 0) throw Failure("No object reaches the minimum voxel count.");
			}

			// component id at a voxel, or -1
			int componentAt(int x, int y, int z) const
			{
				int row = z * height + y;
				int lo = 0, hi = rowCount[row] - 1, base = rowFirst[row];
				while (lo <= hi)
				{
					int mid = (int)((unsigned)(lo + hi) >> 1), j = base + mid;
					if (x < runStart[j]) hi = mid - 1; else if (x > runEnd[j]) lo = mid + 1; else return runComponent[j];
				}
				return -1;
			}
			std::vector<std::vector<int>> runsPerComponent() const
			{
				std::vector<std::vector<int>> out(componentCount);
				std::vector<int> n(componentCount, 0);
				for (int i = 0; i < runCount; i++) if (runComponent[i] >= 0) n[runComponent[i]]++;
				for (int c = 0; c < componentCount; c++) out[c].reserve(n[c]);
				for (int i = 0; i < runCount; i++) { int c = runComponent[i]; if (c >= 0) out[c].push_back(i); }
				return out;
			}
			// is a physical point within radius of any voxel centre of component c?
			bool near(int c, const std::vector<int>& runList, const V3& p, const Grid& g, double radius) const
			{
				for (int d = 0; d < 3; d++)
				{
					double lo = g.phys(d, componentMin[c][d]) - radius, hi = g.phys(d, componentMax[c][d]) + radius;
					if (p[d] < lo || p[d] > hi) return false;
				}
				double r2 = radius * radius;
				for (int i : runList)
				{
					double dy = g.phys(1, runY[i]) - p[1], dz = g.phys(2, runZ[i]) - p[2];
					double yz = dy * dy + dz * dz;
					if (yz > r2) continue;
					double x0 = g.phys(0, runStart[i]), x1 = g.phys(0, runEnd[i]);
					double dx = p[0] < x0 ? x0 - p[0] : p[0] > x1 ? p[0] - x1 : 0;
					if (yz + dx * dx <= r2) return true;
				}
				return false;
			}
		};

		// ---------------------------------------------------------- Thickness
		// local thickness (radius of the largest inscribed sphere covering a point) of one object
		struct Thickness
		{
			int dims[3];
			double spacing[3], origin[3];
			std::vector<float> thick;
			double max = 0;

			static void edt1(double* f, int n, double s, double* d, int* v, double* z, double INF)
			{
				int k = 0; v[0] = 0;
				z[0] = -std::numeric_limits<double>::infinity(); z[1] = std::numeric_limits<double>::infinity();
				double s2 = s * s;
				for (int q = 1; q < n; q++)
				{
					int vk = v[k];
					double sInt = ((f[q] + s2 * q * q) - (f[vk] + s2 * vk * vk)) / (2 * s2 * (q - vk));
					while (sInt <= z[k]) { k--; vk = v[k]; sInt = ((f[q] + s2 * q * q) - (f[vk] + s2 * vk * vk)) / (2 * s2 * (q - vk)); }
					k++; v[k] = q; z[k] = sInt; z[k + 1] = std::numeric_limits<double>::infinity();
				}
				k = 0;
				for (int q = 0; q < n; q++) { while (z[k + 1] < q) k++; int vk = v[k]; d[q] = jmin(INF, s2 * (q - vk) * (q - vk) + f[vk]); }
			}
			// separable squared EDT (Felzenszwalb & Huttenlocher) in place, physical units
			static void edt3(std::vector<float>& g, const int dims[3], const double sp[3], double INF, int threads)
			{
				const int nx = dims[0], ny = dims[1], nz = dims[2];
				const int n = std::max(nx, std::max(ny, nz));
				float* G = g.data();
				struct Scratch { std::vector<double> f, d, z; std::vector<int> v; };
				auto pass = [&](int count, const std::function<void(Scratch&, int)>& body) {
					ParallelFor(count, threads, [&](long long i) {
						thread_local Scratch s;
						if ((int)s.f.size() < n) { s.f.resize(n); s.d.resize(n); s.z.resize(n + 1); s.v.resize(n); }
						body(s, (int)i);
					});
				};
				pass(nz, [&](Scratch& s, int z) {
					for (int y = 0; y < ny; y++)
					{
						size_t base = ((size_t)z * ny + y) * nx;
						for (int x = 0; x < nx; x++) s.f[x] = G[base + x];
						edt1(s.f.data(), nx, sp[0], s.d.data(), s.v.data(), s.z.data(), INF);
						for (int x = 0; x < nx; x++) G[base + x] = (float)s.d[x];
					}
				});
				pass(nz, [&](Scratch& s, int z) {
					for (int x = 0; x < nx; x++)
					{
						size_t base = (size_t)z * ny * nx + x;
						for (int y = 0; y < ny; y++) s.f[y] = G[base + (size_t)y * nx];
						edt1(s.f.data(), ny, sp[1], s.d.data(), s.v.data(), s.z.data(), INF);
						for (int y = 0; y < ny; y++) G[base + (size_t)y * nx] = (float)s.d[y];
					}
				});
				pass(ny, [&](Scratch& s, int y) {
					for (int x = 0; x < nx; x++)
					{
						size_t base = (size_t)y * nx + x, step = (size_t)ny * nx;
						for (int z = 0; z < nz; z++) s.f[z] = G[base + z * step];
						edt1(s.f.data(), nz, sp[2], s.d.data(), s.v.data(), s.z.data(), INF);
						for (int z = 0; z < nz; z++) G[base + z * step] = (float)s.d[z];
					}
				});
			}

			static Thickness of(const Runs& runs, const std::vector<int>& runList, const Grid& mg,
				const std::array<int, 3>& bmin, const std::array<int, 3>& bmax, double margin, int threads)
			{
				long long voxels = 1;
				for (int d = 0; d < 3; d++) voxels *= (bmax[d] - bmin[d] + 1);
				int f = (int)jmax(1, std::ceil(std::cbrt(voxels / 1.2e7)));
				int pad = 2;
				Thickness t;
				for (int d = 0; d < 3; d++)
				{
					t.dims[d] = (bmax[d] - bmin[d]) / f + 1 + 2 * pad;
					t.spacing[d] = mg.spacing[d] * f;
					t.origin[d] = mg.phys(d, bmin[d]) - pad * t.spacing[d];
				}
				const int nx = t.dims[0], ny = t.dims[1], nz = t.dims[2];
				const size_t total = (size_t)nx * ny * nz;
				const double INF = 1e10;
				std::vector<float> dist(total, 0.0f);
				for (int ri : runList)
				{
					int y = (runs.runY[ri] - bmin[1]) / f + pad, z = (runs.runZ[ri] - bmin[2]) / f + pad;
					for (int x = runs.runStart[ri]; x <= runs.runEnd[ri]; x++) dist[((size_t)z * ny + y) * nx + (x - bmin[0]) / f + pad] = (float)INF;
				}
				edt3(dist, t.dims, t.spacing, INF, threads);
				double maxR = 0;
				for (float v : dist) if (v < 1e9) maxR = jmax(maxR, std::sqrt((double)v));
				t.thick.assign(total, 0.0f);
				const int bins = 8;
				std::vector<float> work(total);
				for (int b = 0; b < bins; b++)
				{
					double R = maxR * (bins - b) / bins;
					bool any = false;
					for (size_t i = 0; i < total; i++)
					{
						bool in = dist[i] < 1e9 && std::sqrt((double)dist[i]) >= R;
						work[i] = in ? 0 : (float)INF;
						any |= in;
					}
					if (!any) continue;
					edt3(work, t.dims, t.spacing, INF, threads);
					double limit = (1.5 * R + margin) * (1.5 * R + margin);
					for (size_t i = 0; i < total; i++) if (work[i] <= limit && t.thick[i] < R) t.thick[i] = (float)R;
				}
				t.max = maxR;
				return t;
			}

			float at(const V3& p) const
			{
				size_t idx = 0, stride = 1;
				for (int d = 0; d < 3; d++)
				{
					int i = jint(std::floor((p[d] - origin[d]) / spacing[d] + 0.5));
					if (i < 0) i = 0;
					if (i >= dims[d]) i = dims[d] - 1;
					idx += (size_t)i * stride; stride *= dims[d];
				}
				return thick[idx];
			}
		};

		// ---------------------------------------------------------- OpenCL (loaded at run time)
		typedef int32_t cl_int;
		typedef uint32_t cl_uint;
		typedef uint64_t cl_ulong;
		typedef cl_ulong cl_bitfield;
		typedef cl_bitfield cl_device_type;
		typedef cl_uint cl_device_info;
		typedef cl_uint cl_program_build_info;
		typedef cl_bitfield cl_mem_flags;
		typedef cl_bitfield cl_command_queue_properties;
		typedef intptr_t cl_context_properties;
		typedef cl_uint cl_bool;
		typedef struct _cl_platform_id* cl_platform_id;
		typedef struct _cl_device_id* cl_device_id;
		typedef struct _cl_context* cl_context;
		typedef struct _cl_command_queue* cl_command_queue;
		typedef struct _cl_mem* cl_mem;
		typedef struct _cl_program* cl_program;
		typedef struct _cl_kernel* cl_kernel;
		typedef struct _cl_event* cl_event;
		const cl_int CL_SUCCESS = 0, CL_DEVICE_NOT_FOUND = -1;
		const cl_device_type CL_DEVICE_TYPE_GPU = 1 << 2;
		const cl_device_info CL_DEVICE_NAME = 0x102B;
		const cl_context_properties CL_CONTEXT_PLATFORM = 0x1084;
		const cl_mem_flags CL_MEM_WRITE_ONLY = 1 << 1, CL_MEM_READ_ONLY = 1 << 2, CL_MEM_COPY_HOST_PTR = 1 << 5;
		const cl_program_build_info CL_PROGRAM_BUILD_LOG = 0x1183;
		const cl_bool CL_TRUE = 1;
#if defined(_WIN32)
#define VVD_CL_CALL __stdcall
#else
#define VVD_CL_CALL
#endif

		struct ClApi
		{
			bool ok = false;
			cl_int (VVD_CL_CALL* GetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*) = nullptr;
			cl_int (VVD_CL_CALL* GetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*) = nullptr;
			cl_int (VVD_CL_CALL* GetDeviceInfo)(cl_device_id, cl_device_info, size_t, void*, size_t*) = nullptr;
			cl_context (VVD_CL_CALL* CreateContext)(const cl_context_properties*, cl_uint, const cl_device_id*, void*, void*, cl_int*) = nullptr;
			cl_command_queue (VVD_CL_CALL* CreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int*) = nullptr;
			cl_program (VVD_CL_CALL* CreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*) = nullptr;
			cl_int (VVD_CL_CALL* BuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*, void*, void*) = nullptr;
			cl_int (VVD_CL_CALL* GetProgramBuildInfo)(cl_program, cl_device_id, cl_program_build_info, size_t, void*, size_t*) = nullptr;
			cl_kernel (VVD_CL_CALL* CreateKernel)(cl_program, const char*, cl_int*) = nullptr;
			cl_mem (VVD_CL_CALL* CreateBuffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*) = nullptr;
			cl_int (VVD_CL_CALL* SetKernelArg)(cl_kernel, cl_uint, size_t, const void*) = nullptr;
			cl_int (VVD_CL_CALL* EnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*, const size_t*, cl_uint, const cl_event*, cl_event*) = nullptr;
			cl_int (VVD_CL_CALL* EnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void*, cl_uint, const cl_event*, cl_event*) = nullptr;
			cl_int (VVD_CL_CALL* Finish)(cl_command_queue) = nullptr;
			cl_int (VVD_CL_CALL* ReleaseMemObject)(cl_mem) = nullptr;
			cl_int (VVD_CL_CALL* ReleaseKernel)(cl_kernel) = nullptr;
			cl_int (VVD_CL_CALL* ReleaseProgram)(cl_program) = nullptr;
			cl_int (VVD_CL_CALL* ReleaseCommandQueue)(cl_command_queue) = nullptr;
			cl_int (VVD_CL_CALL* ReleaseContext)(cl_context) = nullptr;

			static ClApi& get()
			{
				static ClApi api;
				static std::once_flag once;
				std::call_once(once, []() { api.load(); });
				return api;
			}
		private:
			void* lib = nullptr;
			void* sym(const char* name)
			{
#if defined(_WIN32)
				return (void*)GetProcAddress((HMODULE)lib, name);
#else
				return dlsym(lib, name);
#endif
			}
			template <class T> bool bind(T& fn, const char* name) { fn = reinterpret_cast<T>(sym(name)); return fn != nullptr; }
			void load()
			{
				if (std::getenv("VVD_NO_OPENCL")) return;
#if defined(_WIN32)
				lib = (void*)LoadLibraryA("OpenCL.dll");
#elif defined(__APPLE__)
				lib = dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_NOW | RTLD_LOCAL);
#else
				lib = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
				if (!lib) lib = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
#endif
				if (!lib) return;
				ok = bind(GetPlatformIDs, "clGetPlatformIDs") && bind(GetDeviceIDs, "clGetDeviceIDs") &&
					bind(GetDeviceInfo, "clGetDeviceInfo") && bind(CreateContext, "clCreateContext") &&
					bind(CreateCommandQueue, "clCreateCommandQueue") && bind(CreateProgramWithSource, "clCreateProgramWithSource") &&
					bind(BuildProgram, "clBuildProgram") && bind(GetProgramBuildInfo, "clGetProgramBuildInfo") &&
					bind(CreateKernel, "clCreateKernel") && bind(CreateBuffer, "clCreateBuffer") &&
					bind(SetKernelArg, "clSetKernelArg") && bind(EnqueueNDRangeKernel, "clEnqueueNDRangeKernel") &&
					bind(EnqueueReadBuffer, "clEnqueueReadBuffer") && bind(Finish, "clFinish") &&
					bind(ReleaseMemObject, "clReleaseMemObject") && bind(ReleaseKernel, "clReleaseKernel") &&
					bind(ReleaseProgram, "clReleaseProgram") && bind(ReleaseCommandQueue, "clReleaseCommandQueue") &&
					bind(ReleaseContext, "clReleaseContext");
			}
		};

		struct GpuDevice { cl_platform_id platform; cl_device_id device; std::string name; };

		std::vector<GpuDevice> gpuDevices()
		{
			std::vector<GpuDevice> result;
			ClApi& cl = ClApi::get();
			if (!cl.ok) return result;
			cl_uint count = 0;
			if (cl.GetPlatformIDs(0, nullptr, &count) != CL_SUCCESS || count == 0) return result;
			std::vector<cl_platform_id> platforms(count);
			if (cl.GetPlatformIDs(count, platforms.data(), nullptr) != CL_SUCCESS) return result;
			for (cl_platform_id p : platforms)
			{
				cl_uint n = 0;
				cl_int st = cl.GetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 0, nullptr, &n);
				if (st == CL_DEVICE_NOT_FOUND || st != CL_SUCCESS || n == 0) continue;
				std::vector<cl_device_id> ids(n);
				if (cl.GetDeviceIDs(p, CL_DEVICE_TYPE_GPU, n, ids.data(), nullptr) != CL_SUCCESS) continue;
				for (cl_device_id id : ids)
				{
					size_t size = 0;
					if (cl.GetDeviceInfo(id, CL_DEVICE_NAME, 0, nullptr, &size) != CL_SUCCESS) continue;
					std::string name(size, '\0');
					if (cl.GetDeviceInfo(id, CL_DEVICE_NAME, size, &name[0], nullptr) != CL_SUCCESS) continue;
					name.erase(std::remove(name.begin(), name.end(), '\0'), name.end());
					while (!name.empty() && (name.back() == ' ')) name.pop_back();
					while (!name.empty() && (name.front() == ' ')) name.erase(name.begin());
					result.push_back({ p, id, name });
				}
			}
			return result;
		}

		const char* const OPENCL_SOURCE =
			"// Flat-top (order-8 super-Gaussian) weighted mean of sample displacements, one work item per field node.\n"
			"__kernel void adaptive_field(__global const float *sp, __global const float *sd, __global const float *st, int ns,\n"
			"        __global const float *np, __global const float *nsig, __global const float *nmin,\n"
			"        __global float *out, __global int *ok, int n) {\n"
			"    int i = get_global_id(0);\n"
			"    if (i >= n) return;\n"
			"    float px = np[3*i], py = np[3*i+1], pz = np[3*i+2];\n"
			"    float sigma = nsig[i], minT = nmin[i];\n"
			"    float s2 = sigma * sigma, r2 = 2.25f * s2;\n"
			"    float wsum = 0.0f, ox = 0.0f, oy = 0.0f, oz = 0.0f;\n"
			"    for (int k = 0; k < ns; ++k) {\n"
			"        if (st[k] < minT) continue;\n"
			"        float dx = sp[3*k] - px, dy = sp[3*k+1] - py, dz = sp[3*k+2] - pz;\n"
			"        float d2 = dx*dx + dy*dy + dz*dz;\n"
			"        if (d2 > r2) continue;\n"
			"        float u = d2 / s2; u = u * u; u = u * u;\n"
			"        float w = exp(-u);\n"
			"        wsum += w; ox += w * sd[3*k]; oy += w * sd[3*k+1]; oz += w * sd[3*k+2];\n"
			"    }\n"
			"    if (wsum > 1e-30f) { out[3*i] = ox / wsum; out[3*i+1] = oy / wsum; out[3*i+2] = oz / wsum; ok[i] = 1; }\n"
			"    else { out[3*i] = 0.0f; out[3*i+1] = 0.0f; out[3*i+2] = 0.0f; ok[i] = 0; }\n"
			"}\n";

		// OpenCL evaluation of the adaptive field nodes (brute force over all samples; one work item per node)
		class GpuField
		{
		public:
			std::string name;
			static std::unique_ptr<GpuField> open(int index)
			{
				std::vector<GpuDevice> devices = gpuDevices();
				if (index < 0 || index >= (int)devices.size()) throw Failure("Selected GPU is no longer available.");
				return std::unique_ptr<GpuField>(new GpuField(devices[index]));
			}
			~GpuField()
			{
				ClApi& cl = ClApi::get();
				if (kernel) cl.ReleaseKernel(kernel);
				if (program) cl.ReleaseProgram(program);
				if (queue) cl.ReleaseCommandQueue(queue);
				if (context) cl.ReleaseContext(context);
			}
			// 3*n displacements; solved[k] false where no sample fell inside the kernel (the CPU widens those)
			std::vector<float> evaluate(const std::vector<float>& sp, const std::vector<float>& sd, const std::vector<float>& st,
				const std::vector<float>& nodePos, const std::vector<float>& nodeSigma, const std::vector<float>& nodeMinThickness,
				std::vector<char>& solved)
			{
				ClApi& cl = ClApi::get();
				const int ns = (int)st.size(), n = (int)nodeSigma.size();
				std::vector<float> out((size_t)3 * n, 0.0f);
				std::vector<int> okv(n, 0);
				solved.assign(n, 0);
				if (n == 0) return out;
				std::vector<cl_mem> mems;
				auto release = [&]() { for (cl_mem m : mems) if (m) cl.ReleaseMemObject(m); mems.clear(); };
				auto floats = [&](const std::vector<float>& v) {
					cl_int e = 0;
					size_t bytes = std::max<size_t>(4, 4 * v.size());
					static const float dummy = 0.0f;
					cl_mem m = cl.CreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, bytes, v.empty() ? (void*)&dummy : (void*)v.data(), &e);
					if (e != CL_SUCCESS) { release(); throw Failure("allocate GPU buffer failed: " + std::to_string(e)); }
					mems.push_back(m);
					return m;
				};
				cl_mem bSp = floats(sp), bSd = floats(sd), bSt = floats(st);
				// node chunks keep each launch short (the result of a node does not depend on the chunking)
				const long long budget = 1LL << 31;
				int chunk = (int)std::max<long long>(4096, std::min<long long>(n, budget / std::max(1, ns)));
				chunk = (chunk + 63) / 64 * 64;
				std::vector<float> pos, sig, mnt;
				for (int k0 = 0; k0 < n; k0 += chunk)
				{
					int m = std::min(chunk, n - k0);
					pos.assign(nodePos.begin() + (size_t)3 * k0, nodePos.begin() + (size_t)3 * (k0 + m));
					sig.assign(nodeSigma.begin() + k0, nodeSigma.begin() + k0 + m);
					mnt.assign(nodeMinThickness.begin() + k0, nodeMinThickness.begin() + k0 + m);
					cl_mem bNp = floats(pos), bSig = floats(sig), bMin = floats(mnt);
					cl_int e = 0;
					cl_mem bOut = cl.CreateBuffer(context, CL_MEM_WRITE_ONLY, (size_t)4 * 3 * m, nullptr, &e);
					if (e != CL_SUCCESS) { release(); throw Failure("allocate GPU output failed"); }
					mems.push_back(bOut);
					cl_mem bOk = cl.CreateBuffer(context, CL_MEM_WRITE_ONLY, (size_t)4 * m, nullptr, &e);
					if (e != CL_SUCCESS) { release(); throw Failure("allocate GPU flags failed"); }
					mems.push_back(bOk);
					cl_int st_ = CL_SUCCESS;
					st_ |= cl.SetKernelArg(kernel, 0, sizeof(cl_mem), &bSp);
					st_ |= cl.SetKernelArg(kernel, 1, sizeof(cl_mem), &bSd);
					st_ |= cl.SetKernelArg(kernel, 2, sizeof(cl_mem), &bSt);
					st_ |= cl.SetKernelArg(kernel, 3, sizeof(cl_int), &ns);
					st_ |= cl.SetKernelArg(kernel, 4, sizeof(cl_mem), &bNp);
					st_ |= cl.SetKernelArg(kernel, 5, sizeof(cl_mem), &bSig);
					st_ |= cl.SetKernelArg(kernel, 6, sizeof(cl_mem), &bMin);
					st_ |= cl.SetKernelArg(kernel, 7, sizeof(cl_mem), &bOut);
					st_ |= cl.SetKernelArg(kernel, 8, sizeof(cl_mem), &bOk);
					st_ |= cl.SetKernelArg(kernel, 9, sizeof(cl_int), &m);
					if (st_ != CL_SUCCESS) { release(); throw Failure("set GPU kernel arguments failed"); }
					size_t global = (size_t)((m + 63) / 64) * 64, local = 64;
					if (cl.EnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, &local, 0, nullptr, nullptr) != CL_SUCCESS) { release(); throw Failure("launch GPU field kernel failed"); }
					if (cl.EnqueueReadBuffer(queue, bOut, CL_TRUE, 0, (size_t)4 * 3 * m, out.data() + (size_t)3 * k0, 0, nullptr, nullptr) != CL_SUCCESS) { release(); throw Failure("read GPU field failed"); }
					if (cl.EnqueueReadBuffer(queue, bOk, CL_TRUE, 0, (size_t)4 * m, okv.data() + k0, 0, nullptr, nullptr) != CL_SUCCESS) { release(); throw Failure("read GPU flags failed"); }
					for (int j = 0; j < 5; j++) { cl.ReleaseMemObject(mems.back()); mems.pop_back(); }
				}
				release();
				for (int k = 0; k < n; k++)
					solved[k] = okv[k] == 1 && std::isfinite(out[3 * k]) && std::isfinite(out[3 * k + 1]) && std::isfinite(out[3 * k + 2]);
				return out;
			}
		private:
			cl_context context = nullptr;
			cl_command_queue queue = nullptr;
			cl_program program = nullptr;
			cl_kernel kernel = nullptr;
			explicit GpuField(const GpuDevice& d) : name(d.name)
			{
				ClApi& cl = ClApi::get();
				cl_int e = 0;
				cl_context_properties props[3] = { CL_CONTEXT_PLATFORM, (cl_context_properties)d.platform, 0 };
				context = cl.CreateContext(props, 1, &d.device, nullptr, nullptr, &e);
				if (e != CL_SUCCESS || !context) throw Failure("create GPU context failed: " + std::to_string(e));
				queue = cl.CreateCommandQueue(context, d.device, 0, &e);
				if (e != CL_SUCCESS || !queue) throw Failure("create GPU queue failed: " + std::to_string(e));
				const char* src = OPENCL_SOURCE;
				program = cl.CreateProgramWithSource(context, 1, &src, nullptr, &e);
				if (e != CL_SUCCESS || !program) throw Failure("create GPU program failed: " + std::to_string(e));
				if (cl.BuildProgram(program, 1, &d.device, "-cl-std=CL1.2", nullptr, nullptr) != CL_SUCCESS)
				{
					size_t size = 0;
					cl.GetProgramBuildInfo(program, d.device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &size);
					std::string log(size, '\0');
					if (size) cl.GetProgramBuildInfo(program, d.device, CL_PROGRAM_BUILD_LOG, size, &log[0], nullptr);
					throw Failure("GPU kernel build failed: " + log);
				}
				kernel = cl.CreateKernel(program, "adaptive_field", &e);
				if (e != CL_SUCCESS || !kernel) throw Failure("create GPU kernel failed: " + std::to_string(e));
			}
		};

		// ---------------------------------------------------------- AdaptiveField
		// thickness-adaptive smoothing of the TPS displacement over one object
		class AdaptiveField
		{
		public:
			std::vector<V3> s, disp;
			std::vector<double> sampleThickness;
			const Thickness& thickness;
			double beta, sigmaMin, neurite, h;
			double origin[3];
			int dims[3];
			std::vector<float> node;
			std::unique_ptr<std::atomic<uint8_t>[]> done;
			size_t nodeCount = 0;
			double cell;
			double cellOrigin[3];
			int cellDims[3];
			std::vector<int> cellStart, cellItems;   // samples of cell c: cellItems[cellStart[c] .. cellStart[c+1])
			std::mutex ensureMutex;

			AdaptiveField(const std::vector<V3>& samples, const std::vector<V3>& mapped, const Thickness& th, double beta_, double sigmaMin_,
				double neurite_, double h_, const Grid& mg, const std::array<int, 3>& bmin, const std::array<int, 3>& bmax)
				: s(samples), thickness(th), beta(beta_), sigmaMin(sigmaMin_), neurite(neurite_), h(h_)
			{
				disp.resize(samples.size()); sampleThickness.resize(samples.size());
				for (size_t i = 0; i < samples.size(); i++)
				{
					sampleThickness[i] = thickness.at(samples[i]);
					for (int d = 0; d < 3; d++) disp[i][d] = mapped[i][d] - samples[i][d];
				}
				long long total = 1;
				for (int d = 0; d < 3; d++)
				{
					origin[d] = mg.phys(d, bmin[d]) - 2 * h - mg.spacing[d];
					dims[d] = jint(std::ceil((mg.phys(d, bmax[d]) + 2 * h + mg.spacing[d] - origin[d]) / h)) + 2;
					total *= dims[d];
				}
				if (total > 60000000LL) throw Failure("Adaptive field grid too large; increase the minimum smoothing.");
				nodeCount = (size_t)total;
				node.assign(nodeCount * 3, 0.0f);
				done.reset(new std::atomic<uint8_t>[nodeCount]);
				for (size_t i = 0; i < nodeCount; i++) done[i].store(0, std::memory_order_relaxed);
				cell = jmax(1.5 * sigmaMin, 0.5);
				for (int d = 0; d < 3; d++) { cellOrigin[d] = origin[d]; cellDims[d] = jint(std::ceil(dims[d] * h / cell)) + 1; }
				size_t nc = (size_t)cellDims[0] * cellDims[1] * cellDims[2];
				std::vector<int> count(nc, 0), id(samples.size());
				for (size_t i = 0; i < samples.size(); i++) { id[i] = cellIndex(samples[i]); count[id[i]]++; }
				cellStart.assign(nc + 1, 0);
				for (size_t c = 0; c < nc; c++) cellStart[c + 1] = cellStart[c] + count[c];
				cellItems.assign(samples.size(), 0);
				std::fill(count.begin(), count.end(), 0);
				for (size_t i = 0; i < samples.size(); i++) cellItems[cellStart[id[i]] + count[id[i]]++] = (int)i;
			}
			int cellCoord(int d, double v) const
			{
				int i = jint(std::floor((v - cellOrigin[d]) / cell));
				return i < 0 ? 0 : i >= cellDims[d] ? cellDims[d] - 1 : i;
			}
			int cellIndex(const V3& p) const { return (cellCoord(2, p[2]) * cellDims[1] + cellCoord(1, p[1])) * cellDims[0] + cellCoord(0, p[0]); }

			// flat-top (order-8 super-Gaussian) weighted mean of the sample displacements, radius sigma(p)
			V3 evaluate(const V3& p) const
			{
				double tp = thickness.at(p), sigma = sigmaFor(tp);
				double minThickness = 0.5 * tp;
				V3 out = { 0, 0, 0 };
				for (int attempt = 0; attempt < 12; attempt++, sigma *= 2)
				{
					if (attempt == 6) minThickness = 0;
					double r = 1.5 * sigma, s2 = sigma * sigma, wsum = 0;
					int x0 = cellCoord(0, p[0] - r), x1 = cellCoord(0, p[0] + r), y0 = cellCoord(1, p[1] - r), y1 = cellCoord(1, p[1] + r), z0 = cellCoord(2, p[2] - r), z1 = cellCoord(2, p[2] + r);
					out = { 0, 0, 0 };
					for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
					{
						int c = (z * cellDims[1] + y) * cellDims[0] + x;
						for (int k = cellStart[c], ke = cellStart[c + 1]; k < ke; k++)
						{
							int i = cellItems[k];
							double dx = s[i][0] - p[0], dy = s[i][1] - p[1], dz = s[i][2] - p[2];
							double d2 = dx * dx + dy * dy + dz * dz;
							if (d2 > r * r || sampleThickness[i] < minThickness) continue;
							double u = d2 / s2, w = std::exp(-u * u * u * u);
							wsum += w; out[0] += w * disp[i][0]; out[1] += w * disp[i][1]; out[2] += w * disp[i][2];
						}
					}
					if (wsum > 1e-300) { for (int d = 0; d < 3; d++) out[d] /= wsum; return out; }
				}
				size_t best = 0;
				double bd = std::numeric_limits<double>::infinity();
				for (size_t i = 0; i < s.size(); i++) { double d2 = distance(s[i], p); if (d2 < bd) { bd = d2; best = i; } }
				return disp[best];
			}
			// kernel radius: sigmaMin below the neurite radius, beta * thickness for thick structures, blended in between
			double sigmaFor(double t) const
			{
				double thick = jmax(sigmaMin, beta * t);
				if (neurite <= 0) return thick;
				if (t <= neurite) return sigmaMin;
				if (t >= 1.5 * neurite) return thick;
				double f = (t - neurite) / (0.5 * neurite);
				return sigmaMin + f * (thick - sigmaMin);
			}
			size_t nodeIndex(int x, int y, int z) const { return ((size_t)z * dims[1] + y) * dims[0] + x; }

			// every grid node the object's voxels interpolate from, on the GPU or on all CPU cores
			void precompute(const Runs& runs, const std::vector<int>& runList, const Grid& mg, GpuField* gpu, int threads)
			{
				std::vector<char> needed(nodeCount, 0);
				double fr[3];
				V3 p;
				for (int ri : runList)
				{
					p[1] = mg.phys(1, runs.runY[ri]); p[2] = mg.phys(2, runs.runZ[ri]);
					for (int x = runs.runStart[ri]; x <= runs.runEnd[ri]; x++)
					{
						p[0] = mg.phys(0, x);
						int bx = base(0, p[0], fr), by = base(1, p[1], fr), bz = base(2, p[2], fr);
						for (int k = -1; k <= 2; k++) for (int j = -1; j <= 2; j++) for (int i = -1; i <= 2; i++)
						{
							int nx = bx + i, ny = by + j, nz = bz + k;
							if (nx >= 0 && ny >= 0 && nz >= 0 && nx < dims[0] && ny < dims[1] && nz < dims[2]) needed[nodeIndex(nx, ny, nz)] = 1;
						}
					}
				}
				std::vector<size_t> idx;
				for (size_t i = 0; i < nodeCount; i++) if (needed[i]) idx.push_back(i);
				const size_t m = idx.size();
				std::vector<float> pos(3 * m), sig(m), minT(m);
				for (size_t k = 0; k < m; k++)
				{
					size_t i = idx[k];
					int x = (int)(i % dims[0]), y = (int)((i / dims[0]) % dims[1]), z = (int)(i / ((size_t)dims[0] * dims[1]));
					V3 q = { origin[0] + x * h, origin[1] + y * h, origin[2] + z * h };
					double tp = thickness.at(q);
					pos[3 * k] = (float)q[0]; pos[3 * k + 1] = (float)q[1]; pos[3 * k + 2] = (float)q[2];
					sig[k] = (float)sigmaFor(tp); minT[k] = (float)(0.5 * tp);
				}
				std::vector<char> solved(m, 0);
				if (gpu)
				{
					std::vector<float> fsp(3 * s.size()), fsd(3 * s.size()), fst(s.size());
					for (size_t i = 0; i < s.size(); i++)
					{
						for (int d = 0; d < 3; d++) { fsp[3 * i + d] = (float)s[i][d]; fsd[3 * i + d] = (float)disp[i][d]; }
						fst[i] = (float)sampleThickness[i];
					}
					std::vector<float> out = gpu->evaluate(fsp, fsd, fst, pos, sig, minT, solved);
					for (size_t k = 0; k < m; k++) if (solved[k])
					{
						size_t i = idx[k];
						node[3 * i] = out[3 * k]; node[3 * i + 1] = out[3 * k + 1]; node[3 * i + 2] = out[3 * k + 2];
						done[i].store(1, std::memory_order_relaxed);
					}
				}
				// CPU (all cores) for everything the GPU did not solve
				std::vector<size_t> rest;
				for (size_t k = 0; k < m; k++) if (!solved[k]) rest.push_back(k);
				if (rest.empty()) return;
				ParallelFor((long long)rest.size(), threads, [&](long long r) {
					size_t kk = rest[(size_t)r], i = idx[kk];
					V3 v = evaluate(V3{ (double)pos[3 * kk], (double)pos[3 * kk + 1], (double)pos[3 * kk + 2] });
					node[3 * i] = (float)v[0]; node[3 * i + 1] = (float)v[1]; node[3 * i + 2] = (float)v[2];
				});
				for (size_t kk : rest) done[idx[kk]].store(1, std::memory_order_relaxed);
			}
			void ensure(int x, int y, int z)
			{
				size_t i = nodeIndex(x, y, z);
				if (done[i].load(std::memory_order_acquire)) return;
				std::lock_guard<std::mutex> lk(ensureMutex);
				if (done[i].load(std::memory_order_relaxed)) return;
				V3 v = evaluate(V3{ origin[0] + x * h, origin[1] + y * h, origin[2] + z * h });
				node[3 * i] = (float)v[0]; node[3 * i + 1] = (float)v[1]; node[3 * i + 2] = (float)v[2];
				done[i].store(1, std::memory_order_release);
			}
			int base(int d, double v, double* frac) const
			{
				double g = (v - origin[d]) / h;
				int i = jint(std::floor(g));
				if (i < 0) { i = 0; g = 0; }
				if (i > dims[d] - 2) { i = dims[d] - 2; g = i + 1; }
				frac[d] = g - i;
				return i;
			}
			// moving physical -> target physical
			V3 map(const V3& p)
			{
				double fr[3];
				int x = base(0, p[0], fr), y = base(1, p[1], fr), z = base(2, p[2], fr);
				V3 q = p;
				for (int k = 0; k < 2; k++) for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++)
				{
					ensure(x + i, y + j, z + k);
					double w = (i == 1 ? fr[0] : 1 - fr[0]) * (j == 1 ? fr[1] : 1 - fr[1]) * (k == 1 ? fr[2] : 1 - fr[2]);
					size_t n = 3 * nodeIndex(x + i, y + j, z + k);
					q[0] += w * node[n]; q[1] += w * node[n + 1]; q[2] += w * node[n + 2];
				}
				return q;
			}
			static double sq(double v) { return v * v; }
			// per-axis stretch factor (>= 1) of the mapping around p
			void stretch(const V3& p, int out[3])
			{
				double fr[3];
				int x = base(0, p[0], fr), y = base(1, p[1], fr), z = base(2, p[2], fr);
				double m[3] = { 0, 0, 0 };
				for (int k = 0; k < 2; k++) for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++)
				{
					ensure(x + i, y + j, z + k);
					size_t n = 3 * nodeIndex(x + i, y + j, z + k);
					// float differences, as in Java (float - float is float)
					if (i == 1) { size_t o = 3 * nodeIndex(x, y + j, z + k); float a = node[n] - node[o], b = node[n + 1] - node[o + 1], c = node[n + 2] - node[o + 2]; m[0] = jmax(m[0], std::sqrt(sq(a) + sq(b) + sq(c))); }
					if (j == 1) { size_t o = 3 * nodeIndex(x + i, y, z + k); float a = node[n] - node[o], b = node[n + 1] - node[o + 1], c = node[n + 2] - node[o + 2]; m[1] = jmax(m[1], std::sqrt(sq(a) + sq(b) + sq(c))); }
					if (k == 1) { size_t o = 3 * nodeIndex(x + i, y + j, z); float a = node[n] - node[o], b = node[n + 1] - node[o + 1], c = node[n + 2] - node[o + 2]; m[2] = jmax(m[2], std::sqrt(sq(a) + sq(b) + sq(c))); }
				}
				for (int d = 0; d < 3; d++) out[d] = jint(std::ceil(1 + m[d] / h));
			}
		};

		// ---------------------------------------------------------- output
		struct Sink
		{
			void* data; int bytes; int tw, th;
			// false when the voxel was already occupied (first writer wins)
			bool set(int x, int y, int z, int value)
			{
				size_t i = ((size_t)z * th + y) * tw + x;
				if (bytes == 1) { uint8_t* b = (uint8_t*)data; if (b[i] != 0) return false; b[i] = (uint8_t)std::min(255, value); }
				else if (bytes == 2) { uint16_t* s = (uint16_t*)data; if (s[i] != 0) return false; s[i] = (uint16_t)std::min(65535, value); }
				else { float* f = (float*)data; if (f[i] != 0) return false; f[i] = (float)value; }
				return true;
			}
		};

		struct ObjectResult : ObjectRow { Fit fit; };

		void appendNote(ObjectRow& r, const std::string& s) { r.note = r.note.empty() ? s : r.note + "; " + s; }

		// ---------------------------------------------------------- Core
		class Core
		{
		public:
			Tps tps;
			const std::vector<V3>& lm;
			const std::vector<V3>& lt;
			int threads;

			Core(const std::vector<V3>& moving, const std::vector<V3>& target, double stiffness, int threads_)
				: tps(moving, target, stiffness), lm(moving), lt(target), threads(threads_) {}

			std::vector<ObjectResult> place(const Runs& runs, const Grid& mg, const Grid& tg, int localMode, double beta, double sigmaMin,
				double neurite, int maxSamples, double radius, GpuField* gpu, Sink& sink, const ProgressFn& progress)
			{
				std::vector<ObjectResult> out;
				std::vector<std::vector<int>> runsOf = runs.runsPerComponent();
				for (int c = 0; c < runs.componentCount; c++)
				{
					if (progress && !progress(c, runs.componentCount, "Placing object " + std::to_string(c + 1) + " / " + std::to_string(runs.componentCount)))
						throw Cancelled();
					ObjectResult r;
					r.label = runs.componentLabel[c]; r.voxels = runs.componentVoxels[c];
					// sample the object's voxels in moving physical coordinates
					long long stride = std::max(1LL, (r.voxels + maxSamples - 1) / maxSamples);
					std::vector<V3> a;
					long long counter = 0;
					for (int ri : runsOf[c])
					{
						int y = runs.runY[ri], z = runs.runZ[ri];
						for (int x = runs.runStart[ri]; x <= runs.runEnd[ri]; x++, counter++)
							if (counter % stride == 0) a.push_back(V3{ mg.phys(0, x), mg.phys(1, y), mg.phys(2, z) });
					}
					const int n = (int)a.size();
					std::vector<V3> b(n);
					double cm[3] = { 0, 0, 0 }, cb[3] = { 0, 0, 0 };
					ParallelFor(n, threads, [&](long long i) { b[(size_t)i] = tps.apply(a[(size_t)i]); });
					for (int i = 0; i < n; i++) for (int d = 0; d < 3; d++) { cm[d] += a[i][d] / n; cb[d] += b[i][d] / n; }
					for (int d = 0; d < 3; d++) { r.moving_c[d] = cm[d]; r.target_c[d] = cb[d]; }
					// shape statistics
					Mat cov(3, std::vector<double>(3, 0.0));
					double gyr = 0;
					for (const V3& p : a) for (int i = 0; i < 3; i++)
					{
						gyr += (p[i] - cm[i]) * (p[i] - cm[i]) / n;
						for (int j = 0; j < 3; j++) cov[i][j] += (p[i] - cm[i]) * (p[j] - cm[j]) / n;
					}
					r.radius_of_gyration = std::sqrt(gyr);
					std::vector<double> ev = Linear::symmetricEigenvalues(cov);
					r.shape_anisotropy = ev[0] > 0 ? std::sqrt(ev[0] / jmax(ev[2], 1e-300)) : 1;
					// folding of the TPS over this object
					std::vector<double> dets(n);
					ParallelFor(n, threads, [&](long long i) { dets[(size_t)i] = tps.jacobianDeterminant(a[(size_t)i]); });
					double dmin = std::numeric_limits<double>::infinity(), dmax = -std::numeric_limits<double>::infinity();
					int folded = 0;
					for (int i = 0; i < n; i++) { double det = dets[i]; dmin = jmin(dmin, det); dmax = jmax(dmax, det); if (det <= 0) folded++; }
					r.tps_jacobian_min = dmin; r.tps_jacobian_max = dmax; r.tps_folded_fraction = folded / (double)n;
					if (localMode == ADAPTIVE)
					{
						auto tA = std::chrono::steady_clock::now();
						double maxExtent = 0;
						for (int d = 0; d < 3; d++) maxExtent = jmax(maxExtent, (runs.componentMax[c][d] - runs.componentMin[c][d] + 1) * mg.spacing[d]);
						double h = jmax(sigmaMin / 2, maxExtent / 200.0);
						Thickness thickness = Thickness::of(runs, runsOf[c], mg, runs.componentMin[c], runs.componentMax[c], 2 * h, threads);
						r.max_thickness = thickness.max;
						auto tB = std::chrono::steady_clock::now();
						// re-sample: thin voxels densely, thick voxels as before
						long long thinCount = 0;
						V3 probe;
						for (int ri : runsOf[c])
						{
							probe[1] = mg.phys(1, runs.runY[ri]); probe[2] = mg.phys(2, runs.runZ[ri]);
							for (int x = runs.runStart[ri]; x <= runs.runEnd[ri]; x++) { probe[0] = mg.phys(0, x); if (thickness.at(probe) < neurite) thinCount++; }
						}
						long long thinStride = std::max(1LL, (thinCount + 4LL * maxSamples - 1) / (4LL * maxSamples));
						long long thickStride = std::max(1LL, (r.voxels - thinCount + maxSamples - 1) / maxSamples);
						std::vector<V3> fa;
						long long ct = 0, cn = 0;
						for (int ri : runsOf[c])
						{
							probe[1] = mg.phys(1, runs.runY[ri]); probe[2] = mg.phys(2, runs.runZ[ri]);
							for (int x = runs.runStart[ri]; x <= runs.runEnd[ri]; x++)
							{
								probe[0] = mg.phys(0, x);
								bool thinVoxel = thickness.at(probe) < neurite;
								if (thinVoxel ? (cn++ % thinStride == 0) : (ct++ % thickStride == 0)) fa.push_back(probe);
							}
						}
						std::vector<V3> fb(fa.size());
						ParallelFor((long long)fa.size(), threads, [&](long long i) { fb[(size_t)i] = tps.apply(fa[(size_t)i]); });
						AdaptiveField field(fa, fb, thickness, beta, sigmaMin, neurite, h, mg, runs.componentMin[c], runs.componentMax[c]);
						field.precompute(runs, runsOf[c], mg, gpu, threads);
						// quality: rigidity of the thick part, closeness of the thin part to the TPS
						std::vector<V3> thickA, thickB;
						double thinSum = 0; int thin = 0;
						for (int i = 0; i < n; i++)
						{
							double t = thickness.at(a[i]);
							V3 q = field.map(a[i]);
							if (t >= 0.75 * thickness.max) { thickA.push_back(a[i]); thickB.push_back(q); }
							else if (t <= 0.5 * thickness.max) { double e = distance(q, b[i]); thinSum += e * e; thin++; }
						}
						if (thickA.size() >= 4)
						{
							Fit rigid = Procrustes::fit(thickA, thickB, RIGID);
							double s = 0;
							for (size_t i = 0; i < thickA.size(); i++) { double e = distance(rigid.apply(thickA[i]), thickB[i]); s += e * e; }
							r.thick_part_nonrigid_rms = std::sqrt(s / thickA.size());
							r.rotation_deg = rigid.degrees();
						}
						r.thin_part_tps_deviation_rms = thin > 0 ? std::sqrt(thinSum / thin) : 0;
						double sum = 0, max = 0;
						for (int i = 0; i < n; i++) { double e = distance(field.map(a[i]), b[i]); sum += e * e; max = jmax(max, e); }
						r.tps_vs_local_rms = std::sqrt(sum / n); r.tps_vs_local_max = max;
						double lsum = 0, lmax = 0; int nearCount = 0;
						for (size_t i = 0; i < lm.size(); i++)
						{
							if (!runs.near(c, runsOf[c], lm[i], mg, radius)) continue;
							nearCount++;
							double e = distance(field.map(lm[i]), lt[i]);
							lsum += e * e; lmax = jmax(lmax, e);
						}
						r.nearby_landmarks = nearCount; r.nearby_landmark_rms = nearCount > 0 ? std::sqrt(lsum / nearCount) : 0; r.nearby_landmark_max = lmax;
						r.note = "adaptive: sigma = " + JavaDouble(sigmaMin) + " below neurite radius " + JavaDouble(neurite) + ", else max(" + JavaDouble(sigmaMin) + ", "
							+ JavaDouble(beta) + " x thickness); " + std::to_string(fa.size()) + " field samples; grid " + jfmtg(h, 3);
						auto tC = std::chrono::steady_clock::now();
						splat(runs, runsOf[c], field, mg, tg, sink, r);
						auto tD = std::chrono::steady_clock::now();
						auto sec = [](std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); };
						r.note += "; time thickness " + fmt1f(sec(tB - tA)) + "s, field+stats " + fmt1f(sec(tC - tB)) + "s, render " + fmt1f(sec(tD - tC)) + "s";
						out.push_back(r);
						continue;
					}
					// one local transform for the whole object
					int mode = localMode;
					bool degenerate = n < 3 || ev[1] <= ev[0] * 1e-6;
					if (mode > TRANSLATION && degenerate) { mode = TRANSLATION; r.note = "rotation undefined (point/line-like object): used Translation"; }
					Fit fit = Procrustes::fit(a, b, mode);
					r.fit = fit; r.scale = fit.scale;
					r.rotation_deg = fit.degrees();
					double sum = 0, max = 0;
					for (int i = 0; i < n; i++) { double e = distance(fit.apply(a[i]), b[i]); sum += e * e; max = jmax(max, e); }
					r.tps_vs_local_rms = std::sqrt(sum / n); r.tps_vs_local_max = max;
					if (mode > TRANSLATION && r.shape_anisotropy < 1.5 && r.note.empty()) r.note = "nearly isotropic object: rotation weakly determined";
					double lsum = 0, lmax = 0; int nearCount = 0;
					for (size_t i = 0; i < lm.size(); i++)
					{
						if (!runs.near(c, runsOf[c], lm[i], mg, radius)) continue;
						nearCount++;
						double e = distance(fit.apply(lm[i]), lt[i]);
						lsum += e * e; lmax = jmax(lmax, e);
					}
					r.nearby_landmarks = nearCount; r.nearby_landmark_rms = nearCount > 0 ? std::sqrt(lsum / nearCount) : 0; r.nearby_landmark_max = lmax;
					render(runs, c, fit, mg, tg, sink, r);
					out.push_back(r);
				}
				if (progress && !progress(runs.componentCount, runs.componentCount, "Done")) throw Cancelled();
				return out;
			}

			// inverse-map every target voxel inside the transformed bounding box
			void render(const Runs& runs, int c, const Fit& fit, const Grid& mg, const Grid& tg, Sink& sink, ObjectRow& r)
			{
				double lo[3], hi[3];
				for (int d = 0; d < 3; d++) { lo[d] = std::numeric_limits<double>::infinity(); hi[d] = -std::numeric_limits<double>::infinity(); }
				const std::array<int, 3>& bmin = runs.componentMin[c];
				const std::array<int, 3>& bmax = runs.componentMax[c];
				for (int corner = 0; corner < 8; corner++)
				{
					V3 p;
					for (int d = 0; d < 3; d++) p[d] = mg.phys(d, ((corner >> d) & 1) == 0 ? bmin[d] - 0.5 : bmax[d] + 0.5);
					V3 q = fit.apply(p);
					for (int d = 0; d < 3; d++) { lo[d] = jmin(lo[d], q[d]); hi[d] = jmax(hi[d], q[d]); }
				}
				int i0[3], i1[3];
				long long total = 1, inside = 1;
				for (int d = 0; d < 3; d++)
				{
					double a = tg.index(d, lo[d]), b = tg.index(d, hi[d]);
					int ia = jint(std::floor(jmin(a, b))) - 1, ib = jint(std::ceil(jmax(a, b))) + 1;
					total *= std::max(0, ib - ia + 1);
					i0[d] = std::max(0, ia); i1[d] = std::min(tg.size[d] - 1, ib);
					inside *= std::max(0, i1[d] - i0[d] + 1);
				}
				if (inside == 0) { appendNote(r, "object lies completely outside the target grid"); return; }
				const int w = runs.width, h = runs.height, dep = runs.depth;
				const int nzs = i1[2] - i0[2] + 1;
				std::vector<long long> written(nzs, 0), collisions(nzs, 0);
				// z slices in parallel: each target voxel is visited once per object, so the result is order independent
				ParallelFor(nzs, threads, [&](long long zi) {
					int z = i0[2] + (int)zi;
					V3 q;
					q[2] = tg.phys(2, z);
					long long wr = 0, co = 0;
					for (int y = i0[1]; y <= i1[1]; y++)
					{
						q[1] = tg.phys(1, y);
						for (int x = i0[0]; x <= i1[0]; x++)
						{
							q[0] = tg.phys(0, x);
							V3 p = fit.inverse(q);
							int mx = jint(std::floor(mg.index(0, p[0]) + 0.5)), my = jint(std::floor(mg.index(1, p[1]) + 0.5)), mz = jint(std::floor(mg.index(2, p[2]) + 0.5));
							if (mx < 0 || my < 0 || mz < 0 || mx >= w || my >= h || mz >= dep) continue;
							if (runs.componentAt(mx, my, mz) != c) continue;
							if (sink.set(x, y, z, r.label)) wr++; else co++;
						}
					}
					written[(size_t)zi] = wr; collisions[(size_t)zi] = co;
				});
				for (int i = 0; i < nzs; i++) { r.written += written[i]; r.collisions += collisions[i]; }
				if (total > inside) r.clipped = jround(r.voxels * (1.0 - inside / (double)total));
			}

			// forward-map every object voxel through the adaptive field, super-sampling where the field stretches
			void splat(const Runs& runs, const std::vector<int>& runList, AdaptiveField& field, const Grid& mg, const Grid& tg, Sink& sink, ObjectRow& r)
			{
				// target voxels are computed in parallel per chunk of runs and written in the
				// plugin's order (voxel by voxel, sub-sample by sub-sample), so writes,
				// collisions and clipping counts are identical to the sequential plugin
				const long long CLIP = -1, MARK = -2;
				// chunks of at most 4096 runs / 256k voxels bound the buffered commands
				const size_t chunkRuns = 4096;
				const long long chunkVoxels = 262144;
				std::vector<std::vector<long long>> cmds(chunkRuns);
				for (size_t r0 = 0, m = 0; r0 < runList.size(); r0 += m)
				{
					long long vox = 0;
					m = 0;
					while (r0 + m < runList.size() && m < chunkRuns && (m == 0 || vox < chunkVoxels))
					{
						int ri = runList[r0 + m];
						vox += runs.runEnd[ri] - runs.runStart[ri] + 1;
						m++;
					}
					ParallelFor((long long)m, threads, [&](long long k) {
						std::vector<long long>& cmd = cmds[(size_t)k];
						cmd.clear();
						int ri = runList[r0 + (size_t)k];
						int y = runs.runY[ri], z = runs.runZ[ri];
						V3 p, ps;
						int nn[3];
						p[1] = mg.phys(1, y); p[2] = mg.phys(2, z);
						for (int x = runs.runStart[ri]; x <= runs.runEnd[ri]; x++)
						{
							p[0] = mg.phys(0, x);
							field.stretch(p, nn);
							for (int d = 0; d < 3; d++) nn[d] = std::max(2, std::min(6, jint(std::ceil(nn[d] * mg.spacing[d] / tg.spacing[d]))));
							for (int i = 0; i < nn[0]; i++) for (int j = 0; j < nn[1]; j++) for (int kk = 0; kk < nn[2]; kk++)
							{
								ps[0] = p[0] + ((i + 0.5) / nn[0] - 0.5) * mg.spacing[0];
								ps[1] = p[1] + ((j + 0.5) / nn[1] - 0.5) * mg.spacing[1];
								ps[2] = p[2] + ((kk + 0.5) / nn[2] - 0.5) * mg.spacing[2];
								V3 q = field.map(ps);
								int tx = jint(std::floor(tg.index(0, q[0]) + 0.5)), ty = jint(std::floor(tg.index(1, q[1]) + 0.5)), tz = jint(std::floor(tg.index(2, q[2]) + 0.5));
								bool first = i == 0 && j == 0 && kk == 0;
								if (first) cmd.push_back(MARK);
								if (tx < 0 || ty < 0 || tz < 0 || tx >= tg.size[0] || ty >= tg.size[1] || tz >= tg.size[2]) { cmd.push_back(CLIP); continue; }
								cmd.push_back(((long long)tz * tg.size[1] + ty) * tg.size[0] + tx);
							}
						}
					});
					for (size_t k = 0; k < m; k++)
					{
						bool first = false;
						for (long long v : cmds[k])
						{
							if (v == MARK) { first = true; continue; }
							if (v == CLIP) { if (first) r.clipped++; first = false; continue; }
							int tx = (int)(v % tg.size[0]), ty = (int)((v / tg.size[0]) % tg.size[1]), tz = (int)(v / ((long long)tg.size[0] * tg.size[1]));
							if (sink.set(tx, ty, tz, r.label)) r.written++; else if (first) r.collisions++;
							first = false;
						}
					}
				}
			}

			// leave-one-out prediction error for every landmark (target units)
			std::vector<double> leaveOneOut()
			{
				int n = (int)lm.size();
				std::vector<double> err(n, std::numeric_limits<double>::quiet_NaN());
				if (n <= 4) return err;
				ParallelFor(n, threads, [&](long long ii) {
					int i = (int)ii;
					std::vector<V3> m, t;
					m.reserve(n - 1); t.reserve(n - 1);
					for (int j = 0; j < n; j++) if (j != i) { m.push_back(lm[j]); t.push_back(lt[j]); }
					try { err[i] = distance(Tps(m, t, tps.stiffness).apply(lm[i]), lt[i]); }
					catch (const std::exception&) { err[i] = std::numeric_limits<double>::quiet_NaN(); }
				});
				return err;
			}
		};

		std::string num(double v)
		{
			if (v != v) return "NaN";
			if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
			return shortest(v);
		}
		std::string csvText(const std::string& s)
		{
			std::string o = "\"";
			for (char ch : s) { if (ch == '"') o += "\"\""; else o += ch; }
			return o + "\"";
		}
	} // namespace

	std::string JavaDouble(double v)
	{
		if (v != v) return "NaN";
		if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
		if (v == 0) return std::signbit(v) ? "-0.0" : "0.0";
		// shortest digits, then Java's layout
		char buf[64];
		int p = 1;
		for (; p <= 17; ++p)
		{
			std::snprintf(buf, sizeof(buf), "%.*e", p - 1, v);
			if (std::strtod(buf, nullptr) == v) break;
		}
		std::string s(buf);
		bool neg = s[0] == '-';
		if (neg) s.erase(0, 1);
		size_t e = s.find('e');
		int exp10 = std::atoi(s.c_str() + e + 1);
		std::string digits;
		for (size_t i = 0; i < e; i++) if (s[i] != '.') digits += s[i];
		while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
		double a = std::fabs(v);
		std::string out;
		if (a >= 1e-3 && a < 1e7)
		{
			if (exp10 >= 0)
			{
				std::string ip = digits.substr(0, std::min(digits.size(), (size_t)exp10 + 1));
				while ((int)ip.size() < exp10 + 1) ip += '0';
				std::string fp = digits.size() > (size_t)exp10 + 1 ? digits.substr(exp10 + 1) : "0";
				out = ip + "." + fp;
			}
			else
				out = "0." + std::string(-exp10 - 1, '0') + digits;
		}
		else
			out = digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : "0") + "E" + std::to_string(exp10);
		return neg ? "-" + out : out;
	}

	std::vector<std::string> Report::ObjectColumns()
	{
		return { "Label", "label", "voxels", "moving_cx", "moving_cy", "moving_cz", "target_cx", "target_cy", "target_cz",
			"rotation_deg", "scale", "tps_vs_local_rms", "tps_vs_local_max", "tps_folded_fraction", "tps_jacobian_min",
			"tps_jacobian_max", "shape_anisotropy", "max_thickness", "thick_part_nonrigid_rms", "thin_part_tps_deviation_rms",
			"nearby_landmarks", "nearby_landmark_rms", "nearby_landmark_max", "written_voxels", "clipped_voxels", "collisions" };
	}
	std::vector<std::string> Report::LandmarkColumns()
	{
		return { "Label", "moving_x", "moving_y", "moving_z", "disp_x", "disp_y", "disp_z", "leave_one_out_error", "forward_jacobian_det", "flags" };
	}
	std::vector<std::string> Report::ObjectCells(const ObjectRow& r)
	{
		return { r.note, std::to_string(r.label), std::to_string(r.voxels), num(r.moving_c[0]), num(r.moving_c[1]), num(r.moving_c[2]),
			num(r.target_c[0]), num(r.target_c[1]), num(r.target_c[2]), num(r.rotation_deg), num(r.scale), num(r.tps_vs_local_rms),
			num(r.tps_vs_local_max), num(r.tps_folded_fraction), num(r.tps_jacobian_min), num(r.tps_jacobian_max), num(r.shape_anisotropy),
			num(r.max_thickness), num(r.thick_part_nonrigid_rms), num(r.thin_part_tps_deviation_rms), std::to_string(r.nearby_landmarks),
			num(r.nearby_landmark_rms), num(r.nearby_landmark_max), std::to_string(r.written), std::to_string(r.clipped), std::to_string(r.collisions) };
	}
	std::vector<std::string> Report::LandmarkCells(const LandmarkRow& r)
	{
		return { r.name, num(r.moving[0]), num(r.moving[1]), num(r.moving[2]), num(r.disp[0]), num(r.disp[1]), num(r.disp[2]),
			num(r.leave_one_out_error), num(r.forward_jacobian_det), r.flags };
	}
	std::string Report::ObjectsCsv() const
	{
		std::ostringstream o;
		std::vector<std::string> cols = ObjectColumns();
		for (size_t i = 0; i < cols.size(); i++) o << (i ? "," : "") << cols[i];
		o << "\n";
		for (const ObjectRow& r : objects)
		{
			std::vector<std::string> c = ObjectCells(r);
			for (size_t i = 0; i < c.size(); i++) o << (i ? "," : "") << (i == 0 ? csvText(c[i]) : c[i]);
			o << "\n";
		}
		return o.str();
	}
	std::string Report::LandmarksCsv() const
	{
		std::ostringstream o;
		std::vector<std::string> cols = LandmarkColumns();
		for (size_t i = 0; i < cols.size(); i++) o << (i ? "," : "") << cols[i];
		o << "\n";
		for (const LandmarkRow& r : landmarks)
		{
			std::vector<std::string> c = LandmarkCells(r);
			for (size_t i = 0; i < c.size(); i++) o << (i ? "," : "") << ((i == 0 || i + 1 == c.size()) ? csvText(c[i]) : c[i]);
			o << "\n";
		}
		return o.str();
	}

	std::vector<std::string> GpuDeviceNames()
	{
		std::vector<std::string> names;
		for (const GpuDevice& d : gpuDevices()) names.push_back(d.name);
		return names;
	}

	bool LoadLandmarksCsv(const std::string& path, Landmarks& out, std::string* error)
	{
		out = Landmarks();
		std::ifstream f(path);
		if (!f) { if (error) *error = "Landmark CSV does not exist: " + path; return false; }
		std::string line;
		int number = 0, dims = 0;
		auto trim = [](std::string s) {
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
		};
		while (std::getline(f, line))
		{
			number++;
			if (trim(line).empty()) continue;
			std::vector<std::string> fl;
			size_t s = 0;
			for (;;) { size_t c = line.find(',', s); fl.push_back(line.substr(s, c == std::string::npos ? std::string::npos : c - s)); if (c == std::string::npos) break; s = c + 1; }
			while (!fl.empty() && fl.back().empty()) fl.pop_back();   // Java split drops trailing empty strings
			for (std::string& x : fl) { x = trim(x); x.erase(std::remove(x.begin(), x.end(), '"'), x.end()); }
			auto fail = [&](const std::string& msg) { if (error) *error = "Line " + std::to_string(number) + ": " + msg; return false; };
			if (dims == 0)
			{
				if (fl.size() != 6 && fl.size() != 8) return fail("expected a 2D (6 columns) or 3D (8 columns) BigWarp CSV.");
				dims = ((int)fl.size() - 2) / 2;
			}
			if ((int)fl.size() != 2 + 2 * dims) return fail("inconsistent number of coordinates.");
			std::string act = fl[1];
			std::transform(act.begin(), act.end(), act.begin(), ::tolower);
			if (act != "true" && act != "false") return fail("active must be true or false (no header row).");
			if (act != "true") continue;
			V3 mm = { 0, 0, 0 }, tt = { 0, 0, 0 };
			for (int d = 0; d < dims; d++)
			{
				char* e1 = nullptr; char* e2 = nullptr;
				mm[d] = std::strtod(fl[2 + d].c_str(), &e1);
				tt[d] = std::strtod(fl[2 + dims + d].c_str(), &e2);
				if (fl[2 + d].empty() || fl[2 + dims + d].empty() || *e1 || *e2) return fail("invalid coordinate.");
				if (!std::isfinite(mm[d]) || !std::isfinite(tt[d])) return fail("active landmarks need finite coordinates.");
			}
			if (dims != 3) { if (error) *error = "This plugin needs 3D landmarks (8-column BigWarp CSV)."; return false; }
			out.names.push_back(fl[0]); out.moving.push_back(mm); out.target.push_back(tt);
		}
		if (out.moving.empty()) { if (error) *error = "Landmark CSV has no active landmarks."; return false; }
		return true;
	}

	bool Apply(const Volume& moving, const Grid& mg, const Grid& tg, void* out, const Landmarks& landmarks,
		const Params& P, Report& report, std::string* error, const ProgressFn& progress)
	{
		report = Report();
		auto t0 = std::chrono::steady_clock::now();
		auto log = [&](const std::string& s) { report.log.push_back(std::string(TITLE) + ": " + s); };
		try
		{
			if (!moving.data || !out) throw Failure("No volume data.");
			if (moving.bytes != 1 && moving.bytes != 2 && moving.bytes != 4) throw Failure("Moving: unsupported voxel type.");
			for (const Grid* g : { &mg, &tg })
			{
				for (int d = 0; d < 3; d++)
				{
					if (!std::isfinite(g->spacing[d]) || g->spacing[d] <= 0) throw Failure("Voxel sizes must be finite and positive.");
					if (!std::isfinite(g->origin[d])) throw Failure("Calibration origin must be finite.");
					if (g->size[d] <= 0) throw Failure("Empty volume.");
				}
			}
			if (landmarks.moving.empty() || landmarks.moving.size() != landmarks.target.size()) throw Failure("No landmarks.");
			const int local = P.local_mode;
			if (local < 0 || local > 3) throw Failure("Unknown local transform.");
			if (P.samples < 10) throw Failure("Use at least 10 TPS samples per object.");
			if (local == ADAPTIVE && (!(P.beta > 0) || !(P.sigma_min > 0) || !(P.neurite >= 0)))
				throw Failure("Adaptive mode needs a positive rigidity factor and minimum smoothing, and a non-negative neurite radius.");
			const int threads = NumThreads(P.threads);
			std::vector<V3> lmv(landmarks.moving.begin(), landmarks.moving.end()), ltv(landmarks.target.begin(), landmarks.target.end());
			Core core(lmv, ltv, P.stiffness, threads);
			report.lambda = core.tps.lambda;
			report.forward_rms = core.tps.rms(lmv, ltv);
			log(std::to_string(lmv.size()) + " landmarks, stiffness=" + JavaDouble(P.stiffness) + " (lambda=" + JavaDouble(core.tps.lambda)
				+ "), forward landmark RMS=" + fmt(report.forward_rms));
			if (progress && !progress(0, 1, "Finding objects...")) throw Cancelled();
			Runs runs;
			runs.build(moving, P.per_label, P.min_voxels);
			report.components = runs.componentCount; report.runs = runs.runCount;
			log(std::to_string(runs.componentCount) + " object(s) in " + std::to_string(runs.runCount) + " runs");
			Sink sink{ out, moving.bytes, tg.size[0], tg.size[1] };
			std::unique_ptr<GpuField> gpu;
			if (local == ADAPTIVE && P.device > 0)
			{
				try { gpu = GpuField::open(P.device - 1); log("adaptive field on GPU " + gpu->name); report.device = "GPU: " + gpu->name; }
				catch (const std::exception& e) { log(std::string("GPU unavailable (") + e.what() + "); using multi-threaded CPU."); gpu.reset(); }
			}
			if (local == ADAPTIVE && !gpu)
			{
				if (P.device == 0) log("adaptive field on CPU, " + std::to_string(threads) + " threads");
				report.device = "CPU, " + std::to_string(threads) + " threads";
			}
			std::vector<ObjectResult> results = core.place(runs, mg, tg, local, P.beta, P.sigma_min, P.neurite, P.samples, P.radius, gpu.get(), sink, progress);
			gpu.reset();
			for (const ObjectResult& r : results)
			{
				report.objects.push_back(r);
				if (local != ADAPTIVE && (r.tps_vs_local_rms > 0.5 * r.radius_of_gyration || r.tps_folded_fraction > 0))
				{
					std::string w = "WARNING label " + std::to_string(r.label) + " (" + std::to_string(r.voxels) + " voxels): the TPS wanted to deform this object strongly "
						+ "(RMS deviation " + fmt(r.tps_vs_local_rms) + ", folded samples " + fmt(100 * r.tps_folded_fraction) + "%). "
						+ "Nearby landmarks disagree with each other; see the landmarks table.";
					log(w);
					report.warnings.push_back(w);
				}
			}
			if (P.loo)
			{
				if (progress && !progress(0, 1, "Leave-one-out landmark diagnostics...")) throw Cancelled();
				report.has_landmarks = true;
				std::vector<double> err = core.leaveOneOut();
				std::vector<double> det(lmv.size());
				for (size_t i = 0; i < lmv.size(); i++) det[i] = core.tps.jacobianDeterminant(lmv[i]);
				std::vector<double> sorted = err;
				// Arrays.sort(double[]): NaN last, -0.0 before 0.0
				std::sort(sorted.begin(), sorted.end(), [](double x, double y) { return jcompare(x, y) < 0; });
				double median = sorted[sorted.size() / 2];
				int flagged = 0;
				for (size_t i = 0; i < err.size(); i++)
				{
					double dispd = distance(lmv[i], ltv[i]);
					std::string flag;
					if (dispd == 0) flag += "ZERO-DISPLACEMENT(unedited?) ";
					if (err[i] > jmax(3 * median, 1e-9)) flag += "INCONSISTENT(LOO>3xmedian) ";
					if (det[i] <= 0) flag += "FOLD(det<=0) ";
					if (!flag.empty()) flagged++;
					LandmarkRow row;
					row.name = i < landmarks.names.size() ? landmarks.names[i] : std::to_string(i + 1);
					for (int d = 0; d < 3; d++) { row.moving[d] = lmv[i][d]; row.disp[d] = ltv[i][d] - lmv[i][d]; }
					row.leave_one_out_error = err[i];
					row.forward_jacobian_det = det[i];
					while (!flag.empty() && flag.back() == ' ') flag.pop_back();
					row.flags = flag;
					report.landmarks.push_back(row);
				}
				log("leave-one-out median error " + fmt(median) + "; " + std::to_string(flagged) + " landmark(s) flagged (see landmarks table).");
			}
			report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			log("done in " + fmt(report.seconds) + " s");
			return true;
		}
		catch (const Cancelled&)
		{
			if (error) *error = "Cancelled.";
		}
		catch (const std::bad_alloc&)
		{
			if (error) *error = "Not enough memory.";
		}
		catch (const std::exception& e)
		{
			if (error) *error = e.what();
		}
		report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		return false;
	}
} // namespace rigidcells

// ------------------------------------------------------------------ VolumeData glue
#ifndef RIGIDCELLS_CORE_ONLY

VolumeData* RigidCellsWarpVolumeByRulers(VolumeData* src, const std::vector<Ruler*>& rulers,
	const rigidcells::Params& params, rigidcells::Report* report_out, VVDString* error, VolumeData* target)
{
	using namespace FLIVR;
	if (error) error->clear();
	if (!src) return nullptr;
	if (target == src) target = nullptr;
	const bool reversed = WarpRulersReversed(src, target, rulers);
	const int im = reversed ? 1 : 0, ifx = reversed ? 0 : 1;
	Texture* tex = src->GetTexture();
	if (!tex) return nullptr;
	if (src->isBrxml())
	{
		if (error) *error = L"The cell warp needs a plain (non-pyramid) volume.";
		return nullptr;
	}
	int nb = tex->nb(0);
	if (tex->nc() > 1 || (nb != 1 && nb != 2 && nb != 4))
	{
		if (error) *error = L"The cell warp supports single-channel label volumes only.";
		return nullptr;
	}
	Nrrd* nrrd = tex->get_nrrd(0) ? tex->get_nrrd(0)->getNrrd() : nullptr;
	if (!nrrd || !nrrd->data)
	{
		if (error) *error = L"The volume data are not in memory.";
		return nullptr;
	}
	Transform* tf = tex->transform();
	if (!tf) return nullptr;

	int resx = 0, resy = 0, resz = 0;
	double spcx = 1, spcy = 1, spcz = 1;
	src->GetResolution(resx, resy, resz);
	src->GetSpacings(spcx, spcy, spcz);
	rigidcells::Grid g;
	g.size[0] = resx; g.size[1] = resy; g.size[2] = resz;
	g.spacing[0] = spcx; g.spacing[1] = spcy; g.spacing[2] = spcz;
	const double aspect[3] = { resx * spcx, resy * spcy, resz * spcz };

	// landmarks exactly as WarpVolumeByRulers (2-point ruler: point 0 moving,
	// point 1 target; locator: zero-displacement anchor), in physical units:
	// the grid-normalized point times the physical box size. The rulers hold
	// (x, y, -z) of the BigWarp CSV; when that is the same point (no extra
	// transform) it is used directly so the CSV values arrive unrounded.
	auto physical = [&](const Point& wp) {
		Point n = tf->unproject(wp);
		std::array<double, 3> p = { n.x() * aspect[0], n.y() * aspect[1], n.z() * aspect[2] };
		std::array<double, 3> direct = { wp.x(), wp.y(), -wp.z() };
		double tol = 1e-9 * std::max({ 1.0, std::fabs(aspect[0]), std::fabs(aspect[1]), std::fabs(aspect[2]) });
		if (std::fabs(p[0] - direct[0]) <= tol && std::fabs(p[1] - direct[1]) <= tol && std::fabs(p[2] - direct[2]) <= tol)
			return direct;
		return p;
	};
	rigidcells::Landmarks lm;
	for (size_t i = 0; i < rulers.size(); ++i)
	{
		Ruler* r = rulers[i];
		if (!r) continue;
		std::wstring wname = r->GetNameDisp().ToStdWstring();
		std::string name(wname.begin(), wname.end());
		if (r->GetRulerType() == 0 && r->GetNumPoint() >= 2)
		{
			Point* p0 = r->GetPoint(im);
			Point* p1 = r->GetPoint(ifx);
			if (!p0 || !p1) continue;
			lm.names.push_back(name);
			lm.moving.push_back(physical(*p0));
			lm.target.push_back(physical(*p1));
		}
		else if (r->GetRulerType() == 2 && r->GetNumPoint() >= 1)
		{
			Point* p0 = r->GetPoint(0);
			if (!p0) continue;
			lm.names.push_back(name);
			lm.moving.push_back(physical(*p0));
			lm.target.push_back(lm.moving.back());
		}
	}
	if (lm.moving.size() < 4)
	{
		if (error) *error = L"Need at least 4 landmark pairs (2-point rulers) for the cell transform.";
		return nullptr;
	}

	static const wchar_t* const suffixes[4] = { L"_CELLS_ADAPTIVE", L"_CELLS_TRANS", L"_CELLS_RIGID", L"_CELLS_SIM" };
	int mode = std::min(3, std::max(0, params.local_mode));
	VolumeData* vd = nullptr;
	rigidcells::Grid tg = g;
	if (!target)
	{
		vd = new VolumeData();
		vd->AddEmptyData(nb * 8, resx, resy, resz, spcx, spcy, spcz);
		vd->SetSpcFromFile(true);
	}
	else
	{
		//the target's grid, its origin in src's physical frame (the plugin's
		//index * spacing + origin)
		VVDString terr;
		vd = NewWarpResultVolume(src, target, &terr);
		if (!vd)
		{
			if (error) *error = terr.empty() ? VVDString(L"Not enough memory for the result volume.") : terr;
			return nullptr;
		}
		double ms[3], mo[3];
		if (!WarpTargetGridMap(src, vd, ms, mo, &terr))
		{
			delete vd;
			if (error) *error = terr;
			return nullptr;
		}
		int trx = 0, try_ = 0, trz = 0;
		double tsx = 1, tsy = 1, tsz = 1;
		vd->GetResolution(trx, try_, trz);
		vd->GetSpacings(tsx, tsy, tsz);
		tg.size[0] = trx; tg.size[1] = try_; tg.size[2] = trz;
		tg.spacing[0] = tsx; tg.spacing[1] = tsy; tg.spacing[2] = tsz;
		for (int d = 0; d < 3; d++)
			tg.origin[d] = mo[d] * aspect[d];
	}
	vd->SetName(src->GetName() + suffixes[mode]);
	Nrrd* onrrd = (vd->GetTexture() && vd->GetTexture()->get_nrrd(0)) ? vd->GetTexture()->get_nrrd(0)->getNrrd() : nullptr;
	if (!onrrd || !onrrd->data)
	{
		delete vd;
		if (error) *error = L"Not enough memory for the result volume.";
		return nullptr;
	}

	rigidcells::Volume mv;
	mv.data = nrrd->data; mv.bytes = nb; mv.nx = resx; mv.ny = resy; mv.nz = resz;
	std::unique_ptr<vvd::Progress> prog = vvd::UI::CreateProgress(L"Apply Transform", L"Finding objects...", 100,
		vvd::PROGRESS_DEFAULT | vvd::PROGRESS_CAN_ABORT);
	rigidcells::ProgressFn pf = [&](int done, int total, const std::string& what) {
		if (!prog) return true;
		int v = total > 0 ? (int)(100.0 * done / total) : 0;
		if (v > 99) v = 99;
		return prog->Update(v, VVDString(std::wstring(what.begin(), what.end())));
	};
	rigidcells::Report local_report;
	rigidcells::Report& rep = report_out ? *report_out : local_report;
	std::string err;
	bool ok = rigidcells::Apply(mv, g, tg, onrrd->data, lm, params, rep, &err, pf);
	prog.reset();
	for (const std::string& line : rep.log)
		std::fprintf(stderr, "%s\n", line.c_str());
	if (!ok)
	{
		delete vd;
		if (error && err != "Cancelled.") *error = VVDString(std::wstring(err.begin(), err.end()));
		return nullptr;
	}

	//maximum value (VolumeData::Warp)
	unsigned long long count = (unsigned long long)tg.size[0] * tg.size[1] * tg.size[2];
	double max_val = 255;
	if (nb == 1) max_val = *std::max_element((uint8_t*)onrrd->data, (uint8_t*)onrrd->data + count);
	else if (nb == 2) max_val = *std::max_element((uint16_t*)onrrd->data, (uint16_t*)onrrd->data + count);
	else max_val = *std::max_element((float*)onrrd->data, (float*)onrrd->data + count);
	vd->SetMaxValue(max_val);

	CopyWarpDisplayProperties(src, vd);
	return vd;
}
#endif // RIGIDCELLS_CORE_ONLY
