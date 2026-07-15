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

// ThinPlateSpline: a 3D Thin Plate Spline transform equivalent to the one used
// by Fiji's BigWarp (jitk-tps). The radial basis kernel is U(r) = r^2*log(r)
// in all dimensions. The forward transform F maps source landmarks onto target
// landmarks: F(src_i) ~= tgt_i, where
//     F(m) = A*m + b + sum_i W_i * U(|m - src_i|).
// The coefficients (A, b, W) are obtained by solving a dense (N+4)x(N+4) linear
// system on the CPU (N is the number of landmarks, typically small). The inverse
// transform (needed for image resampling) is computed numerically with a
// Gauss-Newton iteration plus backtracking line search, matching jitk-tps.
//
// The same class also provides BigWarp's *linear* transform models
// (Translation, Rigid, Similarity, Affine) through the solveLinear* methods.
// These produce a pure linear transform F(m) = A*m + b with NO radial-basis
// terms (num_landmarks() == 0), which the GPU warp shader resamples exactly via
// the analytic affine inverse. Only the way A and b are fit to the landmark
// pairs differs between models; the forward/inverse evaluation and the GPU path
// are shared with the TPS case.

#ifndef ThinPlateSpline_h
#define ThinPlateSpline_h

#include <glm/glm.hpp>
#include <vector>

#include "DLLExport.h"

namespace FLIVR
{
	class EXPORT_API ThinPlateSpline
	{
	public:
		ThinPlateSpline();
		~ThinPlateSpline();

		// Build the forward transform F with F(src_i) ~= tgt_i.
		// src and tgt must have the same size (>= 4 for a 3D spline).
		// lambda is the stiffness/regularization (0 == exact interpolation).
		// Returns false if the system is degenerate (e.g. coplanar landmarks).
		bool solve(const std::vector<glm::dvec3>& src,
			const std::vector<glm::dvec3>& tgt,
			double lambda = 0.0);

		// BigWarp linear transform models. Each fits A,b so that F(src_i) ~= tgt_i
		// (moving -> fixed) in a least-squares sense and leaves num_landmarks()==0
		// (no radial-basis terms). Return false on degenerate/insufficient input.
		//   Translation: A = I, b = mean(tgt) - mean(src)            (>= 1 pair)
		//   Rigid:       A = R   (proper rotation, Horn quaternion)  (>= 3 pairs, non-collinear)
		//   Similarity:  A = s*R (uniform scale + rotation)          (>= 3 pairs, non-collinear)
		//   Affine:      A = general 3x3 (normal equations)          (>= 4 pairs, non-coplanar)
		bool solveTranslation(const std::vector<glm::dvec3>& src,
			const std::vector<glm::dvec3>& tgt);
		// Rigid/Similarity impose isotropic constraints (one rotation / one
		// uniform scale), so they must be fit in a physically isotropic space.
		// Landmarks arrive in grid-normalized [0,1] coords whose axes have very
		// different physical lengths (res*spacing); `aspect` is that per-axis
		// physical length. The fit is done in aspect-scaled (isotropic) space and
		// the resulting A,b are mapped back to grid-normalized coords (what the GPU
		// warp samples in). Default (1,1,1) == treat the input as already isotropic.
		bool solveRigid(const std::vector<glm::dvec3>& src,
			const std::vector<glm::dvec3>& tgt,
			const glm::dvec3& aspect = glm::dvec3(1.0));
		bool solveSimilarity(const std::vector<glm::dvec3>& src,
			const std::vector<glm::dvec3>& tgt,
			const glm::dvec3& aspect = glm::dvec3(1.0));
		// Affine/TPS absorb axis anisotropy in their own degrees of freedom, so
		// they need no aspect and are fit directly in grid-normalized coords.
		bool solveAffine(const std::vector<glm::dvec3>& src,
			const std::vector<glm::dvec3>& tgt);

		bool valid() const { return valid_; }
		int num_landmarks() const { return static_cast<int>(src_.size()); }

		// Forward transform F(m).
		glm::dvec3 evaluate(const glm::dvec3& m) const;
		// Jacobian of F at m.
		glm::dmat3 jacobian(const glm::dvec3& m) const;
		// Numeric inverse: find m such that F(m) ~= f (Gauss-Newton + backtracking).
		// Uses the same algorithm as the GPU shader so behavior matches.
		bool evaluateInverse(const glm::dvec3& f, glm::dvec3& m_out,
			int maxIters = 20, double eps = 1e-6) const;

		const glm::dmat3& affine() const { return A_; }
		const glm::dmat3& affineInv() const { return Ainv_; }
		const glm::dvec3& translation() const { return b_; }
		const std::vector<glm::dvec3>& sources() const { return src_; }
		const std::vector<glm::dvec3>& weights() const { return W_; }

	private:
		// U(r) expressed via squared distance: r^2*log(r) = 0.5*r2*log(r2).
		static double kernel(double r2);
		// Solve a dense n x n system with rhs columns (row-major, in place).
		// On success B holds the solution; returns false if singular.
		static bool solveDense(std::vector<double>& A, int n,
			std::vector<double>& B, int rhs);

		// Finish a pure-linear fit: clear the radial-basis terms (N==0), compute
		// Ainv_, set valid_. Returns false if A_ is singular (non-invertible).
		bool finalizeLinear();

		// Map a linear transform fitted in isotropic (aspect-scaled) space into the
		// grid-normalized coords the GPU samples in: A_ = Dinv*Aiso*D, b_ = Dinv*biso
		// with D = diag(aspect). Writes A_ and b_.
		void mapFromIsotropic(const glm::dmat3& Aiso, const glm::dvec3& biso,
			const glm::dvec3& aspect);

		// Largest-eigenvalue eigenvector of a symmetric 4x4 matrix (row-major) via
		// cyclic Jacobi rotations. Writes the unit eigenvector into evec[4].
		static void jacobiEigenSym4(const double M[16], double evec[4]);

		// Least-squares proper rotation mapping (src-sc) onto (tgt-tc) (Horn 1987,
		// unit-quaternion method). sc/tc are the source/target centroids.
		static glm::dmat3 fitRotation(const std::vector<glm::dvec3>& src,
			const std::vector<glm::dvec3>& tgt,
			const glm::dvec3& sc, const glm::dvec3& tc);

		std::vector<glm::dvec3> src_;   // source landmarks
		std::vector<glm::dvec3> W_;     // per-landmark weight vectors
		glm::dmat3 A_;                  // affine linear part (applied as A_*m)
		glm::dmat3 Ainv_;               // inverse of A_ (initial guess for inverse)
		glm::dvec3 b_;                  // translation
		bool valid_;
	};

} // end namespace FLIVR

#endif // ThinPlateSpline_h
