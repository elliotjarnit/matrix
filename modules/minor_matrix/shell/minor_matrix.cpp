#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_minor_matrix.hpp"
#include "matrix_core/writer.hpp"

#include <graphx.h>
#include <keypadc.h>

#include <utility>

namespace matrix_shell {
namespace {
using detail::dbg_print_matrix;
using detail::dbg_print_rational;
using detail::fail_fast;
using detail::keep_cursor_in_view;
using detail::kKbGroup1;
using detail::kKbGroup6;
using detail::kKbGroupArrows;
using detail::kScreenH;
using detail::kScreenW;
using detail::op_name;
using namespace matrix_shell::text_literals;
} // namespace

void App::run_minor_matrix(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		if (a.rows != a.cols) {
				fmt_buf_[0] = '\0';
				matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
				w.append("msg.requires_square_prefix"_tx);
				w.put(static_cast<char>('A' + slot));
				w.append("=");
				w.append_u64(a.rows);
				w.put('x');
				w.append_u64(a.cols);
				show_message(fmt_buf_);
				return;
		}
		if (a.rows <= 1) {
				show_message("msg.need_n_ge_2"_tx);
				return;
		}

		matrix_core::ArenaScope tx(persist_);
		matrix_core::MatrixMutView out{};
		const matrix_core::ErrorCode ec = matrix_core::matrix_alloc(persist_, a.rows, a.cols, &out);
		if (!matrix_core::is_ok(ec)) {
				SHELL_DBG("[op] minor matrix alloc failed ec=%u used=%u/%u\n",
				        (unsigned)ec,
				        (unsigned)persist_.used(),
				        (unsigned)persist_.capacity());
				show_message("common.out_of_memory"_tx);
				return;
		}

		matrix_core::ArenaScratchScope scratch_tx(scratch_);
		const matrix_core::Error err = matrix_core::op_minor_matrix(a, scratch_, out);
		SHELL_DBG("[op] minor matrix err=%u a=%ux%u\n", (unsigned)err.code, (unsigned)err.a.rows, (unsigned)err.a.cols);
		if (!matrix_core::is_ok(err)) {
				if (err.code == matrix_core::ErrorCode::NotSquare || err.code == matrix_core::ErrorCode::InvalidDimension ||
				        err.code == matrix_core::ErrorCode::Internal) {
						fail_fast("update_slot_pick: minor matrix returned unexpected error");
				}
				show_message("common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = {};
		REQUIRE(pop(), "pop failed (minor matrix)");
		REQUIRE(push(Page::make_result_matrix(OperationId::MinorMatrix, slot, 0, false, out.rows, out.cols, out.stride, out.data)),
		        "push minor matrix result failed");
}

} // namespace matrix_shell
