#pragma once

// Module "lu": LU (and PA = LU) factorization and LU solve.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

/// LU factorization (Doolittle)

// Labels for the prose fragments the LU explanations emit. The caller owns the
// strings and they must outlive the Explanation; a null field falls back to the
// English default. Core has no text catalog, so the shell passes translated
// strings here.
struct LuLabels {
		const char* skip_column = nullptr;  // "no pivot, skip column"
		const char* check = nullptr;        // "check"
		const char* forward = nullptr;      // "forward substitution"
		const char* back = nullptr;         // "back substitution"
		const char* inconsistent = nullptr; // "inconsistent: no solution"
		const char* particular = nullptr;   // "particular solution"
		const char* homogeneous = nullptr;  // "homogeneous solution"
		const char* summary = nullptr;      // "summary"
		const char* swap_rows = nullptr;    // "swap rows"
};

enum class LuPivoting : std::uint8_t {
		None = 0, // stop when a row swap would be needed
		Partial,  // swap rows when a pivot is zero, giving PA = LU
};

// A = LU (or PA = LU) using row replacement only, the way it is done by hand:
//   R_i <- R_i - m R_r   records   L(i, r) = m
//
// A need not be square. l_out is m x m unit lower triangular, u_out is the
// m x n echelon form. A pivot position that is zero with an all zero column
// below it is skipped and the pivot row does not advance.
//
// With LuPivoting::None a zero pivot that has a nonzero entry below it returns
// ErrorCode::RowSwapRequired (Error::j holds the 0 based column, Error::i the
// pivot row): A has no LU factorization without a permutation.
//
// With LuPivoting::Partial those rows are swapped instead, the multipliers
// already recorded in L move with them, and p_out receives the permutation so
// that PA = LU. p_out is optional (pass a default constructed view to skip it)
// and is ignored when pivoting is None.
Error op_lu_factor(In MatrixView a,
        In LuPivoting pivoting,
        Out MatrixMutView p_out,
        Out MatrixMutView l_out,
        Out MatrixMutView u_out,
        In const LuLabels* labels,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

struct LuSolveInfo {
		// true when U is square with a nonzero diagonal, i.e. Ux = y was solved by
		// back substitution. false means [U | y] was row reduced instead.
		bool unique = false;
		bool consistent = true;
		bool permuted = false;    // at least one row swap was applied
		std::uint8_t bad_row = 0; // 0 based row of RREF([U | y]) witnessing inconsistency
		std::uint8_t rank = 0;
		std::uint8_t nullity = 0;
};

// Solve Ax = b through the LU factorization above: Ly = Pb by forward
// substitution, then Ux = y by back substitution (square U with nonzero
// diagonal) or by row reducing [U | y]. P is the identity unless pivoting is
// Partial and a swap was needed.
//
// y_out is m x 1, x_out is n x 1 (free variables set to zero, all zero when the
// system is inconsistent) and rref_out is m x (n+1), always filled with
// RREF([U | y]) so the caller can derive the null space basis.
//
// an inconsistent system is not an error: it is reported through info so the
// steps stay available.
Error op_lu_solve(In MatrixView a,
        In MatrixView b,
        In LuPivoting pivoting,
        InOut Arena& scratch,
        Out MatrixMutView y_out,
        Out MatrixMutView x_out,
        Out MatrixMutView rref_out,
        Out LuSolveInfo* info,
        In const LuLabels* labels,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

} // namespace matrix_core
