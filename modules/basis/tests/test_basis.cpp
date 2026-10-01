#include "matrix_core/matrix_core.hpp"
#include "matrix_core/ops_basis.hpp"

#include "test_dbg_ce.hpp"

#include <cassert>
#include <cstring>

#if defined(MATRIX_CE_TESTS)
#include <debug.h>
#endif

using matrix_core::Arena;
using matrix_core::BasisInfo;
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

bool is_int(const Rational& a, std::int64_t v) {
		return a.num() == v && a.den() == 1;
}

// A v == 0 for every column v of the null space basis
void check_null_basis_solves(MatrixView a, MatrixView basis, std::uint8_t nullity) {
		for (std::uint8_t k = 0; k < nullity; k++) {
				for (std::uint8_t r = 0; r < a.rows; r++) {
						Rational acc = Rational::from_int(0);
						for (std::uint8_t c = 0; c < a.cols; c++) {
								Rational term;
								assert(matrix_core::rational_mul(a.at(r, c), basis.at(c, k), &term) == ErrorCode::Ok);
								assert(matrix_core::rational_add(acc, term, &acc) == ErrorCode::Ok);
						}
						assert(acc.is_zero());
				}
		}
}

// the Col(A) basis is exactly the pivot columns of A
void check_col_basis_is_pivot_columns(MatrixView a, MatrixView basis, const BasisInfo& info) {
		assert(basis.rows == a.rows);
		for (std::uint8_t k = 0; k < info.rank; k++) {
				for (std::uint8_t r = 0; r < a.rows; r++)
						assert(same(basis.at(r, k), a.at(r, info.pivot_cols[k])));
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

// same sizes as the shell's App::step_caption_ / App::step_latex_
char g_caption[128];
char g_latex[1024];

const char* render(const Explanation& expl, std::size_t index, Arena& scratch) {
		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &scratch};
		scratch.clear();
		assert(expl.render_step(index, bufs) == ErrorCode::Ok);
		return g_latex;
}

// every step renders into the shell's buffers, and the tail is where the op
// says it is
void check_all_steps_render(const Explanation& expl, Arena& scratch) {
		const std::size_t n = expl.step_count();
		assert(n >= 8); // 2 definitions + 6 tail steps
		for (std::size_t i = 0; i < n; i++)
				(void)render(expl, i, scratch);

		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &scratch};
		assert(expl.render_step(n, bufs) == ErrorCode::StepOutOfRange);
}

const char* tail_step(const Explanation& expl, std::size_t from_end, Arena& scratch) {
		return render(expl, expl.step_count() - from_end, scratch);
}

} // namespace

int main() {
		// Lay 4.2 example 3: rank 2, three free variables
		//   x1 = 2x2 + x4 - 3x5, x3 = -2x4 + 2x5
		{
				Harness h;
				const std::int64_t av[] = {
				        -3,
				        6,
				        -1,
				        1,
				        -7, //
				        1,
				        -2,
				        2,
				        3,
				        -1, //
				        2,
				        -4,
				        5,
				        8,
				        -4, //
				};
				MatrixMutView a = mat(h.persist, 3, 5, av);

				MatrixMutView col{}, nul{};
				BasisInfo info{};
				Explanation e;
				assert(is_ok(matrix_core::op_basis(
				        a.view(), h.persist, h.scratch, &col, &nul, &info, &e, ExplainOptions{.enable = true, .persist = &h.persist})));

				assert(info.rows == 3 && info.cols == 5);
				assert(info.rank == 2);
				assert(info.nullity == 3);
				assert(info.rank + info.nullity == info.cols); // rank theorem
				assert(info.pivot_cols[0] == 0 && info.pivot_cols[1] == 2);
				assert(info.free_cols[0] == 1 && info.free_cols[1] == 3 && info.free_cols[2] == 4);
				assert(info.pivot_mask == 0x5u);

				assert(col.rows == 3 && col.cols == 2);
				check_col_basis_is_pivot_columns(a.view(), col.view(), info);

				assert(nul.rows == 5 && nul.cols == 3);
				check_null_basis_solves(a.view(), nul.view(), info.nullity);
				// the textbook basis: (2,1,0,0,0), (1,0,-2,1,0), (-3,0,2,0,1)
				const std::int64_t expect[5][3] = {{2, 1, -3}, {1, 0, 0}, {0, -2, 2}, {0, 1, 0}, {0, 0, 1}};
				for (std::uint8_t r = 0; r < 5; r++) {
						for (std::uint8_t k = 0; k < 3; k++)
								assert(is_int(nul.at(r, k), expect[r][k]));
				}

				check_all_steps_render(e, h.scratch);

				// definitions
				assert(std::strstr(
				        render(e, 0, h.scratch), "\\text{Span}\\lbrace a_{1}, a_{2}, a_{3}, a_{4}, a_{5}\\rbrace \\subseteq R^{3}"));
				assert(std::strstr(render(e, 1, h.scratch), "x \\in R^{5} : Ax = 0"));

				// pivots, then the pivot variables solved in terms of the free ones
				(void)tail_step(e, 6, h.scratch);
				assert(std::strcmp(g_caption, "Pivot columns: 1, 3") == 0);
				assert(std::strstr(tail_step(e, 5, h.scratch), "\\dim\\text{Col}(A) = \\text{rank}(A) = 2"));
				assert(!std::strstr(g_latex, "= R^{")); // rank 2 < 3 rows: not all of R^3
				const char* solve = tail_step(e, 4, h.scratch);
				assert(std::strstr(solve, "x_{1} = 2x_{2} + x_{4} - 3x_{5}"));
				assert(std::strstr(solve, "x_{3} = -2x_{4} + 2x_{5}"));
				assert(std::strstr(solve, "x_{2},\\, x_{4},\\, x_{5}\\;\\text{free}"));
				assert(std::strstr(tail_step(e, 3, h.scratch), "x = x_{2}"));
				assert(std::strstr(tail_step(e, 2, h.scratch), "\\dim\\text{Nul}(A) = 3"));
				assert(std::strstr(tail_step(e, 1, h.scratch), "2 + 3 = 5 = n"));
		}

		// invertible: Col(A) = R^3 and Nul(A) = {0}
		{
				Harness h;
				const std::int64_t av[] = {
				        2,
				        1,
				        1, //
				        4,
				        -6,
				        0, //
				        -2,
				        7,
				        2, //
				};
				MatrixMutView a = mat(h.persist, 3, 3, av);

				MatrixMutView col{}, nul{};
				BasisInfo info{};
				Explanation e;
				assert(is_ok(matrix_core::op_basis(
				        a.view(), h.persist, h.scratch, &col, &nul, &info, &e, ExplainOptions{.enable = true, .persist = &h.persist})));

				assert(info.rank == 3 && info.nullity == 0);
				assert(col.rows == 3 && col.cols == 3);
				check_col_basis_is_pivot_columns(a.view(), col.view(), info);
				// the empty basis comes back as the zero column
				assert(nul.rows == 3 && nul.cols == 1);
				for (std::uint8_t r = 0; r < 3; r++)
						assert(nul.at(r, 0).is_zero());

				check_all_steps_render(e, h.scratch);
				assert(std::strstr(tail_step(e, 5, h.scratch), "\\text{Col}(A) = R^{3}"));
				assert(std::strcmp(tail_step(e, 4, h.scratch), "$$x = 0$$") == 0);
				assert(std::strstr(tail_step(e, 2, h.scratch), "\\text{Nul}(A) = \\lbrace 0\\rbrace"));
				assert(std::strstr(g_latex, "\\dim\\text{Nul}(A) = 0"));
				assert(std::strstr(tail_step(e, 1, h.scratch), "3 + 0 = 3 = n"));
		}

		// the zero matrix: Col(A) = {0} and Nul(A) is everything; it is already
		// in RREF so there are no row operations at all
		{
				Harness h;
				const std::int64_t av[] = {0, 0, 0, 0, 0, 0};
				MatrixMutView a = mat(h.persist, 2, 3, av);

				MatrixMutView col{}, nul{};
				BasisInfo info{};
				Explanation e;
				assert(is_ok(matrix_core::op_basis(
				        a.view(), h.persist, h.scratch, &col, &nul, &info, &e, ExplainOptions{.enable = true, .persist = &h.persist})));

				assert(info.rank == 0 && info.nullity == 3);
				assert(col.rows == 2 && col.cols == 1);
				assert(nul.rows == 3 && nul.cols == 3);
				check_null_basis_solves(a.view(), nul.view(), info.nullity);

				assert(e.step_count() == 8);
				check_all_steps_render(e, h.scratch);
				(void)tail_step(e, 6, h.scratch);
				assert(std::strcmp(g_caption, "Pivot columns: -") == 0);
				assert(std::strstr(tail_step(e, 5, h.scratch), "\\text{Col}(A) = \\lbrace 0\\rbrace"));
				// no pivot variables to solve for, only free ones
				assert(std::strstr(tail_step(e, 4, h.scratch), "\\begin{array}{l}x_{1},\\, x_{2},\\, x_{3}\\;\\text{free}"));
				assert(std::strstr(tail_step(e, 1, h.scratch), "0 + 3 = 3 = n"));
		}

		// the largest matrix the editor allows, with fractions all through the
		// reduction, still fits the shell's step buffers (row 3 = row 1 + row 2)
		{
				Harness h;
				const std::int64_t av[] = {
				        7,
				        -3,
				        11,
				        4,
				        9,
				        -5, //
				        2,
				        13,
				        -6,
				        8,
				        1,
				        3, //
				        9,
				        10,
				        5,
				        12,
				        10,
				        -2, //
				        -4,
				        5,
				        17,
				        -9,
				        6,
				        11, //
				        5,
				        23,
				        -1,
				        20,
				        11,
				        1, //
				        3,
				        15,
				        11,
				        -1,
				        7,
				        14, //
				};
				MatrixMutView a = mat(h.persist, 6, 6, av);

				MatrixMutView col{}, nul{};
				BasisInfo info{};
				Explanation e;
				assert(is_ok(matrix_core::op_basis(
				        a.view(), h.persist, h.scratch, &col, &nul, &info, &e, ExplainOptions{.enable = true, .persist = &h.persist})));
				assert(info.rank == 5 && info.nullity == 1);
				check_col_basis_is_pivot_columns(a.view(), col.view(), info);
				check_null_basis_solves(a.view(), nul.view(), info.nullity);
				check_all_steps_render(e, h.scratch);
		}

		// without an explanation the op still produces the bases
		{
				Harness h;
				const std::int64_t av[] = {1, 2, 2, 4};
				MatrixMutView a = mat(h.persist, 2, 2, av);
				MatrixMutView col{}, nul{};
				BasisInfo info{};
				assert(is_ok(matrix_core::op_basis(a.view(), h.persist, h.scratch, &col, &nul, &info, nullptr, ExplainOptions{})));
				assert(info.rank == 1 && info.nullity == 1);
				assert(is_int(nul.at(0, 0), -2) && is_int(nul.at(1, 0), 1));
		}

#if defined(MATRIX_CE_TESTS)
		dbg_printf("[test_basis] ok\n");
#endif
		return 0;
}
