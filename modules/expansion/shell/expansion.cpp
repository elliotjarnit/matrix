#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_expansion.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>
#include <keypadc.h>

#include <utility>

namespace matrix_shell {
namespace {
using detail::dbg_print_rational;
using detail::fail_fast;
using detail::kKbGroup6;
using detail::kKbGroupArrows;
using detail::kScreenW;
using namespace matrix_shell::text_literals;

constexpr std::uint8_t kLineRow = 0;
constexpr std::uint8_t kLineCol = 1;

matrix_core::ExpansionLabels expansion_labels() noexcept {
		matrix_core::ExpansionLabels labels{};
		labels.across_row = "expansion.across_row"_tx;
		labels.down_col = "expansion.down_col"_tx;
		labels.delete_row = "expansion.delete_row"_tx;
		labels.col_word = "expansion.col_word"_tx;
		labels.zero_term = "expansion.zero_term"_tx;
		labels.sum = "expansion.sum"_tx;
		return labels;
}

Page make_expansion_pick(std::uint8_t slot, std::uint8_t n) noexcept {
		Page p{};
		p.kind = PageKind::ExpansionPick;
		p.u.expansion_pick = ExpansionPickState{slot, n, kLineRow, 0, 0};
		return p;
}
} // namespace

void App::run_cofactor_expansion(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
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

		// the slot picker stays under the page so CLEAR goes back to it
		REQUIRE(push(make_expansion_pick(slot, a.rows)), "push expansion pick page failed");
}

void App::render_expansion_pick(const ExpansionPickState& s) noexcept {
		const ui::Layout l = ui::Layout{};

		render_header("op.cofactor_expansion"_tx);
		render_footer_hint("footer.expansion_adjust"_tx);

		gfx_SetTextFGColor(ui::color::kBlack);
		gfx_SetTextXY(l.margin_x, l.header_h + 10);
		gfx_PrintString("expansion.matrix_prefix"_tx);
		gfx_PrintChar(static_cast<char>('A' + s.slot));
		gfx_PrintString(" (n=");
		gfx_PrintChar(static_cast<char>('0' + s.n));
		gfx_PrintString(")");

		const int y_base = l.header_h + 32;
		const int line_h = 16;

		auto draw_field = [&](int y, const char* name, const char* value, bool selected) noexcept {
				if (selected) {
						gfx_SetColor(ui::color::kBlue);
						gfx_FillRectangle(0, y - 1, kScreenW, 14);
						gfx_SetTextFGColor(ui::color::kWhite);
				} else {
						gfx_SetTextFGColor(ui::color::kBlack);
				}
				gfx_SetTextXY(l.margin_x, y + 2);
				gfx_PrintString(name);
				gfx_PrintString(": ");
				gfx_PrintString(value);
		};

		const char* line_name = (s.line == kLineRow) ? "expansion.row"_tx : "expansion.col"_tx;
		draw_field(y_base, "expansion.along"_tx, line_name, s.focus == 0);

		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append_u64(static_cast<std::uint64_t>(s.index) + 1u);
		}
		draw_field(y_base + line_h, line_name, fmt_buf_, s.focus == 1);
}

void App::update_expansion_pick(ExpansionPickState& s) noexcept {
		if (input_.pressed(kKbGroup6, kb_Clear)) {
				SHELL_DBG("[expand] CLEAR back\n");
				REQUIRE(pop(), "pop failed");
				return;
		}

		if (input_.repeat(kKbGroupArrows, kb_Up) && s.focus > 0)
				s.focus--;
		if (input_.repeat(kKbGroupArrows, kb_Down) && s.focus < 1)
				s.focus++;

		// repeat() tracks hold state, so poll both arrows every frame
		const bool left = input_.repeat(kKbGroupArrows, kb_Left);
		const bool right = input_.repeat(kKbGroupArrows, kb_Right);
		if (s.focus == 0) {
				// two choices: either arrow flips between row and column
				if (left || right)
						s.line = (s.line == kLineRow) ? kLineCol : kLineRow;
		} else {
				if (left && s.index > 0)
						s.index--;
				if (right && (s.index + 1u) < s.n)
						s.index++;
		}

		if (!input_.pressed(kKbGroup6, kb_Enter))
				return;

		if (s.slot >= kSlotCount || !slots_[s.slot].is_set())
				fail_fast("update_expansion_pick: expected slot to be set");

		const matrix_core::MatrixView a = slots_[s.slot].view_active();
		const matrix_core::ExpansionLine line = (s.line == kLineRow) ? matrix_core::ExpansionLine::Row : matrix_core::ExpansionLine::Col;
		SHELL_DBG("[expand] RUN slot=%c line=%u index=%u\n", (char)('A' + s.slot), (unsigned)s.line, (unsigned)s.index);

		// ephemeral tail has to be clear before allocating an explanation
		persist_.rewind(persist_base_mark_);
		persist_tail_mark_ = persist_base_mark_;
		expl_ = {};

		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);
		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::Rational det = matrix_core::Rational::from_int(0);
		const matrix_core::Error err =
		        matrix_core::op_cofactor_expansion(a, line, s.index, scratch_, &det, &expl, opts_steps, expansion_labels());
		SHELL_DBG("[op] expansion err=%u value=", (unsigned)err.code);
		dbg_print_rational(det);
		SHELL_DBG("\n");
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::NotSquare || err.code == matrix_core::ErrorCode::IndexOutOfRange ||
				        err.code == matrix_core::ErrorCode::Internal) {
						fail_fast("update_expansion_pick: expansion returned unexpected error");
				}
				show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();

		// pop this page and the slot picker under it, then show the result
		if (depth_ < 2 || stack_[depth_ - 2].kind != PageKind::SlotPick)
				fail_fast("update_expansion_pick: expected SlotPick under ExpansionPick");
		const std::uint8_t slot = s.slot;
		const std::uint8_t line_id = s.line;
		const std::uint8_t index = s.index;
		REQUIRE(pop(), "pop failed (expansion page)");
		REQUIRE(pop(), "pop failed (slot pick)");

		// `i` carries the line (0 row, 1 column), `j` its index
		Page p = Page::make_result_scalar(OperationId::CofactorExpansion, slot, 0, expl_.available(), det.num(), det.den());
		p.u.result.i = line_id;
		p.u.result.j = index;
		REQUIRE(push(p), "push expansion result failed");
}

void App::render_result_cofactor_expansion(const ResultState& s) noexcept {
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
		gfx_PrintString((s.i == kLineRow) ? "result.expanded_across_row"_tx : "result.expanded_down_col"_tx);
		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append_u64(static_cast<std::uint64_t>(s.j) + 1u);
		}
		gfx_PrintString(fmt_buf_);
}

} // namespace matrix_shell
