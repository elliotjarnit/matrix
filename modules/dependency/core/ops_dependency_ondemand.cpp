#include "matrix_core/ops_dependency.hpp"

#include "matrix_core/latex.hpp"
#include "matrix_core/row_ops.hpp"
#include "matrix_core/row_reduction.hpp"
#include "matrix_core/step_detail.hpp"
#include "matrix_core/spaces.hpp"
#include "matrix_core/writer.hpp"

#include <new>

namespace matrix_core {
namespace {

constexpr std::uint8_t kNoPivot = 0xFF;

// steps appended after the row reduction
constexpr std::size_t kTailIndependent = 1; // verdict
constexpr std::size_t kTailDependent = 2;   // verdict, relations

struct DependencyCtx {
		MatrixView input{}; // A, owned by the caller and outlives this context
		MatrixView rref{};  // RREF(A), allocated from persist by the op
		DependencyInfo info{};
		DependencyLabels labels{}; // copied by value: the pointed to strings are caller owned
		DependencyPurpose purpose = DependencyPurpose::Vectors;
		std::size_t op_count = 0;
};

// "v_{3} = 3v_{1} + v_{2}" for one dependent column
ErrorCode write_relation(const DependencyCtx* ctx, std::uint8_t dep_index, Writer& w) noexcept {
		const std::uint8_t f = ctx->info.dependent_cols[dep_index];

		ErrorCode ec = w.append("v_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(f);
		if (!is_ok(ec))
				return ec;
		ec = w.append("} = ");
		if (!is_ok(ec))
				return ec;

		bool wrote = false;
		for (std::uint8_t i = 0; i < ctx->info.rank; i++) {
				const Rational& c = ctx->rref.at(ctx->info.pivot_rows[i], f);
				if (c.is_zero())
						continue;
				ec = latex::append_signed_term(c, "v", ctx->info.pivot_cols[i], !wrote, w);
				if (!is_ok(ec))
						return ec;
				wrote = true;
		}

		// a dependent column with no pivot above it is the zero vector
		if (!wrote)
				return w.append("0");
		return ErrorCode::Ok;
}

std::size_t dependency_step_count(const void* vctx) noexcept {
		const auto* ctx = static_cast<const DependencyCtx*>(vctx);
		const bool relations = (ctx->purpose == DependencyPurpose::Vectors) && !ctx->info.independent;
		return 1 + ctx->op_count + (relations ? kTailDependent : kTailIndependent);
}

// "rank 2 < 4 rows: not onto" and friends: one clause comparing the rank with a
// dimension, then the verdict that follows from it
void caption_clause(
        const DependencyCtx* ctx, std::uint8_t against, const char* noun, bool holds, const char* yes, const char* no, Writer& w) noexcept {
		(void)w.append(detail::lbl(ctx->labels.rank_word, "rank"));
		(void)w.put(' ');
		(void)w.append_u64(static_cast<std::uint64_t>(ctx->info.rank));
		(void)w.append(holds ? " = " : " < ");
		(void)w.append_u64(static_cast<std::uint64_t>(against));
		(void)w.put(' ');
		(void)w.append(noun);
		(void)w.append(": ");
		(void)w.append(holds ? yes : no);
}

// "rank 2 < 3 vectors: linearly dependent"
void caption_verdict(const DependencyCtx* ctx, Writer& w) noexcept {
		if (ctx->purpose == DependencyPurpose::Transformation) {
				// onto is about the rows, one to one about the columns
				caption_clause(ctx,
				        ctx->info.dim,
				        detail::lbl(ctx->labels.rows_word, "rows"),
				        ctx->info.spans,
				        detail::lbl(ctx->labels.onto, "onto"),
				        detail::lbl(ctx->labels.not_onto, "not onto"),
				        w);
				(void)w.append("; ");
				caption_clause(ctx,
				        ctx->info.vector_count,
				        detail::lbl(ctx->labels.cols_word, "columns"),
				        ctx->info.independent,
				        detail::lbl(ctx->labels.one_to_one, "one-to-one"),
				        detail::lbl(ctx->labels.not_one_to_one, "not one-to-one"),
				        w);
				return;
		}

		caption_clause(ctx,
		        ctx->info.vector_count,
		        detail::lbl(ctx->labels.vectors_word, "vectors"),
		        ctx->info.independent,
		        detail::lbl(ctx->labels.independent, "linearly independent"),
		        detail::lbl(ctx->labels.dependent, "linearly dependent"),
		        w);
}

ErrorCode dependency_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const DependencyCtx*>(vctx);
		if (!ctx->input.data || !ctx->rref.data)
				return ErrorCode::Internal;
		if (!out.scratch)
				return ErrorCode::Internal;

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		if (index == 0) {
				if (out.caption && out.caption_cap) {
						Writer cw{out.caption, out.caption_cap, 0};
						(void)cw.append(detail::lbl(ctx->labels.vectors, "Columns are the vectors"));
				}
				return latex::write_matrix_display(ctx->input, latex::MatrixBrackets::BMatrix, {out.latex, out.latex_cap});
		}

		if (index <= ctx->op_count) {
				MatrixMutView work{};
				RowOp last_op{};
				ErrorCode ec = detail::replay_echelon(ctx->input, EchelonKind::Rref, index, *out.scratch, &work, &last_op);
				if (!is_ok(ec))
						return ec;

				detail::step_row_caption(last_op, index == ctx->op_count, ctx->labels.rref, out.caption, out.caption_cap);

				return latex::write_matrix_display(work.view(), latex::MatrixBrackets::BMatrix, {out.latex, out.latex_cap});
		}

		const std::size_t tail = index - ctx->op_count - 1;
		const bool relations = (ctx->purpose == DependencyPurpose::Vectors) && !ctx->info.independent;
		const std::size_t tail_count = relations ? kTailDependent : kTailIndependent;
		if (tail >= tail_count)
				return ErrorCode::StepOutOfRange;

		if (tail == 0) {
				if (out.caption && out.caption_cap) {
						Writer cw{out.caption, out.caption_cap, 0};
						caption_verdict(ctx, cw);
				}
				return latex::write_matrix_display(ctx->rref, latex::MatrixBrackets::BMatrix, {out.latex, out.latex_cap});
		}

		// relations: one line per dependent vector
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append(detail::lbl(ctx->labels.relation, "Read off the RREF columns"));
		}

		Writer w{out.latex, out.latex_cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';

		ErrorCode ec = w.append("$$\\begin{array}{l}");
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t d = 0; d < ctx->info.dependent_count; d++) {
				if (d != 0) {
						ec = w.append(" \\\\ ");
						if (!is_ok(ec))
								return ec;
				}
				ec = write_relation(ctx, d, w);
				if (!is_ok(ec))
						return ec;
		}
		return w.append("\\end{array}$$");
}

constexpr ExplanationVTable kDependencyVTable = {
        .step_count = &dependency_step_count,
        .render_step = &dependency_render_step,
        .destroy = nullptr,
};

} // namespace

Error op_dependency(MatrixView a,
        Arena& persist,
        MatrixMutView* out_rref,
        DependencyInfo* info,
        Explanation* expl,
        const ExplainOptions& opts,
        const DependencyLabels& labels,
        DependencyPurpose purpose) noexcept {
		if (!out_rref || !info)
				return {ErrorCode::Internal};
		if (!a.data)
				return {ErrorCode::Internal};
		if (a.rows == 0 || a.cols == 0)
				return err_invalid_dim(a.dim());
		if (opts.enable && opts.persist != &persist)
				return {ErrorCode::Internal};

		ArenaScope tx(persist);

		// the RREF lives in persist so the result page can read the relation
		// coefficients out of it after the operation returns
		MatrixMutView rref{};
		ErrorCode ec = matrix_clone(persist, a, &rref);
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

		DependencyInfo& result = *info;
		result = DependencyInfo{};
		result.vector_count = a.cols;
		result.dim = a.rows;
		result.rank = space.rank;
		result.pivot_mask = space.pivot_mask;
		result.independent = (space.rank == a.cols);
		result.spans = (space.rank == a.rows);
		result.dependent_count = static_cast<std::uint8_t>(a.cols - space.rank);
		for (std::uint8_t i = 0; i < space.rank; i++) {
				result.pivot_cols[i] = space.pivot_cols[i];
				result.pivot_rows[i] = space.pivot_row_for_col[space.pivot_cols[i]];
		}
		std::uint8_t dep = 0;
		for (std::uint8_t c = 0; c < a.cols; c++) {
				if (space.pivot_row_for_col[c] == kNoPivot)
						result.dependent_cols[dep++] = c;
		}

		if (opts.enable) {
				if (!expl)
						return {ErrorCode::Internal};

				void* mem = persist.allocate(sizeof(DependencyCtx), alignof(DependencyCtx));
				if (!mem)
						return {ErrorCode::Overflow};
				auto* ctx = new (mem) DependencyCtx{};
				ctx->input = a;
				ctx->rref = rref.view();
				ctx->info = result;
				ctx->labels = labels;
				ctx->purpose = purpose;
				ctx->op_count = obs.count;
				*expl = Explanation::make(ctx, &kDependencyVTable);
		}

		*out_rref = rref;

		tx.commit();
		return {};
}

} // namespace matrix_core
