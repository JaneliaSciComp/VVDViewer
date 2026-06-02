<#
.SYNOPSIS
    One-command auto-build for VVDViewer on Windows.

.DESCRIPTION
    Downloads and builds every third-party dependency (EXCEPT the Vulkan SDK)
    from source via vcpkg, then configures and builds VVDViewer with CMake.
    All dependencies are linked statically except wxWidgets, which is a DLL.

    The Vulkan SDK is NOT managed here: install it separately from
    https://vulkan.lunarg.com/ so that the VULKAN_SDK environment variable is set.

.PARAMETER Config
    Build configuration: Release (default) or Debug.

.PARAMETER VcpkgRoot
    Location of vcpkg. Defaults to %VCPKG_ROOT% if set, otherwise <repo>\vcpkg
    (cloned and bootstrapped automatically on first run).

.PARAMETER BuildDir
    CMake build directory. Defaults to <repo>\build.

.PARAMETER Triplet
    vcpkg triplet. Defaults to the bundled x64-windows-vvd (static libs, dynamic
    CRT, wxWidgets dynamic).

.PARAMETER Clean
    Delete the build directory before configuring.

.PARAMETER EnableND2
    Enable the Nikon ND2 reader. Requires the proprietary Nd2ReadSdk; you must
    pass -DND2_INCLUDE_DIR / -DND2_LIBRARY_DIR via CMake yourself. Off by default.

.EXAMPLE
    .\build.ps1
.EXAMPLE
    .\build.ps1 -Config Debug -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [string]$VcpkgRoot = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $PSScriptRoot 'vcpkg' }),
    [string]$BuildDir = $(Join-Path $PSScriptRoot 'build'),
    [string]$Triplet = 'x64-windows-vvd',
    [switch]$Clean,
    [switch]$EnableND2
)

# NOTE: keep this 'Continue', not 'Stop'. Native tools (git, cmake) write
# progress to stderr; under 'Stop' with redirected output PowerShell 5.1 turns
# that stderr into a terminating NativeCommandError. We check $LASTEXITCODE
# explicitly after each native command instead.
$ErrorActionPreference = 'Continue'
$repo = $PSScriptRoot

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Info($msg) { Write-Host $msg -ForegroundColor Cyan }

Info "=== VVDViewer auto-build (Windows) ==="

# 1) Prerequisites ----------------------------------------------------------
if (-not $env:VULKAN_SDK) {
    Fail "VULKAN_SDK is not set. Install the Vulkan SDK (https://vulkan.lunarg.com/) and re-open the shell."
}
if (-not (Test-Path $env:VULKAN_SDK)) {
    Fail "VULKAN_SDK points to a missing path: $env:VULKAN_SDK"
}
Info "Vulkan SDK: $env:VULKAN_SDK"
foreach ($tool in 'git', 'cmake') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool was not found on PATH." }
}

# 2) Locate Visual Studio and import its x64 build environment --------------
#    (vcvars64 puts dumpbin on PATH, which vcpkg uses for app-local DLL deploy.)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found; install Visual Studio 2017+ with the 'Desktop development with C++' workload." }
# -prerelease so newer VS (e.g. Visual Studio 2026 / v18) is detected and preferred.
$vsInstall = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { Fail "No Visual Studio with the C++ (VC Tools x64) workload was found." }
$vsVer = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
$vsMajor = [int]($vsVer.Split('.')[0])
$generator = switch ($vsMajor) {
    18 { 'Visual Studio 18 2026' }
    17 { 'Visual Studio 17 2022' }
    16 { 'Visual Studio 16 2019' }
    15 { 'Visual Studio 15 2017' }
    default { Fail "Unsupported Visual Studio version: $vsVer" }
}
Info "Visual Studio: $vsInstall ($vsVer) -> generator '$generator'"

# Ensure the Windows SDK build tools (rc.exe / mt.exe) exist. vcpkg's native port
# builds and the app's resource (.rc) compilation require them; without a Windows
# SDK, MSVC can compile a file but cannot link it. (We deliberately do NOT import
# vcvars here: vcpkg sets up its own per-triplet MSVC environment, and MSBuild
# supplies dumpbin for the app-local DLL deploy.)
$rc = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\rc.exe' -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $rc) {
    Fail @'
The Windows SDK build tools (rc.exe / mt.exe) were not found, so MSVC cannot link
native code. Install a Windows 10/11 SDK, then re-run this script:
  Visual Studio Installer -> Modify -> "Desktop development with C++" workload
  (or Individual components -> check a "Windows 11 SDK (10.0.22621)" or
   "Windows 10 SDK (10.0.19041)" or newer).
'@
}
Info ("Windows SDK tools: " + (Split-Path $rc.FullName -Parent))

# vcvars (invoked by vcpkg for every native port) resolves the Windows SDK by
# calling vswhere.exe by bare name. Ensure the VS Installer directory (which
# contains vswhere.exe) is on PATH, otherwise vcvars cannot find the SDK and
# native builds fail with rc.exe / mt.exe "not found".
$vsInstallerDir = Split-Path $vswhere -Parent
if (($env:PATH -split ';') -notcontains $vsInstallerDir) {
    $env:PATH = "$vsInstallerDir;$env:PATH"
}

# Force vcpkg to build its ports with the SAME Visual Studio selected above.
# Without this, vcpkg's own detection ignores prerelease installs and may fall
# back to an older VS (e.g. 2019) for the dependency builds.
$env:VCPKG_VISUAL_STUDIO_PATH = $vsInstall

# 3) Bootstrap vcpkg --------------------------------------------------------
if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
    Info "Cloning vcpkg into $VcpkgRoot ..."
    git clone https://github.com/microsoft/vcpkg $VcpkgRoot
    if ($LASTEXITCODE -ne 0) { Fail "vcpkg clone failed." }
}
if (-not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
    Info "Bootstrapping vcpkg ..."
    & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { Fail "vcpkg bootstrap failed." }
}
$env:VCPKG_ROOT = $VcpkgRoot
Info ("vcpkg baseline commit: " + (git -C $VcpkgRoot rev-parse --short HEAD))

# 4) Configure (this triggers the vcpkg manifest install) -------------------
if ($Clean -and (Test-Path $BuildDir)) { Info "Cleaning $BuildDir"; Remove-Item -Recurse -Force $BuildDir }
$toolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
$cmakeArgs = @(
    '-S', $repo,
    '-B', $BuildDir,
    '-G', $generator,
    '-A', 'x64',
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=$Triplet",
    "-DVCPKG_OVERLAY_TRIPLETS=$(Join-Path $repo 'triplets')",
    "-DCMAKE_BUILD_TYPE=$Config"
)
if ($EnableND2) { $cmakeArgs += '-DENABLE_ND2=ON' }

Write-Host "Configuring... first run downloads & builds ALL dependencies from source (can take 30-90 min)." -ForegroundColor Yellow
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed." }

# 5) Build ------------------------------------------------------------------
Write-Host "Building VVDViewer ($Config)..." -ForegroundColor Yellow
& cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { Fail "Build failed." }

# 6) Report -----------------------------------------------------------------
$exe = Join-Path $BuildDir "bin\$Config\VVDViewer.exe"
Write-Host ""
Write-Host "=== Build complete ===" -ForegroundColor Green
if (Test-Path $exe) {
    Write-Host "Executable: $exe"
} else {
    Write-Host "Expected executable not found at $exe - check the build output above." -ForegroundColor Yellow
}
$nd2 = if ($EnableND2) { 'ENABLED' } else { 'disabled (default)' }
Write-Host "ND2 (Nikon) reader: $nd2.  libCZI: enabled (via vcpkg).  Vulkan SDK: external ($env:VULKAN_SDK)."
