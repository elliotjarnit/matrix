# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Step-by-step linear algebra program for the TI-84 Plus CE. Exact rational arithmetic, 21 operations, each producing a LaTeX-rendered step-by-step explanation. See `README.md` (user docs) and `CONTRIBUTING.md` (technical overview, memory budget, feature-flag table).

## Build & test

```bash
cmake --preset native && cmake --build --preset native && ctest --preset native
```

Run one test: `ctest --preset native -R det` (test names are the `add_test(NAME ...)` labels in `CMakeLists.txt`: `rational`, `matrix_ops`, `rref`, `det`, `inverse`, `vectors`, `spaces`, `arena_guards`, and the feature-gated `minor_matrix`, `cofactor_element`, `cramer`). Or run the binary directly: `./build/native/test_det`. Tests are plain `int main()` + `assert` — no framework; a failure aborts.

Sanitizers: presets `native-asan`, `native-ubsan`, `native-lsan`, `native-coverage` (same configure/build/ctest pattern, `build/<preset>/`).

CE (calculator) build:

```bash
cmake --preset ce -DCEDEV_ROOT=/path/to/CEdev && cmake --build --preset ce
```

- **`CEDEV_ROOT` defaults to a hard-coded `/home/sightem/CEdev`** (`cmake/CEdevToolchain.cmake:64`) — always pass it explicitly.
- Requires the submodule: `git submodule update --init --recursive` (`third_party/libtexce` is empty on a fresh clone and the CE build needs its `src/tex/*.c`).
- Output: `build/ce/matrix_shell/bin/MATRIX.8xp`. Other CE presets: `ce-release-fr` (`MATRIXFR.8xp`), `ce-release-dual-lang` (both, used by CI), `ce-debug` / `ce-debug-fr` (enables `MATRIX_SHELL_DEBUG` + CEmu debug console).
- Core unit tests can be built as standalone `.8xp` programs for CEmu with `-DMATRIX_BUILD_CE_TESTS=ON`; they report via `dbg_printf`, there is no automation.

clangd setup (generates two compile DBs — native for `core/`, a host-clang shim DB for `shell/` since ez80 sources can't be parsed directly): `python tools/setup_clangd.py --cedev /path/to/CEdev`.

## Architecture

Two layers, strictly separated:

- **`core/`** (`matrix_core`) — platform-agnostic C++20, no heap, no exceptions, no RTTI, no floating point. Builds and tests natively.
- **`shell/`** (`matrix_shell`) — TI-84 CE front end only. Builds only under `MATRIX_BUILD_CE=ON`, uses graphx/keypadc/fontlibc and the bundled libtexce renderer. Not covered by native tests.

### Memory model

One `malloc` at startup: an 18 KiB `Slab` (`shell/src/app.cpp`) split into two `Arena` bump allocators, `persist_` (9 KiB) and `scratch_` (9 KiB). Everything else — matrices, explanation contexts, working copies — is arena-allocated. The calculator has ~60 KB usable RAM and a 4 KB stack, so **never introduce heap allocation, recursion over matrix size, or large stack frames** (the CE build compiles with `-Wframe-larger-than=127` and `-fstack-usage`).

Arena discipline, which shell code must follow exactly:

- `ArenaScope tx(persist_)` — transactional; rewinds on destruction unless `tx.commit()` is called. Wrap every result allocation in one.
- `ArenaScratchScope(scratch_)` — clears scratch on entry; every op and every step render takes one.
- `persist_base_mark_` is the high-water mark of long-lived slot storage. Starting a new operation rewinds `persist_` to it and resets `expl_` (see `shell/src/pages/slot_pick.cpp:123`), invalidating the previous result — result pages hold raw `const Rational*` pointers into that arena region.

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

Operations are dispatched and executed in `shell/src/pages/slot_pick.cpp` (~1000 lines) once the user picks operand slots; results are pushed as `Result` / `ProjectionResult` / `CramerStepsMenu` pages, and the `Steps` page drives libtexce over the stored `expl_`.

### Feature flags

`MATRIX_FEATURE_{PROJECTION,MINOR_MATRIX,COFACTOR,CRAMER}` default ON natively, OFF for CE (`cmake/matrix_features.cmake`), to keep the `.8xp` under size limits. Each expands to a **pair** of defines, `MATRIX_CORE_ENABLE_X` and `MATRIX_SHELL_ENABLE_X`, and guarded code tests both. When a feature is off, its source file is dropped from the build and `core/src/ops_optional_stubs.cpp` provides a stub returning `err_feature_disabled()`, so the `ops.hpp` API stays link-complete. Shell entry points additionally show a "disabled in this build" message. Keep all three in sync when adding an optional op.

### Localization

All user-facing strings live in `shell/include/matrix_shell/text_catalog.inc` as X-macro rows `(Id, "dotted.key", "English", "Francais")`. Use the consteval UDL: `"msg.some_key"_tx` returns `const char*`, `"op.det"_tid` returns a `TextId` for tables. An unknown key is a compile error. Language is selected at compile time by `MATRIX_SHELL_LANG_FR` (one binary per language). **Never hard-code a display string**; French strings are ASCII-only (no accents) because of the CE font.

## Conventions

- `.clang-format` is authoritative: tabs for indentation (width 8), 140-column limit, `SortIncludes: false`, no short single-line ifs/loops. Run clang-format on touched files.
- Native builds use `-Werror` with a wide warning set including `-Wconversion`/`-Wsign-conversion`/`-Wswitch-enum`; be explicit with casts and exhaustive over enums.
- Recoverable problems return `ErrorCode`; invariant violations use `fail_fast(...)` / `REQUIRE(cond, "msg")` from `shell/.../detail/app_internal.hpp` (abort — these mean a bug, not bad input).
- Shell debug output goes through `SHELL_DBG(...)`, compiled out unless `MATRIX_SHELL_DEBUG=ON`.
- Adding a core op: declare in `ops.hpp`, implement as `core/src/ops_*.cpp`, add the source + a `test_*` executable + `add_test` in `CMakeLists.txt`, and add the test to the `CONTRIBUTING.md` table.

## Releases

`.github/workflows/nightly-release.yml` (daily, moves the `nightly` tag) and `stable-release.yml` (manual `workflow_dispatch` with a tag input) build the `ce-release-dual-lang` preset against a downloaded CEdev nightly and publish `MATRIX.8xp`, `MATRIXFR.8xp`, plus `TeXFonts.8xv` / `TeXScrpt.8xv` from the libtexce submodule — the fonts are required on the calculator or `App::init()` fails.
