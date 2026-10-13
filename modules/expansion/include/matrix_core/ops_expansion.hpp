#pragma once

// Module "expansion": det(A) by cofactor expansion across a row or down a column.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

enum class ExpansionLine : std::uint8_t {
		Row = 0, // across row i:   det(A) = a_i1 C_i1 + ... + a_in C_in
		Col,     // down column j: det(A) = a_1j C_1j + ... + a_nj C_nj
};

// Labels for the prose the expansion explanation emits. The caller owns the
// strings and they must outlive the Explanation; a null field falls back to
// English.
struct ExpansionLabels {
		const char* across_row = nullptr; // "Expand across row"
		const char* down_col = nullptr;   // "Expand down column"
		const char* delete_row = nullptr; // "Delete row"
		const char* col_word = nullptr;   // "column"
		const char* zero_term = nullptr;  // "so the term is 0"
		const char* sum = nullptr;        // "Add up the terms"
};

// det(A) for square A, expanded along row or column `index` (0 based):
//
//   C_ij = (-1)^(i+j) det(M_ij)
//
// where M_ij is A with row i and column j deleted. The value is exact; the
// minors are evaluated by row reduction.
//
// When opts.enable is true the explanation walks the worksheet method: the
// expansion formula, then one step per term (the minor M_ij written out, its
// determinant evaluated inline when it is 2x2 or 3x3, and the cofactor), then
// the sum. A zero entry's term is noted and skipped.
Error op_cofactor_expansion(In MatrixView a,
        In ExpansionLine line,
        In std::uint8_t index,
        InOut Arena& scratch,
        Out Rational* out,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const ExpansionLabels& labels = ExpansionLabels{}) noexcept;

} // namespace matrix_core
