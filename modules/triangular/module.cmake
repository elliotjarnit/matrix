matrix_module(triangular
  DESCRIPTION "det(A) of a triangular matrix: the product of its diagonal"
  CORE_SOURCES
    core/ops_triangular_ondemand.cpp
  SHELL_SOURCES
    shell/triangular.cpp
  TESTS
    test_triangular
)
