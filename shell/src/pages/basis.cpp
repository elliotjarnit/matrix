#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>
#include <keypadc.h>

#include <utility>

#if MATRIX_SHELL_ENABLE_BASIS && MATRIX_CORE_ENABLE_BASIS

namespace matrix_shell {
namespace {
using detail::fail_fast;
using detail::kKbGroup1;
using detail::kKbGroup6;
using detail::kKbGroupArrows;
using detail::kScreenH;
using detail::kScreenW;
using detail::op_name;
using namespace matrix_shell::text_literals;

// summary, Col(A) basis, Nul(A) basis
constexpr std::uint8_t kBasisModeCount = 3;
constexpr int kLineH = 14;

void draw_basis_grid(char* fmt_buf, std::size_t fmt_cap, matrix_core::MatrixView m, int grid_top) noexcept {
		const ui::Layout l = ui::Layout{};
		const int max_cols = 6;
		const int max_rows = 6;
		const int cell_w = (kScreenW - 2 * l.margin_x) / max_cols;
		const int cell_h = (kScreenH - grid_top - l.footer_h - l.margin_y) / max_rows;

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

// "1 3" for the pivot columns, "x2 x4 x5" for the free variables: the set bits
// of `mask` (or the clear ones) among the first `n` columns
void write_columns(std::uint8_t mask, std::uint8_t n, bool pivots, char* buf, std::size_t cap) noexcept {
		buf[0] = '\0';
		matrix_core::CheckedWriter w{buf, cap};
		bool first = true;
		for (std::uint8_t c = 0; c < n; c++) {
				const bool is_pivot = (mask & (1u << c)) != 0;
				if (is_pivot != pivots)
						continue;
				if (!first)
						w.put(' ');
				if (!pivots)
						w.put('x');
				w.append_index1(c);
				first = false;
		}
		if (first)
				w.append("result.basis_none"_tx);
}

void print_u8(char* buf, std::size_t cap, std::uint8_t v) noexcept {
		buf[0] = '\0';
		matrix_core::CheckedWriter w{buf, cap};
		w.append_u64(v);
		gfx_PrintString(buf);
}
} // namespace

void App::run_basis(std::uint8_t slot) noexcept {
		if (slot >= kSlotCount || !slots_[slot].is_set())
				fail_fast("run_basis: slot not set");

		const matrix_core::MatrixView a = slots_[slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::MatrixMutView col{};
		matrix_core::MatrixMutView nul{};
		matrix_core::BasisInfo info{};
		const matrix_core::Error err =
		        matrix_core::op_basis(a, persist_, scratch_, &col, &nul, &info, &expl, opts_steps, detail::basis_labels());
		SHELL_DBG("[op] basis err=%u rank=%u nullity=%u\n", (unsigned)err.code, (unsigned)info.rank, (unsigned)info.nullity);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::Internal || err.code == matrix_core::ErrorCode::InvalidDimension)
						fail_fast("run_basis: op_basis returned unexpected error");
				show_message(err.code == matrix_core::ErrorCode::Overflow ? "common.out_of_memory"_tx : "common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (basis)");
		REQUIRE(push(Page::make_basis_result(slot,
		                expl_.available(),
		                info.rows,
		                info.cols,
		                info.rank,
		                info.nullity,
		                static_cast<std::uint8_t>(info.pivot_mask),
		                col.stride,
		                col.data,
		                nul.stride,
		                nul.data)),
		        "push basis result failed");
}

void App::render_basis_result(const BasisResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};

		render_header(op_name(OperationId::Basis));
		gfx_PrintString(" (");
		gfx_PrintChar(static_cast<char>('A' + s.slot_a));
		gfx_PrintString(")");
		render_footer_hint(s.has_steps ? "footer.basis_steps_back"_tx : "footer.basis_back"_tx);

		if (s.rows < 1 || s.cols < 1 || !s.col_data || !s.nul_data)
				fail_fast("render_basis_result: invalid state");
		if (s.mode >= kBasisModeCount)
				fail_fast("render_basis_result: invalid mode");

		gfx_SetTextFGColor(ui::color::kBlack);
		int y = l.header_h + 4;
		const auto line = [&]() noexcept {
				gfx_SetTextXY(l.margin_x, y);
				y += kLineH;
		};

		if (s.mode == 0) {
				line();
				gfx_PrintString("result.basis_col_space"_tx);
				print_u8(fmt_buf_, sizeof(fmt_buf_), s.rows);

				line();
				gfx_PrintString("result.basis_nul_space"_tx);
				print_u8(fmt_buf_, sizeof(fmt_buf_), s.cols);

				line();
				gfx_PrintString("result.basis_pivots"_tx);
				write_columns(s.pivot_mask, s.cols, true, fmt_buf_, sizeof(fmt_buf_));
				gfx_PrintString(fmt_buf_);

				line();
				gfx_PrintString("result.basis_free"_tx);
				write_columns(s.pivot_mask, s.cols, false, fmt_buf_, sizeof(fmt_buf_));
				gfx_PrintString(fmt_buf_);

				y += kLineH / 2;
				line();
				gfx_PrintString("result.basis_dim_col"_tx);
				print_u8(fmt_buf_, sizeof(fmt_buf_), s.rank);

				line();
				gfx_PrintString("result.basis_dim_nul"_tx);
				print_u8(fmt_buf_, sizeof(fmt_buf_), s.nullity);

				y += kLineH / 2;
				line();
				gfx_PrintString("result.basis_theorem"_tx);

				// "2 + 3 = 5 = n (checked)": the rank theorem with the numbers of A
				line();
				fmt_buf_[0] = '\0';
				{
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						w.append_u64(s.rank);
						w.append(" + ");
						w.append_u64(s.nullity);
						w.append(" = ");
						w.append_u64(static_cast<std::uint64_t>(s.rank) + s.nullity);
						w.append(" = n");
				}
				gfx_PrintString(fmt_buf_);
				gfx_PrintString((s.rank + s.nullity == s.cols) ? "result.basis_checked"_tx : "result.basis_mismatch"_tx);
				return;
		}

		const bool col_view = (s.mode == 1);
		const std::uint8_t count = col_view ? s.rank : s.nullity;

		line();
		gfx_PrintString(col_view ? "result.basis_col_label"_tx : "result.basis_nul_label"_tx);
		line();
		gfx_PrintString(col_view ? "result.basis_dim_col"_tx : "result.basis_dim_nul"_tx);
		print_u8(fmt_buf_, sizeof(fmt_buf_), count);

		if (count == 0) {
				line();
				gfx_PrintString(col_view ? "result.basis_zero_col"_tx : "result.basis_zero_nul"_tx);
				return;
		}

		// basis vectors are the columns of the grid
		const matrix_core::MatrixView m = col_view ? matrix_core::MatrixView{s.rows, s.rank, s.stride_col, s.col_data}
		                                           : matrix_core::MatrixView{s.cols, s.nullity, s.stride_nul, s.nul_data};
		if (m.stride < m.cols)
				fail_fast("render_basis_result: invalid stride");
		draw_basis_grid(fmt_buf_, sizeof(fmt_buf_), m, y + 2);
}

void App::update_basis_result(BasisResultState& s) noexcept {
		if (s.has_steps && input_.pressed(kKbGroup1, kb_2nd) && expl_.available()) {
				SHELL_DBG("[basis] 2ND steps\n");
				REQUIRE(push(Page::make_steps()), "push steps failed");
				return;
		}

		// cycle summary -> Col(A) basis -> Nul(A) basis
		if (input_.repeat(kKbGroupArrows, kb_Right)) {
				s.mode = static_cast<std::uint8_t>((s.mode + 1u) % kBasisModeCount);
				SHELL_DBG("[basis] mode=%u\n", (unsigned)s.mode);
				return;
		}
		if (input_.repeat(kKbGroupArrows, kb_Left)) {
				s.mode = static_cast<std::uint8_t>((s.mode + kBasisModeCount - 1u) % kBasisModeCount);
				SHELL_DBG("[basis] mode=%u\n", (unsigned)s.mode);
				return;
		}

		if (!input_.pressed(kKbGroup6, kb_Clear))
				return;

		SHELL_DBG("[basis] CLEAR back -> drop tail\n");
		REQUIRE(pop(), "pop failed");
		persist_.rewind(persist_base_mark_);
		persist_tail_mark_ = persist_base_mark_;
		expl_ = {};
}

} // namespace matrix_shell

#endif // MATRIX_SHELL_ENABLE_BASIS && MATRIX_CORE_ENABLE_BASIS
