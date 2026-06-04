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

} // end namespace FLIVR
