#!/usr/bin/env python3
"""Format project C/C++ sources with clang-format."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


SOURCE_EXTENSIONS = {".c", ".h", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".hxx"}
EXCLUDED_DIRECTORIES = {".git", "ThirdParty", "Build", "__pycache__", ".cache", ".venv"}


def find_sources(root: Path) -> list[Path]:
    sources = []
    for directory, subdirectories, filenames in os.walk(root, followlinks=False):
        directory = Path(directory)
        subdirectories[:] = sorted(
            name
            for name in subdirectories
            if name not in EXCLUDED_DIRECTORIES and not (directory / name).is_symlink()
        )
        for name in sorted(filenames):
            path = directory / name
            if path.suffix.lower() in SOURCE_EXTENSIONS and not path.is_symlink():
                sources.append(path)
    return sources


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Put clang-format on PATH or set CLANGDWARF_BIN to the LLVM bin directory.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="report formatting differences without modifying files",
    )
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[1]

    try:
        if not (root / ".clang-format").is_file():
            raise FileNotFoundError(f"Formatting configuration is missing: {root / '.clang-format'}")

        prefix = os.environ.get("CLANGDWARF_BIN", "")
        tool = str(Path(prefix).expanduser().resolve() / "clang-format") if prefix else "clang-format"
        executable = shutil.which(tool)
        if executable is None:
            raise FileNotFoundError(
                "clang-format was not found. Put it on PATH or set CLANGDWARF_BIN "
                "to the LLVM bin directory."
            )

        sources = find_sources(root)
        if not sources:
            print("No C/C++ source files found.")
            return 0

        command = [executable, "--style=file", "--fallback-style=none"]
        command.extend(("--dry-run", "--Werror") if arguments.check else ("-i",))
        command.extend(str(path) for path in sources)
        action = "Checking" if arguments.check else "Formatting"
        print(f"{action} {len(sources)} source files...", flush=True)
        subprocess.run(command, cwd=root, check=True)
        print("Formatting check passed." if arguments.check else "Formatting complete.")
        return 0
    except subprocess.CalledProcessError as error:
        return error.returncode
    except OSError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nFormatting interrupted.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
