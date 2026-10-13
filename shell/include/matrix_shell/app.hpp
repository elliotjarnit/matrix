#pragma once

#include <cstddef>
#include <cstdint>

#include "matrix_core/arena.hpp"
#include "matrix_core/explanation.hpp"
#include "matrix_core/ops.hpp"
#include "matrix_core/matrix.hpp"
#include "matrix_core/rational.hpp"
#include "matrix_core/slab.hpp"

#include "matrix_shell/input.hpp"
#include "matrix_shell/config.hpp"

struct TeX_Layout;
struct TeX_Renderer;

namespace matrix_shell {

constexpr std::size_t kMaxPageDepth = 8;
constexpr std::uint8_t kSlotCount = 8;

// The menu tree shared by every program (shell/src/pages/menu.cpp). Entries
// for operations the program leaves out are hidden, and so is a submenu with
// nothing left in it.
enum class MenuId : std::uint8_t {
		Main,
		Matrices,
		Operations,
		Span,
		Spaces,
		AddSub,
		RefRref,
		Multiply,
		CofactorCramer,
		Area,
		SlotList, // A..H (edit)
};

// Every module's operations, whether or not this program includes them, so the
// menu tree compiles the same everywhere; see modules/*/ops.def. A row is
//
//   MATRIX_OP(Id, "op.text_key", is_binary, run, result_body, result_steps)
//
// where `run`, `result_body` and `result_steps` are App members:
//   void run(OperationId, std::uint8_t slot_a, std::uint8_t slot_b) noexcept
//   void result_body(const ResultState&) noexcept      draws a Result page
//   bool result_steps(const ResultState&) noexcept      2ND on a Result page
// The generated ops.inc expands the rows of included modules as MATRIX_OP_IN
// and the rest as MATRIX_OP_OUT.
enum class OperationId : std::uint8_t {
#define MATRIX_OP_IN(id, key, binary, run, body, steps) id,
#define MATRIX_OP_OUT(id, key, binary, run, body, steps) id,
#include "matrix_shell/program/ops.inc"
#undef MATRIX_OP_IN
#undef MATRIX_OP_OUT
};

struct MenuState {
		MenuId id;
		std::uint8_t cursor;
		std::uint8_t scroll;
};

struct DimState {
		std::uint8_t slot;
		std::uint8_t rows;
		std::uint8_t cols;
};

struct EditorState {
		std::uint8_t slot;
		std::uint8_t cur_r;
		std::uint8_t cur_c;

		bool editing;
		char edit_buf[20];
		std::uint8_t edit_len;
};

struct SlotPickState {
		OperationId op;
		std::uint8_t stage;  // 0=pick first, 1=pick second (binary ops)
		std::uint8_t slot_a; // valid when stage==1 or after selection
		std::uint8_t cursor;
		std::uint8_t scroll;
};

struct ResultState {
		OperationId op;
		std::uint8_t slot_a;
		std::uint8_t slot_b;
		bool is_scalar;
		bool has_steps;

		std::uint8_t rows;
		std::uint8_t cols;
		std::uint8_t stride;
		const matrix_core::Rational* data;

		// for scalar results that depend on element selection (cofactor element)
		std::uint8_t i;
		std::uint8_t j;

		std::int64_t num;
		std::int64_t den;
};

enum class ConfirmAction : std::uint8_t {
		Resize,
		Clear,
};

struct ConfirmState {
		std::uint8_t slot;
		ConfirmAction action;
};

struct MessageState {
		bool active;
		std::uint16_t frames_left;
		char text[64];
};

struct StepsState {
		std::uint16_t index;
};

// state structs of the pages modules add (modules/*/shell/pages_state.inc)
#include "matrix_shell/program/page_states.inc"

// Pages modules add are listed in modules/*/shell/pages.def as
//
//   MATRIX_PAGE(Kind, StateType, member)
//
// giving PageKind::Kind, the union member `member`, and the App members
// render_<member>(const StateType&) and update_<member>(StateType&).
enum class PageKind : std::uint8_t {
		Menu,
		Dim,
		Editor,
		SlotPick,
		Result,
		Steps,
		Confirm,
#define MATRIX_PAGE(kind, state, member) kind,
#include "matrix_shell/program/pages.inc"
#undef MATRIX_PAGE
};

struct Page {
		PageKind kind;

		union {
				MenuState menu;
				DimState dim;
				EditorState editor;
				SlotPickState pick;
				ResultState result;
				StepsState steps;
				ConfirmState confirm;
#define MATRIX_PAGE(kind, state, member) state member;
#include "matrix_shell/program/pages.inc"
#undef MATRIX_PAGE
		} u;

		static Page make_menu(MenuId id) noexcept;
		static Page make_dim(std::uint8_t slot, std::uint8_t rows, std::uint8_t cols) noexcept;
		static Page make_editor(std::uint8_t slot) noexcept;
		static Page make_slot_pick(OperationId op) noexcept;
		static Page make_result_matrix(OperationId op,
		        std::uint8_t slot_a,
		        std::uint8_t slot_b,
		        bool has_steps,
		        std::uint8_t rows,
		        std::uint8_t cols,
		        std::uint8_t stride,
		        const matrix_core::Rational* data) noexcept;
		static Page make_result_scalar(
		        OperationId op, std::uint8_t slot_a, std::uint8_t slot_b, bool has_steps, std::int64_t num, std::int64_t den) noexcept;
		static Page make_steps() noexcept;
		static Page make_confirm(std::uint8_t slot, ConfirmAction action) noexcept;
};

struct Slot {
		matrix_core::MatrixMutView backing{}; // always 6x6 when allocated
		std::uint8_t rows = 0;                // active
		std::uint8_t cols = 0;                // active

		bool allocated() const noexcept { return backing.data != nullptr; }
		bool is_set() const noexcept { return allocated() && rows >= 1 && cols >= 1; }

		matrix_core::MatrixView view_active() const noexcept { return {rows, cols, backing.stride, backing.data}; }
		matrix_core::MatrixMutView view_active_mut() noexcept { return {rows, cols, backing.stride, backing.data}; }
};

class App {
	  public:
		bool init() noexcept;
		bool step() noexcept;

	  private:
		matrix_core::Slab slab_{};
		matrix_core::Arena persist_{};
		matrix_core::Arena scratch_{};
		std::size_t persist_base_mark_ = 0;
		std::size_t persist_tail_mark_ = 0;

		Slot slots_[kSlotCount]{};
		Page stack_[kMaxPageDepth]{};
		std::uint8_t depth_ = 0;

		Input input_{};
		MessageState msg_{};
		char fmt_buf_[64]{};
		char step_caption_[128]{};
		char step_latex_[1024]{};
		matrix_core::Explanation expl_{};

		TeX_Renderer* tex_renderer_ = nullptr;
		TeX_Layout* tex_layout_ = nullptr;
		std::uint16_t tex_cached_step_index_ = 0xFFFF;
		matrix_core::ErrorCode tex_cached_step_ec_ = matrix_core::ErrorCode::Internal;
		int tex_scroll_y_ = 0;
		int tex_total_height_ = 0;
		char tex_doc_[1400]{};

		Page& top() noexcept;
		const Page& top() const noexcept;
		[[nodiscard]] bool push(const Page& p) noexcept;
		[[nodiscard]] bool pop() noexcept;

		void push_root() noexcept;

		void render() noexcept;
		void render_header(const char* title) noexcept;
		void render_footer_hint(const char* hint) noexcept;
		void render_message() noexcept;

		void render_menu(const MenuState& s) noexcept;
		void render_dim(const DimState& s) noexcept;
		void render_editor(const EditorState& s) noexcept;
		void render_slot_pick(const SlotPickState& s) noexcept;
		void render_result(const ResultState& s) noexcept;
		void render_steps(const StepsState& s) noexcept;
		void render_confirm(const ConfirmState& s) noexcept;

		void update_message() noexcept;
		void show_message(const char* msg) noexcept;

		void update_menu(MenuState& s) noexcept;
		void update_dim(DimState& s) noexcept;
		void update_editor(EditorState& s) noexcept;
		void update_slot_pick(SlotPickState& s) noexcept;
		void update_result(ResultState& s) noexcept;
		void update_steps(StepsState& s) noexcept;
		void update_confirm(ConfirmState& s) noexcept;

		// runs `op` on the picked slots; the op's `run` member replaces the slot
		// picker with its result page, or leaves it up with a message
		void run_operation(OperationId op, std::uint8_t slot_a, std::uint8_t slot_b) noexcept;

		// A op B into a new matrix shown on a Result page: add, subtract and the
		// products. `product` picks the m x p * p x n shape over equal shapes.
		using BinaryMatrixOp = matrix_core::Error (*)(matrix_core::MatrixView,
		        matrix_core::MatrixView,
		        matrix_core::MatrixMutView,
		        matrix_core::Explanation*,
		        const matrix_core::ExplainOptions&) noexcept;
		void run_binary_matrix_op(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot, bool product, BinaryMatrixOp fn) noexcept;

		// shared pieces of Result pages, for the modules' result bodies
		void render_result_matrix(const ResultState& s) noexcept; // the matrix grid alone
		void render_result_grid(const ResultState& s, int grid_top) noexcept;
		void render_rank_line(const ResultState& s) noexcept;  // "rank=2 null=1 piv:1 3"
		bool open_result_steps(const ResultState& s) noexcept; // push the Steps page

		// what each module adds (modules/*/shell/app_members.inc)
#include "matrix_shell/program/app_members.inc"

		matrix_core::ErrorCode ensure_slot_allocated(std::uint8_t slot) noexcept;
		void clear_slot(std::uint8_t slot) noexcept;

		static bool parse_i64(const char* s, std::int64_t* out) noexcept;

		void steps_tex_reset() noexcept;
		void steps_tex_release() noexcept;
};

} // namespace matrix_shell
