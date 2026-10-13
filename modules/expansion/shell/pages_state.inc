// Page state of module "expansion": a fragment spliced into matrix_shell/app.hpp,
// inside namespace matrix_shell.

// which row or column to expand det(A) along
struct ExpansionPickState {
		std::uint8_t slot;
		std::uint8_t n;     // square size
		std::uint8_t line;  // 0=row, 1=column (matrix_core::ExpansionLine)
		std::uint8_t index; // 0-based
		std::uint8_t focus; // 0=line, 1=index
};
