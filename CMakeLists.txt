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
if(MSVC)
    # MSVC's <cmath> hides M_PI behind _USE_MATH_DEFINES.
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
endif()

# ---- dependencies ----------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

# FITS: CFITSIO (C lib) + CCfits (C++ wrapper).
# Resolve CFITSIO to actual link items in ${CFITSIO_LINK_LIBS} (+ include dirs in
# ${CFITSIO_INC_DIRS}). Prefer pkg-config FULL PATHS (so no -L search dir is
# needed), then a vcpkg/CMake config package, then a manual find_library. This
# works on Linux, macOS/Homebrew, and MSVC/vcpkg alike.
find_package(PkgConfig QUIET)
set(CFITSIO_LINK_LIBS "")
set(CFITSIO_INC_DIRS "")
if(PkgConfig_FOUND)
    pkg_check_modules(PC_CFITSIO QUIET cfitsio)
    if(PC_CFITSIO_FOUND)
        if(PC_CFITSIO_LINK_LIBRARIES)
            set(CFITSIO_LINK_LIBS ${PC_CFITSIO_LINK_LIBRARIES})    # absolute paths
        else()
            set(CFITSIO_LINK_LIBS ${PC_CFITSIO_LIBRARIES})         # bare names ...
            link_directories(${PC_CFITSIO_LIBRARY_DIRS})           # ... + their dir
        endif()
        set(CFITSIO_INC_DIRS ${PC_CFITSIO_INCLUDE_DIRS})
    endif()
endif()
if(NOT CFITSIO_LINK_LIBS)
    find_package(CFITSIO CONFIG QUIET)                             # vcpkg config
    if(TARGET CFITSIO::CFITSIO)
        set(CFITSIO_LINK_LIBS CFITSIO::CFITSIO)
    elseif(TARGET cfitsio)
        set(CFITSIO_LINK_LIBS cfitsio)
    endif()
endif()
if(NOT CFITSIO_LINK_LIBS)
    find_path(CFITSIO_INCLUDE_DIR NAMES fitsio.h PATH_SUFFIXES cfitsio
        HINTS /opt/homebrew/include /usr/local/include)
    find_library(CFITSIO_LIB NAMES cfitsio libcfitsio
        HINTS /opt/homebrew/lib /usr/local/lib)
    if(NOT CFITSIO_INCLUDE_DIR OR NOT CFITSIO_LIB)
        message(FATAL_ERROR "CFITSIO not found. Install it (brew/apt/vcpkg) or pass "
                            "-DCFITSIO_INCLUDE_DIR=... -DCFITSIO_LIB=...")
    endif()
    set(CFITSIO_LINK_LIBS ${CFITSIO_LIB})
    set(CFITSIO_INC_DIRS ${CFITSIO_INCLUDE_DIR})
endif()
if(NOT CFITSIO_LINK_LIBS)
    message(FATAL_ERROR "CFITSIO could not be resolved to a link library.")
endif()
message(STATUS "CFITSIO link: ${CFITSIO_LINK_LIBS}")

find_path(CCFITS_INCLUDE_DIR NAMES CCfits/CCfits)
find_library(CCFITS_LIB NAMES CCfits)
if(NOT CCFITS_INCLUDE_DIR OR NOT CCFITS_LIB)
    message(FATAL_ERROR "CCfits not found. macOS: brew install ccfits cfitsio (configure "
                        "with -DCMAKE_PREFIX_PATH=/opt/homebrew). Linux: apt install libccfits-dev. "
                        "Windows: build CCfits, or pass -DCCFITS_INCLUDE_DIR=... -DCCFITS_LIB=...")
endif()

# XISF: libXISF ships no CMake package config, so locate it directly. Works for
# a shared lib (.dylib/.so/.dll) or a static .a/.lib. Override if it landed
# somewhere unusual:
#   -DXISF_INCLUDE_DIR=/path/include  -DXISF_LIB=/path/to/XISF{.dylib,.so,.lib}
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

# ---- Linux install + .deb packaging (cpack -G DEB) --------------------------
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS nebulascope RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # Bundle libXISF (not packaged by Debian/Ubuntu) as a private library.
    get_filename_component(XISF_LIB_REAL "${XISF_LIB}" REALPATH)
    file(GLOB XISF_LIB_FILES "${XISF_LIB_REAL}" "${XISF_LIB}*")
    install(FILES ${XISF_LIB_FILES} DESTINATION ${CMAKE_INSTALL_LIBDIR}/nebulascope)
    set_target_properties(nebulascope PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/nebulascope")
    install(FILES packaging/nebulascope.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES icon/NebulaScope.iconset/icon_256x256.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME nebulascope.png)

    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGE_NAME "nebulascope")
    set(CPACK_PACKAGE_VERSION "0.83")           # keep in step with src/app/AppInfo.h
    set(CPACK_PACKAGE_CONTACT "Hugues Talbot")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Astronomical image inspector (FITS/XISF) with precise histogram control")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    # Let dpkg-shlibdeps compute Qt/cfitsio/ccfits dependencies for the exact
    # distro release this .deb is built on (avoids the libcfitsio10-vs-t64 mess).
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    include(CPack)
endif()
