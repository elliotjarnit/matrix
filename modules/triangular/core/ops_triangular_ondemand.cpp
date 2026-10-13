#include "matrix_core/ops_triangular.hpp"

#include "matrix_core/latex.hpp"
#include "matrix_core/step_detail.hpp"
#include "matrix_core/writer.hpp"

#include <new>

namespace matrix_core {
namespace {

struct TriangularCtx {
		MatrixView a{};            // A, owned by the caller and outlives this context
		TriangularLabels labels{}; // copied by value: the pointed to strings are caller owned
		TriangularKind kind = TriangularKind::None;
		Rational det = Rational::from_int(0);
};

// 0: A and what kind of triangular it is, 1: the diagonal product
constexpr std::size_t kTriangularSteps = 2;

std::size_t triangular_step_count(const void*) noexcept {
		return kTriangularSteps;
}

const char* kind_caption(const TriangularCtx& ctx) noexcept {
		switch (ctx.kind) {
		case TriangularKind::Upper:
				return detail::lbl(ctx.labels.upper, "A is upper triangular");
		case TriangularKind::Lower:
				return detail::lbl(ctx.labels.lower, "A is lower triangular");
		case TriangularKind::Diagonal:
				return detail::lbl(ctx.labels.diagonal, "A is diagonal");
		case TriangularKind::None:
				break;
		}
		return "";
}

// "$$\det(A) = a_{11}a_{22}a_{33}$$\n$$= (2)(7)(3) = 42$$"
ErrorCode write_product(const TriangularCtx& ctx, Writer& w) noexcept {
		ErrorCode ec = w.append("$$\\det(A) = ");
		for (std::uint8_t i = 0; i < ctx.a.rows && is_ok(ec); i++) {
				ec = w.append("a_{");
				if (is_ok(ec))
						ec = w.append_index1(i);
				if (is_ok(ec))
						ec = w.append_index1(i);
				if (is_ok(ec))
						ec = w.put('}');
		}
		if (is_ok(ec))
				ec = w.append("$$\n$$= ");
		for (std::uint8_t i = 0; i < ctx.a.rows && is_ok(ec); i++) {
				ec = w.put('(');
				if (is_ok(ec))
						ec = w.append_rational_latex(ctx.a.at(i, i));
				if (is_ok(ec))
						ec = w.put(')');
		}
		if (is_ok(ec))
				ec = w.append(" = ");
		if (is_ok(ec))
				ec = w.append_rational_latex(ctx.det);
		if (is_ok(ec))
				ec = w.append("$$");
		return ec;
}

ErrorCode triangular_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const TriangularCtx*>(vctx);
		if (!ctx->a.data)
				return ErrorCode::Internal;

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		if (index >= kTriangularSteps)
				return ErrorCode::StepOutOfRange;

		if (index == 0) {
				if (out.caption && out.caption_cap) {
						Writer cw{out.caption, out.caption_cap, 0};
						(void)cw.append(kind_caption(*ctx));
				}
				return latex::write_matrix_display(ctx->a, latex::MatrixBrackets::VMatrix, {out.latex, out.latex_cap});
		}

		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				(void)cw.append(detail::lbl(ctx->labels.product, "det(A) is the product of the diagonal entries"));
		}
		Writer w{out.latex, out.latex_cap, 0};
		return write_product(*ctx, w);
}

constexpr ExplanationVTable kTriangularVTable = {
        .step_count = &triangular_step_count,
        .render_step = &triangular_render_step,
        .destroy = nullptr,
};

} // namespace

TriangularKind triangular_kind(MatrixView a) noexcept {
		if (a.rows != a.cols || !a.data)
				return TriangularKind::None;
		bool upper = true; // nothing below the diagonal
		bool lower = true; // nothing above it
		for (std::uint8_t r = 0; r < a.rows; r++) {
				for (std::uint8_t c = 0; c < a.cols; c++) {
						if (a.at(r, c).is_zero())
								continue;
						if (r > c)
								upper = false;
						if (r < c)
								lower = false;
				}
		}
		if (upper && lower)
				return TriangularKind::Diagonal;
		if (upper)
				return TriangularKind::Upper;
		if (lower)
				return TriangularKind::Lower;
		return TriangularKind::None;
}

Error op_det_triangular(
        MatrixView a, Rational* out, Explanation* expl, const ExplainOptions& opts, const TriangularLabels& labels) noexcept {
		if (!out)
				return {ErrorCode::Internal};
		if (a.rows != a.cols)
				return err_not_square(a.dim());
		const TriangularKind kind = triangular_kind(a);
		if (kind == TriangularKind::None)
				return err_invalid_dim(a.dim());

		Rational det = Rational::from_int(1);
		for (std::uint8_t i = 0; i < a.rows; i++) {
				const ErrorCode ec = rational_mul(det, a.at(i, i), &det);
				if (!is_ok(ec)) {
						Error err;
						err.code = ec;
						err.a = a.dim();
						return err;
				}
		}
		*out = det;

		if (opts.enable) {
				if (!opts.persist || !expl)
						return {ErrorCode::Internal};
				ArenaScope tx(*opts.persist);
				void* mem = opts.persist->allocate(sizeof(TriangularCtx), alignof(TriangularCtx));
				if (!mem)
						return err_overflow();
				auto* ctx = new (mem) TriangularCtx{};
				ctx->a = a;
				ctx->labels = labels;
				ctx->kind = kind;
				ctx->det = det;
				*expl = Explanation::make(ctx, &kTriangularVTable);
				tx.commit();
		}
		return {};
}

} // namespace matrix_core
