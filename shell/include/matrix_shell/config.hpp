#pragma once

// -----------------------------------------------------------------------------
// Feature flags
// -----------------------------------------------------------------------------
// these are used to keep the build under size limits by compiling
// out rarely needed functionality
//
#ifndef MATRIX_SHELL_ENABLE_PROJECTION
#define MATRIX_SHELL_ENABLE_PROJECTION 1
#endif
#ifndef MATRIX_SHELL_ENABLE_MINOR_MATRIX
#define MATRIX_SHELL_ENABLE_MINOR_MATRIX 1
#endif
#ifndef MATRIX_SHELL_ENABLE_COFACTOR
#define MATRIX_SHELL_ENABLE_COFACTOR 1
#endif
#ifndef MATRIX_SHELL_ENABLE_CRAMER
#define MATRIX_SHELL_ENABLE_CRAMER 1
#endif
#ifndef MATRIX_SHELL_ENABLE_LU
#define MATRIX_SHELL_ENABLE_LU 1
#endif
#ifndef MATRIX_SHELL_ENABLE_COLROW
#define MATRIX_SHELL_ENABLE_COLROW 1
#endif

#ifndef MATRIX_SHELL_ENABLE_DEBUG
#define MATRIX_SHELL_ENABLE_DEBUG 1
#endif

#ifndef MATRIX_SHELL_LANG_FR
#define MATRIX_SHELL_LANG_FR 0
#endif
#ifndef MATRIX_SHELL_ENABLE_VECTOR_OPS
#define MATRIX_SHELL_ENABLE_VECTOR_OPS 1
#endif
#ifndef MATRIX_SHELL_ENABLE_INVERSE
#define MATRIX_SHELL_ENABLE_INVERSE 1
#endif
#ifndef MATRIX_SHELL_ENABLE_TRANSPOSE
#define MATRIX_SHELL_ENABLE_TRANSPOSE 1
#endif
#ifndef MATRIX_SHELL_ENABLE_SPACE_BASES
#define MATRIX_SHELL_ENABLE_SPACE_BASES 1
#endif
#ifndef MATRIX_SHELL_ENABLE_BASIS
#define MATRIX_SHELL_ENABLE_BASIS 1
#endif

// MatrixBasis: a separate program whose only operation is the Col(A) / Nul(A)
// analysis. Set per target by CMake (MATRIX_BUILD_CE_SHELL_BASIS).
#ifndef MATRIX_SHELL_BASIS_ONLY
#define MATRIX_SHELL_BASIS_ONLY 0
#endif
