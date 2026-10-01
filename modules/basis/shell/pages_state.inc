// Page state of module "basis": a fragment spliced into matrix_shell/app.hpp,
// inside namespace matrix_shell.

// Col(A) / Nul(A). Both bases live in the persist tail; a zero subspace is
// stored as a single zero column, so `rank` / `nullity` (not the column counts)
// say how many basis vectors there are.
struct BasisResultState {
		std::uint8_t slot_a;
		std::uint8_t mode; // 0=summary, 1=Col(A) basis, 2=Nul(A) basis
		bool has_steps;

		std::uint8_t rows; // m
		std::uint8_t cols; // n
		std::uint8_t rank;
		std::uint8_t nullity;
		std::uint8_t pivot_mask;

		std::uint8_t stride_col;
		std::uint8_t stride_nul;
		const matrix_core::Rational* col_data; // m x max(rank, 1)
		const matrix_core::Rational* nul_data; // n x max(nullity, 1)
};
