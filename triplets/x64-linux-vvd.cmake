# VVDViewer custom triplet (Linux x64).
# All third-party libraries supplied by vcpkg are linked STATICALLY; wxWidgets
# is the SOLE exception and is built as a shared object (dynamic).
# NOTE: system display libraries (GTK3, X11, Wayland, libva) and the Vulkan
# loader are NOT managed by vcpkg and remain dynamic system libraries.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT STREQUAL "wxwidgets")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
