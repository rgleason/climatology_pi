##---------------------------------------------------------------------------
## Author: Sean d'Epagnier and Rick Gleason
##---------------------------------------------------------------------------

set(CMLOC "CMakeLists: ")

cmake_minimum_required(VERSION 3.15)

# Policies
if(COMMAND cmake_policy)
  if(POLICY CMP0043)
    cmake_policy(SET CMP0043 NEW)
  endif()
  cmake_policy(SET CMP0048 NEW)
  if(POLICY CMP0068)
    cmake_policy(SET CMP0068 NEW)
  endif()
  if(POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
  endif()
  if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
  endif()
  if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
  endif()
  if(POLICY CMP0175)
    cmake_policy(SET CMP0175 NEW)
  endif()
  if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
  endif()
endif()

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# Plugin metadata
set(VERBOSE_NAME Climatology)
set(COMMON_NAME "Climatology")
set(TITLE_NAME "climatology")
set(PACKAGE "climatology")
set(PACKAGE_NAME "climatology_pi")
set(PACKAGE_NAME_LOCKED "${PACKAGE_NAME}")

project(${PACKAGE_NAME})

# Force GL ON before PluginSetup and PluginConfigure
set(PLUGIN_USE_GL ON CACHE BOOL "Use GL" FORCE)
set(OCPN_USE_GL ON CACHE BOOL "Enable OpenGL" FORCE)

set(SHORT_DESCRIPTION "Climatology PlugIn: 32 years of monthly NOAA Data")
set(LONG_DESCRIPTION "Climatology provides overlay capabilities for historic weather data...")

set(VERSION_MAJOR "1")
set(VERSION_MINOR "6")
set(VERSION_PATCH "46")
set(VERSION_TWEAK "0")
set(VERSION_DATE  "06/23/2026")
set(OCPN_MIN_VERSION "ov58")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "20")

set(WX_VER "32")
set(WX_VER_PATCH "8")

set(PARENT "opencpn")

set(CLOUDSMITH_BASE_REPOSITORY "climatology")
set(CLOUDSMITH_USER "opencpn")

set(XML_INFO_URL "https://opencpn.org/OpenCPN/plugins/climatology.html")
set(XML_SUMMARY ${SHORT_DESCRIPTION})
set(XML_DESCRIPTION ${LONG_DESCRIPTION})

option(PLUGIN_USE_SVG "Use SVG graphics" ON)

set(CMAKE_CXX_STANDARD 11)

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

message(STATUS "${CMLOC}Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")


# ---------------------------------------------------------------------------
# Standard OpenCPN plugin setup
# ---------------------------------------------------------------------------
include("PluginSetup")
include("PluginConfigure")

# ---------------------------------------------------------------------------
# API 1.20 include path
# ---------------------------------------------------------------------------
target_compile_definitions(${PACKAGE_NAME} PRIVATE
    OCPN_API_VERSION_MAJOR=${OCPN_API_VERSION_MAJOR}
    OCPN_API_VERSION_MINOR=${OCPN_API_VERSION_MINOR}
)

set(OCPN_API_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/api-20")
target_include_directories(${PACKAGE_NAME} PRIVATE ${OCPN_API_INCLUDE_DIR})


# ---------------------------------------------------------------------------
# Windows headers alias (required by plugin_dc)
# ---------------------------------------------------------------------------
add_library(windows-headers INTERFACE)
target_include_directories(windows-headers INTERFACE "$ENV{WindowsSdkDir}/Include")
add_library(windows::headers ALIAS windows-headers)

# ---------------------------------------------------------------------------
# plugin_dc (must be added early)
# ---------------------------------------------------------------------------
add_subdirectory(opencpn-libs/plugin_dc)

# Ensure dc_utils builds with GL
target_compile_definitions(_DC_UTILS PRIVATE USE_OCPNGL __OCPN__GL__)

# Plugin GL flags
target_compile_definitions(${PACKAGE_NAME} PRIVATE USE_OCPNGL __OCPN__GL__ CLIMATOLOGY_BUNDLED_CURL)

# ---------------------------------------------------------------------------
# Other opencpn-libs
# ---------------------------------------------------------------------------
add_subdirectory(opencpn-libs/api-${OCPN_API_VERSION_MINOR})
add_subdirectory(opencpn-libs/tinyxml)
add_subdirectory(opencpn-libs/jsonlib)
add_subdirectory(opencpn-libs/wxJSON)

if(NOT UNIX)
    add_subdirectory(opencpn-libs/bzip2)
endif()

if(WIN32 AND MSVC)
    add_subdirectory(opencpn-libs/zlib)
endif()

# ---------------------------------------------------------------------------
# Plugin sources
# ---------------------------------------------------------------------------
set(SRCS
    src/climatology_pi.cpp
    src/ClimatologyConfigDialog.cpp
    src/ClimatologyDialog.cpp
    src/ClimatologyOverlayFactory.cpp
    src/ClimatologyUI.cpp
    src/DownloadManager.cpp
    src/icons.cpp
    src/IsoBarMap.cpp
    src/ManifestLoader.cpp
    src/zuFile.cpp
    src/climatology_shaders.cpp
)

set(HDRS
    include/climatology_pi.h
    include/ClimatologyConfigDialog.h
    include/ClimatologyDialog.h
    include/ClimatologyOverlayFactory.h
    include/ClimatologyUI.h
    include/gldefs.h
    include/defs.h
    include/DownloadFileEntry.hpp
    include/DownloadManager.hpp
    include/icons.h
    include/IsoBarMap.h
    include/linmath.h
    include/ManifestEntry.hpp
    include/ManifestLoader.hpp
    include/zuFile.h
    include/climatology_shaders.h
)

target_sources(${PACKAGE_NAME} PRIVATE ${SRCS} ${HDRS})

# ---------------------------------------------------------------------------
# Linking order
# ---------------------------------------------------------------------------
target_link_libraries(${PACKAGE_NAME}
    PRIVATE
        ocpn::api
        _DC_UTILS
        ocpn::tinyxml
        ocpn::jsonlib
        ocpn::wxjson
        ocpn::bzip2
        ocpn::zlib
)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(${PACKAGE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/plugin_dc/glu/include/GL
    ${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/plugin_dc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/plugin_dc/dc_utils/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# GLEW (must be after cleanup)
# ---------------------------------------------------------------------------
set(GLEW_ROOT "C:/Users/fcgle/source/vcpkg/installed/x86-windows")
target_include_directories(${PACKAGE_NAME} PRIVATE ${GLEW_ROOT}/include)
target_link_libraries(${PACKAGE_NAME} PRIVATE ${GLEW_ROOT}/lib/glew32.lib)

#find_package(GLEW REQUIRED)
#target_link_libraries(${PACKAGE_NAME} PRIVATE GLEW::GLEW)

# ---------------------------------------------------------------------------
# Install, localization, packaging
# ---------------------------------------------------------------------------
include("PluginInstall")
include("PluginLocalization")
include("PluginPackage")

# ---------------------------------------------------------------------------
# Curl integration
# ---------------------------------------------------------------------------
if(WIN32)
    message(STATUS "${CMLOC}Configuring curl integration for Windows")
    set(CURL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/win_curl/output32/include")
    set(CURL_LIBRARY "${CMAKE_SOURCE_DIR}/win_curl/output32/lib/libcurl_imp.lib")

    target_include_directories(${PACKAGE_NAME} PRIVATE ${CURL_INCLUDE_DIR})
    target_link_libraries(${PACKAGE_NAME} PRIVATE ${CURL_LIBRARY})

    install(FILES "${CMAKE_SOURCE_DIR}/cacert.pem"
        DESTINATION plugins/climatology_pi)
		
	install(FILES "C:/Users/fcgle/source/vcpkg/installed/x86-windows/bin/glew32.dll"
        DESTINATION plugins/climatology_pi)
	
else()
    find_package(CURL REQUIRED)
    target_link_libraries(${PACKAGE_NAME} PRIVATE CURL::libcurl)
endif()

# ---------------------------------------------------------------------------
# Cleanup vcpkg pollution
# ---------------------------------------------------------------------------
get_target_property(_incs ${PACKAGE_NAME} INCLUDE_DIRECTORIES)
if(_incs)
    list(FILTER _incs EXCLUDE REGEX "vcpkg")
    set_target_properties(${PACKAGE_NAME} PROPERTIES INCLUDE_DIRECTORIES "${_incs}")
endif()
