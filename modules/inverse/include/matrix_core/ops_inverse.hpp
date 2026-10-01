#pragma once

// Module "inverse": the inverse via Gauss-Jordan on [A | I].

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// inverse via gauss jordan elimination on the augmented matrix [A | I]
//
// on success, writes A^{-1} into out
// when opts.enable==true, explanation steps render the augmented matrix after
// each row operation
Error op_inverse(
        In MatrixView a, InOut Arena& scratch, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

} // namespace matrix_core
