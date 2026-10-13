#include "matrix_core/matrix_core.hpp"
#include "matrix_core/ops_area.hpp"

#include "test_dbg_ce.hpp"

#include <cassert>
#include <cstring>

using matrix_core::AreaRegion;
using matrix_core::AreaShape;
using matrix_core::Arena;
using matrix_core::ErrorCode;
using matrix_core::ExplainOptions;
using matrix_core::Explanation;
using matrix_core::MatrixMutView;
using matrix_core::MatrixView;
using matrix_core::Rational;
using matrix_core::Slab;
using matrix_core::StepRenderBuffers;

namespace {

// 2 x cols, row major
MatrixMutView mat(Arena& arena, std::uint8_t rows, std::uint8_t cols, const std::int64_t* vals) {
		MatrixMutView m;
		assert(matrix_core::matrix_alloc(arena, rows, cols, &m) == ErrorCode::Ok);
		for (std::uint8_t r = 0; r < rows; r++) {
				for (std::uint8_t c = 0; c < cols; c++)
						m.at_mut(r, c) = Rational::from_int(vals[static_cast<std::size_t>(r) * cols + c]);
		}
		return m;
}

bool is(const Rational& r, std::int64_t num, std::int64_t den = 1) {
		return r.num() == num && r.den() == den;
}

struct Harness {
		Slab slab;
		Arena persist;
		Arena scratch;

		Harness() {
				assert(slab.init(32 * 1024) == ErrorCode::Ok);
				persist = Arena(slab.data(), slab.size() / 2);
				scratch = Arena(slab.data() + (slab.size() / 2), slab.size() - (slab.size() / 2));
		}

		ExplainOptions opts() { return ExplainOptions{.enable = true, .persist = &persist}; }
};

char g_caption[128];
char g_latex[1024]; // same as App::step_latex_

const char* render(Harness& h, const Explanation& expl, std::size_t index) {
		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
		assert(expl.render_step(index, bufs) == ErrorCode::Ok);
		return g_latex;
}

void render_all(Harness& h, const Explanation& expl) {
		for (std::size_t i = 0; i < expl.step_count(); i++)
				(void)render(h, expl, i);
		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
		assert(expl.render_step(expl.step_count(), bufs) == ErrorCode::StepOutOfRange);
}

// worksheet 3(a): the parallelogram (0,0), (4,1), (2,6), (6,7)
void parallelogram_vertices() {
		Harness h;
		const std::int64_t v[] = {0, 4, 2, 6, 0, 1, 6, 7};
		MatrixMutView p = mat(h.persist, 2, 4, v);
		Rational area;
		Explanation expl;
		assert(matrix_core::is_ok(matrix_core::op_area(p.view(), AreaShape::Parallelogram, h.scratch, &area, &expl, h.opts())));
		assert(is(area, 22));

		// vertices, opposite corner, edges, area
		assert(expl.step_count() == 4);
		render_all(h, expl);
		render(h, expl, 0);
		assert(std::strcmp(g_caption, "Columns are the vertices") == 0);
		assert(std::strcmp(g_latex,
		               "$$\\begin{array}{c}P_1 = (0, 0),\\quad P_2 = (4, 1) \\\\ P_3 = (2, 6),\\quad P_4 = (6, 7)\\end{array}$$") == 0);
		render(h, expl, 1);
		assert(std::strcmp(g_latex, "$$P_2 + P_3 - P_1 = (6, 7) = P_4$$") == 0);
		render(h, expl, 2);
		assert(std::strcmp(g_latex,
		               "$$\\begin{array}{c}\\vec{u} = P_2 - P_1 = \\begin{bmatrix}4 \\\\ 1\\end{bmatrix} \\\\ "
		               "\\vec{v} = P_3 - P_1 = \\begin{bmatrix}2 \\\\ 6\\end{bmatrix}\\end{array}$$") == 0);
		render(h, expl, 3);
		assert(std::strcmp(g_caption, "Area is |det| of the edge vectors") == 0);
		assert(std::strstr(g_latex, "\\text{Area} = \\left|\\det\\begin{bmatrix}4 & 2 \\\\ 1 & 6\\end{bmatrix}\\right|") != nullptr);
		assert(std::strstr(g_latex, "= \\left|4\\cdot 6 - 2\\cdot 1\\right| \\\\ = \\left|22\\right| = 22\\end{array}$$") != nullptr);

		// the same corners in another order: (6,7) next to (0,0) is not a neighbour
		const std::int64_t w[] = {0, 6, 4, 2, 0, 7, 1, 6};
		MatrixMutView q = mat(h.persist, 2, 4, w);
		AreaRegion r;
		assert(matrix_core::area_region(q.view(), AreaShape::Parallelogram, &r) == ErrorCode::Ok);
		assert(r.opposite == 1 && r.u_col == 2 && r.v_col == 3);
		assert(matrix_core::is_ok(matrix_core::op_area(q.view(), AreaShape::Parallelogram, h.scratch, &area, nullptr, ExplainOptions{})));
		assert(is(area, 22));

		// three vertices: the fourth is implied
		MatrixMutView three = mat(h.persist, 2, 4, v);
		three.cols = 3;
		assert(matrix_core::is_ok(matrix_core::op_area(three.view(), AreaShape::Parallelogram, h.scratch, &area, &expl, h.opts())));
		assert(is(area, 22));
		assert(expl.step_count() == 3);
		render_all(h, expl);

		// not a parallelogram
		const std::int64_t bad[] = {0, 4, 2, 7, 0, 1, 6, 7};
		MatrixMutView b = mat(h.persist, 2, 4, bad);
		assert(matrix_core::op_area(b.view(), AreaShape::Parallelogram, h.scratch, &area, &expl, h.opts()).code ==
		        ErrorCode::NotParallelogram);
}

// worksheet 3(b): the triangle (4,1), (2,6), (3,8)
void triangle() {
		Harness h;
		const std::int64_t v[] = {4, 2, 3, 1, 6, 8};
		MatrixMutView p = mat(h.persist, 2, 3, v);
		Rational area;
		Explanation expl;
		assert(matrix_core::is_ok(matrix_core::op_area(p.view(), AreaShape::Triangle, h.scratch, &area, &expl, h.opts())));
		assert(is(area, 9, 2));
		assert(expl.step_count() == 3);
		MatrixMutView tri;
		assert(matrix_core::matrix_alloc(h.persist, 2, 3, &tri) == ErrorCode::Ok);
		assert(matrix_core::is_ok(matrix_core::op_area(
		        p.view(), AreaShape::Triangle, h.scratch, &area, nullptr, ExplainOptions{}, matrix_core::AreaLabels{}, tri)));
		assert(is(tri.at(0, 0), 4) && is(tri.at(1, 1), 6) && is(tri.at(0, 2), 3) && is(tri.at(1, 2), 8));
		render_all(h, expl);
		render(h, expl, 2);
		assert(std::strcmp(g_caption, "A triangle is half the parallelogram") == 0);
		assert(std::strstr(g_latex, "\\text{Area} = \\frac{1}{2}\\left|\\det\\begin{bmatrix}-2 & -1 \\\\ 5 & 7\\end{bmatrix}\\right|") !=
		        nullptr);
		assert(std::strstr(g_latex, "= \\frac{1}{2}\\left|(-2)\\cdot 7 - (-1)\\cdot 5\\right|") != nullptr);
		assert(std::strstr(g_latex, "= \\frac{1}{2}\\left|-9\\right| = \\frac{9}{2}\\end{array}$$") != nullptr);

		// a triangle has no fourth vertex
		const std::int64_t four[] = {0, 1, 0, 1, 0, 0, 1, 1};
		MatrixMutView q = mat(h.persist, 2, 4, four);
		assert(matrix_core::op_area(q.view(), AreaShape::Triangle, h.scratch, &area, nullptr, ExplainOptions{}).code ==
		        ErrorCode::InvalidDimension);
}

// worksheet 4(a) and 4(b): P spanned by (2,-5), (4,1); T(x) = Ax, A = [3 1; 1 2]
void transform() {
		Harness h;
		const std::int64_t pv[] = {2, 4, -5, 1};
		MatrixMutView p = mat(h.persist, 2, 2, pv);
		Rational area;
		Explanation expl;
		assert(matrix_core::is_ok(matrix_core::op_area(p.view(), AreaShape::Parallelogram, h.scratch, &area, &expl, h.opts())));
		assert(is(area, 22));
		assert(expl.step_count() == 2);
		render(h, expl, 0);
		assert(std::strcmp(g_caption, "Columns are the edge vectors") == 0);
		assert(std::strcmp(g_latex,
		               "$$\\vec{u} = \\begin{bmatrix}2 \\\\ -5\\end{bmatrix},\\quad \\vec{v} = \\begin{bmatrix}4 \\\\ 1\\end{bmatrix}$$") ==
		        0);

		const std::int64_t av[] = {3, 1, 1, 2};
		MatrixMutView a = mat(h.persist, 2, 2, av);
		Rational det_a;
		Rational area_p;
		Rational area_tp;
		assert(matrix_core::is_ok(
		        matrix_core::op_area_transform(a.view(), p.view(), h.scratch, &det_a, &area_p, &area_tp, &expl, h.opts())));
		assert(is(det_a, 5) && is(area_p, 22) && is(area_tp, 110));

		// u v, area P, images, vertices of T(P), area T(P), det A, check
		assert(expl.step_count() == 7);
		render_all(h, expl);
		render(h, expl, 1);
		assert(std::strstr(g_latex, "\\text{Area}(P) = ") != nullptr);
		render(h, expl, 2);
		assert(std::strstr(g_latex,
		               "A\\vec{u} = \\begin{bmatrix}3\\cdot 2 + 1\\cdot (-5) \\\\ 1\\cdot 2 + 2\\cdot (-5)\\end{bmatrix} = "
		               "\\begin{bmatrix}1 \\\\ -8\\end{bmatrix}") != nullptr);
		render(h, expl, 3);
		assert(std::strcmp(g_latex,
		               "$$\\begin{array}{c}\\vec{0} = (0, 0) \\\\ A\\vec{u} = (1, -8) \\\\ A\\vec{u} + A\\vec{v} = (14, -2) \\\\ "
		               "A\\vec{v} = (13, 6)\\end{array}$$") == 0);
		render(h, expl, 4);
		assert(std::strstr(g_latex, "\\text{Area}(T(P)) = \\left|\\det\\begin{bmatrix}1 & 13 \\\\ -8 & 6\\end{bmatrix}\\right|") !=
		        nullptr);
		assert(std::strstr(g_latex, "\\left|110\\right| = 110") != nullptr);
		render(h, expl, 5);
		assert(std::strcmp(g_latex, "$$\\det(A) = 3\\cdot 2 - 1\\cdot 1 = 5$$") == 0);
		render(h, expl, 6);
		assert(std::strstr(g_latex, "= \\left|5\\right|\\cdot 22 = 110") != nullptr);

		// the polygons the sketch draws
		MatrixMutView poly;
		AreaRegion r;
		assert(matrix_core::area_region(p.view(), AreaShape::Parallelogram, &r) == ErrorCode::Ok);
		assert(matrix_core::matrix_alloc(h.persist, 2, 4, &poly) == ErrorCode::Ok);
		assert(matrix_core::area_polygon(r, AreaShape::Parallelogram, nullptr, poly) == ErrorCode::Ok);
		const std::int64_t want_p[] = {0, 2, 6, 4, 0, -5, -4, 1};
		for (std::uint8_t i = 0; i < 8; i++)
				assert(is(poly.at(static_cast<std::uint8_t>(i / 4), static_cast<std::uint8_t>(i % 4)), want_p[i]));
		const MatrixView av_view = a.view();
		assert(matrix_core::area_polygon(r, AreaShape::Parallelogram, &av_view, poly) == ErrorCode::Ok);
		const std::int64_t want_tp[] = {0, 1, 14, 13, 0, -8, -2, 6};
		for (std::uint8_t i = 0; i < 8; i++)
				assert(is(poly.at(static_cast<std::uint8_t>(i / 4), static_cast<std::uint8_t>(i % 4)), want_tp[i]));

		// P by its vertices: T(P) starts at A P_1
		const std::int64_t pverts[] = {1, 3, 7, 5, 1, -4, -3, 2};
		MatrixMutView pv4 = mat(h.persist, 2, 4, pverts);
		assert(matrix_core::is_ok(
		        matrix_core::op_area_transform(a.view(), pv4.view(), h.scratch, &det_a, &area_p, &area_tp, &expl, h.opts())));
		assert(is(area_p, 22) && is(area_tp, 110));
		assert(expl.step_count() == 9);
		render_all(h, expl);
		render(h, expl, 5);
		assert(std::strstr(g_latex, "AP_1 = (4, 3) \\\\ AP_1 + A\\vec{u} = ") != nullptr);

		// A must be 2x2
		MatrixMutView a3;
		assert(matrix_core::matrix_alloc(h.persist, 3, 3, &a3) == ErrorCode::Ok);
		assert(matrix_core::op_area_transform(a3.view(), p.view(), h.scratch, &det_a, &area_p, &area_tp, nullptr, ExplainOptions{}).code ==
		        ErrorCode::InvalidDimension);

		// the polygons come back with the result too
		MatrixMutView both;
		assert(matrix_core::matrix_alloc(h.persist, 4, 4, &both) == ErrorCode::Ok);
		assert(matrix_core::is_ok(matrix_core::op_area_transform(a.view(),
		        p.view(),
		        h.scratch,
		        &det_a,
		        &area_p,
		        &area_tp,
		        nullptr,
		        ExplainOptions{},
		        matrix_core::AreaLabels{},
		        MatrixMutView{2, 4, both.stride, both.data},
		        MatrixMutView{2, 4, both.stride, both.data + 2u * both.stride})));
		assert(is(both.at(0, 2), 6) && is(both.at(1, 2), -4) && is(both.at(2, 2), 14) && is(both.at(3, 2), -2));
}

} // namespace

int main() {
		parallelogram_vertices();
		triangle();
		transform();
		return 0;
}
