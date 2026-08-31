##---------------------------------------------------------------------------
## Climatology Plugin — Modernized CMake Build System
##---------------------------------------------------------------------------
## Architecture:
##   1. Define project + metadata
##   2. Load PluginSetup.cmake  (creates PACKAGE_NAME + target)
##   3. Load PluginConfigure.cmake (detects wxWidgets, OpenGL, CURL, etc.)
##   4. Add plugin sources
##   5. Add include + link directories
##   6. Add opencpn-libs subdirectories
##   7. Link plugin against opencpn-libs
##   8. Install, localize, package
##
## IMPORTANT:
##   - PluginSetup.cmake MUST run before any target_* commands.
##   - PACKAGE must be defined before PluginSetup.cmake.
##   - All target_include_directories() must use PRIVATE or BEFORE PRIVATE.
##---------------------------------------------------------------------------

##---------------------------------------------------------------------------
## Climatology Plugin — Modernized CMake Build System (Final Clean Version)
##---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.15)

# ---------------------------------------------------------------------------
# 1. Plugin identity — MUST be defined before PluginSetup.cmake
# ---------------------------------------------------------------------------
project(
    climatology_pi
    LANGUAGES CXX
    VERSION 0.0.0.0   # temporary placeholder
)

# PACKAGE / PACKAGE_NAME are used by PluginSetup.cmake and PluginPackage.cmake
set(PACKAGE "climatology")
set(PACKAGE_NAME "${PACKAGE}_pi")

# ---------------------------------------------------------------------------
# 2. wxWidgets paths — manual tree, no wxWidgetsConfig.cmake
#    We DO NOT use find_package(wxWidgets) here.
#    Instead, we point directly at the ocpn_wxWidgets-3.2.2.1 build:
#      - include:  include, include/msvc, lib/vc_dll/mswu
#      - libs:     lib/vc_dll
# ---------------------------------------------------------------------------
#set(wxWidgets_ROOT "C:/Users/fcgle/source/ocpn_wxWidgets-3.2.2.1")
# wx import libraries directory
#set(OCPN_WXWIDGETS_LIB_DIR "${wxWidgets_ROOT}/lib/vc_dll")
# wx include dirs (mswu = Unicode Release)
#set(OCPN_WXWIDGETS_INCLUDE_DIR "${wxWidgets_ROOT}/include")
# wx include GL
#set(OCPN_WXWIDGETS_GL_INCLUDE_DIR "${wxWidgets_ROOT}/lib/vc_dll/mswu")

set(CMLOC "CMakeLists: ")
#message(STATUS "wxWidgets include: ${OCPN_WXWIDGETS_INCLUDE_DIR}")
#message(STATUS "wxWidgets GL include: ${OCPN_WXWIDGETS_GL_INCLUDE_DIR}")
#message(STATUS "wxWidgets lib dir: ${OCPN_WXWIDGETS_LIB_DIR}")

##---------------------------------------------------------------------------
## 3. CMake policies + compiler settings  
##---------------------------------------------------------------------------
if(COMMAND cmake_policy)
    cmake_policy(SET CMP0048 NEW)   # project() VERSION handling
    cmake_policy(SET CMP0076 NEW)   # target_sources() behavior
    cmake_policy(SET CMP0077 NEW)   # option() honors normal variables
    cmake_policy(SET CMP0079 NEW)   # target_link_libraries() behavior
endif()

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)   # MSVC runtime library selection
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Explicitly request C++17 for MSVC; keeps CRT and STL consistent
    add_compile_options(/std:c++17)
endif()

set(CMLOC "CMakeLists: ")

# ---------------------------------------------------------------------------
# 4. Make cmake/ directory visible (PluginSetup + PluginConfigure)
# ---------------------------------------------------------------------------
# PluginSetup.cmake and PluginConfigure.cmake live in cmake/.
# They must be discoverable via CMAKE_MODULE_PATH.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

message(STATUS "MODULE PATH: ${CMAKE_MODULE_PATH}")

# Force plugins to install into a local staging directory
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")

set(CMAKE_VERBOSE_MAKEFILE OFF)

##---------------------------------------------------------------------------
## 5. Plugin metadata (used by PluginSetup + PluginConfigure)
##---------------------------------------------------------------------------
set(VERBOSE_NAME "Climatology")
set(COMMON_NAME  "Climatology")
set(TITLE_NAME   "climatology")

set(SHORT_DESCRIPTION "Climatology PlugIn: 32 years of monthly NOAA Data")
set(LONG_DESCRIPTION  "Climatology provides overlay capabilities for historic weather data...")

set(VERSION_MAJOR "1")
set(VERSION_MINOR "6")
set(VERSION_PATCH "48")
set(VERSION_TWEAK "0")
set(VERSION_DATE  "08/27/2026")

# Reconstruct the Project full version number
set(PROJECT_VERSION
    "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_TWEAK}"
)

set(OCPN_MIN_VERSION "ov58")
set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "20")

set(WX_VER "32")
set(WX_VER_PATCH "8")

set(PARENT "opencpn")

# ---------------------------------------------------------------------------
# 6. Cloudsmith / CI Metadata (required for packaging)
# ---------------------------------------------------------------------------
set(PROD  "prod")
set(BETA  "beta")
set(ALPHA "alpha")

set(CLOUDSMITH_BASE_REPOSITORY "climatology")
set(CLOUDSMITH_USER            "opencpn")

set(XML_INFO_URL   "https://opencpn.org/OpenCPN/plugins/climatology.html")
set(XML_SUMMARY    "${SHORT_DESCRIPTION}")
set(XML_DESCRIPTION "${LONG_DESCRIPTION}")

# ---------------------------------------------------------------------------
# 7. Graphics / OpenGL Options
# ---------------------------------------------------------------------------
# OpenGL is required for rendering overlays.
# PluginConfigure.cmake uses USE_GL to decide whether to enable GL.
option(USE_GL "Enable OpenGL support (required)" ON)
message(STATUS "CMakeLists: OpenGL enabled: ${USE_GL}")

# Enable SVG icons (used by many plugins)
option(PLUGIN_USE_SVG "Use SVG graphics" ON)

# OpenCPN core prefers legacy OpenGL (compatibility profile)
set(OpenGL_GL_PREFERENCE "LEGACY")

# Default build type (Release unless explicitly set)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
message(STATUS "CMakeLists: Build type: ${CMAKE_BUILD_TYPE}")

##---------------------------------------------------------------------------
## 8. Add opencpn-libs FIRST (MSBuild requires before setup & before Linking
##---------------------------------------------------------------------------
# These provide ocpn::api, ocpn::tinyxml, ocpn::zlib, etc.
# They must be added before we link the plugin target.
add_subdirectory(opencpn-libs/api-20)
add_subdirectory(opencpn-libs/tinyxml)

if(WIN32 AND MSVC)
    add_subdirectory(opencpn-libs/zlib)
endif()

message(STATUS "MODULE PATH: ${CMAKE_MODULE_PATH}")

# ---------------------------------------------------------------------------
# 9. PluginSetup — creates ${PACKAGE_NAME} target
# ---------------------------------------------------------------------------
# PluginSetup.cmake:
#   - defines the plugin target (${PACKAGE_NAME})
#   - sets packaging names
#   - wires basic properties used by PluginInstall/PluginPackage.
# ---------------------------------------------------------------------------
include(PluginSetup)

# At this point, ${PACKAGE_NAME} (climatology_pi) is a valid target.


# ---------------------------------------------------------------------------
# 12. PluginConfigure — version.h, packaging metadata, etc.
#     Where wxWidgets linking happens.
# ---------------------------------------------------------------------------

include(PluginConfigure)

# ---------------------------------------------------------------------------
# 10. Add plugin/API include dirs (but not wx includes) BEFORE any sources are added
# ---------------------------------------------------------------------------
# Use BEFORE PRIVATE to ensure our headers win over any transitive ones and
# so they are searched ahead of any system includes, but remain target-local.
# PluginSetup.cmake will handle wx includes.
target_include_directories(${PACKAGE_NAME} BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/api-${OCPN_API_VERSION_MINOR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # DO NOT repeat wx includes here; PluginSetup already added them
)

# ---------------------------------------------------------------------------
# 11. Add Curl + OpenGL
# ---------------------------------------------------------------------------

# Fallback path for curl
set(CMAKE_PREFIX_PATH "C:/Users/fcgle/source/vcpkg/installed/x86-windows")
# Use vcpkg curl (Schannel backend on Windows)
find_package(CURL CONFIG REQUIRED)

target_link_libraries(${PACKAGE_NAME} PRIVATE
    CURL::libcurl
    opengl32
    glu32
)


# ---------------------------------------------------------------------------
# 11. Add Plugin sources
# ---------------------------------------------------------------------------
set(SRC_LIST
    src/climatology_pi.cpp
    src/climatology_shaders.cpp

    src/ClimatologyConfigDialog.cpp
    src/ClimatologyConstants.cpp
    src/ClimatologyDataModel.cpp
    src/ClimatologyDialog.cpp
    src/ClimatologyOverlay.cpp
    src/ClimatologyOverlayFactory.cpp

    src/DownloadManager.cpp
    src/ManifestLoader.cpp
    src/OverlayMapping.cpp
    src/RenderGridStub.cpp
    src/icons.cpp
    src/IsoBarMap.cpp
    src/zuFile.cpp
    src/UnifiedGrid.cpp
    src/ClimatologyColorGrid.cpp
)

target_sources(${PACKAGE_NAME} PRIVATE ${SRC_LIST})

# no add_library here, that stays in PluginSetup



# ---------------------------------------------------------------------------
# 13. Third-party libraries (ocpn::api curl, glew, bzip2)
# ---------------------------------------------------------------------------

# Hard-coded vcpkg paths (stable, reliable)
set(GLEW_ROOT  "C:/Users/fcgle/source/vcpkg/installed/x86-windows")
set(BZIP2_ROOT "C:/Users/fcgle/source/vcpkg/installed/x86-windows")

target_include_directories(${PACKAGE_NAME} PRIVATE
    ${GLEW_ROOT}/include
    ${BZIP2_ROOT}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/win_curl/include   # curl headers only
)

target_link_libraries(${PACKAGE_NAME} PRIVATE
    ocpn::api
    ocpn::tinyxml
    ocpn::zlib

    ${GLEW_ROOT}/lib/glew32.lib
    ${BZIP2_ROOT}/lib/bz2.lib
)

# ---------------------------------------------------------------------------
# 14. Add wx libraries wxWidgets Block (previously)
# ---------------------------------------------------------------------------
if(WIN32 AND MSVC)
    # We already have include + lib dirs from PluginSetup.cmake
    # Just add the actual wx libraries to the plugin target.
	# Ensure wx lib directory is on the link search path
    message(STATUS "${CMLOC}Linking wxWidgets libs to target: ${PACKAGE_NAME}")

	# Attach wx libraries to the plugin target
    target_link_libraries(${PACKAGE_NAME} PRIVATE
        wxbase32u
        wxbase32u_xml
        wxexpat
        wxjpeg
        wxpng
        wxzlib

        wxmsw32u_core
        wxmsw32u_adv
        wxmsw32u_html
        wxmsw32u_aui
        wxmsw32u_xrc
        wxmsw32u_gl
    )

    get_target_property(WX_LINK_LIBRARIES ${PACKAGE_NAME} LINK_LIBRARIES)
    message(STATUS "${CMLOC}WX LINK LIBRARIES: ${WX_LINK_LIBRARIES}")
endif()


# ---------------------------------------------------------------------------
# 15. Install, Localization, Packaging
# ---------------------------------------------------------------------------
include(PluginInstall)
include(PluginLocalization)
include(PluginPackage)

# ---------------------------------------------------------------------------
# 16. Windows Runtime Dependencies
# ---------------------------------------------------------------------------
if(WIN32)
	#   install(FILES "${CMAKE_SOURCE_DIR}/cacert.pem"
	#      DESTINATION plugins/climatology_pi)

	# Copy data files into the plugin runtime directory
	file(COPY
		${CMAKE_CURRENT_SOURCE_DIR}/data/manifest.json
		${CMAKE_CURRENT_SOURCE_DIR}/data/cacert.pem
#		DESTINATION ${PLUGIN_OUTPUT_DIR}/data
		DESTINATION ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/plugins/climatology_pi/data
	)


	# Install manifest.json and cacert.pem into the plugin data directory
	install(FILES
		${CMAKE_CURRENT_SOURCE_DIR}/data/manifest.json
		${CMAKE_CURRENT_SOURCE_DIR}/data/cacert.pem
		DESTINATION ${CMAKE_INSTALL_PREFIX}/plugins/climatology_pi/data
	)
    install(FILES "C:/Users/fcgle/source/vcpkg/installed/x86-windows/bin/glew32.dll"
        DESTINATION plugins/climatology_pi		
	)
	
endif()
