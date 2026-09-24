#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/writer.hpp"

#include <graphx.h>
#include <keypadc.h>

namespace matrix_shell {
namespace {
using detail::fail_fast;
using detail::kKbGroup6;
using namespace matrix_shell::text_literals;

constexpr bool is_lu_pivot(ConfirmAction action) noexcept {
		return action == ConfirmAction::LuPivotFactor || action == ConfirmAction::LuPivotSolve;
}
} // namespace

void App::render_confirm(const ConfirmState& s) noexcept {
		render_header("common.confirm_title"_tx);
		render_footer_hint("footer.yes_no"_tx);

		if (s.slot >= kSlotCount)
				fail_fast("render_confirm: slot out of range");

		gfx_SetTextFGColor(ui::color::kBlack);

		if (is_lu_pivot(s.action)) {
				// say why first, then offer the permuted factorization
				gfx_SetTextXY(12, 52);
				gfx_PrintString("msg.lu_row_swap_prefix"_tx);
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append_u64(static_cast<std::uint64_t>(s.col) + 1u);
				gfx_PrintString(fmt_buf_);

				gfx_SetTextXY(12, 70);
				gfx_PrintString("confirm.lu_pivot_prompt"_tx);
				return;
		}

		gfx_SetTextXY(12, 60);

		gfx_PrintChar(static_cast<char>('A' + s.slot));
		gfx_PrintString(": ");

		switch (s.action) {
		case ConfirmAction::Resize:
				gfx_PrintString("confirm.resize_prompt"_tx);
				break;
		case ConfirmAction::Clear:
				gfx_PrintString("confirm.clear_prompt"_tx);
				break;
		default:
				fail_fast("render_confirm: unhandled ConfirmAction");
		}
}

void App::update_confirm(ConfirmState& s) noexcept {
		// CLEAR: no
		if (input_.pressed(kKbGroup6, kb_Clear)) {
				SHELL_DBG("[confirm] CLEAR cancel action=%u slot=%c\n", (unsigned)s.action, (char)('A' + s.slot));
				REQUIRE(pop(), "pop failed (cancel)");
				return;
		}

		// ENTER: yes
		if (!input_.pressed(kKbGroup6, kb_Enter))
				return;

		if (s.slot >= kSlotCount)
				fail_fast("update_confirm: slot out of range");

		switch (s.action) {
		case ConfirmAction::LuPivotFactor: {
				// the confirmation is popped first so the LU run replaces the slot
				// picker underneath it, exactly as a direct run would
				const std::uint8_t slot = s.slot;
				SHELL_DBG("[confirm] ENTER lu pivot factor slot=%c\n", (char)('A' + slot));
				REQUIRE(pop(), "pop failed (lu pivot factor)");
				run_lu_factor(slot, true);
				return;
		}
		case ConfirmAction::LuPivotSolve: {
				const std::uint8_t slot_a = s.slot;
				const std::uint8_t slot_b = s.slot_b;
				SHELL_DBG("[confirm] ENTER lu pivot solve a=%c b=%c\n", (char)('A' + slot_a), (char)('A' + slot_b));
				REQUIRE(pop(), "pop failed (lu pivot solve)");
				run_lu_solve(slot_a, slot_b, true);
				return;
		}
		case ConfirmAction::Clear:
				SHELL_DBG("[confirm] ENTER clear slot=%c\n", (char)('A' + s.slot));
				clear_slot(s.slot);
				show_message("common.cleared"_tx);
				REQUIRE(pop(), "pop failed (clear)");
				return;
		case ConfirmAction::Resize:
				// resize: go to dim page
				SHELL_DBG("[confirm] ENTER resize slot=%c -> dim 2x2\n", (char)('A' + s.slot));
				REQUIRE(pop(), "pop failed (resize)");
				REQUIRE(push(Page::make_dim(s.slot, 2, 2)), "push dim failed");
				return;
		}

		fail_fast("Unhandled ConfirmAction in update_confirm");
}

} // namespace matrix_shell
