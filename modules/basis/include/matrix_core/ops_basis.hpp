#pragma once

// Module "basis": Col(A) and Nul(A) with bases, dimensions and the rank theorem.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// Labels for the column space / null space explanation. The caller owns the
// strings and they must outlive the Explanation; a null field falls back to
// English.
struct BasisLabels {
		const char* col_def = nullptr;      // "Col(A) is the span of the columns of A"
		const char* nul_def = nullptr;      // "Nul(A) is every solution of Ax = 0; row reduce A"
		const char* rref = nullptr;         // "RREF"
		const char* pivots = nullptr;       // "Pivot columns"
		const char* col_basis = nullptr;    // "The pivot columns of A form a basis for Col(A)"
		const char* nul_solve = nullptr;    // "Solve Ax = 0 for the pivot variables"
		const char* nul_vector = nullptr;   // "Write x as a combination of the free variables"
		const char* nul_basis = nullptr;    // "Those vectors form a basis for Nul(A)"
		const char* rank_theorem = nullptr; // "Rank theorem"
		const char* free_word = nullptr;    // "free"
};

// Everything the column space / null space exercises ask for, read off RREF(A):
//
//   Col(A) = Span{a_1 .. a_n}, a subspace of R^m, with the pivot columns of A
//            as a basis and dim Col(A) = rank(A)
//   Nul(A) = {x : Ax = 0}, a subspace of R^n, with one basis vector per free
//            variable and dim Nul(A) = n - rank(A)
//
// and the rank theorem rank(A) + dim Nul(A) = n that ties the two together.
struct BasisInfo {
		std::uint8_t rows = 0;               // m: Col(A) lives in R^m
		std::uint8_t cols = 0;               // n: Nul(A) lives in R^n
		std::uint8_t rank = 0;               // dim Col(A)
		std::uint8_t nullity = 0;            // dim Nul(A)
		std::uint32_t pivot_mask = 0;        // bit c set when column c is a pivot column
		std::uint8_t pivot_cols[kMaxCols]{}; // 0 based, valid in [0, rank), ascending
		std::uint8_t free_cols[kMaxCols]{};  // 0 based, valid in [0, nullity), ascending
};

// out_col_basis is m x rank and out_null_basis is n x nullity, both with the
// basis vectors as columns and both allocated from `persist`. A zero subspace
// has an empty basis, which is returned as a single zero column (as
// space_col_basis / space_null_basis do): read info->rank / info->nullity to
// tell the two apart. `scratch` holds the RREF while the op runs.
//
// When opts.enable is true, opts.persist must be &persist and A must outlive
// the Explanation: the steps replay the reduction from it.
Error op_basis(In MatrixView a,
        InOut Arena& persist,
        InOut Arena& scratch,
        Out MatrixMutView* out_col_basis,
        Out MatrixMutView* out_null_basis,
        Out BasisInfo* info,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const BasisLabels& labels = BasisLabels{}) noexcept;

} // namespace matrix_core
