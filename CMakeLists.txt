##---------------------------------------------------------------------------
## Author: Sean d'Epagnier and Rick Gleason
##---------------------------------------------------------------------------
# - License:     GPLv3+
# - This program is free software; you can redistribute it and/or modify  *
# - it under the terms of the GNU General Public License as published by  *
# - the Free Software Foundation; either version 2 of the License, or     *
# - (at your option) any later version.                                   *
# - *
# - This program is distributed in the hope that it will be useful,       *
# - but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# - MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# - GNU General Public License for more details.                          *
# - *
# - You should have received a copy of the GNU General Public License     *
# - along with this program; if not, write to the                         *
# - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,                                       *
# - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx                                       *
# -----------------------------------------------------------------------------
# Frontend2 Author: Jon Gough Testplugin(TP) v1.0.362.0 19/06/2026 DD/MM/YYYY
# format buildCI:yes useODAPI:yes validateJSON:yes
# ---------------------------------------------------------------------------
## -- When changing this file do NOT change the order of the sections.    -- ##
## -- Make changes only between the section blocks identified             -- ##
## -- The whole configuration process relies on this sequence for success.-- ##
##
## -- Modify section below to include all the details for your plugin.    -- ##

set(CMLOC "CMakeLists: ")

cmake_minimum_required(VERSION 3.15)

# Policies
if(COMMAND cmake_policy)
  if (POLICY CMP0043)
    cmake_policy(SET CMP0043 NEW)
  endif (POLICY CMP0043)
  cmake_policy(SET CMP0048 NEW)
  if (POLICY CMP0068)
    cmake_policy(SET CMP0068 NEW)
  endif (POLICY CMP0068)
  if (POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
  endif (POLICY CMP0076)
  if (POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
  endif (POLICY CMP0077)
  if (POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
  endif (POLICY CMP0079)
  if (POLICY CMP0175)
    cmake_policy(SET CMP0175 NEW)
  endif (POLICY CMP0175)
  if (POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
  endif (POLICY CMP0177)
endif (COMMAND cmake_policy)

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/std:c++17)
endif()

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
set(VERSION_PATCH "47")
set(VERSION_TWEAK "0")
set(VERSION_DATE  "06/30/2026")
set(OCPN_MIN_VERSION "ov58")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "20")

set(WX_VER "32")
set(WX_VER_PATCH "8")

set(PARENT "opencpn")

# Use to set up a local webserver with git installed. Default is github.com.
# Now set in PluginSetup.cmake set(GIT_REPOSITORY_SERVER "github.com")

## Specifies Cloudsmith upload repository suffix for each catalog
set(PROD "prod")   #Standard Repos
set(BETA "beta")   #Standard Repos
set(ALPHA "alpha") #Standard Repos

# Set if  Cloudsmith Base Repository name does not match your Git Repository name.
#set (CLOUDSMITH_BASE_REPOSITORY "${GIT_REPOSITORY_NAME}"															  
set(CLOUDSMITH_BASE_REPOSITORY "climatology")
set(CLOUDSMITH_USER "opencpn")

set(XML_INFO_URL "https://opencpn.org/OpenCPN/plugins/climatology.html")
set(XML_SUMMARY ${SHORT_DESCRIPTION})
set(XML_DESCRIPTION ${LONG_DESCRIPTION})

option(PLUGIN_USE_SVG "Use SVG graphics" ON)

# Use local version of GLU library requires libs/glu directory
set(USE_LOCAL_GLU FALSE)
# Prefer libGL.so to libOpenGL.so, see CMP0072
set(OpenGL_GL_PREFERENCE "LEGACY") # not operational

option(USE_GL "Enable OpenGL support" ON)
message(STATUS "${CMLOC}USE_GL: ${USE_GL}")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "Choose the type of build" FORCE
  )
endif ("${CMAKE_BUILD_TYPE}" STREQUAL "")
set_property(
  CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "" "Debug" "Release" "RelWithDebInfo"
                                  "MinSizeRel"
)
message(STATUS "${CMLOC}Build type: ${CMAKE_BUILD_TYPE}")

# ----- Statements below used to setup standard variables required by the CMAKE  -##
# ----- process -- Do not remove -##

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(PLUGIN_SRC_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(PLUGIN_HDR_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)


# ---------------------------------------------------------------------------
# Plugin sources
# ---------------------------------------------------------------------------

 set(SRCS
     src/climatology_pi.cpp
   	src/climatology_shaders.cpp
     src/ClimatologyConfigDialog.cpp
     src/ClimatologyDialog.cpp
     src/ClimatologyOverlayFactory.cpp
     src/DownloadManager.cpp
     src/icons.cpp
     src/IsoBarMap.cpp
     src/ManifestLoader.cpp
     src/zuFile.cpp
)

 set(HDRS
    include/climatology_pi.h
    include/ClimatologyConfigDialog.h
  	include/ClimatologyCoord.h
    include/ClimatologyDialog.h
    include/ClimatologyOverlayFactory.h
 	include/ClimatologyRenderParams.h
  	include/climatology_shaders.h
 	include/CycloneFilterParams.h
	include/CycloneTypes.h
    include/defs.h
    include/DownloadFileEntry.hpp
    include/DownloadManager.hpp
 	include/gldefs.h
    include/icons.h
    include/IsoBarMap.h
    include/linmath.h
    include/ManifestEntry.hpp
    include/ManifestLoader.hpp
    include/zuFile.h
)
# ---------------------------------------------------------------------------
# Standard OpenCPN plugin setup
# ---------------------------------------------------------------------------

add_library(${PACKAGE_NAME} SHARED ${SRCS} ${HDRS})

set(CMAKE_VERBOSE_MAKEFILE "Activate verbose mode for make files" ON)
option(Plugin_CXX11 "Use c++11" OFF)

include("PluginConfigure")

set(CMAKE_VERBOSE_MAKEFILE "Activate verbose mode for make files" ON)
option(Plugin_CXX11 "Use c++11" OFF)

include("PluginConfigure")

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
# Linking order
# ---------------------------------------------------------------------------
set(GLEW_ROOT "C:/Users/fcgle/source/vcpkg/installed/x86-windows")



if(TARGET ${PACKAGE_NAME})
    target_link_libraries(${PACKAGE_NAME}
        ocpn::api
        ocpn::tinyxml
        ocpn::jsonlib
        ocpn::wxjson
        ocpn::bzip2
        ocpn::zlib
    )

    target_include_directories(${PACKAGE_NAME} PRIVATE
        ${OCPN_API_INCLUDE_DIR}
        ${GLEW_ROOT}/include
        ${CURL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(${PACKAGE_NAME} ${GLEW_ROOT}/lib/glew32.lib)

    if(WIN32)
        target_link_libraries(${PACKAGE_NAME} ${CURL_LIBRARY})
    endif()
endif()

	
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

    # already added above with PRIVATE, so remove:
    # target_include_directories(${PACKAGE_NAME} ${CURL_INCLUDE_DIR})
   # target_link_libraries(${PACKAGE_NAME} ${CURL_LIBRARY})

#    message(STATUS "${CMLOC}Configuring curl integration for Windows")
#    set(CURL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/win_curl/output32/include")
#    set(CURL_LIBRARY "${CMAKE_SOURCE_DIR}/win_curl/output32/lib/libcurl_imp.lib")

 #   target_include_directories(${PACKAGE_NAME} ${CURL_INCLUDE_DIR})
  #  target_link_libraries(${PACKAGE_NAME} ${CURL_LIBRARY})

    install(FILES "${CMAKE_SOURCE_DIR}/cacert.pem"
        DESTINATION plugins/climatology_pi)
		
	install(FILES "C:/Users/fcgle/source/vcpkg/installed/x86-windows/bin/glew32.dll"
        DESTINATION plugins/climatology_pi)
	
else()
    find_package(CURL REQUIRED)
    target_link_libraries(${PACKAGE_NAME}  CURL::libcurl)
endif()

# ---------------------------------------------------------------------------
# Cleanup vcpkg pollution
# ---------------------------------------------------------------------------
get_target_property(_incs ${PACKAGE_NAME} INCLUDE_DIRECTORIES)
if(_incs)
    list(FILTER _incs EXCLUDE REGEX "vcpkg")
    set_target_properties(${PACKAGE_NAME} PROPERTIES INCLUDE_DIRECTORIES "${_incs}")
endif()
