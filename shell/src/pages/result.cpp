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
using detail::render_pivot_cols;
using namespace matrix_shell::text_literals;
} // namespace

void App::render_result(const ResultState& s) noexcept {
		render_header(nullptr);
		gfx_PrintString(op_name(s.op));
		gfx_PrintString("result.suffix"_tx);
		if (s.has_steps)
				render_footer_hint("footer.result_steps_back"_tx);
		else
				render_footer_hint("footer.clear_back"_tx);

		gfx_SetTextFGColor(ui::color::kBlack);

		// the body is the operation's own
		switch (s.op) {
#define MATRIX_OP_IN(id, key, binary, run, body, steps)                                                                                    \
		case OperationId::id:                                                                                                              \
				body(s);                                                                                                                   \
				return;
#define MATRIX_OP_OUT(id, key, binary, run, body, steps)
#include "matrix_shell/program/ops.inc"
#undef MATRIX_OP_IN
#undef MATRIX_OP_OUT
		default:
				break;
		}
		fail_fast("render_result: operation not in this program");
}

// the result matrix on its own, for operations with nothing to add to it
void App::render_result_matrix(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		if (!s.data || s.rows < 1 || s.cols < 1 || s.stride < s.cols)
				fail_fast("render_result_matrix: invalid matrix result state");
		render_result_grid(s, l.header_h + 8);
}

void App::render_result_grid(const ResultState& s, int grid_top) noexcept {
		const ui::Layout l = ui::Layout{};
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

// "rank=2 null=1 piv:1 3" from `i`, `j` and the pivot mask in `num`
void App::render_rank_line(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
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
}

bool App::open_result_steps(const ResultState&) noexcept {
		if (!expl_.available())
				return false;
		SHELL_DBG("[result] 2ND steps\n");
		REQUIRE(push(Page::make_steps()), "push steps failed");
		return true;
}

void App::update_result(ResultState& s) noexcept {
		// 2ND: the steps (an operation may open something of its own instead)
		if (s.has_steps && input_.pressed(kKbGroup1, kb_2nd)) {
				bool opened = false;
				switch (s.op) {
#define MATRIX_OP_IN(id, key, binary, run, body, steps)                                                                                    \
		case OperationId::id:                                                                                                              \
				opened = steps(s);                                                                                                         \
				break;
#define MATRIX_OP_OUT(id, key, binary, run, body, steps)
#include "matrix_shell/program/ops.inc"
#undef MATRIX_OP_IN
#undef MATRIX_OP_OUT
				default:
						fail_fast("update_result: operation not in this program");
				}
				if (opened)
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

} // namespace matrix_shell
