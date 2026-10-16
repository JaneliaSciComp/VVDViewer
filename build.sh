#!/usr/bin/env bash
#
# One-command auto-build for VVDViewer on macOS / Linux.
#
# Downloads and builds every third-party dependency (EXCEPT the Vulkan SDK)
# from source via vcpkg, then configures and builds VVDViewer with CMake.
# All dependencies are linked statically except wxWidgets, ffmpeg and libCZI
# (dynamic; on Linux also the GTK3/GLib stack). On Linux, the system display
# libraries (X11, Wayland, libva) and the Vulkan loader stay as dynamic system
# libraries and are NOT managed by vcpkg.
#
# By default ffmpeg is built WITHOUT the GPL x264/x265 encoders, so the
# binaries can be redistributed under the LGPL (movie export: MPEG-4 Part 2).
# --enable-gpl adds them (movie export: H.264); such binaries are GPL-covered.
#
# The Vulkan SDK is NOT managed here: install it separately
# (https://vulkan.lunarg.com/) so that VULKAN_SDK is set.
#
# Usage: bash build.sh [--debug] [--clean] [--enable-nd2] [--enable-gpl] [--enable-qt]
#                      [--vcpkg <dir>] [--build-dir <dir>] [--triplet <name>]
#                      [--cmake-arg <arg>]...
#
#   --enable-gpl       ffmpeg with x264/x265 (vcpkg manifest feature "gpl").
#                      Cannot be combined with --enable-nd2.
#   --enable-qt        also build the Qt front end (VVDViewerQt). Needs an
#                      installed Qt 6: "brew install qt" on macOS, qt6-base-dev
#                      on Debian/Ubuntu, or pass -DCMAKE_PREFIX_PATH=<Qt dir>
#                      with --cmake-arg.
#   --triplet NAME     vcpkg triplet (default: auto). The *-vvd-release
#                      triplets build release-only dependencies (used by CI).
#   --cmake-arg ARG    extra CMake configure argument (repeatable), e.g.
#                      --cmake-arg -DVCPKG_INSTALL_OPTIONS=--clean-buildtrees-after-build

set -euo pipefail

CONFIG="Release"
CLEAN=0
ENABLE_ND2=0
ENABLE_QT=0
ENABLE_GPL=0
TRIPLET=""
EXTRA_CMAKE_ARGS=()
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO/build"
VCPKG_ROOT="${VCPKG_ROOT:-$REPO/vcpkg}"

usage() { sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; }
fail()  { echo "ERROR: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --debug)      CONFIG=Debug ;;
    --clean)      CLEAN=1 ;;
    --enable-nd2) ENABLE_ND2=1 ;;
    --enable-qt)  ENABLE_QT=1 ;;
    --enable-gpl) ENABLE_GPL=1 ;;
    --vcpkg)      shift; VCPKG_ROOT="${1:?--vcpkg needs a directory}" ;;
    --build-dir)  shift; BUILD_DIR="${1:?--build-dir needs a directory}" ;;
    --triplet)    shift; TRIPLET="${1:?--triplet needs a name}" ;;
    --cmake-arg)  shift; EXTRA_CMAKE_ARGS+=("${1:?--cmake-arg needs a value}") ;;
    -h|--help)    usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
  shift
done

echo "=== VVDViewer auto-build ($(uname -s)) ==="

[ "$ENABLE_ND2" = "1" ] && [ "$ENABLE_GPL" = "1" ] && \
  fail "--enable-nd2 and --enable-gpl cannot be combined: the proprietary Nikon ND2 SDK cannot be distributed together with GPL code."
case "$TRIPLET" in
  *-release) [ "$CONFIG" = "Debug" ] && fail "Triplet $TRIPLET builds release-only dependencies; it cannot be used for a Debug build." ;;
esac

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
    # Full Xcode gives the Xcode generator; with only the Command Line Tools
    # installed (xcode-select -p -> /Library/Developer/CommandLineTools) fall
    # back to Ninja (or Unix Makefiles) so no App Store download is needed.
    if xcodebuild -version >/dev/null 2>&1; then
      GEN_ARGS=(-G Xcode)
    elif command -v ninja >/dev/null 2>&1; then
      GEN_ARGS=(-G Ninja)
      echo "Note: full Xcode not found; using Ninja generator with Command Line Tools."
    else
      GEN_ARGS=(-G "Unix Makefiles")
      echo "Note: full Xcode not found; using Unix Makefiles generator with Command Line Tools (brew install ninja for faster builds)."
    fi
    if [ -z "$TRIPLET" ]; then
      if [ "$ARCH" = "arm64" ]; then TRIPLET="arm64-osx-vvd"; else TRIPLET="x64-osx-vvd"; fi
    fi
    ;;
  Linux)
    [ -n "$TRIPLET" ] || TRIPLET="x64-linux-vvd"
    # WSL: the Windows PATH is auto-imported (appendWindowsPath), and CMake
    # searches PATH-adjacent prefixes for packages. A Windows miniconda/MSYS/SDK
    # on PATH then gets discovered by vcpkg port builds (e.g. wxwidgets picked up
    # a Windows TIFF whose include dir shadowed the system X11 headers, breaking
    # the build). Drop Windows drive mounts from PATH for a clean toolchain env.
    case ":$PATH:" in
      *:/mnt/[a-z]/*)
        _clean=""
        _oifs="$IFS"; IFS=':'
        for _p in $PATH; do
          case "$_p" in
            /mnt/[a-z]/*|/mnt/[a-z]) ;;   # drop Windows drive mounts (/mnt/c/...)
            *) _clean="${_clean:+$_clean:}$_p" ;;
          esac
        done
        IFS="$_oifs"
        export PATH="$_clean"
        echo "Note: removed Windows (/mnt/*) entries from PATH (WSL) so CMake/vcpkg use the Linux toolchain only."
        ;;
    esac
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
      echo "  Debian/Ubuntu: sudo apt install build-essential pkg-config libgtk-3-dev libx11-dev libxcb1-dev libwayland-dev wayland-protocols libva-dev libvdpau-dev"
      echo "  Fedora:        sudo dnf install gtk3-devel libX11-devel libxcb-devel wayland-devel wayland-protocols-devel libva-devel libvdpau-devel"
    fi
    # vcpkg compiles every dependency from source; these tools must be present or
    # the build fails deep inside a port (e.g. nasm for x264/ffmpeg assembly,
    # autotools for several ports). Warn early rather than 30+ min in.
    TOOLS_MISSING=""
    command -v cc  >/dev/null 2>&1 || command -v gcc   >/dev/null 2>&1 || command -v clang   >/dev/null 2>&1 || TOOLS_MISSING="$TOOLS_MISSING c-compiler"
    command -v c++ >/dev/null 2>&1 || command -v g++   >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || TOOLS_MISSING="$TOOLS_MISSING c++-compiler"
    for tool in make nasm autoconf automake libtool; do
      command -v "$tool" >/dev/null 2>&1 || TOOLS_MISSING="$TOOLS_MISSING $tool"
    done
    if [ -n "$TOOLS_MISSING" ]; then
      echo "WARNING: missing build tools needed by vcpkg to compile dependencies:$TOOLS_MISSING"
      echo "  Debian/Ubuntu: sudo apt install build-essential nasm autoconf automake libtool libltdl-dev autoconf-archive"
      echo "  Fedora:        sudo dnf install gcc gcc-c++ make nasm autoconf automake libtool libtool-ltdl-devel autoconf-archive"
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
if [ "$ENABLE_QT" = "1" ]; then
  CMAKE_ARGS+=(-DENABLE_QT_GUI=ON)
  if [ "$OS" = "Darwin" ] && command -v brew >/dev/null 2>&1; then
    QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
    if [ -z "$QT_PREFIX" ] || [ ! -d "$QT_PREFIX" ]; then
      echo "Qt 6 not found in Homebrew; installing (brew install qt) ..."
      brew install qt
      QT_PREFIX="$(brew --prefix qt)"
    fi
    CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$QT_PREFIX")
  fi
fi
# Always pass the feature list explicitly (empty = LGPL) so that switching an
# existing build directory between LGPL and GPL takes effect.
if [ "$ENABLE_GPL" = "1" ]; then CMAKE_ARGS+=(-DVCPKG_MANIFEST_FEATURES=gpl); else CMAKE_ARGS+=(-DVCPKG_MANIFEST_FEATURES=); fi
[ ${#EXTRA_CMAKE_ARGS[@]} -gt 0 ] && CMAKE_ARGS+=("${EXTRA_CMAKE_ARGS[@]}")

echo "Configuring... first run downloads & builds ALL dependencies from source (can take 30-90 min)."
cmake "${CMAKE_ARGS[@]}"

# 5) Build ------------------------------------------------------------------
# Use one job per CPU. A bare --parallel lets make start an unlimited number of
# compilers, which exhausted the memory of a 4-CPU/16 GB CI runner on the large
# VVDMainFrame sources. CMAKE_BUILD_PARALLEL_LEVEL overrides the count.
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
echo "Building VVDViewer ($CONFIG, $JOBS parallel jobs)..."
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel "$JOBS"

# 6) Report -----------------------------------------------------------------
echo ""
echo "=== Build complete ==="
nd2_state="disabled (default)"; [ "$ENABLE_ND2" = "1" ] && nd2_state="ENABLED"
qt_state="disabled (default)"; [ "$ENABLE_QT" = "1" ] && qt_state="ENABLED (VVDViewerQt)"
echo "Qt front end: $qt_state."
echo "ND2 (Nikon) reader: $nd2_state.  libCZI: enabled (via vcpkg).  Vulkan SDK: external ($VULKAN_SDK)."
if [ "$ENABLE_GPL" = "1" ]; then
  echo "ffmpeg: GPL (x264/x265 enabled; movie export H.264)."
else
  echo "ffmpeg: LGPL (default; movie export MPEG-4 Part 2)."
fi
