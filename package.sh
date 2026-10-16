#!/usr/bin/env bash
#
# Creates the release package of VVDViewer on Linux (.tar.xz) or macOS (.dmg)
# from the output of build.sh:
#
#   Linux: dist/VVDViewer<stamp>-<distro><ver>-vulkan[-gpl].tar.xz
#          VVDViewer (launcher script) + data/ (application, bundled libraries,
#          plugins, resources). Libraries that vcpkg or the Vulkan SDK provided
#          are bundled; system libraries (glibc, libstdc++, X11, Wayland, ...)
#          are not, so the package runs on the build distro release or newer.
#   macOS: dist/VVDViewer<stamp>-macosx[-intel]-vulkan[-gpl].dmg
#          VVDViewer.app (signed) + a link to /Applications.
#
#   Both:  dist/<name>-sources.tar.xz  source code of the (L)GPL components
#          dist/*.sha256               checksums
#
# The ffmpeg license mode (LGPL by default, GPL when built with
# build.sh --enable-gpl) is detected from the build; GPL packages get "-gpl".
#
# Usage: bash package.sh [options]
#
#   --build              run build.sh first (Release); --enable-gpl, --triplet
#                        and --cmake-arg are passed through
#   --enable-gpl / --triplet NAME / --cmake-arg ARG   (with --build)
#   --build-dir DIR      build directory (default: ./build)
#   --out-dir DIR        output directory (default: ./dist)
#   --stamp TEXT         identifier in the file name (default: today, MMDDYY)
#   --nblast-jar FILE    include NBLAST_Skeletonize.jar (not in the repository)
#   --no-sources         do not create the sources archive
#   --no-strip           (Linux) do not strip the bundled ELF files
#
#   macOS signing / notarization (default: ad-hoc signature):
#   --sign-identity ID   e.g. "Developer ID Application: Name (TEAMID)"
#                        (env VVD_MAC_SIGN_IDENTITY)
#   --entitlements FILE  default: packaging/macos/VVDViewer.entitlements
#   --notary-profile P   notarize with a `xcrun notarytool store-credentials`
#                        keychain profile (env VVD_NOTARY_PROFILE), or
#   --notary-key FILE --notary-key-id ID --notary-issuer ID
#                        an App Store Connect API key (env VVD_NOTARY_KEY,
#                        VVD_NOTARY_KEY_ID, VVD_NOTARY_ISSUER)
#   --keychain FILE      keychain holding the signing identity (CI)
#
#   Linux signing:
#   --gpg-key ID         also write detached GPG signatures (.asc) of the
#                        archives (env VVD_GPG_KEY)

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO/build"
OUT_DIR="$REPO/dist"
STAMP="$(date +%m%d%y)"
DO_BUILD=0
NBLAST_JAR=""
NO_SOURCES=0
NO_STRIP=0
BUILD_ARGS=()
SIGN_ID="${VVD_MAC_SIGN_IDENTITY:-}"
ENTITLEMENTS="$REPO/packaging/macos/VVDViewer.entitlements"
NOTARY_PROFILE="${VVD_NOTARY_PROFILE:-}"
NOTARY_KEY="${VVD_NOTARY_KEY:-}"
NOTARY_KEY_ID="${VVD_NOTARY_KEY_ID:-}"
NOTARY_ISSUER="${VVD_NOTARY_ISSUER:-}"
KEYCHAIN=""
GPG_KEY="${VVD_GPG_KEY:-}"

usage() { sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; }
log()  { printf '\033[1;36m%s\033[0m\n' "$*"; }
warn() { printf '\033[1;33mWARNING:\033[0m %s\n' "$*" >&2; }
fail() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --build)          DO_BUILD=1 ;;
    --enable-gpl)     BUILD_ARGS+=(--enable-gpl) ;;
    --triplet)        shift; BUILD_ARGS+=(--triplet "${1:?--triplet needs a name}") ;;
    --cmake-arg)      shift; BUILD_ARGS+=(--cmake-arg "${1:?--cmake-arg needs a value}") ;;
    --build-dir)      shift; BUILD_DIR="${1:?--build-dir needs a directory}" ;;
    --out-dir)        shift; OUT_DIR="${1:?--out-dir needs a directory}" ;;
    --stamp)          shift; STAMP="${1:?--stamp needs a value}" ;;
    --nblast-jar)     shift; NBLAST_JAR="${1:?--nblast-jar needs a file}" ;;
    --no-sources)     NO_SOURCES=1 ;;
    --no-strip)       NO_STRIP=1 ;;
    --sign-identity)  shift; SIGN_ID="${1:?--sign-identity needs a value}" ;;
    --entitlements)   shift; ENTITLEMENTS="${1:?--entitlements needs a file}" ;;
    --notary-profile) shift; NOTARY_PROFILE="${1:?--notary-profile needs a name}" ;;
    --notary-key)     shift; NOTARY_KEY="${1:?--notary-key needs a file}" ;;
    --notary-key-id)  shift; NOTARY_KEY_ID="${1:?--notary-key-id needs a value}" ;;
    --notary-issuer)  shift; NOTARY_ISSUER="${1:?--notary-issuer needs a value}" ;;
    --keychain)       shift; KEYCHAIN="${1:?--keychain needs a file}" ;;
    --gpg-key)        shift; GPG_KEY="${1:?--gpg-key needs a key id}" ;;
    -h|--help)        usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
  shift
done

OS="$(uname -s)"
case "$OS" in Linux|Darwin) ;; *) fail "Unsupported OS: $OS (use package.ps1 on Windows)" ;; esac
log "=== VVDViewer packaging ($OS) ==="

# Portable realpath (macOS 12 has no readlink -f).
abspath() { (cd "$(dirname "$1")" && printf '%s/%s\n' "$(pwd -P)" "$(basename "$1")"); }

# 1) Build (optional) and read the build configuration ------------------------
if [ "$DO_BUILD" = "1" ]; then
  bash "$REPO/build.sh" --build-dir "$BUILD_DIR" ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}
fi
CACHE="$BUILD_DIR/CMakeCache.txt"
[ -f "$CACHE" ] || fail "No CMake build found in $BUILD_DIR. Run build.sh first (or pass --build)."
cache_get() { sed -n "s/^$1:[A-Z]*=//p" "$CACHE" | head -1; }

TRIPLET="$(cache_get VCPKG_TARGET_TRIPLET)"
INSTALLED_ROOT="$(cache_get VCPKG_INSTALLED_DIR)"; [ -n "$INSTALLED_ROOT" ] || INSTALLED_ROOT="$BUILD_DIR/vcpkg_installed"
INSTALLED="$INSTALLED_ROOT/$TRIPLET"
TOOLCHAIN="$(cache_get CMAKE_TOOLCHAIN_FILE)"
if [ -n "$TOOLCHAIN" ]; then VCPKG_ROOT_DIR="$(cd "$(dirname "$TOOLCHAIN")/../.." && pwd)"; else VCPKG_ROOT_DIR="$REPO/vcpkg"; fi
ND2="$(cache_get ENABLE_ND2)"
VK_INC="$(cache_get Vulkan_INCLUDE_DIR)"
if [ -n "$VK_INC" ]; then VK_SDK="$(cd "$VK_INC/.." && pwd)"; else VK_SDK="${VULKAN_SDK:-}"; fi
[ -d "$INSTALLED/share" ] || fail "vcpkg installed tree not found: $INSTALLED"

FF_COPYRIGHT="$INSTALLED/share/ffmpeg/copyright"
[ -f "$FF_COPYRIGHT" ] || fail "ffmpeg is not installed in $INSTALLED."
FF_HEAD="$(head -5 "$FF_COPYRIGHT" | tr '\n' ' ')"
case "$FF_HEAD" in
  *"GNU GENERAL PUBLIC LICENSE"*)        MODE=GPL ;;
  *"GNU LESSER GENERAL PUBLIC LICENSE"*) MODE=LGPL ;;
  *) fail "Cannot tell whether ffmpeg was built as LGPL or GPL ($FF_COPYRIGHT)." ;;
esac
if [ -d "$INSTALLED/share/x264" ]; then HAS_X264=1; else HAS_X264=0; fi
if { [ "$MODE" = GPL ] && [ "$HAS_X264" = 0 ]; } || { [ "$MODE" = LGPL ] && [ "$HAS_X264" = 1 ]; }; then
  fail "Inconsistent build: ffmpeg is $MODE but x264 is $([ "$HAS_X264" = 1 ] && echo present || echo absent)."
fi
[ "$MODE" = GPL ] && [ "$ND2" = "ON" ] && fail "This build has both the GPL ffmpeg and the proprietary ND2 reader; such a package cannot be distributed."

SUFFIX=""; [ "$MODE" = GPL ] && SUFFIX="-gpl"
if [ "$OS" = Linux ]; then
  # shellcheck disable=SC1091
  DISTRO="$(. /etc/os-release && echo "${ID}${VERSION_ID}")"
  NAME="VVDViewer${STAMP}-${DISTRO}-vulkan${SUFFIX}"
else
  case "$TRIPLET" in
    arm64-*) NAME="VVDViewer${STAMP}-macosx-vulkan${SUFFIX}" ;;
    *)       NAME="VVDViewer${STAMP}-macosx-intel-vulkan${SUFFIX}" ;;
  esac
fi
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
STAGE="$OUT_DIR/$NAME"
SRC_NAME="$NAME-sources"
SRC_STAGE="$OUT_DIR/$SRC_NAME"
log "Package: $NAME  (ffmpeg: $MODE, triplet: $TRIPLET)"
rm -rf "$STAGE" "$SRC_STAGE"

# Helper files of the Fiji / NBLAST plugins (looked up next to the executable
# on Linux, in Contents/Resources on macOS).
HELPERS="plugin_fiji/fiji/macros/vvd_listener.txt plugin_fiji/fiji/macros/vvd_run.txt
plugin_fiji/fiji/macros/vvd_quit.txt plugin_fiji/fiji/plugins/vvd_listener.jar
plugin_fiji/fiji/plugins/Apply_Bigwarp_Filter.jar plugin_nblast/r/nblast_search.R"
if [ -z "$NBLAST_JAR" ]; then
  for cand in plugin_nblast/fiji/NBLAST_Skeletonize.jar packaging/extra/NBLAST_Skeletonize.jar; do
    [ -f "$REPO/$cand" ] && { NBLAST_JAR="$REPO/$cand"; break; }
  done
fi
copy_resources() {  # $1 = destination directory
  local dest="$1" h
  for d in CL_code Fonts Scripts; do rm -rf "${dest:?}/$d"; cp -R "$REPO/$d" "$dest/$d"; done
  cp "$REPO"/Settings/* "$dest/"
  for h in $HELPERS; do
    [ -f "$REPO/$h" ] || fail "Plugin helper file missing: $REPO/$h"
    cp "$REPO/$h" "$dest/"
  done
  if [ -n "$NBLAST_JAR" ]; then
    [ -f "$NBLAST_JAR" ] || fail "--nblast-jar not found: $NBLAST_JAR"
    cp "$NBLAST_JAR" "$dest/NBLAST_Skeletonize.jar"
    log "Included NBLAST_Skeletonize.jar"
  else
    log "NBLAST_Skeletonize.jar not available - skipped (pass --nblast-jar to include it)."
  fi
}

# Licenses + third-party notices (+ sources) via the shared CMake script.
collect_licenses() {  # $1 = directory that receives THIRD_PARTY_NOTICES.txt and licenses/
  local args=(
    "-DINSTALLED_DIR=$INSTALLED" "-DVCPKG_ROOT=$VCPKG_ROOT_DIR" "-DSTAGING=$1"
    "-DFFMPEG_MODE=$MODE" "-DPACKAGE_NAME=$NAME" "-DVULKAN_SDK=$VK_SDK")
  [ -n "$EXTRA_NOTICES" ] && args+=("-DEXTRA_NOTICES=$EXTRA_NOTICES")
  if [ "$NO_SOURCES" = 0 ]; then
    args+=("-DSOURCES_DIR=$SRC_STAGE" "-DSOURCES_ARCHIVE=$SRC_NAME.tar.xz")
  fi
  cmake "${args[@]}" -P "$REPO/packaging/collect_licenses.cmake" || fail "Collecting the third-party licenses failed."
}

write_buildinfo() {  # $1 = file
  local commit dirty vcpkg_commit cxx
  # The Xcode generator records the compiler in CMakeFiles/<ver>/CMakeCXXCompiler.cmake only.
  cxx="$(cache_get CMAKE_CXX_COMPILER)"
  [ -n "$cxx" ] || cxx="$(sed -n 's/^set(CMAKE_CXX_COMPILER "\(.*\)")$/\1/p' "$BUILD_DIR"/CMakeFiles/*/CMakeCXXCompiler.cmake 2>/dev/null | head -1)"
  commit="$(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo unknown)"
  dirty=""; [ -n "$(git -C "$REPO" status --porcelain --untracked-files=no 2>/dev/null)" ] && dirty=" (with uncommitted changes)"
  vcpkg_commit="$(git -C "$VCPKG_ROOT_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"
  {
    echo "VVDViewer $OS package"
    echo "Package:          $NAME"
    echo "Built:            $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "Source commit:    $commit$dirty  (https://github.com/JaneliaSciComp/VVDViewer)"
    if [ "$MODE" = GPL ]; then echo "ffmpeg license:   GPL (x264/x265; movie export H.264)"
    else echo "ffmpeg license:   LGPL (movie export MPEG-4 Part 2)"; fi
    echo "ND2 reader:       $([ "$ND2" = ON ] && echo enabled || echo disabled)"
    echo "vcpkg commit:     $vcpkg_commit"
    echo "vcpkg triplet:    $TRIPLET"
    echo "Compiler:         $cxx $("$cxx" --version 2>/dev/null | head -1)"
    echo "Vulkan SDK:       $VK_SDK"
    if [ "$OS" = Linux ]; then
      echo "Build system:     $(. /etc/os-release && echo "$PRETTY_NAME"), $(ldd --version | head -1)"
    else
      echo "Build system:     macOS $(sw_vers -productVersion), $(xcodebuild -version 2>/dev/null | head -1)"
    fi
  } > "$1"
}

# Minimum macOS version a Mach-O file was linked for (LC_BUILD_VERSION minos,
# or LC_VERSION_MIN_MACOSX version in older binaries) for one architecture.
macho_minos() {  # $1 = file, $2 = arch
  otool -arch "$2" -l "$1" 2>/dev/null | awk '
    /cmd LC_BUILD_VERSION/      { b = 1; next }
    /cmd LC_VERSION_MIN_MACOSX/ { v = 1; next }
    b && $1 == "minos"   { print $2; exit }
    v && $1 == "version" { print $2; exit }'
}

# True when dotted version $1 is newer than $2 (e.g. 15.0 > 14.0, 14.4 > 14).
ver_gt() {
  awk -v a="$1" -v b="$2" 'BEGIN {
    n = split(a, x, "."); m = split(b, y, "."); k = (n > m ? n : m)
    for (i = 1; i <= k; i++) { xi = (i <= n ? x[i] + 0 : 0); yi = (i <= m ? y[i] + 0 : 0)
      if (xi > yi) exit 0; if (xi < yi) exit 1 }
    exit 1 }'
}

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

OUTPUTS=()
EXTRA_NOTICES=""

# Copies the system's gdk-pixbuf SVG loader module (Debian/Ubuntu package
# librsvg2-common) into data/lib, and records its license and - for the
# sources archive - the matching Ubuntu/Debian source package.
bundle_svg_loader() {
  local D="$STAGE/data" mod="" p pkg srcpkg srcver upver f
  for p in /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.so \
           /usr/lib64/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.so; do
    [ -f "$p" ] && { mod="$p"; break; }
  done
  if [ -z "$mod" ]; then
    warn "No gdk-pixbuf SVG loader on this system (install librsvg2-common); GTK check/radio marks may be missing."
    return 0
  fi
  cp "$mod" "$D/lib/libpixbufloader-svg.so"
  chmod 755 "$D/lib/libpixbufloader-svg.so"
  command -v dpkg-query >/dev/null 2>&1 || { warn "Bundled $mod, but cannot determine its source package (no dpkg)."; return 0; }
  pkg="$(dpkg -S "$mod" | head -1 | cut -d: -f1)"
  srcpkg="$(dpkg-query -W -f='${source:Package}' "$pkg")"
  srcver="$(dpkg-query -W -f='${source:Version}' "$pkg")"
  if [ -f "/usr/share/doc/$pkg/copyright" ]; then
    EXTRA_NOTICES="gdk-pixbuf SVG loader (data/lib/libpixbufloader-svg.so from the $(. /etc/os-release && echo "$NAME") package $pkg $srcver) [source provided: $srcpkg-svg-loader/]|LGPL-2.1-or-later|/usr/share/doc/$pkg/copyright|$srcpkg-svg-loader.txt"
  fi
  if [ "$NO_SOURCES" = 0 ]; then
    # Source package files on Launchpad / Debian, e.g. librsvg_2.58.0+dfsg-1build1.debian.tar.xz
    upver="${srcver#*:}"; upver="${upver%-*}"
    mkdir -p "$SRC_STAGE/$srcpkg-svg-loader"
    printf 'Source of data/lib/libpixbufloader-svg.so: the %s source package %s %s\n(.dsc, upstream .orig and distribution .debian archives), as published by the distribution.\n' \
      "$(. /etc/os-release && echo "$NAME")" "$srcpkg" "$srcver" > "$SRC_STAGE/$srcpkg-svg-loader/README.txt"
    for f in "${srcpkg}_${srcver#*:}.dsc" "${srcpkg}_${upver}.orig.tar.xz" "${srcpkg}_${srcver#*:}.debian.tar.xz"; do
      curl -fsSL --retry 3 -o "$SRC_STAGE/$srcpkg-svg-loader/$f" "https://launchpad.net/ubuntu/+archive/primary/+files/$f" ||
      curl -fsSL --retry 3 -o "$SRC_STAGE/$srcpkg-svg-loader/$f" "https://deb.debian.org/debian/pool/main/${srcpkg:0:1}/$srcpkg/$f" ||
      { rm -f "$SRC_STAGE/$srcpkg-svg-loader/$f"; warn "Could not download source file $f of $srcpkg $srcver"; }
    done
  fi
}

# =============================================================================
package_linux() {
  local BIN="$BUILD_DIR/bin"
  [ -x "$BIN/VVDViewer" ] || fail "$BIN/VVDViewer not found. Build the Release configuration first."
  local D="$STAGE/data"
  mkdir -p "$D/lib" "$D/plugins/gui" "$D/share/glib-2.0/schemas"

  # Our own binaries. Take the libraries from the build directory itself (the
  # copies in bin/lib are made by a POST_BUILD step).
  cp "$BIN/VVDViewer" "$D/VVDViewer"
  local f
  for f in libVVDMainFrame.so libwxGuiPluginBase.so; do
    [ -f "$BUILD_DIR/$f" ] || fail "$BUILD_DIR/$f not found"
    cp "$BUILD_DIR/$f" "$D/lib/"
  done
  for f in libFijiInterfacePlugin.so libNBLASTGuiPlugin.so libNeuronAnnotatorPlugin.so; do
    [ -s "$BUILD_DIR/$f" ] || fail "$BUILD_DIR/$f not found (or empty)"
    cp "$BUILD_DIR/$f" "$D/plugins/gui/"
  done

  # Bundle every shared library that resolves into the build tree, the vcpkg
  # tree or the Vulkan SDK. ldd lists the transitive closure; the build's
  # RUNPATH still points at those trees at this point.
  local prefixes=("$(cd "$BUILD_DIR" && pwd -P)/" "$(cd "$INSTALLED" && pwd -P)/")
  [ -n "$VK_SDK" ] && prefixes+=("$(cd "$VK_SDK" && pwd -P)/")
  local elf line soname path real p bundle
  local missing=""
  for elf in "$BIN/VVDViewer" "$BUILD_DIR"/libVVDMainFrame.so "$BUILD_DIR"/libwxGuiPluginBase.so \
             "$BUILD_DIR"/libFijiInterfacePlugin.so "$BUILD_DIR"/libNBLASTGuiPlugin.so "$BUILD_DIR"/libNeuronAnnotatorPlugin.so; do
    while IFS= read -r line; do
      case "$line" in
        *"=> not found"*) missing="$missing ${line%% *}" ;;
        *" => /"*)
          soname="$(echo "$line" | awk '{print $1}')"
          path="$(echo "$line" | awk '{print $3}')"
          real="$(readlink -f "$path")"
          bundle=0
          for p in "${prefixes[@]}"; do case "$real" in "$p"*) bundle=1 ;; esac; done
          case "$soname" in libVVDMainFrame.so|libwxGuiPluginBase.so|lib*Plugin.so) bundle=0 ;; esac
          if [ "$bundle" = 1 ] && [ ! -f "$D/lib/$soname" ]; then cp "$real" "$D/lib/$soname"; fi
          ;;
      esac
    done < <(ldd "$elf")
  done
  [ -z "$missing" ] || fail "Unresolved shared libraries:$missing"
  chmod 755 "$D/lib/"*.so* "$D/plugins/gui/"*.so "$D/VVDViewer"
  log "Bundled libraries: $(cd "$D/lib" && ls | tr '\n' ' ')"

  # Run-time search paths: no absolute build paths may remain.
  local pe=""
  command -v patchelf >/dev/null 2>&1 && pe=patchelf
  set_rpath() {  # $1 = rpath, rest = files
    local rp="$1"; shift
    if [ -n "$pe" ]; then
      for f in "$@"; do patchelf --set-rpath "$rp" "$f"; done
    else
      local list; list="$(printf '%s;' "$@")"
      cmake "-DFILES=${list%;}" "-DRPATH=$rp" -P "$REPO/packaging/set_rpath.cmake" || fail "Setting RUNPATH failed (install patchelf to add missing entries)."
    fi
  }
  set_rpath '$ORIGIN/lib' "$D/VVDViewer"
  local libs_with_rpath=()
  for f in "$D/lib/"*.so*; do
    if readelf -d "$f" | grep -qE '\((RUNPATH|RPATH)\)'; then libs_with_rpath+=("$f"); fi
  done
  [ ${#libs_with_rpath[@]} -gt 0 ] && set_rpath '$ORIGIN' "${libs_with_rpath[@]}"
  set_rpath '$ORIGIN/../../lib' "$D/plugins/gui/"*.so

  if [ "$NO_STRIP" = 0 ] && command -v strip >/dev/null 2>&1; then
    strip --strip-unneeded "$D/VVDViewer" "$D/lib/"*.so* "$D/plugins/gui/"*.so
  fi

  # GLib/GTK data: compiled GSettings schemas (the GTK file chooser aborts
  # without org.gtk.Settings.FileChooser) and the gdk-pixbuf loader cache that
  # VVDViewer points GDK_PIXBUF_MODULE_FILE at (exe dir).
  if [ -d "$INSTALLED/share/glib-2.0/schemas" ]; then
    cp "$INSTALLED/share/glib-2.0/schemas/"*.xml "$D/share/glib-2.0/schemas/" 2>/dev/null || true
  fi
  local gcs=""
  for p in "$INSTALLED/tools/glib/glib-compile-schemas" "$(command -v glib-compile-schemas || true)"; do
    [ -n "$p" ] && [ -x "$p" ] && { gcs="$p"; break; }
  done
  [ -n "$gcs" ] || fail "glib-compile-schemas not found (vcpkg tools/glib or the libglib2.0-bin package)."
  "$gcs" "$D/share/glib-2.0/schemas" || fail "glib-compile-schemas failed"
  local loaders_dir="$INSTALLED/lib/gdk-pixbuf-2.0/2.10.0/loaders"
  if [ -d "$loaders_dir" ] && ls "$loaders_dir"/*.so >/dev/null 2>&1; then
    fail "gdk-pixbuf was built with loadable modules ($loaders_dir); bundling them is not implemented."
  fi
  # The bundled gdk-pixbuf has its raster loaders built in, but no SVG loader,
  # which GTK's Adwaita theme needs for its check/radio marks. VVDViewer
  # rewrites data/loaders.cache at start-up to load data/lib/libpixbufloader-
  # svg.so, so ship the build system's loader module there (librsvg2-common;
  # it uses the system librsvg at run time, present on GNOME/GTK desktops).
  # The relative path below is for read-only installs: this gdk-pixbuf is
  # relocatable and resolves it against the package root.
  bundle_svg_loader
  {
    printf '# GdkPixbuf Image Loader Modules file\n'
    printf '# Built-in loaders of the bundled gdk-pixbuf plus the SVG loader in data/lib.\n'
    printf '# (VVDViewer regenerates this file at start-up when the folder is writable.)\n#\n'
    if [ -f "$D/lib/libpixbufloader-svg.so" ]; then
      printf '"data/lib/libpixbufloader-svg.so"\n"svg" 6 "gdk-pixbuf" "Scalable Vector Graphics" "LGPL"\n'
      printf '"image/svg+xml" "image/svg" "image/svg-xml" "image/vnd.adobe.svg+xml" "text/xml-svg" "image/svg+xml-compressed" ""\n'
      printf '"svg" "svgz" "svg.gz" ""\n" <svg" "*    " 100\n" <!DOCTYPE svg" "*             " 100\n\n'
    fi
  } > "$D/loaders.cache"

  copy_resources "$D"
  install -m 755 "$REPO/packaging/linux/VVDViewer.sh" "$STAGE/VVDViewer"

  cp "$REPO/LICENSE" "$STAGE/LICENSE.txt"
  cp "$REPO/LICENSE_DEPENDENCIES.md" "$STAGE/"
  collect_licenses "$STAGE"
  write_buildinfo "$STAGE/BUILDINFO.txt"

  # Verify: every ELF resolves inside the package or from the system, with no
  # reference to the build machine's trees.
  local bad=""
  for elf in "$D/VVDViewer" "$D/lib/"*.so* "$D/plugins/gui/"*.so; do
    if readelf -d "$elf" | grep -E '\((RUNPATH|RPATH)\)' | grep -q -E '\[[^$]|:/'; then
      bad="$bad\n  $(basename "$elf"): $(readelf -d "$elf" | grep -E '\((RUNPATH|RPATH)\)')"
    fi
    while IFS= read -r line; do
      case "$line" in
        *"not found"*) bad="$bad\n  $(basename "$elf"): $line" ;;
        *" => /"*)
          soname="$(echo "$line" | awk '{print $1}')"
          path="$(echo "$line" | awk '{print $3}')"
          real="$(readlink -f "$path")"
          for p in "${prefixes[@]}"; do case "$real" in "$p"*) bad="$bad\n  $(basename "$elf") loads $path from the build machine" ;; esac; done
          # A bundled library must also be the one that gets loaded (not a
          # same-named system copy, e.g. because a RUNPATH is missing). The
          # gdk-pixbuf loader module is exempt: it is dlopen()ed into a process
          # that has already loaded the bundled libraries it needs.
          if [ -f "$D/lib/$soname" ] && [ "$(basename "$elf")" != libpixbufloader-svg.so ]; then
            case "$real" in "$STAGE"/*) ;; *) bad="$bad\n  $(basename "$elf") resolves $soname to $path instead of the bundled copy" ;; esac
          fi
          ;;
      esac
    done < <(env -u LD_LIBRARY_PATH ldd "$elf")
  done
  [ -z "$bad" ] || fail "Package verification failed:$(printf '%b' "$bad")"
  log "Verified: no unresolved libraries and no references to the build trees."

  local tarball="$OUT_DIR/$NAME.tar.xz"
  rm -f "$tarball"
  tar -C "$OUT_DIR" -cf - "$NAME" | xz -T0 -9 > "$tarball"
  OUTPUTS+=("$tarball")
}

# =============================================================================
package_macos() {
  local APP_SRC="$BUILD_DIR/bin/Release/VVDViewer.app"
  [ -d "$APP_SRC" ] || fail "$APP_SRC not found. Build the Release configuration first."
  mkdir -p "$STAGE"
  local APP="$STAGE/VVDViewer.app"
  ditto "$APP_SRC" "$APP"
  local C="$APP/Contents"

  copy_resources "$C/Resources"
  cp "$REPO/plugin_NA/vvd/term.sh" "$C/Resources/"
  cp "$REPO/LICENSE" "$C/Resources/LICENSE.txt"
  cp "$REPO/LICENSE_DEPENDENCIES.md" "$C/Resources/"
  collect_licenses "$C/Resources"
  write_buildinfo "$C/Resources/BUILDINFO.txt"
  cp "$C/Resources/THIRD_PARTY_NOTICES.txt" "$C/Resources/LICENSE.txt" "$C/Resources/BUILDINFO.txt" "$STAGE/"

  # Bundle identity and version (the CMake-generated Info.plist has neither;
  # notarization and Gatekeeper key off CFBundleIdentifier).
  local plist="$C/Info.plist" pb=/usr/libexec/PlistBuddy version
  version="$(sed -n 's/.*FLUORENDER_TITLE) *+ *"\([0-9.]*\)".*/\1/p' "$REPO/fluorender/FluoRender/Main.cpp" | head -1)"
  [ -n "$version" ] || version="0.0"
  "$pb" -c "Delete :CFBundleIdentifier" "$plist" 2>/dev/null || true
  "$pb" -c "Add :CFBundleIdentifier string org.janelia.VVDViewer" "$plist"
  "$pb" -c "Delete :CFBundleShortVersionString" "$plist" 2>/dev/null || true
  "$pb" -c "Add :CFBundleShortVersionString string $version" "$plist"
  "$pb" -c "Delete :CFBundleVersion" "$plist" 2>/dev/null || true
  "$pb" -c "Add :CFBundleVersion string $STAMP" "$plist"

  # Verify: all Mach-O files are for the right architecture, run on the minimum
  # macOS version, and load only system libraries or libraries in the bundle.
  local want_arch bad="" m dep mv min_os id
  case "$TRIPLET" in arm64-*) want_arch=arm64 ;; *) want_arch=x86_64 ;; esac
  # Minimum macOS: the build's deployment target (CMakeLists.txt sets it), else
  # whatever the executable was linked for.
  min_os="$(cache_get CMAKE_OSX_DEPLOYMENT_TARGET)"
  [ -n "$min_os" ] || min_os="$(macho_minos "$C/MacOS/VVDViewer" "$want_arch")"
  [ -n "$min_os" ] || fail "Cannot determine the minimum macOS version of $C/MacOS/VVDViewer"
  local machos=("$C/MacOS/VVDViewer")
  for m in "$C/Frameworks/"*.dylib "$C/PlugIns/gui/"*.dylib; do [ -f "$m" ] && machos+=("$m"); done
  for m in "${machos[@]}"; do
    case " $(lipo -archs "$m" 2>/dev/null) " in *" $want_arch "*) ;; *) bad="$bad\n  $(basename "$m"): not $want_arch" ;; esac
    mv="$(macho_minos "$m" "$want_arch")"
    if [ -z "$mv" ]; then
      bad="$bad\n  $(basename "$m"): no minimum macOS version recorded"
    elif ver_gt "$mv" "$min_os"; then
      bad="$bad\n  $(basename "$m") requires macOS $mv, newer than the app's minimum $min_os"
    fi
    # Dependencies of the target architecture only (universal dylibs such as
    # MoltenVK list one block per architecture), minus the dylib's own install
    # name, which otool -L prints first and which is not a dependency.
    id="$(otool -arch "$want_arch" -D "$m" 2>/dev/null | sed -n 2p)"
    while IFS= read -r dep; do
      [ -n "$id" ] && [ "$dep" = "$id" ] && continue
      case "$dep" in
        /usr/lib/*|/System/*|@executable_path/*|@loader_path/*) ;;
        @rpath/*) bad="$bad\n  $(basename "$m") uses $dep (unresolved @rpath)" ;;
        *) bad="$bad\n  $(basename "$m") loads $dep from outside the bundle" ;;
      esac
    done < <(otool -arch "$want_arch" -L "$m" | awk 'NR>1 {print $1}')
  done
  [ -z "$bad" ] || fail "Bundle verification failed:$(printf '%b' "$bad")"
  log "Verified: all ${#machos[@]} Mach-O files are $want_arch, run on macOS $min_os or newer, and are self-contained."
  "$pb" -c "Delete :LSMinimumSystemVersion" "$plist" 2>/dev/null || true
  "$pb" -c "Add :LSMinimumSystemVersion string $min_os" "$plist"

  # Code signing, inside-out (dylibs first, then the bundle).
  local kc_args=()
  [ -n "$KEYCHAIN" ] && kc_args=(--keychain "$KEYCHAIN")
  local developer_id=0
  if [ -n "$SIGN_ID" ] && [ "$SIGN_ID" != "-" ]; then
    developer_id=1
    local sign_args=(--force --timestamp --options runtime --sign "$SIGN_ID" ${kc_args[@]+"${kc_args[@]}"})
    log "Signing with: $SIGN_ID"
  else
    warn "No --sign-identity given: ad-hoc signature only (Gatekeeper will block the app on other Macs)."
    local sign_args=(--force --sign -)
  fi
  for m in "$C/Frameworks/"*.dylib "$C/PlugIns/gui/"*.dylib; do
    [ -f "$m" ] && codesign "${sign_args[@]}" "$m"
  done
  if [ "$developer_id" = 1 ]; then
    codesign "${sign_args[@]}" --entitlements "$ENTITLEMENTS" "$APP"
  else
    codesign "${sign_args[@]}" "$APP"
  fi
  codesign --verify --deep --strict --verbose=2 "$APP" || fail "codesign verification failed"

  ln -s /Applications "$STAGE/Applications"
  local dmg="$OUT_DIR/$NAME.dmg"
  rm -f "$dmg"
  hdiutil create -volname "VVDViewer" -srcfolder "$STAGE" -ov -format UDZO "$dmg" >/dev/null || fail "hdiutil create failed"
  if [ "$developer_id" = 1 ]; then
    codesign --force --timestamp --sign "$SIGN_ID" ${kc_args[@]+"${kc_args[@]}"} "$dmg"
    local notary=()
    if [ -n "$NOTARY_PROFILE" ]; then
      notary=(--keychain-profile "$NOTARY_PROFILE")
      [ -n "$KEYCHAIN" ] && notary+=(--keychain "$KEYCHAIN")
    elif [ -n "$NOTARY_KEY" ]; then
      [ -n "$NOTARY_KEY_ID" ] && [ -n "$NOTARY_ISSUER" ] || fail "--notary-key needs --notary-key-id and --notary-issuer"
      notary=(--key "$NOTARY_KEY" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER")
    fi
    if [ ${#notary[@]} -gt 0 ]; then
      log "Notarizing (this can take several minutes)..."
      local result
      result="$(xcrun notarytool submit "$dmg" "${notary[@]}" --wait --output-format json)" || fail "notarytool submit failed: $result"
      echo "$result"
      case "$result" in *'"status":"Accepted"'*|*'"status" : "Accepted"'*) ;; *) fail "Notarization was not accepted (see: xcrun notarytool log <id> ...)" ;; esac
      xcrun stapler staple "$dmg" || fail "stapler staple failed"
      xcrun stapler validate "$dmg"
      spctl -a -t open --context context:primary-signature -vv "$dmg" || fail "Gatekeeper rejects $dmg"
    else
      warn "Signed with a Developer ID but not notarized (no --notary-profile / --notary-key)."
    fi
  fi
  OUTPUTS+=("$dmg")
}

if [ "$OS" = Linux ]; then package_linux; else package_macos; fi

# Sources archive and checksums ------------------------------------------------
if [ "$NO_SOURCES" = 0 ] && [ -d "$SRC_STAGE" ] && [ -n "$(ls -A "$SRC_STAGE")" ]; then
  src_tar="$OUT_DIR/$SRC_NAME.tar.xz"
  rm -f "$src_tar"
  tar -C "$OUT_DIR" -cf - "$SRC_NAME" | xz -T0 -1 > "$src_tar"
  OUTPUTS+=("$src_tar")
fi
for o in "${OUTPUTS[@]}"; do
  printf '%s  %s\n' "$(sha256_file "$o")" "$(basename "$o")" > "$o.sha256"
  if [ -n "$GPG_KEY" ]; then
    gpg --batch --yes --local-user "$GPG_KEY" --armor --detach-sign --output "$o.asc" "$o" || fail "gpg signing failed"
  fi
done

echo
log "=== Packaging complete ==="
for o in "${OUTPUTS[@]}"; do
  printf '%s  (%s)\n' "$o" "$(du -h "$o" | cut -f1)"
done
echo "Staging folder: $STAGE"
