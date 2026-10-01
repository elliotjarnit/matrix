// LU factorization and LU solve, including the T1-T7 cases from the feature
// request. Values are checked as exact rationals; the step captions are checked
// as substrings of the rendered LaTeX.
#include "matrix_core/matrix_core.hpp"
#include "matrix_core/ops_lu.hpp"

#include "test_dbg_ce.hpp"

#include <cassert>
#include <cstring>

#if defined(MATRIX_CE_TESTS)
#include <debug.h>
#endif

using matrix_core::Arena;
using matrix_core::ErrorCode;
using matrix_core::ExplainOptions;
using matrix_core::Explanation;
using matrix_core::LuLabels;
using matrix_core::LuPivoting;
using matrix_core::LuSolveInfo;
using matrix_core::MatrixMutView;
using matrix_core::MatrixView;
using matrix_core::Rational;
using matrix_core::Slab;
using matrix_core::StepRenderBuffers;

namespace {

// a rational written as num/den, so expectations stay exact
struct Frac {
		std::int64_t num = 0;
		std::int64_t den = 1;
};

MatrixMutView make(Arena& a, std::uint8_t rows, std::uint8_t cols, const std::int64_t* values) {
		MatrixMutView m;
		assert(matrix_core::matrix_alloc(a, rows, cols, &m) == ErrorCode::Ok);
		for (std::uint8_t r = 0; r < rows; r++) {
				for (std::uint8_t c = 0; c < cols; c++)
						m.at_mut(r, c) = Rational::from_int(values[static_cast<std::size_t>(r) * cols + c]);
		}
		return m;
}

MatrixMutView alloc(Arena& a, std::uint8_t rows, std::uint8_t cols) {
		MatrixMutView m;
		assert(matrix_core::matrix_alloc(a, rows, cols, &m) == ErrorCode::Ok);
		return m;
}

void assert_frac(const Rational& v, Frac expected) {
		assert(v.num() == expected.num && v.den() == expected.den);
}

void assert_matrix(MatrixView m, const Frac* expected) {
		for (std::uint8_t r = 0; r < m.rows; r++) {
				for (std::uint8_t c = 0; c < m.cols; c++)
						assert_frac(m.at(r, c), expected[static_cast<std::size_t>(r) * m.cols + c]);
		}
}

void assert_equal(MatrixView got, MatrixView want) {
		assert(got.rows == want.rows && got.cols == want.cols);
		for (std::uint8_t r = 0; r < got.rows; r++) {
				for (std::uint8_t c = 0; c < got.cols; c++)
						assert(got.at(r, c).num() == want.at(r, c).num() && got.at(r, c).den() == want.at(r, c).den());
		}
}

// left * right, allocated in `scratch`
MatrixView product(Arena& scratch, MatrixView left, MatrixView right) {
		MatrixMutView out = alloc(scratch, left.rows, right.cols);
		assert(matrix_core::matrix_mul(left, right, out) == ErrorCode::Ok);
		return out.view();
}

#if defined(MATRIX_CE_TESTS)
char g_caption[128];
char g_latex[1024];
#endif

// one set of render buffers, reused by every check below
struct Steps {
		explicit Steps(Arena& scratch) noexcept : scratch_(&scratch) {}

		ErrorCode render(const Explanation& e, std::size_t index) noexcept {
				scratch_->clear();
				StepRenderBuffers bufs{caption, sizeof(caption), latex, sizeof(latex), scratch_};
				return e.render_step(index, bufs);
		}

		// renders every step and checks that one past the end is refused
		void render_all(const Explanation& e) noexcept {
				const std::size_t n = e.step_count();
				for (std::size_t i = 0; i < n; i++)
						assert(render(e, i) == ErrorCode::Ok);
				assert(render(e, n) == ErrorCode::StepOutOfRange);
		}

		void expect_caption(const Explanation& e, std::size_t index, const char* needle) noexcept {
				assert(render(e, index) == ErrorCode::Ok);
				assert(std::strstr(caption, needle) != nullptr);
		}

		void expect_latex(const Explanation& e, std::size_t index, const char* needle) noexcept {
				assert(render(e, index) == ErrorCode::Ok);
				assert(std::strstr(latex, needle) != nullptr);
		}

#if defined(MATRIX_CE_TESTS)
		char* const caption = g_caption;
		char* const latex = g_latex;
#else
		char caption[128]{};
		char latex[1024]{};
#endif

	  private:
		Arena* scratch_ = nullptr;
};

// everything op_lu_solve needs to write into
struct SolveOut {
		MatrixMutView y{};
		MatrixMutView x{};
		MatrixMutView rref{};
};

SolveOut make_solve_out(Arena& persist, std::uint8_t m, std::uint8_t n) {
		SolveOut o;
		o.y = alloc(persist, m, 1);
		o.x = alloc(persist, n, 1);
		o.rref = alloc(persist, m, static_cast<std::uint8_t>(n + 1u));
		return o;
}

} // namespace

int main() {
		Slab slab;
		assert(slab.init(256 * 1024) == ErrorCode::Ok);

		Arena persist(slab.data(), slab.size() / 2);
		Arena scratch(slab.data() + (slab.size() / 2), slab.size() - (slab.size() / 2));
		Steps steps(scratch);

		// ---------------------------------------------------------------------
		// T1 - basic 3x3, factorization and solve
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {2, 1, 1, 4, 3, 3, 8, 7, 9};
				const std::int64_t bv[] = {4, 10, 24};
				MatrixMutView a = make(persist, 3, 3, av);
				MatrixMutView b = make(persist, 3, 1, bv);

				MatrixMutView l = alloc(persist, 3, 3);
				MatrixMutView u = alloc(persist, 3, 3);

				Explanation expl;
				auto err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::None, {}, l, u, nullptr, &expl, ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));

				const Frac lv[] = {{1}, {0}, {0}, {2}, {1}, {0}, {4}, {3}, {1}};
				const Frac uv[] = {{2}, {1}, {1}, {0}, {1}, {1}, {0}, {0}, {2}};
				assert_matrix(l.view(), lv);
				assert_matrix(u.view(), uv);

				scratch.clear();
				assert_equal(product(scratch, l.view(), u.view()), a.view());

				// A, 3 row ops, L, U, LU = A, A = LU
				assert(expl.step_count() == 8);
				steps.expect_latex(expl, 0, "$$A = \\begin{bmatrix}2 & 1 & 1");
				steps.expect_caption(expl, 1, "R_{2} \\leftarrow R_{2} - 2R_{1} \\quad (\\ell_{21} = 2)");
				steps.expect_caption(expl, 2, "R_{3} \\leftarrow R_{3} - 4R_{1} \\quad (\\ell_{31} = 4)");
				steps.expect_caption(expl, 3, "R_{3} \\leftarrow R_{3} - 3R_{2} \\quad (\\ell_{32} = 3)");
				steps.expect_latex(expl, 1, "$$L = ");
				steps.expect_latex(expl, 1, "$$U = ");
				steps.expect_latex(expl, 4, "$$L = ");
				steps.expect_latex(expl, 5, "$$U = ");
				steps.expect_caption(expl, 6, "$LU = A$");
				steps.expect_latex(expl, 6, "$$LU = \\begin{bmatrix}2 & 1 & 1");
				steps.expect_caption(expl, 7, "$A = LU$");
				steps.render_all(expl);

				SolveOut o = make_solve_out(persist, 3, 3);
				LuSolveInfo info{};
				Explanation sexpl;
				err = matrix_core::op_lu_solve(a.view(),
				        b.view(),
				        LuPivoting::None,
				        scratch,
				        o.y,
				        o.x,
				        o.rref,
				        &info,
				        nullptr,
				        &sexpl,
				        ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));
				assert(info.unique && info.consistent && !info.permuted);
				assert(info.rank == 3 && info.nullity == 0);

				const Frac yv[] = {{4}, {2}, {2}};
				const Frac xv[] = {{1}, {1}, {1}};
				assert_matrix(o.y.view(), yv);
				assert_matrix(o.x.view(), xv);

				// A, 3 ops, L, U | y1..y3, y | x3..x1, x | Ax = b, summary
				assert(sexpl.step_count() == 6 + 4 + 4 + 2);
				steps.expect_caption(sexpl, 7, "y_{2} = b_{2} - \\ell_{21}y_{1}");
				steps.expect_latex(sexpl, 7, "10 - 2\\cdot 4 = 2");
				steps.expect_latex(sexpl, 9, "$$y = ");
				steps.expect_caption(sexpl, 12, "x_{1} = (y_{1} - u_{12}x_{2} - u_{13}x_{3}) / u_{11}");
				steps.expect_latex(sexpl, 12, "\\frac{4 - 1\\cdot 1 - 1\\cdot 1}{2} = 1");
				steps.expect_latex(sexpl, 13, "$$x = ");
				steps.expect_caption(sexpl, 14, "$Ax = b$");
				steps.expect_latex(sexpl, 14, "$$Ax = \\begin{bmatrix}4 \\\\ 10 \\\\ 24\\end{bmatrix}$$");
				steps.expect_latex(sexpl, 15, "$$L = ");
				steps.expect_latex(sexpl, 15, "$$x = ");
				steps.render_all(sexpl);
		}

		// ---------------------------------------------------------------------
		// T2 - fraction multipliers
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {3, 1, 1, 2};
				const std::int64_t bv[] = {5, 5};
				MatrixMutView a = make(persist, 2, 2, av);
				MatrixMutView b = make(persist, 2, 1, bv);

				MatrixMutView l = alloc(persist, 2, 2);
				MatrixMutView u = alloc(persist, 2, 2);
				Explanation expl;
				auto err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::None, {}, l, u, nullptr, &expl, ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));

				const Frac lv[] = {{1}, {0}, {1, 3}, {1}};
				const Frac uv[] = {{3}, {1}, {0}, {5, 3}};
				assert_matrix(l.view(), lv);
				assert_matrix(u.view(), uv);
				steps.expect_caption(expl, 1, "\\ell_{21} = \\frac{1}{3}");

				scratch.clear();
				assert_equal(product(scratch, l.view(), u.view()), a.view());

				SolveOut o = make_solve_out(persist, 2, 2);
				LuSolveInfo info{};
				Explanation sexpl;
				err = matrix_core::op_lu_solve(a.view(),
				        b.view(),
				        LuPivoting::None,
				        scratch,
				        o.y,
				        o.x,
				        o.rref,
				        &info,
				        nullptr,
				        &sexpl,
				        ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));

				const Frac yv[] = {{5}, {10, 3}};
				const Frac xv[] = {{1}, {2}};
				assert_matrix(o.y.view(), yv);
				assert_matrix(o.x.view(), xv);
				steps.render_all(sexpl);
		}

		// ---------------------------------------------------------------------
		// T3 - 4x4 with a negative and a fraction multiplier
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {2, 1, 0, 1, 4, 3, 1, 3, -2, 1, 4, 2, 6, 5, 5, 10};
				const std::int64_t bv[] = {2, 6, 7, 21};
				MatrixMutView a = make(persist, 4, 4, av);
				MatrixMutView b = make(persist, 4, 1, bv);

				MatrixMutView l = alloc(persist, 4, 4);
				MatrixMutView u = alloc(persist, 4, 4);
				Explanation expl;
				auto err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::None, {}, l, u, nullptr, &expl, ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));

				const Frac lv[] = {{1}, {0}, {0}, {0}, {2}, {1}, {0}, {0}, {-1}, {2}, {1}, {0}, {3}, {2}, {3, 2}, {1}};
				const Frac uv[] = {{2}, {1}, {0}, {1}, {0}, {1}, {1}, {1}, {0}, {0}, {2}, {1}, {0}, {0}, {0}, {7, 2}};
				assert_matrix(l.view(), lv);
				assert_matrix(u.view(), uv);

				scratch.clear();
				assert_equal(product(scratch, l.view(), u.view()), a.view());

				steps.expect_caption(expl, 1, "R_{2} \\leftarrow R_{2} - 2R_{1}");
				// a negative multiplier is shown as an addition
				steps.expect_caption(expl, 2, "R_{3} \\leftarrow R_{3} + R_{1} \\quad (\\ell_{31} = -1)");
				steps.expect_caption(expl, 3, "R_{4} \\leftarrow R_{4} - 3R_{1}");
				steps.expect_caption(expl, 4, "R_{3} \\leftarrow R_{3} - 2R_{2}");
				steps.expect_caption(expl, 5, "R_{4} \\leftarrow R_{4} - 2R_{2}");
				steps.expect_caption(expl, 6, "R_{4} \\leftarrow R_{4} - \\frac{3}{2}R_{3} \\quad (\\ell_{43} = \\frac{3}{2})");
				steps.render_all(expl);

				SolveOut o = make_solve_out(persist, 4, 4);
				LuSolveInfo info{};
				Explanation sexpl;
				err = matrix_core::op_lu_solve(a.view(),
				        b.view(),
				        LuPivoting::None,
				        scratch,
				        o.y,
				        o.x,
				        o.rref,
				        &info,
				        nullptr,
				        &sexpl,
				        ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));
				assert(info.unique);

				const Frac yv[] = {{2}, {2}, {5}, {7, 2}};
				const Frac xv[] = {{1}, {-1}, {2}, {1}};
				assert_matrix(o.y.view(), yv);
				assert_matrix(o.x.view(), xv);
				steps.render_all(sexpl);
		}

		// ---------------------------------------------------------------------
		// T4 - rectangular 3x4 with a skipped column
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {2, 4, -1, 5, -4, -5, 3, -8, 2, -5, -4, 1};
				MatrixMutView a = make(persist, 3, 4, av);

				MatrixMutView l = alloc(persist, 3, 3);
				MatrixMutView u = alloc(persist, 3, 4);
				Explanation expl;
				auto err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::None, {}, l, u, nullptr, &expl, ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));

				const Frac lv[] = {{1}, {0}, {0}, {-2}, {1}, {0}, {1}, {-3}, {1}};
				const Frac uv[] = {{2}, {4}, {-1}, {5}, {0}, {3}, {1}, {2}, {0}, {0}, {0}, {2}};
				assert_matrix(l.view(), lv);
				assert_matrix(u.view(), uv);

				scratch.clear();
				assert_equal(product(scratch, l.view(), u.view()), a.view());

				steps.expect_caption(expl, 1, "R_{2} \\leftarrow R_{2} + 2R_{1} \\quad (\\ell_{21} = -2)");
				steps.expect_caption(expl, 2, "R_{3} \\leftarrow R_{3} - R_{1} \\quad (\\ell_{31} = 1)");
				steps.expect_caption(expl, 3, "R_{3} \\leftarrow R_{3} + 3R_{2} \\quad (\\ell_{32} = -3)");
				steps.expect_caption(expl, 4, "\\text{col }3");
				steps.expect_caption(expl, 4, "no pivot, skip column");
				steps.render_all(expl);
		}

		// ---------------------------------------------------------------------
		// T5 - a row swap is required
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {0, 1, 1, 1};
				MatrixMutView a = make(persist, 2, 2, av);
				MatrixMutView l = alloc(persist, 2, 2);
				MatrixMutView u = alloc(persist, 2, 2);

				auto err = matrix_core::op_lu_factor(a.view(), LuPivoting::None, {}, l, u, nullptr, nullptr, ExplainOptions{});
				assert(!matrix_core::is_ok(err));
				assert(err.code == ErrorCode::RowSwapRequired);
				assert(err.i == 0); // pivot row
				assert(err.j == 0); // column 1, reported 0 based

				// with partial pivoting the same matrix factors as PA = LU
				MatrixMutView p = alloc(persist, 2, 2);
				Explanation expl;
				err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::Partial, p, l, u, nullptr, &expl, ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));

				const Frac pv[] = {{0}, {1}, {1}, {0}};
				const Frac lv[] = {{1}, {0}, {0}, {1}};
				const Frac uv[] = {{1}, {1}, {0}, {1}};
				assert_matrix(p.view(), pv);
				assert_matrix(l.view(), lv);
				assert_matrix(u.view(), uv);

				scratch.clear();
				{
						MatrixView pa = product(scratch, p.view(), a.view());
						MatrixView lu = product(scratch, l.view(), u.view());
						assert_equal(pa, lu);
				}

				// the swap is a step of its own, and P gets its own screen
				steps.expect_caption(expl, 1, "R_{1} <-> R_{2}");
				steps.expect_caption(expl, 1, "swap rows");
				steps.expect_latex(expl, 2, "$$P = ");
				steps.expect_caption(expl, expl.step_count() - 2, "$PA = LU$");
				steps.expect_latex(expl, expl.step_count() - 2, "$$PA = ");
				steps.expect_latex(expl, expl.step_count() - 2, "$$LU = ");
				steps.render_all(expl);

				// and the solve routes b through P
				const std::int64_t bv[] = {3, 5};
				MatrixMutView b = make(persist, 2, 1, bv);
				SolveOut o = make_solve_out(persist, 2, 2);
				LuSolveInfo info{};
				Explanation sexpl;
				err = matrix_core::op_lu_solve(a.view(),
				        b.view(),
				        LuPivoting::Partial,
				        scratch,
				        o.y,
				        o.x,
				        o.rref,
				        &info,
				        nullptr,
				        &sexpl,
				        ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));
				assert(info.permuted && info.unique);

				// A x = b with A = [[0,1],[1,1]] and b = [3,5] gives x = [2,3]
				const Frac yv[] = {{5}, {3}};
				const Frac xv[] = {{2}, {3}};
				assert_matrix(o.y.view(), yv);
				assert_matrix(o.x.view(), xv);

				scratch.clear();
				assert_equal(product(scratch, a.view(), o.x.view()), b.view());

				// A, the swap, P, L, U, then Pb and the forward substitution
				steps.expect_latex(sexpl, 5, "$$Pb = ");
				steps.expect_caption(sexpl, 6, "y_{1} = (Pb)_{1}");
				steps.render_all(sexpl);

				// LuPivoting::None still refuses the same system
				err = matrix_core::op_lu_solve(
				        a.view(), b.view(), LuPivoting::None, scratch, o.y, o.x, o.rref, &info, nullptr, nullptr, ExplainOptions{});
				assert(err.code == ErrorCode::RowSwapRequired);
		}

		// ---------------------------------------------------------------------
		// A swap after some multipliers are recorded: they must move with the rows
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {1, 1, 1, 2, 2, 5, 4, 6, 8};
				MatrixMutView a = make(persist, 3, 3, av);
				MatrixMutView p = alloc(persist, 3, 3);
				MatrixMutView l = alloc(persist, 3, 3);
				MatrixMutView u = alloc(persist, 3, 3);

				Explanation expl;
				auto err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::Partial, p, l, u, nullptr, &expl, ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));

				// R2 and R3 swap after l21 = 2 and l31 = 4 were recorded, so they swap too
				const Frac pv[] = {{1}, {0}, {0}, {0}, {0}, {1}, {0}, {1}, {0}};
				const Frac lv[] = {{1}, {0}, {0}, {4}, {1}, {0}, {2}, {0}, {1}};
				const Frac uv[] = {{1}, {1}, {1}, {0}, {2}, {4}, {0}, {0}, {3}};
				assert_matrix(p.view(), pv);
				assert_matrix(l.view(), lv);
				assert_matrix(u.view(), uv);

				scratch.clear();
				{
						MatrixView pa = product(scratch, p.view(), a.view());
						MatrixView lu = product(scratch, l.view(), u.view());
						assert_equal(pa, lu);
				}
				steps.render_all(expl);
		}

		// ---------------------------------------------------------------------
		// T6 - singular square matrix, consistent and inconsistent
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {1, 2, 2, 4};
				MatrixMutView a = make(persist, 2, 2, av);

				MatrixMutView l = alloc(persist, 2, 2);
				MatrixMutView u = alloc(persist, 2, 2);
				auto err = matrix_core::op_lu_factor(a.view(), LuPivoting::None, {}, l, u, nullptr, nullptr, ExplainOptions{});
				assert(matrix_core::is_ok(err));

				const Frac lv[] = {{1}, {0}, {2}, {1}};
				const Frac uv[] = {{1}, {2}, {0}, {0}};
				assert_matrix(l.view(), lv);
				assert_matrix(u.view(), uv);

				// consistent: infinitely many solutions
				{
						const std::int64_t bv[] = {3, 6};
						MatrixMutView b = make(persist, 2, 1, bv);
						SolveOut o = make_solve_out(persist, 2, 2);
						LuSolveInfo info{};
						Explanation sexpl;
						err = matrix_core::op_lu_solve(a.view(),
						        b.view(),
						        LuPivoting::None,
						        scratch,
						        o.y,
						        o.x,
						        o.rref,
						        &info,
						        nullptr,
						        &sexpl,
						        ExplainOptions{.enable = true, .persist = &persist});
						assert(matrix_core::is_ok(err));
						assert(!info.unique && info.consistent);
						assert(info.rank == 1 && info.nullity == 1);

						const Frac yv[] = {{3}, {0}};
						const Frac xv[] = {{3}, {0}};
						assert_matrix(o.y.view(), yv);
						assert_matrix(o.x.view(), xv);

						// x = [3,0] + s[-2,1]
						matrix_core::SpaceInfo space{};
						assert(matrix_core::space_info_from_rref(o.rref.view(), 2, &space) == ErrorCode::Ok);
						scratch.clear();
						MatrixMutView basis{};
						assert(matrix_core::space_null_basis(o.rref.view(), 2, space, scratch, &basis) == ErrorCode::Ok);
						assert(basis.cols == 1);
						assert_frac(basis.at(0, 0), {-2});
						assert_frac(basis.at(1, 0), {1});

						// the answer step is followed by the Ax_p = b check and a summary
						const std::size_t answer = sexpl.step_count() - 3;
						steps.expect_caption(sexpl, answer, "particular solution");
						steps.expect_caption(sexpl, answer, "homogeneous solution");
						steps.expect_latex(sexpl, answer, "$$x = x_{p} + sv_{1}$$");
						steps.expect_latex(sexpl, answer, "s = x_{2}");
						steps.expect_caption(sexpl, answer + 1, "$Ax_{p} = b$");
						steps.expect_latex(sexpl, answer + 1, "$$Ax_{p} = \\begin{bmatrix}3 \\\\ 6\\end{bmatrix}$$");
						steps.expect_latex(sexpl, answer + 2, "$$x_{p} = ");
						steps.render_all(sexpl);
				}

				// inconsistent: no solution
				{
						const std::int64_t bv[] = {3, 7};
						MatrixMutView b = make(persist, 2, 1, bv);
						SolveOut o = make_solve_out(persist, 2, 2);
						LuSolveInfo info{};
						Explanation sexpl;
						err = matrix_core::op_lu_solve(a.view(),
						        b.view(),
						        LuPivoting::None,
						        scratch,
						        o.y,
						        o.x,
						        o.rref,
						        &info,
						        nullptr,
						        &sexpl,
						        ExplainOptions{.enable = true, .persist = &persist});
						assert(matrix_core::is_ok(err));
						assert(!info.consistent);
						assert(info.bad_row == 1);

						const Frac yv[] = {{3}, {1}};
						assert_matrix(o.y.view(), yv);
						assert_frac(o.x.at(0, 0), {0});
						assert_frac(o.x.at(1, 0), {0});

						// no solution means no check and no summary
						steps.expect_caption(sexpl, sexpl.step_count() - 1, "inconsistent: no solution");
						steps.expect_latex(sexpl, sexpl.step_count() - 1, "\\begin{array}{rr|r}0 & 0 & 1\\end{array}");
						steps.render_all(sexpl);
				}
		}

		// ---------------------------------------------------------------------
		// T7 - edge cases: 1x1 and an all zero matrix
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {5};
				const std::int64_t bv[] = {10};
				MatrixMutView a = make(persist, 1, 1, av);
				MatrixMutView b = make(persist, 1, 1, bv);

				MatrixMutView l = alloc(persist, 1, 1);
				MatrixMutView u = alloc(persist, 1, 1);
				Explanation expl;
				auto err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::None, {}, l, u, nullptr, &expl, ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));
				assert_frac(l.at(0, 0), {1});
				assert_frac(u.at(0, 0), {5});
				steps.render_all(expl);

				SolveOut o = make_solve_out(persist, 1, 1);
				LuSolveInfo info{};
				Explanation sexpl;
				err = matrix_core::op_lu_solve(a.view(),
				        b.view(),
				        LuPivoting::None,
				        scratch,
				        o.y,
				        o.x,
				        o.rref,
				        &info,
				        nullptr,
				        &sexpl,
				        ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));
				assert(info.unique);
				assert_frac(o.x.at(0, 0), {2});
				steps.render_all(sexpl);
		}
		{
				const std::int64_t av[] = {0, 0, 0, 0};
				MatrixMutView a = make(persist, 2, 2, av);

				MatrixMutView l = alloc(persist, 2, 2);
				MatrixMutView u = alloc(persist, 2, 2);
				Explanation expl;
				auto err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::None, {}, l, u, nullptr, &expl, ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));

				const Frac lv[] = {{1}, {0}, {0}, {1}};
				const Frac uv[] = {{0}, {0}, {0}, {0}};
				assert_matrix(l.view(), lv);
				assert_matrix(u.view(), uv);
				// both columns are skipped
				assert(expl.step_count() == 2 + 5);
				steps.render_all(expl);

				// partial pivoting has nothing to swap either
				MatrixMutView p = alloc(persist, 2, 2);
				err = matrix_core::op_lu_factor(a.view(), LuPivoting::Partial, p, l, u, nullptr, nullptr, ExplainOptions{});
				assert(matrix_core::is_ok(err));
				assert_matrix(p.view(), lv); // identity

				// a zero system is consistent only for b = 0
				const std::int64_t zv[] = {0, 0};
				MatrixMutView zero_b = make(persist, 2, 1, zv);
				SolveOut o = make_solve_out(persist, 2, 2);
				LuSolveInfo info{};
				Explanation sexpl;
				err = matrix_core::op_lu_solve(a.view(),
				        zero_b.view(),
				        LuPivoting::None,
				        scratch,
				        o.y,
				        o.x,
				        o.rref,
				        &info,
				        nullptr,
				        &sexpl,
				        ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));
				assert(info.consistent && !info.unique);
				assert(info.rank == 0 && info.nullity == 2);
				steps.render_all(sexpl);

				const std::int64_t nzv[] = {0, 1};
				MatrixMutView nonzero_b = make(persist, 2, 1, nzv);
				err = matrix_core::op_lu_solve(a.view(),
				        nonzero_b.view(),
				        LuPivoting::None,
				        scratch,
				        o.y,
				        o.x,
				        o.rref,
				        &info,
				        nullptr,
				        &sexpl,
				        ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));
				assert(!info.consistent);
				steps.render_all(sexpl);
		}

		// ---------------------------------------------------------------------
		// Caller supplied labels replace the English defaults
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {1, 2, 3, 0, 0, 1};
				MatrixMutView a = make(persist, 2, 3, av);
				MatrixMutView l = alloc(persist, 2, 2);
				MatrixMutView u = alloc(persist, 2, 3);

				LuLabels labels{};
				labels.skip_column = "pas de pivot";

				Explanation expl;
				auto err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::None, {}, l, u, &labels, &expl, ExplainOptions{.enable = true, .persist = &persist});
				assert(matrix_core::is_ok(err));
				steps.expect_caption(expl, 1, "pas de pivot");
		}

		// ---------------------------------------------------------------------
		// Dimension validation and explanation options
		// ---------------------------------------------------------------------
		{
				const std::int64_t av[] = {1, 2, 3, 4};
				MatrixMutView a = make(persist, 2, 2, av);
				MatrixMutView ok_2x2 = alloc(persist, 2, 2);
				MatrixMutView bad = alloc(persist, 1, 2);

				auto err = matrix_core::op_lu_factor(a.view(), LuPivoting::None, {}, bad, ok_2x2, nullptr, nullptr, ExplainOptions{});
				assert(err.code == ErrorCode::DimensionMismatch);
				err = matrix_core::op_lu_factor(a.view(), LuPivoting::None, {}, ok_2x2, bad, nullptr, nullptr, ExplainOptions{});
				assert(err.code == ErrorCode::DimensionMismatch);
				err = matrix_core::op_lu_factor(a.view(), LuPivoting::Partial, bad, ok_2x2, ok_2x2, nullptr, nullptr, ExplainOptions{});
				assert(err.code == ErrorCode::DimensionMismatch);

				err = matrix_core::op_lu_factor(a.view(),
				        LuPivoting::None,
				        {},
				        ok_2x2,
				        ok_2x2,
				        nullptr,
				        nullptr,
				        ExplainOptions{.enable = true, .persist = &persist});
				assert(err.code == ErrorCode::Internal);

				Explanation expl;
				err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::None, {}, ok_2x2, ok_2x2, nullptr, &expl, ExplainOptions{.enable = true, .persist = nullptr});
				assert(err.code == ErrorCode::Internal);

				std::uint8_t tiny_buf[4] = {};
				Arena tiny(tiny_buf, sizeof(tiny_buf));
				err = matrix_core::op_lu_factor(
				        a.view(), LuPivoting::None, {}, ok_2x2, ok_2x2, nullptr, &expl, ExplainOptions{.enable = true, .persist = &tiny});
				assert(err.code == ErrorCode::Overflow);

				// b must be m x 1
				SolveOut o = make_solve_out(persist, 2, 2);
				LuSolveInfo info{};
				MatrixMutView wrong_b = alloc(persist, 3, 1);
				err = matrix_core::op_lu_solve(
				        a.view(), wrong_b.view(), LuPivoting::None, scratch, o.y, o.x, o.rref, &info, nullptr, nullptr, ExplainOptions{});
				assert(err.code == ErrorCode::DimensionMismatch);
		}

		return 0;
}
