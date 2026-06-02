# VVDViewer custom triplet (macOS Apple Silicon / arm64).
# All third-party libraries are linked STATICALLY; wxWidgets is the SOLE
# exception and is built as a dylib (dynamic). Matches the project's
# -arch arm64 build on macOS.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

if(PORT STREQUAL "wxwidgets")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
