#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_solve.hpp"
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

// "t1=x3 t2=x5": which variable each parameter stands for. Free variables are
// the non pivot columns, in the same ascending order as the basis vectors.
void render_free_map(std::uint32_t pivot_mask, std::uint8_t var_count, char* buf, std::size_t cap) noexcept {
		if (!buf || cap == 0)
				return;
		buf[0] = '\0';
		matrix_core::CheckedWriter w{buf, cap};
		std::uint8_t param = 0;
		for (std::uint8_t c = 0; c < var_count; c++) {
				if ((pivot_mask & (1u << c)) != 0)
						continue;
				if (param != 0)
						w.put(' ');
				w.put('t');
				w.append_u64(static_cast<std::uint64_t>(param) + 1u);
				w.append("=x");
				w.append_u64(static_cast<std::uint64_t>(c) + 1u);
				param++;
		}
}

// "x = xp + t1v1 + t2v2"
void render_param_equation(std::uint8_t param_count, bool has_particular, char* buf, std::size_t cap) noexcept {
		if (!buf || cap == 0)
				return;
		buf[0] = '\0';
		matrix_core::CheckedWriter w{buf, cap};
		w.append("x = ");
		if (has_particular)
				w.append("xp");
		for (std::uint8_t k = 0; k < param_count; k++) {
				if (has_particular || k != 0)
						w.append(" + ");
				w.put('t');
				w.append_u64(static_cast<std::uint64_t>(k) + 1u);
				w.put('v');
				w.append_u64(static_cast<std::uint64_t>(k) + 1u);
		}
}
} // namespace

void App::run_solve_rref(OperationId op, std::uint8_t a_slot, std::uint8_t b_slot) noexcept {
		const matrix_core::MatrixView a = slots_[a_slot].view_active();
		const matrix_core::MatrixView b = slots_[b_slot].view_active();
		if (b.cols != 1 || b.rows != a.rows) {
				show_message("msg.need_b_mx1"_tx);
				return;
		}
		if (a.cols + 1 > matrix_core::kMaxCols) {
				show_message("msg.too_many_cols"_tx);
				return;
		}

		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;
		matrix_core::Explanation expl;

		matrix_core::ParametricLabels labels{};
		labels.augmented = "solve.augmented"_tx;
		labels.rref = "solve.rref"_tx;
		labels.solve_pivots = "solve.solve_pivots"_tx;
		labels.free_vars = "solve.free"_tx;
		labels.homogeneous = "solve.homogeneous"_tx;
		labels.complete = "solve.complete"_tx;
		labels.inconsistent = "solve.inconsistent"_tx;
		labels.unique = "solve.unique"_tx;

		matrix_core::MatrixMutView xp{};
		matrix_core::MatrixMutView n_basis{};
		matrix_core::ParametricSolution info{};
		const matrix_core::Error err =
		        matrix_core::op_solve_parametric(a, b, persist_, scratch_, &xp, &n_basis, &info, &expl, opts_steps, labels);
		if (!matrix_core::is_ok(err)) {
				show_message(err.code == matrix_core::ErrorCode::Overflow ? "common.out_of_memory"_tx : "common.error"_tx);
				return;
		}

		const std::uint8_t n = info.var_count;

		// Pack the result as [x_p | v_1 .. v_k] so the grid shows the whole
		// parametric solution at once; drop x_p when there is no room for it
		// (that only happens when every variable is free, where x_p is zero).
		matrix_core::MatrixMutView out{};
		if (info.nullity == 0) {
				out = xp;
		} else if (info.nullity >= 6) {
				out = n_basis;
		} else {
				const std::uint8_t out_cols = static_cast<std::uint8_t>(info.nullity + 1u);
				const matrix_core::ErrorCode ec_o = matrix_core::matrix_alloc(persist_, n, out_cols, &out);
				if (!matrix_core::is_ok(ec_o)) {
						show_message("common.out_of_memory"_tx);
						return;
				}
				for (std::uint8_t r = 0; r < n; r++) {
						out.at_mut(r, 0) = xp.at(r, 0);
						for (std::uint8_t c = 0; c < info.nullity; c++)
								out.at_mut(r, static_cast<std::uint8_t>(c + 1u)) = n_basis.at(r, c);
				}
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (solve rref)");
		Page p = Page::make_result_matrix(op, a_slot, b_slot, expl_.available(), out.rows, out.cols, out.stride, out.data);
		p.u.result.i = info.rank;
		p.u.result.j = info.nullity;
		p.u.result.num = static_cast<std::int64_t>(info.pivot_mask);
		// `den` carries consistency for the result page: 0 means no solution
		p.u.result.den = info.consistent ? 1 : 0;
		REQUIRE(push(p), "push solve rref result failed");
}

void App::render_result_solve(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		if (!s.data || s.rows < 1 || s.cols < 1 || s.stride < s.cols)
				fail_fast("render_result_solve: invalid matrix result state");

		// an inconsistent system keeps its steps so the work can still be
		// written down; `den` carries consistency
		if (s.den == 0) {
				gfx_SetTextXY(l.margin_x, l.header_h + 30);
				gfx_PrintString("result.solve_no_solution"_tx);
				return;
		}

		render_rank_line(s);

		// the grid holds [x_p | v_1 .. v_k]; x_p is dropped only when every
		// variable is free, where it is the zero vector anyway
		gfx_SetTextXY(l.margin_x, l.header_h + 16);
		const bool has_xp = (s.cols == static_cast<std::uint8_t>(s.j + 1u));
		fmt_buf_[0] = '\0';
		render_param_equation(s.j, has_xp, fmt_buf_, sizeof(fmt_buf_));
		gfx_PrintString(fmt_buf_);
		if (s.j == 0)
				gfx_PrintString("result.solve_unique"_tx);
		int grid_top = l.header_h + 30;

		// name the variable each parameter stands for
		if (s.j > 0) {
				gfx_SetTextXY(l.margin_x, l.header_h + 28);
				gfx_PrintString("result.solve_params"_tx);
				fmt_buf_[0] = '\0';
				render_free_map(static_cast<std::uint32_t>(s.num), s.rows, fmt_buf_, sizeof(fmt_buf_));
				gfx_PrintString(fmt_buf_);
				grid_top = l.header_h + 42;
		}

		render_result_grid(s, grid_top);
}

} // namespace matrix_shell
