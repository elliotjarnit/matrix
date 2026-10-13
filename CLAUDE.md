# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Step-by-step linear algebra programs for the TI-84 Plus CE. Exact rational arithmetic; each operation produces a LaTeX-rendered step-by-step explanation. Several calculator programs (Matrix, MatrixLite, MatrixBasis, MatrixDet, MatrixArea) are built from shared code plus a per-program list of feature modules; a program set (`programs/sets/*.cmake`) is one more program whose modules are the union of the programs it names (Mat2 = everything in MatrixBasis + MatrixDet, MatCram = MatrixDet + MatrixArea; one Operations menu each). See `README.md` (user docs) and `CONTRIBUTING.md` (technical overview, memory budget, module/program tables).

## Build & test

```bash
cmake --preset native && cmake --build --preset native && ctest --preset native
```

Run one test: `ctest --preset native -R det` (a test's name is its file stem minus `test_`: engine tests in `core/tests/` — `rational`, `matrix_ops`, `rref`, `det`, `spaces`, `arena_guards`, `arena_budget` — plus each module's `TESTS` from its `module.cmake`, e.g. `lu`, `basis`, `cramer`, `expansion`, `area`). The native build always includes every module. Or run the binary directly: `./build/native/test_det`. Tests are plain `int main()` + `assert` — no framework; a failure aborts.

Sanitizers: presets `native-asan`, `native-ubsan`, `native-lsan`, `native-coverage` (same configure/build/ctest pattern, `build/<preset>/`).

CE (calculator) build — one configure builds every program in `programs/`:

```bash
cmake --preset ce -DCEDEV_ROOT=/path/to/CEdev && cmake --build --preset ce
```

- **`CEDEV_ROOT` defaults to a hard-coded `/home/sightem/CEdev`** (`cmake/CEdevToolchain.cmake:64`) — always pass it explicitly (here it is `~/CEdev`).
- Requires the submodule: `git submodule update --init --recursive` (`third_party/libtexce` is empty on a fresh clone and the CE build needs its `src/tex/*.c`).
- Output: `build/ce/<program>/bin/<TI name>.8xp` (`matlite/bin/MATLITE.8xp`, `matbasis/bin/MATBASIS.8xp`, `matrix/bin/MATRIX.8xp` + appvars); French builds are targets `<program>_fr`. Restrict with `-DMATRIX_CE_PROGRAMS="matlite;matbasis"` or `--target matbasis`; languages with `MATRIX_CE_LANGS` (`en`, `fr`, `en;fr`). Presets: `ce`, `ce-release-fr`, `ce-debug`/`ce-debug-fr` (enables `MATRIX_SHELL_DEBUG` + CEmu debug console), `ce-release-dual-lang` (MatrixLite EN+FR, used by CI), `ce-mat2` / `ce-matcram` (just the Mat2 / MatCram set program, EN+FR, into `build/ce-mat2/` / `build/ce-matcram/`). Output is `mat2/bin/MAT2.8xp`, `matcram/bin/MATCRAM.8xp`.
- Core unit tests can be built as standalone `.8xp` programs for CEmu with `-DMATRIX_BUILD_CE_TESTS=ON`; they report via `dbg_printf`, there is no automation.
- Shell UI on the desktop: `tools/shell_sim` (PortCE, headless, SDL2) runs a program from a key script and saves `.ppm` screenshots; `tools/shell_sim/scenarios.py <program>` generates a script that walks every operation. Use it to check UI changes (snapshot before/after and compare). See `CONTRIBUTING.md`.

clangd setup (native DB for `core/`, host-clang shim DBs for `shell/` and `modules/` since ez80 sources can't be parsed directly; shell code is parsed with the `matrix` program's generated headers): `python tools/setup_clangd.py --cedev /path/to/CEdev`.

## Architecture

Shared layers plus per-feature modules:

- **`core/`** (`matrix_core`) — the platform-agnostic C++20 engine every module builds on (rational, matrix views, arenas, writer/LaTeX, row reduction, `op_echelon`, `op_det`, add/sub/mul/transpose, `spaces.hpp`). No heap, no exceptions, no RTTI, no floating point. Builds and tests natively.
- **`shell/`** (`matrix_shell`) — the TI-84 CE front end every program shares: app/page stack, slot editor, the one menu tree, the generic slot picker, Result and Steps pages. Uses graphx/keypadc/fontlibc and the bundled libtexce renderer. Not covered by native tests (use `tools/shell_sim`).
- **`modules/<name>/`** — one feature each: `ops.def` (its operations), `include/matrix_core/ops_<name>.hpp` + `core/` (its core op, engine rules apply), `shell/` (the `App` members behind its operations, declared in `shell/app_members.inc`; optional pages via `shell/pages.def` + `pages_state.inc`), `text.inc` (its strings), `tests/`. Registered in order in `modules/modules.cmake`.
- **`programs/<name>/program.cmake`** — `matrix_program(name TI_NAME ... TI_NAME_FR ... MODULES ...)`. A program is just its module list.

`cmake/matrix_program.cmake` generates per-program headers under the build tree (`matrix_shell/program/{ops,pages,page_states,app_members,text}.inc`). `OperationId` lists **every** module's operations (rows expand as `MATRIX_OP_IN` for included modules, `MATRIX_OP_OUT` otherwise) so the menu tree compiles identically everywhere; excluded operations have no name, no dispatch case and no strings, and `op_enabled()` hides them — along with any submenu left empty — so every program has the same Main → Matrices / Operations layout with only its own features in it. Module code must not reference another module's types; shared needs go in `core/` or `shell/`.

### Memory model

One `malloc` at startup: a 12 KiB `Slab` (`shell/src/app.cpp`) split into two `Arena` bump allocators, `persist_` (8 KiB) and `scratch_` (4 KiB); the sizes come from measured peaks and `test_arena_budget` guards them. Everything else — matrices, explanation contexts, working copies — is arena-allocated. The calculator has ~60 KB usable RAM and a 4 KB stack, so **never introduce heap allocation, recursion over matrix size, or large stack frames** (the CE build compiles with `-Wframe-larger-than=127` and `-fstack-usage`).

Arena discipline, which shell code must follow exactly:

- `ArenaScope tx(persist_)` — transactional; rewinds on destruction unless `tx.commit()` is called. Wrap every result allocation in one.
- `ArenaScratchScope(scratch_)` — clears scratch on entry; every op and every step render takes one.
- `persist_base_mark_` is the high-water mark of long-lived slot storage. Starting a new operation rewinds `persist_` to it and resets `expl_` (see `App::update_slot_pick` in `shell/src/pages/slot_pick.cpp`), invalidating the previous result — result pages hold raw `const Rational*` pointers into that arena region.

### Core types

- `Rational` — int64 num/den, auto-reduced. All arithmetic returns `ErrorCode` (overflow is a real failure case, not UB).
- `MatrixView` / `MatrixMutView` — `{rows, cols, stride, data}` handles (~24 bytes) over arena storage. Copying a view copies metadata, never data. Slots are allocated 6x6 and expose an active `rows`x`cols` sub-view. `kMaxRows=6`, `kMaxCols=7` (the extra column is for internal augmented `[A|b]` work).
- `Error` / `ErrorCode` — returned by value everywhere; no exceptions. `In`/`Out`/`InOut` in `error.hpp` are empty documentation macros.
- `Writer` / `CheckedWriter` (`writer.hpp`) — fixed-buffer string building; `CheckedWriter` aborts on overflow and is for cases where overflow is a programmer error.

### On-demand explanations

`Explanation` is a manual vtable over a type-erased context (`core/include/matrix_core/explanation.hpp`): `{step_count, render_step, destroy}`. An op with `ExplainOptions{.enable=true, .persist=&arena}` placement-news a small context struct into the persist arena capturing the *input view*, then returns. Step *i* is materialized only when `render_step(i, StepRenderBuffers)` is called — it re-derives state by replaying the algorithm into scratch up to step *i* (see `core/src/ops_det_ondemand.cpp`). Only one step's LaTeX exists at a time (`App::step_latex_`, 1 KiB).

Consequences: **the input matrix must outlive the Explanation**, step rendering must be side-effect-free and re-entrant, and `render_step` must never allocate from persist.

### Shell page stack

`App` owns a fixed 8-deep array of `Page` (`shell/include/matrix_shell/app.hpp`), each a `PageKind` tag + union of per-page state structs. `App::step()` dispatches to `update_<page>()` then `render()` on the top page — one frame, double-buffered via `gfx_SwapDraw()`. Add a page = add a `PageKind`, a state struct, a union member, a `Page::make_*` factory (`shell/src/page.cpp`), update/render methods, and both switches in `app.cpp`. `CLEAR` at depth 1 exits the program.

The slot picker is generic: once operand slots are picked, `App::run_operation` dispatches to the operation's `run` member from `ops.def` (in its module), which pushes a `Result` page or a module page. A `Result` page draws the op's `result_body` (`render_result_matrix` for a plain grid; shared `render_rank_line` / `render_result_grid` helpers), and 2ND calls its `result_steps` (`open_result_steps` pushes the `Steps` page that drives libtexce over `expl_`).

### Localization

User-facing strings are X-macro rows `(Id, "dotted.key", "English", "Francais")`: shared ones in `shell/include/matrix_shell/text_catalog.inc`, a module's own in `modules/<name>/text.inc` (compiled only into programs that include it). Use the consteval UDL: `"msg.some_key"_tx` returns `const char*`, `"op.det"_tid` returns a `TextId` for tables. An unknown key is a compile error. Language is selected at compile time by `MATRIX_SHELL_LANG_FR` (one binary per language). **Never hard-code a display string**; French strings are ASCII-only (no accents) because of the CE font.

## Conventions

- `.clang-format` is authoritative: tabs for indentation (width 8), 140-column limit, `SortIncludes: false`, no short single-line ifs/loops. Run clang-format on touched files.
- Native builds use `-Werror` with a wide warning set including `-Wconversion`/`-Wsign-conversion`/`-Wswitch-enum`; be explicit with casts and exhaustive over enums.
- Recoverable problems return `ErrorCode`; invariant violations use `fail_fast(...)` / `REQUIRE(cond, "msg")` from `shell/.../detail/app_internal.hpp` (abort — these mean a bug, not bad input).
- Shell debug output goes through `SHELL_DBG(...)`, compiled out unless `MATRIX_SHELL_DEBUG=ON`.
- Adding an operation: usually a new module (see `CONTRIBUTING.md` → Modules and Programs): directory with `module.cmake`, `ops.def`, core + shell code, `text.inc`, tests; list it in `modules/modules.cmake`; add its menu entries to `shell/src/pages/menu.cpp`; add it to the programs that want it. Tests are registered from `module.cmake`, no `CMakeLists.txt` edit. Add the test to the `CONTRIBUTING.md` table.

## Releases

`.github/workflows/nightly-release.yml` (daily, moves the `nightly` tag) and `stable-release.yml` (manual `workflow_dispatch` with a tag input) build the `ce-release-dual-lang` preset (MatrixLite only) against a downloaded CEdev nightly and publish `MATLITE.8xp`, `MATLITFR.8xp` (from `build/cdl/matlite*/bin/`), plus `TeXFonts.8xv` / `TeXScrpt.8xv` from the libtexce submodule — the fonts are required on the calculator or `App::init()` fails.
