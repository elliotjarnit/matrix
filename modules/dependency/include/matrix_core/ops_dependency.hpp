#pragma once

// Module "dependency": linear independence, span and onto / one-to-one, read off RREF(A).

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// Labels for the prose fragments the dependency explanation emits. The caller
// owns the strings and they must outlive the Explanation; a null field falls
// back to the English default.
struct DependencyLabels {
		const char* vectors = nullptr;        // "columns are the vectors"
		const char* rref = nullptr;           // "RREF"
		const char* independent = nullptr;    // "linearly independent"
		const char* dependent = nullptr;      // "linearly dependent"
		const char* vectors_word = nullptr;   // "vectors"
		const char* onto = nullptr;           // "onto"
		const char* not_onto = nullptr;       // "not onto"
		const char* one_to_one = nullptr;     // "one-to-one"
		const char* not_one_to_one = nullptr; // "not one-to-one"
		const char* rows_word = nullptr;      // "rows"
		const char* cols_word = nullptr;      // "columns"
		const char* rank_word = nullptr;      // "rank"
		const char* relation = nullptr;       // "each dependent vector over the pivot vectors"
};

// What the columns of A are being asked about. The reduction is identical; only
// the verdict differs.
enum class DependencyPurpose : std::uint8_t {
		Vectors = 0,    // a set of vectors: independence, and the dependency relations
		Transformation, // the standard matrix of T: onto (rank == rows) and one to one (rank == cols)
};

// Linear (in)dependence of the columns of A, with the dependency relations.
//
// The columns v_1 .. v_n are independent exactly when every column is a pivot
// column of RREF(A). Each non pivot column f is a combination of the pivot
// columns before it, read straight off the RREF:
//
//   v_f = sum over pivots i of  RREF(pivot_rows[i], f) * v_{pivot_cols[i]}
//
// which is what the review exercises mean by "show that one of them is a
// linear combination of the other two".
struct DependencyInfo {
		bool independent = false;
		bool spans = false;            // rank == a.rows: the columns span R^m
		std::uint8_t vector_count = 0; // n, the number of vectors (a.cols)
		std::uint8_t dim = 0;          // m, the dimension they live in (a.rows)
		std::uint8_t rank = 0;
		std::uint8_t dependent_count = 0;        // n - rank
		std::uint32_t pivot_mask = 0;            // bit c set when column c is a pivot column
		std::uint8_t pivot_cols[kMaxCols]{};     // 0 based, valid in [0, rank), ascending
		std::uint8_t pivot_rows[kMaxCols]{};     // row holding pivot_cols[i], valid in [0, rank)
		std::uint8_t dependent_cols[kMaxCols]{}; // 0 based, valid in [0, dependent_count)
};

// RREF(A) is written to `persist` and returned through out_rref so the caller
// can read the relation coefficients out of it; the reduction happens in place
// there, so this op needs no scratch arena of its own.
//
// When opts.enable is true, opts.persist must be &persist: the explanation
// context holds views into A and into that RREF.
//
// `info` is written in place as the reduction proceeds, so it is only
// meaningful when the call succeeds.
Error op_dependency(In MatrixView a,
        InOut Arena& persist,
        Out MatrixMutView* out_rref,
        Out DependencyInfo* info,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const DependencyLabels& labels = DependencyLabels{},
        In DependencyPurpose purpose = DependencyPurpose::Vectors) noexcept;

} // namespace matrix_core
