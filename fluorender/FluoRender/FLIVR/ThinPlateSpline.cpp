//
//  For more information, please see: http://software.sci.utah.edu
//
//  The MIT License
//
//  Copyright (c) 2004 Scientific Computing and Imaging Institute,
//  University of Utah.
//
//
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//

#include <FLIVR/ThinPlateSpline.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <algorithm>
#include <utility>

namespace FLIVR
{
	ThinPlateSpline::ThinPlateSpline()
		: A_(1.0), Ainv_(1.0), b_(0.0), valid_(false)
	{}

	ThinPlateSpline::~ThinPlateSpline()
	{}

	double ThinPlateSpline::kernel(double r2)
	{
		// U(r) = r^2 * log(r) = 0.5 * r2 * log(r2)
		return r2 > 1e-12 ? 0.5 * r2 * std::log(r2) : 0.0;
	}

	bool ThinPlateSpline::solveDense(std::vector<double>& A, int n,
		std::vector<double>& B, int rhs)
	{
		const double EPS = 1e-12;
		// Forward elimination with partial pivoting.
		for (int k = 0; k < n; ++k)
		{
			int piv = k;
			double maxv = std::fabs(A[(size_t)k * n + k]);
			for (int i = k + 1; i < n; ++i)
			{
				double v = std::fabs(A[(size_t)i * n + k]);
				if (v > maxv) { maxv = v; piv = i; }
			}
			if (maxv < EPS)
				return false;
			if (piv != k)
			{
				for (int j = 0; j < n; ++j)
					std::swap(A[(size_t)k * n + j], A[(size_t)piv * n + j]);
				for (int j = 0; j < rhs; ++j)
					std::swap(B[(size_t)k * rhs + j], B[(size_t)piv * rhs + j]);
			}
			double diag = A[(size_t)k * n + k];
			for (int i = k + 1; i < n; ++i)
			{
				double f = A[(size_t)i * n + k] / diag;
				if (f == 0.0)
					continue;
				for (int j = k; j < n; ++j)
					A[(size_t)i * n + j] -= f * A[(size_t)k * n + j];
				for (int j = 0; j < rhs; ++j)
					B[(size_t)i * rhs + j] -= f * B[(size_t)k * rhs + j];
			}
		}
		// Back substitution.
		for (int k = n - 1; k >= 0; --k)
		{
			double diag = A[(size_t)k * n + k];
			for (int j = 0; j < rhs; ++j)
			{
				double s = B[(size_t)k * rhs + j];
				for (int c = k + 1; c < n; ++c)
					s -= A[(size_t)k * n + c] * B[(size_t)c * rhs + j];
				B[(size_t)k * rhs + j] = s / diag;
			}
		}
		return true;
	}

	bool ThinPlateSpline::solve(const std::vector<glm::dvec3>& src,
		const std::vector<glm::dvec3>& tgt, double lambda)
	{
		valid_ = false;
		src_.clear();
		W_.clear();
		A_ = glm::dmat3(1.0);
		Ainv_ = glm::dmat3(1.0);
		b_ = glm::dvec3(0.0);

		if (src.size() != tgt.size() || src.size() < 4)
			return false;

		const int N = static_cast<int>(src.size());
		const int n = N + 4;
		std::vector<double> A((size_t)n * n, 0.0);
		std::vector<double> B((size_t)n * 3, 0.0);

		// K block: K[i][j] = U(|src_i - src_j|)
		for (int i = 0; i < N; ++i)
		{
			for (int j = 0; j < N; ++j)
			{
				glm::dvec3 d = src[i] - src[j];
				A[(size_t)i * n + j] = kernel(glm::dot(d, d));
			}
		}
		// regularization on the diagonal (stiffness)
		for (int i = 0; i < N; ++i)
			A[(size_t)i * n + i] += lambda;

		// P block (row [1 x y z]) and its transpose
		for (int i = 0; i < N; ++i)
		{
			A[(size_t)i * n + (N + 0)] = 1.0;        A[(size_t)(N + 0) * n + i] = 1.0;
			A[(size_t)i * n + (N + 1)] = src[i].x;   A[(size_t)(N + 1) * n + i] = src[i].x;
			A[(size_t)i * n + (N + 2)] = src[i].y;   A[(size_t)(N + 2) * n + i] = src[i].y;
			A[(size_t)i * n + (N + 3)] = src[i].z;   A[(size_t)(N + 3) * n + i] = src[i].z;
		}

		// RHS = target coordinates (3 columns); bottom 4 rows are zero
		for (int i = 0; i < N; ++i)
		{
			B[(size_t)i * 3 + 0] = tgt[i].x;
			B[(size_t)i * 3 + 1] = tgt[i].y;
			B[(size_t)i * 3 + 2] = tgt[i].z;
		}

		if (!solveDense(A, n, B, 3))
			return false;

		src_ = src;
		W_.resize(N);
		for (int i = 0; i < N; ++i)
			W_[i] = glm::dvec3(B[(size_t)i * 3 + 0], B[(size_t)i * 3 + 1], B[(size_t)i * 3 + 2]);

		// affine: translation (constant row) and the 3 linear coefficient rows.
		b_ = glm::dvec3(B[(size_t)(N + 0) * 3 + 0], B[(size_t)(N + 0) * 3 + 1], B[(size_t)(N + 0) * 3 + 2]);
		// glm is column major: A_[col][row]. Column c holds the coefficients of m[c].
		A_[0] = glm::dvec3(B[(size_t)(N + 1) * 3 + 0], B[(size_t)(N + 1) * 3 + 1], B[(size_t)(N + 1) * 3 + 2]);
		A_[1] = glm::dvec3(B[(size_t)(N + 2) * 3 + 0], B[(size_t)(N + 2) * 3 + 1], B[(size_t)(N + 2) * 3 + 2]);
		A_[2] = glm::dvec3(B[(size_t)(N + 3) * 3 + 0], B[(size_t)(N + 3) * 3 + 1], B[(size_t)(N + 3) * 3 + 2]);

		if (std::fabs(glm::determinant(A_)) < 1e-12)
			Ainv_ = glm::dmat3(1.0);
		else
			Ainv_ = glm::inverse(A_);

		valid_ = true;
		return true;
	}

	glm::dvec3 ThinPlateSpline::evaluate(const glm::dvec3& m) const
	{
		glm::dvec3 r = A_ * m + b_;
		const size_t N = src_.size();
		for (size_t i = 0; i < N; ++i)
		{
			glm::dvec3 d = m - src_[i];
			r += W_[i] * kernel(glm::dot(d, d));
		}
		return r;
	}

	glm::dmat3 ThinPlateSpline::jacobian(const glm::dvec3& m) const
	{
		glm::dmat3 J = A_;
		const size_t N = src_.size();
		for (size_t i = 0; i < N; ++i)
		{
			glm::dvec3 d = m - src_[i];
			double r2 = glm::dot(d, d);
			if (r2 > 1e-12)
			{
				// dU/dm = d * (log(r2) + 1); contribution to J is outer(W_i, dU/dm).
				// glm::outerProduct(c, r)[col][row] = c[row]*r[col]
				J += glm::outerProduct(W_[i], d) * (std::log(r2) + 1.0);
			}
		}
		return J;
	}

	bool ThinPlateSpline::evaluateInverse(const glm::dvec3& f, glm::dvec3& m_out,
		int maxIters, double eps) const
	{
		const double beta = 0.5;       // line-search step reduction
		const double cArmijo = 1e-4;   // sufficient-decrease constant
		const int lsTries = 15;

		glm::dvec3 m = Ainv_ * (f - b_);   // affine-inverse initial guess
		for (int it = 0; it < maxIters; ++it)
		{
			glm::dvec3 err = f - evaluate(m);
			if (glm::length(err) < eps)
			{
				m_out = m;
				return true;
			}
			glm::dmat3 J = jacobian(m);
			if (std::fabs(glm::determinant(J)) < 1e-12)
				break;
			glm::dvec3 dir = glm::inverse(J) * err;   // Gauss-Newton step

			double c0 = 0.5 * glm::dot(err, err);
			glm::dvec3 gC = -glm::transpose(J) * err; // gradient of 0.5|f-F(m)|^2
			double md = glm::dot(gC, dir);
			double t = 1.0;
			for (int ls = 0; ls < lsTries; ++ls)
			{
				glm::dvec3 e2 = f - evaluate(m + t * dir);
				if (0.5 * glm::dot(e2, e2) <= c0 + cArmijo * t * md)
					break;
				t *= beta;
			}
			m += t * dir;
		}
		m_out = m;
		return glm::length(f - evaluate(m)) < eps * 100.0;
	}

	bool ThinPlateSpline::finalizeLinear()
	{
		// pure linear transform: no radial-basis terms
		src_.clear();
		W_.clear();
		if (std::fabs(glm::determinant(A_)) < 1e-12)
		{
			valid_ = false;
			return false;
		}
		Ainv_ = glm::inverse(A_);
		valid_ = true;
		return true;
	}

	void ThinPlateSpline::jacobiEigenSym4(const double M[16], double evec[4])
	{
		// cyclic Jacobi eigenvalue decomposition of a symmetric 4x4 matrix.
		double a[4][4], v[4][4];
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j)
			{
				a[i][j] = M[(size_t)i * 4 + j];
				v[i][j] = (i == j) ? 1.0 : 0.0;
			}

		for (int sweep = 0; sweep < 50; ++sweep)
		{
			double off = 0.0;
			for (int i = 0; i < 4; ++i)
				for (int j = i + 1; j < 4; ++j)
					off += a[i][j] * a[i][j];
			if (off < 1e-30)
				break;

			for (int p = 0; p < 4; ++p)
				for (int q = p + 1; q < 4; ++q)
				{
					double apq = a[p][q];
					if (std::fabs(apq) < 1e-300)
						continue;
					double phi = 0.5 * (a[q][q] - a[p][p]) / apq;
					double t = (phi >= 0.0 ? 1.0 : -1.0) /
						(std::fabs(phi) + std::sqrt(phi * phi + 1.0));
					double c = 1.0 / std::sqrt(t * t + 1.0);
					double s = t * c;
					// A <- A * J (rotate columns p,q)
					for (int k = 0; k < 4; ++k)
					{
						double akp = a[k][p], akq = a[k][q];
						a[k][p] = c * akp - s * akq;
						a[k][q] = s * akp + c * akq;
					}
					// A <- J^T * A (rotate rows p,q)
					for (int k = 0; k < 4; ++k)
					{
						double apk = a[p][k], aqk = a[q][k];
						a[p][k] = c * apk - s * aqk;
						a[q][k] = s * apk + c * aqk;
					}
					// V <- V * J (accumulate eigenvectors)
					for (int k = 0; k < 4; ++k)
					{
						double vkp = v[k][p], vkq = v[k][q];
						v[k][p] = c * vkp - s * vkq;
						v[k][q] = s * vkp + c * vkq;
					}
				}
		}

		int best = 0;
		for (int i = 1; i < 4; ++i)
			if (a[i][i] > a[best][best])
				best = i;
		for (int i = 0; i < 4; ++i)
			evec[i] = v[i][best];
		double n = std::sqrt(evec[0] * evec[0] + evec[1] * evec[1] +
			evec[2] * evec[2] + evec[3] * evec[3]);
		if (n > 1e-300)
			for (int i = 0; i < 4; ++i)
				evec[i] /= n;
		else
		{
			evec[0] = 1.0; evec[1] = evec[2] = evec[3] = 0.0;
		}
	}

	glm::dmat3 ThinPlateSpline::fitRotation(const std::vector<glm::dvec3>& src,
		const std::vector<glm::dvec3>& tgt, const glm::dvec3& sc, const glm::dvec3& tc)
	{
		// cross-covariance M = sum (src_i - sc)(tgt_i - tc)^T; Sab = sum p'.a * q'.b
		double Sxx = 0, Sxy = 0, Sxz = 0;
		double Syx = 0, Syy = 0, Syz = 0;
		double Szx = 0, Szy = 0, Szz = 0;
		const size_t N = src.size();
		for (size_t i = 0; i < N; ++i)
		{
			glm::dvec3 p = src[i] - sc;
			glm::dvec3 q = tgt[i] - tc;
			Sxx += p.x * q.x; Sxy += p.x * q.y; Sxz += p.x * q.z;
			Syx += p.y * q.x; Syy += p.y * q.y; Syz += p.y * q.z;
			Szx += p.z * q.x; Szy += p.z * q.y; Szz += p.z * q.z;
		}
		// Horn 1987 symmetric 4x4 (row-major); top eigenvector = rotation quaternion
		double Nm[16] = {
			Sxx + Syy + Szz, Syz - Szy,        Szx - Sxz,        Sxy - Syx,
			Syz - Szy,       Sxx - Syy - Szz,  Sxy + Syx,        Szx + Sxz,
			Szx - Sxz,       Sxy + Syx,       -Sxx + Syy - Szz,  Syz + Szy,
			Sxy - Syx,       Szx + Sxz,        Syz + Szy,       -Sxx - Syy + Szz
		};
		double q[4];
		jacobiEigenSym4(Nm, q);   // (w, x, y, z)
		glm::dquat quat(q[0], q[1], q[2], q[3]);
		// glm::mat3_cast returns a column-major rotation matrix that matches our
		// A_ convention exactly (A_[col][row] == R_math[row][col]).
		return glm::mat3_cast(quat);
	}

	bool ThinPlateSpline::solveTranslation(const std::vector<glm::dvec3>& src,
		const std::vector<glm::dvec3>& tgt)
	{
		valid_ = false;
		src_.clear(); W_.clear();
		A_ = glm::dmat3(1.0); Ainv_ = glm::dmat3(1.0); b_ = glm::dvec3(0.0);
		if (src.size() != tgt.size() || src.empty())
			return false;
		glm::dvec3 sc(0.0), tc(0.0);
		for (size_t i = 0; i < src.size(); ++i) { sc += src[i]; tc += tgt[i]; }
		double inv = 1.0 / (double)src.size();
		sc *= inv; tc *= inv;
		A_ = glm::dmat3(1.0);
		b_ = tc - sc;
		return finalizeLinear();
	}

	bool ThinPlateSpline::solveRigid(const std::vector<glm::dvec3>& src,
		const std::vector<glm::dvec3>& tgt)
	{
		valid_ = false;
		src_.clear(); W_.clear();
		A_ = glm::dmat3(1.0); Ainv_ = glm::dmat3(1.0); b_ = glm::dvec3(0.0);
		if (src.size() != tgt.size() || src.size() < 3)
			return false;
		glm::dvec3 sc(0.0), tc(0.0);
		for (size_t i = 0; i < src.size(); ++i) { sc += src[i]; tc += tgt[i]; }
		double inv = 1.0 / (double)src.size();
		sc *= inv; tc *= inv;
		A_ = fitRotation(src, tgt, sc, tc);
		b_ = tc - A_ * sc;
		return finalizeLinear();
	}

	bool ThinPlateSpline::solveSimilarity(const std::vector<glm::dvec3>& src,
		const std::vector<glm::dvec3>& tgt)
	{
		valid_ = false;
		src_.clear(); W_.clear();
		A_ = glm::dmat3(1.0); Ainv_ = glm::dmat3(1.0); b_ = glm::dvec3(0.0);
		if (src.size() != tgt.size() || src.size() < 3)
			return false;
		glm::dvec3 sc(0.0), tc(0.0);
		for (size_t i = 0; i < src.size(); ++i) { sc += src[i]; tc += tgt[i]; }
		double inv = 1.0 / (double)src.size();
		sc *= inv; tc *= inv;
		glm::dmat3 R = fitRotation(src, tgt, sc, tc);
		// least-squares uniform scale for the fixed optimal rotation
		double num = 0.0, den = 0.0;
		for (size_t i = 0; i < src.size(); ++i)
		{
			glm::dvec3 p = src[i] - sc;
			glm::dvec3 q = tgt[i] - tc;
			num += glm::dot(q, R * p);
			den += glm::dot(p, p);
		}
		double s = (den > 1e-12) ? num / den : 1.0;
		A_ = R; A_[0] *= s; A_[1] *= s; A_[2] *= s;   // s * R
		b_ = tc - A_ * sc;
		return finalizeLinear();
	}

	bool ThinPlateSpline::solveAffine(const std::vector<glm::dvec3>& src,
		const std::vector<glm::dvec3>& tgt)
	{
		valid_ = false;
		src_.clear(); W_.clear();
		A_ = glm::dmat3(1.0); Ainv_ = glm::dmat3(1.0); b_ = glm::dvec3(0.0);
		if (src.size() != tgt.size() || src.size() < 4)
			return false;

		// normal equations: minimize sum |tgt_i - (A*src_i + b)|^2.
		// Per output axis k this is a 4-parameter fit [m_x m_y m_z t] sharing the
		// same 4x4 Gram matrix G; solve all 3 axes at once (rhs = 3).
		std::vector<double> G((size_t)4 * 4, 0.0);
		std::vector<double> H((size_t)4 * 3, 0.0);
		for (size_t i = 0; i < src.size(); ++i)
		{
			const double pv[4] = { src[i].x, src[i].y, src[i].z, 1.0 };
			const double tv[3] = { tgt[i].x, tgt[i].y, tgt[i].z };
			for (int a = 0; a < 4; ++a)
			{
				for (int bb = 0; bb < 4; ++bb)
					G[(size_t)a * 4 + bb] += pv[a] * pv[bb];
				for (int k = 0; k < 3; ++k)
					H[(size_t)a * 3 + k] += pv[a] * tv[k];
			}
		}
		if (!solveDense(G, 4, H, 3))
			return false;

		// rows 0..2 of H are the linear coefficients (columns of A_ in glm's
		// column-major layout), row 3 is the translation.
		A_[0] = glm::dvec3(H[0 * 3 + 0], H[0 * 3 + 1], H[0 * 3 + 2]);
		A_[1] = glm::dvec3(H[1 * 3 + 0], H[1 * 3 + 1], H[1 * 3 + 2]);
		A_[2] = glm::dvec3(H[2 * 3 + 0], H[2 * 3 + 1], H[2 * 3 + 2]);
		b_ = glm::dvec3(H[3 * 3 + 0], H[3 * 3 + 1], H[3 * 3 + 2]);
		return finalizeLinear();
	}

} // end namespace FLIVR
