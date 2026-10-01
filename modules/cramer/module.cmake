matrix_module(cramer
  DESCRIPTION "Cramer's rule, with the steps of det A and of each A_i"
  CORE_SOURCES
    core/ops_cramer.cpp
    core/ops_det_replace_column_ondemand.cpp
  SHELL_SOURCES
    shell/cramer.cpp
  TESTS
    test_cramer
  CE_TESTS
    test_cramer_ce:TSTCRAM
)
