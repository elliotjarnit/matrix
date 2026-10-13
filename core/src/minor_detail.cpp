#include "matrix_core/minor_detail.hpp"

#include "matrix_core/det_detail.hpp"

namespace matrix_core::detail {
namespace {

// "p\cdot s - q\cdot r" for the 2x2 block of `m` left by deleting row `skip_r`
// and column `skip_c` (pass 0xFF to keep everything of a 2x2 matrix)
void write_2x2(StepText& t, MatrixView m, std::uint8_t skip_r, std::uint8_t skip_c) noexcept {
		std::uint8_t rows[2]{};
		std::uint8_t cols[2]{};
		std::uint8_t nr = 0;
		std::uint8_t nc = 0;
		for (std::uint8_t i = 0; i < m.rows && nr < 2; i++) {
				if (i != skip_r)
						rows[nr++] = i;
		}
		for (std::uint8_t j = 0; j < m.cols && nc < 2; j++) {
				if (j != skip_c)
						cols[nc++] = j;
		}
		t.factor(m.at(rows[0], cols[0])).str("\\cdot ").factor(m.at(rows[1], cols[1]));
		t.str(" - ").factor(m.at(rows[0], cols[1])).str("\\cdot ").factor(m.at(rows[1], cols[0]));
}

// det(M) worked out on the lines under "\det(M_{ij}) = |M|": 2x2 by ad - bc,
// 3x3 across its first row (one line per nonzero entry), anything larger by
// its value
void write_minor_value(StepText& t, MatrixView m, const Rational& v) noexcept {
		if (m.rows == 2) {
				t.br().str("= ");
				write_2x2(t, m, 0xFF, 0xFF);
				t.str(" = ").value(v);
				return;
		}
		if (m.rows == 3) {
				bool first = true;
				for (std::uint8_t j = 0; j < 3; j++) {
						const Rational& e = m.at(0, j);
						if (e.is_zero())
								continue;
						const bool minus = (j & 1u) != 0u;
						t.br();
						if (first)
								t.str(minus ? "= -" : "= ");
						else
								t.str(minus ? "- " : "+ ");
						first = false;
						t.factor(e).ch('(');
						write_2x2(t, m, 0, j);
						t.ch(')');
				}
		}
		t.br().str("= ").value(v);
}

} // namespace

ErrorCode build_minor(MatrixView a, std::uint8_t del_r, std::uint8_t del_c, Arena& arena, MatrixMutView* out) noexcept {
		if (a.rows != a.cols || a.rows < 2 || del_r >= a.rows || del_c >= a.cols || !out)
				return ErrorCode::Internal;
		MatrixMutView m;
		ErrorCode ec = matrix_alloc(arena, static_cast<std::uint8_t>(a.rows - 1), static_cast<std::uint8_t>(a.cols - 1), &m);
		if (!is_ok(ec))
				return ec;

		std::uint8_t dr = 0;
		for (std::uint8_t r = 0; r < a.rows; r++) {
				if (r == del_r)
						continue;
				std::uint8_t dc = 0;
				for (std::uint8_t c = 0; c < a.cols; c++) {
						if (c == del_c)
								continue;
						m.at_mut(dr, dc) = a.at(r, c);
						dc++;
				}
				dr++;
		}
		*out = m;
		return ErrorCode::Ok;
}

ErrorCode cofactor_of(MatrixView a, std::uint8_t i, std::uint8_t j, Arena& scratch, Rational* minor, Rational* cofactor) noexcept {
		if (!minor || !cofactor)
				return ErrorCode::Internal;
		Rational m = Rational::from_int(1);
		if (a.rows > 1) {
				const std::size_t mark = scratch.mark();
				MatrixMutView sub;
				ErrorCode ec = build_minor(a, i, j, scratch, &sub);
				if (is_ok(ec))
						ec = det_elim(sub, &m, nullptr, static_cast<std::size_t>(-1), nullptr);
				scratch.rewind(mark);
				if (!is_ok(ec))
						return ec;
		}
		Rational c = m;
		if (((i + j) & 1u) != 0u) {
				const ErrorCode ec = rational_neg(m, &c);
				if (!is_ok(ec))
						return ec;
		}
		*minor = m;
		*cofactor = c;
		return ErrorCode::Ok;
}

void write_cofactor_lines(StepText& t,
        MatrixView a,
        std::uint8_t i,
        std::uint8_t j,
        const Rational& minor,
        const Rational& cofactor,
        Arena& scratch) noexcept {
		if (a.rows == 1) {
				// nothing left to delete: C_11 = 1 by convention
				t.str("C").sub(i, j).str(" = 1");
				return;
		}

		MatrixMutView m;
		const ErrorCode ec = build_minor(a, i, j, scratch, &m);
		if (!is_ok(ec)) {
				t.ec = ec;
				return;
		}

		t.str("\\det(M").sub(i, j).str(") = ");
		if (m.rows == 1) {
				t.value(minor);
		} else {
				t.det_bars(m.view());
				write_minor_value(t, m.view(), minor);
		}
		t.br().str("C").sub(i, j).str(" = (-1)^{").index1(i).ch('+').index1(j).str("}(").value(minor).str(") = ").value(cofactor);
}

} // namespace matrix_core::detail
