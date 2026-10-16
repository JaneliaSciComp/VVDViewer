# Release-only variant of x64-linux-vvd (used by CI / release packaging).
# Identical to x64-linux-vvd.cmake except that vcpkg builds only the release
# configuration of each dependency (about half the build time, disk and cache).
# Only CMAKE_BUILD_TYPE=Release builds work with it. (Self-contained copy:
# vcpkg's ABI hash only covers this file. See x64-linux-vvd.cmake for why each
# dynamic port is dynamic.)
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)

if(PORT MATCHES "^(wxwidgets|glib|gtk3|gdk-pixbuf|pango|atk|at-spi2-core|at-spi2-atk|cairo|harfbuzz|graphene|libepoxy|ffmpeg|libczi)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
