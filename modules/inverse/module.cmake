matrix_module(inverse
  DESCRIPTION "Inverse via Gauss-Jordan on [A | I]"
  CORE_SOURCES
    core/ops_inverse_ondemand.cpp
  SHELL_SOURCES
    shell/inverse.cpp
  TESTS
    test_inverse
  CE_TESTS
    test_inverse_ce:TSTINV
)
