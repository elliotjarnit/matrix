#pragma once

// Module "vectors": dot product, cross product and projection.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

/// Vector operations

// dot product of two vectors. vectors may be represented as n×1 or 1×n
Error op_dot(In MatrixView u, In MatrixView v, Out Rational* out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

// cross product of two 3D vectors. inputs may be 3×1 or 1×3
// output must have the same dimensions as u (same orientation)
Error op_cross(In MatrixView u, In MatrixView v, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

struct ProjDecomposeResult {
		Rational dot_uv = Rational::from_int(0);
		Rational dot_vv = Rational::from_int(0);
		Rational k = Rational::from_int(0);          // k = (u·v)/(v·v)
		Rational proj_norm2 = Rational::from_int(0); // ||proj||^2
		Rational orth_norm2 = Rational::from_int(0); // ||orth||^2
};

// decompose u into a component along v and an orthogonal remainder
//   proj = proj_v(u) = (u·v)/(v·v) * v
//   orth = u - proj
//
// u and v may be n×1 or 1×n (same length n in [1 to 6])
// out_proj and out_orth must have the same dimensions as u
// returns DivisionByZero if v is the zero vector (v·v == 0)
Error op_proj_decompose_u_onto_v(In MatrixView u,
        In MatrixView v,
        Out MatrixMutView out_proj,
        Out MatrixMutView out_orth,
        Out ProjDecomposeResult* out,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

} // namespace matrix_core
