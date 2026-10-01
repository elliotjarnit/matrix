#pragma once

// Module "cramer": Cramer's rule.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// determinant of A with column "col" replaced by vector "b" (n x 1), useful for
// cramer's rule step breakdown (Δ_i)
Error op_det_replace_column(In MatrixView a,
        In MatrixView b,
        In std::uint8_t col,
        InOut Arena& scratch,
        Out Rational* out,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

// cramers rule solve (Ax=b), returning x as an n x 1 matrix. no step breakdown
// is produced here, shell can request Δ and Δ_i explanations via op_det()
// and op_det_replace_column()
Error op_cramer_solve(In MatrixView a, In MatrixView b, InOut Arena& scratch, Out MatrixMutView x_out) noexcept;

} // namespace matrix_core
