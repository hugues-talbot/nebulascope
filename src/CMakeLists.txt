# Targets for NebulaScope. Project setup + dependency discovery live in the
# top-level CMakeLists.txt; the variables it defines are visible here.

# ---- core + io + render (no widgets) --------------------------------------
add_library(astro_core STATIC
    core/Convert.cpp
    core/ImageStats.cpp
    core/Stretch.cpp
    io/ImageReader.cpp
    io/FitsReader.cpp
    io/XisfReader.cpp
    io/ImageWriter.cpp
    io/FitsWriter.cpp
    io/XisfWriter.cpp
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
    ${CCFITS_LIB} ${CFITSIO_LIBRARIES}
    ${XISF_LIB}
    # ${ZLIB_LIB} ${LZ4_LIB} ${PUGIXML_LIB}   # if libXISF is static
)

# ---- application (widgets + ui) -------------------------------------------
add_executable(nebulascope
    app/main.cpp
    ui/ImageView.cpp
    ui/HistogramView.cpp
    ui/HistogramPanel.cpp
    ui/MainWindow.cpp
)
target_link_libraries(nebulascope PRIVATE
    astro_core
    Qt6::Widgets
)
