#include "matrix_core/ops.hpp"

#include "matrix_core/latex.hpp"
#include "matrix_core/step_detail.hpp"
#include "matrix_core/writer.hpp"

#include <new>

namespace matrix_core {
namespace {

struct ColRowCtx {
		MatrixView a{}; // both are caller owned and outlive this context
		MatrixView b{};
		ColRowLabels labels{}; // copied by value: the pointed to strings are caller owned
};

// column k of A, as an m x 1 view over the same storage
MatrixView column_of(MatrixView a, std::uint8_t k) noexcept {
		return MatrixView{a.rows, 1, a.stride, a.data + k};
}

// row k of B, as a 1 x n view over the same storage
MatrixView row_of(MatrixView b, std::uint8_t k) noexcept {
		return MatrixView{1, b.cols, b.stride, b.data + static_cast<std::size_t>(k) * b.stride};
}

// the rank one term col_k(A) row_k(B), allocated from `scratch`
ErrorCode outer_product(MatrixView a, MatrixView b, std::uint8_t k, Arena& scratch, MatrixMutView* out) noexcept {
		MatrixMutView t{};
		ErrorCode ec = matrix_alloc(scratch, a.rows, b.cols, &t);
		if (!is_ok(ec))
				return ec;

		for (std::uint8_t i = 0; i < a.rows; i++) {
				for (std::uint8_t j = 0; j < b.cols; j++) {
						Rational v{};
						ec = rational_mul(a.at(i, k), b.at(k, j), &v);
						if (!is_ok(ec))
								return ec;
						t.at_mut(i, j) = v;
				}
		}

		*out = t;
		return ErrorCode::Ok;
}

std::size_t colrow_step_count(const void* vctx) noexcept {
		const auto* ctx = static_cast<const ColRowCtx*>(vctx);
		// A and B, then one term per k, then the sum
		return static_cast<std::size_t>(ctx->a.cols) + 2u;
}

// "$$A = [..],\; B = [..]$$"
ErrorCode render_intro(const ColRowCtx* ctx, Writer& w) noexcept {
		ErrorCode ec = w.append("$$A = ");
		if (!is_ok(ec))
				return ec;
		ec = latex::append_matrix(ctx->a, latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append(",\\; B = ");
		if (!is_ok(ec))
				return ec;
		ec = latex::append_matrix(ctx->b, latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

// "$$[col][row] = [term]$$"
ErrorCode render_term(const ColRowCtx* ctx, std::uint8_t k, Arena& scratch, Writer& w) noexcept {
		MatrixMutView term{};
		ErrorCode ec = outer_product(ctx->a, ctx->b, k, scratch, &term);
		if (!is_ok(ec))
				return ec;

		ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		ec = latex::append_matrix(column_of(ctx->a, k), latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		ec = latex::append_matrix(row_of(ctx->b, k), latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = ");
		if (!is_ok(ec))
				return ec;
		ec = latex::append_matrix(term.view(), latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

// "$$AB = [T_1] + [T_2] + [T_3] = [result]$$", falling back to just the result
// when the full sum does not fit the caller's buffer
ErrorCode render_sum(const ColRowCtx* ctx, Arena& scratch, const StepRenderBuffers& out) noexcept {
		MatrixMutView product{};
		ErrorCode ec = matrix_alloc(scratch, ctx->a.rows, ctx->b.cols, &product);
		if (!is_ok(ec))
				return ec;
		ec = matrix_mul(ctx->a, ctx->b, product);
		if (!is_ok(ec))
				return ec;

		const std::size_t mark = scratch.mark();

		Writer w{out.latex, out.latex_cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';

		ErrorCode sum_ec = w.append("$$AB = ");
		for (std::uint8_t k = 0; is_ok(sum_ec) && k < ctx->a.cols; k++) {
				if (k != 0)
						sum_ec = w.append(" + ");
				if (!is_ok(sum_ec))
						break;
				MatrixMutView term{};
				const ErrorCode term_ec = outer_product(ctx->a, ctx->b, k, scratch, &term);
				if (!is_ok(term_ec))
						return term_ec;
				sum_ec = latex::append_matrix(term.view(), latex::MatrixBrackets::BMatrix, w);
		}
		if (is_ok(sum_ec))
				sum_ec = w.append(" = ");
		if (is_ok(sum_ec))
				sum_ec = latex::append_matrix(product.view(), latex::MatrixBrackets::BMatrix, w);
		if (is_ok(sum_ec))
				sum_ec = w.append("$$");

		if (is_ok(sum_ec))
				return ErrorCode::Ok;
		if (sum_ec != ErrorCode::BufferTooSmall)
				return sum_ec;

		// the expanded sum grows with p; showing the product alone still answers
		// the question and the per term steps carry the working
		scratch.rewind(mark);
		Writer f{out.latex, out.latex_cap, 0};
		if (f.data && f.cap)
				f.data[0] = '\0';
		ErrorCode ec2 = f.append("$$AB = ");
		if (!is_ok(ec2))
				return ec2;
		ec2 = latex::append_matrix(product.view(), latex::MatrixBrackets::BMatrix, f);
		if (!is_ok(ec2))
				return ec2;
		return f.append("$$");
}

ErrorCode colrow_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const ColRowCtx*>(vctx);
		if (!ctx->a.data || !ctx->b.data)
				return ErrorCode::Internal;
		if (!out.scratch)
				return ErrorCode::Internal;

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		const std::uint8_t p = ctx->a.cols;

		if (index == 0) {
				if (out.caption && out.caption_cap) {
						Writer cw{out.caption, out.caption_cap, 0};
						(void)cw.append(detail::lbl(ctx->labels.intro, "AB is the sum of the outer products"));
				}
				Writer w{out.latex, out.latex_cap, 0};
				if (w.data && w.cap)
						w.data[0] = '\0';
				return render_intro(ctx, w);
		}

		if (index <= p) {
				const std::uint8_t k = static_cast<std::uint8_t>(index - 1u);
				if (out.caption && out.caption_cap) {
						Writer cw{out.caption, out.caption_cap, 0};
						(void)cw.append(detail::lbl(ctx->labels.term, "term"));
						(void)cw.put(' ');
						(void)cw.append_index1(k);
						(void)cw.put('/');
						(void)cw.append_u64(static_cast<std::uint64_t>(p));
				}
				Writer w{out.latex, out.latex_cap, 0};
				if (w.data && w.cap)
						w.data[0] = '\0';
				return render_term(ctx, k, *out.scratch, w);
		}

		if (index != static_cast<std::size_t>(p) + 1u)
				return ErrorCode::StepOutOfRange;

		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append(detail::lbl(ctx->labels.sum, "sum of the outer products"));
		}
		return render_sum(ctx, *out.scratch, out);
}

constexpr ExplanationVTable kColRowVTable = {
        .step_count = &colrow_step_count,
        .render_step = &colrow_render_step,
        .destroy = nullptr,
};

} // namespace

Error op_mul_colrow(
        MatrixView a, MatrixView b, MatrixMutView out, Explanation* expl, const ExplainOptions& opts, const ColRowLabels& labels) noexcept {
		if (!a.data || !b.data)
				return {ErrorCode::Internal};
		if (a.cols != b.rows)
				return err_dim_mismatch(a.dim(), b.dim());
		if (out.rows != a.rows || out.cols != b.cols)
				return {ErrorCode::DimensionMismatch, a.dim(), out.dim()};

		ErrorCode ec = matrix_mul(a, b, out);
		if (!is_ok(ec))
				return {ec};

		if (opts.enable) {
				if (!opts.persist || !expl)
						return {ErrorCode::Internal};

				ArenaScope tx(*opts.persist);
				void* mem = opts.persist->allocate(sizeof(ColRowCtx), alignof(ColRowCtx));
				if (!mem)
						return {ErrorCode::Overflow};
				auto* ctx = new (mem) ColRowCtx{};
				ctx->a = a;
				ctx->b = b;
				ctx->labels = labels;
				*expl = Explanation::make(ctx, &kColRowVTable);
				tx.commit();
		}

		return {};
}

} // namespace matrix_core
