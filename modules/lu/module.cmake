matrix_module(lu
  DESCRIPTION "LU factorization (PA = LU when a row swap is needed) and LU solve"
  CORE_SOURCES
    core/ops_lu_ondemand.cpp
  SHELL_SOURCES
    shell/lu.cpp
  TESTS
    test_lu
)
