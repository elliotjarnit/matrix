matrix_module(vectors
  DESCRIPTION "Dot product, cross product, and projection of u onto v"
  CORE_SOURCES
    core/ops_vector_ondemand.cpp
  SHELL_SOURCES
    shell/vectors.cpp
  TESTS
    test_vectors
  CE_TESTS
    test_vectors_ce:TSTVEC
)
