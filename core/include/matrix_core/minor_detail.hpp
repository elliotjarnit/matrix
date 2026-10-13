#pragma once

// Minors and their determinants written out step by step, shared by the
// operations that work with cofactors.

#include <cstdint>

#include "matrix_core/arena.hpp"
#include "matrix_core/error.hpp"
#include "matrix_core/latex.hpp"
#include "matrix_core/matrix.hpp"
#include "matrix_core/writer.hpp"

namespace matrix_core::detail {

// A Writer whose first failure sticks, so a step is written straight through
// and checked once at the end.
struct StepText {
		Writer w;
		ErrorCode ec = ErrorCode::Ok;

		StepText& str(const char* s) noexcept {
				if (is_ok(ec))
						ec = w.append(s);
				return *this;
		}
		StepText& ch(char c) noexcept {
				if (is_ok(ec))
						ec = w.put(c);
				return *this;
		}
		StepText& index1(std::uint8_t v) noexcept {
				if (is_ok(ec))
						ec = w.append_index1(v);
				return *this;
		}
		StepText& value(const Rational& r) noexcept {
				if (is_ok(ec))
						ec = w.append_rational_latex(r);
				return *this;
		}
		// one factor of a product: a negative value goes in parentheses, "5\cdot (-1)"
		StepText& factor(const Rational& r) noexcept {
				if (r.num() < 0)
						return ch('(').value(r).ch(')');
				return value(r);
		}
		StepText& matrix(MatrixView m, latex::MatrixBrackets brackets) noexcept {
				if (is_ok(ec))
						ec = latex::append_matrix(m, brackets, w);
				return *this;
		}
		StepText& det_bars(MatrixView m) noexcept { return matrix(m, latex::MatrixBrackets::VMatrix); }
		// a step is one display of centred lines: open(), lines split by br(), close()
		StepText& open() noexcept { return str("$$\\begin{array}{c}"); }
		StepText& br() noexcept { return str(" \\\\ "); }
		StepText& close() noexcept { return str("\\end{array}$$"); }
		// "_{31}": a 1 based subscript pair
		StepText& sub(std::uint8_t r, std::uint8_t c) noexcept { return str("_{").index1(r).index1(c).ch('}'); }
};

// M_ij: square A (at least 2x2) without row del_r and column del_c, allocated
// from `arena`
ErrorCode build_minor(In MatrixView a, In std::uint8_t del_r, In std::uint8_t del_c, InOut Arena& arena, Out MatrixMutView* out) noexcept;

// det(M_ij) with C_ij = (-1)^(i+j) det(M_ij), for square A of any size (a 1x1
// A has C_11 = 1 by convention). Uses `scratch` and gives it back.
ErrorCode cofactor_of(
        In MatrixView a, In std::uint8_t i, In std::uint8_t j, InOut Arena& scratch, Out Rational* minor, Out Rational* cofactor) noexcept;

// The lines of one cofactor: "\det(M_{ij}) = |M|", its working (2x2 by
// ad - bc, 3x3 across its first row, larger ones by value), then
// "C_{ij} = (-1)^{i+j}(det) = C". Writes lines only; the caller opens and
// closes the display. `scratch` holds M_ij.
void write_cofactor_lines(InOut StepText& t,
        In MatrixView a,
        In std::uint8_t i,
        In std::uint8_t j,
        In const Rational& minor,
        In const Rational& cofactor,
        InOut Arena& scratch) noexcept;

} // namespace matrix_core::detail
