#pragma once

#include <cstddef>
#include <cstdint>

#include "matrix_core/arena.hpp"
#include "matrix_core/error.hpp"
#include "matrix_core/matrix.hpp"
#include "matrix_core/row_ops.hpp"
#include "matrix_core/writer.hpp"

namespace matrix_core {

// Declared in ops.hpp.
enum class EchelonKind : std::uint8_t;

namespace detail {

// English fallback for an optional caller supplied (translated) label
constexpr const char* lbl(const char* value, const char* fallback) noexcept {
		return value ? value : fallback;
}

// A Writer positioned at the end of the text already in `buf`, so a caption can
// be extended without rebuilding it.
Writer writer_at_end(Out char* buf, In std::size_t cap) noexcept;

// Replays the first `index` row operations of the reduction of `input` into
// `scratch`. `index == 0` yields a plain copy and leaves `last_op` untouched.
//
// Returns StepOutOfRange when the reduction has fewer than `index` operations.
ErrorCode replay_echelon(In MatrixView input,
        In EchelonKind kind,
        In std::size_t index,
        InOut Arena& scratch,
        Out MatrixMutView* out,
        Out RowOp* last_op) noexcept;

// Writes the caption for one row operation, appending `rref_label` when this is
// the last operation of the reduction. Captions are best effort: a caption that
// does not fit is truncated rather than failing the step.
void step_row_caption(In const RowOp& op, In bool is_last, In const char* rref_label, Out char* buf, In std::size_t cap) noexcept;

} // namespace detail
} // namespace matrix_core
