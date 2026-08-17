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

cmake_minimum_required(VERSION 3.15)

set(CMLOC "CMakeLists: ")

##---------------------------------------------------------------------------
## CMake Policies (modern defaults)
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

##---------------------------------------------------------------------------
## Compiler Settings
##---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/std:c++17)
endif()

##---------------------------------------------------------------------------
## Project + Plugin Identity
##---------------------------------------------------------------------------
# Keep project name very short or it will conflict with MSVC compiling
project(climatology LANGUAGES CXX)

## Tell Visual Studio   “Build the plugin target, not the auto‑generated one.”
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${PACKAGE_NAME})
set_property(GLOBAL PROPERTY USE_FOLDERS ON)


# PACKAGE is used by PluginSetup.cmake to compute PACKAGE_NAME
set(PACKAGE "climatology")

# Make cmake/ directory visible to include()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_VERBOSE_MAKEFILE OFF)

##---------------------------------------------------------------------------
## Plugin Metadata
##---------------------------------------------------------------------------
set(VERBOSE_NAME "Climatology")
set(COMMON_NAME  "Climatology")
set(TITLE_NAME   "climatology")

set(SHORT_DESCRIPTION "Climatology PlugIn: 32 years of monthly NOAA Data")
set(LONG_DESCRIPTION  "Climatology provides overlay capabilities for historic weather data...")

set(VERSION_MAJOR "1")
set(VERSION_MINOR "6")
set(VERSION_PATCH "47")
set(VERSION_TWEAK "0")
set(VERSION_DATE  "06/30/2026")

set(OCPN_MIN_VERSION "ov58")
set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "20")

set(WX_VER "32")
set(WX_VER_PATCH "8")

set(PARENT "opencpn")

##---------------------------------------------------------------------------
## Cloudsmith / CI Metadata
##---------------------------------------------------------------------------
set(PROD  "prod")
set(BETA  "beta")
set(ALPHA "alpha")

set(CLOUDSMITH_BASE_REPOSITORY "climatology")
set(CLOUDSMITH_USER "opencpn")

set(XML_INFO_URL "https://opencpn.org/OpenCPN/plugins/climatology.html")
set(XML_SUMMARY  ${SHORT_DESCRIPTION})
set(XML_DESCRIPTION ${LONG_DESCRIPTION})

##---------------------------------------------------------------------------
## Graphics / OpenGL Options
##---------------------------------------------------------------------------
# Climatology requires OpenGL for rendering overlays.
# This option is used by PluginConfigure.cmake to enable OpenGL detection.
option(USE_GL "Enable OpenGL support (required)" ON)
message(STATUS "${CMLOC}OpenGL enabled: ${USE_GL}")

# Enable SVG icons (used by many plugins)
option(PLUGIN_USE_SVG "Use SVG graphics" ON)

# OpenCPN core prefers legacy OpenGL (compatibility profile)
set(OpenGL_GL_PREFERENCE "LEGACY")

# Default build type (Release unless explicitly set)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
message(STATUS "${CMLOC}Build type: ${CMAKE_BUILD_TYPE}")



##---------------------------------------------------------------------------
## 0. Add opencpn-libs FIRST (MSBuild requires before setup & before Linking
##---------------------------------------------------------------------------
add_subdirectory(opencpn-libs/api-${OCPN_API_VERSION_MINOR})
add_subdirectory(opencpn-libs/tinyxml)

if(WIN32 AND MSVC)
    add_subdirectory(opencpn-libs/zlib)
	# No dependency needed — zlib is IMPORTED and prebuilt
endif()	

##---------------------------------------------------------------------------
## 1. PluginSetup — MUST RUN FIRST  (creates ${PACKAGE_NAME} )
##---------------------------------------------------------------------------
include(PluginSetup)

##---------------------------------------------------------------------------
## wxWidgets Paths (Windows/MSVC only, local dev)  Run before PluginSetup
##---------------------------------------------------------------------------
if(WIN32 AND MSVC)
    set(wxWidgets_ROOT "C:/Users/fcgle/source/ocpn_wxWidgets-3.2.2.1")

	set(wxWidgets_INCLUDE_DIRS
		"${wxWidgets_ROOT}/include"
		"${wxWidgets_ROOT}/lib/vc_dll/mswu"
##		"${wxWidgets_ROOT}/lib/vc_dll/mswud"  #For debug builds
	)
	set(wxWidgets_LIB_DIR "${wxWidgets_ROOT}/lib/vc_dll")
	#ensures MSVC can actually find the .lib files.
	
	set(wxWidgets_LIBRARIES
		wxmsw32u_core.lib
		wxmsw32u_gl.lib
		wxbase32u.lib
		wxbase32u_xml.lib    # ← correct XML lib
		wxmsw32u_xrc.lib
		wxmsw32u_aui.lib
	)
	target_include_directories(${PACKAGE_NAME} BEFORE PRIVATE
		${wxWidgets_INCLUDE_DIRS}
	)
    target_link_directories(${PACKAGE_NAME} BEFORE PRIVATE
        ${wxWidgets_LIB_DIR}
    )	
    target_link_libraries(${PACKAGE_NAME}
        ${wxWidgets_LIBRARIES}
    )

endif()

# Advises Visual Studio what the startup project is when you open the solution. Not essential
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${PACKAGE_NAME})
# Enables Visual Studio  "Solution folders” grouping so projects appear under logical folders. Not essential
set_property(GLOBAL PROPERTY USE_FOLDERS ON)


##---------------------------------------------------------------------------
## Plugin Sources
##---------------------------------------------------------------------------
set(SRC_LIST
    src/climatology_pi.cpp
    src/climatology_shaders.cpp
    src/climatology_curl.cpp
    src/ClimatologyConfigDialog.cpp
    src/ClimatologyConstants.cpp
    src/ClimatologyDataModel.cpp
    src/ClimatologyDialog.cpp
    src/ClimatologyOverlay.cpp
    src/DownloadManager.cpp
    src/ManifestLoader.cpp
    src/ClimatologyOverlayFactory.cpp
    src/OverlayMapping.cpp
    src/icons.cpp
    src/IsoBarMap.cpp
    src/zuFile.cpp
    src/UnifiedGrid.cpp
    src/ClimatologyColorGrid.cpp
)

target_sources(${PACKAGE_NAME} PRIVATE ${SRC_LIST})


##---------------------------------------------------------------------------
## 2. PluginConfigure — detects wxWidgets, OpenGL, CURL, etc.
##---------------------------------------------------------------------------
include(PluginConfigure)


##---------------------------------------------------------------------------
## Plugin target include Paths
##---------------------------------------------------------------------------
target_include_directories(${PACKAGE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/api-${OCPN_API_VERSION_MINOR}
)

target_include_directories(${PACKAGE_NAME} BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)


##---------------------------------------------------------------------------
## 4. Link plugin against opencpn-libs — After add_subdirectory()
##---------------------------------------------------------------------------

target_link_libraries(${PACKAGE_NAME}
		ocpn::api
		ocpn::tinyxml
		ocpn::zlib
		${wxWidgets_LIBRARIES} 
)

##---------------------------------------------------------------------------
## GLEW
##---------------------------------------------------------------------------
set(GLEW_ROOT "C:/Users/fcgle/source/vcpkg/installed/x86-windows")

target_include_directories(${PACKAGE_NAME} PRIVATE
    ${GLEW_ROOT}/include
)

target_link_libraries(${PACKAGE_NAME}
	${GLEW_ROOT}/lib/glew32.lib
)

##---------------------------------------------------------------------------
## CURL
##---------------------------------------------------------------------------
if(WIN32)
    target_link_libraries(${PACKAGE_NAME}
		${CURL_LIBRARY}
	)
else()
    find_package(CURL REQUIRED)
    target_link_libraries(${PACKAGE_NAME}
		CURL::libcurl
	)
endif()

##---------------------------------------------------------------------------
## Ensure plugin include directory overrides opencpn-libs
##---------------------------------------------------------------------------
target_include_directories(${PACKAGE_NAME} BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

##---------------------------------------------------------------------------
## Install, Localization, Packaging
##---------------------------------------------------------------------------
include(PluginInstall)
include(PluginLocalization)
include(PluginPackage)

##---------------------------------------------------------------------------
## Windows Runtime Dependencies
##---------------------------------------------------------------------------
if(WIN32)
    install(FILES "${CMAKE_SOURCE_DIR}/cacert.pem"
        DESTINATION plugins/climatology_pi)

    install(FILES "C:/Users/fcgle/source/vcpkg/installed/x86-windows/bin/glew32.dll"
        DESTINATION plugins/climatology_pi)
endif()

##---------------------------------------------------------------------------
## Cleanup vcpkg include pollution
##---------------------------------------------------------------------------
get_target_property(_incs ${PACKAGE_NAME} INCLUDE_DIRECTORIES)
if(_incs)
    list(FILTER _incs EXCLUDE REGEX "vcpkg")
    set_target_properties(${PACKAGE_NAME} PROPERTIES INCLUDE_DIRECTORIES "${_incs}")
endif()
