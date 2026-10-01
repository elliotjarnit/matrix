# Script mode: generate one program's headers without configuring a build.
#
#   cmake -DPROGRAM=matrix -DOUT=build/clangd -P cmake/matrix_generate_program.cmake
#
# Writes OUT/programs/<PROGRAM>/include/matrix_shell/program/*.inc and
# OUT/<PROGRAM>.json ({"include_dirs": [...], "definitions": [...]}), which
# tools/setup_clangd.py turns into compile commands for the shell code.
if(NOT PROGRAM OR NOT OUT)
  message(FATAL_ERROR "usage: cmake -DPROGRAM=<name> -DOUT=<dir> -P matrix_generate_program.cmake")
endif()
get_filename_component(OUT ${OUT} ABSOLUTE)
set(CMAKE_BINARY_DIR ${OUT})
include(${CMAKE_CURRENT_LIST_DIR}/matrix_program.cmake)
matrix_program_config(${PROGRAM} P)

set(json "{\n  \"include_dirs\": [")
set(sep "")
foreach(d IN LISTS P_INCLUDE_DIRS)
  string(APPEND json "${sep}\n    \"${d}\"")
  set(sep ",")
endforeach()
string(APPEND json "\n  ],\n  \"definitions\": [")
set(sep "")
foreach(d IN LISTS P_DEFINITIONS)
  string(APPEND json "${sep}\n    \"${d}\"")
  set(sep ",")
endforeach()
string(APPEND json "\n  ]\n}\n")
file(WRITE ${OUT}/${PROGRAM}.json "${json}")
