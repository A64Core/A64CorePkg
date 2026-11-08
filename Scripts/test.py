#!/usr/bin/env python3
"""Run A64Core host tests with Clang sanitizers."""

from __future__ import annotations

import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    architecture = {"arm64": "AArch64", "aarch64": "AArch64", "x86_64": "X64"}.get(
        platform.machine().lower()
    )
    if sys.platform not in ("darwin", "linux") or architecture is None:
        print("error: Tests require a 64-bit macOS or Linux host.", file=sys.stderr)
        return 1

    prefix = os.environ.get("CLANGDWARF_BIN", "")
    tool = str(Path(prefix).expanduser() / "clang") if prefix else "clang"
    compiler = shutil.which(tool)
    if compiler is None:
        print("error: Put Clang on PATH or set CLANGDWARF_BIN.", file=sys.stderr)
        return 1

    headers = root / "ThirdParty" / "edk2" / "MdePkg" / "Include"
    if not (headers / "Uefi.h").is_file():
        print("error: Prepare the EDK II submodule before running tests.", file=sys.stderr)
        return 1

    library_root = root / "Library"
    suites = [
        ("AcAppleKernelLib", "PatchTests.c", [library_root / "AcAppleKernelLib" / "KernelReader.c", library_root / "AcAppleKernelLib" / "KernelCollection.c", library_root / "AcAppleKernelLib" / "KernelVersion.c", library_root / "AcAppleKernelLib" / "KextPatcher.c"]),
        ("AcApfsLib", "ContainerTests.c", [library_root / "AcApfsLib" / "ApfsName.c", library_root / "AcApfsLib" / "ApfsObject.c", library_root / "AcApfsLib" / "ApfsContainer.c", library_root / "AcApfsLib" / "ApfsBtree.c", library_root / "AcApfsLib" / "ApfsObjectMap.c", library_root / "AcApfsLib" / "ApfsVolume.c"]),
        ("AcPlistLib", "PlistTests.c", [library_root / "AcPlistLib" / "AcPlistLib.c", library_root / "AcPlistLib" / "PlistParser.c", library_root / "AcPlistLib" / "PlistValue.c"]),
    ]
    try:
        with tempfile.TemporaryDirectory(prefix="a64core-tests-") as directory:
            flags = [
                compiler, "-std=c11", "-g", "-O1", "-fshort-wchar", "-fno-builtin",
                "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=undefined",
                "-fno-omit-frame-pointer", "-I", str(root / "Include"),
                "-isystem", str(headers), "-isystem", str(headers / architecture),
            ]
            for library, test, library_sources in suites:
                executable = Path(directory) / Path(test).stem
                sources = [root / "Tests" / library / test]
                sources.extend(library_sources)
                command = [*flags, *(str(source) for source in sources), "-o", str(executable)]
                print(f"Building {library}/{test} with ASan and UBSan...", flush=True)
                subprocess.run(command, check=True)
                subprocess.run([str(executable)], check=True)
        return 0
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nTests interrupted.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
