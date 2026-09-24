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
		// Math 210 Review Exam 1b, Q2: the column row expansion of AB
		//   A = [1 2 3 ; 4 5 6], B = [1 2 ; 3 4 ; 5 6]  ->  AB = [22 28 ; 49 64]
		{
				Harness h;
				const std::int64_t av[] = {1, 2, 3, 4, 5, 6};
				const std::int64_t bv[] = {1, 2, 3, 4, 5, 6};
				MatrixMutView a = mat(h.persist, 2, 3, av);
				MatrixMutView b = mat(h.persist, 3, 2, bv);

				MatrixMutView out{};
				assert(matrix_core::matrix_alloc(h.persist, 2, 2, &out) == ErrorCode::Ok);

				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				auto err = matrix_core::op_mul_colrow(a.view(), b.view(), out, &expl, opts);
				assert(matrix_core::is_ok(err));

				// the value is the ordinary product
				assert(same(out.at(0, 0), Rational::from_int(22)));
				assert(same(out.at(0, 1), Rational::from_int(28)));
				assert(same(out.at(1, 0), Rational::from_int(49)));
				assert(same(out.at(1, 1), Rational::from_int(64)));

				// cross check against op_mul on the same inputs
				MatrixMutView plain{};
				assert(matrix_core::matrix_alloc(h.scratch, 2, 2, &plain) == ErrorCode::Ok);
				assert(matrix_core::is_ok(matrix_core::op_mul(a.view(), b.view(), plain, nullptr, ExplainOptions{.enable = false})));
				for (std::uint8_t r = 0; r < 2; r++) {
						for (std::uint8_t c = 0; c < 2; c++)
								assert(same(out.at(r, c), plain.at(r, c)));
				}

				// intro, one step per column of A, then the sum
				assert(expl.step_count() == 5);
				for (std::size_t i = 0; i < expl.step_count(); i++)
						(void)render(expl, i, h.scratch);

				assert(std::strcmp(render(expl, 0, h.scratch),
				               "$$A = \\begin{bmatrix}1 & 2 & 3 \\\\ 4 & 5 & 6\\end{bmatrix},\\; "
				               "B = \\begin{bmatrix}1 & 2 \\\\ 3 & 4 \\\\ 5 & 6\\end{bmatrix}$$") == 0);

				// col_1(A) row_1(B) = [1 2 ; 4 8]
				assert(std::strcmp(render(expl, 1, h.scratch),
				               "$$\\begin{bmatrix}1 \\\\ 4\\end{bmatrix}\\begin{bmatrix}1 & 2\\end{bmatrix} = "
				               "\\begin{bmatrix}1 & 2 \\\\ 4 & 8\\end{bmatrix}$$") == 0);
				// col_2(A) row_2(B) = [6 8 ; 15 20]
				assert(std::strcmp(render(expl, 2, h.scratch),
				               "$$\\begin{bmatrix}2 \\\\ 5\\end{bmatrix}\\begin{bmatrix}3 & 4\\end{bmatrix} = "
				               "\\begin{bmatrix}6 & 8 \\\\ 15 & 20\\end{bmatrix}$$") == 0);
				// col_3(A) row_3(B) = [15 18 ; 30 36]
				assert(std::strcmp(render(expl, 3, h.scratch),
				               "$$\\begin{bmatrix}3 \\\\ 6\\end{bmatrix}\\begin{bmatrix}5 & 6\\end{bmatrix} = "
				               "\\begin{bmatrix}15 & 18 \\\\ 30 & 36\\end{bmatrix}$$") == 0);

				// the sum of the three terms, then the product
				assert(std::strcmp(render(expl, 4, h.scratch),
				               "$$AB = \\begin{bmatrix}1 & 2 \\\\ 4 & 8\\end{bmatrix} + "
				               "\\begin{bmatrix}6 & 8 \\\\ 15 & 20\\end{bmatrix} + "
				               "\\begin{bmatrix}15 & 18 \\\\ 30 & 36\\end{bmatrix} = "
				               "\\begin{bmatrix}22 & 28 \\\\ 49 & 64\\end{bmatrix}$$") == 0);

				StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
				assert(expl.render_step(expl.step_count(), bufs) == ErrorCode::StepOutOfRange);

				// the terms really do add up to the product
				assert(expl.render_step(1, bufs) == ErrorCode::Ok);
				assert(std::strstr(g_caption, "term 1/3") != nullptr);
		}

		// rectangular with fractions: the terms must still sum to A*B
		{
				Harness h;
				const std::int64_t av[] = {1, -2, 0, 3};
				const std::int64_t bv[] = {2, 1, 5, -1, 4, 0};
				MatrixMutView a = mat(h.persist, 2, 2, av);
				MatrixMutView b = mat(h.persist, 2, 3, bv);
				a.at_mut(0, 1) = matrix_core::Rational::from_int(-2);

				MatrixMutView out{};
				assert(matrix_core::matrix_alloc(h.persist, 2, 3, &out) == ErrorCode::Ok);
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				assert(matrix_core::is_ok(matrix_core::op_mul_colrow(a.view(), b.view(), out, &expl, opts)));

				MatrixMutView plain{};
				assert(matrix_core::matrix_alloc(h.scratch, 2, 3, &plain) == ErrorCode::Ok);
				assert(matrix_core::is_ok(matrix_core::op_mul(a.view(), b.view(), plain, nullptr, ExplainOptions{.enable = false})));
				for (std::uint8_t r = 0; r < 2; r++) {
						for (std::uint8_t c = 0; c < 3; c++)
								assert(same(out.at(r, c), plain.at(r, c)));
				}

				assert(expl.step_count() == 4); // intro + 2 terms + sum
				for (std::size_t i = 0; i < expl.step_count(); i++)
						(void)render(expl, i, h.scratch);
		}

		// dimension mismatch is rejected
		{
				Harness h;
				const std::int64_t av[] = {1, 2, 3, 4, 5, 6};
				MatrixMutView a = mat(h.persist, 2, 3, av);
				MatrixMutView b = mat(h.persist, 2, 2, av);
				MatrixMutView out{};
				assert(matrix_core::matrix_alloc(h.persist, 2, 2, &out) == ErrorCode::Ok);
				auto err = matrix_core::op_mul_colrow(a.view(), b.view(), out, nullptr, ExplainOptions{.enable = false});
				assert(err.code == ErrorCode::DimensionMismatch);
		}

		// a latex buffer too small for the expanded sum falls back to the product
		{
				Harness h;
				const std::int64_t av[] = {1, 2, 3, 4, 5, 6};
				const std::int64_t bv[] = {1, 2, 3, 4, 5, 6};
				MatrixMutView a = mat(h.persist, 2, 3, av);
				MatrixMutView b = mat(h.persist, 3, 2, bv);
				MatrixMutView out{};
				assert(matrix_core::matrix_alloc(h.persist, 2, 2, &out) == ErrorCode::Ok);
				Explanation expl;
				ExplainOptions opts{.enable = true, .persist = &h.persist};
				assert(matrix_core::is_ok(matrix_core::op_mul_colrow(a.view(), b.view(), out, &expl, opts)));

				char small[64];
				StepRenderBuffers bufs{g_caption, sizeof(g_caption), small, sizeof(small), &h.scratch};
				assert(expl.render_step(expl.step_count() - 1, bufs) == ErrorCode::Ok);
				assert(std::strcmp(small, "$$AB = \\begin{bmatrix}22 & 28 \\\\ 49 & 64\\end{bmatrix}$$") == 0);
		}

#if defined(MATRIX_CE_TESTS)
		dbg_printf("[test_mul_colrow] all asserts passed\n");
#endif
		return 0;
}
