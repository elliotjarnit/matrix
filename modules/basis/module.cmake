matrix_module(basis
  DESCRIPTION "Col(A) and Nul(A): bases, rank, dimensions and the rank theorem"
  CORE_SOURCES
    core/ops_basis_ondemand.cpp
  SHELL_SOURCES
    shell/basis.cpp
  TESTS
    test_basis
)
