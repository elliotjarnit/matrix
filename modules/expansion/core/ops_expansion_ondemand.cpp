#include "matrix_core/ops_expansion.hpp"

#include "matrix_core/latex.hpp"
#include "matrix_core/minor_detail.hpp"
#include "matrix_core/step_detail.hpp"
#include "matrix_core/writer.hpp"

#include <new>

namespace matrix_core {
namespace {

// terms per line of a long sum, so it stays on the screen
constexpr std::uint8_t kTermsPerLine = 3;

using Text = detail::StepText;

// what goes before term k of a long sum: nothing, " + ", or a new line
Text& plus(Text& t, std::uint8_t k) noexcept {
		if (k == 0)
				return t;
		if (k % kTermsPerLine == 0)
				return t.br().str("+ ");
		return t.str(" + ");
}

struct ExpansionCtx {
		MatrixView a{};           // A, owned by the caller and outlives this context
		ExpansionLabels labels{}; // copied by value: the pointed to strings are caller owned
		ExpansionLine line = ExpansionLine::Row;
		std::uint8_t index = 0; // the row or column expanded along, 0 based
		Rational det = Rational::from_int(0);
		Rational minor[kMaxRows]{}; // det(M) of each term along the line
		Rational cof[kMaxRows]{};   // the matching cofactors
};

// A position of the k-th term along the expansion line.
void term_pos(ExpansionLine line, std::uint8_t index, std::uint8_t k, std::uint8_t* r, std::uint8_t* c) noexcept {
		if (line == ExpansionLine::Row) {
				*r = index;
				*c = k;
		} else {
				*r = k;
				*c = index;
		}
}

std::size_t expansion_step_count(const void* vctx) noexcept {
		const auto* ctx = static_cast<const ExpansionCtx*>(vctx);
		const std::uint8_t n = ctx->a.rows;
		// the setup, one step per term, the sum; a 1x1 matrix is its own determinant
		return (n == 1) ? 1u : static_cast<std::size_t>(n) + 2u;
}

ErrorCode render_setup(const ExpansionCtx& ctx, const StepRenderBuffers& out) noexcept {
		const std::uint8_t n = ctx.a.rows;
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				if (ctx.line == ExpansionLine::Row)
						(void)cw.append(detail::lbl(ctx.labels.across_row, "Expand across row"));
				else
						(void)cw.append(detail::lbl(ctx.labels.down_col, "Expand down column"));
				(void)cw.put(' ');
				(void)cw.append_index1(ctx.index);
		}

		Text t{{out.latex, out.latex_cap, 0}};
		t.open().str("\\det(A) = ").det_bars(ctx.a);
		if (n == 1)
				return t.br().str("= ").value(ctx.det).close().ec;

		t.br().str("= ");
		for (std::uint8_t k = 0; k < n; k++) {
				plus(t, k);
				std::uint8_t r = 0;
				std::uint8_t c = 0;
				term_pos(ctx.line, ctx.index, k, &r, &c);
				t.str("a").sub(r, c).str("C").sub(r, c);
		}
		return t.close().ec;
}

ErrorCode render_term(const ExpansionCtx& ctx, std::uint8_t k, const StepRenderBuffers& out) noexcept {
		std::uint8_t r = 0;
		std::uint8_t c = 0;
		term_pos(ctx.line, ctx.index, k, &r, &c);
		const Rational& entry = ctx.a.at(r, c);
		Text t{{out.latex, out.latex_cap, 0}};

		if (entry.is_zero()) {
				if (out.caption && out.caption_cap) {
						Text ct{{out.caption, out.caption_cap, 0}};
						ct.str("$a").sub(r, c).str(" = 0$, ").str(detail::lbl(ctx.labels.zero_term, "so the term is 0"));
				}
				return t.str("$$a").sub(r, c).str("C").sub(r, c).str(" = 0$$").ec;
		}

		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append(detail::lbl(ctx.labels.delete_row, "Delete row"));
				(void)cw.put(' ');
				(void)cw.append_index1(r);
				(void)cw.append(", ");
				(void)cw.append(detail::lbl(ctx.labels.col_word, "column"));
				(void)cw.put(' ');
				(void)cw.append_index1(c);
		}

		// det M_rc worked out, then C_rc = (-1)^(r+c) det M_rc
		t.open();
		detail::write_cofactor_lines(t, ctx.a, r, c, ctx.minor[k], ctx.cof[k], *out.scratch);

		// the term itself
		Rational term;
		const ErrorCode ec = rational_mul(entry, ctx.cof[k], &term);
		if (!is_ok(ec))
				return ec;
		t.br().str("a").sub(r, c).str("C").sub(r, c).str(" = ").factor(entry).str("\\cdot ").factor(ctx.cof[k]).str(" = ");
		return t.value(term).close().ec;
}

ErrorCode render_sum(const ExpansionCtx& ctx, const StepRenderBuffers& out) noexcept {
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append(detail::lbl(ctx.labels.sum, "Add up the terms"));
		}

		Text t{{out.latex, out.latex_cap, 0}};
		t.open().str("\\det(A) = ");
		for (std::uint8_t k = 0; k < ctx.a.rows; k++) {
				plus(t, k);
				std::uint8_t r = 0;
				std::uint8_t c = 0;
				term_pos(ctx.line, ctx.index, k, &r, &c);
				const Rational& entry = ctx.a.at(r, c);
				if (entry.is_zero())
						t.ch('0');
				else
						t.factor(entry).str("\\cdot ").factor(ctx.cof[k]);
		}
		return t.br().str("= ").value(ctx.det).close().ec;
}

ErrorCode expansion_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const ExpansionCtx*>(vctx);
		if (!ctx->a.data || !out.scratch)
				return ErrorCode::Internal;

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		const std::size_t total = expansion_step_count(ctx);
		if (index >= total)
				return ErrorCode::StepOutOfRange;

		if (index == 0)
				return render_setup(*ctx, out);
		if (index == total - 1)
				return render_sum(*ctx, out);
		return render_term(*ctx, static_cast<std::uint8_t>(index - 1), out);
}

constexpr ExplanationVTable kExpansionVTable = {
        .step_count = &expansion_step_count,
        .render_step = &expansion_render_step,
        .destroy = nullptr,
};

} // namespace

Error op_cofactor_expansion(MatrixView a,
        ExpansionLine line,
        std::uint8_t index,
        Arena& scratch,
        Rational* out,
        Explanation* expl,
        const ExplainOptions& opts,
        const ExpansionLabels& labels) noexcept {
		Error err;
		if (!out)
				return {ErrorCode::Internal};
		if (a.rows != a.cols)
				return err_not_square(a.dim());
		if (a.rows == 0 || a.rows > kMaxRows)
				return err_invalid_dim(a.dim());
		if (index >= a.rows) {
				err.code = ErrorCode::IndexOutOfRange;
				err.a = a.dim();
				err.i = index;
				return err;
		}

		ArenaScratchScope scratch_scope(scratch);

		// det(M) and C of each term; kept in scratch, the CE stack is small
		const std::uint8_t n = a.rows;
		auto* minors = static_cast<Rational*>(scratch.allocate(2u * sizeof(Rational) * n, alignof(Rational)));
		if (!minors)
				return err_overflow();
		Rational* cofs = minors + n;
		Rational det = Rational::from_int(0);

		for (std::uint8_t k = 0; k < n; k++) {
				std::uint8_t r = 0;
				std::uint8_t c = 0;
				term_pos(line, index, k, &r, &c);

				// a 1x1 matrix: det(A) = a_11, with C_11 = 1 by convention
				Rational minor;
				Rational cof;
				ErrorCode ec = detail::cofactor_of(a, r, c, scratch, &minor, &cof);
				Rational term;
				if (is_ok(ec))
						ec = rational_mul(a.at(r, c), cof, &term);
				if (is_ok(ec))
						ec = rational_add(det, term, &det);
				if (!is_ok(ec)) {
						err.code = ec;
						err.a = a.dim();
						return err;
				}
				minors[k] = minor;
				cofs[k] = cof;
		}

		*out = det;

		if (opts.enable) {
				if (!opts.persist || !expl)
						return {ErrorCode::Internal};
				ArenaScope tx(*opts.persist);
				void* mem = opts.persist->allocate(sizeof(ExpansionCtx), alignof(ExpansionCtx));
				if (!mem)
						return err_overflow();
				auto* ctx = new (mem) ExpansionCtx{};
				ctx->a = a;
				ctx->labels = labels;
				ctx->line = line;
				ctx->index = index;
				ctx->det = det;
				for (std::uint8_t k = 0; k < n; k++) {
						ctx->minor[k] = minors[k];
						ctx->cof[k] = cofs[k];
				}
				*expl = Explanation::make(ctx, &kExpansionVTable);
				tx.commit();
		}

		return err;
}

} // namespace matrix_core
