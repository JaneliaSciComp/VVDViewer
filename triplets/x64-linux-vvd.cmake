# VVDViewer custom triplet (Linux x64).
# All third-party libraries supplied by vcpkg are linked STATICALLY, with the
# shared-object exceptions listed below.
# NOTE: system display libraries (X11, Wayland, libva) and the Vulkan loader
# are NOT managed by vcpkg and remain dynamic system libraries.
# Keep x64-linux-vvd-release.cmake in sync with this file.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# wxWidgets is dynamic. The GLib/GTK (GObject) stack must ALSO be dynamic: it is
# linked into BOTH libwx_gtk3*.so and libVVDMainFrame.so, and GObject's type
# system is a process-global singleton. Two statically-embedded copies double-
# register the fundamental types and abort at startup ("cannot register existing
# type 'gchar'"). Shared .so libraries (one per SONAME) are loaded once, so both
# consumers share a single GObject instance — also required so that a GtkWidget
# created inside wxWidgets is recognised by VVDViewer's own gdk/gtk calls
# (VRenderView.cpp builds the Vulkan surface from the wx widget's GdkWindow).
# ffmpeg and libCZI are dynamic so that these (L)GPL libraries stay separate,
# user-replaceable shared objects (see the "gpl" feature in vcpkg.json).
if(PORT MATCHES "^(wxwidgets|glib|gtk3|gdk-pixbuf|pango|atk|at-spi2-core|at-spi2-atk|cairo|harfbuzz|graphene|libepoxy|ffmpeg|libczi)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
