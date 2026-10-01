matrix_module(dependency
  DESCRIPTION "Span, linear independence (with the dependency relations) and onto / one-to-one"
  CORE_SOURCES
    core/ops_dependency_ondemand.cpp
  SHELL_SOURCES
    shell/dependency.cpp
  TESTS
    test_dependency
)
