#!/bin/bash
# macos_bundle_fixup.sh -- make a macOS .app bundle self-contained and
# relocatable, then re-sign it. Run as the LAST post-build step of
# VVDViewer.app (after fix_bundle.sh) and VVDViewerQt.app (after macdeployqt).
#
#   macos_bundle_fixup.sh <App.app> <libMoltenVK.dylib> <MoltenVK_icd.json|"">
#                         <libvulkan.dylib> <codesign identity|"-">
#                         [Qt plugin to drop, relative to Contents/PlugIns]...
#
# What it does:
#  1. Bundles the Vulkan driver: Contents/Frameworks/libMoltenVK.dylib plus
#     Contents/Resources/vulkan/icd.d/MoltenVK_icd.json (the macOS Vulkan
#     loader searches the bundle's Resources/vulkan/icd.d before the system
#     dirs, so no Vulkan SDK is needed on the target Mac). Also bundles the
#     Vulkan loader if nothing put one in Frameworks yet.
#  2. Optionally removes Qt plugins the app does not need (and, afterwards,
#     every library in Frameworks that nothing references any more).
#  3. For every Mach-O in the bundle: rewrites each non-system dependency to
#     @executable_path/../Frameworks/..., copying a dylib in when it is still
#     missing (e.g. a Homebrew dep macdeployqt did not follow); sets absolute
#     install names (LC_ID_DYLIB) to @rpath/<name>; deletes every LC_RPATH that
#     is not @executable_path/@loader_path relative (build dirs, vcpkg,
#     Homebrew, Vulkan SDK). The executable keeps @executable_path/../Frameworks.
#  4. Fails the build if anything still points outside the bundle or the OS.
#  5. Re-signs everything (install_name_tool invalidates signatures).
#
# Only /bin/bash 3.2 features (stock macOS). No paths of the build machine are
# hardcoded; CMake passes the MoltenVK / loader locations it found.
set -e
IFS='
'

APP="$1"; MVK_LIB="$2"; MVK_ICD="$3"; VULKAN_LIB="$4"; SIGN_ID="${5:--}"
shift 5 || true
DROP_PLUGINS="$*"

[ -d "${APP}/Contents" ] || { echo "macos_bundle_fixup: no bundle at ${APP}" >&2; exit 1; }
APP=$(cd "${APP}" && pwd)
CONTENTS="${APP}/Contents"
FW="${CONTENTS}/Frameworks"
FW_PREFIX="@executable_path/../Frameworks"
EXE_NAME=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "${CONTENTS}/Info.plist")
EXE="${CONTENTS}/MacOS/${EXE_NAME}"
mkdir -p "${FW}" "${CONTENTS}/Resources/vulkan/icd.d"

realpath_of() { python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$1"; }
is_macho() { file -b "$1" | grep -q 'Mach-O'; }
# all Mach-O files of the bundle, one per line (no symlinks)
# (framework binaries are often not +x, so test every file outside Resources)
machos() { find "${CONTENTS}" \( -path "${CONTENTS}/Resources" -o -name _CodeSignature -o -name Headers \) -prune -o -type f -print | while read -r f; do is_macho "$f" && echo "$f"; done; }
deps_of() { otool -L "$1" | grep '^[[:space:]]' | awk '{print $1}' | sort -u; }
id_of() { otool -D "$1" | grep -v ':$' | head -1; }
rpaths_of() { otool -l "$1" | awk '/cmd LC_RPATH/{getline; getline; print $2}'; }
sign() { local out; out=$(codesign --force --sign "${SIGN_ID}" --timestamp=none "$@" 2>&1) || { echo "${out}" >&2; return 1; }; }
is_system() { case "$1" in /System/*|/usr/lib/*) return 0;; esac; return 1; }

# --- 1. Vulkan driver (MoltenVK) + ICD manifest + loader --------------------
if [ ! -f "${MVK_LIB}" ]; then
	echo "macos_bundle_fixup: MoltenVK not found (${MVK_LIB}); install the Vulkan SDK or set MOLTENVK_LIBRARY" >&2
	exit 1
fi
cp -f "$(realpath_of "${MVK_LIB}")" "${FW}/libMoltenVK.dylib"
chmod u+w "${FW}/libMoltenVK.dylib"
ICD_OUT="${CONTENTS}/Resources/vulkan/icd.d/MoltenVK_icd.json"
if [ -n "${MVK_ICD}" ] && [ -f "${MVK_ICD}" ]; then
	# keep the SDK's api_version etc.; point library_path into the bundle
	sed -E 's|"library_path"[[:space:]]*:[[:space:]]*"[^"]*"|"library_path": "../../../Frameworks/libMoltenVK.dylib"|' \
		"${MVK_ICD}" > "${ICD_OUT}"
else
	cat > "${ICD_OUT}" <<'JSON'
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../../Frameworks/libMoltenVK.dylib",
        "api_version": "1.2.0",
        "is_portability_driver": true
    }
}
JSON
fi
if ! ls "${FW}"/libvulkan*.dylib >/dev/null 2>&1; then
	[ -f "${VULKAN_LIB}" ] || { echo "macos_bundle_fixup: Vulkan loader not found (${VULKAN_LIB})" >&2; exit 1; }
	cp -f "$(realpath_of "${VULKAN_LIB}")" "${FW}/libvulkan.1.dylib"
	chmod u+w "${FW}/libvulkan.1.dylib"
fi

# --- 2. drop unneeded Qt plugins --------------------------------------------
for p in ${DROP_PLUGINS}; do
	rm -f "${CONTENTS}/PlugIns/${p}"
done
find "${CONTENTS}/PlugIns" -type d -empty -delete 2>/dev/null || true

# --- 3. rewrite dependencies, install names and rpaths ----------------------
# map a dependency to its in-bundle replacement; copies a missing dylib in.
# $1 = dependency as recorded, $2 = binary that references it
bundle_ref() {
	local ref="$1" bin="$2" name rel src rp
	case "${ref}" in
		*.framework/*)
			# .../QtFoo.framework/Versions/A/QtFoo -> keep the part from QtFoo.framework/
			rel="${ref#"${ref%/*.framework/*}/"}"
			[ -e "${FW}/${rel}" ] && { echo "${FW_PREFIX}/${rel}"; return; }
			echo "MISSING"; return ;;
	esac
	name=$(basename "${ref}")
	if [ ! -e "${FW}/${name}" ]; then
		src=""
		case "${ref}" in
			/*) [ -f "${ref}" ] && src="${ref}" ;;
			@rpath/*)
				for rp in $(rpaths_of "${bin}") $(rpaths_of "${EXE}"); do
					case "${rp}" in /*) [ -f "${rp}/${ref#@rpath/}" ] && { src="${rp}/${ref#@rpath/}"; break; } ;; esac
				done ;;
		esac
		[ -n "${src}" ] || { echo "MISSING"; return; }
		cp -f "$(realpath_of "${src}")" "${FW}/${name}"
		chmod u+w "${FW}/${name}"
		echo "macos_bundle_fixup: bundled missing ${name} (from ${src})" >&2
	fi
	echo "${FW_PREFIX}/${name}"
}

# iterate until no new dylib is copied in (copied libs have their own deps)
pass=0
while :; do
	pass=$((pass + 1))
	before=$(ls "${FW}" | wc -l)
	for f in $(machos); do
		chmod u+w "$f"
		id=$(id_of "$f")
		case "${id}" in
			/*|"${APP}"*) install_name_tool -id "@rpath/$(basename "$f")" "$f" 2>/dev/null ;;
		esac
		for ref in $(deps_of "$f"); do
			[ "${ref}" = "${id}" ] && continue
			is_system "${ref}" && continue
			case "${ref}" in @executable_path/*|@loader_path/*) continue ;; esac
			new=$(bundle_ref "${ref}" "$f")
			if [ "${new}" = "MISSING" ]; then
				# leave it for the final check (reports it); dylibs that are
				# only optional dlopen() targets never appear as load commands
				continue
			fi
			install_name_tool -change "${ref}" "${new}" "$f" 2>/dev/null
		done
	done
	after=$(ls "${FW}" | wc -l)
	[ "${before}" = "${after}" ] && break
	[ ${pass} -lt 10 ] || break
done

# rpaths: only bundle-relative ones survive; the executable needs the
# Frameworks rpath for any remaining @rpath/ refs (e.g. its own libvulkan)
for f in $(machos); do
	for rp in $(rpaths_of "$f"); do
		case "${rp}" in
			@executable_path/*|@loader_path/*) ;;
			*) while rpaths_of "$f" | grep -qxF "${rp}"; do install_name_tool -delete_rpath "${rp}" "$f" 2>/dev/null; done ;;
		esac
	done
done
rpaths_of "${EXE}" | grep -qxF "${FW_PREFIX}" || install_name_tool -add_rpath "${FW_PREFIX}" "${EXE}"

# unreferenced libraries (left behind by dropped plugins): remove
if [ -n "${DROP_PLUGINS}" ]; then
	while :; do
		refs=$(for f in $(machos); do deps_of "$f" | grep -v -xF "$(id_of "$f")"; done | sed -n 's|^@executable_path/../Frameworks/||p; s|^@rpath/||p' | sed 's|/.*||' | sort -u)
		removed=0
		for e in $(ls "${FW}"); do
			case "${e}" in libMoltenVK.dylib|libvulkan*.dylib) continue ;; esac  # dlopen'd / loader
			if ! echo "${refs}" | grep -qxF "${e}"; then
				rm -rf "${FW:?}/${e}"; removed=1
				echo "macos_bundle_fixup: removed unreferenced ${e}"
			fi
		done
		[ ${removed} = 0 ] && break
	done
fi

# --- 4. verify ----------------------------------------------------------------
bad=0
for f in $(machos); do
	for ref in $(deps_of "$f"); do
		is_system "${ref}" && continue
		case "${ref}" in
			@executable_path/*) p="${CONTENTS}/MacOS/${ref#@executable_path/}" ;;
			@loader_path/*) p="$(dirname "$f")/${ref#@loader_path/}" ;;
			@rpath/*) [ "${ref}" = "$(id_of "$f")" ] && continue; p="${FW}/${ref#@rpath/}" ;;
			*) p="" ;;
		esac
		if [ -z "${p}" ] || [ ! -e "${p}" ]; then
			echo "macos_bundle_fixup: ERROR ${f#${APP}/} -> ${ref} is not inside the bundle" >&2; bad=1
		fi
	done
	for rp in $(rpaths_of "$f"); do
		case "${rp}" in @executable_path/*|@loader_path/*) ;; *) echo "macos_bundle_fixup: ERROR ${f#${APP}/} rpath ${rp}" >&2; bad=1 ;; esac
	done
done
[ ${bad} = 0 ] || exit 1

# --- 5. re-sign (inside out, then the whole bundle) --------------------------
# "-" = ad hoc: runs on this Mac and on others after the quarantine attribute
# is cleared; distribution needs a "Developer ID Application" identity plus
# notarization (see docs).
for f in $(machos); do
	[ "$f" = "${EXE}" ] && continue
	sign "$f"
done
for d in "${FW}"/*.framework; do
	[ -d "$d" ] && sign "$d"
done
sign --deep "${APP}"
codesign --verify --deep --strict "${APP}"
echo "macos_bundle_fixup: ${APP##*/} is self-contained and signed (${SIGN_ID})"
