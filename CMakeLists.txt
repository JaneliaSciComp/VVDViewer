#/*
#For more information, please see: http://software.sci.utah.edu
#
#The MIT License
#
#Copyright (c) 2014 Scientific Computing and Imaging Institute,
#University of Utah.
#
#
#Permission is hereby granted, free of charge, to any person obtaining a
#copy of this software and associated documentation files (the "Software"),
#to deal in the Software without restriction, including without limitation
#the rights to use, copy, modify, merge, publish, distribute, sublicense,
#and/or sell copies of the Software, and to permit persons to whom the
#Software is furnished to do so, subject to the following conditions:
#
#The above copyright notice and this permission notice shall be included
#in all copies or substantial portions of the Software.
#
#THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
#OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
#THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#DEALINGS IN THE SOFTWARE.
#*/

#This is an CMake configuration file for VVDViewer

cmake_minimum_required ( VERSION 2.8.8 )

#for MSVC builds
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	if(MSVC)
		SET(MSVC_INCREMENTAL_DEFAULT OFF)
	endif()
endif()

project ( VVDViewer )

IF((COMMAND cmake_policy) AND NOT (CMAKE_MAJOR_VERSION LESS 3))
    CMAKE_POLICY(SET CMP0040 NEW)
    CMAKE_POLICY(SET CMP0043 NEW)
ENDIF()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
   # Mac OS X
   set(ARCHITECTURE 64)
else()
   # Windows
   if(${CMAKE_SIZEOF_VOID_P} MATCHES "8")
     set(ARCHITECTURE 64)
   else()
     set(ARCHITECTURE 32)
   endif()
endif()

if(${ARCHITECTURE} MATCHES "64")
      add_definitions(-DFLUORENDER_ARCH="64bit")
else()
      add_definitions(-DFLUORENDER_ARCH="32bit")
endif()

add_definitions(-DFLUORENDER_TITLE="VVDViewer")

add_definitions(-DVERSION_MAJOR=2)                   #Be sure to update the Versions and Date for each release!!!!
add_definitions(-DVERSION_MINOR=19)					 #
add_definitions(-DVERSION_MAJOR_TAG="2")             #
add_definitions(-DVERSION_MINOR_TAG="19")            #
add_definitions(-DVERSION_COPYRIGHT="November 2015")   # Up to here!

#static compile
add_definitions(-DSTATIC_COMPILE)

set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-Bsymbolic")

#teem required definitions
add_definitions(-DTEEM_DIO=0)
add_definitions(-DTEEM_ENDIAN=1234)
add_definitions(-DTEEM_QNANHIBIT=1)

#output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BUILD_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BUILD_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

#wxWidgets
add_definitions(-DwxSYSTEM_ZLIB_LIB)
add_definitions(-DwxSYSTEM_JPEG_LIB)
set(wxWidgets_USE_REL_AND_DBG ON)
if(${CMAKE_BUILD_TYPE} MATCHES "Release")
	set(wxWidgets_CONFIGURATION mswu)
elseif(${CMAKE_BUILD_TYPE} MATCHES "Debug")
	set(wxWidgets_CONFIGURATION mswud)
endif()
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
set(wxWidgets_EXCLUDE_COMMON_LIBRARIES TRUE)
find_package(wxWidgets COMPONENTS core base aui html xml net adv stc scintilla png tiff regex expat zlib REQUIRED)
else()
find_package(wxWidgets COMPONENTS core base aui html xml net adv stc REQUIRED)
endif()

include(${wxWidgets_USE_FILE})
set(wxWidgets_USE_STATIC ON)
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
   include_directories(${wxWidgets_ROOT_DIR}/src/tiff/libtiff)
else()
   find_package(TIFF REQUIRED)
   include_directories(${TIFF_INCLUDE_DIR})
endif()

add_definitions(-DUNICODE)
add_definitions(-D_UNICODE)
#solution for wxWidgets linking "ambiguous" errors
add_definitions(-D_WCHAR_H_CPLUSPLUS_98_CONFORMANCE_)
#fix the library names for OSX if needed. 
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  foreach(arg ${wxWidgets_LIBRARIES})
    set(wxlib_out "${wxlib_out} ${arg}")
  endforeach()
  string(STRIP ${wxlib_out} wxWidgets_LIBRARIES)
  string(REGEX REPLACE "libwx_osx_cocoau_scintilla" "libwxscintilla" 
                   wxWidgets_LIBRARIES ${wxWidgets_LIBRARIES})
  find_package(Freetype REQUIRED)
  include_directories(${FREETYPE_INCLUDE_DIRS})
endif()

#OpenGL
#find_package(OpenGL REQUIRED)

#Boost
#set(Boost_USE_STATIC_LIBS        ON) # only find static libs
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME    OFF)
find_package(Boost COMPONENTS system chrono filesystem REQUIRED)
include_directories(${Boost_INCLUDE_DIR})
add_definitions( -DBOOST_ALL_NO_LIB )

#teem
# add zlib
set(ZLIB_USE_STATIC_LIBS ON)
mark_as_advanced(CLEAR ZLIB_INCLUDE_DIR)
mark_as_advanced(CLEAR ZLIB_LIBRARY)
find_package(ZLIB REQUIRED)
add_definitions(-DTEEM_ZLIB=1)
include_directories(${ZLIB_INCLUDE_DIRS})

include_directories(${VVDViewer_SOURCE_DIR}/fluorender/teem)
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/teem/Biff)
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/teem/Air)
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/teem/Hest)
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/teem/Nrrd)
file(GLOB airsrc fluorender/teem/Air/*.c)
file(GLOB airhdr fluorender/teem/Air/*.h)
file(GLOB hestsrc fluorender/teem/Hest/*.c)
file(GLOB hesthdr fluorender/teem/Hest/*.h)
file(GLOB biffsrc fluorender/teem/Biff/*.c)
file(GLOB biffhdr fluorender/teem/Biff/*.h) 
file(GLOB nrrdsrc fluorender/teem/Nrrd/*.c)
file(GLOB nrrdhdr fluorender/teem/Nrrd/*.h)
add_library(TEEM_OBJ OBJECT
      ${airsrc} ${hestsrc} ${nrrdsrc} ${biffsrc}
      ${airhdr} ${hesthdr} ${nrrdhdr} ${biffhdr})

#pole
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/pole)
file(GLOB pole_srcs fluorender/pole/pole.cpp)
file(GLOB pole_hdrs fluorender/pole/pole.h)
add_library(POLE_OBJ OBJECT ${pole_srcs} ${pole_hdrs})

#ffmpeg
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  include_directories(${VVDViewer_SOURCE_DIR}/fluorender/ffmpeg/Win64/include)
  file(GLOB ffmpeg_LIBS_D ${VVDViewer_SOURCE_DIR}/fluorender/ffmpeg/Win64/lib/Debug/*.lib)
  file(GLOB ffmpeg_LIBS_R ${VVDViewer_SOURCE_DIR}/fluorender/ffmpeg/Win64/lib/Release/*.lib)
  set(ffmpeg_LIBRARIES)
  foreach (f ${ffmpeg_LIBS_D})
    set(ffmpeg_LIBRARIES ${ffmpeg_LIBRARIES} debug ${f})
  endforeach()
  foreach (f ${ffmpeg_LIBS_R})
    set(ffmpeg_LIBRARIES ${ffmpeg_LIBRARIES} optimized ${f})
  endforeach()
  add_definitions(-D__STDC_CONSTANT_MACROS)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  include_directories(${VVDViewer_SOURCE_DIR}/fluorender/ffmpeg/OSX/include)
  file(GLOB ffmpeg_LIBRARIES ${VVDViewer_SOURCE_DIR}/fluorender/ffmpeg/OSX/lib/*.a)
else()
  include_directories(${VVDViewer_SOURCE_DIR}/fluorender/ffmpeg/Linux/include)
  file(GLOB ffmpeg_LIBRARIES ${VVDViewer_SOURCE_DIR}/fluorender/ffmpeg/Linux/lib/*.a)
endif()

#curl
add_definitions(-DCURL_STATICLIB)
mark_as_advanced(CLEAR CURL_INCLUDE_DIR)
mark_as_advanced(CLEAR CURL_LIBRARY)
find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIR})

#openssl
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
file(GLOB SSL_DEP_LIBRARIES ${VVDViewer_SOURCE_DIR}/fluorender/libcurl/OSX/*.a)
endif()

#libjpeg
mark_as_advanced(CLEAR JPEG_INCLUDE_DIR)
mark_as_advanced(CLEAR JPEG_LIBRARY)
find_package(JPEG REQUIRED)
include_directories(${JPEG_INCLUDE_DIR})

#FluoRender
include_directories(${VVDViewer_SOURCE_DIR}/fluorender)
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender)

#Formats
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Formats)
file(GLOB fmt_src fluorender/FluoRender/Formats/*.cpp)
file(GLOB fmt_hdr fluorender/FluoRender/Formats/*.h)
add_library(FORMATS_OBJ OBJECT
      ${fmt_src} ${fmt_hdr})

#Tracking
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Tracking)
file(GLOB trk_src fluorender/FluoRender/Tracking/*.cpp)
file(GLOB trk_hdr fluorender/FluoRender/Tracking/*.h)
add_library(TRACKING_OBJ OBJECT
      ${trk_src} ${trk_hdr})

#Animators
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Animators)
file(GLOB ani_src fluorender/FluoRender/Animator/*.cpp)
file(GLOB ani_hdr fluorender/FluoRender/Animator/*.h)
add_library(ANIMATORS_OBJ OBJECT
      ${ani_src} ${ani_hdr})

#NV
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/NV)
file(GLOB nv_src fluorender/FluoRender/NV/*.cpp)
file(GLOB nv_hdr fluorender/FluoRender/NV/*.h)
add_library(NV_OBJ OBJECT
      ${nv_src} ${nv_hdr})

#FLIVR
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/FLIVR)
file(GLOB fli_src fluorender/FluoRender/FLIVR/*.cpp)
file(GLOB fli_hdr fluorender/FluoRender/FLIVR/*.h)
add_library(FLIVR_OBJ OBJECT
      ${fli_src} ${fli_hdr})

#Converters
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Converters)
file(GLOB cvt_src fluorender/FluoRender/Converters/*.cpp)
file(GLOB cvt_hdr fluorender/FluoRender/Converters/*.h)
add_library(CONVERTERS_OBJ OBJECT
      ${cvt_src} ${cvt_hdr})

#images
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/img)
file(GLOB pngs_hdr fluorender/FluoRender/img/*.h)
file(GLOB pngs_src fluorender/FluoRender/img/*.cpp)
add_library(IMAGES_OBJ OBJECT  ${pngs_hdr} ${pngs_src})

#plugins
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/PluginController)
set(PLUGIN_DIR ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/PluginController)
add_library(PLUGINS_OBJ OBJECT ${PLUGIN_DIR}/stdwx.h 
							   ${PLUGIN_DIR}/PluginControllerBase.h 
							   ${PLUGIN_DIR}/PluginControllerBaseSettings.h 
							   ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/DLLExport.h
							   ${PLUGIN_DIR}/stdwx.cpp
							   ${PLUGIN_DIR}/PluginControllerBase.cpp
							   ${PLUGIN_DIR}/PluginControllerBaseSettings.cpp)

#hdf5
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  include_directories(${VVDViewer_SOURCE_DIR}/fluorender/hdf5/win/include)
  file(GLOB hdf5_LIBS_DR ${VVDViewer_SOURCE_DIR}/fluorender/hdf5/win/lib/*.lib)
  set(HDF5_LIBRARIES)
  foreach(f ${hdf5_LIBS_DR})
    if(f MATCHES "_D.lib$")
      set(HDF5_LIBRARIES ${HDF5_LIBRARIES} debug ${f})
	elseif(f MATCHES ".lib$")
	  set(HDF5_LIBRARIES ${HDF5_LIBRARIES} optimized ${f})
	endif()
  endforeach()
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  include_directories(${VVDViewer_SOURCE_DIR}/fluorender/hdf5/mac/include)
  file(GLOB HDF5_LIBRARIES ${VVDViewer_SOURCE_DIR}/fluorender/hdf5/mac/lib/*.a)
else()
  find_package(HDF5 REQUIRED)
  include_directories(${HDF5_INCLUDE_DIRS})
  file(GLOB HDF5_LIBRARIES ${VVDViewer_SOURCE_DIR}/fluorender/hdf5/mac/lib/*.a)
endif()

#libCZI
find_path(CZI_INCLUDE_DIR NAMES libCZI.h PATHS ${CZI_INCLUDE_DIR} /usr/local/include/libCZI /usr/include/libCZI /opt/local/include/libCZI)
include_directories(${CZI_INCLUDE_DIR})
set(CZI_NAMES ${CZI_NAMES} libCZIStatic liblibCZIStatic)
find_library(LIBCZI_LIBRARY NAMES ${CZI_NAMES} HINTS ${CZI_LIBRARY_DIR})
find_library(JXRDECODE_LIBRARY NAMES libJxrDecodeStatic HINTS ${CZI_LIBRARY_DIR})
set(CZI_LIBRARIES ${LIBCZI_LIBRARY})
if ( JXRDECODE_LIBRARY )
set(CZI_LIBRARIES ${CZI_LIBRARIES} ${JXRDECODE_LIBRARY})
endif()
mark_as_advanced(CZI_LIBRARY)
message(CZI_INCLUDE_DIR="${CZI_INCLUDE_DIR}")
message(CZI_LIBRARIES="${CZI_LIBRARIES}")

#nd2
find_path(ND2_INCLUDE_DIR NAMES Nd2ReadSdk.h PATHS ${ND2_INCLUDE_DIR} /usr/local/nd2readsdk/include /usr/nd2readsdk/include /opt/nd2readsdk/include /opt/local/nd2readsdk/include)
include_directories(${ND2_INCLUDE_DIR})
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(ND2_NAMES libnd2readsdk-static.lib liblzma.lib liblimfile.lib libjbig.lib)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(ND2_NAMES libnd2readsdk-static.a liblzma.a liblimfile.a libjbig.a libnd2readsdk-shared.so liblimfile.so)
else()
	set(ND2_NAMES libnd2readsdk-shared.so liblimfile.so)
endif()
foreach(LIB ${ND2_NAMES})
	find_library(FOUND_LIB_${LIB} NAMES ${LIB} HINTS ${ND2_LIBRARY_DIR} /usr/local/nd2readsdk/lib /usr/nd2readsdk/lib /opt/nd2readsdk/lib /opt/local/nd2readsdk/lib)
	if (FOUND_LIB_${LIB})
		LIST(APPEND ND2_LIBRARIES ${FOUND_LIB_${LIB}})
	endif()
endforeach()
mark_as_advanced(ND2_LIBRARY)
message(ND2_INCLUDE_DIR="${ND2_INCLUDE_DIR}")
message(ND2_LIBRARIES="${ND2_LIBRARIES}")

#png
find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIR})

#bzip2
find_package(BZip2 REQUIRED)
include_directories(${BZIP2_INCLUDE_DIR})

#lz4
#find_package(LZ4 REQUIRED)
find_path(LZ4_INCLUDE_DIR NAMES lz4.h)
find_library(LZ4_LIBRARY NAMES lz4)
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
        LZ4 DEFAULT_MSG
        LZ4_LIBRARY LZ4_INCLUDE_DIR)

mark_as_advanced(LZ4_INCLUDE_DIR LZ4_LIBRARY)
include_directories(${LZ4_INCLUDE_DIR})

#blosc2
find_path(BLOSC_INCLUDE_DIR blosc2.h)
include_directories(${BLOSC_INCLUDE_DIR})
	if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(BLOSC_NAMES ${BLOSC_NAMES} libblosc2.lib)
else()
	set(BLOSC_NAMES ${BLOSC_NAMES} libblosc2.a)
endif()
find_library(BLOSC_LIBRARIES NAMES ${BLOSC_NAMES} HINTS ${BLOSC_LIBRARY_DIR} /usr/lib /usr/local/lib)
mark_as_advanced(BLOSC_LIBRARIES)
message(BLOSC_INCLUDE_DIR="${BLOSC_INCLUDE_DIR}")
message(BLOSC_LIBRARIES="${BLOSC_LIBRARIES}")

#freetype
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/freetype/include)
if (WIN32)
    file(GLOB FREETYPE_LIBRARIES ${VVDViewer_SOURCE_DIR}/fluorender/freetype/lib/*.lib)
else()
    find_package(Freetype REQUIRED)
    include_directories(${FREETYPE_INCLUDE_DIRS})
endif()

#Vulkan
file(GLOB vsrc ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Vulkan/*.cpp)
file(GLOB vhdr ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Vulkan/*.h ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Vulkan/*.hpp)
file(GLOB imgui_src ${VVDViewer_SOURCE_DIR}/external/imgui/*.cpp)
file(GLOB imgui_hdr ${VVDViewer_SOURCE_DIR}/external/imgui/*.h )
include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Vulkan)
include_directories(${VVDViewer_SOURCE_DIR}/external)
include_directories(${VVDViewer_SOURCE_DIR}/external/assimp)
include_directories(${VVDViewer_SOURCE_DIR}/external/gli)
#include_directories(${VVDViewer_SOURCE_DIR}/external/glm)
include_directories(${VVDViewer_SOURCE_DIR}/external/imgui)
find_package(Vulkan REQUIRED)
include_directories(${Vulkan_INCLUDE_DIR})
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)
    file(GLOB SPIRV_LIBRARIES_R "$ENV{VULKAN_SDK}/glslang/lib/*.lib")
	file(GLOB SPIRV_LIBRARIES_D "$ENV{VULKAN_SDK}/glslang/lib/Debug/*.lib")
	foreach(f ${SPIRV_LIBRARIES_R})
		if( NOT f MATCHES "SPIRV-Tools-shared.lib$" )
			set(SPIRV_LIBRARIES ${SPIRV_LIBRARIES} optimized ${f})
		endif()
	endforeach()
	foreach(f ${SPIRV_LIBRARIES_D})
		if( NOT f MATCHES "SPIRV-Tools-sharedd.lib$" )
			set(SPIRV_LIBRARIES ${SPIRV_LIBRARIES} debug ${f})
		endif()
	endforeach()
	include_directories("$ENV{VULKAN_SDK}/glslang/include")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	add_definitions(-DVK_USE_PLATFORM_MACOS_MVK)
    get_filename_component(VULKAN_LIB_DIR ${Vulkan_LIBRARY} DIRECTORY)
    file(GLOB SPIRV_LIBRARIES ${VULKAN_LIB_DIR}/*.a)
else()
    get_filename_component(VULKAN_LIB_DIR ${Vulkan_LIBRARY} DIRECTORY)
    file(GLOB SPIRV_LIBRARIES ${VULKAN_LIB_DIR}/*.a)
endif()
add_definitions(-DGLM_FORCE_DEPTH_ZERO_TO_ONE)


if( (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows") AND (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin") )
#GTK3
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
  include_directories(${GTK3_INCLUDE_DIRS})
  message(GTK3_INCLUDE_DIRS="${GTK3_INCLUDE_DIRS}")
  
#X11
  	find_package(X11)
  	include_directories(${X11_INCLUDE_DIR})
  	message(X11_INCLUDE_DIR="${X11_INCLUDE_DIR}")
  	message(X11_LIBRARIES="${X11_LIBRARIES}")
    add_definitions(-DVK_USE_PLATFORM_XCB_KHR)
#Wayland 
	FIND_PACKAGE(PkgConfig)
	PKG_CHECK_MODULES(PKG_WAYLAND QUIET wayland-client wayland-server wayland-egl wayland-cursor)

	SET(WAYLAND_DEFINITIONS ${PKG_WAYLAND_CFLAGS})

	FIND_PATH(WAYLAND_CLIENT_INCLUDE_DIR  NAMES wayland-client.h HINTS ${PKG_WAYLAND_INCLUDE_DIRS})
	FIND_PATH(WAYLAND_SERVER_INCLUDE_DIR  NAMES wayland-server.h HINTS ${PKG_WAYLAND_INCLUDE_DIRS})
	FIND_PATH(WAYLAND_EGL_INCLUDE_DIR     NAMES wayland-egl.h    HINTS ${PKG_WAYLAND_INCLUDE_DIRS})
	FIND_PATH(WAYLAND_CURSOR_INCLUDE_DIR  NAMES wayland-cursor.h HINTS ${PKG_WAYLAND_INCLUDE_DIRS})

	FIND_LIBRARY(WAYLAND_CLIENT_LIBRARIES NAMES wayland-client   HINTS ${PKG_WAYLAND_LIBRARY_DIRS})
	FIND_LIBRARY(WAYLAND_SERVER_LIBRARIES NAMES wayland-server   HINTS ${PKG_WAYLAND_LIBRARY_DIRS})
	FIND_LIBRARY(WAYLAND_EGL_LIBRARIES    NAMES wayland-egl      HINTS ${PKG_WAYLAND_LIBRARY_DIRS})
	FIND_LIBRARY(WAYLAND_CURSOR_LIBRARIES NAMES wayland-cursor   HINTS ${PKG_WAYLAND_LIBRARY_DIRS})

	set(WAYLAND_INCLUDE_DIR ${WAYLAND_CLIENT_INCLUDE_DIR} ${WAYLAND_SERVER_INCLUDE_DIR} ${WAYLAND_EGL_INCLUDE_DIR} ${WAYLAND_CURSOR_INCLUDE_DIR})
	set(WAYLAND_LIBRARIES ${WAYLAND_CLIENT_LIBRARIES} ${WAYLAND_SERVER_LIBRARIES} ${WAYLAND_EGL_LIBRARIES} ${WAYLAND_CURSOR_LIBRARIES})
	list(REMOVE_DUPLICATES WAYLAND_INCLUDE_DIR)

	pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
	if (NOT WAYLAND_PROTOCOLS_FOUND)
		message(FATAL_ERROR "Wayland protocols package not found")
	endif ()
	find_program(WAYLAND_SCANNER wayland-scanner)
	if (NOT WAYLAND_SCANNER)
		message(FATAL_ERROR "wayland-scanner binary not found")
	endif ()
	pkg_get_variable(protocol_dir wayland-protocols pkgdatadir)
	execute_process(COMMAND ${WAYLAND_SCANNER} client-header ${protocol_dir}/stable/xdg-shell/xdg-shell.xml ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h
		COMMAND ${WAYLAND_SCANNER} private-code ${protocol_dir}/stable/xdg-shell/xdg-shell.xml ${CMAKE_BINARY_DIR}/xdg-shell-protocol.c)
	include_directories(${WAYLAND_INCLUDE_DIR})
	include_directories(${CMAKE_BINARY_DIR})
    set(WAYLAND_LIBRARIES ${WAYLAND_CLIENT_LIBRARIES})
	add_definitions(-DVK_USE_PLATFORM_WAYLAND_KHR)
endif()

#other sources
file(GLOB srcall ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/*.cpp)
set(src)
foreach(f ${srcall})
  if( (NOT f MATCHES "Main.cpp$") AND (f MATCHES ".cpp$") )
    set(src ${src} ${f})
  endif()
endforeach()
file(GLOB hdrall ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/*.h)
set(hdr)
foreach(f ${hdrall})
  if( (NOT f MATCHES "Main.h$") AND (f MATCHES ".h$") )
    set(hdr ${hdr} ${f})
  endif()
endforeach()
file(GLOB rsc ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/img/*.xpm)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    file(GLOB mac_src ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/*.m)
    set(src ${src} ${mac_src})
endif()

set(src ${src} ${vsrc} ${imgui_src})
set(hdr ${hdr} ${vhdr} ${imgui_hdr})

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	include_directories(${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/WacUtils)
    include_directories(${VVDViewer_SOURCE_DIR}/fluorender/Wacom/Include)
    file(GLOB wac_src fluorender/FluoRender/WacUtils/*.cpp)
    file(GLOB wac_hdr fluorender/FluoRender/WacUtils/*.h)
	add_library(WACUTILS_OBJ OBJECT
				${wac_src} ${wac_hdr})
	add_library(VVDMainFrame SHARED
		${src} ${hdr} ${rsc}
		$<TARGET_OBJECTS:IMAGES_OBJ>
		$<TARGET_OBJECTS:CONVERTERS_OBJ>
		$<TARGET_OBJECTS:FLIVR_OBJ>
		$<TARGET_OBJECTS:ANIMATORS_OBJ>
		$<TARGET_OBJECTS:NV_OBJ>
		$<TARGET_OBJECTS:FORMATS_OBJ>
		$<TARGET_OBJECTS:TRACKING_OBJ>
		$<TARGET_OBJECTS:POLE_OBJ>
		$<TARGET_OBJECTS:TEEM_OBJ>
		$<TARGET_OBJECTS:PLUGINS_OBJ>
		$<TARGET_OBJECTS:WACUTILS_OBJ>)
else()
	add_library(VVDMainFrame SHARED
		${src} ${hdr} ${rsc}
		$<TARGET_OBJECTS:IMAGES_OBJ>
		$<TARGET_OBJECTS:CONVERTERS_OBJ>
		$<TARGET_OBJECTS:FLIVR_OBJ>
		$<TARGET_OBJECTS:ANIMATORS_OBJ>
		$<TARGET_OBJECTS:NV_OBJ>
		$<TARGET_OBJECTS:FORMATS_OBJ>
		$<TARGET_OBJECTS:TRACKING_OBJ>
		$<TARGET_OBJECTS:POLE_OBJ>
		$<TARGET_OBJECTS:TEEM_OBJ>
		$<TARGET_OBJECTS:PLUGINS_OBJ>)
endif()

#platform specific rules
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
   # Mac OS X
   if(NOT OPEN_GL_HEADER_LOC)
      add_definitions(-DOPEN_GL_HEADER_LOC=<OpenGL/gl.h>)
   endif()
   if(NOT OPEN_GLU_HEADER_LOC)
      add_definitions(-DOPEN_GLU_HEADER_LOC=<OpenGL/glu.h>)
   endif()
   add_definitions(-D_DARWIN)
   set(CFLAGS "-fPIC")
   set(CXXFLAGS "-fPIC ")
   include(BundleUtilities)
   #icon file for Apple, bundle properties
   set(ICON_FILE_PATH "FluoRender_icon")
   SET( MACOSX_BUNDLE_ICON_FILE ${ICON_FILE_PATH} )
   SET_SOURCE_FILES_PROPERTIES(${ICON_FILE_PATH} PROPERTIES 
      MACOSX_BUNDLE_LOCATION Resources)
   add_executable(VVDViewer MACOSX_BUNDLE
				  ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Main.h
				  ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Main.cpp)

elseif(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
   #Windows
   if(NOT OPEN_GL_HEADER_LOC)
      add_definitions(-DOPEN_GL_HEADER_LOC=<GL/gl.h>)
   endif()
   if(NOT OPEN_GLU_HEADER_LOC)
      add_definitions(-DOPEN_GLU_HEADER_LOC=<GL/glu.h>)
   endif()
   # windows
   if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
      add_definitions(-DWIN32)
      add_definitions(-D_WIN32)
	  set_target_properties(VVDMainFrame
					  IMAGES_OBJ
					  CONVERTERS_OBJ
					  FLIVR_OBJ
					  ANIMATORS_OBJ
					  NV_OBJ
					  FORMATS_OBJ
					  TRACKING_OBJ
					  POLE_OBJ
					  TEEM_OBJ
					  PLUGINS_OBJ
					  PROPERTIES
					  COMPILE_DEFINITIONS 
					  nrrd_EXPORTS
					  COMPILE_DEFINITIONS 
					  teem_EXPORTS
					  COMPILE_DEFINITIONS 
					  VVD_DLL_EXPORTS)
	  if(MSVC)
		  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
		  add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
		  add_definitions(-Dinline=__inline)
		  add_definitions("/wd4273")
		  add_definitions(-D_XKEYCHECK_H)
		  add_definitions(-DNOMINMAX)
		  add_definitions("/MP")
		  set(CFLAGS "")
		  set(CXXFLAGS "/EHsc")
		  #make sure the reference option is turned off and not incremental build linking
		  STRING(REPLACE "INCREMENTAL" "INCREMENTAL:NO" replacementFlags ${CMAKE_EXE_LINKER_FLAGS_DEBUG}) 
		  STRING(REPLACE "INCREMENTAL:NO:NO" "INCREMENTAL:NO" replacementFlags1 ${replacementFlags}) 
		  SET(CMAKE_EXE_LINKER_FLAGS_DEBUG "/INCREMENTAL:NO /OPT:NOREF ${replacementFlags1}" )

		  STRING(REPLACE "INCREMENTAL" "INCREMENTAL:NO" replacementFlags2 ${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO})
		  STRING(REPLACE "INCREMENTAL:NO:NO" "INCREMENTAL:NO" replacementFlags3 ${replacementFlags2})
		  SET(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/INCREMENTAL:NO /OPT:NOREF ${replacementFlags3}" )

		  SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /OPT:NOREF /FORCE:MULTIPLE")
		  SET(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")
		  SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG /FORCE:MULTIPLE")
		  SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -MDd")
		  SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -MDd")
		  SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -MT /O2 /Ob2 /Oi /Ot /Oy /GT /GL /Qpar")
		  SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -MT /O2 /Ob2 /Oi /Ot /Oy /GT /GL /Qpar")
	  endif()
      add_executable(VVDViewer WIN32
          			 ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Main.h
					 ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Main.cpp)
	else()
		add_definitions(-D_LINUX)
		add_definitions(-DLINUX)
		set(CFLAGS "-fPIC")
   		set(CXXFLAGS "-fPIC ")
		SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG")
		SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
		
		set(CMAKE_CXX_FLAGS "-Wall -Wextra")
		set(CMAKE_CXX_FLAGS_DEBUG "-g")
		set(CMAKE_CXX_FLAGS_RELEASE "-O3")
		
		add_executable(VVDViewer 
          			 ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Main.h
					 ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Main.cpp)
		add_executable(VVDViewerLauncher 
				     ${VVDViewer_SOURCE_DIR}/fluorender/Linux/launcher.cpp)
	endif()
endif()

#architecture specific rules
if(${ARCHITECTURE} MATCHES 64)
   if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
      set(ARCH_FLAGS "-m64 -arch x86_64")
   elseif(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
      set(ARCH_FLAGS "-march=x86-64 -mtune=generic")
   endif()
   add_definitions(-DTEEM_32BIT=0)
   set(CMAKE_C_FLAGS "${ARCH_FLAGS} ${CFLAGS}")
   set(CMAKE_CXX_FLAGS "${ARCH_FLAGS} ${CXXFLAGS} ${CXX_11_FLAG}")
   set(CMAKE_EXE_LINKER_FLAGS "${ARCH_FLAGS} ${CXX_11_FLAG}")
else()
   if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
      set(ARCH_FLAGS "-m32 -arch i386")
   elseif(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
      set(ARCH_FLAGS "-march=i386 -mtune=generic")
   endif()
   add_definitions(-DTEEM_32BIT=1)
   set(CMAKE_C_FLAGS "${ARCH_FLAGS} ${CFLAGS}" )
   set(CMAKE_CXX_FLAGS "${ARCH_FLAGS} ${CXXFLAGS} ${CXX_11_FLAG}")
   set(CMAKE_EXE_LINKER_FLAGS "${ARCH_FLAGS} ${CXX_11_FLAG}")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
   set(CMAKE_EXE_LINKER_FLAGS "-L/usr/local/lib -L/usr/lib ${CMAKE_EXE_LINKER_FLAGS} -framework IOSurface -framework Metal -framework VideoToolbox -framework CoreMedia -framework Security -framework CoreFoundation -framework CoreVideo -framework VideoDecodeAcceleration -framework QuartzCore")
   set(CMAKE_SHARED_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS})
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=10.9 -DWX_PRECOMP -std=c++14 -stdlib=libc++")
   #make sure png is static linked
   string(REGEX REPLACE "-lpng" "/usr/local/lib/libpng.a" 
			wxWidgets_LIBRARIES ${wxWidgets_LIBRARIES})
   string(REGEX REPLACE "-lpng" "/usr/local/lib/libpng.a" 
			CMAKE_MAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS})
   string(REGEX REPLACE "/usr/local/lib/libfreetype.dylib" "/usr/local/lib/libfreetype.a;/usr/local/lib/libpng.a" 
     FREETYPE_LIBRARIES ${FREETYPE_LIBRARIES})
   message(STATUS "${FREETYPE_LIBRARIES}")
endif()

#link the libraries
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_link_libraries(VVDMainFrame
	   ${CURL_LIBRARIES}
	   ${JPEG_LIBRARIES}
	   ${ZLIB_LIBRARIES}
	   ${TIFF_LIBRARIES}
	   ${OPENSSL_LIBRARIES} ws2_32.lib Wldap32.lib
	   ${ffmpeg_LIBRARIES} Secur32.lib  Normaliz.lib crypt32.lib Bcrypt.lib
	   ${Boost_LIBRARIES}
	   ${FREETYPE_LIBRARIES}
	   ${wxWidgets_LIBRARIES}
	   ${HDF5_LIBRARIES}
	   ${CZI_LIBRARIES}
	   ${ND2_LIBRARIES}
	   ${BZIP2_LIBRARIES}
	   ${LZ4_LIBRARIES}
	   ${BLOSC_LIBRARIES}
	   ${Vulkan_LIBRARY}
	   ${SPIRV_LIBRARIES})
	target_link_libraries(VVDViewer
	   VVDMainFrame
	   ws2_32.lib Wldap32.lib
	   ${wxWidgets_LIBRARIES})
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_libraries(VVDMainFrame
	   ${CURL_LIBRARIES}
	   ${JPEG_LIBRARIES}
	   ${ZLIB_LIBRARIES}
	   ${TIFF_LIBRARIES}
	   ${OPENSSL_LIBRARIES}
	   ${SSL_DEP_LIBRARIES}
	   ${ffmpeg_LIBRARIES}
	   ${Boost_LIBRARIES}
	   ${FREETYPE_LIBRARIES}
	   ${wxWidgets_LIBRARIES}
	   ${HDF5_LIBRARIES}
	   ${CZI_LIBRARIES}
	   ${ND2_LIBRARIES}
	   ${BZIP2_LIBRARIES}
	   ${LZ4_LIBRARIES}
	   ${PNG_LIBRARIES}
	   ${BLOSC_LIBRARIES}
       ${Vulkan_LIBRARY}
       ${SPIRV_LIBRARIES})
	target_link_libraries(VVDViewer
	   VVDMainFrame
	   ${wxWidgets_LIBRARIES})
else()
    set(LINK_LIBRARIES_LINUX
       ${CURL_LIBRARIES}
	   ${JPEG_LIBRARIES}
	   ${ZLIB_LIBRARIES}
	   ${TIFF_LIBRARIES}
	   ${OPENSSL_LIBRARIES}
	   ${SSL_DEP_LIBRARIES}
	   ${ffmpeg_LIBRARIES}
	   ${Boost_LIBRARIES}
	   ${FREETYPE_LIBRARIES}
	   ${wxWidgets_LIBRARIES}
	   ${HDF5_LIBRARIES}
	   ${CZI_LIBRARIES}
	   ${ND2_LIBRARIES}
	   ${BZIP2_LIBRARIES}
	   ${LZ4_LIBRARIES}
	   ${PNG_LIBRARIES}
	   ${BLOSC_LIBRARIES}
       ${Vulkan_LIBRARY}
       ${SPIRV_LIBRARIES}
	   ${GTK3_LIBRARIES}
	   ${X11_LIBRARIES} vdpau va va-drm va-x11)
	list(REMOVE_DUPLICATES LINK_LIBRARIES_LINUX)
	set(LINK_LIBRARIES_LINUX_FINAL)
	foreach(lib ${LINK_LIBRARIES_LINUX})
		if( lib MATCHES "libmcheck.a$" OR 
			lib MATCHES "libm-2.35.a$" OR 
			lib MATCHES "libc_nonshared.a$")
			message(lib="${lib}")
		elseif( lib MATCHES "libpixman-1.a$" OR
				lib MATCHES "libselinux.a$" OR
				lib MATCHES "libuuid.a$" OR
				lib MATCHES "libX11.a$" OR 
				lib MATCHES "libc.a$" OR
				lib MATCHES "libm.a$" OR
				lib MATCHES "libmvec.a$" OR
				lib MATCHES "libboost_chrono.a$" OR
				lib MATCHES "libboost_filesystem.a$" OR
				lib MATCHES "libfontconfig.a$" OR
				lib MATCHES "libhdf5_serial.a$" OR
				lib MATCHES "libhdf5_serial_cpp.a$" OR
				lib MATCHES "libXrender.a$" OR
				lib MATCHES "libXcomposite.a$" OR
				lib MATCHES "libXcursor.a$" OR
				lib MATCHES "libXdamage.a$" OR
				lib MATCHES "libXfixes.a$" OR
				lib MATCHES "libXext.a$" OR
				lib MATCHES "libSM.a$" OR
				lib MATCHES "libcairo.a$" OR
				lib MATCHES "libpng.a$" OR
				lib MATCHES "libpng16.a$" )
			message(lib="${lib}")
			string(REGEX REPLACE "a$" "so" lib ${lib})
			message(lib="${lib}")
			set(LINK_LIBRARIES_LINUX_FINAL ${LINK_LIBRARIES_LINUX_FINAL} ${lib})
		elseif( lib MATCHES "libx264.a$" )
			set( lib "/usr/lib/x86_64-linux-gnu/libx264.so" )
			set(LINK_LIBRARIES_LINUX_FINAL ${LINK_LIBRARIES_LINUX_FINAL} ${lib})
		else()
			set(LINK_LIBRARIES_LINUX_FINAL ${LINK_LIBRARIES_LINUX_FINAL} ${lib})
  		endif()
	endforeach()
	#set(LINK_LIBRARIES_LINUX_FINAL ${LINK_LIBRARIES_LINUX_FINAL} "/usr/lib/x86_64-linux-gnu/libva.so")
	message(LINK_LIBRARIES_LINUX_FINAL="${LINK_LIBRARIES_LINUX_FINAL}")

	target_link_libraries(VVDMainFrame ${LINK_LIBRARIES_LINUX_FINAL})
	target_link_libraries(VVDViewer
	   VVDMainFrame
	   ${wxWidgets_LIBRARIES})
	
	set_target_properties(VVDViewer PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        LINK_FLAGS "-Wl,-rpath,\\\$ORIGIN/lib/")
	set_target_properties(VVDMainFrame PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        LINK_FLAGS "-Wl,-rpath,\\\$ORIGIN/")
endif()


set(PLUGINBASE ${PLUGIN_DIR}/stdwx.h 
			   ${PLUGIN_DIR}/Declarations.h 
			   ${PLUGIN_DIR}/wxGuiPluginBase.h 
			   ${PLUGIN_DIR}/wxGuiPluginWindowBase.h 
			   ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/DLLExport.h
			   ${PLUGIN_DIR}/stdwx.cpp
			   ${PLUGIN_DIR}/wxGuiPluginBase.cpp
			   ${PLUGIN_DIR}/wxGuiPluginWindowBase.cpp)
add_library(wxGuiPluginBase SHARED ${PLUGINBASE})
target_link_libraries(wxGuiPluginBase ${wxWidgets_LIBRARIES})
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set_target_properties(wxGuiPluginBase PROPERTIES COMPILE_DEFINITIONS DEMO_PLUGIN_EXPORTS)
endif()

file(GLOB fi_src ${VVDViewer_SOURCE_DIR}/plugin_fiji/vvd/*.cpp)
file(GLOB fi_hdr ${VVDViewer_SOURCE_DIR}/plugin_fiji/vvd/*.h)
set(fi_ext ${PLUGIN_DIR}/stdwx.h ${PLUGIN_DIR}/stdwx.cpp)
add_library(FijiInterfacePlugin SHARED ${fi_src} ${fi_hdr} ${fi_ext})
target_link_libraries(FijiInterfacePlugin VVDMainFrame wxGuiPluginBase ${wxWidgets_LIBRARIES})

file(GLOB nb_src ${VVDViewer_SOURCE_DIR}/plugin_nblast/vvd/*.cpp)
file(GLOB nb_hdr ${VVDViewer_SOURCE_DIR}/plugin_nblast/vvd/*.h)
set(nb_ext ${PLUGIN_DIR}/stdwx.h ${PLUGIN_DIR}/stdwx.cpp)
add_library(NBLASTGuiPlugin SHARED ${nb_src} ${nb_hdr} ${nb_ext})
target_link_libraries(NBLASTGuiPlugin VVDMainFrame wxGuiPluginBase ${wxWidgets_LIBRARIES})

file(GLOB na_src ${VVDViewer_SOURCE_DIR}/plugin_NA/vvd/*.cpp)
file(GLOB na_hdr ${VVDViewer_SOURCE_DIR}/plugin_NA/vvd/*.h)
set(na_ext ${PLUGIN_DIR}/stdwx.h ${PLUGIN_DIR}/stdwx.cpp)
add_library(NeuronAnnotatorPlugin SHARED ${na_src} ${na_hdr} ${na_ext})
target_link_libraries(NeuronAnnotatorPlugin VVDMainFrame wxGuiPluginBase ${wxWidgets_LIBRARIES})

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
#TODO
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
#TODO
else()
	set_target_properties(FijiInterfacePlugin PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        LINK_FLAGS "-Wl,-rpath,\\\$ORIGIN/")
	set_target_properties(NBLASTGuiPlugin PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        LINK_FLAGS "-Wl,-rpath,\\\$ORIGIN/")
	set_target_properties(NeuronAnnotatorPlugin PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        LINK_FLAGS "-Wl,-rpath,\\\$ORIGIN/")
	set_target_properties(wxGuiPluginBase PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        LINK_FLAGS "-Wl,-rpath,\\\$ORIGIN/")
endif()

#build OpenCL examples copies.

#copy openCL examples to the binary directory
add_custom_command(TARGET VVDViewer PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "${VVDViewer_SOURCE_DIR}/CL_code"
  "$<TARGET_FILE_DIR:VVDViewer>/CL_code")
add_custom_command(TARGET VVDViewer PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "${VVDViewer_SOURCE_DIR}/CL_code"
  "${CMAKE_BINARY_DIR}/CL_code")
   
#copy font dir to the binary directory
add_custom_command(TARGET VVDViewer PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "${VVDViewer_SOURCE_DIR}/Fonts"
  "$<TARGET_FILE_DIR:VVDViewer>/Fonts")
add_custom_command(TARGET VVDViewer PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "${VVDViewer_SOURCE_DIR}/Fonts"
  "${CMAKE_BINARY_DIR}/Fonts")
  
#copy script dir to the binary directory
add_custom_command(TARGET VVDViewer PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "${VVDViewer_SOURCE_DIR}/Scripts"
  "$<TARGET_FILE_DIR:VVDViewer>/Scripts")

#copy executables (Windows)
#[[
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	add_custom_command(TARGET VVDViewer PRE_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${VVDViewer_SOURCE_DIR}/Executables"
		"$<TARGET_FILE_DIR:VVDViewer>/Executables")
	add_custom_command(TARGET VVDViewer PRE_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${VVDViewer_SOURCE_DIR}/Executables"
		"${CMAKE_BINARY_DIR}/Executables")
endif()
]]

#build DLL copy
#[[
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	file(GLOB ffmpeg_DLLS fluorender/bin/*.dll)
	foreach (f ${ffmpeg_DLLS})
		add_custom_command(TARGET VVDViewer PRE_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy  
			${f}
			$<TARGET_FILE_DIR:VVDViewer>)
	endforeach()
endif()
]]

#copy VVD DLLs
Set(vvd_dlls)
LIST(APPEND vvd_dlls "libVVDMainFrame.so")
LIST(APPEND vvd_dlls "libwxGuiPluginBase.so")
LIST(APPEND vvd_dlls "libFijiInterfacePlugin.so")
LIST(APPEND vvd_dlls "libNBLASTGuiPlugin.so")
LIST(APPEND vvd_dlls "libNeuronAnnotatorPlugin.so")
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
#TODO
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
#TODO
else()
	foreach (f ${vvd_dlls})
		add_custom_command(TARGET VVDViewer POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy  
			"${CMAKE_BINARY_DIR}/${f}"
			"$<TARGET_FILE_DIR:VVDViewer>/lib")
	endforeach()
endif()

#post build setting files copy
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	add_custom_command(TARGET VVDViewer PRE_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${VVDViewer_SOURCE_DIR}/Settings"
		"$<TARGET_FILE_DIR:VVDViewer>/../Resources")
else()
	file(GLOB settings_files Settings/*)
	foreach (f ${settings_files})
		add_custom_command(TARGET VVDViewer PRE_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy  
			${f}
			$<TARGET_FILE_DIR:VVDViewer>)
	endforeach()
endif()
