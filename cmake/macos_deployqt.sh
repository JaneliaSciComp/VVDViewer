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
# VVD_QUICK_BUNDLE=1 (VVD_QT_QUICK_BUNDLE, local builds): nothing is deployed
# when the bundle already holds every Qt framework the executable links and
# macdeployqt, its arguments and those Qt libraries are the same as at the
# last run (stamp file next to the bundle); macos_bundle_fixup.sh then points
# the relinked executable at the bundled frameworks. A run of macdeployqt
# removes the fixup's stamp, so the fixup that follows does all of its work.
#
# Usage: macos_deployqt.sh <macdeployqt> <App.app> <qt-lib-dir|-> [macdeployqt args...]
set -u
MACDEPLOYQT="$1"; APP="$2"; LIBDIR="$3"; shift 3
EXE="${APP}/Contents/MacOS/$(basename "${APP}" .app)"
STAMP="${APP%.app}.deployqt-stamp"
QUICK="${VVD_QUICK_BUNDLE:-0}"
if [ "${QUICK}" = "1" ]; then
	# the Qt frameworks the executable links, and the files they come from
	fws=$(otool -L "${EXE}" | awk '/\.framework\// && $1 !~ /^\/System\// {print $1}' | sort -u)
	key=$(printf '%s\n' "${MACDEPLOYQT}" "${LIBDIR}" "$@"
		for f in ${fws}; do
			echo "${f##*/}"
			if [ "${f#/}" != "${f}" ]; then stat -L -f '%z %m' "${f}" 2>/dev/null; fi
		done)
	have=1
	for f in ${fws}; do
		name=$(echo "${f}" | sed -n 's|.*/\([^/]*\.framework\)/.*|\1|p')
		[ -n "${name}" ] && [ -d "${APP}/Contents/Frameworks/${name}" ] || { have=0; break; }
	done
	if [ ${have} = 1 ] && [ -f "${STAMP}" ] && [ "$(cat "${STAMP}")" = "${key}" ]; then
		echo "macos_deployqt: the Qt frameworks are deployed already"
		exit 0
	fi
	rm -f "${STAMP}" "${APP%.app}.fixup-stamp"
fi
if [ "${LIBDIR}" != "-" ] && [ -d "${LIBDIR}" ]; then
	install_name_tool -add_rpath "${LIBDIR}" "${EXE}" 2>/dev/null || true
fi
# A first run into an empty bundle cannot resolve the plugins' @rpath
# references to frameworks of other Qt kegs (QtSvg, QtPdf, ...): it looks them
# up through the rpaths of the libraries it has deployed so far. A second run,
# with those libraries in the bundle, finds them. Only the second run's
# errors matter.
"${MACDEPLOYQT}" "${APP}" "$@" >/dev/null 2>&1
"${MACDEPLOYQT}" "${APP}" "$@" || exit $?
[ "${QUICK}" = "1" ] && printf '%s' "${key}" > "${STAMP}"
exit 0
