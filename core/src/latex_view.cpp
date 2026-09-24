#include "matrix_core/latex.hpp"
#include "matrix_core/writer.hpp"

#include <cassert>
#include <cstdint>

namespace matrix_core::latex {
namespace {

ErrorCode write_rational_inner(const Rational& r, Writer& w) noexcept {
		return w.append_rational_latex(r);
}

ErrorCode write_rational_display_inner(const Rational& r, Writer& w) noexcept {
		ErrorCode ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		ec = write_rational_inner(r, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

const char* begin_env(MatrixBrackets b) noexcept {
		switch (b) {
		case MatrixBrackets::BMatrix:
				return "\\begin{bmatrix}";
		case MatrixBrackets::PMatrix:
				return "\\begin{pmatrix}";
		case MatrixBrackets::VMatrix:
				return "\\begin{vmatrix}";
		}
		assert(false && "invalid MatrixBrackets");
		return nullptr;
}

const char* end_env(MatrixBrackets b) noexcept {
		switch (b) {
		case MatrixBrackets::BMatrix:
				return "\\end{bmatrix}";
		case MatrixBrackets::PMatrix:
				return "\\end{pmatrix}";
		case MatrixBrackets::VMatrix:
				return "\\end{vmatrix}";
		}
		assert(false && "invalid MatrixBrackets");
		return nullptr;
}

ErrorCode write_matrix_inner(MatrixView m, MatrixBrackets brackets, Writer& w) noexcept {
		if (!m.data)
				return ErrorCode::Internal;

		const char* begin = begin_env(brackets);
		const char* end = end_env(brackets);
		if (!begin || !end)
				return ErrorCode::Internal;

		ErrorCode ec = w.append(begin);
		if (!is_ok(ec))
				return ec;

		for (std::uint8_t row = 0; row < m.rows; row++) {
				for (std::uint8_t col = 0; col < m.cols; col++) {
						if (col != 0) {
								ec = w.append(" & ");
								if (!is_ok(ec))
										return ec;
						}

						ec = write_rational_inner(m.at(row, col), w);
						if (!is_ok(ec))
								return ec;
				}

				if (row + 1 < m.rows) {
						ec = w.append(" \\\\ ");
						if (!is_ok(ec))
								return ec;
				}
		}

		ec = w.append(end);
		if (!is_ok(ec))
				return ec;

		return ErrorCode::Ok;
}

ErrorCode write_matrix_display_inner(MatrixView m, MatrixBrackets brackets, Writer& w) noexcept {
		ErrorCode ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		ec = write_matrix_inner(m, brackets, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

ErrorCode write_augmented_matrix_inner(MatrixView left, MatrixView right, Writer& w) noexcept {
		if (!left.data || !right.data)
				return ErrorCode::Internal;
		if (left.rows != right.rows)
				return ErrorCode::DimensionMismatch;
		if (left.cols == 0 || right.cols == 0)
				return ErrorCode::InvalidDimension;

		ErrorCode ec = w.append("\\left[\\begin{array}{");
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t i = 0; i < left.cols; i++) {
				ec = w.put('r');
				if (!is_ok(ec))
						return ec;
		}
		ec = w.put('|');
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t i = 0; i < right.cols; i++) {
				ec = w.put('r');
				if (!is_ok(ec))
						return ec;
		}
		ec = w.append("}");
		if (!is_ok(ec))
				return ec;

		const std::uint8_t total_cols = static_cast<std::uint8_t>(left.cols + right.cols);
		for (std::uint8_t row = 0; row < left.rows; row++) {
				for (std::uint8_t col = 0; col < total_cols; col++) {
						if (col != 0) {
								ec = w.append(" & ");
								if (!is_ok(ec))
										return ec;
						}

						if (col < left.cols)
								ec = write_rational_inner(left.at(row, col), w);
						else
								ec = write_rational_inner(right.at(row, static_cast<std::uint8_t>(col - left.cols)), w);
						if (!is_ok(ec))
								return ec;
				}

				if (row + 1 < left.rows) {
						ec = w.append(" \\\\ ");
						if (!is_ok(ec))
								return ec;
				}
		}

		ec = w.append("\\end{array}\\right]");
		if (!is_ok(ec))
				return ec;

		return ErrorCode::Ok;
}

ErrorCode write_augmented_matrix_display_inner(MatrixView left, MatrixView right, Writer& w) noexcept {
		ErrorCode ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		ec = write_augmented_matrix_inner(left, right, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

// writes "t_{k}" for parameter k (0 based)
ErrorCode write_param_symbol(std::uint8_t k, Writer& w) noexcept {
		ErrorCode ec = w.append("t_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(k);
		if (!is_ok(ec))
				return ec;
		return w.append("}");
}

ErrorCode write_parametric_solution_inner(
        const char* lhs, MatrixView particular, MatrixView basis, std::uint8_t param_count, Writer& w) noexcept {
		if (!lhs)
				return ErrorCode::Internal;
		if (param_count > 0) {
				if (!basis.data)
						return ErrorCode::Internal;
				if (param_count > basis.cols)
						return ErrorCode::InvalidDimension;
				if (particular.data && particular.rows != basis.rows)
						return ErrorCode::DimensionMismatch;
		}
		if (particular.data && particular.cols != 1)
				return ErrorCode::InvalidDimension;

		ErrorCode ec = w.append(lhs);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = ");
		if (!is_ok(ec))
				return ec;

		bool wrote_term = false;

		if (particular.data) {
				ec = write_matrix_inner(particular, MatrixBrackets::BMatrix, w);
				if (!is_ok(ec))
						return ec;
				wrote_term = true;
		}

		for (std::uint8_t k = 0; k < param_count; k++) {
				if (wrote_term) {
						ec = w.append(" + ");
						if (!is_ok(ec))
								return ec;
				}
				ec = write_param_symbol(k, w);
				if (!is_ok(ec))
						return ec;

				const MatrixView col{basis.rows, 1, basis.stride, basis.data + k};
				ec = write_matrix_inner(col, MatrixBrackets::BMatrix, w);
				if (!is_ok(ec))
						return ec;
				wrote_term = true;
		}

		if (!wrote_term)
				return w.append("0");

		return ErrorCode::Ok;
}

ErrorCode write_parametric_solution_display_inner(
        const char* lhs, MatrixView particular, MatrixView basis, std::uint8_t param_count, Writer& w) noexcept {
		ErrorCode ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		ec = write_parametric_solution_inner(lhs, particular, basis, param_count, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

} // namespace

ErrorCode write_rational(const Rational& r, Buffer out) noexcept {
		Writer w{out.data, out.cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';
		return write_rational_inner(r, w);
}

ErrorCode write_rational_display(const Rational& r, Buffer out) noexcept {
		Writer w{out.data, out.cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';
		return write_rational_display_inner(r, w);
}

ErrorCode write_matrix(MatrixView m, MatrixBrackets brackets, Buffer out) noexcept {
		Writer w{out.data, out.cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';

		return write_matrix_inner(m, brackets, w);
}

ErrorCode write_matrix_display(MatrixView m, MatrixBrackets brackets, Buffer out) noexcept {
		Writer w{out.data, out.cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';
		return write_matrix_display_inner(m, brackets, w);
}

ErrorCode write_augmented_matrix(MatrixView left, MatrixView right, Buffer out) noexcept {
		Writer w{out.data, out.cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';
		return write_augmented_matrix_inner(left, right, w);
}

ErrorCode append_matrix(MatrixView m, MatrixBrackets brackets, Writer& w) noexcept {
		return write_matrix_inner(m, brackets, w);
}

ErrorCode append_augmented_matrix(MatrixView left, MatrixView right, Writer& w) noexcept {
		return write_augmented_matrix_inner(left, right, w);
}

ErrorCode append_signed_term(const Rational& c, const char* symbol, std::uint8_t index0, bool leading, Writer& w) noexcept {
		if (!symbol)
				return ErrorCode::Internal;
		if (c.is_zero())
				return ErrorCode::Internal;

		Rational mag = c;
		if (c.num() < 0) {
				ErrorCode ec = rational_neg(c, &mag);
				if (!is_ok(ec))
						return ec;
				ec = w.append(leading ? "-" : " - ");
				if (!is_ok(ec))
						return ec;
		} else if (!leading) {
				ErrorCode ec = w.append(" + ");
				if (!is_ok(ec))
						return ec;
		}

		// a unit coefficient is written as just the symbol
		if (!(mag.num() == 1 && mag.den() == 1)) {
				ErrorCode ec = write_rational_inner(mag, w);
				if (!is_ok(ec))
						return ec;
		}

		ErrorCode ec = w.append(symbol);
		if (!is_ok(ec))
				return ec;
		ec = w.append("_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(index0);
		if (!is_ok(ec))
				return ec;
		return w.append("}");
}

ErrorCode write_augmented_matrix_display(MatrixView left, MatrixView right, Buffer out) noexcept {
		Writer w{out.data, out.cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';
		return write_augmented_matrix_display_inner(left, right, w);
}

ErrorCode write_parametric_solution(
        const char* lhs, MatrixView particular, MatrixView basis, std::uint8_t param_count, Buffer out) noexcept {
		Writer w{out.data, out.cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';
		return write_parametric_solution_inner(lhs, particular, basis, param_count, w);
}

ErrorCode write_parametric_solution_display(
        const char* lhs, MatrixView particular, MatrixView basis, std::uint8_t param_count, Buffer out) noexcept {
		Writer w{out.data, out.cap, 0};
		if (w.data && w.cap)
				w.data[0] = '\0';
		return write_parametric_solution_display_inner(lhs, particular, basis, param_count, w);
}

} // namespace matrix_core::latex
