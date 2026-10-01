#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops.hpp"
#include "matrix_core/writer.hpp"
#include "matrix_core/spaces.hpp"

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
} // namespace

void App::run_space_basis(OperationId op, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;
		matrix_core::Explanation expl;

		matrix_core::MatrixView input = a;
		matrix_core::MatrixMutView at_owned{};
		if (op == OperationId::LeftNullSpaceBasis) {
				// Keep A^T alive for the step renderer.
				const matrix_core::ErrorCode ec_t = matrix_core::matrix_alloc(persist_, a.cols, a.rows, &at_owned);
				if (!matrix_core::is_ok(ec_t)) {
						show_message("common.out_of_memory"_tx);
						return;
				}
				const matrix_core::ErrorCode ec_tt = matrix_core::matrix_transpose(a, at_owned);
				if (!matrix_core::is_ok(ec_tt)) {
						show_message("common.error"_tx);
						return;
				}
				input = at_owned.view();
		}

		matrix_core::MatrixMutView rref{};
		const matrix_core::ErrorCode ec_r = matrix_core::matrix_alloc(scratch_, input.rows, input.cols, &rref);
		if (!matrix_core::is_ok(ec_r)) {
				show_message("common.out_of_memory"_tx);
				return;
		}

		const matrix_core::Error err = matrix_core::op_echelon(input, matrix_core::EchelonKind::Rref, rref, &expl, opts_steps);
		if (!matrix_core::is_ok(err)) {
				show_message("common.error"_tx);
				return;
		}

		matrix_core::SpaceInfo info{};
		const matrix_core::ErrorCode ec_info = matrix_core::space_info_from_rref(rref.view(), input.cols, &info);
		if (!matrix_core::is_ok(ec_info)) {
				show_message("common.error"_tx);
				return;
		}
		const std::uint32_t piv_mask = info.pivot_mask;
		const std::uint8_t nullity = info.nullity;

		matrix_core::MatrixMutView out{};
		if (op == OperationId::ColSpaceBasis) {
				const matrix_core::ErrorCode ec_o = matrix_core::space_col_basis(a, info, persist_, &out);
				if (!matrix_core::is_ok(ec_o)) {
						show_message("common.out_of_memory"_tx);
						return;
				}
		} else if (op == OperationId::RowSpaceBasis) {
				const matrix_core::ErrorCode ec_o = matrix_core::space_row_basis(rref.view(), input.cols, info, persist_, &out);
				if (!matrix_core::is_ok(ec_o)) {
						show_message("common.out_of_memory"_tx);
						return;
				}
		} else {
				// Null(A) or Null(A^T)
				const std::uint8_t var_cols = input.cols;
				const matrix_core::ErrorCode ec_o = matrix_core::space_null_basis(rref.view(), var_cols, info, persist_, &out);
				if (!matrix_core::is_ok(ec_o)) {
						show_message("common.out_of_memory"_tx);
						return;
				}
		}

		tx.commit();
		expl_ = std::move(expl);
		Page p = Page::make_result_matrix(op, slot, 0, expl_.available(), out.rows, out.cols, out.stride, out.data);
		p.u.result.i = info.rank;  // rank
		p.u.result.j = nullity;    // nullity (in the working variable space)
		p.u.result.num = piv_mask; // pivot mask (LSB = col 1)
		p.u.result.den = 1;
		REQUIRE(pop(), "pop failed (spaces)");
		REQUIRE(push(p), "push spaces result failed");
}

void App::render_result_space_basis(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		if (!s.data || s.rows < 1 || s.cols < 1 || s.stride < s.cols)
				fail_fast("render_result_space_basis: invalid matrix result state");

		render_rank_line(s);
		gfx_SetTextXY(l.margin_x, l.header_h + 16);
		gfx_PrintString((s.op == OperationId::RowSpaceBasis) ? "result.rows_basis"_tx : "result.cols_basis"_tx);
		render_result_grid(s, l.header_h + 30);
}

} // namespace matrix_shell
