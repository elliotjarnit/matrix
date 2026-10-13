#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_area.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>

#include <utility>

namespace matrix_shell {
namespace {
using detail::fail_fast;
using detail::kScreenH;
using detail::kScreenW;
using namespace matrix_shell::text_literals;

// built once and passed by reference: a fresh copy per call does not fit the
// CE stack frame of the run functions
const matrix_core::AreaLabels& area_labels() noexcept {
		static const matrix_core::AreaLabels labels{
		        .vectors = "area.vectors"_tx,
		        .vertices = "area.vertices"_tx,
		        .opposite = "area.opposite"_tx,
		        .edges = "area.edges"_tx,
		        .area = "area.area"_tx,
		        .half = "area.half"_tx,
		        .area_word = "area.word"_tx,
		        .images = "area.images"_tx,
		        .image_vertices = "area.image_vertices"_tx,
		        .area_image = "area.area_image"_tx,
		        .det_a = "area.det_a"_tx,
		        .check = "area.check"_tx,
		};
		return labels;
}

matrix_core::AreaShape shape_of(OperationId op) noexcept {
		return op == OperationId::AreaTriangle ? matrix_core::AreaShape::Triangle : matrix_core::AreaShape::Parallelogram;
}

std::uint8_t polygon_size(matrix_core::AreaShape shape) noexcept {
		return shape == matrix_core::AreaShape::Triangle ? 3 : 4;
}

void append_rational(matrix_core::CheckedWriter& w, const matrix_core::Rational& r) noexcept {
		w.append_i64(r.num());
		if (r.den() != 1) {
				w.put('/');
				w.append_i64(r.den());
		}
}

// "<prefix>A=2x5"
void append_dims(matrix_core::CheckedWriter& w, const char* prefix, std::uint8_t slot, matrix_core::MatrixView m) noexcept {
		w.append(prefix);
		w.put(static_cast<char>('A' + slot));
		w.append("=");
		w.append_u64(m.rows);
		w.put('x');
		w.append_u64(m.cols);
}

// The sketch: the polygons (columns of `pts`, two rows each) on axes through
// the origin, scaled to fit the rectangle below `top` with equal units on both
// axes. Coordinates go to pixels in fixed point (4 fractional bits); anything
// too large to place is not drawn.
struct SketchPoly {
		matrix_core::MatrixView pts;
		std::uint8_t fill;
		std::uint8_t edge;
};

constexpr std::int64_t kFixOne = 16;
constexpr std::int64_t kFixMax = std::int64_t{1} << 30;

bool to_fixed(const matrix_core::Rational& r, std::int64_t* out) noexcept {
		const std::int64_t num = r.num();
		if (num > kFixMax || num < -kFixMax || r.den() <= 0)
				return false;
		*out = (num * kFixOne) / r.den();
		return true;
}

struct SketchFrame {
		std::int64_t min_x = 0;
		std::int64_t max_y = 0;
		std::int64_t n = 1; // pixels per fixed point unit, n / d
		std::int64_t d = 1;
		int ox = 0;
		int oy = 0;

		int px(std::int64_t fx) const noexcept { return ox + static_cast<int>(((fx - min_x) * n) / d); }
		int py(std::int64_t fy) const noexcept { return oy + static_cast<int>(((max_y - fy) * n) / d); }
};

void render_sketch(const SketchPoly* polys, std::uint8_t count, int top) noexcept {
		const ui::Layout l = ui::Layout{};
		const int left = l.margin_x;
		const int w = kScreenW - 2 * l.margin_x;
		const int h = kScreenH - l.footer_h - 4 - top;
		if (h < 24)
				return;

		// bounding box of every vertex and the origin
		std::int64_t min_x = 0;
		std::int64_t max_x = 0;
		std::int64_t min_y = 0;
		std::int64_t max_y = 0;
		for (std::uint8_t p = 0; p < count; p++) {
				const matrix_core::MatrixView m = polys[p].pts;
				for (std::uint8_t c = 0; c < m.cols; c++) {
						std::int64_t x = 0;
						std::int64_t y = 0;
						if (!to_fixed(m.at(0, c), &x) || !to_fixed(m.at(1, c), &y))
								return;
						min_x = x < min_x ? x : min_x;
						max_x = x > max_x ? x : max_x;
						min_y = y < min_y ? y : min_y;
						max_y = y > max_y ? y : max_y;
				}
		}
		const std::int64_t pad_x = (max_x - min_x) / 10 + kFixOne / 2;
		const std::int64_t pad_y = (max_y - min_y) / 10 + kFixOne / 2;
		min_x -= pad_x;
		max_x += pad_x;
		min_y -= pad_y;
		max_y += pad_y;
		const std::int64_t span_x = max_x - min_x;
		const std::int64_t span_y = max_y - min_y;

		// the axis that fills its side sets the scale; centre the other
		SketchFrame f;
		f.min_x = min_x;
		f.max_y = max_y;
		if (static_cast<std::int64_t>(w) * span_y <= static_cast<std::int64_t>(h) * span_x) {
				f.n = w;
				f.d = span_x;
		} else {
				f.n = h;
				f.d = span_y;
		}
		f.ox = left + (w - static_cast<int>((span_x * f.n) / f.d)) / 2;
		f.oy = top + (h - static_cast<int>((span_y * f.n) / f.d)) / 2;

		// axes, with a tick per unit when they are far enough apart
		const int x0 = f.px(0);
		const int y0 = f.py(0);
		gfx_SetColor(ui::color::kDarkGray);
		gfx_HorizLine(f.px(min_x), y0, f.px(max_x) - f.px(min_x));
		gfx_VertLine(x0, f.py(max_y), f.py(min_y) - f.py(max_y));
		if ((kFixOne * f.n) / f.d >= 5) {
				for (std::int64_t u = min_x / kFixOne; u * kFixOne <= max_x; u++) {
						if (u * kFixOne >= min_x)
								gfx_VertLine(f.px(u * kFixOne), y0 - 2, 5);
				}
				for (std::int64_t u = min_y / kFixOne; u * kFixOne <= max_y; u++) {
						if (u * kFixOne >= min_y)
								gfx_HorizLine(x0 - 2, f.py(u * kFixOne), 5);
				}
		}

		// fills first so no outline is painted over
		for (int pass = 0; pass < 2; pass++) {
				for (std::uint8_t p = 0; p < count; p++) {
						const matrix_core::MatrixView m = polys[p].pts;
						gfx_SetColor(pass == 0 ? polys[p].fill : polys[p].edge);
						std::int64_t fx = 0;
						std::int64_t fy = 0;
						(void)to_fixed(m.at(0, 0), &fx);
						(void)to_fixed(m.at(1, 0), &fy);
						const int ax = f.px(fx);
						const int ay = f.py(fy);
						int prev_x = ax;
						int prev_y = ay;
						for (std::uint8_t c = 1; c <= m.cols; c++) {
								const std::uint8_t k = static_cast<std::uint8_t>(c % m.cols);
								(void)to_fixed(m.at(0, k), &fx);
								(void)to_fixed(m.at(1, k), &fy);
								const int bx = f.px(fx);
								const int by = f.py(fy);
								if (pass == 0) {
										// a fan from the first vertex: triangles (0, c-1, c)
										if (c >= 2 && k != 0)
												gfx_FillTriangle(ax, ay, prev_x, prev_y, bx, by);
								} else {
										gfx_Line(prev_x, prev_y, bx, by);
										gfx_FillCircle(bx, by, 2);
								}
								prev_x = bx;
								prev_y = by;
						}
				}
		}
}

} // namespace

// shows the message for a failed area operation; false when there was none.
// `dims_prefix` explains the shapes the region slot may have.
bool App::area_failed(const matrix_core::Error& err, std::uint8_t slot, matrix_core::MatrixView p, const char* dims_prefix) noexcept {
		if (matrix_core::is_ok(err))
				return false;
		if (err.code == matrix_core::ErrorCode::InvalidDimension) {
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				append_dims(w, dims_prefix, slot, p);
				show_message(fmt_buf_);
		} else if (err.code == matrix_core::ErrorCode::NotParallelogram) {
				show_message("msg.not_parallelogram"_tx);
		} else if (err.code == matrix_core::ErrorCode::Overflow) {
				show_message("common.error"_tx);
		} else {
				fail_fast("area: unexpected error");
		}
		return true;
}

// replaces the slot picker with the result: the grid fields carry `m` (the
// polygons), `num`/`den` the area. Not inlined, so the Page stays out of the
// run functions' stack frames.
[[gnu::noinline]] void App::push_area_result(
        OperationId op, std::uint8_t slot_a, std::uint8_t slot_b, matrix_core::MatrixView m, const matrix_core::Rational& area) noexcept {
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (area)");
		Page page = Page::make_result_matrix(op, slot_a, slot_b, expl_.available(), m.rows, m.cols, m.stride, m.data);
		page.u.result.num = area.num();
		page.u.result.den = area.den();
		REQUIRE(push(page), "push area result failed");
}

// the area of the parallelogram or triangle in the columns of one slot
void App::run_area(OperationId op, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView p = slots_[slot].view_active();
		const matrix_core::AreaShape shape = shape_of(op);

		// the polygon for the sketch lives in the persist tail beside the explanation
		matrix_core::ArenaScope tx(persist_);
		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		matrix_core::MatrixMutView poly{};
		if (!matrix_core::is_ok(matrix_core::matrix_alloc(persist_, 2, polygon_size(shape), &poly))) {
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::Explanation expl;
		matrix_core::Rational area = matrix_core::Rational::from_int(0);
		const matrix_core::Error err = matrix_core::op_area(p, shape, scratch_, &area, &expl, opts_steps, area_labels(), poly);
		SHELL_DBG("[op] area err=%u\n", (unsigned)err.code);
		if (area_failed(err,
		            slot,
		            p,
		            shape == matrix_core::AreaShape::Triangle ? "msg.area_triangle_dims"_tx : "msg.area_parallelogram_dims"_tx))
				return;

		tx.commit();
		expl_ = std::move(expl);
		push_area_result(op, slot, 0, poly.view(), area);
}

// T(x) = Ax with A in the first slot, P in the second
void App::run_area_transform(OperationId op, std::uint8_t a_slot, std::uint8_t p_slot) noexcept {
		const matrix_core::MatrixView a = slots_[a_slot].view_active();
		const matrix_core::MatrixView p = slots_[p_slot].view_active();
		if (a.rows != 2 || a.cols != 2) {
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				append_dims(w, "msg.area_transform_a"_tx, a_slot, a);
				show_message(fmt_buf_);
				return;
		}

		matrix_core::ArenaScope tx(persist_);
		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;

		// rows 0-1: P, rows 2-3: T(P), row 4: det(A), area of P, area of T(P)
		matrix_core::MatrixMutView out{};
		if (!matrix_core::is_ok(matrix_core::matrix_alloc(persist_, 5, 4, &out))) {
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::Explanation expl;
		const matrix_core::Error err = matrix_core::op_area_transform(a,
		        p,
		        scratch_,
		        &out.at_mut(4, 0),
		        &out.at_mut(4, 1),
		        &out.at_mut(4, 2),
		        &expl,
		        opts_steps,
		        area_labels(),
		        matrix_core::MatrixMutView{2, 4, out.stride, out.data},
		        matrix_core::MatrixMutView{2, 4, out.stride, out.data + 2u * out.stride});
		SHELL_DBG("[op] area_transform err=%u\n", (unsigned)err.code);
		if (area_failed(err, p_slot, p, "msg.area_parallelogram_dims"_tx))
				return;

		tx.commit();
		expl_ = std::move(expl);
		push_area_result(op, a_slot, p_slot, out.view(), out.at(4, 2));
}

// "Area = 22" and the sketch
void App::render_result_area(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		gfx_SetTextXY(l.margin_x, l.header_h + 4);
		gfx_PrintString("result.area_eq"_tx);
		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append_i64(s.num);
				if (s.den != 1) {
						w.put('/');
						w.append_i64(s.den);
				}
		}
		gfx_PrintString(fmt_buf_);

		const SketchPoly poly{{s.rows, s.cols, s.stride, s.data}, ui::color::kPaleBlue, ui::color::kBlue};
		render_sketch(&poly, 1, l.header_h + 18);
}

// "Area P = 22  det A = 5", "Area T(P) = 5 x 22 = 110", and both shapes
void App::render_result_area_transform(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		if (!s.data || s.rows != 5 || s.cols != 4)
				fail_fast("render_result_area_transform: invalid result state");
		const matrix_core::MatrixView m{s.rows, s.cols, s.stride, s.data};
		matrix_core::Rational abs_det = m.at(4, 0);
		if (abs_det.num() < 0)
				(void)matrix_core::rational_neg(abs_det, &abs_det);

		// the calculator font has no '|': det A is shown signed, then its size
		// in the product
		gfx_SetTextXY(l.margin_x, l.header_h + 4);
		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append("result.area_p"_tx);
				append_rational(w, m.at(4, 1));
				w.append("result.area_det_a"_tx);
				append_rational(w, m.at(4, 0));
		}
		gfx_PrintString(fmt_buf_);

		gfx_SetTextFGColor(ui::color::kBlue);
		gfx_SetTextXY(l.margin_x, l.header_h + 16);
		fmt_buf_[0] = '\0';
		{
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append("result.area_tp"_tx);
				append_rational(w, abs_det);
				w.append(" x ");
				append_rational(w, m.at(4, 1));
				w.append(" = ");
				w.append_i64(s.num);
				if (s.den != 1) {
						w.put('/');
						w.append_i64(s.den);
				}
		}
		gfx_PrintString(fmt_buf_);
		gfx_SetTextFGColor(ui::color::kBlack);

		// T(P) first so P, usually the smaller, is painted over it
		const SketchPoly polys[2] = {
		        {{2, 4, s.stride, s.data + 2u * s.stride}, ui::color::kPaleBlue, ui::color::kBlue},
		        {{2, 4, s.stride, s.data}, ui::color::kLightGray, ui::color::kBlack},
		};
		render_sketch(polys, 2, l.header_h + 30);
}

} // namespace matrix_shell
