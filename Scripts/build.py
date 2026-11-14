#!/usr/bin/env python3
"""Build the A64Core AArch64 UEFI applications with EDK II."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


class BuildError(Exception):
    """An environment or workspace check failed."""


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog=(
            "Prepare dependencies manually with: "
            "git submodule update --init --recursive. "
            "Put LLVM tools on PATH or set CLANGDWARF_BIN to their directory; "
            "ld.lld must also be available on PATH."
        ),
    )
    parser.add_argument(
        "-b",
        "--build-target",
        choices=("DEBUG", "RELEASE", "NOOPT"),
        default="DEBUG",
        help="EDK II build target (default: DEBUG)",
    )
    return parser.parse_args()


def check_environment() -> dict[str, str]:
    if sys.version_info < (3, 10):
        raise BuildError("Python 3.10 or later is required.")
    if sys.platform not in ("darwin", "linux"):
        raise BuildError("This build script supports macOS and Linux hosts.")

    environment = os.environ.copy()
    prefix = environment.get("CLANGDWARF_BIN", "")
    if prefix:
        prefix = str(Path(prefix).expanduser().resolve()) + os.sep
        environment["CLANGDWARF_BIN"] = prefix
        environment["PATH"] = prefix + os.pathsep + environment.get("PATH", "")

    for tool in ("git", "bash", "make", "clang", "llvm-ar", "llvm-objcopy", "ld.lld"):
        executable = tool
        if prefix and tool in ("clang", "llvm-ar", "llvm-objcopy"):
            executable = prefix + tool
        if shutil.which(executable, path=environment.get("PATH")) is None:
            raise BuildError(
                f"Required tool not found: {executable}. "
                "Install it manually and check PATH / CLANGDWARF_BIN."
            )

    for name in ("WORKSPACE", "PACKAGES_PATH", "EDK_TOOLS_PATH", "EDK_TOOLS_BIN", "CONF_PATH"):
        environment.pop(name, None)
    environment["PYTHON_COMMAND"] = sys.executable
    return environment


def check_submodule(repository: Path, path: str, recursive: bool = False) -> None:
    command = ["git", "-C", str(repository), "submodule", "status"]
    if recursive:
        command.append("--recursive")
    command.extend(("--", path))
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    if not result.stdout:
        raise BuildError(f"No submodule is registered at {repository / path}.")
    for line in result.stdout.splitlines():
        if line.startswith(" "):
            continue
        reason = {
            "-": "not initialized",
            "+": "checked out at a different commit",
            "U": "has an unresolved merge conflict",
        }.get(line[:1], "has an unexpected status")
        raise BuildError(
            f"Submodule {reason}: {line[1:].strip()}. "
            "Prepare dependencies manually from the repository root with "
            "git submodule update --init --recursive."
        )


def prepare_workspace(root: Path, workspace: Path) -> None:
    for relative in ("edksetup.sh", "BaseTools/BuildEnv"):
        if not (workspace / relative).is_file():
            raise BuildError(f"Required EDK II file is missing: {workspace / relative}")

    link = workspace / "A64CorePkg"
    if link.is_symlink():
        if link.resolve() != root:
            raise BuildError(f"Workspace symlink points to the wrong directory: {link}")
    elif link.exists():
        raise BuildError(f"Workspace path is occupied by a file or directory: {link}")
    else:
        link.symlink_to(os.path.relpath(root, workspace), target_is_directory=True)


def build(workspace: Path, target: str, environment: dict[str, str]) -> None:
    jobs = str(os.cpu_count() or 1)
    # Sourcing and both builds share one shell so the EDK II environment persists.
    script = r"""
set -e
printf '%s\n' 'Initializing EDK II environment...'
. ./edksetup.sh BaseTools
printf '%s\n' 'Building BaseTools...'
"$1" -C BaseTools "-j$2"
# Prefer the native C tools over their shell wrappers after building them.
export PATH="$EDK_TOOLS_PATH/Source/C/bin:$PATH"
printf '%s\n' 'Building A64CorePkg...'
build -p A64CorePkg/A64CorePkg.dsc -a AARCH64 -b "$3" -t CLANGDWARF -n "$2"
"""
    subprocess.run(
        [
            "bash",
            "--noprofile",
            "--norc",
            "-c",
            script,
            "a64core-build",
            shutil.which("make", path=environment.get("PATH")),
            jobs,
            target,
        ],
        cwd=workspace,
        env=environment,
        check=True,
    )


def main() -> int:
    arguments = parse_arguments()
    root = Path(__file__).resolve().parents[1]
    workspace = root / "ThirdParty" / "edk2"

    try:
        print("Checking build environment...", flush=True)
        environment = check_environment()
        print("Checking submodules...", flush=True)
        check_submodule(root, "ThirdParty/edk2")
        check_submodule(root, "ThirdParty/slbounce")
        # BaseTools needs Brotli; MdePkg.dec exposes MIPI Sys-T headers.
        check_submodule(workspace, "BaseTools", recursive=True)
        check_submodule(workspace, "MdePkg/Library/MipiSysTLib/mipisyst", recursive=True)
        print("Preparing workspace...", flush=True)
        prepare_workspace(root, workspace)
        build(workspace, arguments.build_target, environment)

        output = workspace / "Build" / "A64Core" / f"{arguments.build_target}_CLANGDWARF" / "AARCH64"
        for name in ("Bootstrap.efi", "A64Core.efi"):
            artifact = output / name
            if not artifact.is_file():
                raise BuildError(f"Expected build output is missing: {artifact}")
            print(f"Built: {artifact}", flush=True)
        return 0
    except subprocess.CalledProcessError as error:
        if error.stderr:
            print(error.stderr.strip(), file=sys.stderr)
        print(f"error: Command failed with exit status {error.returncode}.", file=sys.stderr)
        return error.returncode
    except (BuildError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nBuild interrupted.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
