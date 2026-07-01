# Building on Linux

Tested on Debian/Ubuntu-style distros; adapt package names for your distro.

## 1. Dependencies

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
                 qt6-base-dev libcfitsio-dev libccfits-dev zlib1g-dev liblz4-dev
```

That covers Qt6, CFITSIO, and CCfits. **libXISF is not packaged** — build it
next. (Qt's image plugins for JPEG/PNG/TIFF come with `qt6-base-dev`.)

## 2. Build and install libXISF

```sh
git clone https://gitea.nouspiro.space/nou/libXISF
cd libXISF
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build
sudo ldconfig            # refresh the shared-library cache
cd ..
```

Installs `/usr/local/lib/libXISF.so` and `/usr/local/include/libxisf.h`.

## 3. Configure and build NebulaScope

From the repository root:

```sh
cmake -B build -G Ninja
cmake --build build
./build/src/nebulascope                  # empty session
./build/src/nebulascope *.fits           # open a set
./build/src/nebulascope --list set.txt   # reload a saved list
./build/src/nebulascope --help
```

CFITSIO is found via pkg-config; CCfits and libXISF via `find_library`. If Qt6
is in a non-standard location, add `-DCMAKE_PREFIX_PATH=/path/to/qt6`.

At configure time you should see:

```
-- CFITSIO target: PkgConfig::CFITSIO
-- libXISF: /usr/local/lib/libXISF.so  (headers: /usr/local/include)
```

## Troubleshooting

**`libXISF.so: cannot open shared object file` at runtime** — the loader can't
find it. Run `sudo ldconfig`, or set `LD_LIBRARY_PATH=/usr/local/lib`.

**Undefined symbols for `lz4` / `zlib` / `pugixml` at link** — your libXISF is
static. Uncomment the `find_library` lines in the top-level `CMakeLists.txt` and
the matching entry in `src/CMakeLists.txt`.

**Qt6 not found** — install `qt6-base-dev` (Debian/Ubuntu) or the equivalent,
or point `-DCMAKE_PREFIX_PATH` at your Qt install.
