#!/usr/bin/env bash
#
# One-command auto-build for VVDViewer on macOS / Linux.
#
# Downloads and builds every third-party dependency (EXCEPT the Vulkan SDK)
# from source via vcpkg, then configures and builds VVDViewer with CMake.
# All dependencies are linked statically except wxWidgets (dynamic). On Linux,
# the system display libraries (GTK3, X11, Wayland, libva) and the Vulkan loader
# stay as dynamic system libraries and are NOT managed by vcpkg.
#
# The Vulkan SDK is NOT managed here: install it separately
# (https://vulkan.lunarg.com/) so that VULKAN_SDK is set.
#
# Usage: bash build.sh [--debug] [--clean] [--enable-nd2] [--vcpkg <dir>]

set -euo pipefail

CONFIG="Release"
CLEAN=0
ENABLE_ND2=0
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO/build"
VCPKG_ROOT="${VCPKG_ROOT:-$REPO/vcpkg}"

usage() { grep '^#' "$0" | sed 's/^# \{0,1\}//'; }
fail()  { echo "ERROR: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --debug)      CONFIG=Debug ;;
    --clean)      CLEAN=1 ;;
    --enable-nd2) ENABLE_ND2=1 ;;
    --vcpkg)      shift; VCPKG_ROOT="$1" ;;
    -h|--help)    usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
  shift
done

echo "=== VVDViewer auto-build ($(uname -s)) ==="

# 1) Prerequisites ----------------------------------------------------------
[ -n "${VULKAN_SDK:-}" ] || fail "VULKAN_SDK is not set. Install the Vulkan SDK (https://vulkan.lunarg.com/) and source its setup-env script."
[ -d "$VULKAN_SDK" ]     || fail "VULKAN_SDK points to a missing path: $VULKAN_SDK"
command -v cmake >/dev/null 2>&1 || fail "cmake not found on PATH."
command -v git   >/dev/null 2>&1 || fail "git not found on PATH."
echo "Vulkan SDK: $VULKAN_SDK"

# 2) Platform-specific generator / triplet ----------------------------------
OS="$(uname -s)"
ARCH="$(uname -m)"
GEN_ARGS=()
case "$OS" in
  Darwin)
    GEN_ARGS=(-G Xcode)
    if [ "$ARCH" = "arm64" ]; then TRIPLET="arm64-osx-vvd"; else TRIPLET="x64-osx-vvd"; fi
    ;;
  Linux)
    TRIPLET="x64-linux-vvd"
    # System display libraries are NOT provided by vcpkg; verify they exist.
    MISSING=""
    if command -v pkg-config >/dev/null 2>&1; then
      for pc in gtk+-3.0 x11 xcb wayland-client; do
        pkg-config --exists "$pc" 2>/dev/null || MISSING="$MISSING $pc"
      done
    else
      MISSING="pkg-config$MISSING"
    fi
    if [ -n "$MISSING" ]; then
      echo "WARNING: missing system dev packages:$MISSING"
      echo "  Debian/Ubuntu: sudo apt install build-essential pkg-config libgtk-3-dev libx11-dev libxcb1-dev libwayland-dev wayland-protocols libva-dev"
      echo "  Fedora:        sudo dnf install gtk3-devel libX11-devel libxcb-devel wayland-devel wayland-protocols-devel libva-devel"
    fi
    ;;
  *) fail "Unsupported OS: $OS" ;;
esac
echo "Triplet: $TRIPLET"

# 3) Bootstrap vcpkg --------------------------------------------------------
if [ ! -d "$VCPKG_ROOT/.git" ]; then
  echo "Cloning vcpkg into $VCPKG_ROOT ..."
  git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
fi
if [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
  echo "Bootstrapping vcpkg ..."
  ( cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics )
fi
export VCPKG_ROOT
echo "vcpkg baseline commit: $(git -C "$VCPKG_ROOT" rev-parse --short HEAD)"

# 4) Configure (this triggers the vcpkg manifest install) -------------------
[ "$CLEAN" = "1" ] && { echo "Cleaning $BUILD_DIR"; rm -rf "$BUILD_DIR"; }

CMAKE_ARGS=(
  -S "$REPO" -B "$BUILD_DIR"
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  -DVCPKG_TARGET_TRIPLET="$TRIPLET"
  -DVCPKG_OVERLAY_TRIPLETS="$REPO/triplets"
  -DCMAKE_BUILD_TYPE="$CONFIG"
)
[ ${#GEN_ARGS[@]} -gt 0 ] && CMAKE_ARGS+=("${GEN_ARGS[@]}")
[ "$ENABLE_ND2" = "1" ]   && CMAKE_ARGS+=(-DENABLE_ND2=ON)

echo "Configuring... first run downloads & builds ALL dependencies from source (can take 30-90 min)."
cmake "${CMAKE_ARGS[@]}"

# 5) Build ------------------------------------------------------------------
echo "Building VVDViewer ($CONFIG)..."
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel

# 6) Report -----------------------------------------------------------------
echo ""
echo "=== Build complete ==="
nd2_state="disabled (default)"; [ "$ENABLE_ND2" = "1" ] && nd2_state="ENABLED"
echo "ND2 (Nikon) reader: $nd2_state.  libCZI: enabled (via vcpkg).  Vulkan SDK: external ($VULKAN_SDK)."
