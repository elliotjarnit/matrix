matrix_module(minor_matrix
  DESCRIPTION "Matrix of minors"
  CORE_SOURCES
    core/ops_minor_matrix.cpp
  SHELL_SOURCES
    shell/minor_matrix.cpp
  TESTS
    test_minor_matrix
  CE_TESTS
    test_minor_matrix_ce:TSTMIN
)
