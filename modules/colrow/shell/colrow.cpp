#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_colrow.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>
#include <keypadc.h>

#include <utility>

namespace matrix_shell {
namespace {
using detail::dbg_print_matrix;
using detail::dbg_print_rational;
using detail::fail_fast;
using detail::keep_cursor_in_view;
using detail::kKbGroup1;
using detail::kKbGroup6;
using detail::kKbGroupArrows;
using detail::kScreenH;
using detail::kScreenW;
using detail::op_name;
using namespace matrix_shell::text_literals;

matrix_core::ColRowLabels colrow_labels() noexcept {
		matrix_core::ColRowLabels labels{};
		labels.intro = "colrow.intro"_tx;
		labels.term = "colrow.term"_tx;
		labels.sum = "colrow.sum"_tx;
		return labels;
}

// op_mul with the column row expansion as its explanation
matrix_core::Error mul_colrow(matrix_core::MatrixView a,
        matrix_core::MatrixView b,
        matrix_core::MatrixMutView out,
        matrix_core::Explanation* expl,
        const matrix_core::ExplainOptions& opts) noexcept {
		return matrix_core::op_mul_colrow(a, b, out, expl, opts, colrow_labels());
}
} // namespace

void App::run_mul_colrow(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot) noexcept {
		run_binary_matrix_op(op, a_slot, b_slot, true, &mul_colrow);
}

} // namespace matrix_shell
