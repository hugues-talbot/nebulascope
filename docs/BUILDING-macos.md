# Building on macOS (Apple Silicon / Homebrew)

Tested with Homebrew under `/opt/homebrew`.

## 1. Dependencies from Homebrew

```sh
brew install cmake ninja pkg-config qt cfitsio ccfits
```

That covers Qt6, CFITSIO, and CCfits. **libXISF is not in Homebrew** — build it
yourself (next step).

## 2. Build and install libXISF

```sh
git clone https://gitea.nouspiro.space/nou/libXISF
cd libXISF
cmake -B build -DCMAKE_INSTALL_PREFIX=/opt/homebrew
cmake --build build
cmake --install build
cd ..
```

This installs `/opt/homebrew/lib/libXISF.dylib` and `/opt/homebrew/include/libxisf.h`.

## 3. Configure and build NebulaScope

From the repository root:

```sh
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
./build/nebulascope
```

`-DCMAKE_PREFIX_PATH=/opt/homebrew` is the only flag you need — it lets CMake
find Qt6 (`/opt/homebrew/lib/cmake`), CCfits (`find_library`/`find_path`), and
adds `/opt/homebrew/lib/pkgconfig` to pkg-config so CFITSIO is found. During
configure you should see:

```
-- libXISF: /opt/homebrew/lib/libXISF.dylib  (headers: /opt/homebrew/include)
```

(That line prints at **configure** time, not during `cmake --build`.)

## Troubleshooting

**`ld: library 'XISF' not found`** — you're configuring with an old CMakeLists
that links the bare name `XISF` (→ `-lXISF`), and `/opt/homebrew/lib` is not a
default linker search path on Apple Silicon. This repo's CMake uses
`find_library(XISF_LIB …)` and links the **absolute** path, which avoids it.
Reconfigure clean: `rm -rf build && cmake -B build …`.

**`pkg-config` can't find cfitsio** — add it to the search path explicitly:

```sh
export PKG_CONFIG_PATH=/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH
```

**`no member named 'Zstd' in CompressionCodec`** — libXISF implements only
None/Zlib/LZ4/LZ4HC (no zstd, even on master). The writer maps the `Zstd`
option to `LZ4HC` as a fallback, so this should already be handled; if you see
it, you're compiling an older copy of `src/io/XisfWriter.cpp`.

**Undefined symbols for `lz4_*` / `zlib` / `pugixml` at link time** — your
libXISF was built static. Uncomment the `find_library` lines in the top-level
`CMakeLists.txt` and the matching entry in `target_link_libraries` in
`src/CMakeLists.txt`.

## Verify the link

```sh
otool -L build/nebulascope | grep -i xisf
```

Should list `libXISF.*.dylib`.
