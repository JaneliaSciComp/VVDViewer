#!/bin/bash
# Run macdeployqt on an app bundle after giving the executable an rpath to
# the directory that holds every Qt module's framework (Homebrew splits Qt into
# one keg per module and links all frameworks under <brew prefix>/lib).
# macdeployqt resolves a plugin's @rpath references (iconengines -> QtSvg,
# imageformats -> QtPdf, ...) only through the rpaths it has collected, not
# through -libpath, and vcpkg's applocal step (which runs first and copies the
# ffmpeg / libCZI dylibs) rewrites the executable's rpaths, so a link-time
# rpath does not survive. macos_bundle_fixup.sh removes the rpath again.
#
# Usage: macos_deployqt.sh <macdeployqt> <App.app> <qt-lib-dir|-> [macdeployqt args...]
set -u
MACDEPLOYQT="$1"; APP="$2"; LIBDIR="$3"; shift 3
EXE="${APP}/Contents/MacOS/$(basename "${APP}" .app)"
if [ "${LIBDIR}" != "-" ] && [ -d "${LIBDIR}" ]; then
	install_name_tool -add_rpath "${LIBDIR}" "${EXE}" 2>/dev/null || true
fi
# A first run into an empty bundle cannot resolve the plugins' @rpath
# references to frameworks of other Qt kegs (QtSvg, QtPdf, ...): it looks them
# up through the rpaths of the libraries it has deployed so far. A second run,
# with those libraries in the bundle, finds them. Only the second run's
# errors matter.
"${MACDEPLOYQT}" "${APP}" "$@" >/dev/null 2>&1
exec "${MACDEPLOYQT}" "${APP}" "$@"
