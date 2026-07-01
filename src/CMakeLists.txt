# Targets for NebulaScope. Project setup + dependency discovery live in the
# top-level CMakeLists.txt; the variables it defines are visible here.

# ---- core + io + render (no widgets) --------------------------------------
add_library(astro_core STATIC
    core/Convert.cpp
    core/ImageStats.cpp
    core/Stretch.cpp
    core/Colormap.cpp
    core/Transform.cpp
    io/ImageReader.cpp
    io/FitsReader.cpp
    io/XisfReader.cpp
    io/QtImageReader.cpp
    io/ImageWriter.cpp
    io/FitsWriter.cpp
    io/XisfWriter.cpp
    io/TiffWriter.cpp
    render/StretchModel.cpp
    render/DisplayRenderer.cpp
)
target_include_directories(astro_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}            # so #include "core/..." / "io/..." resolve
    ${CFITSIO_INCLUDE_DIRS}
    ${CCFITS_INCLUDE_DIR}
    ${XISF_INCLUDE_DIR}
)
target_link_libraries(astro_core PUBLIC
    Qt6::Core Qt6::Gui
    ${CCFITS_LIB} ${CFITSIO_TARGET}
    ${XISF_LIB}
    # ${ZLIB_LIB} ${LZ4_LIB} ${PUGIXML_LIB}   # if libXISF is static
)

# ---- application (widgets + ui) -------------------------------------------
set(CMAKE_AUTORCC ON)

# macOS dock icon. Generate it once with:  ./icon/make_icns.sh
# If it isn't present we still build (just without a custom icon).
set(APP_ICON_MACOS ${CMAKE_SOURCE_DIR}/icon/NebulaScope.icns)
set(APP_ICON_SOURCES "")
if(APPLE AND EXISTS ${APP_ICON_MACOS})
    set(APP_ICON_SOURCES ${APP_ICON_MACOS})
endif()

add_executable(nebulascope
    app/main.cpp
    app/app.qrc
    ui/ImageView.cpp
    ui/HistogramView.cpp
    ui/HistogramPanel.cpp
    ui/ColorBar.cpp
    ui/InfoPanel.cpp
    ui/MainWindow.cpp
    ${APP_ICON_SOURCES}
)

if(APPLE)
    # Build NebulaScope.app with a proper Info.plist (+ dock icon if present).
    if(EXISTS ${APP_ICON_MACOS})
        set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        set_target_properties(nebulascope PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "NebulaScope.icns")
    endif()
    set_target_properties(nebulascope PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "NebulaScope"
        MACOSX_BUNDLE_BUNDLE_NAME "NebulaScope"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.nebulascope.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
endif()

target_link_libraries(nebulascope PRIVATE
    astro_core
    Qt6::Widgets
)
