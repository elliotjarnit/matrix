# Headless desktop build of the calculator shell on PortCE (bundled with
# libtexce), driven by a key script; see sim_driver.cpp.
#
#   cmake -S tools/shell_sim -B build/sim-matlite -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
#         -DCMAKE_C_COMPILER=clang -DSIM_PROGRAM=matlite
#   cmake --build build/sim-matlite
#   tools/shell_sim/scenarios.py matlite > script.txt
#   cd build/sim-matlite && SHELL_SIM_SCRIPT=../../script.txt SHELL_SIM_OUT=out ./shell_sim
#
# Needs clang (PortCE uses _BitInt) and SDL2; SDL2_mixer is not needed.
cmake_minimum_required(VERSION 3.20)
project(matrix_shell_sim C CXX)

set(REPO ${CMAKE_CURRENT_LIST_DIR}/../..)
set(TEX ${REPO}/third_party/libtexce)
set(PORTCE ${TEX}/portce/src)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

# PortCE without its audio backend (only compiled with PortCE_Enable_Audio)
file(GLOB PORTCE_SOURCES ${PORTCE}/*.cpp ${PORTCE}/graphz/*.cpp)
add_library(portce_sim STATIC ${PORTCE_SOURCES})
target_include_directories(portce_sim PUBLIC ${PORTCE}/include)
target_compile_options(portce_sim PRIVATE -w)
target_link_libraries(portce_sim PUBLIC SDL2::SDL2 m)

# which program to run: a directory name under programs/
set(SIM_PROGRAM "matlite" CACHE STRING "Program to build (programs/<name>)")
include(${REPO}/cmake/matrix_program.cmake)
matrix_program_config(${SIM_PROGRAM} SIM)

add_executable(shell_sim
  ${SIM_SOURCES}
  ${TEX}/src/tex/tex_util.c
  ${TEX}/src/tex/tex_pool.c
  ${TEX}/src/tex/tex_symbols.c
  ${TEX}/src/tex/tex_metrics.c
  ${TEX}/src/tex/tex_fonts.c
  ${TEX}/src/tex/tex_token.c
  ${TEX}/src/tex/tex_parse.c
  ${TEX}/src/tex/tex_measure.c
  ${TEX}/src/tex/tex_layout.c
  ${TEX}/src/tex/tex_renderer.c
  ${TEX}/src/tex/tex_draw.c
  ${CMAKE_CURRENT_LIST_DIR}/sim_driver.cpp
)
target_include_directories(shell_sim PRIVATE ${SIM_INCLUDE_DIRS} ${TEX}/src ${TEX}/src/tex ${TEX}/include)
target_compile_definitions(shell_sim PRIVATE ${SIM_DEFINITIONS} TEX_USE_FONTLIB TEX_DIRECT_RENDER MATRIX_SHELL_ENABLE_DEBUG=0)
target_compile_options(shell_sim PRIVATE -fno-exceptions -fno-rtti -Wno-bit-int-extension -Wno-c99-extensions -Wno-c23-extensions)
target_link_libraries(shell_sim PRIVATE portce_sim)
target_link_options(shell_sim PRIVATE -Wl,--wrap=kb_Scan -Wl,--wrap=gfx_SwapDraw -Wl,--wrap=fontlib_GetFontByIndex)

# PortCE looks for appvars in ./appvar, ../appvar, ...
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/appvar)
foreach(font TeXFonts.8xv TeXScrpt.8xv)
  configure_file(${TEX}/assets/${font} ${CMAKE_BINARY_DIR}/appvar/${font} COPYONLY)
endforeach()
