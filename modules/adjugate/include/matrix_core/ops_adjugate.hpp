#pragma once

// Module "adjugate": adj(A), the transpose of the matrix of cofactors.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// Labels for the prose the explanation emits. The caller owns the strings and
// they must outlive the Explanation; a null field falls back to English.
struct AdjugateLabels {
		const char* intro = nullptr;       // "adj(A) is the transpose of the matrix of cofactors"
		const char* delete_row = nullptr;  // "Delete row"
		const char* col_word = nullptr;    // "column"
		const char* cofactors = nullptr;   // "Matrix of cofactors"
		const char* transpose = nullptr;   // "Transpose it"
		const char* det = nullptr;         // "det(A) across row 1, and the inverse"
		const char* inverse = nullptr;     // "Divide every entry of adj(A) by det(A)"
		const char* det_inverse = nullptr; // "det of the inverse is 1 over det(A)"
};

// adj(A) for square A, written to `out` (n x n):
//
//   adj(A)_ij = C_ji,   C_ij = (-1)^(i+j) det(M_ij)
//
// so that A adj(A) = det(A) I, and A^-1 = adj(A) / det(A) when det(A) != 0.
// A 1x1 matrix has adj(A) = [1].
//
// When opts.enable is true the explanation shows A, then every cofactor C_ij
// in row order (the minor, its determinant worked out, the sign), the matrix
// of cofactors, its transpose, det(A) across row 1 with the inverse it gives,
// and when det(A) != 0 the inverse worked out and det(A^-1) = 1/det(A).
// `out` must outlive the Explanation. `det`, when given, receives det(A).
Error op_adjugate(In MatrixView a,
        InOut Arena& scratch,
        Out MatrixMutView out,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const AdjugateLabels& labels = AdjugateLabels{},
        Out Rational* det = nullptr) noexcept;

} // namespace matrix_core
