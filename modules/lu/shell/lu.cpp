#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_lu.hpp"
#include "matrix_core/spaces.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>
#include <keypadc.h>

#include <utility>

namespace matrix_shell {
namespace {
using detail::fail_fast;
using detail::kKbGroup1;
using detail::kKbGroup6;
using detail::kKbGroupArrows;
using detail::kScreenH;
using detail::kScreenW;
using namespace matrix_shell::text_literals;

// L, U and, when row swaps were used, P
std::uint8_t lu_mode_count(const LuResultState& s) noexcept {
		return (s.p_data != nullptr) ? 3u : 2u;
}

matrix_core::LuLabels lu_labels() noexcept {
		matrix_core::LuLabels labels{};
		labels.skip_column = "lu.skip_column"_tx;
		labels.check = "lu.check"_tx;
		labels.forward = "lu.forward"_tx;
		labels.back = "lu.back"_tx;
		labels.inconsistent = "lu.inconsistent"_tx;
		labels.particular = "lu.particular"_tx;
		labels.homogeneous = "lu.homogeneous"_tx;
		labels.summary = "lu.summary"_tx;
		labels.swap_rows = "lu.swap_rows"_tx;
		return labels;
}

Page make_lu_result(std::uint8_t slot_a,
        bool has_steps,
        std::uint8_t rows,
        std::uint8_t u_cols,
        std::uint8_t stride_l,
        const matrix_core::Rational* l_data,
        std::uint8_t stride_u,
        const matrix_core::Rational* u_data,
        std::uint8_t stride_p,
        const matrix_core::Rational* p_data) noexcept {
		Page p{};
		p.kind = PageKind::LuResult;
		p.u.lu_result = LuResultState{
		        .slot_a = slot_a,
		        .mode = 0,
		        .has_steps = has_steps,
		        .rows = rows,
		        .u_cols = u_cols,
		        .stride_l = stride_l,
		        .stride_u = stride_u,
		        .stride_p = stride_p,
		        .l_data = l_data,
		        .u_data = u_data,
		        .p_data = p_data,
		};
		return p;
}

Page make_lu_confirm(std::uint8_t slot_a, std::uint8_t slot_b, std::uint8_t col, bool solve) noexcept {
		Page p{};
		p.kind = PageKind::LuPivotConfirm;
		p.u.lu_confirm = LuPivotConfirmState{slot_a, slot_b, col, solve};
		return p;
}

void draw_matrix_grid(char* fmt_buf, std::size_t fmt_cap, matrix_core::MatrixView m, int grid_top) noexcept {
		const ui::Layout l = ui::Layout{};
		const int max_cols = 6;
		const int max_rows = 6;
		const int cell_w = (kScreenW - 2 * l.margin_x) / max_cols;
		const int cell_h = (kScreenH - l.header_h - l.footer_h - 2 * l.margin_y) / max_rows;

		for (std::uint8_t r = 0; r < m.rows; r++) {
				for (std::uint8_t c = 0; c < m.cols; c++) {
						const int x = l.margin_x + c * cell_w;
						const int y = grid_top + r * cell_h;

						gfx_SetColor(ui::color::kLightGray);
						gfx_FillRectangle(x, y, cell_w - 1, cell_h - 1);
						gfx_SetColor(ui::color::kDarkGray);
						gfx_Rectangle(x, y, cell_w - 1, cell_h - 1);

						const matrix_core::Rational& v = m.at(r, c);
						fmt_buf[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf, fmt_cap};
						w.append_i64(v.num());
						if (v.den() != 1) {
								w.put('/');
								w.append_i64(v.den());
						}

						gfx_SetTextFGColor(ui::color::kBlack);
						gfx_SetTextXY(x + 2, y + 3);
						gfx_PrintString(fmt_buf);
				}
		}
}
} // namespace

void App::render_lu_result(const LuResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};

		render_header((s.p_data != nullptr) ? "result.lu_pa_title"_tx : "result.lu_title"_tx);
		if (s.has_steps)
				render_footer_hint("footer.lu_steps_back"_tx);
		else
				render_footer_hint("footer.lu_back"_tx);

		if (s.rows < 1 || s.u_cols < 1)
				fail_fast("render_lu_result: invalid dims");
		if (s.mode >= lu_mode_count(s))
				fail_fast("render_lu_result: invalid mode");

		// L and P are rows x rows, U is rows x u_cols
		matrix_core::MatrixView m{};
		const char* label = nullptr;
		if (s.mode == 0) {
				m = matrix_core::MatrixView{s.rows, s.rows, s.stride_l, s.l_data};
				label = "result.lu_lower"_tx;
		} else if (s.mode == 1) {
				m = matrix_core::MatrixView{s.rows, s.u_cols, s.stride_u, s.u_data};
				label = "result.lu_upper"_tx;
		} else {
				m = matrix_core::MatrixView{s.rows, s.rows, s.stride_p, s.p_data};
				label = "result.lu_perm"_tx;
		}

		if (!m.data)
				fail_fast("render_lu_result: missing factor data");
		if (m.stride < m.cols)
				fail_fast("render_lu_result: invalid stride");

		gfx_SetTextFGColor(ui::color::kBlack);
		gfx_SetTextXY(l.margin_x, l.header_h + 4);
		gfx_PrintString(label);
		gfx_PrintString("result.lu_of"_tx);
		gfx_PrintChar(static_cast<char>('A' + s.slot_a));

		draw_matrix_grid(fmt_buf_, sizeof(fmt_buf_), m, l.header_h + 20);
}

void App::update_lu_result(LuResultState& s) noexcept {
		if (s.has_steps && input_.pressed(kKbGroup1, kb_2nd) && expl_.available()) {
				SHELL_DBG("[lu] 2ND steps\n");
				REQUIRE(push(Page::make_steps()), "push steps failed");
				return;
		}

		// cycle through the factors
		const std::uint8_t count = lu_mode_count(s);
		if (input_.repeat(kKbGroupArrows, kb_Right)) {
				s.mode = static_cast<std::uint8_t>((s.mode + 1u) % count);
				SHELL_DBG("[lu] mode=%u\n", (unsigned)s.mode);
				return;
		}
		if (input_.repeat(kKbGroupArrows, kb_Left)) {
				s.mode = static_cast<std::uint8_t>((s.mode + count - 1u) % count);
				SHELL_DBG("[lu] mode=%u\n", (unsigned)s.mode);
				return;
		}

		if (!input_.pressed(kKbGroup6, kb_Clear))
				return;

		SHELL_DBG("[lu] CLEAR back -> drop tail\n");
		REQUIRE(pop(), "pop failed");
		persist_.rewind(persist_base_mark_);
		persist_tail_mark_ = persist_base_mark_;
		expl_ = {};
}

namespace {
// the errors both LU entry points can surface; anything else means a bug here
[[nodiscard]] const char* lu_error_message(matrix_core::ErrorCode code) noexcept {
		if (code == matrix_core::ErrorCode::Overflow)
				return "common.out_of_memory"_tx;
		if (code == matrix_core::ErrorCode::DimensionMismatch || code == matrix_core::ErrorCode::InvalidDimension ||
		        code == matrix_core::ErrorCode::Internal)
				fail_fast("LU returned unexpected error");
		return "common.error"_tx;
}
} // namespace

void App::run_lu_factor(std::uint8_t slot, bool allow_swaps) noexcept {
		if (slot >= kSlotCount || !slots_[slot].is_set())
				fail_fast("run_lu_factor: slot not set");

		const matrix_core::MatrixView a = slots_[slot].view_active();

		// drop any previous ephemeral result before allocating this one
		persist_.rewind(persist_base_mark_);
		persist_tail_mark_ = persist_base_mark_;
		expl_ = {};

		matrix_core::ArenaScope tx(persist_);

		// L and P are m x m, U has the shape of A
		matrix_core::MatrixMutView l_out{};
		matrix_core::MatrixMutView u_out{};
		matrix_core::MatrixMutView p_out{};
		matrix_core::ErrorCode ec = matrix_core::matrix_alloc(persist_, a.rows, a.rows, &l_out);
		if (matrix_core::is_ok(ec))
				ec = matrix_core::matrix_alloc(persist_, a.rows, a.cols, &u_out);
		if (matrix_core::is_ok(ec) && allow_swaps)
				ec = matrix_core::matrix_alloc(persist_, a.rows, a.rows, &p_out);
		if (!matrix_core::is_ok(ec)) {
				SHELL_DBG(
				        "[op] lu alloc failed ec=%u used=%u/%u\n", (unsigned)ec, (unsigned)persist_.used(), (unsigned)persist_.capacity());
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		const matrix_core::LuLabels labels = lu_labels();
		const matrix_core::LuPivoting pivoting = allow_swaps ? matrix_core::LuPivoting::Partial : matrix_core::LuPivoting::None;

		matrix_core::Explanation expl;
		const matrix_core::Error err = matrix_core::op_lu_factor(a, pivoting, p_out, l_out, u_out, &labels, &expl, opts_steps);
		SHELL_DBG("[op] lu factor err=%u swaps=%u\n", (unsigned)err.code, (unsigned)allow_swaps);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::RowSwapRequired) {
						// offer PA = LU instead of stopping
						REQUIRE(push(make_lu_confirm(slot, 0, err.j, false)), "push lu pivot confirm failed");
						return;
				}
				show_message(lu_error_message(err.code));
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (lu factor)");
		REQUIRE(push(make_lu_result(slot,
		                expl_.available(),
		                u_out.rows,
		                u_out.cols,
		                l_out.stride,
		                l_out.data,
		                u_out.stride,
		                u_out.data,
		                p_out.stride,
		                p_out.data)),
		        "push lu result failed");
}

void App::run_lu_solve(std::uint8_t slot_a, std::uint8_t slot_b, bool allow_swaps) noexcept {
		if (slot_a >= kSlotCount || slot_b >= kSlotCount)
				fail_fast("run_lu_solve: slot out of range");
		if (!slots_[slot_a].is_set() || !slots_[slot_b].is_set())
				fail_fast("run_lu_solve: slot not set");

		const matrix_core::MatrixView a = slots_[slot_a].view_active();
		const matrix_core::MatrixView b = slots_[slot_b].view_active();

		if (b.cols != 1 || b.rows != a.rows) {
				show_message("msg.need_b_mx1"_tx);
				return;
		}
		if (a.cols + 1 > matrix_core::kMaxCols) {
				show_message("msg.too_many_cols"_tx);
				return;
		}

		persist_.rewind(persist_base_mark_);
		persist_tail_mark_ = persist_base_mark_;
		expl_ = {};

		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		const std::uint8_t m = a.rows;
		const std::uint8_t n = a.cols;

		// y, x and RREF([U | y]) live in persist: the result page and the null
		// space basis below both read them after the op returns
		matrix_core::MatrixMutView y{};
		matrix_core::MatrixMutView x{};
		matrix_core::MatrixMutView rref{};
		matrix_core::ErrorCode ec = matrix_core::matrix_alloc(persist_, m, 1, &y);
		if (matrix_core::is_ok(ec))
				ec = matrix_core::matrix_alloc(persist_, n, 1, &x);
		if (matrix_core::is_ok(ec))
				ec = matrix_core::matrix_alloc(persist_, m, static_cast<std::uint8_t>(n + 1u), &rref);
		if (!matrix_core::is_ok(ec)) {
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		const matrix_core::LuLabels labels = lu_labels();
		const matrix_core::LuPivoting pivoting = allow_swaps ? matrix_core::LuPivoting::Partial : matrix_core::LuPivoting::None;

		matrix_core::LuSolveInfo info{};
		matrix_core::Explanation expl;
		const matrix_core::Error err = matrix_core::op_lu_solve(a, b, pivoting, scratch_, y, x, rref, &info, &labels, &expl, opts_steps);
		SHELL_DBG("[op] lu solve err=%u unique=%u consistent=%u rank=%u\n",
		        (unsigned)err.code,
		        (unsigned)info.unique,
		        (unsigned)info.consistent,
		        (unsigned)info.rank);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::RowSwapRequired) {
						REQUIRE(push(make_lu_confirm(slot_a, slot_b, err.j, true)), "push lu pivot confirm failed");
						return;
				}
				show_message(lu_error_message(err.code));
				return;
		}

		matrix_core::SpaceInfo space{};
		const matrix_core::ErrorCode ec_info = matrix_core::space_info_from_rref(rref.view(), n, &space);
		if (!matrix_core::is_ok(ec_info)) {
				show_message("common.error"_tx);
				return;
		}

		// same packing as Solve (RREF): x_p first, then a basis for Null(A)
		matrix_core::MatrixMutView out = x;
		if (info.consistent && info.nullity != 0) {
				matrix_core::MatrixMutView n_basis{};
				const matrix_core::ErrorCode ec_nb = matrix_core::space_null_basis(rref.view(), n, space, persist_, &n_basis);
				if (!matrix_core::is_ok(ec_nb)) {
						show_message("common.out_of_memory"_tx);
						return;
				}

				if (n_basis.cols >= 6) {
						out = n_basis; // fully free, x_p is zero
				} else {
						const std::uint8_t out_cols = static_cast<std::uint8_t>(n_basis.cols + 1u);
						matrix_core::MatrixMutView packed{};
						const matrix_core::ErrorCode ec_o = matrix_core::matrix_alloc(persist_, n, out_cols, &packed);
						if (!matrix_core::is_ok(ec_o)) {
								show_message("common.out_of_memory"_tx);
								return;
						}
						for (std::uint8_t r = 0; r < n; r++) {
								packed.at_mut(r, 0) = x.at(r, 0);
								for (std::uint8_t c = 0; c < n_basis.cols; c++)
										packed.at_mut(r, static_cast<std::uint8_t>(c + 1u)) = n_basis.at(r, c);
						}
						out = packed;
				}
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (lu solve)");
		Page p =
		        Page::make_result_matrix(OperationId::LuSolve, slot_a, slot_b, expl_.available(), out.rows, out.cols, out.stride, out.data);
		p.u.result.i = info.rank;
		p.u.result.j = info.nullity;
		p.u.result.num = static_cast<std::int64_t>(space.pivot_mask);
		// `den` carries consistency for the result page: 0 means no solution
		p.u.result.den = info.consistent ? 1 : 0;
		REQUIRE(push(p), "push lu solve result failed");
}

void App::run_lu_factor_op(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
		run_lu_factor(slot, false);
}

void App::run_lu_solve_op(OperationId, std::uint8_t a_slot, std::uint8_t b_slot) noexcept {
		run_lu_solve(a_slot, b_slot, false);
}

void App::render_result_lu_solve(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		if (!s.data || s.rows < 1 || s.cols < 1 || s.stride < s.cols)
				fail_fast("render_result_lu_solve: invalid matrix result state");

		// `den` carries consistency: 0 means the system has no solution
		if (s.den == 0) {
				gfx_SetTextXY(l.margin_x, l.header_h + 30);
				gfx_PrintString("result.lu_no_solution"_tx);
				return;
		}

		render_rank_line(s);
		gfx_SetTextXY(l.margin_x, l.header_h + 16);
		if (s.cols == 1)
				gfx_PrintString("result.solution_vector"_tx);
		else
				gfx_PrintString("result.param_solution"_tx);
		render_result_grid(s, l.header_h + 30);
}

// A zero pivot with a nonzero entry below it: offer PA = LU instead of stopping.
void App::render_lu_confirm(const LuPivotConfirmState& s) noexcept {
		render_header("common.confirm_title"_tx);
		render_footer_hint("footer.yes_no"_tx);

		if (s.slot_a >= kSlotCount)
				fail_fast("render_lu_confirm: slot out of range");

		gfx_SetTextFGColor(ui::color::kBlack);

		// say why first, then offer the permuted factorization
		gfx_SetTextXY(12, 52);
		gfx_PrintString("msg.lu_row_swap_prefix"_tx);
		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
		w.append_u64(static_cast<std::uint64_t>(s.col) + 1u);
		gfx_PrintString(fmt_buf_);

		gfx_SetTextXY(12, 70);
		gfx_PrintString("confirm.lu_pivot_prompt"_tx);
}

void App::update_lu_confirm(LuPivotConfirmState& s) noexcept {
		// CLEAR: no
		if (input_.pressed(kKbGroup6, kb_Clear)) {
				SHELL_DBG("[confirm] CLEAR cancel lu pivot slot=%c\n", (char)('A' + s.slot_a));
				REQUIRE(pop(), "pop failed (cancel)");
				return;
		}

		// ENTER: yes
		if (!input_.pressed(kKbGroup6, kb_Enter))
				return;

		if (s.slot_a >= kSlotCount)
				fail_fast("update_lu_confirm: slot out of range");

		// the confirmation is popped first so the LU run replaces the slot
		// picker underneath it, exactly as a direct run would
		const std::uint8_t slot_a = s.slot_a;
		const std::uint8_t slot_b = s.slot_b;
		const bool solve = s.solve;
		REQUIRE(pop(), "pop failed (lu pivot)");
		if (solve) {
				SHELL_DBG("[confirm] ENTER lu pivot solve a=%c b=%c\n", (char)('A' + slot_a), (char)('A' + slot_b));
				run_lu_solve(slot_a, slot_b, true);
		} else {
				SHELL_DBG("[confirm] ENTER lu pivot factor slot=%c\n", (char)('A' + slot_a));
				run_lu_factor(slot_a, true);
		}
}

} // namespace matrix_shell
