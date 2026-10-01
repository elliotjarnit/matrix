#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>
#include <keypadc.h>

#include <new>
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
using detail::op_is_binary;
using detail::op_name;
using namespace matrix_shell::text_literals;
} // namespace

void App::render_slot_pick(const SlotPickState& s) noexcept {
		const ui::Layout l = ui::Layout{};

		render_header(nullptr);
		gfx_PrintString(op_name(s.op));
		gfx_PrintString(": ");
		if (op_is_binary(s.op))
				gfx_PrintString((s.stage == 0) ? "slot_pick.pick1"_tx : "slot_pick.pick2"_tx);
		else
				gfx_PrintString("slot_pick.pick"_tx);

		render_footer_hint("footer.select_back"_tx);

		const int list_top = l.header_h + l.margin_y;
		const int line_h = 14;
		const int visible = (kScreenH - l.header_h - l.footer_h - 2 * l.margin_y) / line_h;

		for (std::uint8_t row = 0; row < visible; row++) {
				const std::uint8_t idx = static_cast<std::uint8_t>(s.scroll + row);
				if (idx >= kSlotCount)
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

				const char name = static_cast<char>('A' + idx);
				gfx_PrintChar(name);
				gfx_PrintString(": ");

				const Slot& slot = slots_[idx];
				if (!slot.is_set()) {
						gfx_PrintString("common.unset"_tx);
				} else {
						gfx_PrintChar(static_cast<char>('0' + slot.rows));
						gfx_PrintChar('x');
						gfx_PrintChar(static_cast<char>('0' + slot.cols));
				}
		}
}

void App::update_slot_pick(SlotPickState& s) noexcept {
		// back
		if (input_.pressed(kKbGroup6, kb_Clear)) {
				if (op_is_binary(s.op) && s.stage == 1) {
						SHELL_DBG("[pick] CLEAR backstep op=%s stage=1 -> 0\n", op_name(s.op));
						s.stage = 0;
						return;
				}
				SHELL_DBG("[pick] CLEAR pop op=%s stage=%u\n", op_name(s.op), (unsigned)s.stage);
				REQUIRE(pop(), "pop failed");
				return;
		}

		const std::uint8_t old_cursor = s.cursor;
		if (input_.repeat(kKbGroupArrows, kb_Up)) {
				if (s.cursor > 0)
						s.cursor--;
		}
		if (input_.repeat(kKbGroupArrows, kb_Down)) {
				if (s.cursor + 1 < kSlotCount)
						s.cursor++;
		}
		if (s.cursor != old_cursor) {
				const int line_h = 14;
				const ui::Layout l = ui::Layout{};
				const int visible = (kScreenH - l.header_h - l.footer_h - 2 * l.margin_y) / line_h;
				s.scroll = keep_cursor_in_view(s.cursor, s.scroll, kSlotCount, visible);
		}

		if (!input_.pressed(kKbGroup6, kb_Enter))
				return;

		const std::uint8_t sel = s.cursor;
		if (sel >= kSlotCount || !slots_[sel].is_set()) {
				SHELL_DBG("[pick] ENTER sel=%c unset\n", (char)('A' + sel));
				show_message("common.slot_unset"_tx);
				return;
		}

		// drop any previous ephemeral result
		persist_.rewind(persist_base_mark_);
		expl_ = {};
		persist_tail_mark_ = persist_base_mark_;
		SHELL_DBG("[pick] rewind persist to base_mark=%u used=%u/%u\n",
		        (unsigned)persist_base_mark_,
		        (unsigned)persist_.used(),
		        (unsigned)persist_.capacity());

		if (op_is_binary(s.op) && s.stage == 0) {
				SHELL_DBG("[pick] binary op=%s pick1=%c\n", op_name(s.op), (char)('A' + sel));
				s.slot_a = sel;
				s.stage = 1;
				return;
		}

		if (op_is_binary(s.op))
				run_operation(s.op, s.slot_a, sel);
		else
				run_operation(s.op, sel, 0);
}

void App::run_operation(OperationId op, std::uint8_t slot_a, std::uint8_t slot_b) noexcept {
		SHELL_DBG("[pick] run op=%s a=%c b=%c\n", op_name(op), (char)('A' + slot_a), (char)('A' + slot_b));
		dbg_print_matrix("A", slots_[slot_a].view_active());
		if (op_is_binary(op))
				dbg_print_matrix("B", slots_[slot_b].view_active());

		switch (op) {
#define MATRIX_OP_IN(id, key, binary, run, body, steps)                                                                                    \
		case OperationId::id:                                                                                                              \
				run(op, slot_a, slot_b);                                                                                                   \
				return;
#define MATRIX_OP_OUT(id, key, binary, run, body, steps)
#include "matrix_shell/program/ops.inc"
#undef MATRIX_OP_IN
#undef MATRIX_OP_OUT
		default:
				break;
		}
		fail_fast("run_operation: operation not in this program");
}

// A + B, A - B and the products: allocate the result, run `fn`, and report a
// size mismatch the way the review exercises phrase it. `product` selects the
// m x p times p x n shape rule over the same size rule.
void App::run_binary_matrix_op(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot, bool product, BinaryMatrixOp fn) noexcept {
		const matrix_core::MatrixView a = slots_[a_slot].view_active();
		const matrix_core::MatrixView b = slots_[b_slot].view_active();

		matrix_core::Dim out_dim{};
		if (product)
				out_dim = {a.rows, b.cols};
		else
				out_dim = a.dim();

		SHELL_DBG("[op] %s dims A=%ux%u B=%ux%u out=%ux%u\n",
		        op_name(op),
		        (unsigned)a.rows,
		        (unsigned)a.cols,
		        (unsigned)b.rows,
		        (unsigned)b.cols,
		        (unsigned)out_dim.rows,
		        (unsigned)out_dim.cols);

		matrix_core::ArenaScope tx(persist_);
		matrix_core::MatrixMutView out{};
		const matrix_core::ErrorCode ec = matrix_core::matrix_alloc(persist_, out_dim.rows, out_dim.cols, &out);
		if (!matrix_core::is_ok(ec)) {
				SHELL_DBG("[op] alloc failed ec=%u used=%u/%u\n", (unsigned)ec, (unsigned)persist_.used(), (unsigned)persist_.capacity());
				show_message("common.out_of_memory"_tx);
				return;
		}
		SHELL_DBG("[op] alloc ok out.data=%p stride=%u used=%u/%u\n",
		        (void*)out.data,
		        (unsigned)out.stride,
		        (unsigned)persist_.used(),
		        (unsigned)persist_.capacity());

		matrix_core::Explanation expl;
		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;
		const matrix_core::Error err = fn(a, b, out, &expl, opts_steps);

		SHELL_DBG("[op] %s err=%u a=%ux%u b=%ux%u\n",
		        op_name(op),
		        (unsigned)err.code,
		        (unsigned)err.a.rows,
		        (unsigned)err.a.cols,
		        (unsigned)err.b.rows,
		        (unsigned)err.b.cols);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::DimensionMismatch) {
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						if (product)
								w.append("msg.inner_sizes_match_prefix"_tx);
						else
								w.append("msg.need_same_size_prefix"_tx);
						w.put(static_cast<char>('A' + a_slot));
						w.append("=");
						w.append_u64(a.rows);
						w.put('x');
						w.append_u64(a.cols);
						w.append(", ");
						w.put(static_cast<char>('A' + b_slot));
						w.append("=");
						w.append_u64(b.rows);
						w.put('x');
						w.append_u64(b.cols);
						show_message(fmt_buf_);
				} else if (err.code == matrix_core::ErrorCode::Internal || err.code == matrix_core::ErrorCode::InvalidDimension)
						fail_fast("update_slot_pick: binary op returned unexpected error");
				else
						show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (binary op)");
		REQUIRE(push(Page::make_result_matrix(op, a_slot, b_slot, expl_.available(), out.rows, out.cols, out.stride, out.data)),
		        "push binary result failed");
}

} // namespace matrix_shell
