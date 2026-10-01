matrix_module(cofactor
  DESCRIPTION "A single cofactor C_ij, with the row and column picked on screen"
  CORE_SOURCES
    core/ops_cofactor_element_ondemand.cpp
  SHELL_SOURCES
    shell/cofactor.cpp
  TESTS
    test_cofactor_element
  CE_TESTS
    test_cofactor_element_ce:TSTCOF
)
