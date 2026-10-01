#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_cofactor.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>
#include <keypadc.h>

#include <utility>

namespace matrix_shell {
namespace {
using detail::fail_fast;
using detail::kKbGroup6;
using detail::kKbGroupArrows;
using detail::kScreenW;
using detail::op_name;
using namespace matrix_shell::text_literals;

Page make_cofactor_element(std::uint8_t slot, std::uint8_t n) noexcept {
		Page p{};
		p.kind = PageKind::CofactorElement;
		p.u.cofactor_element = CofactorElementState{slot, n, 0, 0, 0};
		return p;
}

Page make_result_cofactor_element(
        std::uint8_t slot_a, std::uint8_t i, std::uint8_t j, bool has_steps, std::int64_t num, std::int64_t den) noexcept {
		Page p{};
		p.kind = PageKind::Result;
		ResultState r;
		r.op = OperationId::CofactorElement;
		r.slot_a = slot_a;
		r.slot_b = 0;
		r.is_scalar = true;
		r.has_steps = has_steps;
		r.rows = 0;
		r.cols = 0;
		r.stride = 0;
		r.data = nullptr;
		r.i = i;
		r.j = j;
		r.num = num;
		r.den = den;
		p.u.result = r;
		return p;
}
} // namespace

void App::render_cofactor_element(const CofactorElementState& s) noexcept {
		const ui::Layout l = ui::Layout{};

		render_header("op.cofactor"_tx);
		render_footer_hint("footer.cofactor_adjust"_tx);

		gfx_SetTextFGColor(ui::color::kBlack);
		gfx_SetTextXY(l.margin_x, l.header_h + 10);
		gfx_PrintString("cofactor.matrix_prefix"_tx);
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

		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append_u64(static_cast<std::uint64_t>(s.i) + 1u);
		}
		draw_field(y_base + 0 * line_h, "cofactor.row_i"_tx, fmt_buf_, s.focus == 0);

		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append_u64(static_cast<std::uint64_t>(s.j) + 1u);
		}
		draw_field(y_base + 1 * line_h, "cofactor.col_j"_tx, fmt_buf_, s.focus == 1);
}

void App::update_cofactor_element(CofactorElementState& s) noexcept {
		// back
		if (input_.pressed(kKbGroup6, kb_Clear)) {
				SHELL_DBG("[cof] CLEAR back\n");
				REQUIRE(pop(), "pop failed");
				return;
		}

		// field nav
		if (input_.repeat(kKbGroupArrows, kb_Up)) {
				if (s.focus > 0)
						s.focus--;
		}
		if (input_.repeat(kKbGroupArrows, kb_Down)) {
				if (s.focus < 1)
						s.focus++;
		}

		// adjust focused field
		if (s.focus == 0) {
				if (input_.repeat(kKbGroupArrows, kb_Left) && s.i > 0)
						s.i--;
				if (input_.repeat(kKbGroupArrows, kb_Right) && (s.i + 1u) < s.n)
						s.i++;
		} else {
				if (input_.repeat(kKbGroupArrows, kb_Left) && s.j > 0)
						s.j--;
				if (input_.repeat(kKbGroupArrows, kb_Right) && (s.j + 1u) < s.n)
						s.j++;
		}

		if (!input_.pressed(kKbGroup6, kb_Enter))
				return;

		if (s.slot >= kSlotCount || !slots_[s.slot].is_set())
				fail_fast("update_cofactor_element: expected slot to be set");

		const matrix_core::MatrixView a = slots_[s.slot].view_active();
		SHELL_DBG("[cof] RUN slot=%c i=%u j=%u\n", (char)('A' + s.slot), (unsigned)s.i, (unsigned)s.j);

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
		matrix_core::Rational cofactor = matrix_core::Rational::from_int(0);
		const matrix_core::Error err = matrix_core::op_cofactor_element(a, s.i, s.j, scratch_, &cofactor, &expl, opts_steps);

		SHELL_DBG("[op] cofactor err=%u a=%ux%u\n", (unsigned)err.code, (unsigned)err.a.rows, (unsigned)err.a.cols);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::NotSquare || err.code == matrix_core::ErrorCode::IndexOutOfRange ||
				        err.code == matrix_core::ErrorCode::Internal) {
						fail_fast("update_cofactor_element: cofactor returned unexpected error");
				}
				show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);

		// pop Cofactor page and its underlying slot picker, then show result
		if (depth_ < 2)
				fail_fast("update_cofactor_element: expected >=2 pages");
		if (stack_[depth_ - 1].kind != PageKind::CofactorElement)
				fail_fast("update_cofactor_element: expected CofactorElement on top");
		if (stack_[depth_ - 2].kind != PageKind::SlotPick)
				fail_fast("update_cofactor_element: expected SlotPick under CofactorElement");

		REQUIRE(pop(), "pop failed (cofactor page)");
		REQUIRE(pop(), "pop failed (slot pick)");
		REQUIRE(push(make_result_cofactor_element(s.slot, s.i, s.j, expl_.available(), cofactor.num(), cofactor.den())),
		        "push result failed");
}

void App::run_cofactor_element(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
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

		REQUIRE(push(make_cofactor_element(slot, a.rows)), "push cofactor element page failed");
}

void App::render_result_cofactor_element(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		const int y0 = l.header_h + 30;

		gfx_SetTextXY(l.margin_x, y0);
		gfx_PrintString("C_{");

		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
		w.append_u64(static_cast<std::uint64_t>(s.i) + 1u);
		w.put(',');
		w.append_u64(static_cast<std::uint64_t>(s.j) + 1u);
		gfx_PrintString(fmt_buf_);
		gfx_PrintString("} = ");

		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w2{fmt_buf_, sizeof(fmt_buf_)};
		if (s.den == 1)
				w2.append_i64(s.num);
		else {
				w2.append_i64(s.num);
				w2.put('/');
				w2.append_i64(s.den);
		}
		gfx_PrintString(fmt_buf_);
}

} // namespace matrix_shell
