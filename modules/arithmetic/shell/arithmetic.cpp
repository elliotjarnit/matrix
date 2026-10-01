#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops.hpp"
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
} // namespace

void App::run_arithmetic(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot) noexcept {
		if (op == OperationId::Add)
				run_binary_matrix_op(op, a_slot, b_slot, false, &matrix_core::op_add);
		else if (op == OperationId::Sub)
				run_binary_matrix_op(op, a_slot, b_slot, false, &matrix_core::op_sub);
		else
				run_binary_matrix_op(op, a_slot, b_slot, true, &matrix_core::op_mul);
}

} // namespace matrix_shell
