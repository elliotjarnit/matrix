#pragma once

#include <cstddef>
#include <cstdint>

#include "matrix_core/error.hpp"
#include "matrix_core/matrix.hpp"
#include "matrix_core/rational.hpp"
#include "matrix_core/writer.hpp"

namespace matrix_core::latex {
struct Buffer {
		char* data = nullptr;
		std::size_t cap = 0;
};

ErrorCode write_rational(In const Rational& r, Out Buffer out) noexcept;
ErrorCode write_rational_display(In const Rational& r, Out Buffer out) noexcept;

enum class MatrixBrackets : std::uint8_t {
		BMatrix,
		PMatrix,
		VMatrix,
};

ErrorCode write_matrix(In MatrixView m, In MatrixBrackets brackets, Out Buffer out) noexcept;
ErrorCode write_matrix_display(In MatrixView m, In MatrixBrackets brackets, Out Buffer out) noexcept;

// writes an augmented matrix [L | R] using the latex array environment
//
// example:
//   \\left[\\begin{array}{rr|rr} ... \\end{array}\\right]
ErrorCode write_augmented_matrix(In MatrixView left, In MatrixView right, Out Buffer out) noexcept;
ErrorCode write_augmented_matrix_display(In MatrixView left, In MatrixView right, Out Buffer out) noexcept;

// composable variants: append to an existing Writer instead of resetting a buffer
ErrorCode append_matrix(In MatrixView m, In MatrixBrackets brackets, InOut Writer& w) noexcept;
ErrorCode append_augmented_matrix(In MatrixView left, In MatrixView right, InOut Writer& w) noexcept;
// writes a parametric solution as a sum of column vectors:
//
//   lhs = particular + t_{1} basis_1 + ... + t_{param_count} basis_k
//
// `lhs` is raw LaTeX for the left hand side ("x", "x_{h}", ...). Pass a
// default constructed MatrixView for `particular` to omit the constant vector
// (used for the homogeneous solution) and param_count == 0 to omit the
// parameters (a unique solution). With both omitted the result is "lhs = 0".
//
// `basis` supplies v_1 .. v_k in its first param_count columns.
ErrorCode write_parametric_solution(
        In const char* lhs, In MatrixView particular, In MatrixView basis, In std::uint8_t param_count, Out Buffer out) noexcept;
ErrorCode write_parametric_solution_display(
        In const char* lhs, In MatrixView particular, In MatrixView basis, In std::uint8_t param_count, Out Buffer out) noexcept;
} // namespace matrix_core::latex
