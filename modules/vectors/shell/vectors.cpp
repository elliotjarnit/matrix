#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_vectors.hpp"
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

Page make_projection_result(std::uint8_t slot_u,
        std::uint8_t slot_v,
        bool has_steps,
        std::uint8_t rows,
        std::uint8_t cols,
        std::uint8_t stride_proj,
        const matrix_core::Rational* proj_data,
        std::uint8_t stride_orth,
        const matrix_core::Rational* orth_data,
        std::int64_t k_num,
        std::int64_t k_den) noexcept {
		Page p{};
		p.kind = PageKind::ProjectionResult;
		p.u.projection_result = ProjectionResultState{
		        .slot_u = slot_u,
		        .slot_v = slot_v,
		        .mode = 0,
		        .has_steps = has_steps,
		        .rows = rows,
		        .cols = cols,
		        .stride_proj = stride_proj,
		        .stride_orth = stride_orth,
		        .proj_data = proj_data,
		        .orth_data = orth_data,
		        .k_num = k_num,
		        .k_den = k_den,
		};
		return p;
}
} // namespace

void App::run_dot(OperationId, std::uint8_t a_slot, std::uint8_t b_slot) noexcept {
		const matrix_core::MatrixView a = slots_[a_slot].view_active();
		const matrix_core::MatrixView b = slots_[b_slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);
		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::Rational v = matrix_core::Rational::from_int(0);
		const matrix_core::Error err = matrix_core::op_dot(a, b, &v, &expl, opts_steps);
		SHELL_DBG("[op] dot err=%u a=%ux%u b=%ux%u value=",
		        (unsigned)err.code,
		        (unsigned)err.a.rows,
		        (unsigned)err.a.cols,
		        (unsigned)err.b.rows,
		        (unsigned)err.b.cols);
		dbg_print_rational(v);
		SHELL_DBG("\n");
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::InvalidDimension)
						show_message("msg.need_vectors"_tx);
				else if (err.code == matrix_core::ErrorCode::DimensionMismatch) {
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						w.append("msg.need_same_length_prefix"_tx);
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
				} else if (err.code == matrix_core::ErrorCode::Internal) {
						fail_fast("update_slot_pick: dot returned Internal");
				} else
						show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		REQUIRE(pop(), "pop failed (dot)");
		REQUIRE(push(Page::make_result_scalar(OperationId::Dot, a_slot, b_slot, expl_.available(), v.num(), v.den())),
		        "push dot result failed");
}

void App::run_cross(OperationId, std::uint8_t a_slot, std::uint8_t b_slot) noexcept {
		const matrix_core::MatrixView a = slots_[a_slot].view_active();
		const matrix_core::MatrixView b = slots_[b_slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::MatrixMutView out{};
		const matrix_core::ErrorCode ec = matrix_core::matrix_alloc(persist_, a.rows, a.cols, &out);
		if (!matrix_core::is_ok(ec)) {
				SHELL_DBG("[op] cross alloc failed ec=%u used=%u/%u\n",
				        (unsigned)ec,
				        (unsigned)persist_.used(),
				        (unsigned)persist_.capacity());
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		const matrix_core::Error err = matrix_core::op_cross(a, b, out, &expl, opts_steps);
		SHELL_DBG("[op] cross err=%u a=%ux%u b=%ux%u out=%ux%u\n",
		        (unsigned)err.code,
		        (unsigned)err.a.rows,
		        (unsigned)err.a.cols,
		        (unsigned)err.b.rows,
		        (unsigned)err.b.cols,
		        (unsigned)out.rows,
		        (unsigned)out.cols);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::InvalidDimension)
						show_message("msg.need_3d_vectors"_tx);
				else if (err.code == matrix_core::ErrorCode::DimensionMismatch)
						fail_fast("update_slot_pick: cross returned DimensionMismatch");
				else if (err.code == matrix_core::ErrorCode::Internal)
						fail_fast("update_slot_pick: cross returned Internal");
				else
						show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		REQUIRE(pop(), "pop failed (cross)");
		REQUIRE(push(Page::make_result_matrix(
		                OperationId::Cross, a_slot, b_slot, expl_.available(), out.rows, out.cols, out.stride, out.data)),
		        "push cross result failed");
}

void App::run_projection(OperationId, std::uint8_t a_slot, std::uint8_t b_slot) noexcept {
		const matrix_core::MatrixView a = slots_[a_slot].view_active();
		const matrix_core::MatrixView b = slots_[b_slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::MatrixMutView out_proj{};
		matrix_core::MatrixMutView out_orth{};

		const matrix_core::ErrorCode ec1 = matrix_core::matrix_alloc(persist_, a.rows, a.cols, &out_proj);
		const matrix_core::ErrorCode ec2 = matrix_core::matrix_alloc(persist_, a.rows, a.cols, &out_orth);
		if (!matrix_core::is_ok(ec1) || !matrix_core::is_ok(ec2)) {
				SHELL_DBG("[op] proj alloc failed ec1=%u ec2=%u used=%u/%u\n",
				        (unsigned)ec1,
				        (unsigned)ec2,
				        (unsigned)persist_.used(),
				        (unsigned)persist_.capacity());
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::Explanation expl;
		matrix_core::ProjDecomposeResult res{};
		const matrix_core::Error err = matrix_core::op_proj_decompose_u_onto_v(a, b, out_proj, out_orth, &res, &expl, opts_steps);

		SHELL_DBG("[op] proj err=%u a=%ux%u b=%ux%u k=",
		        (unsigned)err.code,
		        (unsigned)err.a.rows,
		        (unsigned)err.a.cols,
		        (unsigned)err.b.rows,
		        (unsigned)err.b.cols);
		dbg_print_rational(res.k);
		SHELL_DBG("\n");

		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::InvalidDimension)
						show_message("msg.need_vectors"_tx);
				else if (err.code == matrix_core::ErrorCode::DimensionMismatch) {
						fmt_buf_[0] = '\0';
						matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
						w.append("msg.need_same_length_prefix"_tx);
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
				} else if (err.code == matrix_core::ErrorCode::DivisionByZero) {
						show_message("msg.v_zero_vector"_tx);
				} else {
						if (err.code == matrix_core::ErrorCode::Internal)
								fail_fast("update_slot_pick: projection returned Internal");
						show_message("common.error"_tx);
				}
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (projection)");
		REQUIRE(push(make_projection_result(a_slot,
		                b_slot,
		                expl_.available(),
		                out_proj.rows,
		                out_proj.cols,
		                out_proj.stride,
		                out_proj.data,
		                out_orth.stride,
		                out_orth.data,
		                res.k.num(),
		                res.k.den())),
		        "push projection result failed");
}

void App::render_result_dot(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
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
