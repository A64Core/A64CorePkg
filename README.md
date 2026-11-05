# A64Core

A64Core is an AArch64 UEFI bootloader project built on EDK II. It aims to support booting macOS on general-purpose AArch64 UEFI platforms.

## Building

Building requires:

- Python 3.10 or later
- Git
- Bash
- Make
- A native C/C++ toolchain and development headers for BaseTools
- LLVM tools: `clang`, `llvm-ar`, and `llvm-objcopy`
- LLD: `ld.lld`

The build script accepts macOS and Linux hosts. Builds have been verified on macOS.

Make the required tools available through `PATH`. If LLVM is installed outside `PATH`, set `CLANGDWARF_BIN` to its binary directory:

```sh
export CLANGDWARF_BIN="/path/to/llvm/bin/"
```

If LLD is installed separately, add its binary directory to `PATH`.

Then build the project:
```sh
git submodule update --init --recursive
python3 Scripts/build.py
# python3 Scripts/build.py -b RELEASE
# python3 Scripts/build.py -b NOOPT
```

## Code Formatting

Format project C/C++ sources using `.clang-format`:

```sh
python3 Scripts/format.py
```

Check formatting without modifying files:

```sh
python3 Scripts/format.py --check
```

The script requires `clang-format`, located through `PATH` or `CLANGDWARF_BIN`. It excludes third-party dependencies, generated directories, and symbolic links.

The repository also includes `.clang-tidy` for static analysis configuration.

## License

BSD 3-Clause License. See [LICENSE](LICENSE).

Third-party dependencies are distributed under their respective licenses.