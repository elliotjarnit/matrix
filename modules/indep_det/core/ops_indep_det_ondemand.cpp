#include "matrix_core/ops_indep_det.hpp"

#include "matrix_core/step_detail.hpp"
#include "matrix_core/writer.hpp"

#include <new>
#include <utility>

namespace matrix_core {
namespace {

struct IndepDetCtx {
		Explanation det_steps;   // op_det's explanation, also in persist
		IndepDetLabels labels{}; // copied by value: the pointed to strings are caller owned
		Rational det = Rational::from_int(0);
};

// op_det's steps, then the verdict
std::size_t indep_det_step_count(const void* vctx) noexcept {
		const auto* ctx = static_cast<const IndepDetCtx*>(vctx);
		return ctx->det_steps.step_count() + 1;
}

ErrorCode indep_det_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const IndepDetCtx*>(vctx);
		const std::size_t det_count = ctx->det_steps.step_count();

		if (index < det_count) {
				const ErrorCode ec = ctx->det_steps.render_step(index, out);
				if (is_ok(ec) && index == 0 && out.caption && out.caption_cap) {
						Writer cw{out.caption, out.caption_cap, 0};
						(void)cw.append(detail::lbl(ctx->labels.vectors, "Columns are the vectors"));
				}
				return ec;
		}

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';
		if (index != det_count)
				return ErrorCode::StepOutOfRange;

		const bool independent = !ctx->det.is_zero();
		if (out.caption && out.caption_cap) {
				Writer cw{out.caption, out.caption_cap, 0};
				if (independent)
						(void)cw.append(detail::lbl(ctx->labels.independent, "A is invertible, so the columns are linearly independent"));
				else
						(void)cw.append(detail::lbl(ctx->labels.dependent, "A is not invertible, so the columns are linearly dependent"));
		}

		Writer w{out.latex, out.latex_cap, 0};
		ErrorCode ec = w.append("$$\\det(A) = ");
		if (is_ok(ec))
				ec = w.append_rational_latex(ctx->det);
		if (is_ok(ec))
				ec = w.append(independent ? " \\neq 0$$" : "$$");
		return ec;
}

constexpr ExplanationVTable kIndepDetVTable = {
        .step_count = &indep_det_step_count,
        .render_step = &indep_det_render_step,
        .destroy = nullptr,
};

} // namespace

Error op_indep_det(MatrixView a,
        Arena& scratch,
        Rational* det,
        bool* independent,
        Explanation* expl,
        const ExplainOptions& opts,
        const IndepDetLabels& labels) noexcept {
		if (!det || !independent)
				return {ErrorCode::Internal};

		if (!opts.enable) {
				const Error err = op_det(a, scratch, det, nullptr, opts);
				if (is_ok(err))
						*independent = !det->is_zero();
				return err;
		}
		if (!opts.persist || !expl)
				return {ErrorCode::Internal};

		// the wrapper and op_det's own context succeed or fail together
		ArenaScope tx(*opts.persist);
		Explanation det_steps;
		const Error err = op_det(a, scratch, det, &det_steps, opts);
		if (!is_ok(err))
				return err;
		*independent = !det->is_zero();

		void* mem = opts.persist->allocate(sizeof(IndepDetCtx), alignof(IndepDetCtx));
		if (!mem)
				return err_overflow();
		auto* ctx = new (mem) IndepDetCtx{};
		ctx->det_steps = std::move(det_steps);
		ctx->labels = labels;
		ctx->det = *det;
		*expl = Explanation::make(ctx, &kIndepDetVTable);
		tx.commit();
		return {};
}

} // namespace matrix_core
