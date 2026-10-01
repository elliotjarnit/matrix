#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_inverse.hpp"
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

void App::run_inverse(OperationId op, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		if (a.rows != a.cols) {
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append("msg.inverse_requires_square_prefix"_tx);
				w.put(static_cast<char>('A' + slot));
				w.append("=");
				w.append_u64(a.rows);
				w.put('x');
				w.append_u64(a.cols);
				show_message(fmt_buf_);
				return;
		}

		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		const std::uint8_t n = a.rows;
		matrix_core::ArenaScope tx(persist_);
		matrix_core::MatrixMutView outm{};
		const matrix_core::ErrorCode ec = matrix_core::matrix_alloc(persist_, n, n, &outm);
		if (!matrix_core::is_ok(ec)) {
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		const matrix_core::Error err = matrix_core::op_inverse(a, scratch_, outm, &expl, opts_steps);
		SHELL_DBG("[op] inverse err=%u a=%ux%u\n", (unsigned)err.code, (unsigned)err.a.rows, (unsigned)err.a.cols);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::Singular) {
						show_message("msg.singular_no_inverse"_tx);
				} else if (err.code == matrix_core::ErrorCode::NotSquare || err.code == matrix_core::ErrorCode::DimensionMismatch ||
				           err.code == matrix_core::ErrorCode::Internal) {
						fail_fast("update_slot_pick: inverse returned unexpected error");
				} else {
						show_message("common.error"_tx);
				}
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		REQUIRE(pop(), "pop failed (inverse)");
		REQUIRE(push(Page::make_result_matrix(op, slot, 0, expl_.available(), outm.rows, outm.cols, outm.stride, outm.data)),
		        "push inverse result failed");
}

} // namespace matrix_shell
