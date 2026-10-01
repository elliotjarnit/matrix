// Page state of module "vectors": a fragment spliced into matrix_shell/app.hpp,
// inside namespace matrix_shell.

struct ProjectionResultState {
		std::uint8_t slot_u;
		std::uint8_t slot_v;
		std::uint8_t mode; // 0=proj, 1=orth
		bool has_steps;

		std::uint8_t rows;
		std::uint8_t cols;
		std::uint8_t stride_proj;
		std::uint8_t stride_orth;
		const matrix_core::Rational* proj_data;
		const matrix_core::Rational* orth_data;

		std::int64_t k_num;
		std::int64_t k_den;
};
