# MatrixDet: the Math 210 determinants worksheet. Cofactor expansion across
# any row or down any column, triangular determinants, det(A) from an echelon
# form as (-1)^r times the pivots, and invertibility / linear independence
# decided by det(A) != 0.
matrix_program(matdet
  TI_NAME MATDET
  TI_NAME_FR MATDETFR
  DESCRIPTION "MatrixDet"
  MODULES
    det
    expansion
    triangular
    adjugate
    invertible
    indep_det
)
