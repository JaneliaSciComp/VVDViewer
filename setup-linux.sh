#!/usr/bin/env bash
#
# setup-linux.sh — Prepare a Debian/Ubuntu machine to run the VVDViewer
# auto-build (build.sh).
#
# It installs:
#   * the C/C++ toolchain and the tools vcpkg needs to compile every dependency
#     from source (nasm, autotools, bison/flex, pkg-config, cmake, ninja, ...);
#   * the system display dev libraries that are NOT managed by vcpkg
#     (GTK3, X11/XCB, Wayland, VA-API/VDPAU);
#   * the LunarG Vulkan SDK (which build.sh intentionally does not manage), and
#     wires up VULKAN_SDK in ~/.bashrc.
#
# Run as your NORMAL user (it calls sudo itself only for apt, so the Vulkan SDK
# and ~/.bashrc land in YOUR home — do NOT run the whole script with sudo).
#
# Usage: bash setup-linux.sh [--sdk-dir DIR] [--skip-apt] [--skip-vulkan]
#
#   --sdk-dir DIR   Where to install the Vulkan SDK (default: $HOME/vulkan)
#   --skip-apt      Do not install apt packages
#   --skip-vulkan   Do not install the Vulkan SDK
#
# After it finishes: open a new shell (or `source ~/.bashrc`), then `bash build.sh`.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_DIR="${HOME}/vulkan"
DO_APT=1
DO_VULKAN=1
SDK_URL="https://sdk.lunarg.com/sdk/download/latest/linux/vulkan_sdk.tar.xz"

log()  { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWARNING:\033[0m %s\n' "$*" >&2; }
fail() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --sdk-dir)     shift; SDK_DIR="${1:?--sdk-dir needs a path}" ;;
    --skip-apt)    DO_APT=0 ;;
    --skip-vulkan) DO_VULKAN=0 ;;
    -h|--help)     grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) fail "Unknown argument: $1" ;;
  esac
  shift
done

# Decide how to elevate. Refuse the footgun of `sudo bash setup-linux.sh`, which
# would put the SDK and .bashrc changes in root's home instead of the user's.
if [ "$(id -u)" -eq 0 ]; then
  if [ -n "${SUDO_USER:-}" ]; then
    fail "Run as your normal user, NOT via sudo. The script sudo's for apt by itself."
  fi
  SUDO=""           # genuine root (e.g. a container) — run apt directly
else
  command -v sudo >/dev/null 2>&1 || fail "sudo not found, and you are not root. Install sudo or run as root."
  SUDO="sudo"
fi

echo "=== VVDViewer Linux environment setup ==="
echo "Repo:        $REPO"
echo "Vulkan SDK:  $SDK_DIR"
echo

# 1) System packages -----------------------------------------------------------
if [ "$DO_APT" = "1" ]; then
  command -v apt-get >/dev/null 2>&1 || fail \
"This script targets Debian/Ubuntu (apt). On Fedora install the equivalents:
  sudo dnf install gcc gcc-c++ make cmake ninja-build git curl zip unzip tar xz \\
    pkgconf-pkg-config nasm yasm autoconf automake libtool bison flex gperf perl \\
    python3-pip \\
    gtk3-devel mesa-libGL-devel mesa-libGLU-devel libX11-devel libxcb-devel \\
    libXext-devel libXrender-devel libXrandr-devel libXi-devel libXfixes-devel \\
    libXcursor-devel libXcomposite-devel libXdamage-devel libXinerama-devel \\
    libxkbcommon-devel wayland-devel wayland-protocols-devel libva-devel libvdpau-devel
then install the Vulkan SDK from https://vulkan.lunarg.com/ and re-run with --skip-apt."

  log "apt-get update (sudo)..."
  $SUDO apt-get update -y

  log "Installing toolchain + vcpkg build deps + display libraries (sudo)..."
  $SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake ninja-build git curl ca-certificates \
    zip unzip tar xz-utils pkg-config \
    nasm yasm autoconf automake libtool libtool-bin autoconf-archive \
    bison flex gperf python3 python3-venv python3-pip perl \
    libgtk-3-dev \
    libgl1-mesa-dev libglu1-mesa-dev \
    libx11-dev libx11-xcb-dev libxcb1-dev libxext-dev libxrender-dev libxrandr-dev \
    libxi-dev libxfixes-dev libxcursor-dev libxcomposite-dev libxdamage-dev \
    libxinerama-dev libxkbcommon-dev \
    libwayland-dev libwayland-bin wayland-protocols \
    libva-dev libvdpau-dev
else
  log "Skipping apt (per --skip-apt)."
fi

# 2) Vulkan SDK (LunarG tarball) -----------------------------------------------
setup_env=""
if [ "$DO_VULKAN" = "1" ]; then
  existing="$(ls -d "${SDK_DIR}"/*/setup-env.sh 2>/dev/null | sort | tail -1 || true)"
  if [ -n "$existing" ]; then
    log "Vulkan SDK already present: $(dirname "$existing")"
  else
    log "Downloading Vulkan SDK (LunarG, ~320 MB)..."
    mkdir -p "$SDK_DIR"
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    curl -fL --retry 3 --retry-delay 5 "$SDK_URL" -o "$tmp/vulkan_sdk.tar.xz"
    log "Extracting into ${SDK_DIR}..."
    tar -xf "$tmp/vulkan_sdk.tar.xz" -C "$SDK_DIR"
    rm -rf "$tmp"; trap - EXIT
  fi

  setup_env="$(ls -d "${SDK_DIR}"/*/setup-env.sh 2>/dev/null | sort | tail -1 || true)"
  [ -n "$setup_env" ] || fail "Could not find setup-env.sh under ${SDK_DIR}; unexpected SDK layout."

  # Activate for this process (so we can verify). setup-env.sh may reference
  # unset vars, so relax -eu while sourcing it.
  set +eu
  # shellcheck disable=SC1090
  source "$setup_env"
  set -eu
  log "VULKAN_SDK=${VULKAN_SDK:-<unset>}"

  # The CMake build does file(GLOB ... ${VULKAN_SDK}/lib/*.a) for glslang/SPIRV.
  miss=""
  for stem in glslang SPIRV SPIRV-Tools; do
    ls "${VULKAN_SDK}/lib/lib${stem}".a >/dev/null 2>&1 || miss="$miss lib${stem}.a"
  done
  [ -z "$miss" ] && log "glslang/SPIRV static libs found in ${VULKAN_SDK}/lib." \
                 || warn "Static libs not found in ${VULKAN_SDK}/lib:$miss"

  marker="# >>> VVDViewer Vulkan SDK >>>"
  if ! grep -qF "$marker" "${HOME}/.bashrc" 2>/dev/null; then
    log "Adding 'source ${setup_env}' to ~/.bashrc"
    {
      echo ""
      echo "$marker"
      echo "[ -f \"${setup_env}\" ] && . \"${setup_env}\""
      echo "# <<< VVDViewer Vulkan SDK <<<"
    } >> "${HOME}/.bashrc"
  else
    log "~/.bashrc already sources a Vulkan SDK; not modifying it."
  fi
else
  log "Skipping Vulkan SDK (per --skip-vulkan)."
fi

# 3) Verify --------------------------------------------------------------------
echo
log "Verification:"
ok=1
check() {
  if command -v "$1" >/dev/null 2>&1; then
    printf '  %-12s %s\n' "$1" "$("$1" --version 2>/dev/null | head -1)"
  else
    printf '  %-12s MISSING\n' "$1"; ok=0
  fi
}
check gcc; check g++; check cmake; check git; check pkg-config; check nasm; check ninja
if command -v pkg-config >/dev/null 2>&1; then
  for pc in gtk+-3.0 x11 xcb wayland-client; do
    if pkg-config --exists "$pc" 2>/dev/null; then
      printf '  pkg-config %-12s OK\n' "$pc"
    else
      printf '  pkg-config %-12s MISSING\n' "$pc"; ok=0
    fi
  done
fi
if [ -n "${VULKAN_SDK:-}" ] && [ -d "${VULKAN_SDK:-/nonexistent}" ]; then
  printf '  %-12s %s\n' "VULKAN_SDK" "$VULKAN_SDK"
elif [ "$DO_VULKAN" = "1" ]; then
  printf '  %-12s %s\n' "VULKAN_SDK" "(set in new shells via ~/.bashrc)"
fi

echo
if [ "$ok" = "1" ]; then
  log "Environment setup complete."
else
  warn "Some checks reported MISSING above — review before building."
fi

cat <<EOF

Next steps
----------
  1) Activate the Vulkan SDK in your CURRENT shell (new terminals do this
     automatically via ~/.bashrc):
EOF
[ -n "$setup_env" ] && echo "         source \"$setup_env\"" || echo "         source <your-vulkan-sdk>/setup-env.sh"
cat <<EOF
  2) Build VVDViewer (first run compiles all dependencies via vcpkg, ~45-90 min):
         cd "$REPO"
         bash build.sh                 # Release
         bash build.sh --debug --clean # Debug, from scratch
EOF
