matrix_module(colrow
  DESCRIPTION "A*B as the column row (outer product) expansion"
  CORE_SOURCES
    core/ops_mul_colrow_ondemand.cpp
  SHELL_SOURCES
    shell/colrow.cpp
  TESTS
    test_mul_colrow
)
