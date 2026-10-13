#pragma once

// Module "triangular": det(A) of a triangular matrix, the product of its diagonal.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

enum class TriangularKind : std::uint8_t {
		None = 0, // not triangular (or not square)
		Upper,    // zeros below the diagonal
		Lower,    // zeros above the diagonal
		Diagonal, // both
};

TriangularKind triangular_kind(In MatrixView a) noexcept;

// Labels for the prose the explanation emits. The caller owns the strings and
// they must outlive the Explanation; a null field falls back to English.
struct TriangularLabels {
		const char* upper = nullptr;    // "A is upper triangular"
		const char* lower = nullptr;    // "A is lower triangular"
		const char* diagonal = nullptr; // "A is diagonal"
		const char* product = nullptr;  // "det(A) is the product of the diagonal entries"
};

// det(A) = a_11 a_22 ... a_nn for triangular A.
//
// Returns NotSquare for a non square A and InvalidDimension when A is square
// but not triangular (triangular_kind(a) == None), so callers check first.
Error op_det_triangular(In MatrixView a,
        Out Rational* out,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const TriangularLabels& labels = TriangularLabels{}) noexcept;

} // namespace matrix_core
