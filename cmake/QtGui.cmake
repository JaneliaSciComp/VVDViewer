# Qt front end (VVDViewerQt) -- phase 1 of the wxWidgets -> Qt port.
#
# Enabled with -DENABLE_QT_GUI=ON. Builds a second executable next to the wx
# app from the toolkit-neutral engine (the same object libraries VVDMainFrame
# uses) plus the sources under fluorender/FluoRender/QtGui/. wxWidgets is NOT
# linked into this target.
#
# Qt is not managed by vcpkg (qtbase from source adds 1-2 h to a first build);
# point CMake at an installed Qt 6 instead:
#   macOS:   brew install qt            (build.sh does this automatically)
#   Linux:   apt install qt6-base-dev   / Qt online installer
#   Windows: Qt online installer, then -DCMAKE_PREFIX_PATH=C:/Qt/6.x/msvc2019_64

if(NOT ENABLE_QT_GUI)
	return()
endif()

# Homebrew hint on macOS
if(APPLE AND NOT DEFINED Qt6_DIR AND NOT DEFINED CMAKE_PREFIX_PATH)
	execute_process(COMMAND brew --prefix qt OUTPUT_VARIABLE _brew_qt OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	if(_brew_qt)
		list(APPEND CMAKE_PREFIX_PATH "${_brew_qt}")
	endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
message(STATUS "Qt6 ${Qt6_VERSION} found: ${Qt6_DIR}")

set(QTGUI_DIR ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/QtGui)
file(GLOB qtgui_src ${QTGUI_DIR}/*.cpp)
file(GLOB qtgui_hdr ${QTGUI_DIR}/*.h)
# Qt ports of the three wx plugins, compiled into the app (batch 4b; the wx
# plugin libraries in plugin_*/vvd are built by the top-level CMakeLists)
file(GLOB qtplugin_src
	${VVDViewer_SOURCE_DIR}/plugin_fiji/qt/*.cpp
	${VVDViewer_SOURCE_DIR}/plugin_nblast/qt/*.cpp
	${VVDViewer_SOURCE_DIR}/plugin_NA/qt/*.cpp)
file(GLOB qtplugin_hdr
	${VVDViewer_SOURCE_DIR}/plugin_fiji/qt/*.h
	${VVDViewer_SOURCE_DIR}/plugin_nblast/qt/*.h
	${VVDViewer_SOURCE_DIR}/plugin_NA/qt/*.h)
list(APPEND qtgui_src ${qtplugin_src})
list(APPEND qtgui_hdr ${qtplugin_hdr})

# Non-GUI sources from the FluoRender/ top level (everything the wx frame
# shares with the engine). Keep this list explicit: it is the contract of
# what is toolkit-neutral today.
set(qt_engine_src
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/DataManager.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/RigidCellsWarp.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/VolumeSelector.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/VolumeCalculator.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/KernelExecutor.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/TextRenderer.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/TextureAtlas.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/AtlasTexture.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/AreaAllocator.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Area.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/AreaList.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Font.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Glyph.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/utility.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/VVulkan.cpp
	${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/img/icons.cpp
	${vsrc} ${imgui_src})
if(APPLE)
	list(APPEND qt_engine_src ${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/MetalCompatibility.m)
endif()

set(CMAKE_AUTOMOC ON)

if(APPLE)
	add_executable(VVDViewerQt MACOSX_BUNDLE ${qtgui_src} ${qtgui_hdr} ${qt_engine_src})
	set_target_properties(VVDViewerQt PROPERTIES
		MACOSX_BUNDLE_BUNDLE_NAME "VVDViewerQt"
		MACOSX_BUNDLE_GUI_IDENTIFIER "org.janelia.vvdviewer.qt"
		MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}")
elseif(WIN32)
	add_executable(VVDViewerQt WIN32 ${qtgui_src} ${qtgui_hdr} ${qt_engine_src})
else()
	add_executable(VVDViewerQt ${qtgui_src} ${qtgui_hdr} ${qt_engine_src})
endif()

target_sources(VVDViewerQt PRIVATE
	$<TARGET_OBJECTS:CORE_OBJ>
	$<TARGET_OBJECTS:FLIVR_OBJ>
	$<TARGET_OBJECTS:FORMATS_OBJ>
	$<TARGET_OBJECTS:TRACKING_OBJ>
	$<TARGET_OBJECTS:ANIMATORS_OBJ>
	$<TARGET_OBJECTS:NV_OBJ>
	$<TARGET_OBJECTS:CONVERTERS_OBJ>
	$<TARGET_OBJECTS:POLE_OBJ>
	$<TARGET_OBJECTS:TEEM_OBJ>)

# BEFORE: QtGui/ must win over the wx headers of the same name in
# fluorender/FluoRender (TreePanel.h); the plugin_*/qt sources are not in QtGui/,
# so a quoted include does not find the Qt sibling first.
target_include_directories(VVDViewerQt BEFORE PRIVATE ${QTGUI_DIR}
	${VVDViewer_SOURCE_DIR}/plugin_fiji/qt
	${VVDViewer_SOURCE_DIR}/plugin_nblast/qt
	${VVDViewer_SOURCE_DIR}/plugin_NA/qt)
# The SDK's vulkan headers must win over the older copy in external/vulkan
# (VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR on macOS). Qt6::Gui pulls
# in WrapVulkanHeaders, whose SYSTEM interface include is the same SDK
# directory; CMake then demotes it to -isystem (searched after every -I) for
# this target only. Do not treat imported targets' includes as system here,
# and list the SDK first explicitly.
set_property(TARGET VVDViewerQt PROPERTY NO_SYSTEM_FROM_IMPORTED ON)
target_include_directories(VVDViewerQt BEFORE PRIVATE ${Vulkan_INCLUDE_DIR})
# ...but glm must be the bundled copy (fluorender/glm) the engine object
# libraries are compiled with; the SDK ships a newer glm whose types mangle
# differently (glm::qualifier vs glm::precision), which breaks linking against
# engine functions taking glm arguments. BEFORE prepends, so this goes first.
target_include_directories(VVDViewerQt BEFORE PRIVATE ${VVDViewer_SOURCE_DIR}/fluorender)
# QT_NO_KEYWORDS: the FLIVR shader classes have a method called emit().
target_compile_definitions(VVDViewerQt PRIVATE VVD_QT_STRING_INTEROP=1 QT_NO_KEYWORDS)
if(WIN32)
	# The engine sources compiled into this executable declare their symbols
	# EXPORT_API; without VVD_DLL_EXPORTS that is dllimport, and defining a
	# dllimport static (icons.cpp) is an error (C2491).
	target_compile_definitions(VVDViewerQt PRIVATE VVD_DLL_EXPORTS)
	# The top-level _HAS_STD_BYTE=0 (CMakeLists.txt) breaks Qt 6 headers
	# (QByteArrayView needs std::byte), and every source here includes <QString>
	# through VVDString.h (VVD_QT_STRING_INTEROP), engine files too. Keep
	# std::byte for the whole target, and force-include WinCompat.h so that
	# <windows.h>/<ole2.h> (the SDK's ::byte) are parsed before any engine
	# header's "using namespace std" can make unqualified 'byte' ambiguous.
	set_property(TARGET VVDViewerQt PROPERTY VVD_ALLOW_STD_BYTE ON)
	target_compile_options(VVDViewerQt PRIVATE "/FI${VVDViewer_SOURCE_DIR}/fluorender/FluoRender/Core/WinCompat.h")
endif()

if(APPLE)
	target_link_libraries(VVDViewerQt PRIVATE
		Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
		${CURL_LIBRARIES} ${JPEG_LIBRARIES} ${ZLIB_LIBRARIES} ${TIFF_LIBRARIES} ${LZMA_LIBRARY}
		${OPENSSL_LIBRARIES} ${SSL_DEP_LIBRARIES} ${ffmpeg_LIBRARIES} ${Boost_LIBRARIES}
		${FREETYPE_LIBRARIES} ${HDF5_LIBRARIES} ${CZI_LIBRARIES} ${BZIP2_LIBRARIES} ${LZ4_LIBRARIES}
		${PNG_LIBRARIES} ${BLOSC_LIBRARIES} ${ZSTD_LIBRARIES} ${ZFP_LIBRARY} ${Vulkan_LIBRARY} ${SPIRV_LIBRARIES}
		"-framework Cocoa" "-framework QuartzCore" "-framework Metal" "-framework IOKit" "-framework IOSurface")
elseif(WIN32)
	target_link_libraries(VVDViewerQt PRIVATE
		Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
		${CURL_LIBRARIES} ${JPEG_LIBRARIES} ${ZLIB_LIBRARIES} ${PNG_LIBRARIES} ${TIFF_LIBRARIES}
		${OPENSSL_LIBRARIES} ws2_32.lib Wldap32.lib ${ffmpeg_LIBRARIES} Secur32.lib Normaliz.lib crypt32.lib
		Bcrypt.lib windowscodecs.lib ${ZFP_LIBRARY} ${Boost_LIBRARIES} ${FREETYPE_LIBRARIES} ${HDF5_LIBRARIES}
		${CZI_LIBRARIES} ${ND2_LIBRARIES} ${BZIP2_LIBRARIES} ${LZ4_LIBRARIES} ${BLOSC_LIBRARIES} ${ZSTD_LIBRARIES}
		${Vulkan_LIBRARY} ${SPIRV_LIBRARIES})
else()
	set(_qt_linux_libs ${LINK_LIBRARIES_LINUX})
	list(REMOVE_ITEM _qt_linux_libs ${wxWidgets_LIBRARIES})
	target_link_libraries(VVDViewerQt PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network ${_qt_linux_libs} ${SPIRV_GROUP} xcb)
endif()

# Resources the engine expects next to the executable / in the bundle
set(qtplugin_res
	${VVDViewer_SOURCE_DIR}/plugin_fiji/fiji/macros/vvd_listener.txt
	${VVDViewer_SOURCE_DIR}/plugin_fiji/fiji/macros/vvd_run.txt
	${VVDViewer_SOURCE_DIR}/plugin_fiji/fiji/macros/vvd_quit.txt
	${VVDViewer_SOURCE_DIR}/plugin_fiji/fiji/plugins/vvd_listener.jar
	${VVDViewer_SOURCE_DIR}/plugin_fiji/fiji/plugins/Apply_Bigwarp_Filter.jar
	${VVDViewer_SOURCE_DIR}/plugin_nblast/r/nblast_search.R)
if(APPLE)
	add_custom_command(TARGET VVDViewerQt POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${VVDViewer_SOURCE_DIR}/Settings" "$<TARGET_FILE_DIR:VVDViewerQt>/../Resources"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${VVDViewer_SOURCE_DIR}/CL_code" "$<TARGET_FILE_DIR:VVDViewerQt>/../Resources/CL_code"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${VVDViewer_SOURCE_DIR}/Fonts" "$<TARGET_FILE_DIR:VVDViewerQt>/../Resources/Fonts"
		# plugin files (wx StartFiji copies these from Resources into Fiji;
		# runNBLAST runs Resources/nblast_search.R)
		COMMAND ${CMAKE_COMMAND} -E copy ${qtplugin_res} "$<TARGET_FILE_DIR:VVDViewerQt>/../Resources/"
		COMMENT "Copying VVDViewerQt resources")
	# Qt frameworks + MoltenVK into the bundle for a relocatable app
	find_program(MACDEPLOYQT macdeployqt HINTS ${_brew_qt}/bin ${Qt6_DIR}/../../../bin)
	if(MACDEPLOYQT)
		# Homebrew splits Qt into one keg per module (qtbase, qtsvg, ...), so
		# frameworks referenced by plugins (QtSvg, QtPdf, ...) are not next to
		# QtCore; they are all linked under <brew prefix>/lib.
		set(_macdeployqt_libpath)
		execute_process(COMMAND brew --prefix OUTPUT_VARIABLE _brew_prefix OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
		if(_brew_prefix)
			set(_macdeployqt_libpath -libpath=${_brew_prefix}/lib)
		endif()
		set(_qt_fw_dir -)
		if(_brew_prefix)
			set(_qt_fw_dir "${_brew_prefix}/lib")
		endif()
		# (macos_deployqt.sh: why the executable needs an rpath to that dir)
		add_custom_command(TARGET VVDViewerQt POST_BUILD
			COMMAND /bin/bash "${VVDViewer_SOURCE_DIR}/cmake/macos_deployqt.sh" ${MACDEPLOYQT}
				"$<TARGET_BUNDLE_DIR:VVDViewerQt>" "${_qt_fw_dir}" -always-overwrite ${_macdeployqt_libpath}
			COMMENT "macdeployqt VVDViewerQt.app")
	else()
		message(WARNING "macdeployqt not found: VVDViewerQt.app will not contain Qt and only runs on this machine")
	endif()
	# Last step: bundle MoltenVK + ICD manifest, rewrite every remaining
	# non-system reference into Frameworks, strip build-machine LC_RPATHs
	# (vcpkg_installed, /opt/homebrew/opt/qt/lib), re-sign. Also drops Qt
	# plugins the app does not use, together with what only they pulled in:
	#  - virtual keyboard input context (QtQuick/QtQml/...),
	#  - glib network-information backend (macOS has its own),
	#  - PDF image format (QtPdf, from qtwebengine),
	#  - OpenSSL TLS backend (the app only uses plain QTcpSocket/QTcpServer;
	#    SecureTransport stays. libssl/libcrypto remain: QtNetwork links them).
	add_custom_command(TARGET VVDViewerQt POST_BUILD
		COMMAND /bin/bash "${VVDViewer_SOURCE_DIR}/cmake/macos_bundle_fixup.sh"
			"$<TARGET_BUNDLE_DIR:VVDViewerQt>" "${MOLTENVK_LIBRARY}" "${MOLTENVK_ICD_JSON}"
			"${Vulkan_LIBRARY}" "${VVD_CODESIGN_IDENTITY}"
			platforminputcontexts/libqtvirtualkeyboardplugin.dylib
			networkinformation/libqglib.dylib
			imageformats/libqpdf.dylib
			tls/libqopensslbackend.dylib
		COMMENT "Making VVDViewerQt.app self-contained (MoltenVK, rpaths, codesign)")
else()
	add_custom_command(TARGET VVDViewerQt POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${VVDViewer_SOURCE_DIR}/CL_code" "$<TARGET_FILE_DIR:VVDViewerQt>/CL_code"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${VVDViewer_SOURCE_DIR}/Fonts" "$<TARGET_FILE_DIR:VVDViewerQt>/Fonts"
		COMMAND ${CMAKE_COMMAND} -E copy ${qtplugin_res} "$<TARGET_FILE_DIR:VVDViewerQt>/"
		COMMENT "Copying VVDViewerQt resources")
endif()

if(WIN32)
	# A runnable build output, as the wx target gets (CMakeLists.txt): the
	# settings files (fluorender.set, default_*.dft; the wx target copies them
	# only when it is built), the app-local VC++ runtime, and the Qt DLLs with
	# the platform plugin (windeployqt). The vcpkg DLLs it uses (ffmpeg,
	# libCZI) are copied by vcpkg's applocal step (VCPKG_APPLOCAL_DEPS);
	# copying the whole vcpkg bin directory here as well races with the
	# plugins' applocal step in a parallel build (permission denied).
	add_custom_command(TARGET VVDViewerQt POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${VVDViewer_SOURCE_DIR}/Settings" "$<TARGET_FILE_DIR:VVDViewerQt>"
		COMMENT "Copying the settings files next to VVDViewerQt.exe")
	if(_vvd_crt_libs)   # found by the wx target's block in CMakeLists.txt
		add_custom_command(TARGET VVDViewerQt POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_vvd_crt_libs} "$<TARGET_FILE_DIR:VVDViewerQt>"
			COMMAND_EXPAND_LISTS
			COMMENT "Deploying app-local VC++ runtime (CRT) DLLs next to VVDViewerQt.exe")
	endif()
	# the windeployqt of the Qt the target links (Qt6CoreTools); the cached
	# find_program result could stay at another Qt after Qt6_DIR changed
	if(TARGET Qt6::windeployqt)
		set(VVD_WINDEPLOYQT_EXE "$<TARGET_FILE:Qt6::windeployqt>")
	else()
		unset(VVD_WINDEPLOYQT CACHE)
		find_program(VVD_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin" NO_DEFAULT_PATH NO_CACHE)
		set(VVD_WINDEPLOYQT_EXE "${VVD_WINDEPLOYQT}")
	endif()
	if(VVD_WINDEPLOYQT_EXE)
		add_custom_command(TARGET VVDViewerQt POST_BUILD
			COMMAND "${VVD_WINDEPLOYQT_EXE}" $<IF:$<CONFIG:Debug>,--debug,--release> --no-translations
				--no-system-d3d-compiler --no-opengl-sw --verbose 0 "$<TARGET_FILE:VVDViewerQt>"
			COMMENT "windeployqt VVDViewerQt.exe")
	else()
		message(WARNING "windeployqt not found next to Qt (${Qt6_DIR}): VVDViewerQt.exe needs Qt's bin directory on PATH")
	endif()
endif()
