// Page state of module "lu": a fragment spliced into matrix_shell/app.hpp,
// inside namespace matrix_shell.

// the factors share a row count: L and P are rows x rows, U is rows x u_cols.
// p_data is null unless row swaps were used.
struct LuResultState {
		std::uint8_t slot_a;
		std::uint8_t mode; // 0=L, 1=U, 2=P
		bool has_steps;

		std::uint8_t rows;
		std::uint8_t u_cols;
		std::uint8_t stride_l;
		std::uint8_t stride_u;
		std::uint8_t stride_p;
		const matrix_core::Rational* l_data;
		const matrix_core::Rational* u_data;
		const matrix_core::Rational* p_data;
};

// "Row swap needed in col N. Use row swaps (PA = LU)?" Yes reruns the
// factorization (or the solve, when `solve`) with partial pivoting.
struct LuPivotConfirmState {
		std::uint8_t slot_a;
		std::uint8_t slot_b; // the right hand side, for the solve
		std::uint8_t col;    // 0 based column that needs the swap
		bool solve;
};
