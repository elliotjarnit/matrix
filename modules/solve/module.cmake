matrix_module(solve
  DESCRIPTION "Complete solution of Ax = b (parametric vector form) by RREF"
  CORE_SOURCES
    core/ops_solve_parametric_ondemand.cpp
  SHELL_SOURCES
    shell/solve.cpp
  TESTS
    test_solve_parametric
)
