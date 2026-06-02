# VVDViewer custom triplet (Windows x64).
# All third-party libraries are linked STATICALLY, against the DYNAMIC CRT
# (/MD, /MDd) to match the project's -MD/-MDd flags in CMakeLists.txt.
# wxWidgets is the SOLE exception: it is built as a DLL (dynamic).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

if(PORT STREQUAL "wxwidgets")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
