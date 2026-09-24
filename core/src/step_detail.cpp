#include "matrix_core/step_detail.hpp"

#include "matrix_core/ops.hpp"
#include "matrix_core/row_reduction.hpp"

namespace matrix_core::detail {

Writer writer_at_end(char* buf, std::size_t cap) noexcept {
		Writer w{buf, cap, 0};
		if (!buf || cap == 0)
				return w;
		while (w.len + 1 < cap && buf[w.len] != '\0')
				w.len++;
		return w;
}

ErrorCode replay_echelon(
        MatrixView input, EchelonKind kind, std::size_t index, Arena& scratch, MatrixMutView* out, RowOp* last_op) noexcept {
		if (!out)
				return ErrorCode::Internal;

		MatrixMutView work{};
		ErrorCode ec = matrix_clone(scratch, input, &work);
		if (!is_ok(ec))
				return ec;

		if (index != 0) {
				OpObserver obs;
				obs.target = index; // stop after applying `index` ops
				ec = echelon_apply(work, kind, &obs);
				if (!is_ok(ec))
						return ec;
				if (obs.count < index)
						return ErrorCode::StepOutOfRange;
				if (last_op)
						*last_op = obs.last_op;
		}

		*out = work;
		return ErrorCode::Ok;
}

void step_row_caption(const RowOp& op, bool is_last, const char* rref_label, char* buf, std::size_t cap) noexcept {
		if (!buf || cap == 0)
				return;

		// a truncated caption must not cost the student the matrix itself
		(void)row_op_caption(op, buf, cap);
		if (!is_last)
				return;

		Writer w = writer_at_end(buf, cap);
		(void)w.append("  ");
		(void)w.append(lbl(rref_label, "RREF"));
}

} // namespace matrix_core::detail
