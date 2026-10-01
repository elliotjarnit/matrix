#include "matrix_shell/detail/app_internal.hpp"
#include "matrix_shell/text.hpp"

#include "matrix_core/writer.hpp"

#include <cassert>
#include <cstdlib>

namespace matrix_shell::detail {

using namespace matrix_shell::text_literals;

[[noreturn]] void fail_fast(const char* msg) noexcept {
		assert(msg);
		SHELL_DBG("[fatal] %s\n", msg);
		assert(false && "matrix_shell fail_fast");
		(void)msg;
		std::abort();
}

[[noreturn]] void fail_fast(const char* func, const char* msg) noexcept {
		assert(func);
		assert(msg);
		SHELL_DBG("[fatal] %s: %s\n", func, msg);
		assert(false && "matrix_shell fail_fast");
		(void)func;
		(void)msg;
		std::abort();
}

void require_impl(bool ok, const char* func, const char* msg) noexcept {
		if (!ok)
				fail_fast(func, msg);
}

std::uint8_t keep_cursor_in_view(std::uint8_t cursor, std::uint8_t scroll, std::uint8_t count, int visible) noexcept {
		if (visible <= 0)
				fail_fast("keep_cursor_in_view: visible must be > 0");

		int scr = static_cast<int>(scroll);
		const int cur = static_cast<int>(cursor);

		if (cur < scr)
				scr = cur;

		const int last_visible = scr + visible - 1;
		if (cur > last_visible)
				scr = cur - (visible - 1);

		int max_scroll = static_cast<int>(count) - visible;
		if (max_scroll < 0)
				max_scroll = 0;
		if (scr < 0)
				scr = 0;
		if (scr > max_scroll)
				scr = max_scroll;

		return static_cast<std::uint8_t>(scr);
}

namespace {
struct OpMeta {
		TextId name = TextId::None;
		bool binary = false;
		bool enabled = false; // compiled into this program
};

// one row per OperationId; an operation whose module the program leaves out
// keeps its slot but has no name, so its strings are not compiled in
constexpr OpMeta kOpMeta[] = {
#define MATRIX_OP_IN(id, key, binary, run, body, steps) {key##_tid, binary, true},
#define MATRIX_OP_OUT(id, key, binary, run, body, steps) {TextId::None, binary, false},
#include "matrix_shell/program/ops.inc"
#undef MATRIX_OP_IN
#undef MATRIX_OP_OUT
};

const OpMeta& op_meta(OperationId op) noexcept {
		const std::size_t idx = static_cast<std::size_t>(op);
		if (idx >= (sizeof(kOpMeta) / sizeof(kOpMeta[0])))
				fail_fast("Unhandled OperationId in op_meta");
		return kOpMeta[idx];
}
} // namespace

bool op_is_binary(OperationId op) noexcept {
		return op_meta(op).binary;
}

bool op_enabled(OperationId op) noexcept {
		return op_meta(op).enabled;
}

const char* op_name(OperationId op) noexcept {
		const OpMeta& meta = op_meta(op);
		if (!meta.enabled)
				fail_fast("op_name: operation not in this program");
		return tr(meta.name);
}

// "1 3": the 1 based columns whose bit is set in `mask`, or "-" when none is
void render_pivot_cols(std::uint32_t mask, char* buf, std::size_t cap) noexcept {
		if (!buf || cap == 0)
				return;
		buf[0] = '\0';
		matrix_core::CheckedWriter w{buf, cap};
		bool first = true;
		for (std::uint8_t i = 0; i < 7; i++) {
				if ((mask & (1u << i)) == 0)
						continue;
				if (!first)
						w.put(' ');
				w.append_u64(static_cast<std::uint64_t>(i + 1u));
				first = false;
		}
		if (first)
				w.append("-");
}

void dbg_print_i64(std::int64_t v) noexcept {
#if MATRIX_SHELL_ENABLE_DEBUG
		static char buf[32];
		char* p = buf + sizeof(buf) - 1;
		*p = '\0';

		const bool neg = v < 0;
		std::uint64_t mag = neg ? (static_cast<std::uint64_t>(-(v + 1)) + 1u) : static_cast<std::uint64_t>(v);
		do {
				*--p = static_cast<char>('0' + (mag % 10u));
				mag /= 10u;
		} while (mag != 0u);
		if (neg)
				*--p = '-';
		dbg_printf("%s", p);
#else
		(void)v;
#endif
}

void dbg_print_rational(matrix_core::Rational r) noexcept {
#if MATRIX_SHELL_ENABLE_DEBUG
		dbg_print_i64(r.num());
		dbg_printf("/");
		dbg_print_i64(r.den());
#else
		(void)r;
#endif
}

void dbg_print_matrix(const char* tag, matrix_core::MatrixView m) noexcept {
#if MATRIX_SHELL_ENABLE_DEBUG
		if (!tag)
				tag = "(null)";
		SHELL_DBG("[%s] %ux%u stride=%u data=%p\n", tag, m.rows, m.cols, m.stride, (const void*)m.data);
		if (!m.data)
				return;

		for (std::uint8_t r = 0; r < m.rows; r++) {
				for (std::uint8_t c = 0; c < m.cols; c++) {
						const matrix_core::Rational& v = m.at(r, c);
						dbg_printf("  (%u,%u)=", (unsigned)r, (unsigned)c);
						dbg_print_i64(v.num());
						dbg_printf("/");
						dbg_print_i64(v.den());
						dbg_printf("\n");
				}
		}
#else
		(void)tag;
		(void)m;
#endif
}

} // namespace matrix_shell::detail
