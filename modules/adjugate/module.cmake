matrix_module(adjugate
  DESCRIPTION "adj(A): the transpose of the matrix of cofactors"
  CORE_SOURCES
    core/ops_adjugate_ondemand.cpp
  SHELL_SOURCES
    shell/adjugate.cpp
  TESTS
    test_adjugate
)
