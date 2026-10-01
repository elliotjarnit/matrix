#pragma once

#include <cstddef>
#include <cstdint>

#include "matrix_core/arena.hpp"
#include "matrix_core/config.hpp"
#include "matrix_core/error.hpp"
#include "matrix_core/explanation.hpp"
#include "matrix_core/matrix.hpp"
#include "matrix_core/rational.hpp"
#include "matrix_core/row_ops.hpp"

namespace matrix_core {
enum class ExplainDetail : std::uint8_t {
		Full = 0,
		Compact, // fewer, higher level steps
};

struct ExplainOptions {
		bool enable = false;
		Arena* persist = nullptr; // required when enable==true
		ExplainDetail detail = ExplainDetail::Full;
};

// indices in these APIs are 0 based (consistent with m.at(r, c))
// the shell/ui can present 1 based indices and convert as needed
//
// memory model:
// - matrix data must outlive any Explanation created from it
// - when opts.enable==true, opts.persist must be a valid long lived arena for
// the
//   explanation context
// - step rendering requires StepRenderBuffers::scratch to be a valid arena, it
// is
//   cleared by the renderer on each call
//
Error op_add(In MatrixView a, In MatrixView b, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;
Error op_sub(In MatrixView a, In MatrixView b, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;
Error op_mul(In MatrixView a, In MatrixView b, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;
Error op_transpose(In MatrixView a, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

enum class EchelonKind : std::uint8_t {
		Ref,
		Rref,
};

Error op_echelon(
        In MatrixView a, In EchelonKind kind, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

Error op_det(In MatrixView a, InOut Arena& scratch, Out Rational* out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

} // namespace matrix_core
