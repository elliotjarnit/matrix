#pragma once

#include <cstddef>
#include <cstdint>

#include "matrix_core/arena.hpp"
#include "matrix_core/config.hpp"
#include "matrix_core/error.hpp"
#include "matrix_core/explanation.hpp"
#include "matrix_core/matrix.hpp"
#include "matrix_core/rational.hpp"
#include "matrix_core/row_ops.hpp"

namespace matrix_core {
enum class ExplainDetail : std::uint8_t {
		Full = 0,
		Compact, // fewer, higher level steps
};

struct ExplainOptions {
		bool enable = false;
		Arena* persist = nullptr; // required when enable==true
		ExplainDetail detail = ExplainDetail::Full;
};

// indices in these APIs are 0 based (consistent with m.at(r, c))
// the shell/ui can present 1 based indices and convert as needed
//
// memory model:
// - matrix data must outlive any Explanation created from it
// - when opts.enable==true, opts.persist must be a valid long lived arena for
// the
//   explanation context
// - step rendering requires StepRenderBuffers::scratch to be a valid arena, it
// is
//   cleared by the renderer on each call
//
Error op_add(In MatrixView a, In MatrixView b, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;
Error op_sub(In MatrixView a, In MatrixView b, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;
Error op_mul(In MatrixView a, In MatrixView b, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;
Error op_transpose(In MatrixView a, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

enum class EchelonKind : std::uint8_t {
		Ref,
		Rref,
};

Error op_echelon(
        In MatrixView a, In EchelonKind kind, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

// Complete ("general" or parametric) solution of A x = b.
//
// Row reduces the augmented matrix [A | b] and splits the solution set into
//   x = x_p + t_1 v_1 + ... + t_k v_k
// where x_p is the particular solution obtained by setting every free variable
// to zero and v_1 .. v_k is the basis of Null(A) produced by
// space_null_basis(): a particular solution of the non homogeneous system plus
// the general solution of the homogeneous one.
// Labels for the column row expansion explanation. The caller owns the strings
// and they must outlive the Explanation; a null field falls back to English.
struct ColRowLabels {
		const char* intro = nullptr; // "AB is the sum of the outer products"
		const char* term = nullptr;	 // "term"
		const char* sum = nullptr;	 // "sum of the outer products"
};

// A*B computed the way the review exercises call the "column row expansion":
//
//   AB = sum over k of  col_k(A) * row_k(B)
//
// Each term is the outer product of a column of A with the matching row of B,
// an m x n matrix of rank at most one. a is m x p, b is p x n, out is m x n.
//
// The value written to `out` is the ordinary product; only the explanation
// differs from op_mul().
Error op_mul_colrow(In MatrixView a,
        In MatrixView b,
        Out MatrixMutView out,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const ColRowLabels& labels = ColRowLabels{}) noexcept;

// Labels for the prose fragments the dependency explanation emits. The caller
// owns the strings and they must outlive the Explanation; a null field falls
// back to the English default.
struct DependencyLabels {
		const char* vectors = nullptr;        // "columns are the vectors"
		const char* rref = nullptr;           // "RREF"
		const char* independent = nullptr;    // "linearly independent"
		const char* dependent = nullptr;      // "linearly dependent"
		const char* vectors_word = nullptr;   // "vectors"
		const char* onto = nullptr;           // "onto"
		const char* not_onto = nullptr;       // "not onto"
		const char* one_to_one = nullptr;     // "one-to-one"
		const char* not_one_to_one = nullptr; // "not one-to-one"
		const char* rows_word = nullptr;      // "rows"
		const char* cols_word = nullptr;      // "columns"
		const char* rank_word = nullptr;      // "rank"
		const char* relation = nullptr;       // "each dependent vector over the pivot vectors"
};

// What the columns of A are being asked about. The reduction is identical; only
// the verdict differs.
enum class DependencyPurpose : std::uint8_t {
		Vectors = 0,    // a set of vectors: independence, and the dependency relations
		Transformation, // the standard matrix of T: onto (rank == rows) and one to one (rank == cols)
};

// Linear (in)dependence of the columns of A, with the dependency relations.
//
// The columns v_1 .. v_n are independent exactly when every column is a pivot
// column of RREF(A). Each non pivot column f is a combination of the pivot
// columns before it, read straight off the RREF:
//
//   v_f = sum over pivots i of  RREF(pivot_rows[i], f) * v_{pivot_cols[i]}
//
// which is what the review exercises mean by "show that one of them is a
// linear combination of the other two".
struct DependencyInfo {
		bool independent = false;
		bool spans = false;            // rank == a.rows: the columns span R^m
		std::uint8_t vector_count = 0; // n, the number of vectors (a.cols)
		std::uint8_t dim = 0;          // m, the dimension they live in (a.rows)
		std::uint8_t rank = 0;
		std::uint8_t dependent_count = 0;        // n - rank
		std::uint32_t pivot_mask = 0;            // bit c set when column c is a pivot column
		std::uint8_t pivot_cols[kMaxCols]{};     // 0 based, valid in [0, rank), ascending
		std::uint8_t pivot_rows[kMaxCols]{};     // row holding pivot_cols[i], valid in [0, rank)
		std::uint8_t dependent_cols[kMaxCols]{}; // 0 based, valid in [0, dependent_count)
};

// RREF(A) is written to `persist` and returned through out_rref so the caller
// can read the relation coefficients out of it; the reduction happens in place
// there, so this op needs no scratch arena of its own.
//
// When opts.enable is true, opts.persist must be &persist: the explanation
// context holds views into A and into that RREF.
//
// `info` is written in place as the reduction proceeds, so it is only
// meaningful when the call succeeds.
Error op_dependency(In MatrixView a,
        InOut Arena& persist,
        Out MatrixMutView* out_rref,
        Out DependencyInfo* info,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const DependencyLabels& labels = DependencyLabels{},
        In DependencyPurpose purpose = DependencyPurpose::Vectors) noexcept;

// Labels for the prose fragments the parametric solve explanation emits. The
// caller owns the strings and they must outlive the Explanation; a null field
// falls back to the English default. Core has no text catalog, so the shell
// passes translated strings here.
struct ParametricLabels {
		const char* augmented = nullptr;    // "augmented matrix [A | b]"
		const char* rref = nullptr;         // "reduced row echelon form"
		const char* solve_pivots = nullptr; // "solve each row for its pivot variable"
		const char* free_vars = nullptr;    // "free"
		const char* homogeneous = nullptr;  // "general solution of Ax = 0"
		const char* complete = nullptr;     // "complete solution"
		const char* inconsistent = nullptr; // "no solution"
		const char* unique = nullptr;       // "unique solution"
};

struct ParametricSolution {
		bool consistent = false;
		std::uint8_t var_count = 0; // n, the number of unknowns (a.cols)
		std::uint8_t rank = 0;
		std::uint8_t nullity = 0;            // k, the number of free parameters
		std::uint32_t pivot_mask = 0;        // bit c is set when column c is a pivot column
		std::uint8_t pivot_cols[kMaxCols]{}; // 0 based, valid in [0, rank), ascending
		std::uint8_t pivot_rows[kMaxCols]{}; // row holding pivot_cols[i], valid in [0, rank)
		std::uint8_t free_cols[kMaxCols]{};  // 0 based, valid in [0, nullity), ascending
		std::uint8_t inconsistent_row = 0;   // row of the 0 = c contradiction, valid when !consistent
};

// a is m x n and b is m x 1.
//
// The copy of [A | b] that the steps replay, x_p and the null space basis are
// allocated from `persist` and must outlive the Explanation; `scratch` holds
// working storage only.
//
// out_particular is n x 1 and is all zero when the system is inconsistent.
// out_null_basis is n x k, or the n x 1 zero vector when k == 0 (matching
// space_null_basis, which has no empty basis representation) so callers must
// read info->nullity to tell a single basis vector from "no parameters".
//
// An inconsistent system is not an error: it is reported through
// info->consistent so the steps stay available. `info` is written in place
// as the solve proceeds, so it is only meaningful when the call succeeds.
//
// When opts.enable is true, opts.persist must be &persist: the explanation
// context holds views into the augmented copy allocated there.
Error op_solve_parametric(In MatrixView a,
        In MatrixView b,
        InOut Arena& persist,
        InOut Arena& scratch,
        Out MatrixMutView* out_particular,
        Out MatrixMutView* out_null_basis,
        Out ParametricSolution* info,
        Out Explanation* expl,
        In const ExplainOptions& opts,
        In const ParametricLabels& labels = ParametricLabels{}) noexcept;

Error op_det(In MatrixView a, InOut Arena& scratch, Out Rational* out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

// determinant of A with column "col" replaced by vector "b" (n x 1), useful for
// cramer's rule step breakdown (Δ_i)
Error op_det_replace_column(In MatrixView a,
        In MatrixView b,
        In std::uint8_t col,
        InOut Arena& scratch,
        Out Rational* out,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

// element cofactor for square A (0 based indices)
//
// For A of size 1x1, C_{1,1} is defined as 1.
//
// when opts.enable==true, explanation provides:
//   A -> submatrix -> row ops -> final value
Error op_cofactor_element(In MatrixView a,
        In std::uint8_t i,
        In std::uint8_t j,
        InOut Arena& scratch,
        Out Rational* out,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

// matrix of minors for square A (n>=2). output is n×n:
//   M[i,j] = det(A with row i and column j removed)
Error op_minor_matrix(In MatrixView a, InOut Arena& scratch, Out MatrixMutView out) noexcept;

// cramers rule solve (Ax=b), returning x as an n x 1 matrix. no step breakdown
// is produced here, shell can request Δ and Δ_i explanations via op_det()
// and op_det_replace_column()
Error op_cramer_solve(In MatrixView a, In MatrixView b, InOut Arena& scratch, Out MatrixMutView x_out) noexcept;

// inverse via gauss jordan elimination on the augmented matrix [A | I]
//
// on success, writes A^{-1} into out
// when opts.enable==true, explanation steps render the augmented matrix after
// each row operation
Error op_inverse(
        In MatrixView a, InOut Arena& scratch, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

/// LU factorization (Doolittle)

// Labels for the prose fragments the LU explanations emit. The caller owns the
// strings and they must outlive the Explanation; a null field falls back to the
// English default. Core has no text catalog, so the shell passes translated
// strings here.
struct LuLabels {
		const char* skip_column = nullptr;  // "no pivot, skip column"
		const char* check = nullptr;        // "check"
		const char* forward = nullptr;      // "forward substitution"
		const char* back = nullptr;         // "back substitution"
		const char* inconsistent = nullptr; // "inconsistent: no solution"
		const char* particular = nullptr;   // "particular solution"
		const char* homogeneous = nullptr;  // "homogeneous solution"
		const char* summary = nullptr;      // "summary"
		const char* swap_rows = nullptr;    // "swap rows"
};

enum class LuPivoting : std::uint8_t {
		None = 0, // stop when a row swap would be needed
		Partial,  // swap rows when a pivot is zero, giving PA = LU
};

// A = LU (or PA = LU) using row replacement only, the way it is done by hand:
//   R_i <- R_i - m R_r   records   L(i, r) = m
//
// A need not be square. l_out is m x m unit lower triangular, u_out is the
// m x n echelon form. A pivot position that is zero with an all zero column
// below it is skipped and the pivot row does not advance.
//
// With LuPivoting::None a zero pivot that has a nonzero entry below it returns
// ErrorCode::RowSwapRequired (Error::j holds the 0 based column, Error::i the
// pivot row): A has no LU factorization without a permutation.
//
// With LuPivoting::Partial those rows are swapped instead, the multipliers
// already recorded in L move with them, and p_out receives the permutation so
// that PA = LU. p_out is optional (pass a default constructed view to skip it)
// and is ignored when pivoting is None.
Error op_lu_factor(In MatrixView a,
        In LuPivoting pivoting,
        Out MatrixMutView p_out,
        Out MatrixMutView l_out,
        Out MatrixMutView u_out,
        In const LuLabels* labels,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

struct LuSolveInfo {
		// true when U is square with a nonzero diagonal, i.e. Ux = y was solved by
		// back substitution. false means [U | y] was row reduced instead.
		bool unique = false;
		bool consistent = true;
		bool permuted = false;    // at least one row swap was applied
		std::uint8_t bad_row = 0; // 0 based row of RREF([U | y]) witnessing inconsistency
		std::uint8_t rank = 0;
		std::uint8_t nullity = 0;
};

// Solve Ax = b through the LU factorization above: Ly = Pb by forward
// substitution, then Ux = y by back substitution (square U with nonzero
// diagonal) or by row reducing [U | y]. P is the identity unless pivoting is
// Partial and a swap was needed.
//
// y_out is m x 1, x_out is n x 1 (free variables set to zero, all zero when the
// system is inconsistent) and rref_out is m x (n+1), always filled with
// RREF([U | y]) so the caller can derive the null space basis.
//
// an inconsistent system is not an error: it is reported through info so the
// steps stay available.
Error op_lu_solve(In MatrixView a,
        In MatrixView b,
        In LuPivoting pivoting,
        InOut Arena& scratch,
        Out MatrixMutView y_out,
        Out MatrixMutView x_out,
        Out MatrixMutView rref_out,
        Out LuSolveInfo* info,
        In const LuLabels* labels,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

/// Vector operations

// dot product of two vectors. vectors may be represented as n×1 or 1×n
Error op_dot(In MatrixView u, In MatrixView v, Out Rational* out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

// cross product of two 3D vectors. inputs may be 3×1 or 1×3
// output must have the same dimensions as u (same orientation)
Error op_cross(In MatrixView u, In MatrixView v, Out MatrixMutView out, Out Explanation* expl, In const ExplainOptions& opts) noexcept;

struct ProjDecomposeResult {
		Rational dot_uv = Rational::from_int(0);
		Rational dot_vv = Rational::from_int(0);
		Rational k = Rational::from_int(0);          // k = (u·v)/(v·v)
		Rational proj_norm2 = Rational::from_int(0); // ||proj||^2
		Rational orth_norm2 = Rational::from_int(0); // ||orth||^2
};

// decompose u into a component along v and an orthogonal remainder
//   proj = proj_v(u) = (u·v)/(v·v) * v
//   orth = u - proj
//
// u and v may be n×1 or 1×n (same length n in [1 to 6])
// out_proj and out_orth must have the same dimensions as u
// returns DivisionByZero if v is the zero vector (v·v == 0)
Error op_proj_decompose_u_onto_v(In MatrixView u,
        In MatrixView v,
        Out MatrixMutView out_proj,
        Out MatrixMutView out_orth,
        Out ProjDecomposeResult* out,
        Out Explanation* expl,
        In const ExplainOptions& opts) noexcept;

} // namespace matrix_core
