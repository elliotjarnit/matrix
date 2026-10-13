#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_adjugate.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>

#include <utility>

namespace matrix_shell {
namespace {
using detail::fail_fast;
using namespace matrix_shell::text_literals;

matrix_core::AdjugateLabels adjugate_labels() noexcept {
		matrix_core::AdjugateLabels labels{};
		labels.intro = "adjugate.intro"_tx;
		labels.delete_row = "adjugate.delete_row"_tx;
		labels.col_word = "adjugate.col_word"_tx;
		labels.cofactors = "adjugate.cofactors"_tx;
		labels.transpose = "adjugate.transpose"_tx;
		labels.det = "adjugate.det"_tx;
		labels.inverse = "adjugate.inverse"_tx;
		labels.det_inverse = "adjugate.det_inverse"_tx;
		return labels;
}
} // namespace

void App::run_adjugate(OperationId op, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		if (a.rows != a.cols) {
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append("msg.requires_square_prefix"_tx);
				w.put(static_cast<char>('A' + slot));
				w.append("=");
				w.append_u64(a.rows);
				w.put('x');
				w.append_u64(a.cols);
				show_message(fmt_buf_);
				return;
		}

		// adj(A) lives in the persist tail for the result page and the steps
		matrix_core::ArenaScope tx(persist_);
		matrix_core::MatrixMutView out{};
		if (!matrix_core::is_ok(matrix_core::matrix_alloc(persist_, a.rows, a.cols, &out))) {
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::Rational det = matrix_core::Rational::from_int(0);
		const matrix_core::Error err = matrix_core::op_adjugate(a, scratch_, out, &expl, opts_steps, adjugate_labels(), &det);
		SHELL_DBG("[op] adjugate err=%u a=%ux%u\n", (unsigned)err.code, (unsigned)err.a.rows, (unsigned)err.a.cols);
		if (!matrix_core::is_ok(err)) {
				if (err.code != matrix_core::ErrorCode::Overflow)
						fail_fast("run_adjugate: unexpected error");
				show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (adjugate)");
		// `num`/`den` carry det(A) for the line above the grid
		Page p = Page::make_result_matrix(op, slot, 0, expl_.available(), out.rows, out.cols, out.stride, out.data);
		p.u.result.num = det.num();
		p.u.result.den = det.den();
		REQUIRE(push(p), "push adjugate result failed");
}

// "det(A) = d  det(A^-1) = 1/d", then adj(A)
void App::render_result_adjugate(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		const char name = static_cast<char>('A' + s.slot_a);
		gfx_SetTextXY(l.margin_x, l.header_h + 4);
		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append("det(");
				w.put(name);
				w.append(")=");
				w.append_i64(s.num);
				if (s.den != 1) {
						w.put('/');
						w.append_i64(s.den);
				}
				if (s.num != 0) {
						// det(A^-1) = den/num, sign on the numerator
						const bool neg = s.num < 0;
						w.append("  det(");
						w.put(name);
						w.append("^-1)=");
						if (neg)
								w.put('-');
						w.append_i64(s.den);
						const std::int64_t mag = neg ? -s.num : s.num;
						if (mag != 1) {
								w.put('/');
								w.append_i64(mag);
						}
				}
		}
		gfx_PrintString(fmt_buf_);
		if (s.num == 0) {
				gfx_SetTextXY(l.margin_x, l.header_h + 16);
				gfx_PrintString("result.adjugate_no_inverse"_tx);
		}
		render_result_grid(s, l.header_h + 30);
}

} // namespace matrix_shell
