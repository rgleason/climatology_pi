##---------------------------------------------------------------------------
## Author:      Sean d'Epagnier   and  Rick Gleason 
##***************************************************************************
## *   Copyright (C) 2010 by Sean d'Epagnier                              *
## *   License:     GPLv3+
## *   This program is free software; you can redistribute it and/or modify  *
## *   it under the terms of the GNU General Public License as published by  *
## *   the Free Software Foundation; either version 2 of the License, or     *
## *   (at your option) any later version.                                   *
## *                                                                         *
## *   This program is distributed in the hope that it will be useful,       *
## *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
## *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
## *   GNU General Public License for more details.                          *
## *                                                                         *
## *   You should have received a copy of the GNU General Public License     *
## *   along with this program; if not, write to the                         *
## *   xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,                                       *
## *   xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx                                       *
#-----------------------------------------------------------------------------
# Frontend2 Author: Jon Gough 
# Frontend2 Author: Jon Gough Testplugin(TP) v1.0.359.0 5/10/2026 DD/MM/YYYY
# buildCI:yes useODAPI:no validateJSON:no
# ---------------------------------------------------------------------------
## -- When changing this file do NOT change the order of the sections.    -- ##
## -- Make changes only between the section blocks identified             -- ##
## -- The whole configuration process relies on this sequence for success.-- ##
##
## -- Modify section below to include all the details for your plugin.    -- ##

set(CMLOC "CMakeLists: ")

# define minimum cmake version
cmake_minimum_required(VERSION 3.15)
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


# set(VCPKG_TARGET_TRIPLET "x86-windows-static" CACHE STRING "")
# set(VCPKG_CMAKE_SYSTEM_INCLUDE_PATH "" CACHE STRING "")
#set(VCPKG_CMAKE_SYSTEM_LIBRARY_PATH "" CACHE STRING "")


# set for vscode/vscodium
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# define plugin name, owner and versions
set(VERBOSE_NAME Climatology)
set(COMMON_NAME "Climatology")    # Search "getcommonname"in climatology_pi.
set(TITLE_NAME "climatology")
set(PACKAGE_CONTACT "Sean D'Epagnier")
set(PACKAGE "climatology_pi")
# set(PACKAGE "climatology") -old setting
# new setting to conform to project name
set(PACKAGE_NAME "climatology_pi")
project(${PACKAGE_NAME})
#project(${PACKAGE}) Old setting


set(SHORT_DESCRIPTION "Climatology PlugIn: 32 years of monthly NOAA Data")
set(LONG_DESCRIPTION "Climatology provides overlay capabilities for historic weather data. Supported Climatology types include: - Average wind directions and speed- Percentage of gale and calm conditions - Sea Surface Currents - Average swell and sea state (not yet implemented) - Precipitation, Humidity, Cloud Cover, and Lightning Strikes - Monthly average Sea Level pressure and Sea Temperature, Air Temperature - Tropical Cyclone tracks")

set(VERSION_MAJOR "1")
set(VERSION_MINOR "6")
set(VERSION_PATCH "40")
set(VERSION_TWEAK "0")
set(VERSION_DATE  "06/18/2026")#DD/MM/YYYY format
set(OCPN_MIN_VERSION "ov58")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "20")
set(WX_VER "32")
set(WX_VER_PATCH "8")

set(TP_COMMENT "  * RelWithDebInfo using CI")

set(PARENT "opencpn")

#Use to set up a local webserver with git installed.
#Default is github.com. Now set in pluginconfigure.cmake
#set(GIT_REPOSITORY_SERVER "github.com")

## Specifies Cloudsmith upload repository suffix for each catalog
set(PROD "prod")   #Standard Repos
set(BETA "beta")   #Standard Repos
set(ALPHA "alpha") #Standard Repos

# Set if  Cloudsmith Base Repository name does not match your Git Repository name.
#set(CLOUDSMITH_BASE_REPOSITORY "${GIT_REPOSITORY_NAME}")
#set(CLOUDSMITH_BASE_REPOSITORY "opencpn-plugins")  #Ricks's customization
set(CLOUDSMITH_BASE_REPOSITORY "climatology")  #without the _pi

# Set the following to use OpenCPN cloudsmith repositories instead of own repositories
set(CLOUDSMITH_USER "opencpn")
# set(CLOUDSMITH_USER "rick-gleason")

set(XML_INFO_URL 
    "https://opencpn.org/OpenCPN/plugins/climatology.html"
)
set(XML_SUMMARY ${SHORT_DESCRIPTION})
set(XML_DESCRIPTION ${LONG_DESCRIPTION})

## ----- Modify section above to include all the details for your plugin ------- ##
## ----- Modify section below if there are special requirements for the plugin --##

# Forces all install() output to go to 
# "..source\opencpn\build\RelWithDebInfo\plugins\climatology_pi\

# Force installation into the build configuration directory
# set(PLUGIN_INSTALL_DIR
#    "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/plugins/${PACKAGE_NAME}" CACHE PATH "" FORCE)
# set(PLUGIN_INSTALL_DIR
#    "C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins/${PACKAGE_NAME}" CACHE PATH "" FORCE)
# set(PLUGIN_INSTALL_DIR
#    "C:/Users/fcgle/source/opencpn/build/${CMAKE_BUILD_TYPE}/plugins/${PACKAGE_NAME}"
#    CACHE PATH "" FORCE)

option(PLUGIN_USE_SVG "Use SVG graphics" ON)

set(CMAKE_CXX_STANDARD 11)

# Prefer libGL.so to libOpenGL.so, see CMP0072
#  set(OpenGL_GL_PREFERENCE "LEGACY")   #not operational

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

# YES — you must keep OCPN_USE_GL ON
# OCPN_USE_GL controls the plugin’s own GL compile flags
# It enables: USE_OCPNGL  __OCPN__GL__   GL  aware code paths in your plugin
# If you turn it OFF: Your plugin compiles in DC‑only mode
#  All GL code in ClimatologyOverlayFactory becomes inactive, missing GL symbol errors
# But OCPN_USE_GL does NOT add plugin_dc in your TP version.
# OCPN_USE_GL enables GL in your plugin, 
# But PluginSetup does NOT use it to add plugin_dc
# So the correct combination is:
# ✔ OCPN_USE_GL ON
# ✔ Manual add_subdirectory(opencpn-libs/plugin_dc)
# ✔ Windows headers alias
# ✔ No PLUGIN_USE_GL (ignored in your TP version)
# So plugin_dc must be added manually
set(OCPN_USE_GL ON CACHE BOOL "Enable OpenGL" FORCE)
# set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)

# set(OCPN_USE_GL ON)
# OFF prevents PluginSetup from adding plugin_dc
# so that we can build it manually below.
# Best for building plugins in Standalone mode
# set(PLUGIN_USE_GL OFF) 

##========================================================
## Standard OpenCPN plugin setup
## Do not change - needed to configure build process
##========================================================
include("PluginSetup")
include("PluginConfigure")
##===========================================================

# Bring in plugin_dc manually for standalone builds
# Now add plugin_dc so the library builds without a duplicate.
add_subdirectory(opencpn-libs/plugin_dc)

# ---------------------------------------------------------
# ADD plugin_dc EARLY — BEFORE linking, BEFORE includes
# ---------------------------------------------------------
# Needed - satisfies plugin_dc’s dependency and produces no actual .lib file
# because INTERFACE IMPORTED targets do not generate binaries
# add_library(windows::headers INTERFACE IMPORTED)
# Provide Windows headers for plugin_dc without creating a .lib
add_library(windows-headers INTERFACE)

# Windows SDK include path (CMake sets this automatically on MSVC)
target_include_directories(windows-headers INTERFACE
    "$ENV{WindowsSdkDir}/Include"
)

# Alias to match plugin_dc expectations
add_library(windows::headers ALIAS windows-headers)


# Bring in plugin_dc (modern GL abstraction)
# No - PluginSetup already brings in plugin_dc, don't add it twice.
# add_subdirectory(opencpn-libs/plugin_dc)


# Enable OpenCPN GL mode and bundled curl for win
target_compile_definitions(${PACKAGE_NAME} PRIVATE USE_OCPNGL)
target_compile_definitions(${PACKAGE_NAME} PRIVATE __OCPN__GL__)
target_compile_definitions(${PACKAGE_NAME} PRIVATE CLIMATOLOGY_BUNDLED_CURL)

# ---------------------------------------------------------
# Add other opencpn-libs BEFORE linking
# ---------------------------------------------------------
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

##===========================================================
## Plugin sources
##===========================================================
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
)

target_sources(${PACKAGE_NAME} PRIVATE ${SRCS} ${HDRS})

##===========================================================
## Correct linking order — plugin_dc MUST be here
##===========================================================
target_link_libraries(${PACKAGE_NAME}
    PRIVATE
        ocpn::api
        ocpn::dc-utils
        ocpn::tinyxml
        ocpn::jsonlib
        ocpn::wxjson
        ocpn::bzip2
        ocpn::zlib
)

##===========================================================
## Correct include directory order — MUST come AFTER linking
##===========================================================

target_include_directories(${PACKAGE_NAME} PRIVATE
	# pointer to gl_private.h modern opengl put first - not in cmakelists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/plugin_dc/glu/include/GL
    # plugin_dc core headers	
    ${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/plugin_dc/include
    # dc_utils 	
    ${CMAKE_CURRENT_SOURCE_DIR}/opencpn-libs/plugin_dc/dc_utils/include
	# for pi_shaders.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

##===========================================================
## Install, localization, packaging
##===========================================================
include("PluginInstall")
include("PluginLocalization")
include("PluginPackage")

# --- Bundle libcurl.dll (WeatherRouting style) ---
# if (WIN32)
#    install(FILES "${VCPKG_ROOT}/installed/x86-windows/bin/libcurl.dll"
#        DESTINATION .
#    )
# endif()

if (WIN32)
    message(STATUS "${CMLOC}Configuring curl integration for Windows")
    set(CURL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/win_curl/output32/include")
    set(CURL_LIBRARY "${CMAKE_SOURCE_DIR}/win_curl/output32/lib/libcurl_imp.lib")

    # Include curl headers
    target_include_directories(${PACKAGE_NAME} PRIVATE ${CURL_INCLUDE_DIR})
    target_link_libraries(${PACKAGE_NAME} PRIVATE ${CURL_LIBRARY})

    # Install the curl runtime DLL
	#install(FILES "${CMAKE_SOURCE_DIR}/win_curl/output32/bin/libcurl.dll"
    #    DESTINATION plugins/climatology_pi)

    # Install CA certificate bundle
    install(FILES "${CMAKE_SOURCE_DIR}/cacert.pem"
        DESTINATION plugins/climatology_pi)
else()  # Linux / macOS
    message(STATUS "${CMLOC}Configuring system curl for non-Windows")
	find_package(CURL REQUIRED)
	target_link_libraries(${PACKAGE_NAME} PRIVATE CURL::libcurl)
 endif()

##===========================================================
## FINAL CLEANUP — remove vcpkg GL pollution
##===========================================================

get_target_property(_incs ${PACKAGE_NAME} INCLUDE_DIRECTORIES)

if(_incs)
    list(FILTER _incs EXCLUDE REGEX "vcpkg")
    set_target_properties(${PACKAGE_NAME} PROPERTIES INCLUDE_DIRECTORIES "${_incs}")
endif()
