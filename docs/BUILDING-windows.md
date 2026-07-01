# Building on Windows (MSVC + vcpkg)

The recommended route on Windows is **vcpkg** for dependencies and the Visual
Studio (MSVC) toolchain. There is no pkg-config requirement — the build falls
back to CMake package configs / `find_library`.

## 1. Toolchain

- Visual Studio 2022 (Desktop C++ workload) or the Build Tools.
- [vcpkg](https://github.com/microsoft/vcpkg) bootstrapped somewhere, e.g.
  `C:\vcpkg`.

## 2. Dependencies via vcpkg

```bat
vcpkg install qtbase cfitsio zlib lz4
```

`qtbase` brings Qt6 (with the JPEG/PNG/TIFF image plugins). `cfitsio` provides a
CMake config package that this project picks up automatically.

**CCfits and libXISF are not in vcpkg** — build them yourself against the same
toolchain and install into a prefix you pass to CMake (below). For each:

```bat
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
               -DCMAKE_INSTALL_PREFIX=C:/local
cmake --build build --config Release
cmake --install build --config Release
```

(CCfits source: HEASARC. libXISF: https://gitea.nouspiro.space/nou/libXISF)

## 3. Configure and build NebulaScope

From the repository root:

```bat
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
               -DCMAKE_PREFIX_PATH=C:/local
cmake --build build --config Release
build\src\Release\nebulascope.exe
```

- `cfitsio` is found through its vcpkg CMake config (the build auto-detects the
  `cfitsio` / `CFITSIO::CFITSIO` target — no pkg-config needed).
- `CCfits` and `libXISF` are found via `find_library`/`find_path` under
  `CMAKE_PREFIX_PATH`. If they sit elsewhere, pass them explicitly:
  `-DCCFITS_INCLUDE_DIR=... -DCCFITS_LIB=... -DXISF_INCLUDE_DIR=... -DXISF_LIB=...`

At configure time you should see a line like:

```
-- CFITSIO target: cfitsio
-- libXISF: C:/local/lib/XISF.lib  (headers: C:/local/include)
```

## 4. Running

Qt apps on Windows need their DLLs alongside the `.exe`. The simplest way:

```bat
windeployqt build\src\Release\nebulascope.exe
```

That copies the required Qt DLLs and image-format plugins next to the binary.
Also ensure `cfitsio.dll`, `CCfits.dll`, and `XISF.dll` (and zlib/lz4) are on
`PATH` or copied beside the `.exe`.

Command-line use is the same as elsewhere:

```bat
nebulascope.exe *.fits
nebulascope.exe --list set.txt
nebulascope.exe --help
```

## Troubleshooting

**`The application was unable to start (0xc000007b)` / missing DLLs** — run
`windeployqt`, and copy the cfitsio/CCfits/XISF DLLs next to the `.exe`.

**`cfitsio` not found** — make sure you passed the vcpkg toolchain file. Without
it, CMake can't see vcpkg-installed packages.

**Link errors for lz4/zlib/pugixml** — your libXISF is static; uncomment the
`find_library` lines in the top-level `CMakeLists.txt` and add them in
`src/CMakeLists.txt`.
