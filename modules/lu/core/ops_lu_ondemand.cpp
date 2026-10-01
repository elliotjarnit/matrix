#include "matrix_core/ops_lu.hpp"

#include "matrix_core/latex.hpp"
#include "matrix_core/row_ops.hpp"
#include "matrix_core/row_reduction.hpp"
#include "matrix_core/spaces.hpp"
#include "matrix_core/writer.hpp"

#include <new>

namespace matrix_core {
namespace {

// English fallbacks, used when the caller supplies no label. The shell passes
// translated strings through LuLabels.
constexpr const char* kSkipColumnLabel = "no pivot, skip column";
constexpr const char* kCheckLabel = "check";
constexpr const char* kForwardLabel = "forward substitution";
constexpr const char* kBackLabel = "back substitution";
constexpr const char* kInconsistentLabel = "inconsistent: no solution";
constexpr const char* kParticularLabel = "particular solution";
constexpr const char* kHomogeneousLabel = "homogeneous solution";
constexpr const char* kSummaryLabel = "summary";
constexpr const char* kSwapRowsLabel = "swap rows";

// free variable parameter names, in order
constexpr char kParamLetters[] = {'s', 't', 'u', 'v', 'w', 'z'};

constexpr std::uint8_t kNoPivot = 0xFF;

constexpr const char* label_or(const char* s, const char* fallback) noexcept {
		return (s && s[0] != '\0') ? s : fallback;
}

LuLabels resolve_labels(const LuLabels* labels) noexcept {
		const LuLabels in = labels ? *labels : LuLabels{};
		LuLabels out{};
		out.skip_column = label_or(in.skip_column, kSkipColumnLabel);
		out.check = label_or(in.check, kCheckLabel);
		out.forward = label_or(in.forward, kForwardLabel);
		out.back = label_or(in.back, kBackLabel);
		out.inconsistent = label_or(in.inconsistent, kInconsistentLabel);
		out.particular = label_or(in.particular, kParticularLabel);
		out.homogeneous = label_or(in.homogeneous, kHomogeneousLabel);
		out.summary = label_or(in.summary, kSummaryLabel);
		out.swap_rows = label_or(in.swap_rows, kSwapRowsLabel);
		return out;
}

constexpr bool is_one(const Rational& r) noexcept {
		return r.num() == 1 && r.den() == 1;
}

ErrorCode rational_abs(const Rational& r, Rational* out) noexcept {
		if (!out)
				return ErrorCode::Internal;
		if (r.num() >= 0) {
				*out = r;
				return ErrorCode::Ok;
		}
		return rational_neg(r, out);
}

// -----------------------------------------------------------------------------
// Elimination engine
// -----------------------------------------------------------------------------

enum class LuEventKind : std::uint8_t {
		Eliminate,
		Skip, // column with no pivot at or below the pivot row
		Swap, // partial pivoting only
};

struct LuEvent {
		LuEventKind kind = LuEventKind::Eliminate;
		std::uint8_t col = 0;        // 0 based column
		std::uint8_t pivot_row = 0;  // 0 based pivot row r
		std::uint8_t target_row = 0; // row i for Eliminate, the other row for Swap
		Rational mult{};             // L(i, r); the op is R_i <- R_i - mult R_r
};

struct LuObserver {
		std::size_t target = static_cast<std::size_t>(-1); // 1 based event index to stop after
		std::size_t count = 0;
		std::size_t swaps = 0;
		LuEvent last{};

		bool on_event(const LuEvent& e) noexcept {
				count++;
				if (e.kind == LuEventKind::Swap)
						swaps++;
				last = e;
				return count != target;
		}
};

// where a row swap would have been needed
struct LuFailure {
		std::uint8_t row = 0;
		std::uint8_t col = 0;
};

void lu_set_identity(MatrixMutView l) noexcept {
		matrix_fill_zero(l);
		for (std::uint8_t i = 0; i < l.rows; i++)
				l.at_mut(i, i) = Rational::from_int(1);
}

// swaps the multipliers already recorded in L, i.e. only the columns left of
// the pivot; the unit diagonal and the not yet computed entries stay put
void lu_swap_multipliers(MatrixMutView l, std::uint8_t r1, std::uint8_t r2, std::uint8_t cols) noexcept {
		if (r1 == r2)
				return;
		for (std::uint8_t c = 0; c < cols; c++) {
				const Rational tmp = l.at(r1, c);
				l.at_mut(r1, c) = l.at(r2, c);
				l.at_mut(r2, c) = tmp;
		}
}

// Doolittle elimination with row replacement only. `u` starts as a copy of A
// and is reduced in place, `l` starts as the identity and collects multipliers.
// `p` (optional, partial pivoting only) starts as the identity and records the
// row swaps, so that PA = LU.
ErrorCode lu_apply(MatrixMutView u, MatrixMutView l, MatrixMutView p, LuPivoting pivoting, LuObserver* obs, LuFailure* fail) noexcept {
		if (!u.data || !l.data)
				return ErrorCode::Internal;
		if (l.rows != u.rows || l.cols != u.rows)
				return ErrorCode::Internal;
		if (p.data && (p.rows != u.rows || p.cols != u.rows))
				return ErrorCode::Internal;

		const std::uint8_t m = u.rows;
		const std::uint8_t n = u.cols;

		std::uint8_t pivot_row = 0;
		for (std::uint8_t col = 0; col < n && pivot_row < m; col++) {
				if (u.at(pivot_row, col).is_zero()) {
						std::uint8_t swap_row = 0;
						bool found = false;
						for (std::uint8_t row = static_cast<std::uint8_t>(pivot_row + 1u); row < m; row++) {
								if (u.at(row, col).is_zero())
										continue;
								swap_row = row;
								found = true;
								break;
						}

						if (!found) {
								if (obs) {
										LuEvent e;
										e.kind = LuEventKind::Skip;
										e.col = col;
										e.pivot_row = pivot_row;
										if (!obs->on_event(e))
												return ErrorCode::Ok;
								}
								continue;
						}

						if (pivoting == LuPivoting::None) {
								// eliminating here would need a swap: no LU without a permutation
								if (fail) {
										fail->row = pivot_row;
										fail->col = col;
								}
								return ErrorCode::RowSwapRequired;
						}

						apply_swap(u, pivot_row, swap_row);
						lu_swap_multipliers(l, pivot_row, swap_row, pivot_row);
						if (p.data)
								apply_swap(p, pivot_row, swap_row);

						if (obs) {
								LuEvent e;
								e.kind = LuEventKind::Swap;
								e.col = col;
								e.pivot_row = pivot_row;
								e.target_row = swap_row;
								if (!obs->on_event(e))
										return ErrorCode::Ok;
						}
				}

				const Rational pivot = u.at(pivot_row, col);
				for (std::uint8_t row = static_cast<std::uint8_t>(pivot_row + 1u); row < m; row++) {
						if (u.at(row, col).is_zero())
								continue;

						Rational mult;
						ErrorCode ec = rational_div(u.at(row, col), pivot, &mult);
						if (!is_ok(ec))
								return ec;

						Rational factor;
						ec = rational_neg(mult, &factor);
						if (!is_ok(ec))
								return ec;

						ec = apply_addmul(u, row, pivot_row, factor);
						if (!is_ok(ec))
								return ec;

						l.at_mut(row, pivot_row) = mult;

						if (obs) {
								LuEvent e;
								e.kind = LuEventKind::Eliminate;
								e.col = col;
								e.pivot_row = pivot_row;
								e.target_row = row;
								e.mult = mult;
								if (!obs->on_event(e))
										return ErrorCode::Ok;
						}
				}

				pivot_row++;
		}

		return ErrorCode::Ok;
}

struct LuWork {
		MatrixMutView p{}; // only allocated with partial pivoting
		MatrixMutView l{};
		MatrixMutView u{};
};

// allocates P, L and U in `scratch` and replays the factorization of `a`,
// stopping after `target` events (use (size_t)-1 to run to completion)
ErrorCode lu_build(
        MatrixView a, Arena& scratch, LuPivoting pivoting, std::size_t target, LuObserver* obs, LuFailure* fail, LuWork* out) noexcept {
		if (!out)
				return ErrorCode::Internal;

		LuWork w{};
		ErrorCode ec = matrix_clone(scratch, a, &w.u);
		if (!is_ok(ec))
				return ec;
		ec = matrix_alloc(scratch, a.rows, a.rows, &w.l);
		if (!is_ok(ec))
				return ec;
		lu_set_identity(w.l);

		if (pivoting == LuPivoting::Partial) {
				ec = matrix_alloc(scratch, a.rows, a.rows, &w.p);
				if (!is_ok(ec))
						return ec;
				lu_set_identity(w.p);
		}

		LuObserver local;
		LuObserver* use = obs ? obs : &local;
		use->target = target;
		use->count = 0;
		use->swaps = 0;

		ec = lu_apply(w.u, w.l, w.p, pivoting, use, fail);
		if (!is_ok(ec))
				return ec;

		*out = w;
		return ErrorCode::Ok;
}

// -----------------------------------------------------------------------------
// Substitution
// -----------------------------------------------------------------------------

// Ly = rhs for unit lower triangular L (m x m)
ErrorCode lu_forward_substitute(MatrixView l, MatrixView rhs, MatrixMutView y) noexcept {
		if (!l.data || !rhs.data || !y.data)
				return ErrorCode::Internal;
		if (rhs.rows != l.rows || rhs.cols != 1 || y.rows != l.rows || y.cols != 1)
				return ErrorCode::DimensionMismatch;

		for (std::uint8_t i = 0; i < l.rows; i++) {
				Rational acc = rhs.at(i, 0);
				for (std::uint8_t j = 0; j < i; j++) {
						if (l.at(i, j).is_zero())
								continue;
						Rational term;
						ErrorCode ec = rational_mul(l.at(i, j), y.at(j, 0), &term);
						if (!is_ok(ec))
								return ec;
						ec = rational_sub(acc, term, &acc);
						if (!is_ok(ec))
								return ec;
				}
				y.at_mut(i, 0) = acc;
		}
		return ErrorCode::Ok;
}

// true when U is square with a nonzero diagonal, i.e. back substitution applies
bool lu_u_is_triangular_invertible(MatrixView u) noexcept {
		if (u.rows != u.cols)
				return false;
		for (std::uint8_t i = 0; i < u.rows; i++) {
				if (u.at(i, i).is_zero())
						return false;
		}
		return true;
}

// Ux = y for square U with a nonzero diagonal
ErrorCode lu_back_substitute(MatrixView u, MatrixView y, MatrixMutView x) noexcept {
		if (!u.data || !y.data || !x.data)
				return ErrorCode::Internal;
		if (u.rows != u.cols)
				return ErrorCode::NotSquare;
		if (y.rows != u.rows || y.cols != 1 || x.rows != u.cols || x.cols != 1)
				return ErrorCode::DimensionMismatch;

		const std::uint8_t n = u.rows;
		for (std::uint8_t k = 0; k < n; k++) {
				const std::uint8_t i = static_cast<std::uint8_t>(n - 1u - k);
				Rational acc = y.at(i, 0);
				for (std::uint8_t j = static_cast<std::uint8_t>(i + 1u); j < n; j++) {
						if (u.at(i, j).is_zero())
								continue;
						Rational term;
						ErrorCode ec = rational_mul(u.at(i, j), x.at(j, 0), &term);
						if (!is_ok(ec))
								return ec;
						ec = rational_sub(acc, term, &acc);
						if (!is_ok(ec))
								return ec;
				}
				ErrorCode ec = rational_div(acc, u.at(i, i), &acc);
				if (!is_ok(ec))
						return ec;
				x.at_mut(i, 0) = acc;
		}
		return ErrorCode::Ok;
}

// [U | y] as an m x (n+1) matrix allocated in `scratch`
ErrorCode lu_build_augmented(MatrixView u, MatrixView y, Arena& scratch, MatrixMutView* out) noexcept {
		if (!out)
				return ErrorCode::Internal;
		if (static_cast<std::size_t>(u.cols) + 1u > kMaxCols)
				return ErrorCode::InvalidDimension;

		MatrixMutView aug{};
		ErrorCode ec = matrix_alloc(scratch, u.rows, static_cast<std::uint8_t>(u.cols + 1u), &aug);
		if (!is_ok(ec))
				return ec;

		for (std::uint8_t r = 0; r < u.rows; r++) {
				for (std::uint8_t c = 0; c < u.cols; c++)
						aug.at_mut(r, c) = u.at(r, c);
				aug.at_mut(r, u.cols) = y.at(r, 0);
		}

		*out = aug;
		return ErrorCode::Ok;
}

// first all zero row of [U | y] whose augmented entry is nonzero
bool lu_find_inconsistent_row(MatrixView rref, std::uint8_t n, std::uint8_t* out_row) noexcept {
		for (std::uint8_t r = 0; r < rref.rows; r++) {
				bool all_zero = true;
				for (std::uint8_t c = 0; c < n; c++) {
						if (!rref.at(r, c).is_zero()) {
								all_zero = false;
								break;
						}
				}
				if (all_zero && !rref.at(r, n).is_zero()) {
						if (out_row)
								*out_row = r;
						return true;
				}
		}
		return false;
}

// particular solution of a consistent RREF([U | y]): free variables set to zero
void lu_particular_solution(MatrixView rref, std::uint8_t n, const SpaceInfo& info, MatrixMutView x) noexcept {
		matrix_fill_zero(x);
		for (std::uint8_t k = 0; k < info.rank; k++) {
				const std::uint8_t pc = info.pivot_cols[k];
				const std::uint8_t pr = info.pivot_row_for_col[pc];
				x.at_mut(pc, 0) = rref.at(pr, n);
		}
}

// -----------------------------------------------------------------------------
// LaTeX helpers
// -----------------------------------------------------------------------------

ErrorCode append_named_matrix(const char* name, MatrixView m, Writer& w) noexcept {
		ErrorCode ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		ec = w.append(name);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = ");
		if (!is_ok(ec))
				return ec;
		ec = latex::append_matrix(m, latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

// "L = [...]" above "U = [...]"; falls back to U alone when both do not fit
ErrorCode write_lu_state(MatrixView l, MatrixView u, char* out, std::size_t cap) noexcept {
		if (!out || cap == 0)
				return ErrorCode::BufferTooSmall;
		out[0] = '\0';

		Writer w{out, cap, 0};
		ErrorCode ec = append_named_matrix("L", l, w);
		if (is_ok(ec))
				ec = w.put('\n');
		if (is_ok(ec))
				ec = append_named_matrix("U", u, w);
		if (is_ok(ec))
				return ErrorCode::Ok;
		if (ec != ErrorCode::BufferTooSmall)
				return ec;

		// the working matrix is the one the caption talks about, keep that
		out[0] = '\0';
		Writer narrow{out, cap, 0};
		return append_named_matrix("U", u, narrow);
}

// "$$[A] = [L][U]$$", with P in front of A when the rows were permuted
ErrorCode append_factorization(MatrixView p, MatrixView a, MatrixView l, MatrixView u, Writer& w) noexcept {
		ErrorCode ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		if (p.data) {
				ec = latex::append_matrix(p, latex::MatrixBrackets::BMatrix, w);
				if (!is_ok(ec))
						return ec;
		}
		ec = latex::append_matrix(a, latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = ");
		if (!is_ok(ec))
				return ec;
		ec = latex::append_matrix(l, latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		ec = latex::append_matrix(u, latex::MatrixBrackets::BMatrix, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

// "4R_{1}", or "R_{1}" when the coefficient is 1
ErrorCode append_scaled_row(const Rational& magnitude, std::uint8_t row, Writer& w) noexcept {
		if (!is_one(magnitude)) {
				ErrorCode ec = w.append_rational_latex(magnitude);
				if (!is_ok(ec))
						return ec;
		}
		ErrorCode ec = w.append("R_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(row);
		if (!is_ok(ec))
				return ec;
		return w.append("}");
}

// "\ell_{31}", "u_{12}" and friends (every index here is a single digit)
ErrorCode append_subscript_pair(const char* symbol, std::uint8_t row, std::uint8_t col, Writer& w) noexcept {
		ErrorCode ec = w.append(symbol);
		if (!is_ok(ec))
				return ec;
		ec = w.append("_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(row);
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(col);
		if (!is_ok(ec))
				return ec;
		return w.append("}");
}

// "x_{3}"
ErrorCode append_subscript_one(const char* symbol, std::uint8_t index, Writer& w) noexcept {
		ErrorCode ec = w.append(symbol);
		if (!is_ok(ec))
				return ec;
		ec = w.append("_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(index);
		if (!is_ok(ec))
				return ec;
		return w.append("}");
}

// "(-2)" for negatives so products stay readable, "2" otherwise
ErrorCode append_factor(const Rational& r, Writer& w) noexcept {
		const bool wrap = r.num() < 0;
		if (wrap) {
				ErrorCode ec = w.put('(');
				if (!is_ok(ec))
						return ec;
		}
		ErrorCode ec = w.append_rational_latex(r);
		if (!is_ok(ec))
				return ec;
		if (!wrap)
				return ErrorCode::Ok;
		return w.put(')');
}

// " - 2\cdot 4"
ErrorCode append_subtracted_product(const Rational& coef, const Rational& value, Writer& w) noexcept {
		ErrorCode ec = w.append(" - ");
		if (!is_ok(ec))
				return ec;
		ec = append_factor(coef, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append("\\cdot ");
		if (!is_ok(ec))
				return ec;
		return append_factor(value, w);
}

// "R_{3} \leftarrow R_{3} - 4R_{1} \quad (\ell_{31} = 4)", written as an
// addition when the multiplier is negative
ErrorCode lu_event_caption(const LuEvent& e, const LuLabels& labels, char* out, std::size_t cap) noexcept {
		if (!out || cap == 0)
				return ErrorCode::BufferTooSmall;
		out[0] = '\0';

		Writer w{out, cap, 0};
		ErrorCode ec = ErrorCode::Ok;

		if (e.kind == LuEventKind::Skip) {
				ec = w.append("$\\text{col }");
				if (!is_ok(ec))
						return ec;
				ec = w.append_index1(e.col);
				if (!is_ok(ec))
						return ec;
				ec = w.append(",\\;R_{");
				if (!is_ok(ec))
						return ec;
				ec = w.append_index1(e.pivot_row);
				if (!is_ok(ec))
						return ec;
				ec = w.append("}$: ");
				if (!is_ok(ec))
						return ec;
				return w.append(labels.skip_column);
		}

		if (e.kind == LuEventKind::Swap) {
				ec = w.append("$R_{");
				if (!is_ok(ec))
						return ec;
				ec = w.append_index1(e.pivot_row);
				if (!is_ok(ec))
						return ec;
				ec = w.append("} <-> R_{");
				if (!is_ok(ec))
						return ec;
				ec = w.append_index1(e.target_row);
				if (!is_ok(ec))
						return ec;
				ec = w.append("}$: ");
				if (!is_ok(ec))
						return ec;
				return w.append(labels.swap_rows);
		}

		ec = w.append("$R_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(e.target_row);
		if (!is_ok(ec))
				return ec;
		ec = w.append("} \\leftarrow R_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(e.target_row);
		if (!is_ok(ec))
				return ec;
		ec = w.append("} ");
		if (!is_ok(ec))
				return ec;

		Rational magnitude;
		ec = rational_abs(e.mult, &magnitude);
		if (!is_ok(ec))
				return ec;
		ec = w.append((e.mult.num() < 0) ? "+ " : "- ");
		if (!is_ok(ec))
				return ec;
		ec = append_scaled_row(magnitude, e.pivot_row, w);
		if (!is_ok(ec))
				return ec;

		ec = w.append(" \\quad (");
		if (!is_ok(ec))
				return ec;
		ec = append_subscript_pair("\\ell", e.target_row, e.pivot_row, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = ");
		if (!is_ok(ec))
				return ec;
		ec = w.append_rational_latex(e.mult);
		if (!is_ok(ec))
				return ec;
		return w.append(")$");
}

// -----------------------------------------------------------------------------
// Shared factorization steps
// -----------------------------------------------------------------------------

// steps shared by both operations: 0 = A, 1..events = row ops, then [P,] L, U
constexpr std::size_t lu_prefix_step_count(std::size_t event_count, bool has_p) noexcept {
		return event_count + (has_p ? 4u : 3u);
}

ErrorCode lu_prefix_render(MatrixView a,
        const LuLabels& labels,
        std::size_t event_count,
        LuPivoting pivoting,
        std::size_t index,
        const StepRenderBuffers& out) noexcept {
		if (index == 0) {
				Writer w{out.latex, out.latex_cap, 0};
				return append_named_matrix("A", a, w);
		}

		if (index > event_count) {
				LuWork w{};
				ErrorCode ec = lu_build(a, *out.scratch, pivoting, static_cast<std::size_t>(-1), nullptr, nullptr, &w);
				if (!is_ok(ec))
						return ec;

				std::size_t which = index - event_count - 1u;
				Writer writer{out.latex, out.latex_cap, 0};
				if (w.p.data) {
						if (which == 0)
								return append_named_matrix("P", w.p.view(), writer);
						which--;
				}
				return append_named_matrix(which == 0 ? "L" : "U", which == 0 ? w.l.view() : w.u.view(), writer);
		}

		LuObserver obs;
		LuWork w{};
		ErrorCode ec = lu_build(a, *out.scratch, pivoting, index, &obs, nullptr, &w);
		if (!is_ok(ec))
				return ec;
		if (obs.count < index)
				return ErrorCode::StepOutOfRange;

		if (out.caption) {
				ec = lu_event_caption(obs.last, labels, out.caption, out.caption_cap);
				if (!is_ok(ec))
						return ec;
		}

		return write_lu_state(w.l.view(), w.u.view(), out.latex, out.latex_cap);
}

// -----------------------------------------------------------------------------
// op_lu_factor
// -----------------------------------------------------------------------------

struct LuFactorCtx {
		MatrixView input;
		LuLabels labels{};
		std::size_t event_count = 0;
		LuPivoting pivoting = LuPivoting::None;
};

// after the shared prefix: the LU = A check, then the A = LU summary
constexpr std::size_t kLuFactorTailSteps = 2;

std::size_t lu_factor_step_count(const void* vctx) noexcept {
		const auto* ctx = static_cast<const LuFactorCtx*>(vctx);
		return lu_prefix_step_count(ctx->event_count, ctx->pivoting == LuPivoting::Partial) + kLuFactorTailSteps;
}

ErrorCode lu_factor_render_tail(const LuFactorCtx* ctx, std::size_t tail, const StepRenderBuffers& out) noexcept {
		LuWork w{};
		ErrorCode ec = lu_build(ctx->input, *out.scratch, ctx->pivoting, static_cast<std::size_t>(-1), nullptr, nullptr, &w);
		if (!is_ok(ec))
				return ec;
		const bool has_p = (w.p.data != nullptr);

		if (tail == 0) {
				// verification: recompute both sides rather than assert them
				MatrixMutView lu{};
				ec = matrix_alloc(*out.scratch, ctx->input.rows, ctx->input.cols, &lu);
				if (!is_ok(ec))
						return ec;
				ec = matrix_mul(w.l.view(), w.u.view(), lu);
				if (!is_ok(ec))
						return ec;

				if (out.caption) {
						Writer cap{out.caption, out.caption_cap, 0};
						ec = cap.append(has_p ? "$PA = LU$ " : "$LU = A$ ");
						if (!is_ok(ec))
								return ec;
						ec = cap.append(ctx->labels.check);
						if (!is_ok(ec))
								return ec;
				}

				Writer writer{out.latex, out.latex_cap, 0};
				if (!has_p)
						return append_named_matrix("LU", lu.view(), writer);

				MatrixMutView pa{};
				ec = matrix_alloc(*out.scratch, ctx->input.rows, ctx->input.cols, &pa);
				if (!is_ok(ec))
						return ec;
				ec = matrix_mul(w.p.view(), ctx->input, pa);
				if (!is_ok(ec))
						return ec;

				ec = append_named_matrix("PA", pa.view(), writer);
				if (!is_ok(ec))
						return ec;
				ec = writer.put('\n');
				if (!is_ok(ec))
						return ec;
				return append_named_matrix("LU", lu.view(), writer);
		}

		if (out.caption) {
				Writer cap{out.caption, out.caption_cap, 0};
				ec = cap.append(has_p ? "$PA = LU$" : "$A = LU$");
				if (!is_ok(ec))
						return ec;
		}

		Writer writer{out.latex, out.latex_cap, 0};
		ec = append_factorization(has_p ? w.p.view() : MatrixView{}, ctx->input, w.l.view(), w.u.view(), writer);
		if (is_ok(ec))
				return ErrorCode::Ok;
		if (ec != ErrorCode::BufferTooSmall)
				return ec;

		// too wide to show as one product: fall back to the factors stacked
		return write_lu_state(w.l.view(), w.u.view(), out.latex, out.latex_cap);
}

ErrorCode lu_factor_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const LuFactorCtx*>(vctx);
		if (!ctx->input.data)
				return ErrorCode::Internal;
		if (!out.scratch)
				return ErrorCode::Internal;

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		if (index >= lu_factor_step_count(vctx))
				return ErrorCode::StepOutOfRange;

		const std::size_t prefix = lu_prefix_step_count(ctx->event_count, ctx->pivoting == LuPivoting::Partial);
		if (index < prefix)
				return lu_prefix_render(ctx->input, ctx->labels, ctx->event_count, ctx->pivoting, index, out);

		return lu_factor_render_tail(ctx, index - prefix, out);
}

constexpr ExplanationVTable kLuFactorVTable = {
        .step_count = &lu_factor_step_count,
        .render_step = &lu_factor_render_step,
        .destroy = nullptr,
};

// -----------------------------------------------------------------------------
// op_lu_solve
// -----------------------------------------------------------------------------

struct LuSolveCtx {
		MatrixView input_a;
		MatrixView input_b;
		LuLabels labels{};
		std::size_t event_count = 0;
		std::size_t rref_op_count = 0;
		std::uint8_t m = 0;
		std::uint8_t n = 0;
		LuPivoting pivoting = LuPivoting::None;
		bool permuted = false;
		bool unique = false;
		bool consistent = true;
		std::uint8_t bad_row = 0;
};

// Pb (when permuted), one step per component, then the finished y
constexpr std::size_t lu_solve_forward_steps(std::uint8_t m, bool permuted) noexcept {
		return static_cast<std::size_t>(m) + (permuted ? 2u : 1u);
}

// back substitution: one step per component plus the finished x
// row reduction: [U | y], one step per row op, then the answer
std::size_t lu_solve_answer_steps(const LuSolveCtx* ctx) noexcept {
		return ctx->unique ? (static_cast<std::size_t>(ctx->n) + 1u) : (ctx->rref_op_count + 2u);
}

std::size_t lu_solve_step_count(const void* vctx) noexcept {
		const auto* ctx = static_cast<const LuSolveCtx*>(vctx);
		const std::size_t head = lu_prefix_step_count(ctx->event_count, ctx->pivoting == LuPivoting::Partial) +
		                         lu_solve_forward_steps(ctx->m, ctx->permuted);
		// a solvable system also gets an Ax = b check and a closing summary
		return head + lu_solve_answer_steps(ctx) + (ctx->consistent ? 2u : 0u);
}

// P, L, U, the right hand side Pb and y, plus x when back substitution applies
struct LuSolveState {
		LuWork w{};
		MatrixMutView rhs{};
		MatrixMutView y{};
		MatrixMutView x{};
};

ErrorCode lu_solve_build(const LuSolveCtx* ctx, Arena& scratch, bool want_x, LuSolveState* out) noexcept {
		LuSolveState st{};
		ErrorCode ec = lu_build(ctx->input_a, scratch, ctx->pivoting, static_cast<std::size_t>(-1), nullptr, nullptr, &st.w);
		if (!is_ok(ec))
				return ec;

		ec = matrix_alloc(scratch, ctx->m, 1, &st.rhs);
		if (!is_ok(ec))
				return ec;
		if (st.w.p.data) {
				ec = matrix_mul(st.w.p.view(), ctx->input_b, st.rhs);
		} else {
				ec = matrix_copy(ctx->input_b, st.rhs);
		}
		if (!is_ok(ec))
				return ec;

		ec = matrix_alloc(scratch, ctx->m, 1, &st.y);
		if (!is_ok(ec))
				return ec;
		ec = lu_forward_substitute(st.w.l.view(), st.rhs.view(), st.y);
		if (!is_ok(ec))
				return ec;

		if (want_x && ctx->unique) {
				ec = matrix_alloc(scratch, ctx->n, 1, &st.x);
				if (!is_ok(ec))
						return ec;
				ec = lu_back_substitute(st.w.u.view(), st.y.view(), st.x);
				if (!is_ok(ec))
						return ec;
		}

		*out = st;
		return ErrorCode::Ok;
}

// the answer vector for either branch, plus how many free variables it has
ErrorCode lu_solve_answer_vector(
        const LuSolveCtx* ctx, const LuSolveState& st, Arena& scratch, MatrixMutView* out, std::uint8_t* out_nullity) noexcept {
		if (ctx->unique) {
				if (out_nullity)
						*out_nullity = 0;
				*out = st.x;
				return ErrorCode::Ok;
		}

		MatrixMutView aug{};
		ErrorCode ec = lu_build_augmented(st.w.u.view(), st.y.view(), scratch, &aug);
		if (!is_ok(ec))
				return ec;

		OpObserver obs;
		obs.target = static_cast<std::size_t>(-1);
		ec = echelon_apply(aug, EchelonKind::Rref, &obs);
		if (!is_ok(ec))
				return ec;

		SpaceInfo info{};
		ec = space_info_from_rref(aug.view(), ctx->n, &info);
		if (!is_ok(ec))
				return ec;

		MatrixMutView x{};
		ec = matrix_alloc(scratch, ctx->n, 1, &x);
		if (!is_ok(ec))
				return ec;
		lu_particular_solution(aug.view(), ctx->n, info, x);

		if (out_nullity)
				*out_nullity = info.nullity;
		*out = x;
		return ErrorCode::Ok;
}

// "(Pb)_{2}" when the rows were permuted, "b_{2}" otherwise
ErrorCode append_rhs_symbol(bool permuted, std::uint8_t i, Writer& w) noexcept {
		if (!permuted)
				return append_subscript_one("b", i, w);

		ErrorCode ec = w.append("(Pb)_{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_index1(i);
		if (!is_ok(ec))
				return ec;
		return w.append("}");
}

// "$y_{2} = b_{2} - \ell_{21}y_{1}$"
ErrorCode lu_forward_caption(MatrixView l, bool permuted, std::uint8_t i, char* out, std::size_t cap) noexcept {
		if (!out || cap == 0)
				return ErrorCode::BufferTooSmall;
		out[0] = '\0';

		Writer w{out, cap, 0};
		ErrorCode ec = w.put('$');
		if (!is_ok(ec))
				return ec;
		ec = append_subscript_one("y", i, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = ");
		if (!is_ok(ec))
				return ec;
		ec = append_rhs_symbol(permuted, i, w);
		if (!is_ok(ec))
				return ec;

		for (std::uint8_t j = 0; j < i; j++) {
				if (l.at(i, j).is_zero())
						continue;
				ec = w.append(" - ");
				if (!is_ok(ec))
						return ec;
				ec = append_subscript_pair("\\ell", i, j, w);
				if (!is_ok(ec))
						return ec;
				ec = append_subscript_one("y", j, w);
				if (!is_ok(ec))
						return ec;
		}

		return w.put('$');
}

// "$$y_{2} = 10 - 2\cdot 4 = 2$$"
ErrorCode lu_forward_latex(MatrixView l, MatrixView rhs, MatrixView y, std::uint8_t i, char* out, std::size_t cap) noexcept {
		if (!out || cap == 0)
				return ErrorCode::BufferTooSmall;
		out[0] = '\0';

		Writer w{out, cap, 0};
		ErrorCode ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		ec = append_subscript_one("y", i, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = ");
		if (!is_ok(ec))
				return ec;
		ec = w.append_rational_latex(rhs.at(i, 0));
		if (!is_ok(ec))
				return ec;

		bool any_term = false;
		for (std::uint8_t j = 0; j < i; j++) {
				if (l.at(i, j).is_zero())
						continue;
				ec = append_subtracted_product(l.at(i, j), y.at(j, 0), w);
				if (!is_ok(ec))
						return ec;
				any_term = true;
		}

		// with nothing to subtract, y_i is just the right hand side entry
		if (any_term) {
				ec = w.append(" = ");
				if (!is_ok(ec))
						return ec;
				ec = w.append_rational_latex(y.at(i, 0));
				if (!is_ok(ec))
						return ec;
		}
		return w.append("$$");
}

// "$x_{1} = (y_{1} - u_{12}x_{2}) / u_{11}$"
ErrorCode lu_back_caption(MatrixView u, std::uint8_t i, char* out, std::size_t cap) noexcept {
		if (!out || cap == 0)
				return ErrorCode::BufferTooSmall;
		out[0] = '\0';

		Writer w{out, cap, 0};
		ErrorCode ec = w.put('$');
		if (!is_ok(ec))
				return ec;
		ec = append_subscript_one("x", i, w);
		if (!is_ok(ec))
				return ec;

		bool any_term = false;
		for (std::uint8_t j = static_cast<std::uint8_t>(i + 1u); j < u.cols; j++) {
				if (!u.at(i, j).is_zero()) {
						any_term = true;
						break;
				}
		}

		ec = w.append(any_term ? " = (" : " = ");
		if (!is_ok(ec))
				return ec;
		ec = append_subscript_one("y", i, w);
		if (!is_ok(ec))
				return ec;

		for (std::uint8_t j = static_cast<std::uint8_t>(i + 1u); j < u.cols; j++) {
				if (u.at(i, j).is_zero())
						continue;
				ec = w.append(" - ");
				if (!is_ok(ec))
						return ec;
				ec = append_subscript_pair("u", i, j, w);
				if (!is_ok(ec))
						return ec;
				ec = append_subscript_one("x", j, w);
				if (!is_ok(ec))
						return ec;
		}

		ec = w.append(any_term ? ") / " : " / ");
		if (!is_ok(ec))
				return ec;
		ec = append_subscript_pair("u", i, i, w);
		if (!is_ok(ec))
				return ec;
		return w.put('$');
}

// "$$x_{1} = \frac{4 - 1\cdot 1}{2} = 3$$"
ErrorCode lu_back_latex(MatrixView u, MatrixView y, MatrixView x, std::uint8_t i, char* out, std::size_t cap) noexcept {
		if (!out || cap == 0)
				return ErrorCode::BufferTooSmall;
		out[0] = '\0';

		Writer w{out, cap, 0};
		ErrorCode ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		ec = append_subscript_one("x", i, w);
		if (!is_ok(ec))
				return ec;
		ec = w.append(" = \\frac{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_rational_latex(y.at(i, 0));
		if (!is_ok(ec))
				return ec;

		for (std::uint8_t j = static_cast<std::uint8_t>(i + 1u); j < u.cols; j++) {
				if (u.at(i, j).is_zero())
						continue;
				ec = append_subtracted_product(u.at(i, j), x.at(j, 0), w);
				if (!is_ok(ec))
						return ec;
		}

		ec = w.append("}{");
		if (!is_ok(ec))
				return ec;
		ec = w.append_rational_latex(u.at(i, i));
		if (!is_ok(ec))
				return ec;
		ec = w.append("} = ");
		if (!is_ok(ec))
				return ec;
		ec = w.append_rational_latex(x.at(i, 0));
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

// the offending row of RREF([U | y]) on its own, as "[0 0 0 | 5]"
ErrorCode lu_inconsistent_latex(MatrixView rref, std::uint8_t n, std::uint8_t row, char* out, std::size_t cap) noexcept {
		if (!out || cap == 0)
				return ErrorCode::BufferTooSmall;
		out[0] = '\0';

		const Rational* base = &rref.at(row, 0);
		const MatrixView left{1, n, rref.stride, base};
		const MatrixView right{1, 1, rref.stride, base + n};

		Writer w{out, cap, 0};
		ErrorCode ec = w.append("$$");
		if (!is_ok(ec))
				return ec;
		ec = latex::append_augmented_matrix(left, right, w);
		if (!is_ok(ec))
				return ec;
		return w.append("$$");
}

// "x = x_p + s v_1 + t v_2" with each vector on its own line, plus the free
// variable naming
ErrorCode lu_parametric_latex(MatrixView rref, std::uint8_t n, const SpaceInfo& info, Arena& scratch, char* out, std::size_t cap) noexcept {
		if (!out || cap == 0)
				return ErrorCode::BufferTooSmall;
		out[0] = '\0';

		MatrixMutView xp{};
		ErrorCode ec = matrix_alloc(scratch, n, 1, &xp);
		if (!is_ok(ec))
				return ec;
		lu_particular_solution(rref, n, info, xp);

		Writer w{out, cap, 0};
		ec = w.append("$$x = x_{p}");
		if (!is_ok(ec))
				return ec;
		for (std::uint8_t k = 0; k < info.nullity; k++) {
				ec = w.append(" + ");
				if (!is_ok(ec))
						return ec;
				ec = w.put(kParamLetters[k]);
				if (!is_ok(ec))
						return ec;
				ec = append_subscript_one("v", k, w);
				if (!is_ok(ec))
						return ec;
		}
		ec = w.append("$$\n");
		if (!is_ok(ec))
				return ec;

		ec = append_named_matrix("x_{p}", xp.view(), w);
		if (!is_ok(ec))
				return ec;

		if (info.nullity == 0)
				return ErrorCode::Ok;

		MatrixMutView basis{};
		ec = space_null_basis(rref, n, info, scratch, &basis);
		if (!is_ok(ec))
				return ec;

		for (std::uint8_t k = 0; k < info.nullity; k++) {
				ec = w.put('\n');
				if (!is_ok(ec))
						return ec;

				const char name[6] = {'v', '_', '{', static_cast<char>('1' + k), '}', '\0'};
				const MatrixView column{basis.rows, 1, basis.stride, &basis.at(0, k)};
				ec = append_named_matrix(name, column, w);
				if (!is_ok(ec))
						return ec;
		}

		// name the free variables the parameters stand for
		ec = w.append("\n$$");
		if (!is_ok(ec))
				return ec;
		std::uint8_t seen = 0;
		for (std::uint8_t c = 0; c < n && seen < info.nullity; c++) {
				if (info.pivot_row_for_col[c] != kNoPivot)
						continue;
				if (seen != 0) {
						ec = w.append(",\\;");
						if (!is_ok(ec))
								return ec;
				}
				ec = w.put(kParamLetters[seen]);
				if (!is_ok(ec))
						return ec;
				ec = w.append(" = ");
				if (!is_ok(ec))
						return ec;
				ec = append_subscript_one("x", c, w);
				if (!is_ok(ec))
						return ec;
				seen++;
		}
		return w.append("$$");
}

// P, L, U, y and x on one screen; drops the factors when they do not all fit
ErrorCode lu_solve_summary_latex(
        MatrixView p, MatrixView l, MatrixView u, MatrixView y, MatrixView x, const char* x_name, char* out, std::size_t cap) noexcept {
		if (!out || cap == 0)
				return ErrorCode::BufferTooSmall;

		for (int attempt = 0; attempt < 2; attempt++) {
				out[0] = '\0';
				Writer w{out, cap, 0};
				ErrorCode ec = ErrorCode::Ok;

				if (attempt == 0) {
						if (p.data) {
								ec = append_named_matrix("P", p, w);
								if (is_ok(ec))
										ec = w.put('\n');
						}
						if (is_ok(ec))
								ec = append_named_matrix("L", l, w);
						if (is_ok(ec))
								ec = w.put('\n');
						if (is_ok(ec))
								ec = append_named_matrix("U", u, w);
						if (is_ok(ec))
								ec = w.put('\n');
						if (!is_ok(ec)) {
								if (ec != ErrorCode::BufferTooSmall)
										return ec;
								continue;
						}
				}

				ec = append_named_matrix("y", y, w);
				if (is_ok(ec))
						ec = w.put('\n');
				if (is_ok(ec))
						ec = append_named_matrix(x_name, x, w);
				if (is_ok(ec))
						return ErrorCode::Ok;
				if (ec != ErrorCode::BufferTooSmall)
						return ec;
		}

		return ErrorCode::BufferTooSmall;
}

ErrorCode lu_solve_render_check(const LuSolveCtx* ctx, const LuSolveState& st, const StepRenderBuffers& out) noexcept {
		MatrixMutView x{};
		std::uint8_t nullity = 0;
		ErrorCode ec = lu_solve_answer_vector(ctx, st, *out.scratch, &x, &nullity);
		if (!is_ok(ec))
				return ec;

		MatrixMutView product{};
		ec = matrix_alloc(*out.scratch, ctx->m, 1, &product);
		if (!is_ok(ec))
				return ec;
		ec = matrix_mul(ctx->input_a, x.view(), product);
		if (!is_ok(ec))
				return ec;

		// with free variables the check is done on the particular solution
		const bool parametric = (nullity != 0);
		if (out.caption) {
				Writer cap{out.caption, out.caption_cap, 0};
				ec = cap.append(parametric ? "$Ax_{p} = b$ " : "$Ax = b$ ");
				if (!is_ok(ec))
						return ec;
				ec = cap.append(ctx->labels.check);
				if (!is_ok(ec))
						return ec;
		}

		Writer w{out.latex, out.latex_cap, 0};
		ec = append_named_matrix(parametric ? "Ax_{p}" : "Ax", product.view(), w);
		if (!is_ok(ec))
				return ec;
		ec = w.put('\n');
		if (!is_ok(ec))
				return ec;
		return append_named_matrix("b", ctx->input_b, w);
}

ErrorCode lu_solve_render_summary(const LuSolveCtx* ctx, const LuSolveState& st, const StepRenderBuffers& out) noexcept {
		MatrixMutView x{};
		std::uint8_t nullity = 0;
		ErrorCode ec = lu_solve_answer_vector(ctx, st, *out.scratch, &x, &nullity);
		if (!is_ok(ec))
				return ec;

		const bool parametric = (nullity != 0);
		if (out.caption) {
				Writer cap{out.caption, out.caption_cap, 0};
				ec = cap.append(ctx->labels.summary);
				if (!is_ok(ec))
						return ec;
				if (parametric) {
						ec = cap.append(": $x_{p}$ ");
						if (!is_ok(ec))
								return ec;
						ec = cap.append(ctx->labels.particular);
						if (!is_ok(ec))
								return ec;
				}
		}

		return lu_solve_summary_latex(st.w.p.data ? st.w.p.view() : MatrixView{},
		        st.w.l.view(),
		        st.w.u.view(),
		        st.y.view(),
		        x.view(),
		        parametric ? "x_{p}" : "x",
		        out.latex,
		        out.latex_cap);
}

ErrorCode lu_solve_render_answer(const LuSolveCtx* ctx, const LuSolveState& st, std::size_t tail, const StepRenderBuffers& out) noexcept {
		if (ctx->unique) {
				if (tail < ctx->n) {
						const std::uint8_t i = static_cast<std::uint8_t>(ctx->n - 1u - tail);
						if (out.caption) {
								ErrorCode ec = lu_back_caption(st.w.u.view(), i, out.caption, out.caption_cap);
								if (!is_ok(ec))
										return ec;
						}
						return lu_back_latex(st.w.u.view(), st.y.view(), st.x.view(), i, out.latex, out.latex_cap);
				}

				if (out.caption) {
						Writer cap{out.caption, out.caption_cap, 0};
						ErrorCode ec = cap.append(ctx->labels.back);
						if (!is_ok(ec))
								return ec;
				}
				Writer w{out.latex, out.latex_cap, 0};
				return append_named_matrix("x", st.x.view(), w);
		}

		MatrixMutView aug{};
		ErrorCode ec = lu_build_augmented(st.w.u.view(), st.y.view(), *out.scratch, &aug);
		if (!is_ok(ec))
				return ec;

		const MatrixView left{aug.rows, ctx->n, aug.stride, aug.data};
		const MatrixView right{aug.rows, 1, aug.stride, aug.data + ctx->n};

		if (tail == 0)
				return latex::write_augmented_matrix_display(left, right, {out.latex, out.latex_cap});

		OpObserver obs;
		obs.target = (tail <= ctx->rref_op_count) ? tail : static_cast<std::size_t>(-1);
		ec = echelon_apply(aug, EchelonKind::Rref, &obs);
		if (!is_ok(ec))
				return ec;

		if (tail <= ctx->rref_op_count) {
				if (obs.count < tail)
						return ErrorCode::StepOutOfRange;
				if (out.caption) {
						ec = row_op_caption(obs.last_op, out.caption, out.caption_cap);
						if (!is_ok(ec))
								return ec;
				}
				return latex::write_augmented_matrix_display(left, right, {out.latex, out.latex_cap});
		}

		if (!ctx->consistent) {
				if (out.caption) {
						Writer cap{out.caption, out.caption_cap, 0};
						ec = cap.append(ctx->labels.inconsistent);
						if (!is_ok(ec))
								return ec;
				}
				return lu_inconsistent_latex(aug.view(), ctx->n, ctx->bad_row, out.latex, out.latex_cap);
		}

		SpaceInfo info{};
		ec = space_info_from_rref(aug.view(), ctx->n, &info);
		if (!is_ok(ec))
				return ec;

		if (out.caption) {
				Writer cap{out.caption, out.caption_cap, 0};
				ec = cap.append("$x_{p}$: ");
				if (!is_ok(ec))
						return ec;
				ec = cap.append(ctx->labels.particular);
				if (!is_ok(ec))
						return ec;
				if (info.nullity != 0) {
						ec = cap.append(", $v_{i}$: ");
						if (!is_ok(ec))
								return ec;
						ec = cap.append(ctx->labels.homogeneous);
						if (!is_ok(ec))
								return ec;
				}
		}

		return lu_parametric_latex(aug.view(), ctx->n, info, *out.scratch, out.latex, out.latex_cap);
}

ErrorCode lu_solve_render_step(const void* vctx, std::size_t index, const StepRenderBuffers& out) noexcept {
		const auto* ctx = static_cast<const LuSolveCtx*>(vctx);
		if (!ctx->input_a.data || !ctx->input_b.data)
				return ErrorCode::Internal;
		if (!out.scratch)
				return ErrorCode::Internal;

		if (out.caption && out.caption_cap)
				out.caption[0] = '\0';
		if (out.latex && out.latex_cap)
				out.latex[0] = '\0';

		if (index >= lu_solve_step_count(vctx))
				return ErrorCode::StepOutOfRange;

		const std::size_t prefix = lu_prefix_step_count(ctx->event_count, ctx->pivoting == LuPivoting::Partial);
		if (index < prefix)
				return lu_prefix_render(ctx->input_a, ctx->labels, ctx->event_count, ctx->pivoting, index, out);

		std::size_t rest = index - prefix;
		const std::size_t forward_steps = lu_solve_forward_steps(ctx->m, ctx->permuted);

		if (rest < forward_steps) {
				LuSolveState st{};
				ErrorCode ec = lu_solve_build(ctx, *out.scratch, false, &st);
				if (!is_ok(ec))
						return ec;

				if (ctx->permuted) {
						if (rest == 0) {
								if (out.caption) {
										Writer cap{out.caption, out.caption_cap, 0};
										ec = cap.append("$Ly = Pb$");
										if (!is_ok(ec))
												return ec;
								}
								Writer w{out.latex, out.latex_cap, 0};
								return append_named_matrix("Pb", st.rhs.view(), w);
						}
						rest--;
				}

				if (rest < ctx->m) {
						const std::uint8_t i = static_cast<std::uint8_t>(rest);
						if (out.caption) {
								ec = lu_forward_caption(st.w.l.view(), ctx->permuted, i, out.caption, out.caption_cap);
								if (!is_ok(ec))
										return ec;
						}
						return lu_forward_latex(st.w.l.view(), st.rhs.view(), st.y.view(), i, out.latex, out.latex_cap);
				}

				if (out.caption) {
						Writer cap{out.caption, out.caption_cap, 0};
						ec = cap.append(ctx->labels.forward);
						if (!is_ok(ec))
								return ec;
				}
				Writer w{out.latex, out.latex_cap, 0};
				return append_named_matrix("y", st.y.view(), w);
		}

		rest -= forward_steps;

		LuSolveState st{};
		ErrorCode ec = lu_solve_build(ctx, *out.scratch, true, &st);
		if (!is_ok(ec))
				return ec;

		const std::size_t answer_steps = lu_solve_answer_steps(ctx);
		if (rest < answer_steps)
				return lu_solve_render_answer(ctx, st, rest, out);

		return (rest == answer_steps) ? lu_solve_render_check(ctx, st, out) : lu_solve_render_summary(ctx, st, out);
}

constexpr ExplanationVTable kLuSolveVTable = {
        .step_count = &lu_solve_step_count,
        .render_step = &lu_solve_render_step,
        .destroy = nullptr,
};

} // namespace

Error op_lu_factor(MatrixView a,
        LuPivoting pivoting,
        MatrixMutView p_out,
        MatrixMutView l_out,
        MatrixMutView u_out,
        const LuLabels* labels,
        Explanation* expl,
        const ExplainOptions& opts) noexcept {
		Error err;
		if (!a.data)
				return {ErrorCode::Internal};
		if (l_out.rows != a.rows || l_out.cols != a.rows)
				return err_dim_mismatch(a.dim(), l_out.dim());
		if (u_out.rows != a.rows || u_out.cols != a.cols)
				return err_dim_mismatch(a.dim(), u_out.dim());
		if (p_out.data && (p_out.rows != a.rows || p_out.cols != a.rows))
				return err_dim_mismatch(a.dim(), p_out.dim());

		ErrorCode ec = matrix_copy(a, u_out);
		if (!is_ok(ec))
				return {ec};
		lu_set_identity(l_out);

		MatrixMutView p_work{};
		if (pivoting == LuPivoting::Partial && p_out.data) {
				lu_set_identity(p_out);
				p_work = p_out;
		}

		LuObserver obs;
		LuFailure fail{};
		ec = lu_apply(u_out, l_out, p_work, pivoting, &obs, &fail);
		if (!is_ok(ec)) {
				if (ec == ErrorCode::RowSwapRequired)
						return err_row_swap_required(a.dim(), fail.row, fail.col);
				err.code = ec;
				err.a = a.dim();
				return err;
		}

		if (opts.enable) {
				if (!opts.persist || !expl)
						return {ErrorCode::Internal};

				ArenaScope tx(*opts.persist);
				void* mem = opts.persist->allocate(sizeof(LuFactorCtx), alignof(LuFactorCtx));
				if (!mem)
						return err_overflow();
				auto* ctx = new (mem) LuFactorCtx{};
				ctx->input = a;
				ctx->labels = resolve_labels(labels);
				ctx->event_count = obs.count;
				ctx->pivoting = pivoting;
				*expl = Explanation::make(ctx, &kLuFactorVTable);
				tx.commit();
		}

		return err;
}

Error op_lu_solve(MatrixView a,
        MatrixView b,
        LuPivoting pivoting,
        Arena& scratch,
        MatrixMutView y_out,
        MatrixMutView x_out,
        MatrixMutView rref_out,
        LuSolveInfo* info,
        const LuLabels* labels,
        Explanation* expl,
        const ExplainOptions& opts) noexcept {
		Error err;
		if (!a.data || !b.data || !info)
				return {ErrorCode::Internal};
		if (b.rows != a.rows || b.cols != 1)
				return err_dim_mismatch(a.dim(), b.dim());
		if (static_cast<std::size_t>(a.cols) + 1u > kMaxCols)
				return err_invalid_dim(a.dim());
		if (y_out.rows != a.rows || y_out.cols != 1)
				return err_dim_mismatch(a.dim(), y_out.dim());
		if (x_out.rows != a.cols || x_out.cols != 1)
				return err_dim_mismatch(a.dim(), x_out.dim());
		if (rref_out.rows != a.rows || rref_out.cols != static_cast<std::uint8_t>(a.cols + 1u))
				return err_dim_mismatch(a.dim(), rref_out.dim());

		ArenaScratchScope scratch_scope(scratch);

		LuObserver obs;
		LuFailure fail{};
		LuWork w{};
		ErrorCode ec = lu_build(a, scratch, pivoting, static_cast<std::size_t>(-1), &obs, &fail, &w);
		if (!is_ok(ec)) {
				if (ec == ErrorCode::RowSwapRequired)
						return err_row_swap_required(a.dim(), fail.row, fail.col);
				err.code = ec;
				err.a = a.dim();
				return err;
		}

		MatrixView rhs = b;
		if (w.p.data) {
				MatrixMutView pb{};
				ec = matrix_alloc(scratch, a.rows, 1, &pb);
				if (is_ok(ec))
						ec = matrix_mul(w.p.view(), b, pb);
				if (!is_ok(ec)) {
						err.code = ec;
						err.a = a.dim();
						return err;
				}
				rhs = pb.view();
		}

		ec = lu_forward_substitute(w.l.view(), rhs, y_out);
		if (!is_ok(ec)) {
				err.code = ec;
				err.a = a.dim();
				return err;
		}

		MatrixMutView aug{};
		ec = lu_build_augmented(w.u.view(), y_out.view(), scratch, &aug);
		if (!is_ok(ec)) {
				err.code = ec;
				err.a = a.dim();
				return err;
		}

		OpObserver rref_obs;
		rref_obs.target = static_cast<std::size_t>(-1);
		ec = echelon_apply(aug, EchelonKind::Rref, &rref_obs);
		if (is_ok(ec))
				ec = matrix_copy(aug.view(), rref_out);
		if (!is_ok(ec)) {
				err.code = ec;
				err.a = a.dim();
				return err;
		}

		SpaceInfo space{};
		ec = space_info_from_rref(rref_out.view(), a.cols, &space);
		if (!is_ok(ec)) {
				err.code = ec;
				err.a = a.dim();
				return err;
		}

		LuSolveInfo result{};
		result.rank = space.rank;
		result.nullity = space.nullity;
		result.unique = lu_u_is_triangular_invertible(w.u.view());
		result.permuted = (obs.swaps != 0);
		std::uint8_t bad_row = 0;
		result.consistent = !lu_find_inconsistent_row(rref_out.view(), a.cols, &bad_row);
		result.bad_row = bad_row;

		if (result.unique) {
				ec = lu_back_substitute(w.u.view(), y_out.view(), x_out);
				if (!is_ok(ec)) {
						err.code = ec;
						err.a = a.dim();
						return err;
				}
		} else if (result.consistent) {
				lu_particular_solution(rref_out.view(), a.cols, space, x_out);
		} else {
				matrix_fill_zero(x_out);
		}

		*info = result;

		if (opts.enable) {
				if (!opts.persist || !expl)
						return {ErrorCode::Internal};

				ArenaScope tx(*opts.persist);
				void* mem = opts.persist->allocate(sizeof(LuSolveCtx), alignof(LuSolveCtx));
				if (!mem)
						return err_overflow();
				auto* ctx = new (mem) LuSolveCtx{};
				ctx->input_a = a;
				ctx->input_b = b;
				ctx->labels = resolve_labels(labels);
				ctx->event_count = obs.count;
				ctx->rref_op_count = rref_obs.count;
				ctx->m = a.rows;
				ctx->n = a.cols;
				ctx->pivoting = pivoting;
				ctx->permuted = result.permuted;
				ctx->unique = result.unique;
				ctx->consistent = result.consistent;
				ctx->bad_row = result.bad_row;
				*expl = Explanation::make(ctx, &kLuSolveVTable);
				tx.commit();
		}

		return err;
}

} // namespace matrix_core
