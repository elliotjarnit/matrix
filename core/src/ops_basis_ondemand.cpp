#include "matrix_core/ops.hpp"

#include "matrix_core/latex.hpp"
#include "matrix_core/row_ops.hpp"
#include "matrix_core/row_reduction.hpp"
#include "matrix_core/spaces.hpp"
#include "matrix_core/step_detail.hpp"
#include "matrix_core/writer.hpp"

#include <new>

namespace matrix_core {
namespace {

constexpr std::uint8_t kNoPivot = 0xFF;

// steps before the row reduction: the definitions of Col(A) and Nul(A)
constexpr std::size_t kHeadSteps = 2;

// steps after it
enum class Tail : std::uint8_t {
		Pivots = 0,
		ColBasis,
		NulSolve,
		NulVector,
		NulBasis,
		RankTheorem,
		Count,
};
constexpr std::size_t kTailSteps = static_cast<std::size_t>(Tail::Count);

struct BasisCtx {
		MatrixView input{};      // A, owned by the caller and outlives this context
		MatrixView col_basis{};  // m x rank (or the zero column), in persist
		MatrixView null_basis{}; // n x nullity (or the zero column), in persist
		BasisInfo info{};
		BasisLabels labels{}; // copied by value: the pointed to strings are caller owned
		std::size_t op_count = 0;
};

// "R^{3}"
ErrorCode append_rn(std::uint8_t n, Writer& w) noexcept {
		ErrorCode ec = w.append("R^{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_u64(n);
		if (!is_ok(ec))
				return ec;
		return w.put('}');
}

// "x_{3}" for a 0 based column
ErrorCode append_var(std::uint8_t col, Writer& w) noexcept {
		ErrorCode ec = w.append("x_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(col);
		if (!is_ok(ec))
				return ec;
		return w.put('}');
}

MatrixView column_of(MatrixView m, std::uint8_t c) noexcept {
		return MatrixView{m.rows, 1, m.stride, &m.at(0, c)};
}

// "\text{Span}\left\lbrace [v_1], [v_2] \right\rbrace" over the first `count`
// columns of `basis`
ErrorCode append_span(MatrixView basis, std::uint8_t count, Writer& w) noexcept {
		ErrorCode ec = w.append("\\text{Span}\\left\\lbrace ");
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t k = 0; k < count; k++) {
				if (k != 0) {
						ec = w.append(", ");
						if (!is_ok(ec))
								return ec;
				}
				ec = latex::append_matrix(column_of(basis, k), latex::MatrixBrackets::BMatrix, w);
				if (!is_ok(ec))
						return ec;
		}
		return w.append("\\right\\rbrace");
}

// "$$\dim\text{Col}(A) = 2$$"
ErrorCode append_dim_line(const char* space, const char* middle, std::uint8_t dim, Writer& w) noexcept {
		ErrorCode ec = w.append("$$\\dim\\text{");
		if (!is_ok(ec))
				return ec;
		ec = w.append(space);
		if (!is_ok(ec))
				return ec;
		ec = w.append("}(A) = ");
		if (!is_ok(ec))
				return ec;
		if (middle) {
				ec = w.append(middle);
				if (!is_ok(ec))
						return ec;
		}
		ec = w.append_u64(dim);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

// "$$\text{Nul}(A) = \lbrace 0\rbrace = \text{Span}\,\emptyset$$": the zero
// subspace, whose basis is the empty set
ErrorCode append_zero_space(const char* space, Writer& w) noexcept {
		ErrorCode ec = w.append("$$\\text{");
		if (!is_ok(ec))
				return ec;
		ec = w.append(space);
		if (!is_ok(ec))
				return ec;
		return w.append("}(A) = \\lbrace 0\\rbrace = \\text{Span}\\,\\emptyset$$\n");
}

std::size_t basis_step_count(const void* vctx) noexcept {
		const auto* ctx = static_cast<const BasisCtx*>(vctx);
		return kHeadSteps + ctx->op_count + kTailSteps;
}

ErrorCode render_col_def(const BasisCtx* ctx, Writer& w) noexcept {
		ErrorCode ec = w.append("$$\\text{Col}(A) = \\text{Span}\\lbrace ");
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t c = 0; c < ctx->info.cols; c++) {
				if (c != 0) {
						ec = w.append(", ");
						if (!is_ok(ec))
								return ec;
				}
				ec = w.append("a_{");
				if (!is_ok(ec))
						return ec;
				ec = w.append_index1(c);
				if (!is_ok(ec))
						return ec;
				ec = w.put('}');
				if (!is_ok(ec))
						return ec;
		}
		ec = w.append("\\rbrace \\subseteq ");
		if (!is_ok(ec))
				return ec;
		ec = append_rn(ctx->info.rows, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append("$$\n$$A = ");
		if (!is_ok(ec))
				return ec;
		ec = latex::append_matrix(ctx->input, latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

ErrorCode render_nul_def(const BasisCtx* ctx, Arena& scratch, Writer& w) noexcept {
		ErrorCode ec = w.append("$$\\text{Nul}(A) = \\lbrace x \\in ");
		if (!is_ok(ec))
				return ec;
		ec = append_rn(ctx->info.cols, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" : Ax = 0\\rbrace$$\n$$");
		if (!is_ok(ec))
				return ec;

		MatrixMutView zero{};
		ec = matrix_alloc(scratch, ctx->info.rows, 1, &zero);
		if (!is_ok(ec))
				return ec;
		matrix_fill_zero(zero);
		ec = latex::append_augmented_matrix(ctx->input, zero.view(), w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

ErrorCode render_col_basis(const BasisCtx* ctx, Writer& w) noexcept {
		const BasisInfo& info = ctx->info;
		ErrorCode ec = ErrorCode::Ok;
		if (info.rank == 0) {
				ec = append_zero_space("Col", w);
		} else {
				ec = w.append("$$\\text{Col}(A) = ");
				if (!is_ok(ec))
						return ec;
				ec = append_span(ctx->col_basis, info.rank, w);
				if (!is_ok(ec))
						return ec;
				ec = w.append("$$\n");
		}
		if (!is_ok(ec))
				return ec;

		ec = append_dim_line("Col", "\\text{rank}(A) = ", info.rank, w);
		if (!is_ok(ec))
				return ec;

		// every row has a pivot: the columns span all of R^m
		if (info.rank != 0 && info.rank == info.rows) {
				ec = w.append("\n$$\\text{Col}(A) = ");
				if (!is_ok(ec))
						return ec;
				ec = append_rn(info.rows, w);
				if (!is_ok(ec))
						return ec;
				ec = w.append("$$");
		}
		return ec;
}

// "x_{1} = -2x_{3} + x_{4}" for every pivot variable, then "x_{3}, x_{4} free".
// Each coefficient is read from the null space basis, whose entry in a pivot
// row is already the negated RREF entry.
ErrorCode render_nul_solve(const BasisCtx* ctx, Writer& w) noexcept {
		const BasisInfo& info = ctx->info;
		if (info.nullity == 0)
				return w.append("$$x = 0$$");

		ErrorCode ec = w.append("$$\\begin{array}{l}");
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t i = 0; i < info.rank; i++) {
				const std::uint8_t pc = info.pivot_cols[i];
				ec = append_var(pc, w);
				if (!is_ok(ec))
						return ec;
				ec = w.append(" = ");
				if (!is_ok(ec))
						return ec;

				bool wrote = false;
				for (std::uint8_t k = 0; k < info.nullity; k++) {
						const Rational& c = ctx->null_basis.at(pc, k);
						if (c.is_zero())
								continue;
						ec = latex::append_signed_term(c, "x", info.free_cols[k], !wrote, w);
						if (!is_ok(ec))
								return ec;
						wrote = true;
				}
				if (!wrote) {
						ec = w.put('0');
						if (!is_ok(ec))
								return ec;
				}
				ec = w.append(" \\\\ ");
				if (!is_ok(ec))
						return ec;
		}

		for (std::uint8_t k = 0; k < info.nullity; k++) {
				if (k != 0) {
						ec = w.append(",\\, ");
						if (!is_ok(ec))
								return ec;
				}
				ec = append_var(info.free_cols[k], w);
				if (!is_ok(ec))
						return ec;
		}
		ec = w.append("\\;\\text{");
		if (!is_ok(ec))
				return ec;
		ec = w.append(detail::lbl(ctx->labels.free_word, "free"));
		if (!is_ok(ec))
				return ec;
		return w.append("}\\end{array}$$");
}

// "x = x_{3}[v_1] + x_{4}[v_2]"
ErrorCode render_nul_vector(const BasisCtx* ctx, Writer& w) noexcept {
		const BasisInfo& info = ctx->info;
		if (info.nullity == 0)
				return w.append("$$x = 0$$");

		ErrorCode ec = w.append("$$x = ");
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t k = 0; k < info.nullity; k++) {
				if (k != 0) {
						ec = w.append(" + ");
						if (!is_ok(ec))
								return ec;
				}
				ec = append_var(info.free_cols[k], w);
				if (!is_ok(ec))
						return ec;
				ec = latex::append_matrix(column_of(ctx->null_basis, k), latex::MatrixBrackets::BMatrix, w);
				if (!is_ok(ec))
						return ec;
		}
		return w.append("$$");
}

ErrorCode render_nul_basis(const BasisCtx* ctx, Writer& w) noexcept {
		const BasisInfo& info = ctx->info;
		ErrorCode ec = ErrorCode::Ok;
		if (info.nullity == 0) {
				ec = append_zero_space("Nul", w);
		} else {
				ec = w.append("$$\\text{Nul}(A) = ");
				if (!is_ok(ec))
						return ec;
				ec = append_span(ctx->null_basis, info.nullity, w);
				if (!is_ok(ec))
						return ec;
				ec = w.append("$$\n");
		}
		if (!is_ok(ec))
				return ec;
		return append_dim_line("Nul", nullptr, info.nullity, w);
}

// "rank(A) + dim Nul(A) = n", then the numbers: "2 + 1 = 3"
ErrorCode render_rank_theorem(const BasisCtx* ctx, Writer& w) noexcept {
		const BasisInfo& info = ctx->info;
		ErrorCode ec = w.append("$$\\text{rank}(A) + \\dim\\text{Nul}(A) = n$$\n$$");
		if (!is_ok(ec))
				return ec;
		ec = w.append_u64(info.rank);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" + ");
		if (!is_ok(ec))
				return ec;
		ec = w.append_u64(info.nullity);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = ");
		if (!is_ok(ec))
				return ec;
		ec = w.append_u64(static_cast<std::uint64_t>(info.rank) + info.nullity);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = n$$");
		return ec;
}

// "Pivot columns: 1, 3"
void caption_pivots(const BasisCtx* ctx, Writer& w) noexcept {
		(void)w.append(detail::lbl(ctx->labels.pivots, "Pivot columns"));
		(void)w.append(": ");
		if (ctx->info.rank == 0) {
				(void)w.put('-');
				return;
		}
		for (std::uint8_t i = 0; i < ctx->info.rank; i++) {
				if (i != 0)
						(void)w.append(", ");
				(void)w.append_index1(ctx->info.pivot_cols[i]);
		}
}

ErrorCode basis_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const BasisCtx*>(vctx);
		if (!ctx->input.data || !ctx->col_basis.data || !ctx->null_basis.data)
				return ErrorCode::Internal;
		if (!out.scratch)
				return ErrorCode::Internal;

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		Writer cw{out.caption, out.caption_cap, 0};
		Writer w{out.latex, out.latex_cap, 0};

		if (index == 0) {
				(void)cw.append(detail::lbl(ctx->labels.col_def, "Col(A) is the span of the columns of A"));
				return render_col_def(ctx, w);
		}
		if (index == 1) {
				(void)cw.append(detail::lbl(ctx->labels.nul_def, "Nul(A) is every solution of $Ax = 0$. Row reduce A"));
				return render_nul_def(ctx, *out.scratch, w);
		}

		const std::size_t op_index = index - kHeadSteps + 1; // 1 based row operation
		if (op_index <= ctx->op_count) {
				MatrixMutView work{};
				RowOp last_op{};
				ErrorCode ec = detail::replay_echelon(ctx->input, EchelonKind::Rref, op_index, *out.scratch, &work, &last_op);
				if (!is_ok(ec))
						return ec;
				detail::step_row_caption(last_op, op_index == ctx->op_count, ctx->labels.rref, out.caption, out.caption_cap);
				return latex::write_matrix_display(work.view(), latex::MatrixBrackets::BMatrix, {out.latex, out.latex_cap});
		}

		const std::size_t tail = index - kHeadSteps - ctx->op_count;
		if (tail >= kTailSteps)
				return ErrorCode::StepOutOfRange;

		switch (static_cast<Tail>(tail)) {
		case Tail::Pivots: {
				caption_pivots(ctx, cw);
				MatrixMutView rref{};
				RowOp last_op{};
				ErrorCode ec = detail::replay_echelon(ctx->input, EchelonKind::Rref, ctx->op_count, *out.scratch, &rref, &last_op);
				if (!is_ok(ec))
						return ec;
				return latex::write_matrix_display(rref.view(), latex::MatrixBrackets::BMatrix, {out.latex, out.latex_cap});
		}
		case Tail::ColBasis:
				(void)cw.append(detail::lbl(ctx->labels.col_basis, "The pivot columns of A form a basis for Col(A)"));
				return render_col_basis(ctx, w);
		case Tail::NulSolve:
				(void)cw.append(detail::lbl(ctx->labels.nul_solve, "Solve $Ax = 0$ for the pivot variables"));
				return render_nul_solve(ctx, w);
		case Tail::NulVector:
				(void)cw.append(detail::lbl(ctx->labels.nul_vector, "Write x in terms of the free variables"));
				return render_nul_vector(ctx, w);
		case Tail::NulBasis:
				(void)cw.append(detail::lbl(ctx->labels.nul_basis, "Those vectors form a basis for Nul(A)"));
				return render_nul_basis(ctx, w);
		case Tail::RankTheorem:
				(void)cw.append(detail::lbl(ctx->labels.rank_theorem, "Rank theorem"));
				return render_rank_theorem(ctx, w);
		case Tail::Count:
				break;
		}
		return ErrorCode::StepOutOfRange;
}

constexpr ExplanationVTable kBasisVTable = {
        .step_count = &basis_step_count,
        .render_step = &basis_render_step,
        .destroy = nullptr,
};

} // namespace

Error op_basis(MatrixView a,
        Arena& persist,
        Arena& scratch,
        MatrixMutView* out_col_basis,
        MatrixMutView* out_null_basis,
        BasisInfo* info,
        Explanation* expl,
        const ExplainOptions& opts,
        const BasisLabels& labels) noexcept {
		if (!out_col_basis || !out_null_basis || !info)
				return {ErrorCode::Internal};
		if (!a.data)
				return {ErrorCode::Internal};
		if (a.rows == 0 || a.cols == 0 || a.rows > kMaxRows || a.cols > kMaxCols)
				return err_invalid_dim(a.dim());
		if (opts.enable && opts.persist != &persist)
				return {ErrorCode::Internal};

		ArenaScope tx(persist);
		// the RREF is only needed to derive the bases; the steps replay it
		ArenaScope scratch_tx(scratch);

		MatrixMutView rref{};
		ErrorCode ec = matrix_clone(scratch, a, &rref);
		if (!is_ok(ec))
				return {ec};

		OpObserver obs;
		obs.target = static_cast<std::size_t>(-1);
		ec = echelon_apply(rref, EchelonKind::Rref, &obs);
		if (!is_ok(ec))
				return {ec};

		SpaceInfo space{};
		ec = space_info_from_rref(rref.view(), a.cols, &space);
		if (!is_ok(ec))
				return {ec};

		MatrixMutView col_basis{};
		ec = space_col_basis(a, space, persist, &col_basis);
		if (!is_ok(ec))
				return {ec};

		MatrixMutView null_basis{};
		ec = space_null_basis(rref.view(), a.cols, space, persist, &null_basis);
		if (!is_ok(ec))
				return {ec};

		BasisInfo& result = *info;
		result = BasisInfo{};
		result.rows = a.rows;
		result.cols = a.cols;
		result.rank = space.rank;
		result.nullity = space.nullity;
		result.pivot_mask = space.pivot_mask;
		for (std::uint8_t i = 0; i < space.rank; i++)
				result.pivot_cols[i] = space.pivot_cols[i];
		std::uint8_t free_count = 0;
		for (std::uint8_t c = 0; c < a.cols; c++) {
				if (space.pivot_row_for_col[c] == kNoPivot)
						result.free_cols[free_count++] = c;
		}

		if (opts.enable) {
				if (!expl)
						return {ErrorCode::Internal};

				void* mem = persist.allocate(sizeof(BasisCtx), alignof(BasisCtx));
				if (!mem)
						return {ErrorCode::Overflow};
				auto* ctx = new (mem) BasisCtx{};
				ctx->input = a;
				ctx->col_basis = col_basis.view();
				ctx->null_basis = null_basis.view();
				ctx->info = result;
				ctx->labels = labels;
				ctx->op_count = obs.count;
				*expl = Explanation::make(ctx, &kBasisVTable);
		}

		*out_col_basis = col_basis;
		*out_null_basis = null_basis;

		tx.commit();
		return {};
}

} // namespace matrix_core
