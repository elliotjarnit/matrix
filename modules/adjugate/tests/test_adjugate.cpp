#include "matrix_core/matrix_core.hpp"
#include "matrix_core/ops_adjugate.hpp"

#include "test_dbg_ce.hpp"

#include <cassert>
#include <cstring>

using matrix_core::AdjugateLabels;
using matrix_core::Arena;
using matrix_core::ErrorCode;
using matrix_core::ExplainOptions;
using matrix_core::Explanation;
using matrix_core::MatrixMutView;
using matrix_core::Rational;
using matrix_core::Slab;
using matrix_core::StepRenderBuffers;

namespace {

MatrixMutView mat(Arena& arena, std::uint8_t n, const std::int64_t* vals) {
		MatrixMutView m;
		assert(matrix_core::matrix_alloc(arena, n, n, &m) == ErrorCode::Ok);
		for (std::uint8_t r = 0; r < n; r++) {
				for (std::uint8_t c = 0; c < n; c++)
						m.at_mut(r, c) = Rational::from_int(vals[static_cast<std::size_t>(r) * n + c]);
		}
		return m;
}

bool is_int(const Rational& r, std::int64_t v) {
		return r.den() == 1 && r.num() == v;
}

struct Harness {
		Slab slab;
		Arena persist;
		Arena scratch;

		Harness() {
				assert(slab.init(64 * 1024) == ErrorCode::Ok);
				persist = Arena(slab.data(), slab.size() / 2);
				scratch = Arena(slab.data() + (slab.size() / 2), slab.size() - (slab.size() / 2));
		}
};

char g_caption[128];
char g_latex[1024]; // same as App::step_latex_

const char* render(const Explanation& expl, std::size_t index, Arena& scratch) {
		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &scratch};
		assert(expl.render_step(index, bufs) == ErrorCode::Ok);
		return g_latex;
}

// adj(A) with every step rendered
MatrixMutView adjugate(Harness& h, MatrixMutView a, Explanation* expl) {
		MatrixMutView out;
		assert(matrix_core::matrix_alloc(h.persist, a.rows, a.cols, &out) == ErrorCode::Ok);
		const auto err = matrix_core::op_adjugate(a.view(), h.scratch, out, expl, ExplainOptions{.enable = true, .persist = &h.persist});
		assert(matrix_core::is_ok(err));
		assert(expl->available());
		for (std::size_t i = 0; i < expl->step_count(); i++)
				(void)render(*expl, i, h.scratch);
		return out;
}

// A adj(A) = det(A) I
void check_identity(Harness& h, MatrixMutView a, MatrixMutView adj) {
		MatrixMutView prod;
		assert(matrix_core::matrix_alloc(h.persist, a.rows, a.cols, &prod) == ErrorCode::Ok);
		assert(matrix_core::matrix_mul(a.view(), adj.view(), prod) == ErrorCode::Ok);
		Rational det;
		assert(matrix_core::is_ok(matrix_core::op_det(a.view(), h.scratch, &det, nullptr, ExplainOptions{})));
		for (std::uint8_t i = 0; i < a.rows; i++) {
				for (std::uint8_t j = 0; j < a.cols; j++) {
						const Rational& v = prod.at(i, j);
						if (i == j)
								assert(v.num() == det.num() && v.den() == det.den());
						else
								assert(v.is_zero());
				}
		}
}

void three_by_three() {
		Harness h;
		const std::int64_t v[] = {1, 2, 3, 0, 4, 5, 1, 0, 6};
		MatrixMutView a = mat(h.persist, 3, v);
		Explanation expl;
		MatrixMutView adj = adjugate(h, a, &expl);
		const std::int64_t want[] = {24, -12, -2, 5, 3, -5, -4, 2, 4};
		for (std::uint8_t i = 0; i < 3; i++) {
				for (std::uint8_t j = 0; j < 3; j++)
						assert(is_int(adj.at(i, j), want[i * 3 + j]));
		}
		check_identity(h, a, adj);

		// A, nine cofactors, C, adj(A), det, A^-1, det(A^-1)
		assert(expl.step_count() == 15);
		render(expl, 0, h.scratch);
		assert(std::strcmp(g_caption, "adj(A) is the transpose of the matrix of cofactors") == 0);

		// C_12, second in row order
		render(expl, 2, h.scratch);
		assert(std::strcmp(g_caption, "Delete row 1, column 2") == 0);
		assert(std::strstr(g_latex, "\\det(M_{12}) = ") != nullptr);
		assert(std::strstr(g_latex, " \\\\ = 0\\cdot 6 - 5\\cdot 1 = -5 \\\\ ") != nullptr);
		assert(std::strstr(g_latex, "C_{12} = (-1)^{1+2}(-5) = 5\\end{array}$$") != nullptr);

		render(expl, 10, h.scratch);
		assert(std::strcmp(g_caption, "Matrix of cofactors") == 0);
		assert(std::strstr(g_latex, "$$C = ") == g_latex);

		render(expl, 11, h.scratch);
		assert(std::strstr(g_latex, "$$\\text{adj}(A) = C^{T} = ") == g_latex);

		render(expl, 12, h.scratch);
		assert(std::strcmp(g_latex,
		               "$$\\begin{array}{c}\\det(A) = 1\\cdot 24 + 2\\cdot 5 + 3\\cdot (-4) \\\\ = 22 \\\\ "
		               "A^{-1} = \\frac{1}{22}\\text{adj}(A)\\end{array}$$") == 0);

		render(expl, 14, h.scratch);
		assert(std::strcmp(g_caption, "det of the inverse is 1 over det(A)") == 0);
		assert(std::strcmp(g_latex, "$$\\det(A^{-1}) = \\frac{1}{\\det(A)} = \\frac{1}{22} = \\frac{1}{22}$$") == 0);

		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
		assert(expl.render_step(15, bufs) == ErrorCode::StepOutOfRange);
}

// worksheet 2(a): adj(A), det(A), A^-1 and det(A^-1)
void worksheet() {
		Harness h;
		const std::int64_t v[] = {2, 0, 1, 0, 0, 2, 1, 1, 0};
		MatrixMutView a = mat(h.persist, 3, v);
		MatrixMutView adj;
		assert(matrix_core::matrix_alloc(h.persist, 3, 3, &adj) == ErrorCode::Ok);
		Explanation expl;
		Rational det;
		assert(matrix_core::is_ok(matrix_core::op_adjugate(
		        a.view(), h.scratch, adj, &expl, ExplainOptions{.enable = true, .persist = &h.persist}, AdjugateLabels{}, &det)));
		assert(is_int(det, -4));
		const std::int64_t want[] = {-2, 1, 0, 2, -1, -4, 0, -2, 0};
		for (std::uint8_t i = 0; i < 3; i++) {
				for (std::uint8_t j = 0; j < 3; j++)
						assert(is_int(adj.at(i, j), want[i * 3 + j]));
		}
		check_identity(h, a, adj);

		assert(expl.step_count() == 15);
		render(expl, 13, h.scratch);
		assert(std::strcmp(g_caption, "Divide every entry of adj(A) by det(A)") == 0);
		assert(std::strcmp(g_latex,
		               "$$A^{-1} = \\begin{bmatrix}\\frac{1}{2} & \\frac{-1}{4} & 0 \\\\ \\frac{-1}{2} & \\frac{1}{4} & 1 \\\\ 0 & "
		               "\\frac{1}{2} & 0\\end{bmatrix}$$") == 0);
		render(expl, 14, h.scratch);
		assert(std::strcmp(g_latex, "$$\\det(A^{-1}) = \\frac{1}{\\det(A)} = \\frac{1}{-4} = \\frac{-1}{4}$$") == 0);
}

void other_sizes() {
		Harness h;

		const std::int64_t one[] = {-7};
		MatrixMutView a1 = mat(h.persist, 1, one);
		Explanation e1;
		MatrixMutView adj1 = adjugate(h, a1, &e1);
		assert(is_int(adj1.at(0, 0), 1));
		render(e1, 1, h.scratch);
		assert(std::strstr(g_latex, "C_{11} = 1") != nullptr);

		const std::int64_t two[] = {1, 2, 3, 4};
		MatrixMutView a2 = mat(h.persist, 2, two);
		Explanation e2;
		MatrixMutView adj2 = adjugate(h, a2, &e2);
		assert(is_int(adj2.at(0, 0), 4) && is_int(adj2.at(0, 1), -2) && is_int(adj2.at(1, 0), -3) && is_int(adj2.at(1, 1), 1));

		// singular: no inverse line and no inverse steps
		const std::int64_t sing[] = {1, 2, 2, 4};
		MatrixMutView as = mat(h.persist, 2, sing);
		Explanation es;
		(void)adjugate(h, as, &es);
		assert(es.step_count() == 1 + 4 + 3);
		render(es, es.step_count() - 1, h.scratch);
		assert(std::strstr(g_latex, "A^{-1}") == nullptr);

		// worksheet 1(b), 4x4: every cofactor's 3x3 minor is worked out
		const std::int64_t ws[] = {4, -5, 2, 6, 0, 1, 4, 6, 0, -2, 2, 6, 0, 4, -3, 2};
		MatrixMutView a4 = mat(h.persist, 4, ws);
		Explanation e4;
		check_identity(h, a4, adjugate(h, a4, &e4));

		const std::int64_t five[] = {3, 1, -2, 0, 5, 2, 7, -1, 0, 4, 1, 6, -3, 2, 0, 9, 1, 1, 8, -4, 2, 0, -6, 3, 5};
		MatrixMutView a5 = mat(h.persist, 5, five);
		Explanation e5;
		check_identity(h, a5, adjugate(h, a5, &e5));
}

void bad_inputs() {
		Harness h;
		const std::int64_t v[] = {1, 2, 3, 4};
		MatrixMutView a = mat(h.persist, 2, v);
		MatrixMutView out;
		assert(matrix_core::matrix_alloc(h.persist, 2, 2, &out) == ErrorCode::Ok);

		MatrixMutView wide = a;
		wide.cols = 1;
		assert(matrix_core::op_adjugate(wide.view(), h.scratch, out, nullptr, ExplainOptions{}).code == ErrorCode::NotSquare);
		MatrixMutView small = out;
		small.rows = 1;
		assert(matrix_core::op_adjugate(a.view(), h.scratch, small, nullptr, ExplainOptions{}).code == ErrorCode::DimensionMismatch);
		(void)AdjugateLabels{};
}

} // namespace

int main() {
		three_by_three();
		worksheet();
		other_sizes();
		bad_inputs();
		return 0;
}
