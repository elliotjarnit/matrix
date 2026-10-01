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

void App::run_det(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::Rational v = matrix_core::Rational::from_int(0);
		const matrix_core::Error err = matrix_core::op_det(a, scratch_, &v, &expl, opts_steps);
		SHELL_DBG("[op] det err=%u a=%ux%u value=", (unsigned)err.code, (unsigned)err.a.rows, (unsigned)err.a.cols);
		dbg_print_rational(v);
		SHELL_DBG("\n");
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::NotSquare) {
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						w.append("msg.det_requires_square_prefix"_tx);
						w.put(static_cast<char>('A' + slot));
						w.append("=");
						w.append_u64(a.rows);
						w.put('x');
						w.append_u64(a.cols);
						show_message(fmt_buf_);
				} else
						show_message("common.error"_tx);
				return;
		}

		tx.commit();
		REQUIRE(pop(), "pop failed (det)");
		expl_ = std::move(expl);
		REQUIRE(push(Page::make_result_scalar(OperationId::Det, slot, 0, expl_.available(), v.num(), v.den())), "push det result failed");
}

void App::render_result_det(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		gfx_SetTextXY(l.margin_x, l.header_h + 30);
		gfx_PrintString("det(");
		gfx_PrintChar(static_cast<char>('A' + s.slot_a));
		gfx_PrintString(") = ");
		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
		if (s.den == 1) {
				w.append_i64(s.num);
		} else {
				w.append_i64(s.num);
				w.put('/');
				w.append_i64(s.den);
		}
		gfx_PrintString(fmt_buf_);
}

} // namespace matrix_shell
