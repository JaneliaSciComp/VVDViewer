# VVDViewer custom triplet (macOS Intel / x86_64).
# All third-party libraries are linked STATICALLY; wxWidgets is the SOLE
# exception and is built as a dylib (dynamic).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)

if(PORT STREQUAL "wxwidgets")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
