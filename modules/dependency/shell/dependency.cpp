#include "matrix_shell/app.hpp"

#include "matrix_shell/text.hpp"
#include "matrix_shell/ui.hpp"

#include "matrix_shell/detail/app_internal.hpp"

#include "matrix_core/ops_dependency.hpp"
#include "matrix_core/writer.hpp"
#include "matrix_core/spaces.hpp"

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
using detail::render_pivot_cols;

// "v3 = 3v1 + v2": the dependent vector `dep_col` written over the pivot
// vectors, with the coefficients read out of the RREF. Overflow truncates: the
// steps page carries the full relation.
void render_relation(const matrix_core::Rational* rref,
        std::uint8_t stride,
        std::uint8_t rows,
        std::uint32_t pivot_mask,
        std::uint8_t dep_col,
        char* buf,
        std::size_t cap) noexcept {
		if (!buf || cap == 0 || !rref)
				return;
		buf[0] = '\0';
		matrix_core::Writer w{buf, cap, 0};
		(void)w.put('v');
		(void)w.append_index1(dep_col);
		(void)w.append(" = ");

		bool wrote = false;
		std::uint8_t row = 0;
		for (std::uint8_t pc = 0; pc < 7 && row < rows; pc++) {
				if ((pivot_mask & (1u << pc)) == 0)
						continue;

				// in an RREF the k-th pivot column carries its 1 in the k-th row
				const matrix_core::Rational& c = rref[static_cast<std::size_t>(row) * stride + dep_col];
				row++;
				if (c.is_zero())
						continue;

				const bool neg = c.num() < 0;
				if (neg)
						(void)w.append(wrote ? " - " : "-");
				else if (wrote)
						(void)w.append(" + ");

				// magnitude without negating, which would be UB at INT64_MIN
				const std::uint64_t mag = neg ? (static_cast<std::uint64_t>(-(c.num() + 1)) + 1u) : static_cast<std::uint64_t>(c.num());
				if (!(mag == 1u && c.den() == 1)) {
						(void)w.append_u64(mag);
						if (c.den() != 1) {
								(void)w.put('/');
								(void)w.append_i64(c.den());
						}
				}
				(void)w.put('v');
				(void)w.append_index1(pc);
				wrote = true;
		}

		if (!wrote)
				(void)w.put('0');
}

matrix_core::DependencyLabels dependency_labels() noexcept {
		matrix_core::DependencyLabels labels{};
		labels.vectors = "dep.vectors"_tx;
		labels.rref = "dep.rref"_tx;
		labels.independent = "dep.independent"_tx;
		labels.dependent = "dep.dependent"_tx;
		labels.vectors_word = "dep.vectors_word"_tx;
		labels.rank_word = "dep.rank_word"_tx;
		labels.relation = "dep.relation"_tx;
		labels.onto = "dep.onto"_tx;
		labels.not_onto = "dep.not_onto"_tx;
		labels.one_to_one = "dep.one_to_one"_tx;
		labels.not_one_to_one = "dep.not_one_to_one"_tx;
		labels.rows_word = "dep.rows_word"_tx;
		labels.cols_word = "dep.cols_word"_tx;
		return labels;
}
} // namespace

void App::run_span_test(OperationId op, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;
		matrix_core::Explanation expl;

		matrix_core::MatrixMutView rref{};
		const matrix_core::ErrorCode ec_r = matrix_core::matrix_alloc(scratch_, a.rows, a.cols, &rref);
		if (!matrix_core::is_ok(ec_r)) {
				show_message("common.out_of_memory"_tx);
				return;
		}

		const matrix_core::Error err = matrix_core::op_echelon(a, matrix_core::EchelonKind::Rref, rref, &expl, opts_steps);
		if (!matrix_core::is_ok(err)) {
				show_message("common.error"_tx);
				return;
		}

		matrix_core::SpaceInfo info{};
		const matrix_core::ErrorCode ec_p = matrix_core::space_info_from_rref(rref.view(), a.cols, &info);
		if (!matrix_core::is_ok(ec_p)) {
				show_message("common.error"_tx);
				return;
		}
		const std::uint32_t piv_mask = info.pivot_mask;

		const bool ok = (info.rank == a.rows);

		tx.commit();
		expl_ = std::move(expl);
		REQUIRE(pop(), "pop failed (span)");

		Page p = Page::make_result_scalar(op, slot, 0, expl_.available(), ok ? 1 : 0, 1);
		p.u.result.rows = a.rows;
		p.u.result.cols = a.cols;
		p.u.result.i = info.rank; // rank
		p.u.result.j = 0;
		// For the span test, `num` is the boolean (YES/NO) and `den` stores the pivot mask.
		p.u.result.num = ok ? 1 : 0;
		p.u.result.den = static_cast<std::int64_t>(piv_mask);
		REQUIRE(push(p), "push span result failed");
}

void App::run_indep_test(OperationId op, std::uint8_t slot, std::uint8_t) noexcept {
		const matrix_core::MatrixView a = slots_[slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;
		matrix_core::Explanation expl;

		matrix_core::MatrixMutView rref{};
		matrix_core::DependencyInfo info{};
		const matrix_core::Error err = matrix_core::op_dependency(a, persist_, &rref, &info, &expl, opts_steps, dependency_labels());
		if (!matrix_core::is_ok(err)) {
				show_message(err.code == matrix_core::ErrorCode::Overflow ? "common.out_of_memory"_tx : "common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (indep)");

		Page p = Page::make_result_scalar(op, slot, 0, expl_.available(), info.independent ? 1 : 0, 1);
		p.u.result.rows = a.rows;
		p.u.result.cols = a.cols;
		p.u.result.i = info.rank;
		// `j` is the first dependent column, 0xFF when the vectors are independent
		p.u.result.j = info.independent ? 0xFFu : info.dependent_cols[0];
		// `num` is the boolean (YES/NO) and `den` stores the pivot mask
		p.u.result.num = info.independent ? 1 : 0;
		p.u.result.den = static_cast<std::int64_t>(info.pivot_mask);
		// the RREF backs the dependency relation drawn on the result page
		p.u.result.stride = rref.stride;
		p.u.result.data = rref.data;
		REQUIRE(push(p), "push indep result failed");
}

// Q5 and Q6: read onto and one to one off the RREF of the standard matrix.
void App::run_transformation(OperationId, std::uint8_t slot, std::uint8_t) noexcept {
		if (slot >= kSlotCount || !slots_[slot].is_set())
				fail_fast("slot not set");

		const matrix_core::MatrixView a = slots_[slot].view_active();
		matrix_core::ArenaScope tx(persist_);
		matrix_core::ArenaScratchScope scratch_tx(scratch_);

		matrix_core::ExplainOptions opts_steps;
		opts_steps.enable = true;
		opts_steps.persist = &persist_;
		matrix_core::Explanation expl;

		matrix_core::MatrixMutView rref{};
		matrix_core::DependencyInfo info{};
		const matrix_core::Error err = matrix_core::op_dependency(
		        a, persist_, &rref, &info, &expl, opts_steps, dependency_labels(), matrix_core::DependencyPurpose::Transformation);
		if (!matrix_core::is_ok(err)) {
				show_message(err.code == matrix_core::ErrorCode::Overflow ? "common.out_of_memory"_tx : "common.error"_tx);
				return;
		}

		tx.commit();
		expl_ = std::move(expl);
		persist_tail_mark_ = persist_.mark();
		REQUIRE(pop(), "pop failed (transformation)");

		// onto is rank == rows, one to one is rank == cols
		Page p = Page::make_result_scalar(
		        OperationId::Transformation, slot, 0, expl_.available(), info.spans ? 1 : 0, info.independent ? 1 : 0);
		p.u.result.rows = a.rows;
		p.u.result.cols = a.cols;
		p.u.result.i = info.rank;
		REQUIRE(push(p), "push transformation result failed");
}

void App::render_result_span_indep(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		const int y0 = l.header_h + 26;
		gfx_SetTextXY(l.margin_x, y0);
		gfx_PrintString((s.op == OperationId::SpanTest) ? "result.span_label"_tx : "result.indep_label"_tx);
		gfx_PrintString((s.num != 0) ? "result.bool_yes"_tx : "result.bool_no"_tx);

		gfx_SetTextXY(l.margin_x, y0 + 14);
		gfx_PrintString("result.rank_eq"_tx);
		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
		w.append_u64(static_cast<std::uint64_t>(s.i));
		gfx_PrintString(fmt_buf_);

		gfx_PrintString("result.m_eq"_tx);
		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w2{fmt_buf_, sizeof(fmt_buf_)};
		w2.append_u64(static_cast<std::uint64_t>(s.rows));
		gfx_PrintString(fmt_buf_);
		gfx_PrintString("result.n_eq"_tx);
		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w3{fmt_buf_, sizeof(fmt_buf_)};
		w3.append_u64(static_cast<std::uint64_t>(s.cols));
		gfx_PrintString(fmt_buf_);

		gfx_SetTextXY(l.margin_x, y0 + 28);
		gfx_PrintString("result.piv_eq"_tx);
		render_pivot_cols(static_cast<std::uint32_t>(s.den), fmt_buf_, sizeof(fmt_buf_));
		gfx_PrintString(fmt_buf_);

		// for a dependent set, show the first vector as a combination of the
		// pivot vectors: the answer the review exercises ask to be written out
		if (s.op == OperationId::IndepTest && s.num == 0 && s.j != 0xFFu && s.data) {
				gfx_SetTextXY(l.margin_x, y0 + 42);
				fmt_buf_[0] = '\0';
				render_relation(s.data, s.stride, s.rows, static_cast<std::uint32_t>(s.den), s.j, fmt_buf_, sizeof(fmt_buf_));
				gfx_PrintString(fmt_buf_);
		}
}

void App::render_result_transformation(const ResultState& s) noexcept {
		const ui::Layout l = ui::Layout{};
		const int y0 = l.header_h + 26;
		// `num` is onto, `den` is one-to-one
		gfx_SetTextXY(l.margin_x, y0);
		gfx_PrintString("result.onto_label"_tx);
		gfx_PrintString((s.num != 0) ? "result.bool_yes"_tx : "result.bool_no"_tx);

		gfx_SetTextXY(l.margin_x, y0 + 14);
		gfx_PrintString("result.one_to_one_label"_tx);
		gfx_PrintString((s.den != 0) ? "result.bool_yes"_tx : "result.bool_no"_tx);

		gfx_SetTextXY(l.margin_x, y0 + 28);
		gfx_PrintString("result.rank_eq"_tx);
		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w{fmt_buf_, sizeof(fmt_buf_)};
		w.append_u64(static_cast<std::uint64_t>(s.i));
		gfx_PrintString(fmt_buf_);

		gfx_PrintString("result.m_eq"_tx);
		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w2{fmt_buf_, sizeof(fmt_buf_)};
		w2.append_u64(static_cast<std::uint64_t>(s.rows));
		gfx_PrintString(fmt_buf_);
		gfx_PrintString("result.n_eq"_tx);
		fmt_buf_[0] = '\0';
		matrix_core::CheckedWriter w3{fmt_buf_, sizeof(fmt_buf_)};
		w3.append_u64(static_cast<std::uint64_t>(s.cols));
		gfx_PrintString(fmt_buf_);
}

} // namespace matrix_shell
