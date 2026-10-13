#include "matrix_core/ops_adjugate.hpp"

#include "matrix_core/latex.hpp"
#include "matrix_core/minor_detail.hpp"
#include "matrix_core/step_detail.hpp"
#include "matrix_core/writer.hpp"

#include <new>

namespace matrix_core {
namespace {

using Text = detail::StepText;

// terms per line of a long sum, so it stays on the screen
constexpr std::uint8_t kTermsPerLine = 3;

// after the n*n cofactor steps: the matrix of cofactors, adj(A), det(A), and
// when det(A) != 0 also A^-1 and det(A^-1)
constexpr std::size_t kTailSteps = 3;
constexpr std::size_t kInverseSteps = 2;

struct AdjugateCtx {
		MatrixView a{};          // A, owned by the caller and outlives this context
		MatrixView adj{};        // adj(A), owned by the caller; C_ij is adj(j, i)
		AdjugateLabels labels{}; // copied by value: the pointed to strings are caller owned
		Rational det = Rational::from_int(0);
};

std::size_t adjugate_step_count(const void* vctx) noexcept {
		const auto* ctx = static_cast<const AdjugateCtx*>(vctx);
		const std::size_t n = ctx->a.rows;
		return 1u + n * n + kTailSteps + (ctx->det.is_zero() ? 0u : kInverseSteps);
}

void caption(const StepRenderBuffers& out, const char* text) noexcept {
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append(text);
		}
}

// "C = [C_ij]" from adj(A), transposing it back
ErrorCode render_cofactor_matrix(const AdjugateCtx& ctx, const StepRenderBuffers& out) noexcept {
		MatrixMutView c;
		const ErrorCode ec = matrix_alloc(*out.scratch, ctx.a.rows, ctx.a.cols, &c);
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t i = 0; i < c.rows; i++) {
				for (std::uint8_t j = 0; j < c.cols; j++)
						c.at_mut(i, j) = ctx.adj.at(j, i);
		}
		Text t{{out.latex, out.latex_cap, 0}};
		return t.str("$$C = ").matrix(c.view(), latex::MatrixBrackets::BMatrix).str("$$").ec;
}

// "det(A) = a_11 C_11 + ... = d", then "A^-1 = (1/d) adj(A)" when d != 0
ErrorCode render_det(const AdjugateCtx& ctx, const StepRenderBuffers& out) noexcept {
		Text t{{out.latex, out.latex_cap, 0}};
		t.open().str("\\det(A) = ");
		for (std::uint8_t j = 0; j < ctx.a.cols; j++) {
				if (j != 0)
						t.str((j % kTermsPerLine == 0) ? " \\\\ + " : " + ");
				const Rational& entry = ctx.a.at(0, j);
				if (entry.is_zero())
						t.ch('0');
				else
						t.factor(entry).str("\\cdot ").factor(ctx.adj.at(j, 0));
		}
		t.br().str("= ").value(ctx.det);
		if (!ctx.det.is_zero())
				t.br().str("A^{-1} = \\frac{1}{").value(ctx.det).str("}\\text{adj}(A)");
		return t.close().ec;
}

// "A^-1 = [adj(A)_ij / det(A)]", built in scratch
ErrorCode render_inverse(const AdjugateCtx& ctx, const StepRenderBuffers& out) noexcept {
		MatrixMutView inv;
		ErrorCode ec = matrix_alloc(*out.scratch, ctx.adj.rows, ctx.adj.cols, &inv);
		for (std::uint8_t i = 0; is_ok(ec) && i < inv.rows; i++) {
				for (std::uint8_t j = 0; is_ok(ec) && j < inv.cols; j++)
						ec = rational_div(ctx.adj.at(i, j), ctx.det, &inv.at_mut(i, j));
		}
		if (!is_ok(ec))
				return ec;
		Text t{{out.latex, out.latex_cap, 0}};
		return t.str("$$A^{-1} = ").matrix(inv.view(), latex::MatrixBrackets::BMatrix).str("$$").ec;
}

// "det(A^-1) = 1/det(A) = 1/d = 1/d reduced"
ErrorCode render_det_inverse(const AdjugateCtx& ctx, const StepRenderBuffers& out) noexcept {
		Rational recip;
		const ErrorCode ec = rational_div(Rational::from_int(1), ctx.det, &recip);
		if (!is_ok(ec))
				return ec;
		Text t{{out.latex, out.latex_cap, 0}};
		t.str("$$\\det(A^{-1}) = \\frac{1}{\\det(A)} = \\frac{1}{").value(ctx.det).str("} = ").value(recip).str("$$");
		return t.ec;
}

ErrorCode adjugate_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const AdjugateCtx*>(vctx);
		if (!ctx->a.data || !ctx->adj.data || !out.scratch)
				return ErrorCode::Internal;

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		const std::uint8_t n = ctx->a.rows;
		const std::size_t cells = static_cast<std::size_t>(n) * n;
		if (index >= adjugate_step_count(ctx))
				return ErrorCode::StepOutOfRange;

		if (index == 0) {
				caption(out, detail::lbl(ctx->labels.intro, "adj(A) is the transpose of the matrix of cofactors"));
				Text t{{out.latex, out.latex_cap, 0}};
				return t.str("$$A = ").matrix(ctx->a, latex::MatrixBrackets::BMatrix).str("$$").ec;
		}

		if (index <= cells) {
				const auto i = static_cast<std::uint8_t>((index - 1) / n);
				const auto j = static_cast<std::uint8_t>((index - 1) % n);
				const Rational& cof = ctx->adj.at(j, i);
				Rational minor = cof;
				if (((i + j) & 1u) != 0u) {
						const ErrorCode ec = rational_neg(cof, &minor);
						if (!is_ok(ec))
								return ec;
				}

				if (n > 1 && out.caption && out.caption_cap) {
						Writer cw{out.caption, out.caption_cap, 0};
						(void)cw.append(detail::lbl(ctx->labels.delete_row, "Delete row"));
						(void)cw.put(' ');
						(void)cw.append_index1(i);
						(void)cw.append(", ");
						(void)cw.append(detail::lbl(ctx->labels.col_word, "column"));
						(void)cw.put(' ');
						(void)cw.append_index1(j);
				}
				Text t{{out.latex, out.latex_cap, 0}};
				t.open();
				detail::write_cofactor_lines(t, ctx->a, i, j, minor, cof, *out.scratch);
				return t.close().ec;
		}

		const std::size_t tail = index - cells - 1;
		if (tail == 0) {
				caption(out, detail::lbl(ctx->labels.cofactors, "Matrix of cofactors"));
				return render_cofactor_matrix(*ctx, out);
		}
		if (tail == 1) {
				caption(out, detail::lbl(ctx->labels.transpose, "Transpose it"));
				Text t{{out.latex, out.latex_cap, 0}};
				return t.str("$$\\text{adj}(A) = C^{T} = ").matrix(ctx->adj, latex::MatrixBrackets::BMatrix).str("$$").ec;
		}
		if (tail == 2) {
				caption(out, detail::lbl(ctx->labels.det, "det(A) across row 1, and the inverse"));
				return render_det(*ctx, out);
		}
		if (tail == 3) {
				caption(out, detail::lbl(ctx->labels.inverse, "Divide every entry of adj(A) by det(A)"));
				return render_inverse(*ctx, out);
		}
		caption(out, detail::lbl(ctx->labels.det_inverse, "det of the inverse is 1 over det(A)"));
		return render_det_inverse(*ctx, out);
}

constexpr ExplanationVTable kAdjugateVTable = {
        .step_count = &adjugate_step_count,
        .render_step = &adjugate_render_step,
        .destroy = nullptr,
};

} // namespace

Error op_adjugate(MatrixView a,
        Arena& scratch,
        MatrixMutView out,
        Explanation* expl,
        const ExplainOptions& opts,
        const AdjugateLabels& labels,
        Rational* det_out) noexcept {
		Error err;
		if (a.rows != a.cols)
				return err_not_square(a.dim());
		if (a.rows == 0 || a.rows > kMaxRows)
				return err_invalid_dim(a.dim());
		if (!out.data || out.rows != a.rows || out.cols != a.cols)
				return err_dim_mismatch(a.dim(), out.dim());

		ArenaScratchScope scratch_scope(scratch);

		// adj(A)_ji = C_ij; det(A) comes along from row 1
		const std::uint8_t n = a.rows;
		Rational det = Rational::from_int(0);
		for (std::uint8_t i = 0; i < n; i++) {
				for (std::uint8_t j = 0; j < n; j++) {
						Rational minor;
						Rational cof;
						ErrorCode ec = detail::cofactor_of(a, i, j, scratch, &minor, &cof);
						if (is_ok(ec) && i == 0) {
								Rational term;
								ec = rational_mul(a.at(0, j), cof, &term);
								if (is_ok(ec))
										ec = rational_add(det, term, &det);
						}
						if (!is_ok(ec)) {
								err.code = ec;
								err.a = a.dim();
								return err;
						}
						out.at_mut(j, i) = cof;
				}
		}

		if (det_out)
				*det_out = det;

		if (opts.enable) {
				if (!opts.persist || !expl)
						return {ErrorCode::Internal};
				ArenaScope tx(*opts.persist);
				void* mem = opts.persist->allocate(sizeof(AdjugateCtx), alignof(AdjugateCtx));
				if (!mem)
						return err_overflow();
				auto* ctx = new (mem) AdjugateCtx{};
				ctx->a = a;
				ctx->adj = out.view();
				ctx->labels = labels;
				ctx->det = det;
				*expl = Explanation::make(ctx, &kAdjugateVTable);
				tx.commit();
		}
		return err;
}

} // namespace matrix_core
