#pragma once

// Module "cofactor": a single cofactor C_ij.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// element cofactor for square A (0 based indices)
//
// For A of size 1x1, C_{1,1} is defined as 1.
//
// when opts.enable==true, explanation provides:
//   A -> submatrix -> row ops -> final value
Error op_cofactor_element(In MatrixView a,
        In std::uint8_t i,
        In std::uint8_t j,
        InOut Arena& scratch,
        Out Rational* out,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

} // namespace matrix_core
