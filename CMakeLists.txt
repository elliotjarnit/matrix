cmake_minimum_required(VERSION 3.20)

project(matrix_app_core C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MATRIX_BUILD_CE "Build for TI-84 Plus CE via CEdevToolchain.cmake" OFF)
option(MATRIX_ENABLE_TESTS "Build native tests" ON)
option(MATRIX_STRICT_WARNINGS "Enable -Werror and extra warnings" ON)
option(MATRIX_BUILD_CE_TESTS "Build core unit tests as CE programs when MATRIX_BUILD_CE=ON" OFF)
option(MATRIX_SHELL_DEBUG "Enable shell debug logging" OFF)

# Calculator programs to build (programs/<name>/program.cmake); empty for all.
set(MATRIX_CE_PROGRAMS "" CACHE STRING "Programs to build for the CE (semicolon list, empty = all)")
# Languages to build each of them in: en, fr.
set(MATRIX_CE_LANGS "en" CACHE STRING "Languages to build CE programs in (semicolon list of en, fr)")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/matrix_program.cmake)
matrix_load_definitions()

get_property(MATRIX_ALL_PROGRAMS GLOBAL PROPERTY MATRIX_PROGRAMS)
get_property(MATRIX_ALL_MODULES GLOBAL PROPERTY MATRIX_MODULES)
message(STATUS "Matrix modules: ${MATRIX_ALL_MODULES}")
message(STATUS "Matrix programs: ${MATRIX_ALL_PROGRAMS}")

set(MATRIX_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/core")

if(MATRIX_SHELL_DEBUG)
  set(MATRIX_SHELL_DEBUG_DEFINITION MATRIX_SHELL_ENABLE_DEBUG=1)
else()
  set(MATRIX_SHELL_DEBUG_DEFINITION MATRIX_SHELL_ENABLE_DEBUG=0)
endif()

# Every module's core, for the native tests and the CE core test programs.
matrix_all_modules_core(MATRIX_ALL_CORE)

if(MATRIX_BUILD_CE)
  set(MATRIX_LIBTEXCE_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/libtexce")

  # CE toolchain integration (kept in-repo to avoid patching submodules).
  set(CEDEV_DEBUG_CONSOLE OFF CACHE BOOL "Disable debug console for size")
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/CEdevToolchain.cmake)

  set(MATRIX_TEX_SOURCES
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_util.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_pool.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_symbols.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_metrics.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_fonts.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_token.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_parse.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_measure.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_layout.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_renderer.c
    ${MATRIX_LIBTEXCE_DIR}/src/tex/tex_draw.c
  )

  set(MATRIX_TEX_INCLUDE_DIRS
    ${MATRIX_LIBTEXCE_DIR}/src
    ${MATRIX_LIBTEXCE_DIR}/src/tex
    ${MATRIX_LIBTEXCE_DIR}/include
  )

  set(programs ${MATRIX_CE_PROGRAMS})
  if(NOT programs)
    set(programs ${MATRIX_ALL_PROGRAMS})
  endif()

  # one calculator program per (program, language): target <program>[_fr],
  # output build/<preset>/<target>/bin/<TI name>.8xp
  foreach(program IN LISTS programs)
    matrix_program_config(${program} P)
    get_property(ti_name GLOBAL PROPERTY MATRIX_PROGRAM_${program}_TI_NAME)
    get_property(ti_name_fr GLOBAL PROPERTY MATRIX_PROGRAM_${program}_TI_NAME_FR)
    get_property(description GLOBAL PROPERTY MATRIX_PROGRAM_${program}_DESCRIPTION)
    list(TRANSFORM P_DEFINITIONS PREPEND "-D" OUTPUT_VARIABLE program_defs)

    foreach(lang IN LISTS MATRIX_CE_LANGS)
      if(lang STREQUAL "en")
        set(target ${program})
        set(name ${ti_name})
        set(desc "${description} (English)")
        set(lang_fr 0)
      elseif(lang STREQUAL "fr")
        if(NOT ti_name_fr)
          message(FATAL_ERROR "program '${program}' has no TI_NAME_FR")
        endif()
        set(target ${program}_fr)
        set(name ${ti_name_fr})
        set(desc "${description} (Francaise)")
        set(lang_fr 1)
      else()
        message(FATAL_ERROR "unknown language '${lang}' in MATRIX_CE_LANGS (use en, fr)")
      endif()

      cedev_add_program(
        TARGET ${target}
        NAME ${name}
        DESCRIPTION ${desc}
        MODE auto

        SOURCES
          ${P_SOURCES}
          ${MATRIX_TEX_SOURCES}

        INCLUDE_DIRECTORIES
          ${P_INCLUDE_DIRS}
          ${MATRIX_TEX_INCLUDE_DIRS}

        LIBLOAD
          graphx
          keypadc
          fontlibc

        COMPILE_OPTIONS
          -DTEX_USE_FONTLIB
          -DTEX_DIRECT_RENDER
          -Wframe-larger-than=127
          -Wno-error=frame-larger-than
          -fstack-usage
          ${program_defs}
          -D${MATRIX_SHELL_DEBUG_DEFINITION}
          -DMATRIX_SHELL_LANG_FR=${lang_fr}
      )
    endforeach()
  endforeach()

  if(MATRIX_BUILD_CE_TESTS)
    list(TRANSFORM MATRIX_ALL_CORE_DEFINITIONS PREPEND "-D" OUTPUT_VARIABLE core_defs)
    function(matrix_add_ce_core_test target name source)
      cedev_add_program(
        TARGET ${target}
        NAME ${name}
        DESCRIPTION "Matrix core test (${name})"
        MODE auto

        SOURCES
          ${MATRIX_ALL_CORE_SOURCES}
          ${MATRIX_CORE_DIR}/tests_ce/ce_test_runtime.cpp
          ${source}

        INCLUDE_DIRECTORIES
          ${MATRIX_ALL_CORE_INCLUDE_DIRS}
          ${MATRIX_CORE_DIR}/tests

        COMPILE_OPTIONS
          -UNDEBUG
          -DDEBUG=1
          -DMATRIX_CE_TESTS=1
          -Wframe-larger-than=127
          -Wno-error=frame-larger-than
          -fstack-usage
          ${core_defs}
      )
    endfunction()

    matrix_add_ce_core_test(ce_test_rational TSTRAT ${MATRIX_CORE_DIR}/tests_ce/test_rational_ce.cpp)
    matrix_add_ce_core_test(ce_test_matrix_ops TSTMOPS ${MATRIX_CORE_DIR}/tests_ce/test_matrix_ops_ce.cpp)
    matrix_add_ce_core_test(ce_test_rref TSTRREF ${MATRIX_CORE_DIR}/tests_ce/test_rref_ce.cpp)
    matrix_add_ce_core_test(ce_test_det TSTDET ${MATRIX_CORE_DIR}/tests_ce/test_det_ce.cpp)
    matrix_add_ce_core_test(ce_test_spaces TSTSPC ${MATRIX_CORE_DIR}/tests_ce/test_spaces_ce.cpp)
    foreach(module IN LISTS MATRIX_ALL_MODULES)
      get_property(dir GLOBAL PROPERTY MATRIX_MODULE_${module}_DIR)
      get_property(ce_tests GLOBAL PROPERTY MATRIX_MODULE_${module}_CE_TESTS)
      foreach(entry IN LISTS ce_tests)
        string(REPLACE ":" ";" parts ${entry})
        list(GET parts 0 stem)
        list(GET parts 1 name)
        matrix_add_ce_core_test(ce_${stem} ${name} ${dir}/tests/${stem}.cpp)
      endforeach()
    endforeach()
  endif()

  message(STATUS "Matrix CE build: ${programs} (${MATRIX_CE_LANGS})")
  return()
endif()

# native library + tests
function(matrix_core_apply target_name)
  target_include_directories(${target_name} PRIVATE ${MATRIX_ALL_CORE_INCLUDE_DIRS} ${MATRIX_CORE_DIR}/tests)
  target_compile_features(${target_name} PRIVATE cxx_std_20)

  target_compile_definitions(${target_name} PRIVATE ${MATRIX_ALL_CORE_DEFINITIONS})

  target_compile_options(${target_name} PRIVATE
    -fno-exceptions
    -fno-rtti
  )

  if(MATRIX_STRICT_WARNINGS)
    target_compile_options(${target_name} PRIVATE
      -Wall -Wextra -Wpedantic -Werror
      -Wconversion -Wsign-conversion
      -Wshadow -Wdouble-promotion
      -Wformat=2 -Wformat-security
      -Wcast-qual -Wpointer-arith
      -Wswitch-enum -Wundef
      -Wnull-dereference -Wfloat-equal
      -Wredundant-decls -Wunreachable-code
      -Wimplicit-fallthrough
    )
  endif()
endfunction()

# Every module's core compiled once and shared by the test executables.
add_library(matrix_core_all STATIC ${MATRIX_ALL_CORE_SOURCES})
matrix_core_apply(matrix_core_all)

# Optional profiling helper; the source is not checked in, so only build it
# when present rather than failing configure for everyone.
if(EXISTS ${MATRIX_CORE_DIR}/profile/profile_cramer.cpp)
  add_executable(matrix_profile_cramer ${MATRIX_CORE_DIR}/profile/profile_cramer.cpp)
  matrix_core_apply(matrix_profile_cramer)
  target_link_libraries(matrix_profile_cramer PRIVATE matrix_core_all)
  target_compile_options(matrix_profile_cramer PRIVATE -fstack-usage)
endif()

if(MATRIX_ENABLE_TESTS)
  enable_testing()

  # tests/<stem>.cpp -> executable <stem>, ctest name <stem> without "test_"
  function(matrix_add_native_test stem source)
    add_executable(${stem} ${source})
    matrix_core_apply(${stem})
    target_link_libraries(${stem} PRIVATE matrix_core_all)
    string(REGEX REPLACE "^test_" "" name ${stem})
    add_test(NAME ${name} COMMAND ${stem})
  endfunction()

  # the engine
  foreach(stem IN ITEMS test_rational test_matrix_ops test_rref test_det test_spaces test_arena_guards test_arena_budget)
    matrix_add_native_test(${stem} ${MATRIX_CORE_DIR}/tests/${stem}.cpp)
  endforeach()

  # every module's own tests
  foreach(module IN LISTS MATRIX_ALL_MODULES)
    get_property(dir GLOBAL PROPERTY MATRIX_MODULE_${module}_DIR)
    get_property(tests GLOBAL PROPERTY MATRIX_MODULE_${module}_TESTS)
    foreach(stem IN LISTS tests)
      matrix_add_native_test(${stem} ${dir}/tests/${stem}.cpp)
    endforeach()
  endforeach()
endif()
