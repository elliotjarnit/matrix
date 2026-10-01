#pragma once

// Module "colrow": A*B as the column row (outer product) expansion.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// Labels for the column row expansion explanation. The caller owns the strings
// and they must outlive the Explanation; a null field falls back to English.
struct ColRowLabels {
		const char* intro = nullptr; // "AB is the sum of the outer products"
		const char* term = nullptr;  // "term"
		const char* sum = nullptr;   // "sum of the outer products"
};

// A*B computed the way the review exercises call the "column row expansion":
//
//   AB = sum over k of  col_k(A) * row_k(B)
//
// Each term is the outer product of a column of A with the matching row of B,
// an m x n matrix of rank at most one. a is m x p, b is p x n, out is m x n.
//
// The value written to `out` is the ordinary product; only the explanation
// differs from op_mul().
Error op_mul_colrow(In MatrixView a,
        In MatrixView b,
        Out MatrixMutView out,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const ColRowLabels& labels = ColRowLabels{}) noexcept;

} // namespace matrix_core
