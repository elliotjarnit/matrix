# Modules and programs.
#
# A module (modules/<name>/module.cmake) is one feature: its core operation,
# its shell screens, the operations it adds to the menus, its strings and its
# tests. A program (programs/<name>/program.cmake) is a calculator program
# made of a list of modules. The menu tree is shared by every program; an
# operation whose module a program leaves out is not compiled in and drops out
# of the menus, along with any submenu left empty.
#
# Module layout (every part is optional):
#
#   module.cmake                     matrix_module(...) call, see below
#   ops.def                          MATRIX_OP(...) rows: the operations
#   text.inc                         MATRIX_SHELL_TEXT_ENTRY(...) rows
#   include/matrix_core/ops_<m>.hpp  core API
#   core/*.cpp                       core implementation (no shell, no heap)
#   shell/*.cpp                      App member functions: run_*, render_*, ...
#   shell/app_members.inc            declarations of those, spliced into App
#   shell/pages.def                  MATRIX_PAGE(...) rows: the pages it adds
#   shell/pages_state.inc            their state structs, spliced into app.hpp
#   tests/test_*.cpp                 native tests (and *_ce.cpp CE wrappers)
#
# Global order matters: modules/modules.cmake includes every module in the
# order their operations appear in OperationId.

include_guard(GLOBAL)

get_filename_component(MATRIX_REPO_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

# Shared math engine: everything core that more than one module builds on.
set(MATRIX_ENGINE_SOURCES
  ${MATRIX_REPO_DIR}/core/src/det_detail.cpp
  ${MATRIX_REPO_DIR}/core/src/explanation.cpp
  ${MATRIX_REPO_DIR}/core/src/latex_view.cpp
  ${MATRIX_REPO_DIR}/core/src/matrix_view.cpp
  ${MATRIX_REPO_DIR}/core/src/minor_detail.cpp
  ${MATRIX_REPO_DIR}/core/src/ops_basic_view.cpp
  ${MATRIX_REPO_DIR}/core/src/ops_det_ondemand.cpp
  ${MATRIX_REPO_DIR}/core/src/ops_rref_ondemand.cpp
  ${MATRIX_REPO_DIR}/core/src/rational.cpp
  ${MATRIX_REPO_DIR}/core/src/row_reduction.cpp
  ${MATRIX_REPO_DIR}/core/src/row_ops.cpp
  ${MATRIX_REPO_DIR}/core/src/spaces.cpp
  ${MATRIX_REPO_DIR}/core/src/step_detail.cpp
)

# Shared calculator UI: app, slot editor, menus, slot picker, result and steps.
set(MATRIX_SHELL_COMMON_SOURCES
  ${MATRIX_REPO_DIR}/shell/src/app.cpp
  ${MATRIX_REPO_DIR}/shell/src/app_internal.cpp
  ${MATRIX_REPO_DIR}/shell/src/input.cpp
  ${MATRIX_REPO_DIR}/shell/src/main_ce.cpp
  ${MATRIX_REPO_DIR}/shell/src/page.cpp
  ${MATRIX_REPO_DIR}/shell/src/text.cpp
  ${MATRIX_REPO_DIR}/shell/src/pages/confirm.cpp
  ${MATRIX_REPO_DIR}/shell/src/pages/dim.cpp
  ${MATRIX_REPO_DIR}/shell/src/pages/editor.cpp
  ${MATRIX_REPO_DIR}/shell/src/pages/menu.cpp
  ${MATRIX_REPO_DIR}/shell/src/pages/result.cpp
  ${MATRIX_REPO_DIR}/shell/src/pages/slot_pick.cpp
  ${MATRIX_REPO_DIR}/shell/src/pages/steps.cpp
)

# matrix_module(<name>
#   DESCRIPTION <text>
#   [CORE_SOURCES files...]   relative to the module directory
#   [SHELL_SOURCES files...]
#   [TESTS stems...]          tests/<stem>.cpp, registered as ctest "<stem minus test_>"
#   [CE_TESTS stem:NAME...])  tests/<stem>.cpp built as CE program NAME
function(matrix_module name)
  cmake_parse_arguments(M "" "DESCRIPTION" "CORE_SOURCES;SHELL_SOURCES;TESTS;CE_TESTS" ${ARGN})
  set(dir ${CMAKE_CURRENT_LIST_DIR})
  get_property(known GLOBAL PROPERTY MATRIX_MODULES)
  if(name IN_LIST known)
    message(FATAL_ERROR "module '${name}' is defined twice")
  endif()
  set_property(GLOBAL APPEND PROPERTY MATRIX_MODULES ${name})
  set_property(GLOBAL PROPERTY MATRIX_MODULE_${name}_DIR ${dir})
  set_property(GLOBAL PROPERTY MATRIX_MODULE_${name}_DESCRIPTION "${M_DESCRIPTION}")
  foreach(kind CORE_SOURCES SHELL_SOURCES)
    set(files)
    foreach(f IN LISTS M_${kind})
      if(NOT EXISTS ${dir}/${f})
        message(FATAL_ERROR "module '${name}': ${f} does not exist")
      endif()
      list(APPEND files ${dir}/${f})
    endforeach()
    set_property(GLOBAL PROPERTY MATRIX_MODULE_${name}_${kind} "${files}")
  endforeach()
  set_property(GLOBAL PROPERTY MATRIX_MODULE_${name}_TESTS "${M_TESTS}")
  set_property(GLOBAL PROPERTY MATRIX_MODULE_${name}_CE_TESTS "${M_CE_TESTS}")
endfunction()

# matrix_program(<name>
#   TI_NAME <NAME>            8 characters at most: the calculator variable
#   [TI_NAME_FR <NAME>]       name of the French build
#   DESCRIPTION <text>
#   MODULES modules...)
function(matrix_program name)
  cmake_parse_arguments(P "" "TI_NAME;TI_NAME_FR;DESCRIPTION" "MODULES" ${ARGN})
  get_property(modules GLOBAL PROPERTY MATRIX_MODULES)
  foreach(m IN LISTS P_MODULES)
    if(NOT m IN_LIST modules)
      message(FATAL_ERROR "program '${name}': unknown module '${m}' (known: ${modules})")
    endif()
  endforeach()
  foreach(n IN ITEMS ${P_TI_NAME} ${P_TI_NAME_FR})
    string(LENGTH "${n}" len)
    if(len GREATER 8)
      message(FATAL_ERROR "program '${name}': TI name '${n}' is longer than 8 characters")
    endif()
  endforeach()
  if(NOT P_TI_NAME)
    message(FATAL_ERROR "program '${name}': TI_NAME is required")
  endif()
  set_property(GLOBAL APPEND PROPERTY MATRIX_PROGRAMS ${name})
  set_property(GLOBAL PROPERTY MATRIX_PROGRAM_${name}_TI_NAME ${P_TI_NAME})
  set_property(GLOBAL PROPERTY MATRIX_PROGRAM_${name}_TI_NAME_FR "${P_TI_NAME_FR}")
  set_property(GLOBAL PROPERTY MATRIX_PROGRAM_${name}_DESCRIPTION "${P_DESCRIPTION}")
  set_property(GLOBAL PROPERTY MATRIX_PROGRAM_${name}_MODULES "${P_MODULES}")
endfunction()

# matrix_program_set(<name>
#   TI_NAME <NAME>
#   [TI_NAME_FR <NAME>]
#   DESCRIPTION <text>
#   PROGRAMS programs...)
#
# One calculator program with every feature of the programs it names: its
# modules are the union of theirs, so their operations all sit together in the
# shared menu tree. Defined in programs/sets/<name>.cmake, which are read after
# every programs/*/program.cmake; otherwise it is an ordinary program (target
# <name>, built with the rest, selectable with MATRIX_CE_PROGRAMS).
function(matrix_program_set name)
  cmake_parse_arguments(S "" "TI_NAME;TI_NAME_FR;DESCRIPTION" "PROGRAMS" ${ARGN})
  get_property(programs GLOBAL PROPERTY MATRIX_PROGRAMS)
  if(NOT S_PROGRAMS)
    message(FATAL_ERROR "program set '${name}': PROGRAMS is required")
  endif()
  set(modules)
  foreach(p IN LISTS S_PROGRAMS)
    if(NOT p IN_LIST programs)
      message(FATAL_ERROR "program set '${name}': unknown program '${p}' (known: ${programs})")
    endif()
    get_property(program_modules GLOBAL PROPERTY MATRIX_PROGRAM_${p}_MODULES)
    list(APPEND modules ${program_modules})
  endforeach()
  list(REMOVE_DUPLICATES modules)

  set(fr)
  if(S_TI_NAME_FR)
    set(fr TI_NAME_FR ${S_TI_NAME_FR})
  endif()
  matrix_program(${name} TI_NAME ${S_TI_NAME} ${fr} DESCRIPTION "${S_DESCRIPTION}" MODULES ${modules})
  set_property(GLOBAL APPEND PROPERTY MATRIX_PROGRAM_SETS ${name})
  set_property(GLOBAL PROPERTY MATRIX_PROGRAM_SET_${name}_PROGRAMS "${S_PROGRAMS}")
endfunction()

# Registers every module, program and program set in the repository (once).
macro(matrix_load_definitions)
  get_property(_matrix_loaded GLOBAL PROPERTY MATRIX_DEFINITIONS_LOADED)
  if(NOT _matrix_loaded)
    set_property(GLOBAL PROPERTY MATRIX_DEFINITIONS_LOADED TRUE)
    include(${MATRIX_REPO_DIR}/modules/modules.cmake)
    file(GLOB _matrix_program_files ${MATRIX_REPO_DIR}/programs/*/program.cmake)
    list(SORT _matrix_program_files)
    foreach(_f IN LISTS _matrix_program_files)
      include(${_f})
    endforeach()
    file(GLOB _matrix_set_files ${MATRIX_REPO_DIR}/programs/sets/*.cmake)
    list(SORT _matrix_set_files)
    foreach(_f IN LISTS _matrix_set_files)
      include(${_f})
    endforeach()
  endif()
endmacro()

# Writes `content` to `path` only when it changed, so regenerating does not
# rebuild everything.
function(_matrix_write_if_changed path content)
  set(tmp ${path}.tmp)
  file(WRITE ${tmp} "${content}")
  configure_file(${tmp} ${path} COPYONLY)
  file(REMOVE ${tmp})
endfunction()

# MATRIX_MODULE_<NAME>=1/0 for every module.
function(_matrix_module_definitions out_var included)
  get_property(modules GLOBAL PROPERTY MATRIX_MODULES)
  set(defs)
  foreach(m IN LISTS modules)
    string(TOUPPER ${m} up)
    if(m IN_LIST included)
      list(APPEND defs MATRIX_MODULE_${up}=1)
    else()
      list(APPEND defs MATRIX_MODULE_${up}=0)
    endif()
  endforeach()
  set(${out_var} "${defs}" PARENT_SCOPE)
endfunction()

# Core sources, include directories and definitions with every module: what
# the native tests build against.
function(matrix_all_modules_core out_prefix)
  matrix_load_definitions()
  get_property(modules GLOBAL PROPERTY MATRIX_MODULES)
  set(sources ${MATRIX_ENGINE_SOURCES})
  set(includes ${MATRIX_REPO_DIR}/core/include)
  foreach(m IN LISTS modules)
    get_property(dir GLOBAL PROPERTY MATRIX_MODULE_${m}_DIR)
    get_property(core GLOBAL PROPERTY MATRIX_MODULE_${m}_CORE_SOURCES)
    list(APPEND sources ${core})
    if(EXISTS ${dir}/include)
      list(APPEND includes ${dir}/include)
    endif()
  endforeach()
  _matrix_module_definitions(defs "${modules}")
  set(${out_prefix}_SOURCES "${sources}" PARENT_SCOPE)
  set(${out_prefix}_INCLUDE_DIRS "${includes}" PARENT_SCOPE)
  set(${out_prefix}_DEFINITIONS "${defs}" PARENT_SCOPE)
endfunction()

# Everything needed to compile program `name`'s shell: sets
#   <out_prefix>_SOURCES, <out_prefix>_INCLUDE_DIRS, <out_prefix>_DEFINITIONS
# and generates its matrix_shell/program/*.inc headers under the build tree.
function(matrix_program_config name out_prefix)
  matrix_load_definitions()
  get_property(programs GLOBAL PROPERTY MATRIX_PROGRAMS)
  if(NOT name IN_LIST programs)
    message(FATAL_ERROR "unknown program '${name}' (known: ${programs})")
  endif()
  get_property(modules GLOBAL PROPERTY MATRIX_MODULES)
  get_property(included GLOBAL PROPERTY MATRIX_PROGRAM_${name}_MODULES)

  set(gen ${CMAKE_BINARY_DIR}/programs/${name}/include)
  set(sources ${MATRIX_ENGINE_SOURCES} ${MATRIX_SHELL_COMMON_SOURCES})
  set(includes ${MATRIX_REPO_DIR}/core/include ${MATRIX_REPO_DIR}/shell/include ${gen})

  set(ops "// Generated for program '${name}' by cmake/matrix_program.cmake.\n")
  set(members "${ops}")
  set(pages "${ops}")
  set(states "${ops}")
  set(text "${ops}#include \"${MATRIX_REPO_DIR}/shell/include/matrix_shell/text_catalog.inc\"\n")

  # every module's operations are listed so OperationId and the menu tree are
  # the same in every program; only included ones get MATRIX_OP_IN
  foreach(m IN LISTS modules)
    get_property(dir GLOBAL PROPERTY MATRIX_MODULE_${m}_DIR)
    set(in FALSE)
    if(m IN_LIST included)
      set(in TRUE)
    endif()
    if(EXISTS ${dir}/ops.def)
      if(in)
        string(APPEND ops "#define MATRIX_OP MATRIX_OP_IN\n")
      else()
        string(APPEND ops "#define MATRIX_OP MATRIX_OP_OUT\n")
      endif()
      string(APPEND ops "#include \"${dir}/ops.def\"\n#undef MATRIX_OP\n")
    endif()
    if(NOT in)
      continue()
    endif()
    get_property(core GLOBAL PROPERTY MATRIX_MODULE_${m}_CORE_SOURCES)
    get_property(shell GLOBAL PROPERTY MATRIX_MODULE_${m}_SHELL_SOURCES)
    list(APPEND sources ${core} ${shell})
    if(EXISTS ${dir}/include)
      list(APPEND includes ${dir}/include)
    endif()
    if(EXISTS ${dir}/shell/app_members.inc)
      string(APPEND members "#include \"${dir}/shell/app_members.inc\"\n")
    endif()
    if(EXISTS ${dir}/shell/pages.def)
      string(APPEND pages "#include \"${dir}/shell/pages.def\"\n")
    endif()
    if(EXISTS ${dir}/shell/pages_state.inc)
      string(APPEND states "#include \"${dir}/shell/pages_state.inc\"\n")
    endif()
    if(EXISTS ${dir}/text.inc)
      string(APPEND text "#include \"${dir}/text.inc\"\n")
    endif()
  endforeach()

  set(out ${gen}/matrix_shell/program)
  _matrix_write_if_changed(${out}/ops.inc "${ops}")
  _matrix_write_if_changed(${out}/app_members.inc "${members}")
  _matrix_write_if_changed(${out}/pages.inc "${pages}")
  _matrix_write_if_changed(${out}/page_states.inc "${states}")
  _matrix_write_if_changed(${out}/text.inc "${text}")

  _matrix_module_definitions(defs "${included}")
  set(${out_prefix}_SOURCES "${sources}" PARENT_SCOPE)
  set(${out_prefix}_INCLUDE_DIRS "${includes}" PARENT_SCOPE)
  set(${out_prefix}_DEFINITIONS "${defs}" PARENT_SCOPE)
endfunction()
