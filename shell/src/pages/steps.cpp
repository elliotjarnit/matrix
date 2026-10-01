#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>
#include <keypadc.h>

#include "tex/tex.h"
#include "tex/tex_renderer.h"

#include <utility>

namespace matrix_shell {
namespace {
using detail::fail_fast;
using detail::keep_cursor_in_view;
using detail::kKbGroup1;
using detail::kKbGroup6;
using detail::kKbGroupArrows;
using detail::kScreenH;
using detail::kScreenW;
using namespace matrix_shell::text_literals;

static TeX_Config kTeXCfg{ui::color::kBlack, ui::color::kWhite, "TeXFonts", nullptr, nullptr};

const char* footer_steps_no_scroll() noexcept {
		return "footer.steps_no_scroll"_tx;
}

const char* footer_steps_scroll() noexcept {
		return "footer.steps_scroll"_tx;
}
} // namespace

void App::steps_tex_reset() noexcept {
		steps_tex_release();
}

void App::steps_tex_release() noexcept {
		tex_cached_step_index_ = 0xFFFF;
		tex_cached_step_ec_ = matrix_core::ErrorCode::Internal;
		tex_scroll_y_ = 0;
		tex_total_height_ = 0;
		tex_doc_[0] = '\0';
		if (tex_layout_) {
				if (tex_renderer_)
						tex_renderer_invalidate(tex_renderer_);
				tex_free(tex_layout_);
				tex_layout_ = nullptr;
		}
}

void App::render_steps(const StepsState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		render_header("steps.title"_tx);

		if (!expl_.available()) {
				gfx_SetTextFGColor(ui::color::kBlack);
				gfx_SetTextXY(l.margin_x, l.header_h + 30);
				gfx_PrintString("steps.none_available"_tx);
				return;
		}

		const std::size_t n = expl_.step_count();
		if (n == 0) {
				gfx_SetTextFGColor(ui::color::kBlack);
				gfx_SetTextXY(l.margin_x, l.header_h + 30);
				gfx_PrintString("steps.none"_tx);
				return;
		}

		if (static_cast<std::size_t>(s.index) >= n)
				fail_fast("render_steps: step index out of range");
		const std::size_t idx = static_cast<std::size_t>(s.index);

		gfx_SetTextFGColor(ui::color::kBlack);
		gfx_SetTextXY(l.margin_x, l.header_h + 4);
		gfx_PrintString("steps.title"_tx);
		gfx_PrintString(" ");
		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
		w.append_u64(static_cast<std::uint64_t>(idx + 1));
		w.put('/');
		w.append_u64(static_cast<std::uint64_t>(n));
		gfx_PrintString(fmt_buf_);

		const int content_y = l.header_h + 18;

		// build and cache core step buffers + tex layout only when the step changes
		if (tex_cached_step_index_ != static_cast<std::uint16_t>(idx)) {
				steps_tex_release();

				step_caption_[0] = '\0';
				step_latex_[0] = '\0';

				{
						matrix_core::ArenaScratchScope scratch_tx(scratch_);
						matrix_core::StepRenderBuffers out;
						out.caption = step_caption_;
						out.caption_cap = sizeof(step_caption_);
						out.latex = step_latex_;
						out.latex_cap = sizeof(step_latex_);
						out.scratch = &scratch_;

						tex_cached_step_ec_ = expl_.render_step(idx, out);
				}
				SHELL_DBG("[steps] render idx=%u/%u ec=%u\n", (unsigned)idx, (unsigned)n, (unsigned)tex_cached_step_ec_);

				if (!matrix_core::is_ok(tex_cached_step_ec_)) {
						gfx_SetTextFGColor(ui::color::kBlack);
						gfx_SetTextXY(l.margin_x, content_y);
						gfx_PrintString("steps.render_failed"_tx);
						render_footer_hint(footer_steps_no_scroll());
						return;
				}

				tex_doc_[0] = '\0';
				matrix_core::CheckedWriter texw{tex_doc_, sizeof(tex_doc_)};
				if (step_caption_[0] != '\0') {
						texw.append(step_caption_);
						texw.put('\n');
				}
				texw.append(step_latex_);

				tex_layout_ = tex_format(tex_doc_, kScreenW - 2 * l.margin_x, &kTeXCfg);
				if (!tex_layout_) {
						SHELL_DBG("[tex] tex_format failed\n");
						gfx_SetTextXY(l.margin_x, content_y);
						gfx_PrintString("steps.tex_failed"_tx);
						render_footer_hint(footer_steps_no_scroll());
						return;
				}
				tex_total_height_ = tex_get_total_height(tex_layout_);
				tex_cached_step_index_ = static_cast<std::uint16_t>(idx);
				tex_scroll_y_ = 0;
		}

		if (!matrix_core::is_ok(tex_cached_step_ec_)) {
				gfx_SetTextFGColor(ui::color::kBlack);
				gfx_SetTextXY(l.margin_x, content_y);
				gfx_PrintString("steps.render_failed"_tx);
				render_footer_hint(footer_steps_no_scroll());
				return;
		}

		if (tex_layout_ && tex_renderer_) {
				// libtexce uses a fixed 240px viewport. render first, then redraw footer to
				// keep UI on top
				const int view_h = kScreenH - content_y - l.footer_h;
				int max_scroll = tex_total_height_ - view_h;
				if (max_scroll < 0)
						max_scroll = 0;
				if (tex_scroll_y_ < 0)
						tex_scroll_y_ = 0;
				if (tex_scroll_y_ > max_scroll)
						tex_scroll_y_ = max_scroll;

				tex_draw(tex_renderer_, tex_layout_, l.margin_x, content_y, tex_scroll_y_);
		}

		render_footer_hint(footer_steps_scroll());
}

void App::update_steps(StepsState& s) noexcept {
		if (input_.pressed(kKbGroup6, kb_Clear)) {
				SHELL_DBG("[steps] CLEAR back\n");
				steps_tex_release();
				REQUIRE(pop(), "pop failed");
				return;
		}

		if (!expl_.available())
				return;

		const std::size_t n = expl_.step_count();
		if (n == 0)
				return;

		const bool jump = input_.down(kKbGroup1, kb_2nd);

		if (jump && input_.repeat(kKbGroupArrows, kb_Left)) {
				s.index = 0;
				steps_tex_reset();
				return;
		}
		if (jump && input_.repeat(kKbGroupArrows, kb_Right)) {
				s.index = static_cast<std::uint16_t>(n - 1);
				steps_tex_reset();
				return;
		}

		if (input_.repeat(kKbGroupArrows, kb_Left)) {
				if (s.index > 0)
						s.index--;
				steps_tex_reset();
				return;
		}
		if (input_.repeat(kKbGroupArrows, kb_Right)) {
				if (static_cast<std::size_t>(s.index + 1) < n)
						s.index++;
				steps_tex_reset();
				return;
		}

		// vertical scroll (not sure when this would ever be the case, but just in case)
		if (input_.repeat(kKbGroupArrows, kb_Up)) {
				tex_scroll_y_ -= 10;
				return;
		}
		if (input_.repeat(kKbGroupArrows, kb_Down)) {
				tex_scroll_y_ += 10;
				return;
		}
}

} // namespace matrix_shell
