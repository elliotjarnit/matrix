#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_cramer.hpp"
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

Page make_cramer_steps_menu(std::uint8_t slot_a, std::uint8_t slot_b, std::uint8_t n) noexcept {
		Page p{};
		p.kind = PageKind::CramerStepsMenu;
		p.u.cramer_steps_menu = CramerStepsMenuState{slot_a, slot_b, n, 0, 0};
		return p;
}
} // namespace

void App::run_cramer(OperationId, std::uint8_t a_slot, std::uint8_t b_slot) noexcept {
		const matrix_core::MatrixView a = slots_[a_slot].view_active();
		const matrix_core::MatrixView b = slots_[b_slot].view_active();
		if (a.rows != a.cols) {
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append("msg.a_must_be_square_prefix"_tx);
				w.put(static_cast<char>('A' + a_slot));
				w.append("=");
				w.append_u64(a.rows);
				w.put('x');
				w.append_u64(a.cols);
				show_message(fmt_buf_);
				return;
		}
		if (b.rows != a.rows || b.cols != 1) {
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append("msg.need_b_nx1_prefix"_tx);
				w.append_u64(a.rows);
				w.put('x');
				w.append_u64(a.cols);
				w.append("msg.b_is_prefix"_tx);
				w.append_u64(b.rows);
				w.put('x');
				w.append_u64(b.cols);
				show_message(fmt_buf_);
				return;
		}

		matrix_core::ArenaScope tx(persist_);
		matrix_core::MatrixMutView x_out{};
		const matrix_core::ErrorCode ec = matrix_core::matrix_alloc(persist_, a.rows, 1, &x_out);
		if (!matrix_core::is_ok(ec)) {
				SHELL_DBG("[op] cramer alloc failed ec=%u used=%u/%u\n",
				        (unsigned)ec,
				        (unsigned)persist_.used(),
				        (unsigned)persist_.capacity());
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::ArenaScratchScope scratch_tx(scratch_);
		const matrix_core::Error err = matrix_core::op_cramer_solve(a, b, scratch_, x_out);
		SHELL_DBG("[op] cramer err=%u a=%ux%u b=%ux%u\n",
		        (unsigned)err.code,
		        (unsigned)err.a.rows,
		        (unsigned)err.a.cols,
		        (unsigned)err.b.rows,
		        (unsigned)err.b.cols);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::Singular)
						show_message("msg.no_unique_solution"_tx);
				else if (err.code == matrix_core::ErrorCode::NotSquare || err.code == matrix_core::ErrorCode::DimensionMismatch ||
				         err.code == matrix_core::ErrorCode::Internal)
						fail_fast("update_slot_pick: cramer returned unexpected error");
				else
						show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = {};
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (cramer)");
		REQUIRE(push(Page::make_result_matrix(OperationId::Cramer, a_slot, b_slot, true, x_out.rows, x_out.cols, x_out.stride, x_out.data)),
		        "push cramer result failed");
}

// Cramer's rule has no single explanation: 2ND opens a menu of the
// determinants (det A and each A_i) and shows the steps of the one picked.
bool App::open_cramer_steps(const ResultState& s) noexcept {
		SHELL_DBG("[result] 2ND cramer steps menu\n");
		REQUIRE(push(make_cramer_steps_menu(s.slot_a, s.slot_b, s.rows)), "push cramer steps menu failed");
		return true;
}

void App::render_cramer_steps_menu(const CramerStepsMenuState& s) noexcept {
		const ui::Layout l = ui::Layout{};

		render_header("steps.cramer_title"_tx);
		render_footer_hint("footer.select_back"_tx);

		const std::uint8_t count = static_cast<std::uint8_t>(s.n + 1u);

		const int list_top = l.header_h + l.margin_y;
		const int line_h = 14;
		const int visible = (kScreenH - l.header_h - l.footer_h - 2 * l.margin_y) / line_h;

		for (std::uint8_t row = 0; row < visible; row++) {
				const std::uint8_t idx = static_cast<std::uint8_t>(s.scroll + row);
				if (idx >= count)
						break;

				const int y = list_top + row * line_h;
				const bool selected = (idx == s.cursor);
				if (selected) {
						gfx_SetColor(ui::color::kBlue);
						gfx_FillRectangle(0, y - 1, kScreenW, line_h);
						gfx_SetTextFGColor(ui::color::kWhite);
				} else {
						gfx_SetTextFGColor(ui::color::kBlack);
				}

				gfx_SetTextXY(l.margin_x, y + 2);
				if (idx == 0) {
						gfx_PrintString("steps.delta_det_a"_tx);
				} else {
						gfx_PrintString("steps.delta_label"_tx);
						gfx_PrintChar('_');
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						w.append_u64(static_cast<std::uint64_t>(idx));
						gfx_PrintString(fmt_buf_);
				}
		}
}

void App::update_cramer_steps_menu(CramerStepsMenuState& s) noexcept {
		if (input_.pressed(kKbGroup6, kb_Clear)) {
				SHELL_DBG("[cramer] CLEAR back -> drop expl\n");
				REQUIRE(pop(), "pop failed");
				persist_.rewind(persist_tail_mark_);
				expl_ = {};
				return;
		}

		const std::uint8_t count = static_cast<std::uint8_t>(s.n + 1u);

		const std::uint8_t old_cursor = s.cursor;
		if (input_.repeat(kKbGroupArrows, kb_Up)) {
				if (s.cursor > 0)
						s.cursor--;
		}
		if (input_.repeat(kKbGroupArrows, kb_Down)) {
				if (s.cursor + 1 < count)
						s.cursor++;
		}
		if (s.cursor != old_cursor) {
				const int line_h = 14;
				const ui::Layout l = ui::Layout{};
				const int visible = (kScreenH - l.header_h - l.footer_h - 2 * l.margin_y) / line_h;
				s.scroll = keep_cursor_in_view(s.cursor, s.scroll, count, visible);
		}

		if (!input_.pressed(kKbGroup6, kb_Enter))
				return;

		if (s.slot_a >= kSlotCount || s.slot_b >= kSlotCount)
				fail_fast("update_cramer_steps_menu: slot out of range");
		if (!slots_[s.slot_a].is_set() || !slots_[s.slot_b].is_set())
				fail_fast("update_cramer_steps_menu: expected slots to be set");

		const matrix_core::MatrixView a = slots_[s.slot_a].view_active();
		const matrix_core::MatrixView b = slots_[s.slot_b].view_active();

		// replace any previous Δ/Δ_i explanation but keep the result matrix
		persist_.rewind(persist_tail_mark_);
		expl_ = {};

		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);
		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::Rational v = matrix_core::Rational::from_int(0);
		matrix_core::Error err{};

		if (s.cursor == 0) {
				SHELL_DBG("[cramer] run det(A)\n");
				err = matrix_core::op_det(a, scratch_, &v, &expl, opts_steps);
		} else {
				const std::uint8_t col = static_cast<std::uint8_t>(s.cursor - 1u);
				SHELL_DBG("[cramer] run det_replace col=%u\n", (unsigned)col);
				err = matrix_core::op_det_replace_column(a, b, col, scratch_, &v, &expl, opts_steps);
		}

		SHELL_DBG("[cramer] step err=%u\n", (unsigned)err.code);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::NotSquare || err.code == matrix_core::ErrorCode::DimensionMismatch ||
				        err.code == matrix_core::ErrorCode::IndexOutOfRange || err.code == matrix_core::ErrorCode::InvalidDimension ||
				        err.code == matrix_core::ErrorCode::Internal) {
						fail_fast("update_cramer_steps_menu: unexpected error");
				}
				show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		REQUIRE(push(Page::make_steps()), "push steps failed");
}

} // namespace matrix_shell
