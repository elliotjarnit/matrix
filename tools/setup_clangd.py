from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def _write_text(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8", newline="\n")


def _write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8", newline="\n")


def _ensure_native_compile_db(repo_root: Path, native_build_dir: Path) -> Path:
    compile_db = native_build_dir / "compile_commands.json"
    if compile_db.exists():
        return compile_db

    print(f"[clangd] Missing {compile_db}, running `cmake --preset native`...")
    subprocess.run(["cmake", "--preset", "native"], cwd=repo_root, check=True)

    if not compile_db.exists():
        raise RuntimeError(
            f"Expected {compile_db} after configuring, but it was not created."
        )
    return compile_db


def _symlink_or_copy(dest: Path, target: Path) -> None:
    if dest.exists() or dest.is_symlink():
        dest.unlink()

    try:
        rel_target = os.path.relpath(target, start=dest.parent)
        dest.symlink_to(rel_target)
    except OSError:
        shutil.copyfile(target, dest)


def _program_config(repo_root: Path, program: str, out_dir: Path) -> dict:
    """Generate `program`'s headers and return its include dirs and definitions."""
    subprocess.run(
        [
            "cmake",
            f"-DPROGRAM={program}",
            f"-DOUT={out_dir}",
            "-P",
            str(repo_root / "cmake" / "matrix_generate_program.cmake"),
        ],
        cwd=repo_root,
        check=True,
    )
    return json.loads((out_dir / f"{program}.json").read_text(encoding="utf-8"))


def _shell_args(repo_root: Path, cedev_include: Path, config: dict) -> list[str]:
    shim = repo_root / "tools" / "clangd" / "ce_shim.hpp"
    return (
        [
            "clang++",
            "-std=gnu++20",
            "-fno-exceptions",
            "-fno-rtti",
            f"-include{shim}",
        ]
        + [f"-I{d}" for d in config["include_dirs"]]
        + [
            f"-I{repo_root / 'third_party' / 'libtexce' / 'src'}",
            f"-I{repo_root / 'third_party' / 'libtexce' / 'include'}",
            # Must be AFTER host system headers; CEdev provides sys/types.h etc.
            f"-idirafter{cedev_include}",
            # Match the CE build.
            "-DTEX_USE_FONTLIB",
            "-DTEX_DIRECT_RENDER",
        ]
        + [f"-D{d}" for d in config["definitions"]]
    )


def _generate_shell_compile_dbs(
    repo_root: Path, cedev_include: Path, native_compile_db: Path, program: str
) -> None:
    """shell/compile_commands.json for the shared shell, and
    modules/compile_commands.json for the modules: their shell code parsed as
    for the CE, their core code with the native flags."""
    config = _program_config(repo_root, program, repo_root / "build" / "clangd")
    args = _shell_args(repo_root, cedev_include, config)

    def entries(sources: list[Path]) -> list[dict[str, object]]:
        return [
            {"directory": str(repo_root), "file": str(src), "arguments": args + ["-c", str(src)]}
            for src in sources
        ]

    shell_sources = sorted((repo_root / "shell" / "src").rglob("*.cpp"))
    if not shell_sources:
        raise RuntimeError("No shell sources found under shell/src")
    _write_json(repo_root / "shell" / "compile_commands.json", entries(shell_sources))

    modules_dir = repo_root / "modules"
    module_shell = sorted(modules_dir.glob("*/shell/*.cpp"))
    native = json.loads(native_compile_db.read_text(encoding="utf-8"))
    module_core = [e for e in native if str(Path(e["file"]).resolve()).startswith(str(modules_dir))]
    _write_json(modules_dir / "compile_commands.json", entries(module_shell) + module_core)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Set up clangd (and optional VSCode settings) for this repo."
    )
    parser.add_argument(
        "--cedev",
        default=str(Path.home() / "CEdev"),
        help="Path to CEdev root (default: ~/CEdev).",
    )
    parser.add_argument(
        "--native-build-dir",
        default="build/native",
        help="CMake native build directory (default: build/native).",
    )
    parser.add_argument(
        "--program",
        default="matrix",
        help="Program whose module set the shell is parsed with (default: matrix, every module).",
    )
    parser.add_argument(
        "--no-vscode",
        action="store_true",
        help="Do not write .vscode settings/extensions/shim.",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    cedev_root = Path(os.path.expanduser(args.cedev)).resolve()
    cedev_include = cedev_root / "include"
    if not cedev_include.is_dir():
        print(
            f"error: CEdev include dir not found at {cedev_include}. "
            f"Pass --cedev /path/to/CEdev.",
            file=sys.stderr,
        )
        return 2

    libtexce_src = repo_root / "third_party" / "libtexce" / "src"
    if not libtexce_src.is_dir():
        print(
            f"warning: libtexce sources not found at {libtexce_src}. "
            "If this is a fresh clone, run `git submodule update --init --recursive`.",
            file=sys.stderr,
        )

    native_build_dir = (repo_root / args.native_build_dir).resolve()
    native_compile_db = _ensure_native_compile_db(repo_root, native_build_dir)

    if not args.no_vscode:
        _write_text(
            repo_root / ".vscode" / "extensions.json",
            json.dumps(
                {
                    "recommendations": [
                        "llvm-vs-code-extensions.vscode-clangd",
                        "ms-vscode.cmake-tools",
                        "twxs.cmake",
                    ]
                },
                indent=2,
            )
            + "\n",
        )

        _write_text(
            repo_root / ".vscode" / "settings.json",
            "{\n"
            '  "clangd.arguments": [\n'
            '    "--background-index",\n'
            '    "--clang-tidy",\n'
            '    "--completion-style=detailed",\n'
            '    "--header-insertion=never"\n'
            "  ],\n"
            '  "clangd.fallbackFlags": [\n'
            '    "-std=c++20",\n'
            '    "-include${workspaceFolder}/tools/clangd/ce_shim.hpp",\n'
            '    "-I${workspaceFolder}/core/include",\n'
            '    "-I${workspaceFolder}/shell/include",\n'
            '    "-I${workspaceFolder}/build/clangd/programs/matrix/include",\n'
            '    "-I${workspaceFolder}/third_party/libtexce/src",\n'
            '    "-I${workspaceFolder}/third_party/libtexce/include",\n'
            '    "-idirafter${env:HOME}/CEdev/include"\n'
            "  ],\n"
            '  "C_Cpp.intelliSenseEngine": "disabled",\n'
            '  "C_Cpp.errorSquiggles": "disabled",\n'
            '  "cmake.useCMakePresets": "always",\n'
            '  "cmake.configureOnOpen": false\n'
            "}\n",
        )

    _symlink_or_copy(
        repo_root / "core" / "compile_commands.json",
        native_compile_db,
    )

    _generate_shell_compile_dbs(repo_root, cedev_include, native_compile_db, args.program)

    print("[clangd] Done.")
    print("  - core/compile_commands.json -> build/native/compile_commands.json")
    print(f"  - shell/ and modules/compile_commands.json generated for host clang++ parsing ({args.program})")
    if not args.no_vscode:
        print("  - .vscode/{settings,extensions}.json written")
        print("Next: restart clangd in VSCode (Command Palette: 'Clangd: Restart language server').")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
