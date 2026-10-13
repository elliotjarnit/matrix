#include "matrix_core/matrix_core.hpp"
#include "matrix_core/ops_triangular.hpp"

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
using matrix_core::TriangularKind;

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

		// worksheet 2(a): lower triangular
		{
				const std::int64_t v[] = {2, 0, 0, 4, 7, 0, 1, -2, 3};
				MatrixMutView a = mat(persist, 3, v);
				assert(matrix_core::triangular_kind(a.view()) == TriangularKind::Lower);
				Rational det;
				Explanation expl;
				assert(matrix_core::is_ok(matrix_core::op_det_triangular(a.view(), &det, &expl, opts)));
				assert(det.num() == 42 && det.den() == 1);
				assert(expl.step_count() == 2);
				assert(expl.render_step(0, bufs) == ErrorCode::Ok);
				assert(std::strcmp(g_caption, "A is lower triangular") == 0);
				assert(expl.render_step(1, bufs) == ErrorCode::Ok);
				assert(std::strcmp(g_latex, "$$\\det(A) = a_{11}a_{22}a_{33}$$\n$$= (2)(7)(3) = 42$$") == 0);
				assert(expl.render_step(2, bufs) == ErrorCode::StepOutOfRange);
		}

		// worksheet 2(b): upper triangular
		{
				const std::int64_t v[] = {-2, 3, 8, 1, 0, 1, 2, -5, 0, 0, -1, 4, 0, 0, 0, 2};
				MatrixMutView a = mat(persist, 4, v);
				assert(matrix_core::triangular_kind(a.view()) == TriangularKind::Upper);
				Rational det;
				Explanation expl;
				assert(matrix_core::is_ok(matrix_core::op_det_triangular(a.view(), &det, &expl, opts)));
				assert(det.num() == 4 && det.den() == 1);
				assert(expl.render_step(0, bufs) == ErrorCode::Ok);
				assert(std::strcmp(g_caption, "A is upper triangular") == 0);
		}

		// diagonal, and a matrix that is neither
		{
				const std::int64_t d[] = {5, 0, 0, -1};
				assert(matrix_core::triangular_kind(mat(persist, 2, d).view()) == TriangularKind::Diagonal);

				const std::int64_t v[] = {1, 2, 3, 4};
				MatrixMutView a = mat(persist, 2, v);
				assert(matrix_core::triangular_kind(a.view()) == TriangularKind::None);
				Rational det;
				assert(matrix_core::op_det_triangular(a.view(), &det, nullptr, ExplainOptions{}).code == ErrorCode::InvalidDimension);
				a.cols = 1;
				assert(matrix_core::triangular_kind(a.view()) == TriangularKind::None);
				assert(matrix_core::op_det_triangular(a.view(), &det, nullptr, ExplainOptions{}).code == ErrorCode::NotSquare);
		}
		return 0;
}
