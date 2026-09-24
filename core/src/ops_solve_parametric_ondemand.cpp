#include "matrix_core/ops.hpp"

#include "matrix_core/latex.hpp"
#include "matrix_core/row_ops.hpp"
#include "matrix_core/row_reduction.hpp"
#include "matrix_core/spaces.hpp"
#include "matrix_core/writer.hpp"

#include <new>

namespace matrix_core {
namespace {

constexpr std::uint8_t kNoPivot = 0xFF;

// steps appended after the row reduction
constexpr std::size_t kTailConsistent = 3; // equations, homogeneous, complete
constexpr std::size_t kTailInconsistent = 1;

struct SolveParamCtx {
		MatrixView aug{};        // [A | b], allocated from persist by the op
		MatrixView particular{}; // x_p, n x 1
		MatrixView basis{};      // Null(A) basis, n x k (or the zero vector when k == 0)
		ParametricSolution info{};
		ParametricLabels labels{}; // copied by value: the pointed to strings are caller owned
		std::size_t op_count = 0;
};

const char* lbl(const char* value, const char* fallback) noexcept {
		return value ? value : fallback;
}

// a Writer positioned at the end of the text already in `buf`, so a caption can
// be extended without rebuilding it
Writer writer_at_end(char* buf, std::size_t cap) noexcept {
		Writer w{buf, cap, 0};
		if (!buf || cap == 0)
				return w;
		while (w.len + 1 < cap && buf[w.len] != '\0')
				w.len++;
		return w;
}

// "x_{3}" for the 0 based variable index `col`
ErrorCode write_var(std::uint8_t col, Writer& w) noexcept {
		ErrorCode ec = w.append("x_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(col);
		if (!is_ok(ec))
				return ec;
		return w.append("}");
}

// "t_{2}" for the 0 based parameter index `k`
ErrorCode write_param(std::uint8_t k, Writer& w) noexcept {
		ErrorCode ec = w.append("t_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(k);
		if (!is_ok(ec))
				return ec;
		return w.append("}");
}

constexpr bool is_one(const Rational& r) noexcept {
		return r.num() == 1 && r.den() == 1;
}

// writes a signed term "c x_{j}", prefixed with " + " or " - " unless it leads
// the expression. `c` must be non zero; a coefficient of 1 is elided.
ErrorCode write_term(const Rational& c, std::uint8_t var_col, bool leading, Writer& w) noexcept {
		Rational mag = c;
		if (c.num() < 0) {
				ErrorCode ec = rational_neg(c, &mag);
				if (!is_ok(ec))
						return ec;
				ec = w.append(leading ? "-" : " - ");
				if (!is_ok(ec))
						return ec;
		} else if (!leading) {
				ErrorCode ec = w.append(" + ");
				if (!is_ok(ec))
						return ec;
		}

		if (!is_one(mag)) {
				ErrorCode ec = w.append_rational_latex(mag);
				if (!is_ok(ec))
						return ec;
		}
		return write_var(var_col, w);
}

// "x_{1} = 10 - 2x_{2}" for one pivot row of the RREF
ErrorCode write_pivot_equation(MatrixView rref, const ParametricSolution& info, std::uint8_t pivot_index, Writer& w) noexcept {
		const std::uint8_t pc = info.pivot_cols[pivot_index];
		const std::uint8_t pr = info.pivot_rows[pivot_index];
		const std::uint8_t n = info.var_count;

		ErrorCode ec = write_var(pc, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = ");
		if (!is_ok(ec))
				return ec;

		bool wrote = false;

		const Rational& konst = rref.at(pr, n);
		if (!konst.is_zero()) {
				ec = w.append_rational_latex(konst);
				if (!is_ok(ec))
						return ec;
				wrote = true;
		}

		for (std::uint8_t k = 0; k < info.nullity; k++) {
				const std::uint8_t fc = info.free_cols[k];
				const Rational& coef = rref.at(pr, fc);
				if (coef.is_zero())
						continue;
				Rational neg{};
				ec = rational_neg(coef, &neg);
				if (!is_ok(ec))
						return ec;
				ec = write_term(neg, fc, !wrote, w);
				if (!is_ok(ec))
						return ec;
				wrote = true;
		}

		if (!wrote)
				return w.append("0");
		return ErrorCode::Ok;
}

ErrorCode write_augmented(MatrixView aug, std::uint8_t var_cols, char* buf, std::size_t cap) noexcept {
		const MatrixView left{aug.rows, var_cols, aug.stride, aug.data};
		const MatrixView right{aug.rows, 1, aug.stride, aug.data + var_cols};
		return latex::write_augmented_matrix_display(left, right, {buf, cap});
}

// replays the row reduction into scratch, returning the RREF of [A | b]
ErrorCode replay_rref(const SolveParamCtx* ctx, Arena& scratch, MatrixMutView* out) noexcept {
		MatrixMutView work{};
		ErrorCode ec = matrix_clone(scratch, ctx->aug, &work);
		if (!is_ok(ec))
				return ec;
		ec = echelon_apply(work, EchelonKind::Rref, nullptr);
		if (!is_ok(ec))
				return ec;
		*out = work;
		return ErrorCode::Ok;
}

std::size_t solve_param_step_count(const void* vctx) noexcept {
		const auto* ctx = static_cast<const SolveParamCtx*>(vctx);
		const std::size_t tail = ctx->info.consistent ? kTailConsistent : kTailInconsistent;
		return 1 + ctx->op_count + tail;
}

// caption appends are best effort: a caption that does not fit is truncated
// rather than failing the whole step
void caption_free_vars(const SolveParamCtx* ctx, Writer& w) noexcept {
		if (ctx->info.nullity == 0)
				return;
		(void)w.append(" (");
		(void)w.append(lbl(ctx->labels.free_vars, "free"));
		(void)w.append(": $");
		for (std::uint8_t k = 0; k < ctx->info.nullity; k++) {
				if (k != 0)
						(void)w.append(", ");
				(void)write_var(ctx->info.free_cols[k], w);
		}
		(void)w.append("$)");
}

void caption_parameter_map(const SolveParamCtx* ctx, Writer& w) noexcept {
		if (ctx->info.nullity == 0)
				return;
		(void)w.append(" ($");
		for (std::uint8_t k = 0; k < ctx->info.nullity; k++) {
				if (k != 0)
						(void)w.append(",\\; ");
				(void)write_var(ctx->info.free_cols[k], w);
				(void)w.append(" = ");
				(void)write_param(k, w);
		}
		(void)w.append("$)");
}

ErrorCode render_equations_step(const SolveParamCtx* ctx, MatrixView rref, const StepRenderBuffers& out) noexcept {
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append(lbl(ctx->labels.solve_pivots, "Solve each row for its pivot variable"));
				caption_free_vars(ctx, cw);
		}

		// no pivot rows: nothing to solve, show the reduced matrix instead
		if (ctx->info.rank == 0)
				return write_augmented(rref, ctx->info.var_count, out.latex, out.latex_cap);

		Writer w{out.latex, out.latex_cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';

		ErrorCode ec = w.append("$$\\begin{array}{l}");
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t i = 0; i < ctx->info.rank; i++) {
				if (i != 0) {
						ec = w.append(" \\\\ ");
						if (!is_ok(ec))
								return ec;
				}
				ec = write_pivot_equation(rref, ctx->info, i, w);
				if (!is_ok(ec))
						return ec;
		}
		return w.append("\\end{array}$$");
}

ErrorCode render_homogeneous_step(const SolveParamCtx* ctx, const StepRenderBuffers& out) noexcept {
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				if (ctx->info.nullity == 0) {
						(void)cw.append(lbl(ctx->labels.unique, "Unique solution"));
						(void)cw.append(": $x_{h} = 0$");
				} else {
						(void)cw.append(lbl(ctx->labels.homogeneous, "General solution of $Ax = 0$"));
						caption_parameter_map(ctx, cw);
				}
		}

		return latex::write_parametric_solution_display("x_{h}", MatrixView{}, ctx->basis, ctx->info.nullity, {out.latex, out.latex_cap});
}

ErrorCode render_complete_step(const SolveParamCtx* ctx, const StepRenderBuffers& out) noexcept {
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append(lbl(ctx->labels.complete, "Complete solution"));
				(void)cw.append(": $x = x_{p} + x_{h}$");
		}

		return latex::write_parametric_solution_display("x", ctx->particular, ctx->basis, ctx->info.nullity, {out.latex, out.latex_cap});
}

ErrorCode render_inconsistent_step(const SolveParamCtx* ctx, MatrixView rref, const StepRenderBuffers& out) noexcept {
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append("$R_{");
				(void)cw.append_index1(ctx->info.inconsistent_row);
				(void)cw.append("}$: $0 = ");
				(void)cw.append_rational_latex(rref.at(ctx->info.inconsistent_row, ctx->info.var_count));
				(void)cw.append("$ - ");
				(void)cw.append(lbl(ctx->labels.inconsistent, "no solution"));
		}

		return write_augmented(rref, ctx->info.var_count, out.latex, out.latex_cap);
}

ErrorCode solve_param_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const SolveParamCtx*>(vctx);
		if (!ctx->aug.data)
				return ErrorCode::Internal;
		if (!out.scratch)
				return ErrorCode::Internal;

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		const std::uint8_t n = ctx->info.var_count;

		if (index == 0) {
				if (out.caption && out.caption_cap) {
						Writer cw{out.caption, out.caption_cap, 0};
						(void)cw.append(lbl(ctx->labels.augmented, "Augmented matrix [A | b]"));
				}
				return write_augmented(ctx->aug, n, out.latex, out.latex_cap);
		}

		if (index <= ctx->op_count) {
				MatrixMutView work{};
				ErrorCode ec = matrix_clone(*out.scratch, ctx->aug, &work);
				if (!is_ok(ec))
						return ec;

				OpObserver obs;
				obs.target = index; // stop after applying `index` ops
				ec = echelon_apply(work, EchelonKind::Rref, &obs);
				if (!is_ok(ec))
						return ec;
				if (obs.count < index)
						return ErrorCode::StepOutOfRange;

				if (out.caption && out.caption_cap) {
						// captions are best effort: a truncated caption must not
						// cost the student the matrix itself
						ec = row_op_caption(obs.last_op, out.caption, out.caption_cap);
						if (!is_ok(ec) && ec != ErrorCode::BufferTooSmall)
								return ec;
						if (index == ctx->op_count) {
								Writer cw = writer_at_end(out.caption, out.caption_cap);
								(void)cw.append("  ");
								(void)cw.append(lbl(ctx->labels.rref, "RREF"));
						}
				}

				return write_augmented(work.view(), n, out.latex, out.latex_cap);
		}

		const std::size_t tail = index - ctx->op_count - 1;
		const std::size_t tail_count = ctx->info.consistent ? kTailConsistent : kTailInconsistent;
		if (tail >= tail_count)
				return ErrorCode::StepOutOfRange;

		MatrixMutView rref{};
		ErrorCode ec = replay_rref(ctx, *out.scratch, &rref);
		if (!is_ok(ec))
				return ec;

		if (!ctx->info.consistent)
				return render_inconsistent_step(ctx, rref.view(), out);

		switch (tail) {
		case 0:
				return render_equations_step(ctx, rref.view(), out);
		case 1:
				return render_homogeneous_step(ctx, out);
		default:
				return render_complete_step(ctx, out);
		}
}

constexpr ExplanationVTable kSolveParamVTable = {
        .step_count = &solve_param_step_count,
        .render_step = &solve_param_render_step,
        .destroy = nullptr,
};

} // namespace

Error op_solve_parametric(MatrixView a,
        MatrixView b,
        Arena& persist,
        Arena& scratch,
        MatrixMutView* out_particular,
        MatrixMutView* out_null_basis,
        ParametricSolution* info,
        Explanation* expl,
        const ExplainOptions& opts,
        const ParametricLabels& labels) noexcept {
		if (!out_particular || !out_null_basis || !info)
				return {ErrorCode::Internal};
		if (!a.data || !b.data)
				return {ErrorCode::Internal};
		if (a.rows == 0 || a.cols == 0)
				return err_invalid_dim(a.dim());
		if (b.cols != 1 || b.rows != a.rows)
				return err_dim_mismatch(a.dim(), b.dim());
		if (static_cast<std::size_t>(a.cols) + 1u > kMaxCols)
				return err_invalid_dim(a.dim());
		if (opts.enable && opts.persist != &persist)
				return {ErrorCode::Internal};

		const std::uint8_t m = a.rows;
		const std::uint8_t n = a.cols;
		const std::uint8_t aug_cols = static_cast<std::uint8_t>(n + 1u);

		ArenaScope tx(persist);

		// [A | b] lives in persist: the explanation replays the reduction from it
		MatrixMutView aug{};
		ErrorCode ec = matrix_alloc(persist, m, aug_cols, &aug);
		if (!is_ok(ec))
				return {ec};
		for (std::uint8_t r = 0; r < m; r++) {
				for (std::uint8_t c = 0; c < n; c++)
						aug.at_mut(r, c) = a.at(r, c);
				aug.at_mut(r, n) = b.at(r, 0);
		}

		MatrixMutView rref{};
		ec = matrix_clone(scratch, aug.view(), &rref);
		if (!is_ok(ec))
				return {ec};

		OpObserver obs;
		obs.target = static_cast<std::size_t>(-1);
		ec = echelon_apply(rref, EchelonKind::Rref, &obs);
		if (!is_ok(ec))
				return {ec};

		SpaceInfo space{};
		ec = space_info_from_rref(rref.view(), n, &space);
		if (!is_ok(ec))
				return {ec};

		ParametricSolution& result = *info;
		result = ParametricSolution{};
		result.var_count = n;
		result.rank = space.rank;
		result.nullity = space.nullity;
		result.pivot_mask = space.pivot_mask;
		for (std::uint8_t i = 0; i < space.rank; i++) {
				result.pivot_cols[i] = space.pivot_cols[i];
				result.pivot_rows[i] = space.pivot_row_for_col[space.pivot_cols[i]];
		}
		std::uint8_t free_count = 0;
		for (std::uint8_t c = 0; c < n; c++) {
				if (space.pivot_row_for_col[c] == kNoPivot)
						result.free_cols[free_count++] = c;
		}

		// a row that is zero across A but not in b is the contradiction 0 = c
		result.consistent = true;
		for (std::uint8_t r = 0; r < m; r++) {
				bool all_zero = true;
				for (std::uint8_t c = 0; c < n; c++) {
						if (!rref.at(r, c).is_zero()) {
								all_zero = false;
								break;
						}
				}
				if (all_zero && !rref.at(r, n).is_zero()) {
						result.consistent = false;
						result.inconsistent_row = r;
						break;
				}
		}

		// particular solution: every free variable set to zero
		MatrixMutView xp{};
		ec = matrix_alloc(persist, n, 1, &xp);
		if (!is_ok(ec))
				return {ec};
		matrix_fill_zero(xp);
		if (result.consistent) {
				for (std::uint8_t i = 0; i < space.rank; i++) {
						const std::uint8_t pc = space.pivot_cols[i];
						const std::uint8_t pr = space.pivot_row_for_col[pc];
						xp.at_mut(pc, 0) = rref.at(pr, n);
				}
		}

		MatrixMutView basis{};
		ec = space_null_basis(rref.view(), n, space, persist, &basis);
		if (!is_ok(ec))
				return {ec};

		if (opts.enable) {
				if (!expl)
						return {ErrorCode::Internal};

				void* mem = persist.allocate(sizeof(SolveParamCtx), alignof(SolveParamCtx));
				if (!mem)
						return {ErrorCode::Overflow};
				auto* ctx = new (mem) SolveParamCtx{};
				ctx->aug = aug.view();
				ctx->particular = xp.view();
				ctx->basis = basis.view();
				ctx->info = result;
				ctx->labels = labels;
				ctx->op_count = obs.count;
				*expl = Explanation::make(ctx, &kSolveParamVTable);
		}

		*out_particular = xp;
		*out_null_basis = basis;

		tx.commit();
		return {};
}

} // namespace matrix_core
