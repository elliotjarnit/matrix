# Contributing

## Technical Overview

The repository builds several calculator programs out of shared parts:

- **`core/`** (matrix_core): the platform agnostic C++20 math engine: exact rational matrix arithmetic, row reduction, determinants, subspace helpers, and a type erased on demand explanation system. it has zero heap allocations during operation (everything runs on a preallocated arena). fully tested with native sanitizer builds (ASan, UBSan, LSan)

- **`shell/`** (matrix_shell): the TI-84+ CE front end every program shares: the page stack, the slot editor, the menu tree, the slot picker, and the result and steps pages. It uses the CEs graphics library, keyboard library, and font library. LaTeX rendering is handled by the bundled **libtexce** engine

- **`modules/<name>/`**: one feature each (LU, Col(A) & Nul(A), Cramer's rule, ...), holding everything that feature needs: its core operation, its screens, the operations it puts in the menus, its strings and its tests

- **`programs/<name>/`**: a calculator program is a name plus a list of modules. See [Modules and Programs](#modules-and-programs)

### Memory Architecture

The calculator has roughly **60 KB** of usable RAM and **4 KB** of stack space. Matrix's math engine uses an 12 KiB slab allocated once at startup split into two monotonic bump arenas:

| Arena | Size | Purpose |
|---|---|---|
| **persist** | 8 KiB | Long lived matrix slot storage + ephemeral explanation contexts |
| **scratch** | 4 KiB | Temporary working memory, reset per-operation or per step render |

Sizes come from measured peaks, not guesswork: 4608 bytes of that persist is
permanent slot storage, and the worst operations on top of it are `op_lu_factor`
at 2408 bytes of persist and `op_lu_solve` at 2016 bytes of scratch. The program,
this slab and the 20 KiB TeX renderer pool all live in the calculator's RAM at
once, so the budget is deliberately tight. `test_arena_budget` runs every
explained operation at 6x6 against these exact sizes and fails if one no longer
fits.

There are **zero heap allocations** during normal operation — the single `malloc` happens at startup. This was verified with Valgrind/Massif profiling.

### Design Decisions

- **View/handle pattern** for matrices: a `MatrixView` is just a pointer + dimensions (~24 bytes). Backing storage (`Rational[36]` = 576 bytes per slot) lives in the arena. Copying a matrix copies metadata, not data
- **Exact Rational type**: 64 bit numerator and denominator, auto reduced to lowest terms. No floating point anywhere in the math pipeline
- **On demand step generation**: explanation objects are type erased closures. the shell calls `render_step(i)` only when the user navigates to step *i*. only one steps LaTeX is in memory at a time
- **Programs built from modules**: a program compiles only the modules it lists, so a small program (say, for one exam) carries only the code and strings it needs


## Building from Source

### Prerequisites

- **CMake** .= 3.20
- **Ninja** build system
- **Clang** for native builds
- **[CE C/C++ Toolchain](https://ce-programming.github.io/toolchain/)** (for TI84 CE builds)

### Native Build (for development and testing)

```bash
cmake --preset native
cmake --build --preset native
```

This builds the core library and all test executables with strict warnings (`-Werror`) enabled

### TI84 CE Build (produces the .8xp programs)

```bash
cmake --preset ce -DCEDEV_ROOT=/path/to/CEdev
cmake --build --preset ce
```

One configure builds every program in `programs/`, each to
`build/ce/<program>/bin/<TI name>.8xp` (French builds go to `<program>_fr`). To
build only some of them, set `MATRIX_CE_PROGRAMS`, or build one target:

```bash
cmake --preset ce -DMATRIX_CE_PROGRAMS="matlite;matbasis"
cmake --build build/ce --target matbasis
```

`MATRIX_CE_LANGS` picks the languages (`en`, `fr`, or `en;fr`). The presets:
`ce` (every program, English), `ce-release-fr` (French), `ce-debug` /
`ce-debug-fr` (shell debug logging and the CEmu debug console), `ce-mat2` and
`ce-matcram` (the Mat2 and MatCram program sets, see
[Program sets](#program-sets)), and
`ce-release-dual-lang`, which is what the release workflows build: MatrixLite
in both languages.

### Sanitizer Builds

Several sanitizer presets are available for catching memory and undefined behavior bugs

```bash
cmake --preset native-asan && cmake --build --preset native-asan

cmake --preset native-ubsan && cmake --build --preset native-ubsan

cmake --preset native-lsan && cmake --build --preset native-lsan

cmake --preset native-coverage && cmake --build --preset native-coverage
```

## Modules and Programs

### Programs

| Program | TI name (FR) | Modules |
|---|---|---|
| Matrix | `MATRIX` (`MATRIXFR`) | every module |
| MatrixLite | `MATLITE` (`MATLITFR`) | arithmetic, colrow, det, echelon, solve, dependency, invertible, lu |
| MatrixBasis | `MATBASIS` (`MATBASFR`) | basis |
| MatrixDet | `MATDET` (`MATDETFR`) | det, expansion, triangular, adjugate, invertible, indep_det |
| MatrixArea | `MATAREA` (`MATAREFR`) | cramer, area |
| Mat2 (set) | `MAT2` (`MAT2FR`) | every module of MatrixBasis and MatrixDet |
| MatCram (set) | `MATCRAM` (`MATCRMFR`) | every module of MatrixDet and MatrixArea |

A program is `programs/<name>/program.cmake`:

```cmake
matrix_program(exam2
  TI_NAME EXAM2          # 8 characters at most
  TI_NAME_FR EXAM2FR
  DESCRIPTION "Exam 2"
  MODULES
    det
    echelon
    basis
)
```

Every program shares one menu tree (`shell/src/pages/menu.cpp`): Main ->
Matrices / Operations, with the operations in their usual submenus. An
operation whose module the program leaves out is not compiled in and drops out
of the menus, as does any submenu left empty, so the program above shows
Determinant, REF/RREF and Col(A) & Nul(A) under Operations and nothing else.
Make the directory, re-run the `ce` configure, and `build/ce/exam2/bin/EXAM2.8xp`
appears.

### Program sets

A program set is one program with every feature of the programs it names:
its modules are the union of theirs, so all their operations share the one
Operations menu. Sets live in `programs/sets/<name>.cmake`, read after every
program:

```cmake
matrix_program_set(mat2
  TI_NAME MAT2
  TI_NAME_FR MAT2FR
  DESCRIPTION "Mat2"
  PROGRAMS
    matbasis
    matdet
)
```

Otherwise a set is an ordinary program: target `mat2`, built by `ce` with the
rest, selectable with `MATRIX_CE_PROGRAMS`. The `ce-mat2` preset builds just
Mat2, in both languages, into `build/ce-mat2/mat2[_fr]/bin/`:

```bash
cmake --preset ce-mat2 -DCEDEV_ROOT=/path/to/CEdev
cmake --build build/ce-mat2
```

MatCram (`programs/sets/matcram.cmake`, MatrixDet + MatrixArea) works the same
way with the `ce-matcram` preset, into `build/ce-matcram/matcram[_fr]/bin/`.

A TI variable holds at most 65535 bytes. A program bigger than that (Matrix,
for one) is split by `convbin` into a small launcher `.8xp` plus `.8xv`
appvars, which all have to be sent; check the build output for
`Input too large; split across N appvars`. Fitting in a variable is also not
the same as fitting in calculator RAM next to the 12 KiB slab and the 20 KiB
TeX pool, which is why MatrixLite exists.

### Modules

| Module | Operations |
|---|---|
| `arithmetic` | Add, Subtract, Multiply |
| `colrow` | Multiply (col-row): A*B as a sum of outer products |
| `vectors` | Dot product, Cross product, Projection |
| `det` | Determinant (row reduction, ending on (-1)^r times the pivots) |
| `expansion` | Cofactor expansion across any row or down any column |
| `triangular` | Det (triangular): the product of the diagonal |
| `adjugate` | Adjoint adj(A): every cofactor, adj(A) = C^T, det(A), A^-1 = adj(A)/det(A) and det(A^-1) |
| `cofactor` | Cofactor C_ij |
| `minor_matrix` | Matrix of minors |
| `cramer` | Cramer's rule |
| `echelon` | REF, RREF |
| `solve` | Solve Ax = b (parametric vector form) |
| `dependency` | Spans R^m?, Linearly independent?, Onto / 1-1? |
| `transpose` | Transpose |
| `inverse` | Inverse |
| `invertible` | Invertible? |
| `indep_det` | Indep.? (det): n vectors in R^n are independent iff det(A) != 0 |
| `area` | Area: parallelogram, Area: triangle (from edge vectors or vertices), Area of T(P) for T(x) = Ax with Area T(P) = \|det A\| Area P; the result screen sketches the region |
| `lu` | LU factorization (PA = LU), LU solve |
| `spaces` | Col / Row / Null / Left null space bases |
| `basis` | Col(A) & Nul(A): bases, rank, dimensions, rank theorem |

A module directory holds (every part optional):

| Path | What it is |
|---|---|
| `module.cmake` | `matrix_module(<name> DESCRIPTION ... CORE_SOURCES ... SHELL_SOURCES ... TESTS ... CE_TESTS ...)` |
| `ops.def` | `MATRIX_OP(Id, "op.key", is_binary, run, result_body, result_steps)` rows: the operations it adds |
| `text.inc` | its strings, as rows of the text catalog |
| `include/matrix_core/ops_<name>.hpp`, `core/` | its core operation (engine rules: no heap, no exceptions) |
| `shell/*.cpp`, `shell/app_members.inc` | the `App` members behind its operations, and their declarations |
| `shell/pages.def`, `shell/pages_state.inc` | pages it adds (`MATRIX_PAGE(Kind, State, member)`) and their state |
| `tests/` | native tests, and `*_ce.cpp` wrappers for CEmu |

`cmake/matrix_program.cmake` generates, per program, the headers that tie these
together (`matrix_shell/program/*.inc` under the build tree): `OperationId`
lists every module's operations so the menu tree compiles the same in every
program, but only the included ones get a name, a dispatch case and their
strings. Strings used by more than one module, and by the shell, stay in
`shell/include/matrix_shell/text_catalog.inc`.

To add a module: create the directory, list it in `modules/modules.cmake`, put
its operations in the menu tree in `menu.cpp`, and add it to the programs that
want it.

### Shell simulator

`tools/shell_sim` runs the real calculator UI on the desktop, headless, using
PortCE (bundled with libtexce; needs SDL2, not SDL2_mixer). A key script drives
it and it saves screenshots, which makes UI changes testable without a
calculator:

```bash
cmake -S tools/shell_sim -B build/sim-matlite -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DSIM_PROGRAM=matlite
cmake --build build/sim-matlite
tools/shell_sim/scenarios.py matlite > build/sim-matlite/script.txt
(cd build/sim-matlite && mkdir -p out && SHELL_SIM_SCRIPT=script.txt SHELL_SIM_OUT=out ./shell_sim)
```

`scenarios.py` enters a set of matrices and walks every operation of a program,
snapshotting each result and step (`.ppm`). Run it before and after a change
and compare the two directories.

## Testing

### Running Tests

```bash
cmake --preset native
cmake --build --preset native
ctest --preset native
```

### Test Suites

Engine tests live in `core/tests/`; each module's tests live in its own
`tests/` directory. The native build always builds every module, so it runs all
of them.

| Test Suite | What It Covers |
|---|---|
| `test_rational` | Rational number arithmetic, reduction, edge cases |
| `test_matrix_ops` | Add, subtract, multiply, transpose |
| `test_det` | Determinant computation and step generation |
| `test_rref` | REF and RREF with step verification |
| `test_inverse` | Matrix inverse via Gauss-Jordan |
| `test_cramer` | Cramer's rule solution and Δ/Δ\_i explanations |
| `test_lu` | LU factorization (including PA = LU), substitution, and LU step rendering |
| `test_vectors` | Dot product, cross product, projection |
| `test_cofactor_element` | Single cofactor/minor with step rendering |
| `test_expansion` | Cofactor expansion along every row and column, against the determinants worksheet |
| `test_triangular` | Triangular detection and the diagonal product |
| `test_indep_det` | Linear independence by det(A) != 0, with the det steps and the verdict |
| `test_adjugate` | adj(A), A adj(A) = det(A) I, the inverse and det(A^-1) steps, against the Cramer worksheet |
| `test_area` | Parallelogram and triangle areas from vectors or vertices, the opposite corner of four vertices, T(P) and the \|det A\| check, against the areas worksheet |
| `test_minor_matrix` | Full matrix of minors |
| `test_spaces` | Column/row/null space basis extraction |
| `test_solve_parametric` | Complete solution of Ax=b in parametric vector form, with step rendering |
| `test_dependency` | Linear (in)dependence and the dependency relation of each dependent vector |
| `test_basis` | Col(A) / Nul(A) bases, dimensions, the rank theorem, and step rendering into the shell buffers |
| `test_mul_colrow` | Matrix product built as the sum of outer products (column row expansion) |
| `test_arena_budget` | Every explained op at 6x6 fits the shell arenas (8 KiB persist / 4 KiB scratch) |
| `test_arena_guards` | Arena allocator boundary checks |

### CE Target Tests

The core can also be compiled and run directly on [CEmu](github.com/CE-Programming/CEmu) as individual .8xp test programs:

```bash
cmake --preset ce -DCEDEV_ROOT=/path/to/CEdev -DMATRIX_BUILD_CE_TESTS=ON
cmake --build --preset ce
```

These are somewhat scuffed, I have not bothered setting up autotests. you are reliant on the output you see on the emulator.
