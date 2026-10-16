# VVDViewer custom triplet (macOS Intel / x86_64).
# All third-party libraries are linked STATICALLY. Exceptions built as dylibs
# (dynamic): wxWidgets, plus ffmpeg and libCZI ((L)GPL libraries kept as
# separate, user-replaceable dylibs).
# Keep x64-osx-vvd-release.cmake in sync with this file.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)
# Minimum macOS version of the dependencies; keep in sync with
# CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt (the application).
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)

if(PORT MATCHES "^(wxwidgets|ffmpeg|libczi)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
