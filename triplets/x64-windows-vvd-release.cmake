# Release-only variant of x64-windows-vvd (used by CI / release packaging).
# Identical to x64-windows-vvd.cmake except that vcpkg builds only the release
# configuration of each dependency, which roughly halves the dependency build
# time and binary-cache size. Only CMAKE_BUILD_TYPE=Release builds work with it.
# (A self-contained copy rather than include(): vcpkg's ABI hash only covers
# the triplet file itself, so an included file's edits would not be noticed.)
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

if(PORT MATCHES "^(wxwidgets|ffmpeg|libczi)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
