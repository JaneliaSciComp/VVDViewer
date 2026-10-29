# Building VVDViewerQt on this Mac (otsunah-lm1)

Checkout: `~/Documents/GitHub/VVDViewer`, branch `VVDViewer_QT` (df2c815,
"NA plugin: keep the previous session until the new data load", 2026-10-23).

## 1. One-time prerequisites (skip anything already installed)

```bash
# Command Line Tools (full Xcode is optional; build.sh falls back to Ninja)
xcode-select --install

# Homebrew, then the tools build.sh expects
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install cmake ninja qt nasm autoconf automake libtool autoconf-archive pkg-config
```

Vulkan SDK: download the macOS installer from https://vulkan.lunarg.com/sdk/home,
run it (default location `~/VulkanSDK/<version>`), and tick "System Global
Installation" so `libMoltenVK.dylib` lands in `/usr/local/lib`.

## 2. Build (every new Terminal window)

```bash
source ~/VulkanSDK/*/setup-env.sh          # sets VULKAN_SDK
cd ~/Documents/GitHub/VVDViewer
bash build.sh --enable-qt
```

The first run clones vcpkg into `./vcpkg` and compiles every dependency
(wxWidgets, boost, ffmpeg, HDF5, ...) from source: expect 30-90 min on an
M-series Mac. Later runs only rebuild what changed.

Result:

* Xcode generator (full Xcode installed): `build/bin/Release/VVDViewerQt.app`
* Ninja / Makefiles (Command Line Tools only): `build/bin/VVDViewerQt.app`

Run it from Terminal to see its log output:

```bash
open build/bin/*/VVDViewerQt.app 2>/dev/null || open build/bin/VVDViewerQt.app
# or, with stdout/stderr in the terminal:
build/bin/Release/VVDViewerQt.app/Contents/MacOS/VVDViewerQt
```

## 3. If something fails

* `VULKAN_SDK is not set` -> you forgot step 2's `source` line.
* `Does not match the generator used previously` -> `bash build.sh --enable-qt --clean`
  (you installed/removed Xcode or ninja since the last configure).
* Hang at start-up with a black render view -> run with
  `QT_MTL_NO_TRANSACTION=1` set; if that fixes it the branch is missing the
  QtMain.cpp fix (it is present at df2c815, so this should not happen).
