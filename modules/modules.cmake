# Every module, in the order their operations are numbered in OperationId.
# Add a new module's directory here.
foreach(_matrix_module IN ITEMS
    arithmetic
    colrow
    vectors
    det
    cofactor
    minor_matrix
    cramer
    echelon
    solve
    dependency
    transpose
    inverse
    invertible
    lu
    spaces
    basis
)
  include(${CMAKE_CURRENT_LIST_DIR}/${_matrix_module}/module.cmake)
endforeach()
