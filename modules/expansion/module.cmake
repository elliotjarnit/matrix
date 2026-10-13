matrix_module(expansion
  DESCRIPTION "det(A) by cofactor expansion across any row or down any column"
  CORE_SOURCES
    core/ops_expansion_ondemand.cpp
  SHELL_SOURCES
    shell/expansion.cpp
  TESTS
    test_expansion
)
