include_guard(GLOBAL)


set(_matrix_optional_default ON)
if(MATRIX_BUILD_CE)
  set(_matrix_optional_default OFF)
endif()

option(MATRIX_FEATURE_PROJECTION "Enable vector projection decomposition operation" ${_matrix_optional_default})
option(MATRIX_FEATURE_MINOR_MATRIX "Enable minor matrix operation" ${_matrix_optional_default})
option(MATRIX_FEATURE_COFACTOR "Enable cofactor element operation" ${_matrix_optional_default})
option(MATRIX_FEATURE_CRAMER "Enable Cramer's rule operation (and det-replace-column helper for steps)" ${_matrix_optional_default})

# LU is on by default everywhere, including the CE build: it is one of the
# operations the program is used for day to day. Turn it off to save space.
option(MATRIX_FEATURE_LU "Enable LU factorization and LU solve operations" ON)

option(MATRIX_FEATURE_COLROW "Enable the column row (outer product) expansion of A*B" ON)

# Operations the Math 210 review sheets never ask for. They stay on natively;
# the CE presets drop them because the program has to fit in calculator RAM.
option(MATRIX_FEATURE_VECTOR_OPS "Enable dot and cross product operations" ON)
option(MATRIX_FEATURE_INVERSE "Enable the matrix inverse operation" ON)
option(MATRIX_FEATURE_TRANSPOSE "Enable the transpose operation" ON)
option(MATRIX_FEATURE_SPACE_BASES "Enable the column/row/null/left-null space basis operations" ON)

option(MATRIX_SHELL_DEBUG "Enable shell debug logging" OFF)

function(matrix_resolve_feature_deps)
  # for future dependency toggles
endfunction()

function(matrix_get_feature_compile_definitions out_var)
  set(defs)

  if(MATRIX_FEATURE_PROJECTION)
    list(APPEND defs MATRIX_CORE_ENABLE_PROJECTION=1 MATRIX_SHELL_ENABLE_PROJECTION=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_PROJECTION=0 MATRIX_SHELL_ENABLE_PROJECTION=0)
  endif()

  if(MATRIX_FEATURE_MINOR_MATRIX)
    list(APPEND defs MATRIX_CORE_ENABLE_MINOR_MATRIX=1 MATRIX_SHELL_ENABLE_MINOR_MATRIX=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_MINOR_MATRIX=0 MATRIX_SHELL_ENABLE_MINOR_MATRIX=0)
  endif()

  if(MATRIX_FEATURE_COFACTOR)
    list(APPEND defs MATRIX_CORE_ENABLE_COFACTOR=1 MATRIX_SHELL_ENABLE_COFACTOR=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_COFACTOR=0 MATRIX_SHELL_ENABLE_COFACTOR=0)
  endif()

  if(MATRIX_FEATURE_CRAMER)
    list(APPEND defs MATRIX_CORE_ENABLE_CRAMER=1 MATRIX_SHELL_ENABLE_CRAMER=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_CRAMER=0 MATRIX_SHELL_ENABLE_CRAMER=0)
  endif()

  if(MATRIX_FEATURE_LU)
    list(APPEND defs MATRIX_CORE_ENABLE_LU=1 MATRIX_SHELL_ENABLE_LU=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_LU=0 MATRIX_SHELL_ENABLE_LU=0)
  endif()

  if(MATRIX_FEATURE_COLROW)
    list(APPEND defs MATRIX_CORE_ENABLE_COLROW=1 MATRIX_SHELL_ENABLE_COLROW=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_COLROW=0 MATRIX_SHELL_ENABLE_COLROW=0)
  endif()

  if(MATRIX_FEATURE_VECTOR_OPS)
    list(APPEND defs MATRIX_CORE_ENABLE_VECTOR_OPS=1 MATRIX_SHELL_ENABLE_VECTOR_OPS=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_VECTOR_OPS=0 MATRIX_SHELL_ENABLE_VECTOR_OPS=0)
  endif()

  if(MATRIX_FEATURE_INVERSE)
    list(APPEND defs MATRIX_CORE_ENABLE_INVERSE=1 MATRIX_SHELL_ENABLE_INVERSE=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_INVERSE=0 MATRIX_SHELL_ENABLE_INVERSE=0)
  endif()

  if(MATRIX_FEATURE_TRANSPOSE)
    list(APPEND defs MATRIX_CORE_ENABLE_TRANSPOSE=1 MATRIX_SHELL_ENABLE_TRANSPOSE=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_TRANSPOSE=0 MATRIX_SHELL_ENABLE_TRANSPOSE=0)
  endif()

  if(MATRIX_FEATURE_SPACE_BASES)
    list(APPEND defs MATRIX_CORE_ENABLE_SPACE_BASES=1 MATRIX_SHELL_ENABLE_SPACE_BASES=1)
  else()
    list(APPEND defs MATRIX_CORE_ENABLE_SPACE_BASES=0 MATRIX_SHELL_ENABLE_SPACE_BASES=0)
  endif()

  if(MATRIX_SHELL_DEBUG)
    list(APPEND defs MATRIX_SHELL_ENABLE_DEBUG=1)
  else()
    list(APPEND defs MATRIX_SHELL_ENABLE_DEBUG=0)
  endif()

  set(${out_var} "${defs}" PARENT_SCOPE)
endfunction()

function(matrix_get_feature_compile_options out_var)
  matrix_get_feature_compile_definitions(defs)
  set(opts "${defs}")
  list(TRANSFORM opts PREPEND "-D")
  set(${out_var} "${opts}" PARENT_SCOPE)
endfunction()

function(matrix_print_feature_summary)
  message(STATUS "Matrix features: PROJECTION=${MATRIX_FEATURE_PROJECTION} MINOR_MATRIX=${MATRIX_FEATURE_MINOR_MATRIX} COFACTOR=${MATRIX_FEATURE_COFACTOR} CRAMER=${MATRIX_FEATURE_CRAMER} LU=${MATRIX_FEATURE_LU} COLROW=${MATRIX_FEATURE_COLROW} VECTOR_OPS=${MATRIX_FEATURE_VECTOR_OPS} INVERSE=${MATRIX_FEATURE_INVERSE} TRANSPOSE=${MATRIX_FEATURE_TRANSPOSE} SPACE_BASES=${MATRIX_FEATURE_SPACE_BASES} SHELL_DEBUG=${MATRIX_SHELL_DEBUG}")
endfunction()
