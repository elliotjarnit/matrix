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

void App::run_echelon(OperationId op, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::MatrixMutView outm{};
		const matrix_core::ErrorCode ec = matrix_core::matrix_alloc(persist_, a.rows, a.cols, &outm);
		if (!matrix_core::is_ok(ec)) {
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::Explanation expl;
		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		const matrix_core::EchelonKind kind = (op == OperationId::Ref) ? matrix_core::EchelonKind::Ref : matrix_core::EchelonKind::Rref;
		const matrix_core::Error err = matrix_core::op_echelon(a, kind, outm, &expl, opts_steps);
		SHELL_DBG("[op] %s err=%u a=%ux%u\n", op_name(op), (unsigned)err.code, (unsigned)err.a.rows, (unsigned)err.a.cols);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::DimensionMismatch || err.code == matrix_core::ErrorCode::Internal) {
						fail_fast("update_slot_pick: echelon returned unexpected error");
				}
				show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		REQUIRE(pop(), "pop failed (echelon)");
		REQUIRE(push(Page::make_result_matrix(op, slot, 0, expl_.available(), outm.rows, outm.cols, outm.stride, outm.data)),
		        "push echelon result failed");
}

} // namespace matrix_shell
