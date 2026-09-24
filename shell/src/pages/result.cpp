#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops.hpp"
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
using detail::op_name;
using namespace matrix_shell::text_literals;

void render_pivot_cols(std::uint32_t mask, char* buf, std::size_t cap) noexcept {
		if (!buf || cap == 0)
				return;
		buf[0] = '\0';
		matrix_core::CheckedWriter w{buf, cap};
		bool first = true;
		for (std::uint8_t i = 0; i < 7; i++) {
				if ((mask & (1u << i)) == 0)
						continue;
				if (!first)
						w.put(' ');
				w.append_u64(static_cast<std::uint64_t>(i + 1u));
				first = false;
		}
		if (first)
				w.append("-");
}

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

// "v3 = 3v1 + v2": the dependent vector `dep_col` written over the pivot
// vectors, with the coefficients read out of the RREF. Overflow truncates: the
// steps page carries the full relation.
void render_relation(const matrix_core::Rational* rref,
        std::uint8_t stride,
        std::uint8_t rows,
        std::uint32_t pivot_mask,
        std::uint8_t dep_col,
        char* buf,
        std::size_t cap) noexcept {
		if (!buf || cap == 0 || !rref)
				return;
		buf[0] = '\0';
		matrix_core::Writer w{buf, cap, 0};
		(void)w.put('v');
		(void)w.append_index1(dep_col);
		(void)w.append(" = ");

		bool wrote = false;
		std::uint8_t row = 0;
		for (std::uint8_t pc = 0; pc < 7 && row < rows; pc++) {
				if ((pivot_mask & (1u << pc)) == 0)
						continue;

				// in an RREF the k-th pivot column carries its 1 in the k-th row
				const matrix_core::Rational& c = rref[static_cast<std::size_t>(row) * stride + dep_col];
				row++;
				if (c.is_zero())
						continue;

				const bool neg = c.num() < 0;
				if (neg)
						(void)w.append(wrote ? " - " : "-");
				else if (wrote)
						(void)w.append(" + ");

				// magnitude without negating, which would be UB at INT64_MIN
				const std::uint64_t mag = neg ? (static_cast<std::uint64_t>(-(c.num() + 1)) + 1u) : static_cast<std::uint64_t>(c.num());
				if (!(mag == 1u && c.den() == 1)) {
						(void)w.append_u64(mag);
						if (c.den() != 1) {
								(void)w.put('/');
								(void)w.append_i64(c.den());
						}
				}
				(void)w.put('v');
				(void)w.append_index1(pc);
				wrote = true;
		}

		if (!wrote)
				(void)w.put('0');
}
} // namespace

void App::render_result(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};

		render_header(nullptr);
		gfx_PrintString(op_name(s.op));
		gfx_PrintString("result.suffix"_tx);
		if (s.has_steps)
				render_footer_hint("footer.result_steps_back"_tx);
		else
				render_footer_hint("footer.clear_back"_tx);

		gfx_SetTextFGColor(ui::color::kBlack);

		if (s.is_scalar) {
				if (s.op == OperationId::SpanTest || s.op == OperationId::IndepTest) {
						const int y0 = l.header_h + 26;
						gfx_SetTextXY(l.margin_x, y0);
						gfx_PrintString((s.op == OperationId::SpanTest) ? "result.span_label"_tx : "result.indep_label"_tx);
						gfx_PrintString((s.num != 0) ? "result.bool_yes"_tx : "result.bool_no"_tx);

						gfx_SetTextXY(l.margin_x, y0 + 14);
						gfx_PrintString("result.rank_eq"_tx);
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						w.append_u64(static_cast<std::uint64_t>(s.i));
						gfx_PrintString(fmt_buf_);

						gfx_PrintString("result.m_eq"_tx);
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w2{fmt_buf_, sizeof(fmt_buf_)};
						w2.append_u64(static_cast<std::uint64_t>(s.rows));
						gfx_PrintString(fmt_buf_);
						gfx_PrintString("result.n_eq"_tx);
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w3{fmt_buf_, sizeof(fmt_buf_)};
						w3.append_u64(static_cast<std::uint64_t>(s.cols));
						gfx_PrintString(fmt_buf_);

						gfx_SetTextXY(l.margin_x, y0 + 28);
						gfx_PrintString("result.piv_eq"_tx);
						render_pivot_cols(static_cast<std::uint32_t>(s.den), fmt_buf_, sizeof(fmt_buf_));
						gfx_PrintString(fmt_buf_);

						// for a dependent set, show the first vector as a combination of the
						// pivot vectors: the answer the review exercises ask to be written out
						if (s.op == OperationId::IndepTest && s.num == 0 && s.j != 0xFFu && s.data) {
								gfx_SetTextXY(l.margin_x, y0 + 42);
								fmt_buf_[0] = '\0';
								render_relation(
								        s.data, s.stride, s.rows, static_cast<std::uint32_t>(s.den), s.j, fmt_buf_, sizeof(fmt_buf_));
								gfx_PrintString(fmt_buf_);
						}
						return;
				}

				if (s.op == OperationId::Det) {
						gfx_SetTextXY(l.margin_x, l.header_h + 30);
						gfx_PrintString("det(");
						gfx_PrintChar(static_cast<char>('A' + s.slot_a));
						gfx_PrintString(") = ");
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						if (s.den == 1) {
								w.append_i64(s.num);
						} else {
								w.append_i64(s.num);
								w.put('/');
								w.append_i64(s.den);
						}
						gfx_PrintString(fmt_buf_);
						return;
				}

				if (s.op == OperationId::Dot) {
						gfx_SetTextXY(l.margin_x, l.header_h + 30);
						gfx_PrintString("dot(");
						gfx_PrintChar(static_cast<char>('A' + s.slot_a));
						gfx_PrintString(", ");
						gfx_PrintChar(static_cast<char>('A' + s.slot_b));
						gfx_PrintString(") = ");

						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						if (s.den == 1)
								w.append_i64(s.num);
						else {
								w.append_i64(s.num);
								w.put('/');
								w.append_i64(s.den);
						}
						gfx_PrintString(fmt_buf_);
						return;
				}

#if MATRIX_SHELL_ENABLE_COFACTOR
				if (s.op == OperationId::CofactorElement) {
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
						return;
				}
#endif

				// scalar result op not handled should not happen
				SHELL_DBG("[result] unsupported scalar op=%s num=%lld den=%lld\n", op_name(s.op), (long long)s.num, (long long)s.den);
				gfx_SetTextXY(l.margin_x, l.header_h + 30);
				gfx_PrintString("result.internal_scalar_error"_tx);
				return;
		}

		if (!s.data || s.rows < 1 || s.cols < 1 || s.stride < s.cols)
				fail_fast("render_result: invalid matrix result state");

#if MATRIX_SHELL_ENABLE_LU && MATRIX_CORE_ENABLE_LU
		// LU solve stores consistency in `den`: 0 means the system has no solution
		if (s.op == OperationId::LuSolve && s.den == 0) {
				gfx_SetTextXY(l.margin_x, l.header_h + 30);
				gfx_PrintString("result.lu_no_solution"_tx);
				return;
		}
#endif

		// the RREF solve reports an inconsistent system the same way, and keeps
		// its steps so the work can still be written down
		if (s.op == OperationId::SolveRref && s.den == 0) {
				gfx_SetTextXY(l.margin_x, l.header_h + 30);
				gfx_PrintString("result.solve_no_solution"_tx);
				return;
		}

		int grid_top = l.header_h + 8;
		if (s.op == OperationId::SolveRref || s.op == OperationId::LuSolve || s.op == OperationId::ColSpaceBasis ||
		        s.op == OperationId::RowSpaceBasis || s.op == OperationId::NullSpaceBasis || s.op == OperationId::LeftNullSpaceBasis) {
				gfx_SetTextXY(l.margin_x, l.header_h + 4);
				gfx_PrintString("result.rank_eq"_tx);
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append_u64(static_cast<std::uint64_t>(s.i));
				gfx_PrintString(fmt_buf_);

				gfx_PrintString("result.null_eq"_tx);
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w2{fmt_buf_, sizeof(fmt_buf_)};
				w2.append_u64(static_cast<std::uint64_t>(s.j));
				gfx_PrintString(fmt_buf_);

				gfx_PrintString("result.piv_compact_eq"_tx);
				render_pivot_cols(static_cast<std::uint32_t>(s.num), fmt_buf_, sizeof(fmt_buf_));
				gfx_PrintString(fmt_buf_);

				gfx_SetTextXY(l.margin_x, l.header_h + 16);
				if (s.op == OperationId::RowSpaceBasis) {
						gfx_PrintString("result.rows_basis"_tx);
				} else if (s.op == OperationId::SolveRref) {
						// the grid holds [x_p | v_1 .. v_k]; x_p is dropped only when every
						// variable is free, where it is the zero vector anyway
						const bool has_xp = (s.cols == static_cast<std::uint8_t>(s.j + 1u));
						fmt_buf_[0] = '\0';
						render_param_equation(s.j, has_xp, fmt_buf_, sizeof(fmt_buf_));
						gfx_PrintString(fmt_buf_);
						if (s.j == 0)
								gfx_PrintString("result.solve_unique"_tx);
				} else if (s.op == OperationId::LuSolve) {
						if (s.cols == 1) {
								gfx_PrintString("result.solution_vector"_tx);
						} else {
								gfx_PrintString("result.param_solution"_tx);
						}
				} else {
						gfx_PrintString("result.cols_basis"_tx);
				}
				grid_top = l.header_h + 30;

				// name the variable each parameter stands for
				if (s.op == OperationId::SolveRref && s.j > 0) {
						gfx_SetTextXY(l.margin_x, l.header_h + 28);
						gfx_PrintString("result.solve_params"_tx);
						fmt_buf_[0] = '\0';
						render_free_map(static_cast<std::uint32_t>(s.num), s.rows, fmt_buf_, sizeof(fmt_buf_));
						gfx_PrintString(fmt_buf_);
						grid_top = l.header_h + 42;
				}
		}

		const int grid_left = l.margin_x;
		const int max_cols = 6;
		const int max_rows = 6;

		const int cell_w = (kScreenW - 2 * l.margin_x) / max_cols;
		const int cell_h = (kScreenH - grid_top - l.footer_h - l.margin_y) / max_rows;

		const std::uint8_t rows = s.rows;
		const std::uint8_t cols = s.cols;

		for (std::uint8_t r = 0; r < rows; r++) {
				for (std::uint8_t c = 0; c < cols; c++) {
						const int x = grid_left + c * cell_w;
						const int y = grid_top + r * cell_h;

						gfx_SetColor(ui::color::kLightGray);
						gfx_FillRectangle(x, y, cell_w - 1, cell_h - 1);
						gfx_SetColor(ui::color::kDarkGray);
						gfx_Rectangle(x, y, cell_w - 1, cell_h - 1);

						const matrix_core::Rational& v = s.data[static_cast<std::size_t>(r) * s.stride + static_cast<std::size_t>(c)];
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						if (v.den() == 1) {
								w.append_i64(v.num());
						} else {
								w.append_i64(v.num());
								w.put('/');
								w.append_i64(v.den());
						}

						gfx_SetTextFGColor(ui::color::kBlack);
						gfx_SetTextXY(x + 2, y + 3);
						gfx_PrintString(fmt_buf_);
				}
		}
}

void App::update_result(ResultState& s) noexcept {
		// steps (cramer uses a selector menu, others go straight to pager)
#if MATRIX_SHELL_ENABLE_CRAMER
		if (s.op == OperationId::Cramer && s.has_steps && input_.pressed(kKbGroup1, kb_2nd)) {
				SHELL_DBG("[result] 2ND cramer steps menu\n");
				REQUIRE(push(Page::make_cramer_steps_menu(s.slot_a, s.slot_b, s.rows)), "push cramer steps menu failed");
				return;
		}
#endif
		if (s.has_steps && input_.pressed(kKbGroup1, kb_2nd) && expl_.available()) {
				SHELL_DBG("[result] 2ND steps\n");
				REQUIRE(push(Page::make_steps()), "push steps failed");
				return;
		}

		if (!input_.pressed(kKbGroup6, kb_Clear))
				return;
		SHELL_DBG("[result] CLEAR back -> drop tail\n");
		REQUIRE(pop(), "pop failed");
		// drop ephemeral result buffers
		persist_.rewind(persist_base_mark_);
		persist_tail_mark_ = persist_base_mark_;
		expl_ = {};
}

void App::render_projection_result(const ProjectionResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};

		render_header("result.projection_title"_tx);
		if (s.has_steps)
				render_footer_hint("footer.projection_steps_back"_tx);
		else
				render_footer_hint("footer.projection_back"_tx);

		const bool show_proj = (s.mode == 0);
		if (s.rows < 1 || s.cols < 1)
				fail_fast("render_projection_result: invalid dims");
		if (show_proj ? (s.proj_data == nullptr) : (s.orth_data == nullptr))
				fail_fast("render_projection_result: missing vector data");
		const matrix_core::MatrixView vec = show_proj ? matrix_core::MatrixView{s.rows, s.cols, s.stride_proj, s.proj_data}
		                                              : matrix_core::MatrixView{s.rows, s.cols, s.stride_orth, s.orth_data};
		if (vec.stride < vec.cols)
				fail_fast("render_projection_result: invalid stride");

		gfx_SetTextFGColor(ui::color::kBlack);

		// info line
		gfx_SetTextXY(l.margin_x, l.header_h + 4);
		gfx_PrintString(show_proj ? "result.proj_label"_tx : "result.orth_label"_tx);
		gfx_PrintString("result.proj_u_prefix"_tx);
		gfx_PrintChar(static_cast<char>('A' + s.slot_u));
		gfx_PrintString("result.proj_v_prefix"_tx);
		gfx_PrintChar(static_cast<char>('A' + s.slot_v));
		gfx_PrintString(")");

		// k line
		gfx_SetTextXY(l.margin_x, l.header_h + 18);
		gfx_PrintString("result.k_label"_tx);
		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				if (s.k_den == 1) {
						w.append_i64(s.k_num);
				} else {
						w.append_i64(s.k_num);
						w.put('/');
						w.append_i64(s.k_den);
				}
		}
		gfx_PrintString(fmt_buf_);

		// vector grid
		const int grid_top = l.header_h + 34;
		const int grid_left = l.margin_x;
		const int max_cols = 6;
		const int max_rows = 6;

		const int cell_w = (kScreenW - 2 * l.margin_x) / max_cols;
		const int cell_h = (kScreenH - l.header_h - l.footer_h - 2 * l.margin_y) / max_rows;

		const std::uint8_t rows = vec.rows;
		const std::uint8_t cols = vec.cols;

		for (std::uint8_t r = 0; r < rows; r++) {
				for (std::uint8_t c = 0; c < cols; c++) {
						const int x = grid_left + c * cell_w;
						const int y = grid_top + r * cell_h;

						gfx_SetColor(ui::color::kLightGray);
						gfx_FillRectangle(x, y, cell_w - 1, cell_h - 1);
						gfx_SetColor(ui::color::kDarkGray);
						gfx_Rectangle(x, y, cell_w - 1, cell_h - 1);

						const matrix_core::Rational& v = vec.data[static_cast<std::size_t>(r) * vec.stride + static_cast<std::size_t>(c)];
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						if (v.den() == 1) {
								w.append_i64(v.num());
						} else {
								w.append_i64(v.num());
								w.put('/');
								w.append_i64(v.den());
						}

						gfx_SetTextFGColor(ui::color::kBlack);
						gfx_SetTextXY(x + 2, y + 3);
						gfx_PrintString(fmt_buf_);
				}
		}
}

void App::update_projection_result(ProjectionResultState& s) noexcept {
		// steps
		if (s.has_steps && input_.pressed(kKbGroup1, kb_2nd) && expl_.available()) {
				SHELL_DBG("[proj] 2ND steps\n");
				REQUIRE(push(Page::make_steps()), "push steps failed");
				return;
		}

		// toggle view
		if (input_.repeat(kKbGroupArrows, kb_Left) || input_.repeat(kKbGroupArrows, kb_Right)) {
				s.mode = (s.mode == 0) ? 1 : 0;
				SHELL_DBG("[proj] toggle mode=%u\n", (unsigned)s.mode);
				return;
		}

		if (!input_.pressed(kKbGroup6, kb_Clear))
				return;

		SHELL_DBG("[proj] CLEAR back -> drop tail\n");
		REQUIRE(pop(), "pop failed");
		persist_.rewind(persist_base_mark_);
		persist_tail_mark_ = persist_base_mark_;
		expl_ = {};
}

} // namespace matrix_shell
