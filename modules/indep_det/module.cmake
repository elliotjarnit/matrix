matrix_module(indep_det
  DESCRIPTION "Linear independence of n vectors in R^n by det(A) != 0"
  CORE_SOURCES
    core/ops_indep_det_ondemand.cpp
  SHELL_SOURCES
    shell/indep_det.cpp
  TESTS
    test_indep_det
)
