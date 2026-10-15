#!/usr/bin/env bash
#
# Installs a pinned LunarG Vulkan SDK on a macOS CI runner and exports
# VULKAN_SDK for the following GitHub Actions steps.
#
# Besides the SDK itself this installs the "System Global Installation"
# component (com.lunarg.vulkan.usr) into /usr/local, because the bundle step in
# CMakeLists.txt (fix_bundle.sh) copies libMoltenVK.dylib and MoltenVK_icd.json
# from /usr/local.
#
# Usage: bash packaging/ci/install-vulkan-sdk-macos.sh <version>
set -euo pipefail

VER="${1:?usage: install-vulkan-sdk-macos.sh <version>}"
ROOT="$HOME/VulkanSDK/$VER"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

URL="https://sdk.lunarg.com/sdk/download/$VER/mac/vulkansdk-macos-$VER.zip"
echo "Downloading $URL"
curl -fL --retry 3 --retry-delay 5 -o "$TMP/sdk.zip" "$URL"
unzip -q "$TMP/sdk.zip" -d "$TMP"
APP="$(ls -d "$TMP"/*.app | head -1)"
[ -n "$APP" ] || { echo "No installer app found in $URL" >&2; exit 1; }
EXE="$APP/Contents/MacOS/$(basename "${APP%.app}")"

echo "Installing Vulkan SDK $VER into $ROOT (+ /usr/local)"
sudo "$EXE" --root "$ROOT" --accept-licenses --default-answer --confirm-command \
  install com.lunarg.vulkan.core com.lunarg.vulkan.usr
sudo chown -R "$(id -u):$(id -g)" "$ROOT"

for f in "$ROOT/macOS/include/vulkan/vulkan.h" /usr/local/lib/libMoltenVK.dylib \
         /usr/local/share/vulkan/icd.d/MoltenVK_icd.json; do
  [ -e "$f" ] || { echo "Vulkan SDK install is missing $f" >&2; exit 1; }
done

if [ -n "${GITHUB_ENV:-}" ]; then
  echo "VULKAN_SDK=$ROOT/macOS" >> "$GITHUB_ENV"
  echo "$ROOT/macOS/bin" >> "$GITHUB_PATH"
fi
echo "VULKAN_SDK=$ROOT/macOS"
