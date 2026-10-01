#pragma once

// Module "solve": complete solution of Ax = b in parametric vector form.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// Complete ("general" or parametric) solution of A x = b.
//
// Row reduces the augmented matrix [A | b] and splits the solution set into
//   x = x_p + t_1 v_1 + ... + t_k v_k
// where x_p is the particular solution obtained by setting every free variable
// to zero and v_1 .. v_k is the basis of Null(A) produced by
// space_null_basis(): a particular solution of the non homogeneous system plus
// the general solution of the homogeneous one.

// Labels for the prose fragments the parametric solve explanation emits. The
// caller owns the strings and they must outlive the Explanation; a null field
// falls back to the English default. Core has no text catalog, so the shell
// passes translated strings here.
struct ParametricLabels {
		const char* augmented = nullptr;    // "augmented matrix [A | b]"
		const char* rref = nullptr;         // "reduced row echelon form"
		const char* solve_pivots = nullptr; // "solve each row for its pivot variable"
		const char* free_vars = nullptr;    // "free"
		const char* homogeneous = nullptr;  // "general solution of Ax = 0"
		const char* complete = nullptr;     // "complete solution"
		const char* inconsistent = nullptr; // "no solution"
		const char* unique = nullptr;       // "unique solution"
};

struct ParametricSolution {
		bool consistent = false;
		std::uint8_t var_count = 0; // n, the number of unknowns (a.cols)
		std::uint8_t rank = 0;
		std::uint8_t nullity = 0;            // k, the number of free parameters
		std::uint32_t pivot_mask = 0;        // bit c is set when column c is a pivot column
		std::uint8_t pivot_cols[kMaxCols]{}; // 0 based, valid in [0, rank), ascending
		std::uint8_t pivot_rows[kMaxCols]{}; // row holding pivot_cols[i], valid in [0, rank)
		std::uint8_t free_cols[kMaxCols]{};  // 0 based, valid in [0, nullity), ascending
		std::uint8_t inconsistent_row = 0;   // row of the 0 = c contradiction, valid when !consistent
};

// a is m x n and b is m x 1.
//
// The copy of [A | b] that the steps replay, x_p and the null space basis are
// allocated from `persist` and must outlive the Explanation; `scratch` holds
// working storage only.
//
// out_particular is n x 1 and is all zero when the system is inconsistent.
// out_null_basis is n x k, or the n x 1 zero vector when k == 0 (matching
// space_null_basis, which has no empty basis representation) so callers must
// read info->nullity to tell a single basis vector from "no parameters".
//
// An inconsistent system is not an error: it is reported through
// info->consistent so the steps stay available. `info` is written in place
// as the solve proceeds, so it is only meaningful when the call succeeds.
//
// When opts.enable is true, opts.persist must be &persist: the explanation
// context holds views into the augmented copy allocated there.
Error op_solve_parametric(In MatrixView a,
        In MatrixView b,
        InOut Arena& persist,
        InOut Arena& scratch,
        Out MatrixMutView* out_particular,
        Out MatrixMutView* out_null_basis,
        Out ParametricSolution* info,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const ParametricLabels& labels = ParametricLabels{}) noexcept;

} // namespace matrix_core
