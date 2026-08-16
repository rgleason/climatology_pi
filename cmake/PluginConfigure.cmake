# ---------------------------------------------------------------------------
# Author:      Jon Gough (Based on the work of Sean D'Epagnier and Pavel Kalian)
# Copyright:   2019 License:     GPLv3+
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# PluginConfigure.cmake — Modernized for 2026
# ---------------------------------------------------------------------------

set(SAVE_CMLOC ${CMLOC})
set(CMLOC "PluginConfigure: ")

message(STATUS "${CMLOC}Configuring ${PACKAGE_NAME}")

# ---------------------------------------------------------------------------
# 1. OpenGL Detection (non‑Windows)
# ---------------------------------------------------------------------------

if(NOT WIN32 AND NOT QT_ANDROID)
    if(USE_GL)
        message(STATUS "${CMLOC}Finding OpenGL")
        find_package(OpenGL REQUIRED)
        include_directories(${OPENGL_INCLUDE_DIR})
        add_definitions(-DocpnUSE_GL)
        message(STATUS "${CMLOC}OpenGL Include: ${OPENGL_INCLUDE_DIR}")
        message(STATUS "${CMLOC}OpenGL Libs: ${OPENGL_LIBRARIES}")
    else()
        message(STATUS "${CMLOC}OpenGL disabled")
    endif()
endif()

# ---------------------------------------------------------------------------
# 2. wxWidgets Detection (non‑Windows)
# ---------------------------------------------------------------------------

if(NOT WIN32 AND NOT QT_ANDROID)
    message(STATUS "${CMLOC}Detecting wxWidgets components: ${wxWidgets_USE_LIBS}")
    find_package(wxWidgets REQUIRED COMPONENTS ${wxWidgets_USE_LIBS})
    include_directories(${wxWidgets_INCLUDE_DIRS})
    message(STATUS "${CMLOC}wxWidgets Include: ${wxWidgets_INCLUDE_DIRS}")
    message(STATUS "${CMLOC}wxWidgets Libraries: ${wxWidgets_LIBRARIES}")
endif()

# ---------------------------------------------------------------------------
# 3. Packaging Metadata (plugin.xml, pkg_version.sh, CPack options)
# ---------------------------------------------------------------------------

message(STATUS "${CMLOC}Generating packaging metadata")

configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/in-files/plugin.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGING_NAME_XML}.xml
)

configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/in-files/pkg_version.sh.in
    ${CMAKE_CURRENT_BINARY_DIR}/pkg_version.sh
)

configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/in-files/cloudsmith-upload.sh.in
    ${CMAKE_CURRENT_BINARY_DIR}/cloudsmith-upload.sh @ONLY
)

configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/in-files/PluginCPackOptions.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/PluginCPackOptions.cmake @ONLY
)

set(CMLOC ${SAVE_CMLOC})
