# Matrix: every module. Too large for one calculator variable, so convbin
# splits it into a launcher plus appvars; it may not fit in calculator RAM.
matrix_program(matrix
  TI_NAME MATRIX
  TI_NAME_FR MATRIXFR
  DESCRIPTION "Matrix"
  MODULES
    arithmetic
    colrow
    vectors
    det
    expansion
    triangular
    adjugate
    cofactor
    minor_matrix
    cramer
    echelon
    solve
    dependency
    transpose
    inverse
    invertible
    indep_det
    area
    lu
    spaces
    basis
)
