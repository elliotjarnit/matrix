#pragma once

// Module "indep_det": are the columns of a square A linearly independent, decided by det(A).

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// Labels for the prose the explanation emits. The caller owns the strings and
// they must outlive the Explanation; a null field falls back to English.
struct IndepDetLabels {
		const char* vectors = nullptr;     // "Columns are the vectors"
		const char* independent = nullptr; // "A is invertible, so the columns are linearly independent"
		const char* dependent = nullptr;   // "A is not invertible, so the columns are linearly dependent"
};

// The Invertible Matrix Theorem: the n columns of an n x n matrix A are
// linearly independent exactly when det(A) != 0.
//
// det(A) is computed by row reduction (op_det); the explanation is that
// reduction, ending on (-1)^r times the pivots, followed by the verdict.
// `independent` is det != 0.
Error op_indep_det(In MatrixView a,
        InOut Arena& scratch,
        Out Rational* det,
        Out bool* independent,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const IndepDetLabels& labels = IndepDetLabels{}) noexcept;

} // namespace matrix_core
