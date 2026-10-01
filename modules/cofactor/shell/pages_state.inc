// Page state of module "cofactor": a fragment spliced into matrix_shell/app.hpp,
// inside namespace matrix_shell.

struct CofactorElementState {
		std::uint8_t slot;
		std::uint8_t n;     // square size
		std::uint8_t i;     // 0-based
		std::uint8_t j;     // 0-based
		std::uint8_t focus; // 0=i, 1=j
};
