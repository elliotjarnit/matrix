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

void App::run_transpose(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::MatrixMutView out{};
		const matrix_core::ErrorCode ec = matrix_core::matrix_alloc(persist_, a.cols, a.rows, &out);
		if (!matrix_core::is_ok(ec)) {
				SHELL_DBG("[op] transpose alloc failed ec=%u used=%u/%u\n",
				        (unsigned)ec,
				        (unsigned)persist_.used(),
				        (unsigned)persist_.capacity());
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		const matrix_core::Error err = matrix_core::op_transpose(a, out, &expl, opts_steps);
		SHELL_DBG("[op] transpose err=%u a=%ux%u\n", (unsigned)err.code, (unsigned)err.a.rows, (unsigned)err.a.cols);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::Overflow) {
						show_message("common.out_of_memory"_tx);
						return;
				}
				fail_fast("update_slot_pick: transpose returned unexpected error");
		}

		tx.commit();
		expl_ = std::move(expl);
		REQUIRE(pop(), "pop failed (transpose)");
		REQUIRE(push(Page::make_result_matrix(
		                OperationId::Transpose, slot, 0, expl_.available(), out.rows, out.cols, out.stride, out.data)),
		        "push transpose result failed");
}

} // namespace matrix_shell
