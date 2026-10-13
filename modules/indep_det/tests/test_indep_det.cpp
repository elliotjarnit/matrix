#include "matrix_core/matrix_core.hpp"
#include "matrix_core/ops_indep_det.hpp"

#include "test_dbg_ce.hpp"

#include <cassert>
#include <cstring>

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

char g_caption[128];
char g_latex[1024];

} // namespace

int main() {
		Slab slab;
		assert(slab.init(32 * 1024) == ErrorCode::Ok);
		Arena persist(slab.data(), slab.size() / 2);
		Arena scratch(slab.data() + (slab.size() / 2), slab.size() - (slab.size() / 2));
		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &scratch};
		const ExplainOptions opts{.enable = true, .persist = &persist};

		// worksheet 4: v1 = (-4, 8, 6), v2 = (2, 3, 1), v3 = (0, 14, 8) as columns
		{
				const std::int64_t v[] = {-4, 2, 0, 8, 3, 14, 6, 1, 8};
				MatrixMutView a = mat(persist, 3, v);
				Rational det;
				bool independent = true;
				Explanation expl;
				assert(matrix_core::is_ok(matrix_core::op_indep_det(a.view(), scratch, &det, &independent, &expl, opts)));
				assert(det.is_zero());
				assert(!independent);

				const std::size_t n = expl.step_count();
				assert(n >= 3);
				for (std::size_t i = 0; i < n; i++)
						assert(expl.render_step(i, bufs) == ErrorCode::Ok);
				assert(expl.render_step(0, bufs) == ErrorCode::Ok);
				assert(std::strcmp(g_caption, "Columns are the vectors") == 0);
				assert(expl.render_step(n - 1, bufs) == ErrorCode::Ok);
				assert(std::strcmp(g_caption, "A is not invertible, so the columns are linearly dependent") == 0);
				assert(std::strcmp(g_latex, "$$\\det(A) = 0$$") == 0);
				assert(expl.render_step(n, bufs) == ErrorCode::StepOutOfRange);
		}

		// independent: det != 0
		{
				const std::int64_t v[] = {2, 5, -3, 4, 7, -1, 1, 0, 3};
				MatrixMutView a = mat(persist, 3, v);
				Rational det;
				bool independent = false;
				Explanation expl;
				assert(matrix_core::is_ok(matrix_core::op_indep_det(a.view(), scratch, &det, &independent, &expl, opts)));
				assert(det.num() == -2);
				assert(independent);
				assert(expl.render_step(expl.step_count() - 1, bufs) == ErrorCode::Ok);
				assert(std::strcmp(g_latex, "$$\\det(A) = -2 \\neq 0$$") == 0);

				Rational again;
				bool again_indep = false;
				assert(matrix_core::is_ok(matrix_core::op_indep_det(a.view(), scratch, &again, &again_indep, nullptr, ExplainOptions{})));
				assert(again_indep && again.num() == -2);
		}

		// non square: n vectors in R^m with m != n have no determinant
		{
				const std::int64_t v[] = {1, 2, 3, 4};
				MatrixMutView a = mat(persist, 2, v);
				a.cols = 1;
				Rational det;
				bool independent = false;
				Explanation expl;
				assert(matrix_core::op_indep_det(a.view(), scratch, &det, &independent, &expl, opts).code == ErrorCode::NotSquare);
				assert(!expl.available());
		}
		return 0;
}
