#pragma once

#include <cstddef>
#include <cstdint>

#include "matrix_core/error.hpp"
#include "matrix_core/matrix.hpp"
#include "matrix_core/row_ops.hpp"

namespace matrix_core::detail {
// determinant via row ops (triangularize + multiply diagonal), with op-count
// tracking
//
// if stop_after != size_t(-1), applies row ops until exactly stop_after ops
// have been performed (1 based), then returns with det_out set to 0 (det not
// meaningful)
//
// `swap_count`, when given, receives the number of row swaps performed: the r
// in det(A) = (-1)^r * (product of the pivots of U)
ErrorCode det_elim(InOut MatrixMutView m,
        Out Rational* det_out,
        Out std::size_t* op_count,
        In std::size_t stop_after,
        Out RowOp* last_op,
        Out std::size_t* swap_count = nullptr) noexcept;
} // namespace matrix_core::detail
