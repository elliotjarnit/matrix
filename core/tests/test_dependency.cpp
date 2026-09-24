#include "matrix_core/matrix_core.hpp"

#include "test_dbg_ce.hpp"

#include <cassert>
#include <cstring>

#if defined(MATRIX_CE_TESTS)
#include <debug.h>
#endif

using matrix_core::Arena;
using matrix_core::DependencyInfo;
using matrix_core::ErrorCode;
using matrix_core::ExplainOptions;
using matrix_core::Explanation;
using matrix_core::MatrixMutView;
using matrix_core::MatrixView;
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

bool same(const Rational& a, const Rational& b) {
		return a.num() == b.num() && a.den() == b.den();
}

// Rebuilds the dependent column from the relation the RREF encodes and checks
// it against the original column: this is the claim the exam answer makes.
void check_relation_reproduces_column(MatrixView a, MatrixView rref, const DependencyInfo& info, std::uint8_t dep_index) {
		const std::uint8_t f = info.dependent_cols[dep_index];
		for (std::uint8_t r = 0; r < a.rows; r++) {
				Rational acc = Rational::from_int(0);
				for (std::uint8_t i = 0; i < info.rank; i++) {
						const Rational& coef = rref.at(info.pivot_rows[i], f);
						Rational term;
						assert(matrix_core::rational_mul(coef, a.at(r, info.pivot_cols[i]), &term) == ErrorCode::Ok);
						assert(matrix_core::rational_add(acc, term, &acc) == ErrorCode::Ok);
				}
				assert(same(acc, a.at(r, f)));
		}
}

struct Harness {
		Slab slab;
		Arena persist;
		Arena scratch;

		Harness() {
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
		// Math 210 Review Exam 1a, Q4(a): three vectors in R^4, independent
		//   v1 = (3,-1,2,4), v2 = (1,2,6,7), v3 = (-2,3,1,5)
		{
				Harness h;
				const std::int64_t av[] = {3, 1, -2, -1, 2, 3, 2, 6, 1, 4, 7, 5};
				MatrixMutView a = mat(h.persist, 4, 3, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_dependency(a.view(), h.persist, &rref, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(info.independent);
				assert(info.rank == 3);
				assert(info.vector_count == 3);
				assert(info.dim == 4);
				assert(info.dependent_count == 0);
				// three vectors cannot span R^4
				assert(!info.spans);

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);

				// independent: the verdict is the last step, there are no relations
				StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
				assert(expl.render_step(n_steps - 1, bufs) == ErrorCode::Ok);
				assert(std::strstr(g_caption, "linearly independent") != nullptr);
				assert(expl.render_step(n_steps, bufs) == ErrorCode::StepOutOfRange);
		}

		// Math 210 Review Exam 1a, Q4(b): A = [1 0 3 ; 3 4 13]
		//   columns span R^2, are dependent, and v3 = 3v1 + v2
		{
				Harness h;
				const std::int64_t av[] = {1, 0, 3, 3, 4, 13};
				MatrixMutView a = mat(h.persist, 2, 3, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_dependency(a.view(), h.persist, &rref, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(!info.independent);
				assert(info.spans); // rank 2 == 2 rows
				assert(info.rank == 2);
				assert(info.dependent_count == 1);
				assert(info.dependent_cols[0] == 2);
				assert(info.pivot_cols[0] == 0 && info.pivot_cols[1] == 1);

				// v3 = 3 v1 + 1 v2
				assert(same(rref.at(info.pivot_rows[0], 2), Rational::from_int(3)));
				assert(same(rref.at(info.pivot_rows[1], 2), Rational::from_int(1)));
				check_relation_reproduces_column(a.view(), rref.view(), info, 0);

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);

				// a coefficient of 1 is elided, as it would be written by hand
				assert(std::strcmp(render(expl, n_steps - 1, h.scratch), "$$\\begin{array}{l}v_{3} = 3v_{1} + v_{2}\\end{array}$$") == 0);

				StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
				assert(expl.render_step(n_steps - 2, bufs) == ErrorCode::Ok);
				assert(std::strstr(g_caption, "linearly dependent") != nullptr);
		}

		// Math 210 Review Exam 1b, Q5(a): v1 = (2,3,7), v2 = (-4,6,-2), v3 = (-6,15,3)
		//   dependent, v3 = v1 + 2v2
		{
				Harness h;
				const std::int64_t av[] = {2, -4, -6, 3, 6, 15, 7, -2, 3};
				MatrixMutView a = mat(h.persist, 3, 3, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_dependency(a.view(), h.persist, &rref, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(!info.independent);
				assert(info.rank == 2);
				assert(info.dependent_count == 1);
				assert(info.dependent_cols[0] == 2);
				assert(same(rref.at(info.pivot_rows[0], 2), Rational::from_int(1)));
				assert(same(rref.at(info.pivot_rows[1], 2), Rational::from_int(2)));
				check_relation_reproduces_column(a.view(), rref.view(), info, 0);

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);
				assert(std::strcmp(render(expl, n_steps - 1, h.scratch), "$$\\begin{array}{l}v_{3} = v_{1} + 2v_{2}\\end{array}$$") == 0);
		}

		// Math 210 Review Exam 1b, Q5(b): v1 = (4,5), v2 = (2,-1) are independent
		{
				Harness h;
				const std::int64_t av[] = {4, 2, 5, -1};
				MatrixMutView a = mat(h.persist, 2, 2, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_dependency(a.view(), h.persist, &rref, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(info.independent);
				assert(info.spans);
				assert(info.rank == 2);
				assert(info.dependent_count == 0);
		}

		// fractional and negative coefficients, and more than one dependent vector
		//   v1 = (2,0), v2 = (0,4), v3 = (1,-2), v4 = (0,0)
		//   v3 = 1/2 v1 - 1/2 v2, v4 = 0
		{
				Harness h;
				const std::int64_t av[] = {2, 0, 1, 0, 0, 4, -2, 0};
				MatrixMutView a = mat(h.persist, 2, 4, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_dependency(a.view(), h.persist, &rref, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(!info.independent);
				assert(info.rank == 2);
				assert(info.dependent_count == 2);
				assert(info.dependent_cols[0] == 2 && info.dependent_cols[1] == 3);
				check_relation_reproduces_column(a.view(), rref.view(), info, 0);
				check_relation_reproduces_column(a.view(), rref.view(), info, 1);

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);
				assert(std::strcmp(render(expl, n_steps - 1, h.scratch),
				               "$$\\begin{array}{l}v_{3} = \\frac{1}{2}v_{1} - \\frac{1}{2}v_{2} \\\\ v_{4} = 0\\end{array}$$") == 0);
		}

		// a zero vector is always dependent
		{
				Harness h;
				const std::int64_t av[] = {0, 0, 0, 0};
				MatrixMutView a = mat(h.persist, 2, 2, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_dependency(a.view(), h.persist, &rref, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				assert(!info.independent);
				assert(info.rank == 0);
				assert(info.dependent_count == 2);

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);
				assert(std::strcmp(render(expl, n_steps - 1, h.scratch), "$$\\begin{array}{l}v_{1} = 0 \\\\ v_{2} = 0\\end{array}$$") == 0);
		}

		// explanations disabled, and argument validation
		{
				Harness h;
				const std::int64_t av[] = {1, 0, 0, 1};
				MatrixMutView a = mat(h.persist, 2, 2, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				auto err = matrix_core::op_dependency(a.view(), h.persist, &rref, &info, nullptr, ExplainOptions{.enable = false});
				assert(matrix_core::is_ok(err));
				assert(!expl.available());
				assert(info.independent && info.rank == 2);

				// opts.persist must be the arena the RREF is allocated from
				Arena other(h.slab.data(), 64);
				err = matrix_core::op_dependency(
				        a.view(), h.persist, &rref, &info, &expl, ExplainOptions{.enable = true, .persist = &other});
				assert(err.code == ErrorCode::Internal);
		}

		// captions truncate rather than failing the step; latex overflow is real
		{
				Harness h;
				const std::int64_t av[] = {1, 0, 3, 3, 4, 13};
				MatrixMutView a = mat(h.persist, 2, 3, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_dependency(a.view(), h.persist, &rref, &info, &expl, opts);
				assert(matrix_core::is_ok(err));

				char tiny[8];
				StepRenderBuffers bufs{tiny, sizeof(tiny), g_latex, sizeof(g_latex), &h.scratch};
				for (std::size_t i = 0; i < expl.step_count(); i++)
						assert(expl.render_step(i, bufs) == ErrorCode::Ok);

				char small_latex[12];
				StepRenderBuffers bad{g_caption, sizeof(g_caption), small_latex, sizeof(small_latex), &h.scratch};
				assert(expl.render_step(expl.step_count() - 1, bad) == ErrorCode::BufferTooSmall);
		}

		// Math 210 Review Exam 1a, Q5: T : R^3 -> R^4, T(x,y,z) = (3x+y, x+y, 0, 2x)
		// Standard matrix has T(e_1), T(e_2), T(e_3) as its columns.
		{
				Harness h;
				const std::int64_t av[] = {3, 1, 0, 1, 1, 0, 0, 0, 0, 2, 0, 0};
				MatrixMutView a = mat(h.persist, 4, 3, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_dependency(a.view(),
				        h.persist,
				        &rref,
				        &info,
				        &expl,
				        opts,
				        matrix_core::DependencyLabels{},
				        matrix_core::DependencyPurpose::Transformation);
				assert(matrix_core::is_ok(err));

				assert(info.rank == 2);
				assert(info.dim == 4);          // R^4 is the codomain
				assert(info.vector_count == 3); // R^3 is the domain
				assert(!info.spans);            // rank 2 < 4: not onto
				assert(!info.independent);      // rank 2 < 3: not one-to-one

				// a transformation stops at the verdict: no dependency relations
				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);

				StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
				assert(expl.render_step(n_steps - 1, bufs) == ErrorCode::Ok);
				assert(std::strstr(g_caption, "rank 2 < 4 rows: not onto") != nullptr);
				assert(std::strstr(g_caption, "rank 2 < 3 columns: not one-to-one") != nullptr);
				assert(expl.render_step(n_steps, bufs) == ErrorCode::StepOutOfRange);
		}

		// Math 210 Review Exam 1a, Q6: T : R^4 -> R^2, T(x1,x2,x3,x4) = (x1, 2x1)
		{
				Harness h;
				const std::int64_t av[] = {1, 0, 0, 0, 2, 0, 0, 0};
				MatrixMutView a = mat(h.persist, 2, 4, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_dependency(a.view(),
				        h.persist,
				        &rref,
				        &info,
				        &expl,
				        opts,
				        matrix_core::DependencyLabels{},
				        matrix_core::DependencyPurpose::Transformation);
				assert(matrix_core::is_ok(err));

				assert(info.rank == 1);
				assert(!info.spans);       // rank 1 < 2: not onto
				assert(!info.independent); // rank 1 < 4: not one-to-one

				const std::size_t n_steps = expl.step_count();
				for (std::size_t i = 0; i < n_steps; i++)
						(void)render(expl, i, h.scratch);
				StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
				assert(expl.render_step(n_steps - 1, bufs) == ErrorCode::Ok);
				assert(std::strstr(g_caption, "not onto") != nullptr);
				assert(std::strstr(g_caption, "not one-to-one") != nullptr);
		}

		// onto but not one-to-one: A = [1 0 0 ; 0 1 0] maps R^3 onto R^2
		{
				Harness h;
				const std::int64_t av[] = {1, 0, 0, 0, 1, 0};
				MatrixMutView a = mat(h.persist, 2, 3, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				assert(matrix_core::is_ok(matrix_core::op_dependency(a.view(),
				        h.persist,
				        &rref,
				        &info,
				        &expl,
				        opts,
				        matrix_core::DependencyLabels{},
				        matrix_core::DependencyPurpose::Transformation)));

				assert(info.rank == 2);
				assert(info.spans);        // rank 2 = 2 rows: onto
				assert(!info.independent); // rank 2 < 3 columns: not one-to-one

				const std::size_t n_steps = expl.step_count();
				StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
				assert(expl.render_step(n_steps - 1, bufs) == ErrorCode::Ok);
				assert(std::strstr(g_caption, "rank 2 = 2 rows: onto") != nullptr);
				assert(std::strstr(g_caption, "rank 2 < 3 columns: not one-to-one") != nullptr);
		}

		// both: the identity on R^2 is onto and one-to-one
		{
				Harness h;
				const std::int64_t av[] = {1, 0, 0, 1};
				MatrixMutView a = mat(h.persist, 2, 2, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				assert(matrix_core::is_ok(matrix_core::op_dependency(a.view(),
				        h.persist,
				        &rref,
				        &info,
				        &expl,
				        opts,
				        matrix_core::DependencyLabels{},
				        matrix_core::DependencyPurpose::Transformation)));

				assert(info.spans && info.independent);

				StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
				assert(expl.render_step(expl.step_count() - 1, bufs) == ErrorCode::Ok);
				assert(std::strstr(g_caption, "rank 2 = 2 rows: onto") != nullptr);
				assert(std::strstr(g_caption, "rank 2 = 2 columns: one-to-one") != nullptr);
		}

		// the Vectors purpose is unaffected: it still ends with the relations
		{
				Harness h;
				const std::int64_t av[] = {1, 0, 3, 3, 4, 13};
				MatrixMutView a = mat(h.persist, 2, 3, av);

				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation vec_expl;
				Explanation tr_expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				assert(matrix_core::is_ok(matrix_core::op_dependency(a.view(), h.persist, &rref, &info, &vec_expl, opts)));
				assert(matrix_core::is_ok(matrix_core::op_dependency(a.view(),
				        h.persist,
				        &rref,
				        &info,
				        &tr_expl,
				        opts,
				        matrix_core::DependencyLabels{},
				        matrix_core::DependencyPurpose::Transformation)));

				// the transformation reading drops exactly the relations step
				assert(vec_expl.step_count() == tr_expl.step_count() + 1);
				assert(std::strcmp(render(vec_expl, vec_expl.step_count() - 1, h.scratch),
				               "$$\\begin{array}{l}v_{3} = 3v_{1} + v_{2}\\end{array}$$") == 0);
		}

#if defined(MATRIX_CE_TESTS)
		dbg_printf("[test_dependency] all asserts passed\n");
#endif
		return 0;
}
