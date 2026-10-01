#pragma once

// Module "minor_matrix": the matrix of minors.

#include <cstdint>

#include "matrix_core/ops.hpp"

namespace matrix_core {

// matrix of minors for square A (n>=2). output is n×n:
//   M[i,j] = det(A with row i and column j removed)
Error op_minor_matrix(In MatrixView a, InOut Arena& scratch, Out MatrixMutView out) noexcept;

} // namespace matrix_core
