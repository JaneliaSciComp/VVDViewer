#!/bin/bash
# macos_publish_bundle.sh <App.app> <dir> -- put a copy of a finished app
# bundle at <dir>/<App.app> in one step (VVD_QT_STAGED_BUNDLE, cmake/QtGui.cmake).
#
# The bundle is built (and made self-contained and signed) elsewhere; here it
# is copied next to the destination under a temporary name -- as an APFS
# clone, so no data is copied -- and then renamed over the old one. The app
# at <dir> is never half-built, and a copy of it that is running keeps the
# files it has open (the old bundle is only unlinked).
set -e
SRC="$1"; DIR="$2"
[ -d "${SRC}/Contents" ] || { echo "macos_publish_bundle: no bundle at ${SRC}" >&2; exit 1; }
NAME=$(basename "${SRC}")
DST="${DIR}/${NAME}"
TMP="${DIR}/.${NAME}.new"
OLD="${DIR}/.${NAME}.old"
mkdir -p "${DIR}"
rm -rf "${TMP}" "${OLD}"
# clonefile(2) copies on APFS; ditto elsewhere
cp -c -R "${SRC}" "${TMP}" 2>/dev/null || { rm -rf "${TMP}"; ditto "${SRC}" "${TMP}"; }
codesign --verify --deep --strict "${TMP}"
[ -e "${DST}" ] && mv "${DST}" "${OLD}"
mv "${TMP}" "${DST}"
rm -rf "${OLD}"
echo "macos_publish_bundle: ${DST}"
