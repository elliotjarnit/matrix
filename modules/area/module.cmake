matrix_module(area
  DESCRIPTION "Areas of parallelograms and triangles by determinants, and how T(x) = Ax scales them"
  CORE_SOURCES
    core/ops_area_ondemand.cpp
  SHELL_SOURCES
    shell/area.cpp
  TESTS
    test_area
)
