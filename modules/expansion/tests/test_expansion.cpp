#include "matrix_core/matrix_core.hpp"
#include "matrix_core/ops_expansion.hpp"

#include "test_dbg_ce.hpp"

#include <cassert>
#include <cstring>
#include <initializer_list>

using matrix_core::Arena;
using matrix_core::ErrorCode;
using matrix_core::ExpansionLine;
using matrix_core::ExplainOptions;
using matrix_core::Explanation;
using matrix_core::MatrixMutView;
using matrix_core::Rational;
using matrix_core::Slab;
using matrix_core::StepRenderBuffers;

namespace {

MatrixMutView mat(Arena& arena, std::uint8_t rows, std::uint8_t cols, const std::int64_t* vals) {
		MatrixMutView m;
		assert(matrix_core::matrix_alloc(arena, rows, cols, &m) == ErrorCode::Ok);
		for (std::uint8_t r = 0; r < rows; r++) {
				for (std::uint8_t c = 0; c < cols; c++)
						m.at_mut(r, c) = Rational::from_int(vals[static_cast<std::size_t>(r) * cols + c]);
		}
		return m;
}

struct Harness {
		Slab slab;
		Arena persist;
		Arena scratch;

		Harness() {
				assert(slab.init(64 * 1024) == ErrorCode::Ok);
				persist = Arena(slab.data(), slab.size() / 2);
				scratch = Arena(slab.data() + (slab.size() / 2), slab.size() - (slab.size() / 2));
		}
};

char g_caption[128];
char g_latex[1024]; // same as App::step_latex_

const char* render(const Explanation& expl, std::size_t index, Arena& scratch) {
		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &scratch};
		assert(expl.render_step(index, bufs) == ErrorCode::Ok);
		return g_latex;
}

// det by expansion along `line` `index`, with every step rendered
Rational expand(Harness& h, MatrixMutView a, ExpansionLine line, std::uint8_t index, Explanation* expl) {
		Rational det;
		const auto err = matrix_core::op_cofactor_expansion(
		        a.view(), line, index, h.scratch, &det, expl, ExplainOptions{.enable = true, .persist = &h.persist});
		assert(matrix_core::is_ok(err));
		assert(expl->available());
		for (std::size_t i = 0; i < expl->step_count(); i++)
				(void)render(*expl, i, h.scratch);
		return det;
}

void worksheet_1a_third_row() {
		Harness h;
		const std::int64_t v[] = {2, 5, -3, 4, 7, -1, 1, 0, 3};
		MatrixMutView a = mat(h.persist, 3, 3, v);
		Explanation expl;
		const Rational det = expand(h, a, ExpansionLine::Row, 2, &expl);
		assert(det.num() == -2 && det.den() == 1);
		assert(expl.step_count() == 5); // setup, three terms, sum

		render(expl, 0, h.scratch);
		assert(std::strcmp(g_caption, "Expand across row 3") == 0);
		assert(std::strstr(g_latex, " \\\\ = a_{31}C_{31} + a_{32}C_{32} + a_{33}C_{33}\\end{array}$$") != nullptr);

		render(expl, 1, h.scratch);
		assert(std::strcmp(g_caption, "Delete row 3, column 1") == 0);
		assert(std::strstr(g_latex, "$$\\begin{array}{c}\\det(M_{31}) = ") == g_latex);
		assert(std::strstr(g_latex, " \\\\ = 5\\cdot (-1) - (-3)\\cdot 7 = 16 \\\\ ") != nullptr);
		assert(std::strstr(g_latex, " \\\\ C_{31} = (-1)^{3+1}(16) = 16 \\\\ ") != nullptr);
		assert(std::strstr(g_latex, " \\\\ a_{31}C_{31} = 1\\cdot 16 = 16\\end{array}$$") != nullptr);

		// a_32 = 0: the term drops out without a minor
		render(expl, 2, h.scratch);
		assert(std::strcmp(g_caption, "$a_{32} = 0$, so the term is 0") == 0);
		assert(std::strcmp(g_latex, "$$a_{32}C_{32} = 0$$") == 0);

		render(expl, 3, h.scratch);
		assert(std::strstr(g_latex, " \\\\ C_{33} = (-1)^{3+3}(-6) = -6 \\\\ ") != nullptr);

		render(expl, 4, h.scratch);
		assert(std::strcmp(g_latex, "$$\\begin{array}{c}\\det(A) = 1\\cdot 16 + 0 + 3\\cdot (-6) \\\\ = -2\\end{array}$$") == 0);
}

void worksheet_1b_columns() {
		Harness h;
		const std::int64_t v[] = {4, -5, 2, 6, 0, 1, 4, 6, 0, -2, 2, 6, 0, 4, -3, 2};
		MatrixMutView a = mat(h.persist, 4, 4, v);

		Explanation down_first;
		const Rational d1 = expand(h, a, ExpansionLine::Col, 0, &down_first);
		assert(d1.num() == 488 && d1.den() == 1);
		// the 3x3 minor is worked across its first row
		render(down_first, 1, h.scratch);
		assert(std::strstr(g_latex, " \\\\ = 1(2\\cdot 2 - 6\\cdot (-3)) \\\\ ") != nullptr);
		assert(std::strstr(g_latex, " \\\\ - 4((-2)\\cdot 2 - 6\\cdot 4) \\\\ ") != nullptr);
		assert(std::strstr(g_latex, " \\\\ + 6((-2)\\cdot (-3) - 2\\cdot 4) \\\\ ") != nullptr);
		assert(std::strstr(g_latex, " \\\\ = 122 \\\\ ") != nullptr);
		assert(std::strstr(g_latex, " \\\\ a_{11}C_{11} = 4\\cdot 122 = 488\\end{array}$$") != nullptr);

		Explanation down_fourth;
		const Rational d4 = expand(h, a, ExpansionLine::Col, 3, &down_fourth);
		assert(d4.num() == 488 && d4.den() == 1);
		render(down_fourth, 0, h.scratch);
		assert(std::strcmp(g_caption, "Expand down column 4") == 0);
		// four terms: the sum wraps after three
		assert(std::strstr(g_latex, "a_{14}C_{14} + a_{24}C_{24} + a_{34}C_{34} \\\\ + a_{44}C_{44}\\end{array}$$") != nullptr);
}

void worksheet_3a_first_row() {
		Harness h;
		const std::int64_t v[] = {4, -2, 8, 8, -8, 16, 4, -4, 2};
		MatrixMutView a = mat(h.persist, 3, 3, v);
		Explanation expl;
		const Rational det = expand(h, a, ExpansionLine::Row, 0, &expl);
		assert(det.num() == 96 && det.den() == 1);
		render(expl, 2, h.scratch);
		assert(std::strstr(g_latex, " \\\\ C_{12} = (-1)^{1+2}(-48) = 48 \\\\ ") != nullptr);
}

void every_line_agrees() {
		Harness h;
		const std::int64_t v[] = {3, 1, -2, 0, 5, 2, 7, -1, 0, 4, 1, 6, -3, 2, 0, 9, 1, 1, 8, -4, 2, 0, -6, 3, 5};
		MatrixMutView a = mat(h.persist, 5, 5, v);
		Rational expected;
		assert(matrix_core::is_ok(matrix_core::op_det(a.view(), h.scratch, &expected, nullptr, ExplainOptions{})));
		for (std::uint8_t k = 0; k < 5; k++) {
				for (ExpansionLine line : {ExpansionLine::Row, ExpansionLine::Col}) {
						const std::size_t mark = h.persist.mark();
						Explanation expl;
						const Rational d = expand(h, a, line, k, &expl);
						assert(d.num() == expected.num() && d.den() == expected.den());
						expl = Explanation{};
						h.persist.rewind(mark);
				}
		}
}

void small_and_bad_inputs() {
		Harness h;
		Rational det;
		Explanation expl;
		const ExplainOptions opts{.enable = true, .persist = &h.persist};

		const std::int64_t one[] = {-7};
		MatrixMutView a1 = mat(h.persist, 1, 1, one);
		assert(matrix_core::is_ok(matrix_core::op_cofactor_expansion(a1.view(), ExpansionLine::Row, 0, h.scratch, &det, &expl, opts)));
		assert(det.num() == -7);
		assert(expl.step_count() == 1);
		render(expl, 0, h.scratch);

		const std::int64_t two[] = {1, 2, 3, 4};
		MatrixMutView a2 = mat(h.persist, 2, 2, two);
		Explanation expl2;
		assert(matrix_core::is_ok(matrix_core::op_cofactor_expansion(a2.view(), ExpansionLine::Col, 1, h.scratch, &det, &expl2, opts)));
		assert(det.num() == -2);
		render(expl2, 1, h.scratch);
		assert(std::strstr(g_latex, "$$\\begin{array}{c}\\det(M_{12}) = 3 \\\\ ") == g_latex);
		assert(std::strstr(g_latex, " \\\\ C_{12} = (-1)^{1+2}(3) = -3 \\\\ ") != nullptr);

		StepRenderBuffers bufs{g_caption, sizeof(g_caption), g_latex, sizeof(g_latex), &h.scratch};
		assert(expl2.render_step(expl2.step_count(), bufs) == ErrorCode::StepOutOfRange);

		MatrixMutView wide = mat(h.persist, 2, 2, two);
		wide.cols = 1;
		assert(matrix_core::op_cofactor_expansion(wide.view(), ExpansionLine::Row, 0, h.scratch, &det, nullptr, ExplainOptions{}).code ==
		        ErrorCode::NotSquare);
		assert(matrix_core::op_cofactor_expansion(a2.view(), ExpansionLine::Row, 2, h.scratch, &det, nullptr, ExplainOptions{}).code ==
		        ErrorCode::IndexOutOfRange);
}

} // namespace

int main() {
		worksheet_1a_third_row();
		worksheet_1b_columns();
		worksheet_3a_first_row();
		every_line_agrees();
		small_and_bad_inputs();
		return 0;
}
