#include "matrix_core/matrix_core.hpp"

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
using matrix_core::MatrixMutView;
using matrix_core::MatrixView;
using matrix_core::ParametricSolution;
using matrix_core::Rational;
using matrix_core::Slab;
using matrix_core::StepRenderBuffers;

namespace {

MatrixMutView mat(Arena& arena, std::uint8_t rows, std::uint8_t cols, const std::int64_t* vals) {
		MatrixMutView m;
		assert(matrix_core::matrix_alloc(arena, rows, cols, &m) == ErrorCode::Ok);
		for (std::uint8_t r = 0; r < rows; r++) {
				for (std::uint8_t c = 0; c < cols; c++)
						m.at_mut(r, c) = Rational::from_int(vals[static_cast<std::size_t>(r) * cols + c]);
		}
		return m;
}

Rational rat(std::int64_t num, std::int64_t den) {
		Rational r;
		assert(Rational::make(num, den, &r) == ErrorCode::Ok);
		return r;
}

bool same(const Rational& a, const Rational& b) {
		return a.num() == b.num() && a.den() == b.den();
}

// (A * col)[i], used to check the solution against the original system
Rational apply_row(MatrixView a, MatrixView col, std::uint8_t row) {
		Rational acc = Rational::from_int(0);
		for (std::uint8_t c = 0; c < a.cols; c++) {
				Rational term;
				assert(matrix_core::rational_mul(a.at(row, c), col.at(c, 0), &term) == ErrorCode::Ok);
				assert(matrix_core::rational_add(acc, term, &acc) == ErrorCode::Ok);
		}
		return acc;
}

// A x_p == b, and every basis vector is in Null(A)
void check_solution_satisfies_system(MatrixView a, MatrixView b, MatrixView xp, MatrixView basis, const ParametricSolution& info) {
		for (std::uint8_t r = 0; r < a.rows; r++)
				assert(same(apply_row(a, xp, r), b.at(r, 0)));

		for (std::uint8_t k = 0; k < info.nullity; k++) {
				const MatrixView v{basis.rows, 1, basis.stride, basis.data + k};
				for (std::uint8_t r = 0; r < a.rows; r++)
						assert(apply_row(a, v, r).is_zero());
		}

		assert(info.rank + info.nullity == info.var_count);
}

struct Harness {
		Slab slab;
		Arena persist;
		Arena scratch;

		Harness() : persist(nullptr, 0), scratch(nullptr, 0) {
				assert(slab.init(128 * 1024) == ErrorCode::Ok);
				persist = Arena(slab.data(), slab.size() / 2);
				scratch = Arena(slab.data() + (slab.size() / 2), slab.size() - (slab.size() / 2));
		}
};

char g_caption[256];
char g_latex[1024];

const char* render(const Explanation& expl, std::size_t index, Arena& scratch) {
		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &scratch};
		assert(expl.render_step(index, bufs) == ErrorCode::Ok);
		return g_latex;
}

} // namespace

int main() {
		// Math 210 Review Exam 1a, Q2(a): unique solution
		//   x + 3y = 1 ; 2x + 7y = 1 ; x + 4y = 0   ->   x = 4, y = -1
		{
				Harness h;
				const std::int64_t av[] = {1, 3, 2, 7, 1, 4};
				const std::int64_t bv[] = {1, 1, 0};
				MatrixMutView a = mat(h.persist, 3, 2, av);
				MatrixMutView b = mat(h.persist, 3, 1, bv);

				MatrixMutView xp{}, basis{};
				ParametricSolution info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_solve_parametric(a.view(), b.view(), h.persist, h.scratch, &xp, &basis, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(info.consistent);
				assert(info.var_count == 2);
				assert(info.rank == 2);
				assert(info.nullity == 0);
				assert(same(xp.at(0, 0), Rational::from_int(4)));
				assert(same(xp.at(1, 0), Rational::from_int(-1)));
				check_solution_satisfies_system(a.view(), b.view(), xp.view(), basis.view(), info);

				assert(expl.available());
				const std::size_t n_steps = expl.step_count();
				assert(n_steps >= 4);
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);
				assert(expl.render_step(n_steps, StepRenderBuffers{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch}) ==
				        ErrorCode::StepOutOfRange);

				// no parameters: the homogeneous solution is the zero vector
				assert(std::strcmp(render(expl, n_steps - 2, h.scratch), "$$x_{h} = 0$$") == 0);
				assert(std::strcmp(render(expl, n_steps - 1, h.scratch), "$$x = \\begin{bmatrix}4 \\\\ -1\\end{bmatrix}$$") == 0);
		}

		// Math 210 Review Exam 1a, Q2(b): A is 2x4, two free variables
		//   A = [1 -2 1 6 ; 3 -6 4 22], b = [-2 ; -7]
		{
				Harness h;
				const std::int64_t av[] = {1, -2, 1, 6, 3, -6, 4, 22};
				const std::int64_t bv[] = {-2, -7};
				MatrixMutView a = mat(h.persist, 2, 4, av);
				MatrixMutView b = mat(h.persist, 2, 1, bv);

				MatrixMutView xp{}, basis{};
				ParametricSolution info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_solve_parametric(a.view(), b.view(), h.persist, h.scratch, &xp, &basis, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(info.consistent);
				assert(info.var_count == 4);
				assert(info.rank == 2);
				assert(info.nullity == 2);
				assert(info.pivot_cols[0] == 0 && info.pivot_cols[1] == 2);
				assert(info.free_cols[0] == 1 && info.free_cols[1] == 3);

				// x_p = (-1, 0, -1, 0)
				assert(same(xp.at(0, 0), Rational::from_int(-1)));
				assert(same(xp.at(1, 0), Rational::from_int(0)));
				assert(same(xp.at(2, 0), Rational::from_int(-1)));
				assert(same(xp.at(3, 0), Rational::from_int(0)));

				// v1 = (2, 1, 0, 0), v2 = (-2, 0, -4, 1)
				assert(basis.rows == 4 && basis.cols == 2);
				const std::int64_t v1[] = {2, 1, 0, 0};
				const std::int64_t v2[] = {-2, 0, -4, 1};
				for (std::uint8_t r = 0; r < 4; r++) {
						assert(same(basis.at(r, 0), Rational::from_int(v1[r])));
						assert(same(basis.at(r, 1), Rational::from_int(v2[r])));
				}
				check_solution_satisfies_system(a.view(), b.view(), xp.view(), basis.view(), info);

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);

				assert(std::strcmp(render(expl, n_steps - 3, h.scratch),
				               "$$\\begin{array}{l}x_{1} = -1 + 2x_{2} - 2x_{4} \\\\ x_{3} = -1 - 4x_{4}\\end{array}$$") == 0);
				assert(std::strcmp(render(expl, n_steps - 2, h.scratch),
				               "$$x_{h} = t_{1}\\begin{bmatrix}2 \\\\ 1 \\\\ 0 \\\\ 0\\end{bmatrix} + "
				               "t_{2}\\begin{bmatrix}-2 \\\\ 0 \\\\ -4 \\\\ 1\\end{bmatrix}$$") == 0);
				assert(std::strcmp(render(expl, n_steps - 1, h.scratch),
				               "$$x = \\begin{bmatrix}-1 \\\\ 0 \\\\ -1 \\\\ 0\\end{bmatrix} + "
				               "t_{1}\\begin{bmatrix}2 \\\\ 1 \\\\ 0 \\\\ 0\\end{bmatrix} + "
				               "t_{2}\\begin{bmatrix}-2 \\\\ 0 \\\\ -4 \\\\ 1\\end{bmatrix}$$") == 0);
		}

		// Math 210 Review Exam 1a, Q3 and Q7 (the same system, asked twice)
		//   x + 2y -  z = 10
		//  3x -  y + 2z =  9
		//  2x - 3y + 3z = -1
		// -> x = (4, 3, 0) + t(-3/7, 5/7, 1)
		{
				Harness h;
				const std::int64_t av[] = {1, 2, -1, 3, -1, 2, 2, -3, 3};
				const std::int64_t bv[] = {10, 9, -1};
				MatrixMutView a = mat(h.persist, 3, 3, av);
				MatrixMutView b = mat(h.persist, 3, 1, bv);

				MatrixMutView xp{}, basis{};
				ParametricSolution info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_solve_parametric(a.view(), b.view(), h.persist, h.scratch, &xp, &basis, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(info.consistent);
				assert(info.rank == 2);
				assert(info.nullity == 1);
				assert(info.free_cols[0] == 2);

				assert(same(xp.at(0, 0), Rational::from_int(4)));
				assert(same(xp.at(1, 0), Rational::from_int(3)));
				assert(same(xp.at(2, 0), Rational::from_int(0)));

				assert(same(basis.at(0, 0), rat(-3, 7)));
				assert(same(basis.at(1, 0), rat(5, 7)));
				assert(same(basis.at(2, 0), Rational::from_int(1)));

				check_solution_satisfies_system(a.view(), b.view(), xp.view(), basis.view(), info);

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);

				assert(std::strcmp(render(expl, n_steps - 3, h.scratch),
				               "$$\\begin{array}{l}x_{1} = 4 - \\frac{3}{7}x_{3} \\\\ x_{2} = 3 + \\frac{5}{7}x_{3}\\end{array}$$") == 0);
				assert(std::strcmp(render(expl, n_steps - 1, h.scratch),
				               "$$x = \\begin{bmatrix}4 \\\\ 3 \\\\ 0\\end{bmatrix} + "
				               "t_{1}\\begin{bmatrix}\\frac{-3}{7} \\\\ \\frac{5}{7} \\\\ 1\\end{bmatrix}$$") == 0);

				// step 0 is the augmented matrix, drawn with the vertical rule
				const char* first = render(expl, 0, h.scratch);
				assert(std::strstr(first, "\\begin{array}{rrr|r}") != nullptr);
				assert(std::strstr(first, "10") != nullptr);
		}

		// inconsistent: x + y = 1, 2x + 2y = 3
		{
				Harness h;
				const std::int64_t av[] = {1, 1, 2, 2};
				const std::int64_t bv[] = {1, 3};
				MatrixMutView a = mat(h.persist, 2, 2, av);
				MatrixMutView b = mat(h.persist, 2, 1, bv);

				MatrixMutView xp{}, basis{};
				ParametricSolution info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_solve_parametric(a.view(), b.view(), h.persist, h.scratch, &xp, &basis, &info, &expl, opts);

				// an inconsistent system is reported through info, not as an error
				assert(matrix_core::is_ok(err));
				assert(!info.consistent);
				assert(info.inconsistent_row == 1);
				assert(info.rank == 1);

				// x_p is zeroed when there is no solution
				assert(xp.at(0, 0).is_zero() && xp.at(1, 0).is_zero());

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);

				StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
				assert(expl.render_step(n_steps - 1, bufs) == ErrorCode::Ok);
				assert(std::strstr(g_caption, "no solution") != nullptr);
		}

		// zero matrix: every variable is free
		{
				Harness h;
				const std::int64_t av[] = {0, 0, 0, 0};
				const std::int64_t bv[] = {0, 0};
				MatrixMutView a = mat(h.persist, 2, 2, av);
				MatrixMutView b = mat(h.persist, 2, 1, bv);

				MatrixMutView xp{}, basis{};
				ParametricSolution info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_solve_parametric(a.view(), b.view(), h.persist, h.scratch, &xp, &basis, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(info.consistent);
				assert(info.rank == 0);
				assert(info.nullity == 2);
				check_solution_satisfies_system(a.view(), b.view(), xp.view(), basis.view(), info);

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);
		}

		// explanations disabled: the numbers still come back
		{
				Harness h;
				const std::int64_t av[] = {1, 0, 0, 1};
				const std::int64_t bv[] = {5, 6};
				MatrixMutView a = mat(h.persist, 2, 2, av);
				MatrixMutView b = mat(h.persist, 2, 1, bv);

				MatrixMutView xp{}, basis{};
				ParametricSolution info{};
				Explanation expl;
				auto err = matrix_core::op_solve_parametric(
				        a.view(), b.view(), h.persist, h.scratch, &xp, &basis, &info, nullptr, ExplainOptions{.enable = false});
				assert(matrix_core::is_ok(err));
				assert(!expl.available());
				assert(info.rank == 2 && info.nullity == 0 && info.consistent);
				assert(same(xp.at(0, 0), Rational::from_int(5)));
				assert(same(xp.at(1, 0), Rational::from_int(6)));
		}

		// argument validation
		{
				Harness h;
				const std::int64_t av[] = {1, 0, 0, 1};
				const std::int64_t bv[] = {1, 2, 3};
				MatrixMutView a = mat(h.persist, 2, 2, av);
				MatrixMutView b3 = mat(h.persist, 3, 1, bv);
				MatrixMutView xp{}, basis{};
				ParametricSolution info{};

				// b must be m x 1 with m == a.rows
				auto err = matrix_core::op_solve_parametric(
				        a.view(), b3.view(), h.persist, h.scratch, &xp, &basis, &info, nullptr, ExplainOptions{.enable = false});
				assert(err.code == ErrorCode::DimensionMismatch);

				// opts.persist must be the arena the results are allocated from
				Arena other(h.slab.data(), 64);
				Explanation expl;
				MatrixMutView b2 = mat(h.persist, 2, 1, bv);
				err = matrix_core::op_solve_parametric(a.view(),
				        b2.view(),
				        h.persist,
				        h.scratch,
				        &xp,
				        &basis,
				        &info,
				        &expl,
				        ExplainOptions{.enable = true, .persist = &other});
				assert(err.code == ErrorCode::Internal);
		}

		// a caption that does not fit is truncated rather than failing the step
		{
				Harness h;
				const std::int64_t av[] = {1, 2, -1, 3, -1, 2, 2, -3, 3};
				const std::int64_t bv[] = {10, 9, -1};
				MatrixMutView a = mat(h.persist, 3, 3, av);
				MatrixMutView b = mat(h.persist, 3, 1, bv);

				MatrixMutView xp{}, basis{};
				ParametricSolution info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_solve_parametric(a.view(), b.view(), h.persist, h.scratch, &xp, &basis, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				char tiny[8];
				StepRenderBuffers bufs{tiny, sizeof(tiny), g_latex, sizeof(g_latex), &h.scratch};
				for (std::size_t i = 0; i < expl.step_count(); i++)
						assert(expl.render_step(i, bufs) == ErrorCode::Ok);

				// a latex buffer that does not fit is a real failure
				char small_latex[16];
				StepRenderBuffers bad{g_caption, sizeof(g_caption), small_latex, sizeof(small_latex), &h.scratch};
				assert(expl.render_step(expl.step_count() - 1, bad) == ErrorCode::BufferTooSmall);
		}

		// the LaTeX writer on its own
		{
				Harness h;
				const std::int64_t xv[] = {1, 2};
				const std::int64_t bvals[] = {3, 4};
				MatrixMutView x = mat(h.persist, 2, 1, xv);
				MatrixMutView bs = mat(h.persist, 2, 1, bvals);

				char buf[256];
				assert(matrix_core::latex::write_parametric_solution("x", x.view(), bs.view(), 1, {buf, sizeof(buf)}) == ErrorCode::Ok);
				assert(std::strcmp(buf, "x = \\begin{bmatrix}1 \\\\ 2\\end{bmatrix} + t_{1}\\begin{bmatrix}3 \\\\ 4\\end{bmatrix}") == 0);

				// no particular vector and no parameters
				assert(matrix_core::latex::write_parametric_solution("x_{h}", MatrixView{}, MatrixView{}, 0, {buf, sizeof(buf)}) ==
				        ErrorCode::Ok);
				assert(std::strcmp(buf, "x_{h} = 0") == 0);
		}

#if defined(MATRIX_CE_TESTS)
		dbg_printf("[test_solve_parametric] all asserts passed\n");
#endif
		return 0;
}
