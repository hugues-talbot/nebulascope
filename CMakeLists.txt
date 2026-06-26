cmake_minimum_required(VERSION 3.20)
project(NebulaScope LANGUAGES CXX)

# ---- project-wide settings -------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)            # for Q_OBJECT classes (StretchModel, widgets)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- dependencies ----------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# FITS: CFITSIO (pkg-config) + CCfits (C++ wrapper)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CFITSIO REQUIRED cfitsio)
find_path(CCFITS_INCLUDE_DIR NAMES CCfits/CCfits)
find_library(CCFITS_LIB NAMES CCfits)
if(NOT CCFITS_INCLUDE_DIR OR NOT CCFITS_LIB)
    message(FATAL_ERROR "CCfits not found. On macOS: brew install ccfits cfitsio, "
                        "and configure with -DCMAKE_PREFIX_PATH=/opt/homebrew")
endif()

# XISF: libXISF ships no CMake package config, so locate it directly. Works for
# a shared .dylib/.so or a static .a. Override if it landed somewhere unusual:
#   -DXISF_INCLUDE_DIR=/path/include  -DXISF_LIB=/path/libXISF.dylib
find_path(XISF_INCLUDE_DIR NAMES libxisf.h
    HINTS /opt/homebrew/include /usr/local/include)
find_library(XISF_LIB NAMES XISF xisf libXISF
    HINTS /opt/homebrew/lib /usr/local/lib)
if(NOT XISF_INCLUDE_DIR OR NOT XISF_LIB)
    message(FATAL_ERROR "libXISF not found. Build it from "
                        "https://gitea.nouspiro.space/nou/libXISF into your prefix, "
                        "or pass -DXISF_INCLUDE_DIR=... and -DXISF_LIB=...")
endif()
message(STATUS "libXISF: ${XISF_LIB}  (headers: ${XISF_INCLUDE_DIR})")

# If libXISF was built STATIC, its compression deps aren't pulled in
# transitively. Uncomment any the linker reports as undefined symbols and add
# them to astro_core in src/CMakeLists.txt:
# find_library(ZLIB_LIB z)
# find_library(LZ4_LIB lz4)
# find_library(PUGIXML_LIB pugixml)

add_subdirectory(src)
