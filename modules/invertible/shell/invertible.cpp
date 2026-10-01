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

// Q1 on the review sheet: is this matrix invertible, and if not, why not.
void App::run_invertible(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
		if (slot >= kSlotCount || !slots_[slot].is_set())
				fail_fast("slot not set");

		const matrix_core::MatrixView a = slots_[slot].view_active();
		// a non square matrix has no inverse at all: that is the answer
		// the exercise asks for, not an error
		if (a.rows != a.cols) {
				REQUIRE(pop(), "pop failed (invertible)");
				expl_ = {};
				Page p = Page::make_result_scalar(OperationId::Invertible, slot, 0, false, 0, 1);
				p.u.result.i = 2; // not square
				REQUIRE(push(p), "push invertible result failed");
				return;
		}

		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::Rational v = matrix_core::Rational::from_int(0);
		const matrix_core::Error err = matrix_core::op_det(a, scratch_, &v, &expl, opts_steps);
		if (!matrix_core::is_ok(err)) {
				show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (invertible)");

		// `num`/`den` carry the determinant, `i` says why when the answer is no
		Page p = Page::make_result_scalar(OperationId::Invertible, slot, 0, expl_.available(), v.num(), v.den());
		p.u.result.i = v.is_zero() ? 1u : 0u;
		REQUIRE(push(p), "push invertible result failed");
}

void App::render_result_invertible(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		const int y0 = l.header_h + 26;
		gfx_SetTextXY(l.margin_x, y0);
		gfx_PrintString("result.invertible_label"_tx);
		// `i`: 0 invertible, 1 singular, 2 not square
		gfx_PrintString((s.i == 0) ? "result.bool_yes"_tx : "result.bool_no"_tx);

		if (s.i != 2) {
				gfx_SetTextXY(l.margin_x, y0 + 14);
				gfx_PrintString("result.det_eq"_tx);
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append_i64(s.num);
				if (s.den != 1) {
						w.put('/');
						w.append_i64(s.den);
				}
				gfx_PrintString(fmt_buf_);
		}

		// the exercise asks why when the answer is no
		if (s.i != 0) {
				gfx_SetTextXY(l.margin_x, y0 + 28);
				gfx_PrintString((s.i == 2) ? "result.reason_not_square"_tx : "result.reason_singular"_tx);
		}
}

} // namespace matrix_shell
