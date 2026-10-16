# VVDViewer custom triplet (Windows x64).
# All third-party libraries are linked STATICALLY, against the DYNAMIC CRT
# (/MD, /MDd) to match the project's -MD/-MDd flags in CMakeLists.txt.
# Exceptions built as DLLs (dynamic):
#   * wxWidgets
#   * ffmpeg and libCZI: (L)GPL libraries are kept as separate, user-replaceable
#     DLLs so the LGPL obligations stay confined to those DLLs (see the "gpl"
#     feature in vcpkg.json and README "Licensing of binary packages").
# Keep x64-windows-vvd-release.cmake in sync with this file.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

if(PORT MATCHES "^(wxwidgets|ffmpeg|libczi)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
