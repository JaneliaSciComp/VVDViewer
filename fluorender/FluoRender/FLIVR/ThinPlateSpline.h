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

// ThinPlateSpline: the resampling transform of the GPU volume warp, equivalent
// to Fiji's BigWarp (jitk-tps TPS and mpicbg linear models).
//
// The class stores the *backward* map G the resampler needs: it takes a point
// of the output (fixed/target) volume and returns where to sample the moving
// (source) volume, both in grid-normalized [0,1] coords. Like BigWarp
// (TpsTransformSolver: ThinPlateR2LogRSplineKernelTransform(tgtPts, mvgPts))
// the TPS is fit directly in that direction and evaluated directly; it is never
// inverted numerically. (The inverse of a moving->fixed TPS is a different map
// whose local stretch diverges as that map folds under large displacements,
// undersampling thin structures.)
//
// TPS:  G(f) = A*u + b + sum_i W_i * U(|u - k_i|),  U(r) = r^2*log(r),
//       u = (f*aspect - c) / R
// The fit is done in isotropic physical space (aspect = per-axis physical
// length of the normalized box), centered on the fixed landmarks' centroid c
// and scaled by their RMS radius R, like the ImageJ "Apply BigWarp with
// Stiffness" plugin. The r^2*log(r) TPS is invariant to that similarity, so
// this is the same TPS BigWarp fits in physical units; the plugin's
// dimensionless stiffness s (lambda = s*R^2 in physical units) becomes plain s
// on the kernel diagonal. s = 0 is BigWarp's unregularized TPS. The
// coefficients come from a dense (N+4)x(N+4) solve on the CPU.
//
// Linear models (Translation, Rigid, Similarity, Affine) are fit moving->fixed
// in a least-squares sense like BigWarp's ModelTransformSolver and stored
// inverted with no radial-basis terms (num_landmarks() == 0, aspect = (1,1,1),
// c = 0, R = 1), so G(f) = A*f + b.

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

		// Fit the TPS resampling map with G(tgt_i) ~= src_i (src = moving, tgt =
		// fixed landmarks, grid-normalized; same size, >= 4). aspect is the
		// per-axis physical length of the normalized box (res*spacing); the
		// kernel is evaluated in that isotropic space. stiffness is the
		// dimensionless regularization of the ImageJ plugin (0 == exact
		// interpolation, BigWarp). Returns false if the system is degenerate
		// (duplicate fixed points, collinear/coplanar landmarks, zero extent).
		bool solve(const std::vector<glm::dvec3>& src,
			const std::vector<glm::dvec3>& tgt,
			double stiffness, const glm::dvec3& aspect);

		// BigWarp linear transform models. Each fits A,b so that A*src_i+b ~= tgt_i
		// (moving -> fixed) in a least-squares sense, then stores the inverse as
		// G with num_landmarks()==0. Return false on degenerate/insufficient input.
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
		// Affine absorbs axis anisotropy in its own degrees of freedom, so it
		// needs no aspect and is fit directly in grid-normalized coords.
		bool solveAffine(const std::vector<glm::dvec3>& src,
			const std::vector<glm::dvec3>& tgt);

		bool valid() const { return valid_; }
		int num_landmarks() const { return static_cast<int>(knots_.size()); }

		// Resampling map G: output (fixed) normalized coords f -> source
		// (moving) normalized coords.
		glm::dvec3 evaluate(const glm::dvec3& f) const;

		// Parameters of G (see the formula at the top of this file).
		const glm::dmat3& affine() const { return A_; }
		const glm::dvec3& translation() const { return b_; }
		const glm::dvec3& aspect() const { return aspect_; }
		const glm::dvec3& center() const { return c_; }
		double radius() const { return R_; }
		const std::vector<glm::dvec3>& knots() const { return knots_; }
		const std::vector<glm::dvec3>& weights() const { return W_; }

	private:
		// U(r) expressed via squared distance: r^2*log(r) = 0.5*r2*log(r2).
		static double kernel(double r2);
		// Solve a dense n x n system with rhs columns (row-major, in place).
		// On success B holds the solution; returns false if singular.
		static bool solveDense(std::vector<double>& A, int n,
			std::vector<double>& B, int rhs);

		// Reset to an invalid identity map before a fit.
		void reset();
		// Finish a pure-linear fit: A_, b_ hold the forward (moving -> fixed)
		// fit; replace them by its inverse, clear the radial-basis terms
		// (N==0), set valid_. Returns false if A_ is singular.
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

		std::vector<glm::dvec3> knots_; // fixed landmarks in u space (k_i)
		std::vector<glm::dvec3> W_;     // per-landmark weight vectors
		glm::dmat3 A_;                  // affine linear part (applied as A_*u)
		glm::dvec3 b_;                  // translation
		glm::dvec3 aspect_;             // u = (f*aspect_ - c_) / R_
		glm::dvec3 c_;
		double R_;
		bool valid_;
	};

} // end namespace FLIVR

#endif // ThinPlateSpline_h
