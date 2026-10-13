#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include <graphx.h>
#include <keypadc.h>

namespace matrix_shell {
namespace {
using detail::fail_fast;
using detail::keep_cursor_in_view;
using detail::kKbGroup1;
using detail::kKbGroup6;
using detail::kKbGroupArrows;
using detail::kScreenH;
using detail::kScreenW;
using detail::op_enabled;
using detail::op_name;
using namespace matrix_shell::text_literals;

template <typename T, std::size_t N> constexpr std::uint8_t array_count(const T (&)[N]) noexcept {
		static_assert(N <= 32);
		return static_cast<std::uint8_t>(N);
}

constexpr std::uint8_t kMenuMaxEntries = 24;

enum class MenuEntryKind : std::uint8_t {
		Submenu = 0,
		Operation,
};

struct MenuEntry {
		TextId label = TextId::None;
		MenuEntryKind kind = MenuEntryKind::Submenu;
		MenuId submenu = MenuId::Main;     // when kind==Submenu
		OperationId op = OperationId::Add; // when kind==Operation
};

constexpr MenuEntry submenu(TextId label, MenuId id) noexcept {
		return {label, MenuEntryKind::Submenu, id, OperationId::Add};
}

// labelled with the operation's own name unless `label` overrides it
constexpr MenuEntry operation(OperationId op, TextId label = TextId::None) noexcept {
		return {label, MenuEntryKind::Operation, MenuId::Main, op};
}

// The one menu tree every program shares. An operation whose module the program
// leaves out is hidden, and so is a submenu with nothing visible in it, so a
// program made of a few modules shows just those operations in the usual place.

constexpr MenuEntry kMainMenuEntries[] = {
        submenu("menu.entry.matrices"_tid, MenuId::Matrices),
        submenu("menu.entry.operations"_tid, MenuId::Operations),
};

constexpr MenuEntry kMatricesMenuEntries[] = {
        submenu("menu.entry.edit_slots"_tid, MenuId::SlotList),
};

constexpr MenuEntry kOperationsMenuEntries[] = {
        submenu("menu.entry.add_sub"_tid, MenuId::AddSub),
        submenu("menu.entry.multiply"_tid, MenuId::Multiply),
        operation(OperationId::Dot),
        operation(OperationId::Cross),
        operation(OperationId::Det),
        operation(OperationId::CofactorExpansion),
        operation(OperationId::DetTriangular),
        operation(OperationId::Adjugate),
        submenu("menu.entry.cofactor_cramer"_tid, MenuId::CofactorCramer),
        submenu("menu.entry.area"_tid, MenuId::Area),
        submenu("menu.entry.ref_rref"_tid, MenuId::RefRref),
        operation(OperationId::SolveRref),
        submenu("menu.entry.span_indep"_tid, MenuId::Span),
        operation(OperationId::Transpose),
        operation(OperationId::Inverse),
        operation(OperationId::Invertible),
        operation(OperationId::IndepDet),
        operation(OperationId::LuFactor),
        operation(OperationId::LuSolve),
        submenu("menu.entry.spaces"_tid, MenuId::Spaces),
        operation(OperationId::Basis),
        operation(OperationId::Projection),
};

constexpr MenuEntry kAddSubMenuEntries[] = {
        operation(OperationId::Add),
        operation(OperationId::Sub),
};

constexpr MenuEntry kMultiplyMenuEntries[] = {
        operation(OperationId::Mul),
        operation(OperationId::MulColRow),
};

constexpr MenuEntry kCofactorCramerMenuEntries[] = {
        operation(OperationId::CofactorElement),
        operation(OperationId::MinorMatrix),
        operation(OperationId::Cramer),
};

constexpr MenuEntry kAreaMenuEntries[] = {
        operation(OperationId::AreaParallelogram),
        operation(OperationId::AreaTriangle),
        operation(OperationId::AreaTransform),
};

constexpr MenuEntry kRefRrefMenuEntries[] = {
        operation(OperationId::Ref),
        operation(OperationId::Rref),
};

constexpr MenuEntry kSpanMenuEntries[] = {
        operation(OperationId::SolveRref, "menu.entry.in_span_solve"_tid),
        operation(OperationId::SpanTest),
        operation(OperationId::IndepTest),
        operation(OperationId::ColSpaceBasis, "menu.entry.basis_from_set"_tid),
        operation(OperationId::Transformation, "menu.entry.onto_one_to_one"_tid),
};

constexpr MenuEntry kSpacesMenuEntries[] = {
        operation(OperationId::ColSpaceBasis),
        operation(OperationId::RowSpaceBasis),
        operation(OperationId::NullSpaceBasis),
        operation(OperationId::LeftNullSpaceBasis),
};

struct MenuDef {
		TextId title = TextId::None;
		const MenuEntry* entries = nullptr; // null for the slot list
		std::uint8_t count = 0;
};

MenuDef menu_def(MenuId id) noexcept {
		switch (id) {
		case MenuId::Main:
				return {"menu.main.title"_tid, kMainMenuEntries, array_count(kMainMenuEntries)};
		case MenuId::Matrices:
				return {"menu.matrices.title"_tid, kMatricesMenuEntries, array_count(kMatricesMenuEntries)};
		case MenuId::Operations:
				return {"menu.operations.title"_tid, kOperationsMenuEntries, array_count(kOperationsMenuEntries)};
		case MenuId::Span:
				return {"menu.span.title"_tid, kSpanMenuEntries, array_count(kSpanMenuEntries)};
		case MenuId::Spaces:
				return {"menu.spaces.title"_tid, kSpacesMenuEntries, array_count(kSpacesMenuEntries)};
		case MenuId::AddSub:
				return {"menu.add_sub.title"_tid, kAddSubMenuEntries, array_count(kAddSubMenuEntries)};
		case MenuId::RefRref:
				return {"menu.ref_rref.title"_tid, kRefRrefMenuEntries, array_count(kRefRrefMenuEntries)};
		case MenuId::Multiply:
				return {"menu.multiply.title"_tid, kMultiplyMenuEntries, array_count(kMultiplyMenuEntries)};
		case MenuId::CofactorCramer:
				return {"menu.cofactor_cramer.title"_tid, kCofactorCramerMenuEntries, array_count(kCofactorCramerMenuEntries)};
		case MenuId::Area:
				return {"menu.area.title"_tid, kAreaMenuEntries, array_count(kAreaMenuEntries)};
		case MenuId::SlotList:
				return {"menu.edit_matrix.title"_tid, nullptr, 0};
		}
		fail_fast("Unhandled MenuId in menu_def");
}

bool menu_has_visible(MenuId id) noexcept;

// the tree is three levels deep, so the recursion through submenus is too
bool entry_visible(const MenuEntry& e) noexcept {
		if (e.kind == MenuEntryKind::Operation)
				return op_enabled(e.op);
		return menu_has_visible(e.submenu);
}

bool menu_has_visible(MenuId id) noexcept {
		const MenuDef def = menu_def(id);
		if (!def.entries)
				return true; // the slot list always has its eight slots
		for (std::uint8_t i = 0; i < def.count; i++) {
				if (entry_visible(def.entries[i]))
						return true;
		}
		return false;
}

// indices into def.entries of the entries this program shows
std::uint8_t build_visible_menu(const MenuDef& def, std::uint8_t* out) noexcept {
		std::uint8_t count = 0;
		for (std::uint8_t i = 0; i < def.count; i++) {
				if (!entry_visible(def.entries[i]))
						continue;
				if (count >= kMenuMaxEntries)
						fail_fast("build_visible_menu overflow");
				out[count++] = i;
		}
		return count;
}

const char* menu_entry_label(const MenuEntry& e) noexcept {
		if (e.label != TextId::None)
				return tr(e.label);
		if (e.kind == MenuEntryKind::Operation)
				return op_name(e.op);
		return "";
}

int visible_rows() noexcept {
		const int line_h = 14;
		const ui::Layout l = ui::Layout{};
		return (kScreenH - l.header_h - l.footer_h - 2 * l.margin_y) / line_h;
}
} // namespace

void App::render_menu(const MenuState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		const MenuDef def = menu_def(s.id);

		if (s.id == MenuId::Main)
				render_footer_hint("footer.select_exit"_tx);
		else if (s.id == MenuId::SlotList)
				render_footer_hint("footer.edit_slot_menu"_tx);
		else
				render_footer_hint("footer.select_back"_tx);
		render_header(tr(def.title));

		std::uint8_t visible_entries[kMenuMaxEntries]{};
		std::uint8_t count = 0;
		if (s.id == MenuId::SlotList)
				count = kSlotCount;
		else
				count = build_visible_menu(def, visible_entries);

		const int list_top = l.header_h + l.margin_y;
		const int line_h = 14;
		const int visible = visible_rows();

		const std::uint8_t scroll = s.scroll;
		const std::uint8_t cursor = s.cursor;

		for (std::uint8_t row = 0; row < visible; row++) {
				const std::uint8_t idx = static_cast<std::uint8_t>(scroll + row);
				if (idx >= count)
						break;

				const int y = list_top + row * line_h;
				const bool selected = (idx == cursor);
				if (selected) {
						gfx_SetColor(ui::color::kBlue);
						gfx_FillRectangle(0, y - 1, kScreenW, line_h);
						gfx_SetTextFGColor(ui::color::kWhite);
				} else {
						gfx_SetTextFGColor(ui::color::kBlack);
				}

				gfx_SetTextXY(l.margin_x, y + 2);

				if (s.id == MenuId::SlotList) {
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
				} else {
						gfx_PrintString(menu_entry_label(def.entries[visible_entries[idx]]));
				}
		}
}

void App::update_menu(MenuState& s) noexcept {
		const bool clear_pressed = input_.pressed(kKbGroup6, kb_Clear);
		if (clear_pressed) {
				SHELL_DBG("[menu] CLEAR back id=%u cursor=%u scroll=%u\n", (unsigned)s.id, (unsigned)s.cursor, (unsigned)s.scroll);
				REQUIRE(pop(), "pop failed");
				return;
		}

		const MenuDef def = menu_def(s.id);
		std::uint8_t visible_entries[kMenuMaxEntries]{};
		std::uint8_t count = 0;
		if (s.id == MenuId::SlotList)
				count = kSlotCount;
		else
				count = build_visible_menu(def, visible_entries);

		if (count == 0)
				fail_fast("update_menu: menu has no visible entries");

		if (s.cursor >= count)
				s.cursor = static_cast<std::uint8_t>(count - 1u);

		const std::uint8_t old_cursor = s.cursor;
		if (input_.repeat(kKbGroupArrows, kb_Up)) {
				if (s.cursor > 0)
						s.cursor--;
		}
		if (input_.repeat(kKbGroupArrows, kb_Down)) {
				if (s.cursor + 1 < count)
						s.cursor++;
		}
		if (s.cursor != old_cursor)
				s.scroll = keep_cursor_in_view(s.cursor, s.scroll, count, visible_rows());

		if (s.id == MenuId::SlotList) {
				const std::uint8_t slot = s.cursor;
				const Slot& sl = slots_[slot];

				// resize: 2ND
				if (input_.pressed(kKbGroup1, kb_2nd)) {
						SHELL_DBG("[slotlist] 2ND resize sel=%c set=%u\n", (char)('A' + slot), (unsigned)sl.is_set());
						if (sl.is_set())
								REQUIRE(push(Page::make_confirm(slot, ConfirmAction::Resize)), "push confirm resize failed");
						else
								REQUIRE(push(Page::make_dim(slot, 2, 2)), "push dim failed");
						return;
				}

				// clear slot: DEL
				if (input_.pressed(kKbGroup1, kb_Del)) {
						SHELL_DBG("[slotlist] DEL clear sel=%c set=%u\n", (char)('A' + slot), (unsigned)sl.is_set());
						if (sl.is_set())
								REQUIRE(push(Page::make_confirm(slot, ConfirmAction::Clear)), "push confirm clear failed");
						else
								show_message("common.slot_unset"_tx);
						return;
				}

				if (!input_.pressed(kKbGroup6, kb_Enter))
						return;

				SHELL_DBG("[slotlist] ENTER edit sel=%c set=%u rows=%u cols=%u\n",
				        (char)('A' + slot),
				        (unsigned)sl.is_set(),
				        (unsigned)sl.rows,
				        (unsigned)sl.cols);

				// edit entries: ENTER
				if (sl.is_set())
						REQUIRE(push(Page::make_editor(slot)), "push editor failed");
				else
						REQUIRE(push(Page::make_dim(slot, 2, 2)), "push dim failed");
				return;
		}

		if (!input_.pressed(kKbGroup6, kb_Enter))
				return;

		const MenuEntry& e = def.entries[visible_entries[s.cursor]];
		if (e.kind == MenuEntryKind::Submenu) {
				SHELL_DBG("[menu] submenu id=%u cursor=%u -> %u\n", (unsigned)s.id, (unsigned)s.cursor, (unsigned)e.submenu);
				REQUIRE(push(Page::make_menu(e.submenu)), "push submenu menu failed");
				return;
		}

		SHELL_DBG("[menu] op id=%u cursor=%u -> %u\n", (unsigned)s.id, (unsigned)s.cursor, (unsigned)e.op);
		REQUIRE(push(Page::make_slot_pick(e.op)), "push slot pick from menu failed");
}

} // namespace matrix_shell
