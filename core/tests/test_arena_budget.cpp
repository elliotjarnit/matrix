// The shell hands core two fixed arenas carved out of a single 12 KiB malloc
// (shell/src/app.cpp): 8 KiB of persist, of which 4608 bytes are permanent slot
// storage, and 4 KiB of scratch. The calculator has to hold the decompressed
// program, this slab and the TeX renderer pool in RAM at the same time, so the
// budget is tight on purpose.
//
// This test runs every explained operation at the largest dimensions the UI
// allows and fails if any of them would not fit. Adding a heavier operation
// should break this test rather than the calculator.
#include "matrix_core/matrix_core.hpp"
#include "matrix_core/ops_dependency.hpp"
#include "matrix_core/ops_inverse.hpp"
#include "matrix_core/ops_solve.hpp"
#if MATRIX_MODULE_COLROW
#include "matrix_core/ops_colrow.hpp"
#endif
#if MATRIX_MODULE_BASIS
#include "matrix_core/ops_basis.hpp"
#endif
#if MATRIX_MODULE_LU
#include "matrix_core/ops_lu.hpp"
#endif
#if MATRIX_MODULE_EXPANSION
#include "matrix_core/ops_expansion.hpp"
#endif
#if MATRIX_MODULE_ADJUGATE
#include "matrix_core/ops_adjugate.hpp"
#endif
#if MATRIX_MODULE_TRIANGULAR
#include "matrix_core/ops_triangular.hpp"
#endif
#if MATRIX_MODULE_INDEP_DET
#include "matrix_core/ops_indep_det.hpp"
#endif
#if MATRIX_MODULE_AREA
#include "matrix_core/ops_area.hpp"
#endif

#include "test_dbg_ce.hpp"

#include <cassert>

#if defined(MATRIX_CE_TESTS)
#include <debug.h>
#endif

using namespace matrix_core;

namespace {

// must match shell/src/app.cpp
constexpr std::size_t kPersistBytes = 8u * 1024u;
constexpr std::size_t kScratchBytes = 4u * 1024u;
constexpr std::size_t kSlotCount = 8;

MatrixMutView fill(Arena& a, std::uint8_t r, std::uint8_t c, int seed) {
		MatrixMutView m;
		assert(matrix_alloc(a, r, c, &m) == ErrorCode::Ok);
		for (std::uint8_t i = 0; i < r; i++) {
				for (std::uint8_t j = 0; j < c; j++)
						m.at_mut(i, j) = Rational::from_int((i * 7 + j * 3 + seed) % 11 + 1);
		}
		return m;
}

char g_caption[128];
char g_latex[1024]; // same as App::step_latex_

} // namespace

int main() {
		Slab slab;
		assert(slab.init(kPersistBytes + kScratchBytes) == ErrorCode::Ok);
		Arena persist(slab.data(), kPersistBytes);
		Arena scratch(slab.data() + kPersistBytes, kScratchBytes);

		// slots are allocated once at startup and never released
		for (std::size_t i = 0; i < kSlotCount; i++) {
				MatrixMutView backing;
				assert(matrix_alloc(persist, 6, 6, &backing) == ErrorCode::Ok);
		}
		const std::size_t base = persist.mark();
		assert(base == 4608);

		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &scratch};

		// every operation gets a clean arena pair, exactly as the shell gives it
		const auto reset = [&]() {
				persist.rewind(base);
				scratch.clear();
		};

		// solve Ax = b, widest augmented matrix the shell builds
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 1);
				MatrixMutView b = fill(persist, 6, 1, 5);
				MatrixMutView xp{}, nb{};
				ParametricSolution info{};
				Explanation e;
				assert(is_ok(op_solve_parametric(
				        a.view(), b.view(), persist, scratch, &xp, &nb, &info, &e, ExplainOptions{.enable = true, .persist = &persist})));
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) != ErrorCode::Overflow);
		}

		// independence / transformation
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 2);
				MatrixMutView rref{};
				DependencyInfo info{};
				Explanation e;
				assert(is_ok(op_dependency(a.view(), persist, &rref, &info, &e, ExplainOptions{.enable = true, .persist = &persist})));
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) != ErrorCode::Overflow);
		}

#if MATRIX_MODULE_COLROW
		// column row expansion: the sum step allocates one term per column
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 3);
				MatrixMutView b = fill(persist, 6, 6, 4);
				MatrixMutView out{};
				assert(matrix_alloc(persist, 6, 6, &out) == ErrorCode::Ok);
				Explanation e;
				assert(is_ok(op_mul_colrow(a.view(), b.view(), out, &e, ExplainOptions{.enable = true, .persist = &persist})));
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) != ErrorCode::Overflow);
		}
#endif

		// inverse via [A | I]
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 6);
				MatrixMutView out{};
				assert(matrix_alloc(persist, 6, 6, &out) == ErrorCode::Ok);
				Explanation e;
				if (is_ok(op_inverse(a.view(), scratch, out, &e, ExplainOptions{.enable = true, .persist = &persist}))) {
						for (std::size_t i = 0; i < e.step_count(); i++)
								assert(e.render_step(i, bufs) != ErrorCode::Overflow);
				}
		}

		// echelon plus every space basis
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 9);
				MatrixMutView rref{};
				assert(matrix_alloc(scratch, 6, 6, &rref) == ErrorCode::Ok);
				Explanation e;
				assert(is_ok(op_echelon(a.view(), EchelonKind::Rref, rref, &e, ExplainOptions{.enable = true, .persist = &persist})));
				SpaceInfo info{};
				assert(space_info_from_rref(rref.view(), 6, &info) == ErrorCode::Ok);
				MatrixMutView basis{};
				assert(space_col_basis(a.view(), info, persist, &basis) == ErrorCode::Ok);
				assert(space_row_basis(rref.view(), 6, info, persist, &basis) == ErrorCode::Ok);
				assert(space_null_basis(rref.view(), 6, info, persist, &basis) == ErrorCode::Ok);
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) != ErrorCode::Overflow);
		}

#if MATRIX_MODULE_BASIS
		// Col(A) / Nul(A): both bases stay in persist for the result page
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 11);
				MatrixMutView col{}, nul{};
				BasisInfo info{};
				Explanation e;
				assert(is_ok(
				        op_basis(a.view(), persist, scratch, &col, &nul, &info, &e, ExplainOptions{.enable = true, .persist = &persist})));
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) != ErrorCode::Overflow);
		}
#endif

#if MATRIX_MODULE_EXPANSION
		// cofactor expansion: every term's step writes out a 5x5 minor, and has
		// to fit the step buffer, not just the arenas
		for (std::uint8_t line = 0; line < 2; line++) {
				reset();
				MatrixMutView a = fill(persist, 6, 6, 12);
				Rational det;
				Explanation e;
				assert(is_ok(op_cofactor_expansion(a.view(),
				        line == 0 ? ExpansionLine::Row : ExpansionLine::Col,
				        5,
				        scratch,
				        &det,
				        &e,
				        ExplainOptions{.enable = true, .persist = &persist})));
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) == ErrorCode::Ok);
		}
#endif

#if MATRIX_MODULE_ADJUGATE
		// adjoint: adj(A) in persist, every 5x5 minor written out in the step buffer,
		// and an invertible A so the 6x6 inverse is built in scratch too
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 15);
				a.at_mut(0, 0) = Rational::from_int(20);
				MatrixMutView out{};
				assert(matrix_alloc(persist, 6, 6, &out) == ErrorCode::Ok);
				Explanation e;
				Rational det;
				assert(is_ok(op_adjugate(
				        a.view(), scratch, out, &e, ExplainOptions{.enable = true, .persist = &persist}, AdjugateLabels{}, &det)));
				assert(!det.is_zero());
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) == ErrorCode::Ok);
		}
#endif

#if MATRIX_MODULE_TRIANGULAR
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 13);
				for (std::uint8_t r = 0; r < 6; r++) {
						for (std::uint8_t c = 0; c < r; c++)
								a.at_mut(r, c) = Rational::from_int(0);
				}
				Rational det;
				Explanation e;
				assert(is_ok(op_det_triangular(a.view(), &det, &e, ExplainOptions{.enable = true, .persist = &persist})));
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) == ErrorCode::Ok);
		}
#endif

#if MATRIX_MODULE_INDEP_DET
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 14);
				Rational det;
				bool independent = false;
				Explanation e;
				assert(is_ok(op_indep_det(a.view(), scratch, &det, &independent, &e, ExplainOptions{.enable = true, .persist = &persist})));
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) == ErrorCode::Ok);
		}
#endif

#if MATRIX_MODULE_AREA
		// area of T(P) from four vertices, with both sketch polygons in persist
		{
				reset();
				MatrixMutView a = fill(persist, 2, 2, 16);
				MatrixMutView p = fill(persist, 2, 4, 0);
				for (std::uint8_t i = 0; i < 2; i++)
						p.at_mut(i, 3) = Rational::from_int(p.at(i, 1).num() + p.at(i, 2).num() - p.at(i, 0).num());
				MatrixMutView out{};
				assert(matrix_alloc(persist, 5, 4, &out) == ErrorCode::Ok);
				Explanation e;
				assert(is_ok(op_area_transform(a.view(),
				        p.view(),
				        scratch,
				        &out.at_mut(4, 0),
				        &out.at_mut(4, 1),
				        &out.at_mut(4, 2),
				        &e,
				        ExplainOptions{.enable = true, .persist = &persist},
				        AreaLabels{},
				        MatrixMutView{2, 4, out.stride, out.data},
				        MatrixMutView{2, 4, out.stride, out.data + 2u * out.stride})));
				for (std::size_t i = 0; i < e.step_count(); i++)
						assert(e.render_step(i, bufs) == ErrorCode::Ok);
		}
#endif

#if MATRIX_MODULE_LU
		// LU factorisation is the persist high water mark
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 10);
				MatrixMutView l{}, u{}, pm{};
				assert(matrix_alloc(persist, 6, 6, &l) == ErrorCode::Ok);
				assert(matrix_alloc(persist, 6, 6, &u) == ErrorCode::Ok);
				assert(matrix_alloc(persist, 6, 6, &pm) == ErrorCode::Ok);
				Explanation e;
				if (is_ok(op_lu_factor(
				            a.view(), LuPivoting::Partial, pm, l, u, nullptr, &e, ExplainOptions{.enable = true, .persist = &persist}))) {
						for (std::size_t i = 0; i < e.step_count(); i++)
								assert(e.render_step(i, bufs) != ErrorCode::Overflow);
				}
		}

		// LU solve is the scratch high water mark
		{
				reset();
				MatrixMutView a = fill(persist, 6, 6, 7);
				MatrixMutView b = fill(persist, 6, 1, 8);
				MatrixMutView y{}, x{}, rr{};
				assert(matrix_alloc(persist, 6, 1, &y) == ErrorCode::Ok);
				assert(matrix_alloc(persist, 6, 1, &x) == ErrorCode::Ok);
				assert(matrix_alloc(persist, 6, 7, &rr) == ErrorCode::Ok);
				LuSolveInfo info{};
				Explanation e;
				if (is_ok(op_lu_solve(a.view(),
				            b.view(),
				            LuPivoting::Partial,
				            scratch,
				            y,
				            x,
				            rr,
				            &info,
				            nullptr,
				            &e,
				            ExplainOptions{.enable = true, .persist = &persist}))) {
						for (std::size_t i = 0; i < e.step_count(); i++)
								assert(e.render_step(i, bufs) != ErrorCode::Overflow);
				}
		}
#endif

		// every allocation above came out of the real budget, so reaching here
		// means the shell's arenas are large enough for all of it
#if defined(MATRIX_CE_TESTS)
		dbg_printf("[test_arena_budget] all operations fit\n");
#endif
		return 0;
}
