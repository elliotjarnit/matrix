#include "matrix_core/ops_area.hpp"

#include "matrix_core/latex.hpp"
#include "matrix_core/minor_detail.hpp"
#include "matrix_core/step_detail.hpp"
#include "matrix_core/writer.hpp"

#include <new>

namespace matrix_core {
namespace {

using Text = detail::StepText;

// Rational arithmetic whose first failure sticks, checked once at the end
struct Calc {
		ErrorCode ec = ErrorCode::Ok;

		Rational add(const Rational& a, const Rational& b) noexcept {
				Rational r;
				if (is_ok(ec))
						ec = rational_add(a, b, &r);
				return r;
		}
		Rational sub(const Rational& a, const Rational& b) noexcept {
				Rational r;
				if (is_ok(ec))
						ec = rational_sub(a, b, &r);
				return r;
		}
		Rational mul(const Rational& a, const Rational& b) noexcept {
				Rational r;
				if (is_ok(ec))
						ec = rational_mul(a, b, &r);
				return r;
		}
		Rational abs(const Rational& a) noexcept {
				Rational r = a;
				if (is_ok(ec) && a.num() < 0)
						ec = rational_neg(a, &r);
				return r;
		}
		// x0 y1 - y0 x1: det of the columns x, y
		Rational det2(const Rational* x, const Rational* y) noexcept { return sub(mul(x[0], y[1]), mul(y[0], x[1])); }
		// m x for 2x2 m
		void apply(MatrixView m, const Rational* x, Rational* out) noexcept {
				for (std::uint8_t r = 0; r < 2; r++)
						out[r] = add(mul(m.at(r, 0), x[0]), mul(m.at(r, 1), x[1]));
		}
};

constexpr bool same(const Rational& a, const Rational& b) noexcept {
		return a.num() == b.num() && a.den() == b.den();
}

// the steps an area explanation is made of, in order
enum class Step : std::uint8_t {
		Input,
		Opposite,
		Edges,
		Area,
		Images,
		ImageVertices,
		AreaImage,
		DetA,
		Check,
};

constexpr std::uint8_t kMaxSteps = 9;

struct AreaCtx {
		MatrixView p{}; // the region's columns, owned by the caller
		MatrixView a{}; // the transformation, rows == 0 for op_area
		AreaShape shape = AreaShape::Parallelogram;
		AreaRegion region{};
		Rational au[2]{}; // A u, A v (transform)
		Rational av[2]{};
		Rational det_uv{};    // det[u v]
		Rational det_image{}; // det[Au Av]
		Rational det_a{};
		Rational area{};       // of the region (P)
		Rational area_image{}; // of T(P)
		AreaLabels labels{};   // copied by value: the pointed to strings are caller owned
};

bool is_transform(const AreaCtx& ctx) noexcept {
		return ctx.a.rows != 0;
}

std::uint8_t plan(const AreaCtx& ctx, Step* steps) noexcept {
		std::uint8_t n = 0;
		steps[n++] = Step::Input;
		if (ctx.region.vertices == 4)
				steps[n++] = Step::Opposite;
		if (ctx.region.vertices != 0)
				steps[n++] = Step::Edges;
		steps[n++] = Step::Area;
		if (is_transform(ctx)) {
				steps[n++] = Step::Images;
				steps[n++] = Step::ImageVertices;
				steps[n++] = Step::AreaImage;
				steps[n++] = Step::DetA;
				steps[n++] = Step::Check;
		}
		return n;
}

void caption(const StepRenderBuffers& out, const char* text) noexcept {
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append(text);
		}
}

// "(x, y)"
void point(Text& t, const Rational* xy) noexcept {
		t.ch('(').value(xy[0]).str(", ").value(xy[1]).ch(')');
}

// "\begin{bmatrix}x \\ y\end{bmatrix}"
void column(Text& t, const Rational* xy) noexcept {
		t.str("\\begin{bmatrix}").value(xy[0]).str(" \\\\ ").value(xy[1]).str("\\end{bmatrix}");
}

// "P_3", the vertex in column `col`
void vertex(Text& t, std::uint8_t col) noexcept {
		t.str("P_").index1(col);
}

void column_of(MatrixView p, std::uint8_t col, Rational* xy) noexcept {
		xy[0] = p.at(0, col);
		xy[1] = p.at(1, col);
}

// "u = [..], v = [..]" or "P_1 = (..), ..." for the vertex input
ErrorCode render_input(const AreaCtx& ctx, const StepRenderBuffers& out) noexcept {
		Text t{{out.latex, out.latex_cap, 0}};
		const AreaRegion& r = ctx.region;
		if (r.vertices == 0) {
				caption(out, detail::lbl(ctx.labels.vectors, "Columns are the edge vectors"));
				t.str("$$\\vec{u} = ");
				column(t, r.u);
				t.str(",\\quad \\vec{v} = ");
				column(t, r.v);
				return t.str("$$").ec;
		}
		caption(out, detail::lbl(ctx.labels.vertices, "Columns are the vertices"));
		t.open();
		for (std::uint8_t c = 0; c < ctx.p.cols; c++) {
				if (c == 2)
						t.br();
				else if (c != 0)
						t.str(",\\quad ");
				Rational xy[2];
				column_of(ctx.p, c, xy);
				vertex(t, c);
				t.str(" = ");
				point(t, xy);
		}
		return t.close().ec;
}

// "P_2 + P_3 - P_1 = (6, 7) = P_4"
ErrorCode render_opposite(const AreaCtx& ctx, const StepRenderBuffers& out) noexcept {
		caption(out, detail::lbl(ctx.labels.opposite, "Find the corner opposite P1"));
		const AreaRegion& r = ctx.region;
		Rational xy[2];
		column_of(ctx.p, r.opposite, xy);
		Text t{{out.latex, out.latex_cap, 0}};
		t.str("$$");
		vertex(t, r.u_col);
		t.str(" + ");
		vertex(t, r.v_col);
		t.str(" - ");
		vertex(t, 0);
		t.str(" = ");
		point(t, xy);
		t.str(" = ");
		vertex(t, r.opposite);
		return t.str("$$").ec;
}

// "u = P_2 - P_1 = [..]", "v = P_3 - P_1 = [..]"
ErrorCode render_edges(const AreaCtx& ctx, const StepRenderBuffers& out) noexcept {
		caption(out, detail::lbl(ctx.labels.edges, "Edge vectors from P1"));
		const AreaRegion& r = ctx.region;
		Text t{{out.latex, out.latex_cap, 0}};
		t.open().str("\\vec{u} = ");
		vertex(t, r.u_col);
		t.str(" - ");
		vertex(t, 0);
		t.str(" = ");
		column(t, r.u);
		t.br().str("\\vec{v} = ");
		vertex(t, r.v_col);
		t.str(" - ");
		vertex(t, 0);
		t.str(" = ");
		column(t, r.v);
		return t.close().ec;
}

// "\text{Area}<name> = [\frac{1}{2}]|det[x y]| = [\frac{1}{2}]|x0 y1 - y0 x1| = .. = area"
ErrorCode render_area_lines(const AreaCtx& ctx,
        const char* name,
        const Rational* x,
        const Rational* y,
        const Rational& det,
        const Rational& area,
        bool half,
        const StepRenderBuffers& out) noexcept {
		const Rational cols[4] = {x[0], y[0], x[1], y[1]};
		const MatrixView m{2, 2, 2, cols};
		const char* const factor = half ? "\\frac{1}{2}" : "";
		Text t{{out.latex, out.latex_cap, 0}};
		t.open().str("\\text{").str(detail::lbl(ctx.labels.area_word, "Area")).ch('}').str(name).str(" = ").str(factor);
		t.str("\\left|\\det").matrix(m, latex::MatrixBrackets::BMatrix).str("\\right|");
		t.br().str("= ").str(factor).str("\\left|").factor(x[0]).str("\\cdot ").factor(y[1]).str(" - ");
		t.factor(y[0]).str("\\cdot ").factor(x[1]).str("\\right|");
		t.br().str("= ").str(factor).str("\\left|").value(det).str("\\right| = ").value(area);
		return t.close().ec;
}

// "Au = [3*2 + 1*(-5) \\ ..] = [1 \\ -8]", the same for v
void image_line(Text& t, MatrixView a, const char* name, const Rational* x, const Rational* image) noexcept {
		t.str("A\\vec{").str(name).str("} = \\begin{bmatrix}");
		for (std::uint8_t r = 0; r < 2; r++) {
				if (r != 0)
						t.str(" \\\\ ");
				t.factor(a.at(r, 0)).str("\\cdot ").factor(x[0]).str(" + ").factor(a.at(r, 1)).str("\\cdot ").factor(x[1]);
		}
		t.str("\\end{bmatrix} = ");
		column(t, image);
}

ErrorCode render_images(const AreaCtx& ctx, const StepRenderBuffers& out) noexcept {
		caption(out, detail::lbl(ctx.labels.images, "Images of the edge vectors"));
		Text t{{out.latex, out.latex_cap, 0}};
		t.open();
		image_line(t, ctx.a, "u", ctx.region.u, ctx.au);
		t.br();
		image_line(t, ctx.a, "v", ctx.region.v, ctx.av);
		return t.close().ec;
}

// the four corners of T(P), from A P_1 (vertex input) or 0, along Au and Av
ErrorCode render_image_vertices(const AreaCtx& ctx, const StepRenderBuffers& out) noexcept {
		caption(out, detail::lbl(ctx.labels.image_vertices, "T(P) is spanned by Au and Av"));
		MatrixMutView poly;
		ErrorCode ec = matrix_alloc(*out.scratch, 2, 4, &poly);
		if (is_ok(ec))
				ec = area_polygon(ctx.region, AreaShape::Parallelogram, &ctx.a, poly);
		if (!is_ok(ec))
				return ec;

		const bool corner = ctx.region.vertices != 0;
		// polygon order: corner, +Au, +Au+Av, +Av
		static constexpr const char* kEdges[4] = {"", "A\\vec{u}", "A\\vec{u} + A\\vec{v}", "A\\vec{v}"};
		Text t{{out.latex, out.latex_cap, 0}};
		t.open();
		for (std::uint8_t k = 0; k < 4; k++) {
				if (k != 0)
						t.br();
				if (corner) {
						t.str("AP_1");
						if (k != 0)
								t.str(" + ");
				} else if (k == 0) {
						t.str("\\vec{0}");
				}
				t.str(kEdges[k]).str(" = ");
				t.ch('(').value(poly.at(0, k)).str(", ").value(poly.at(1, k)).ch(')');
		}
		return t.close().ec;
}

// "det(A) = a d - b c = det"
ErrorCode render_det_a(const AreaCtx& ctx, const StepRenderBuffers& out) noexcept {
		caption(out, detail::lbl(ctx.labels.det_a, "det(A)"));
		const MatrixView a = ctx.a;
		Text t{{out.latex, out.latex_cap, 0}};
		t.str("$$\\det(A) = ").factor(a.at(0, 0)).str("\\cdot ").factor(a.at(1, 1)).str(" - ");
		t.factor(a.at(0, 1)).str("\\cdot ").factor(a.at(1, 0)).str(" = ").value(ctx.det_a);
		return t.str("$$").ec;
}

// "|det(A)| Area(P) = |5| 22 = 110 = Area(T(P))"
ErrorCode render_check(const AreaCtx& ctx, const StepRenderBuffers& out) noexcept {
		caption(out, detail::lbl(ctx.labels.check, "Check: Area T(P) = |det A| Area P"));
		const char* const word = detail::lbl(ctx.labels.area_word, "Area");
		Text t{{out.latex, out.latex_cap, 0}};
		t.open().str("\\left|\\det(A)\\right|\\cdot\\text{").str(word).str("}(P)");
		t.br().str("= \\left|").value(ctx.det_a).str("\\right|\\cdot ").factor(ctx.area).str(" = ").value(ctx.area_image);
		t.br().str("= \\text{").str(word).str("}(T(P))");
		return t.close().ec;
}

std::size_t area_step_count(const void* vctx) noexcept {
		Step steps[kMaxSteps];
		return plan(*static_cast<const AreaCtx*>(vctx), steps);
}

ErrorCode area_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const AreaCtx*>(vctx);
		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		Step steps[kMaxSteps];
		const std::uint8_t count = plan(*ctx, steps);
		if (index >= count)
				return ErrorCode::StepOutOfRange;

		const bool transform = is_transform(*ctx);
		const bool half = ctx->shape == AreaShape::Triangle;
		switch (steps[index]) {
		case Step::Input:
				return render_input(*ctx, out);
		case Step::Opposite:
				return render_opposite(*ctx, out);
		case Step::Edges:
				return render_edges(*ctx, out);
		case Step::Area:
				caption(out,
				        half ? detail::lbl(ctx->labels.half, "A triangle is half the parallelogram")
				             : detail::lbl(ctx->labels.area, "Area is |det| of the edge vectors"));
				return render_area_lines(*ctx, transform ? "(P)" : "", ctx->region.u, ctx->region.v, ctx->det_uv, ctx->area, half, out);
		case Step::Images:
				return render_images(*ctx, out);
		case Step::ImageVertices:
				return render_image_vertices(*ctx, out);
		case Step::AreaImage:
				caption(out, detail::lbl(ctx->labels.area_image, "Area of T(P) is |det| of Au, Av"));
				return render_area_lines(*ctx, "(T(P))", ctx->au, ctx->av, ctx->det_image, ctx->area_image, false, out);
		case Step::DetA:
				return render_det_a(*ctx, out);
		case Step::Check:
				return render_check(*ctx, out);
		}
		return ErrorCode::Internal;
}

constexpr ExplanationVTable kAreaVTable = {
        .step_count = &area_step_count,
        .render_step = &area_render_step,
        .destroy = nullptr,
};

// |det[u v]|, halved for a triangle
ErrorCode region_area(const AreaRegion& r, AreaShape shape, Rational* det, Rational* area) noexcept {
		Calc c;
		*det = c.det2(r.u, r.v);
		*area = c.abs(*det);
		if (is_ok(c.ec) && shape == AreaShape::Triangle)
				c.ec = rational_div(*area, Rational::from_int(2), area);
		return c.ec;
}

// the working context, in scratch (it is too big for the CE stack)
AreaCtx* new_ctx(Arena& scratch, MatrixView p, MatrixView a, AreaShape shape, const AreaLabels& labels) noexcept {
		void* mem = scratch.allocate(sizeof(AreaCtx), alignof(AreaCtx));
		if (!mem)
				return nullptr;
		// copied from a blank one: value initialising it in place makes the ez80
		// backend address fields past IY+127, which does not assemble
		static constexpr AreaCtx kBlank{};
		auto* ctx = new (mem) AreaCtx(kBlank);
		ctx->p = p;
		ctx->a = a;
		ctx->shape = shape;
		ctx->labels = labels;
		return ctx;
}

// copies the context into persist for the explanation
Error commit_explanation(const AreaCtx& ctx, Explanation* expl, const ExplainOptions& opts) noexcept {
		if (!opts.enable)
				return {};
		if (!opts.persist || !expl)
				return {ErrorCode::Internal};
		void* mem = opts.persist->allocate(sizeof(AreaCtx), alignof(AreaCtx));
		if (!mem)
				return err_overflow();
		auto* stored = new (mem) AreaCtx{ctx};
		*expl = Explanation::make(stored, &kAreaVTable);
		return {};
}

Error error_of(ErrorCode ec, MatrixView a, MatrixView b = MatrixView{}) noexcept {
		Error err;
		err.code = ec;
		err.a = a.dim();
		err.b = b.dim();
		return err;
}

// column `col` of m becomes a times it
ErrorCode apply_in_place(MatrixView a, MatrixMutView m, std::uint8_t col) noexcept {
		const Rational x0 = m.at(0, col);
		const Rational x1 = m.at(1, col);
		Rational t;
		ErrorCode ec = ErrorCode::Ok;
		for (std::uint8_t i = 0; i < 2 && is_ok(ec); i++) {
				ec = rational_mul(a.at(i, 0), x0, &m.at_mut(i, col));
				if (is_ok(ec))
						ec = rational_mul(a.at(i, 1), x1, &t);
				if (is_ok(ec))
						ec = rational_add(m.at(i, col), t, &m.at_mut(i, col));
		}
		return ec;
}

} // namespace

ErrorCode area_region(MatrixView p, AreaShape shape, AreaRegion* out) noexcept {
		if (!out || !p.data)
				return ErrorCode::Internal;
		const std::uint8_t max_cols = shape == AreaShape::Parallelogram ? 4 : 3;
		if (p.rows != 2 || p.cols < 2 || p.cols > max_cols)
				return ErrorCode::InvalidDimension;

		// field by field: a blank AreaRegion temporary is too big for the CE stack
		AreaRegion& r = *out;
		r.u_col = 1;
		r.v_col = 2;
		r.opposite = 0;
		if (p.cols == 2) {
				r.vertices = 0;
				r.origin[0] = Rational::from_int(0);
				r.origin[1] = Rational::from_int(0);
				column_of(p, 0, r.u);
				column_of(p, 1, r.v);
				return ErrorCode::Ok;
		}

		r.vertices = p.cols;
		column_of(p, 0, r.origin);
		ErrorCode ec = ErrorCode::Ok;
		if (p.cols == 4) {
				// the corner opposite P_1 is the sum of the other two minus P_1;
				// u and v hold that sum while it is checked
				for (std::uint8_t k = 1; k < 4 && r.opposite == 0; k++) {
						const auto a = static_cast<std::uint8_t>(k == 1 ? 2 : 1);
						const auto b = static_cast<std::uint8_t>(k == 3 ? 2 : 3);
						bool match = true;
						for (std::uint8_t i = 0; i < 2 && match && is_ok(ec); i++) {
								ec = rational_add(p.at(i, a), p.at(i, b), &r.u[i]);
								if (is_ok(ec))
										ec = rational_sub(r.u[i], p.at(i, 0), &r.u[i]);
								match = is_ok(ec) && same(r.u[i], p.at(i, k));
						}
						if (!is_ok(ec))
								return ec;
						if (match) {
								r.opposite = k;
								r.u_col = a;
								r.v_col = b;
						}
				}
				if (r.opposite == 0)
						return ErrorCode::NotParallelogram;
		}

		for (std::uint8_t i = 0; i < 2 && is_ok(ec); i++) {
				ec = rational_sub(p.at(i, r.u_col), p.at(i, 0), &r.u[i]);
				if (is_ok(ec))
						ec = rational_sub(p.at(i, r.v_col), p.at(i, 0), &r.v[i]);
		}
		return ec;
}

ErrorCode area_polygon(const AreaRegion& r, AreaShape shape, const MatrixView* a, MatrixMutView out) noexcept {
		const std::uint8_t n = shape == AreaShape::Parallelogram ? 4 : 3;
		if (!out.data || out.rows != 2 || out.cols != n)
				return ErrorCode::Internal;
		if (a && (a->rows != 2 || a->cols != 2 || !a->data))
				return ErrorCode::Internal;

		// columns: corner, corner + u, corner + u + v, corner + v (triangle: no
		// third), with corner, u, v mapped by A first when given
		const std::uint8_t last = static_cast<std::uint8_t>(n - 1);
		ErrorCode ec = ErrorCode::Ok;
		for (std::uint8_t i = 0; i < 2; i++) {
				out.at_mut(i, 0) = r.origin[i];
				out.at_mut(i, 1) = r.u[i];
				out.at_mut(i, last) = r.v[i];
		}
		if (a) {
				ec = apply_in_place(*a, out, 0);
				if (is_ok(ec))
						ec = apply_in_place(*a, out, 1);
				if (is_ok(ec))
						ec = apply_in_place(*a, out, last);
		}
		for (std::uint8_t i = 0; i < 2 && is_ok(ec); i++) {
				ec = rational_add(out.at(i, 0), out.at(i, 1), &out.at_mut(i, 1));
				if (is_ok(ec) && n == 4)
						ec = rational_add(out.at(i, 1), out.at(i, last), &out.at_mut(i, 2));
				if (is_ok(ec))
						ec = rational_add(out.at(i, 0), out.at(i, last), &out.at_mut(i, last));
		}
		return ec;
}

Error op_area(MatrixView p,
        AreaShape shape,
        Arena& scratch,
        Rational* area,
        Explanation* expl,
        const ExplainOptions& opts,
        const AreaLabels& labels,
        MatrixMutView polygon) noexcept {
		if (!area)
				return {ErrorCode::Internal};
		ArenaScratchScope scratch_scope(scratch);
		AreaCtx* ctx = new_ctx(scratch, p, MatrixView{}, shape, labels);
		if (!ctx)
				return err_overflow();
		ErrorCode ec = area_region(p, shape, &ctx->region);
		if (is_ok(ec))
				ec = region_area(ctx->region, shape, &ctx->det_uv, &ctx->area);
		if (is_ok(ec) && polygon.data)
				ec = area_polygon(ctx->region, shape, nullptr, polygon);
		if (!is_ok(ec))
				return error_of(ec, p);
		*area = ctx->area;
		return commit_explanation(*ctx, expl, opts);
}

Error op_area_transform(MatrixView a,
        MatrixView p,
        Arena& scratch,
        Rational* det_a,
        Rational* area_p,
        Rational* area_tp,
        Explanation* expl,
        const ExplainOptions& opts,
        const AreaLabels& labels,
        MatrixMutView polygon_p,
        MatrixMutView polygon_tp) noexcept {
		if (!det_a || !area_p || !area_tp)
				return {ErrorCode::Internal};
		if (a.rows != 2 || a.cols != 2)
				return error_of(a.rows == a.cols ? ErrorCode::InvalidDimension : ErrorCode::NotSquare, a, p);

		ArenaScratchScope scratch_scope(scratch);
		AreaCtx* ctx = new_ctx(scratch, p, a, AreaShape::Parallelogram, labels);
		if (!ctx)
				return err_overflow();
		ErrorCode ec = area_region(p, AreaShape::Parallelogram, &ctx->region);
		if (is_ok(ec))
				ec = region_area(ctx->region, AreaShape::Parallelogram, &ctx->det_uv, &ctx->area);
		if (!is_ok(ec))
				return error_of(ec, a, p);

		Calc c;
		c.apply(a, ctx->region.u, ctx->au);
		c.apply(a, ctx->region.v, ctx->av);
		ctx->det_image = c.det2(ctx->au, ctx->av);
		ctx->area_image = c.abs(ctx->det_image);
		// det(A) = a d - b c
		ctx->det_a = c.sub(c.mul(a.at(0, 0), a.at(1, 1)), c.mul(a.at(0, 1), a.at(1, 0)));
		ec = c.ec;
		if (is_ok(ec) && polygon_p.data)
				ec = area_polygon(ctx->region, AreaShape::Parallelogram, nullptr, polygon_p);
		if (is_ok(ec) && polygon_tp.data)
				ec = area_polygon(ctx->region, AreaShape::Parallelogram, &a, polygon_tp);
		if (!is_ok(ec))
				return error_of(ec, a, p);

		*det_a = ctx->det_a;
		*area_p = ctx->area;
		*area_tp = ctx->area_image;
		return commit_explanation(*ctx, expl, opts);
}

} // namespace matrix_core
