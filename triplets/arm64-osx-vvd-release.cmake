# Release-only variant of arm64-osx-vvd (used by CI / release packaging).
# Identical to arm64-osx-vvd.cmake except that vcpkg builds only the release
# configuration of each dependency. Only CMAKE_BUILD_TYPE=Release builds work
# with it. (Self-contained copy: vcpkg's ABI hash only covers this file.)
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
# Minimum macOS version of the dependencies; keep in sync with
# CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt (the application).
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)
set(VCPKG_BUILD_TYPE release)

if(PORT MATCHES "^(wxwidgets|ffmpeg|libczi)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
