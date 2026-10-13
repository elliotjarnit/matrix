#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_triangular.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>

#include <utility>

namespace matrix_shell {
namespace {
using detail::fail_fast;
using namespace matrix_shell::text_literals;

matrix_core::TriangularLabels triangular_labels() noexcept {
		matrix_core::TriangularLabels labels{};
		labels.upper = "triangular.upper"_tx;
		labels.lower = "triangular.lower"_tx;
		labels.diagonal = "triangular.diagonal"_tx;
		labels.product = "triangular.product"_tx;
		return labels;
}
} // namespace

void App::run_det_triangular(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();

		// "Requires square. A=2x3" / "Not triangular: A=3x3"
		const bool square = a.rows == a.cols;
		if (!square || matrix_core::triangular_kind(a) == matrix_core::TriangularKind::None) {
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append(square ? "msg.not_triangular_prefix"_tx : "msg.requires_square_prefix"_tx);
				w.put(static_cast<char>('A' + slot));
				w.append("=");
				w.append_u64(a.rows);
				w.put('x');
				w.append_u64(a.cols);
				show_message(fmt_buf_);
				return;
		}

		matrix_core::ArenaScope tx(persist_);
		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::Rational v = matrix_core::Rational::from_int(0);
		const matrix_core::Error err = matrix_core::op_det_triangular(a, &v, &expl, opts_steps, triangular_labels());
		SHELL_DBG("[op] det_triangular err=%u\n", (unsigned)err.code);
		if (!matrix_core::is_ok(err)) {
				if (err.code != matrix_core::ErrorCode::Overflow)
						fail_fast("run_det_triangular: unexpected error");
				show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (det triangular)");
		REQUIRE(push(Page::make_result_scalar(OperationId::DetTriangular, slot, 0, expl_.available(), v.num(), v.den())),
		        "push det triangular result failed");
}

void App::render_result_det_triangular(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		const int y0 = l.header_h + 30;
		gfx_SetTextXY(l.margin_x, y0);
		gfx_PrintString("det(");
		gfx_PrintChar(static_cast<char>('A' + s.slot_a));
		gfx_PrintString(") = ");
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
		gfx_SetTextXY(l.margin_x, y0 + 14);
		gfx_PrintString("result.diag_product"_tx);
}

} // namespace matrix_shell
