#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_indep_det.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>

#include <utility>

namespace matrix_shell {
namespace {
using detail::fail_fast;
using namespace matrix_shell::text_literals;

matrix_core::IndepDetLabels indep_det_labels() noexcept {
		matrix_core::IndepDetLabels labels{};
		labels.vectors = "indep_det.vectors"_tx;
		labels.independent = "indep_det.independent"_tx;
		labels.dependent = "indep_det.dependent"_tx;
		return labels;
}
} // namespace

// the columns of A are the vectors; they are independent exactly when det(A) != 0
void App::run_indep_det(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		if (a.rows != a.cols) {
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append("msg.indep_det_square_prefix"_tx);
				w.put(static_cast<char>('A' + slot));
				w.append("=");
				w.append_u64(a.rows);
				w.put('x');
				w.append_u64(a.cols);
				show_message(fmt_buf_);
				return;
		}

		matrix_core::ArenaScratchScope scratch_tx(scratch_);
		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::Rational v = matrix_core::Rational::from_int(0);
		bool independent = false;
		const matrix_core::Error err = matrix_core::op_indep_det(a, scratch_, &v, &independent, &expl, opts_steps, indep_det_labels());
		SHELL_DBG("[op] indep_det err=%u independent=%u\n", (unsigned)err.code, (unsigned)independent);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::NotSquare || err.code == matrix_core::ErrorCode::Internal)
						fail_fast("run_indep_det: unexpected error");
				show_message("common.error"_tx);
				return;
		}

		// op_indep_det commits its own persist allocations
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (indep det)");

		// `num`/`den` carry the determinant, `i` the verdict
		Page p = Page::make_result_scalar(OperationId::IndepDet, slot, 0, expl_.available(), v.num(), v.den());
		p.u.result.i = independent ? 1u : 0u;
		REQUIRE(push(p), "push indep det result failed");
}

void App::render_result_indep_det(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		const int y0 = l.header_h + 26;
		gfx_SetTextXY(l.margin_x, y0);
		gfx_PrintString("result.indep_label"_tx);
		gfx_PrintString((s.i != 0) ? "result.bool_yes"_tx : "result.bool_no"_tx);

		gfx_SetTextXY(l.margin_x, y0 + 14);
		gfx_PrintString("result.indep_det_eq"_tx);
		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append_i64(s.num);
				if (s.den != 1) {
						w.put('/');
						w.append_i64(s.den);
				}
		}
		gfx_PrintString(fmt_buf_);

		gfx_SetTextXY(l.margin_x, y0 + 28);
		gfx_PrintString((s.i != 0) ? "result.indep_det_nonzero"_tx : "result.indep_det_zero"_tx);
}

} // namespace matrix_shell
