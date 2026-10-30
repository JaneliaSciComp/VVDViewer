<#
.SYNOPSIS
    Creates the Windows release package (ZIP) of VVDViewer.

.DESCRIPTION
    Takes the output of build.ps1 (build\bin\Release) and produces

      dist\VVDViewer<stamp>-win64-vulkan[-gpl].zip           the application
      dist\VVDViewer<stamp>-win64-vulkan[-gpl]-sources.zip   source code of the
                                                              (L)GPL components
      dist\*.sha256                                           checksums

    The ZIP holds one flat folder: VVDViewer.exe with exactly the DLLs it needs
    (the dependency closure computed with dumpbin, including the app-local
    VC++ runtime), plugins\gui\, the resource folders, the settings files, and
    the license notices (THIRD_PARTY_NOTICES.txt, licenses\, BUILDINFO.txt).

    The ffmpeg license mode (LGPL by default, GPL when built with
    build.ps1 -EnableGPL) is detected from the build; GPL packages get a
    "-gpl" suffix.

    Code signing (optional): pass -CertThumbprint (certificate store, also
    hardware tokens), -PfxPath (password in $env:VVD_SIGN_PFX_PASSWORD), or
    -ArtifactSigningMetadata with -ArtifactSigningDlib (Azure Artifact
    Signing). Without any of them the package is created unsigned. The
    Microsoft VC++ runtime DLLs are never re-signed (they must stay unmodified).

.PARAMETER Build
    Run build.ps1 first (Release). -EnableGPL / -Triplet / -ExtraCMakeArgs are
    passed through.

.PARAMETER Stamp
    Identifier in the file name. Defaults to today's date as MMddyy, matching
    the existing releases (e.g. VVDViewer101525-win64-vulkan.zip).

.EXAMPLE
    .\package.ps1
.EXAMPLE
    .\package.ps1 -Build -EnableGPL
.EXAMPLE
    .\package.ps1 -CertThumbprint 0123456789ABCDEF0123456789ABCDEF01234567
#>
[CmdletBinding()]
param(
    [string]$BuildDir = $(Join-Path $PSScriptRoot 'build'),
    [string]$OutDir = $(Join-Path $PSScriptRoot 'dist'),
    [string]$Stamp = $(Get-Date -Format 'MMddyy'),
    [switch]$Build,
    [switch]$EnableGPL,
    [string]$Triplet,
    [string[]]$ExtraCMakeArgs = @(),
    [string]$NblastJar,
    [switch]$NoSourcesArchive,
    [string]$CertThumbprint = $env:VVD_SIGN_CERT_THUMBPRINT,
    [string]$PfxPath = $env:VVD_SIGN_PFX_PATH,
    [string]$ArtifactSigningMetadata = $env:VVD_SIGN_ARTIFACT_METADATA,
    [string]$ArtifactSigningDlib = $env:VVD_SIGN_ARTIFACT_DLIB,
    [string]$TimestampUrl
)

# 'Continue' + explicit $LASTEXITCODE checks, as in build.ps1 (native tools
# write progress to stderr).
$ErrorActionPreference = 'Continue'
$repo = $PSScriptRoot
$Config = 'Release'

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Info($msg) { Write-Host $msg -ForegroundColor Cyan }
function Warn($msg) { Write-Host "WARNING: $msg" -ForegroundColor Yellow }
# UTF-8 without BOM (Set-Content -Encoding utf8 in PowerShell 5.1 writes a BOM).
function Write-TextFile($path, $text) { [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding $false)) }

Info "=== VVDViewer packaging (Windows) ==="

# 1) Build (optional) and read the build configuration -----------------------
if ($Build) {
    $buildArgs = @{ Config = $Config; BuildDir = $BuildDir; ExtraCMakeArgs = $ExtraCMakeArgs }
    if ($EnableGPL) { $buildArgs.EnableGPL = $true }
    if ($Triplet) { $buildArgs.Triplet = $Triplet }
    & (Join-Path $repo 'build.ps1') @buildArgs
    if ($LASTEXITCODE -ne 0) { Fail "build.ps1 failed." }
}

$cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
if (-not (Test-Path $cacheFile)) { Fail "No CMake build found in $BuildDir. Run build.ps1 first (or pass -Build)." }
$cache = @{}
foreach ($line in Get-Content $cacheFile) {
    if ($line -match '^([A-Za-z0-9_]+):[A-Z]+=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
}
$binDir = Join-Path $BuildDir "bin\$Config"
$exe = Join-Path $binDir 'VVDViewer.exe'
if (-not (Test-Path $exe)) { Fail "$exe not found. Build the Release configuration first." }

$tripletName = $cache['VCPKG_TARGET_TRIPLET']
$installedRoot = if ($cache['VCPKG_INSTALLED_DIR']) { $cache['VCPKG_INSTALLED_DIR'] } else { Join-Path $BuildDir 'vcpkg_installed' }
$installed = Join-Path $installedRoot $tripletName
$toolchain = $cache['CMAKE_TOOLCHAIN_FILE']
$vcpkgRoot = if ($toolchain) { (Resolve-Path (Join-Path (Split-Path $toolchain -Parent) '..\..')).Path } else { Join-Path $repo 'vcpkg' }
if (-not (Test-Path (Join-Path $installed 'share'))) { Fail "vcpkg installed tree not found: $installed" }

# ffmpeg license mode, from what vcpkg actually built.
$ffCopyright = Join-Path $installed 'share\ffmpeg\copyright'
if (-not (Test-Path $ffCopyright)) { Fail "ffmpeg is not installed in $installed." }
$ffHead = (Get-Content $ffCopyright -TotalCount 5) -join ' '
if ($ffHead -match 'GNU GENERAL PUBLIC LICENSE') { $mode = 'GPL' }
elseif ($ffHead -match 'GNU LESSER GENERAL PUBLIC LICENSE') { $mode = 'LGPL' }
else { Fail "Cannot tell whether ffmpeg was built as LGPL or GPL ($ffCopyright)." }
$hasX264 = Test-Path (Join-Path $installed 'share\x264')
if (($mode -eq 'GPL') -ne $hasX264) { Fail "Inconsistent build: ffmpeg is $mode but x264 is $(if ($hasX264) {'present'} else {'absent'})." }
if ($mode -eq 'GPL' -and $cache['ENABLE_ND2'] -eq 'ON') {
    Fail "This build has both the GPL ffmpeg and the proprietary ND2 reader; such a package cannot be distributed."
}

$name = "VVDViewer$Stamp-win64-vulkan" + $(if ($mode -eq 'GPL') { '-gpl' } else { '' })
$stage = Join-Path $OutDir $name
$zip = Join-Path $OutDir "$name.zip"
$srcName = "$name-sources"
$srcStage = Join-Path $OutDir $srcName
$srcZip = Join-Path $OutDir "$srcName.zip"
Info "Package:  $name  (ffmpeg: $mode, triplet: $tripletName)"

# 2) Toolchain checks -----------------------------------------------------------
# The Visual Studio generators record the compiler in CMakeFiles/<ver>/CMakeCXXCompiler.cmake,
# not in the cache.
$compiler = $cache['CMAKE_CXX_COMPILER']
if (-not $compiler) {
    $cxxInfo = Get-ChildItem (Join-Path $BuildDir 'CMakeFiles\*\CMakeCXXCompiler.cmake') -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($cxxInfo -and ((Get-Content $cxxInfo.FullName -Raw) -match 'set\(CMAKE_CXX_COMPILER "([^"]+)"\)')) { $compiler = $Matches[1] }
}
$compiler = $compiler -replace '/', '\'
if (-not $compiler -or -not (Test-Path $compiler)) { Fail "C++ compiler of the build not found ($compiler)." }
$dumpbin = Join-Path (Split-Path $compiler -Parent) 'dumpbin.exe'
if (-not (Test-Path $dumpbin)) { Fail "dumpbin.exe not found next to $compiler" }

# Files of a pre-release Visual Studio may not be redistributed (VS license,
# "Distributable Code"), which matters for the VC++ runtime DLLs we ship.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsVersion = 'unknown'
if (Test-Path $vswhere) {
    $vsAll = & $vswhere -all -prerelease -format json | ConvertFrom-Json
    $vs = $vsAll | Where-Object { $compiler.StartsWith($_.installationPath, [StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
    if ($vs) {
        $vsVersion = "$($vs.displayName) $($vs.catalog.productDisplayVersion) ($($vs.channelId))"
        if ($vs.isPrerelease) {
            Fail "The build used a pre-release Visual Studio ($vsVersion). Its VC++ runtime files are not licensed for redistribution; build with a release-channel Visual Studio."
        }
    }
}

# 3) Stage the application with its exact DLL dependency closure ----------------
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force (Join-Path $stage 'plugins\gui') | Out-Null

$pluginNames = 'FijiInterfacePlugin.dll', 'NBLASTGuiPlugin.dll', 'NeuronAnnotatorPlugin.dll'
Copy-Item $exe $stage
foreach ($p in $pluginNames) {
    $src = Join-Path $binDir $p
    if (-not (Test-Path $src)) { Fail "Plugin not found: $src" }
    Copy-Item $src (Join-Path $stage 'plugins\gui')
}

function Get-PEDependencies([string]$file) {
    $out = & $dumpbin /nologo /dependents $file
    if ($LASTEXITCODE -ne 0) { Fail "dumpbin failed on $file" }
    $out | ForEach-Object { if ($_ -match '^\s{4}(\S+\.dll)\s*$') { $Matches[1] } }
}

$sys32 = Join-Path $env:SystemRoot 'System32'
$debugCrt = '^(msvcp140d|msvcp140_[0-9a-z_]+d|vcruntime140d|vcruntime140_1d|concrt140d|ucrtbased)\.dll$'
# Provided by the GPU driver / Vulkan runtime on the user's machine, not by us.
$externalOk = @('vulkan-1.dll')
$appDlls = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$systemDlls = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$queue = [System.Collections.Generic.Queue[string]]::new()
$queue.Enqueue($exe)
foreach ($p in $pluginNames) { $queue.Enqueue((Join-Path $binDir $p)) }
while ($queue.Count -gt 0) {
    $file = $queue.Dequeue()
    foreach ($dep in Get-PEDependencies $file) {
        if ($dep -match $debugCrt) { Fail "$(Split-Path $file -Leaf) depends on the debug runtime $dep (not redistributable). Package a Release build." }
        $local = Join-Path $binDir $dep
        if (Test-Path $local) {
            if ($pluginNames -contains $dep) { continue }
            if ($appDlls.Add($dep)) { $queue.Enqueue($local) }
        } elseif ($dep -match '^(api|ext)-ms-' -or (Test-Path (Join-Path $sys32 $dep)) -or $externalOk -contains $dep) {
            [void]$systemDlls.Add($dep)
        } else {
            Fail "Unresolved dependency: $(Split-Path $file -Leaf) needs $dep, which is neither in $binDir nor a Windows system DLL."
        }
    }
}
foreach ($d in $appDlls) { Copy-Item (Join-Path $binDir $d) $stage }
Info ("Bundled DLLs ($($appDlls.Count)): " + (($appDlls | Sort-Object) -join ', '))
Info ("System DLLs used ($($systemDlls.Count)): " + ($systemDlls -join ', '))

# 4) Resources, settings and plugin helper files ---------------------------------
foreach ($d in 'CL_code', 'Fonts', 'Scripts', 'ColorDepthMIP') {
    Copy-Item -Recurse (Join-Path $repo $d) (Join-Path $stage $d)
}
Copy-Item (Join-Path $repo 'Settings\*') $stage
# Fiji / NBLAST plugin helpers are looked up next to VVDViewer.exe.
$helpers = @(
    'plugin_fiji\fiji\macros\vvd_listener.txt',
    'plugin_fiji\fiji\macros\vvd_run.txt',
    'plugin_fiji\fiji\macros\vvd_quit.txt',
    'plugin_fiji\fiji\plugins\vvd_listener.jar',
    'plugin_fiji\fiji\plugins\Apply_Bigwarp_Filter.jar',
    'plugin_fiji\vvd\ActivatePID.vbs',
    'plugin_nblast\r\nblast_search.R'
)
foreach ($h in $helpers) {
    $src = Join-Path $repo $h
    if (-not (Test-Path $src)) { Fail "Plugin helper file missing: $src" }
    Copy-Item $src $stage
}
# NBLAST_Skeletonize.jar is not in the repository (only its .java source); ship
# it when one is supplied or present next to the source.
if (-not $NblastJar) {
    foreach ($cand in 'plugin_nblast\fiji\NBLAST_Skeletonize.jar', 'packaging\extra\NBLAST_Skeletonize.jar') {
        if (Test-Path (Join-Path $repo $cand)) { $NblastJar = Join-Path $repo $cand; break }
    }
}
if ($NblastJar) {
    if (-not (Test-Path $NblastJar)) { Fail "-NblastJar not found: $NblastJar" }
    Copy-Item $NblastJar (Join-Path $stage 'NBLAST_Skeletonize.jar')
    Info "Included NBLAST_Skeletonize.jar"
} else {
    Info "NBLAST_Skeletonize.jar not available - skipped (pass -NblastJar to include it)."
}

# 5) Licenses, notices, build info ------------------------------------------------
Copy-Item (Join-Path $repo 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item (Join-Path $repo 'LICENSE_DEPENDENCIES.md') $stage

$crtShipped = $appDlls | Where-Object { $_ -match '^(msvcp140|vcruntime140|concrt140|vccorlib140)' } | Sort-Object
$crtVersion = ''
if ($crtShipped) { $crtVersion = (Get-Item (Join-Path $stage ($crtShipped | Select-Object -First 1))).VersionInfo.FileVersion }
$crtNotice = Join-Path $env:TEMP "microsoft-vc-runtime-$PID.txt"
@"
Microsoft Visual C++ Runtime $crtVersion
($($crtShipped -join ', '))

These files are redistributed unmodified as "Distributable Code" under the
Microsoft Software License Terms for Microsoft Visual Studio, solely for use
with VVDViewer. Microsoft and its suppliers retain all rights in them; they
may not be modified, reverse engineered or distributed separately from
VVDViewer except as permitted by those terms. The official installer is
available from https://aka.ms/vs/17/release/vc_redist.x64.exe .
"@ | ForEach-Object { Write-TextFile $crtNotice $_ }

$vulkanSdk = $env:VULKAN_SDK
if ($cache['Vulkan_INCLUDE_DIR']) { $vulkanSdk = Split-Path $cache['Vulkan_INCLUDE_DIR'] -Parent }

$collectArgs = @(
    "-DINSTALLED_DIR=$installed",
    "-DVCPKG_ROOT=$vcpkgRoot",
    "-DSTAGING=$stage",
    "-DFFMPEG_MODE=$mode",
    "-DPACKAGE_NAME=$name",
    "-DVULKAN_SDK=$vulkanSdk",
    "-DEXTRA_NOTICES=Microsoft Visual C++ Runtime $crtVersion|Microsoft Software License Terms (Distributable Code)|$crtNotice|microsoft-vc-runtime.txt"
)
if (-not $NoSourcesArchive) {
    if (Test-Path $srcStage) { Remove-Item -Recurse -Force $srcStage }
    $collectArgs += "-DSOURCES_DIR=$srcStage", "-DSOURCES_ARCHIVE=$srcName.zip"
}
& cmake @collectArgs -P (Join-Path $repo 'packaging\collect_licenses.cmake')
$rc = $LASTEXITCODE
Remove-Item -Force $crtNotice -ErrorAction SilentlyContinue
if ($rc -ne 0) { Fail "Collecting the third-party licenses failed." }

$commit = (git -C $repo rev-parse HEAD 2>$null)
$dirty = if (git -C $repo status --porcelain --untracked-files=no 2>$null) { ' (with uncommitted changes)' } else { '' }
$vcpkgCommit = (git -C $vcpkgRoot rev-parse HEAD 2>$null)
@"
VVDViewer Windows package
Package:          $name
Built:            $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')
Source commit:    $commit$dirty  (https://github.com/JaneliaSciComp/VVDViewer)
ffmpeg license:   $mode $(if ($mode -eq 'GPL') { '(x264/x265; movie export H.264)' } else { '(movie export MPEG-4 Part 2)' })
ND2 reader:       $(if ($cache['ENABLE_ND2'] -eq 'ON') { 'enabled' } else { 'disabled' })
vcpkg commit:     $vcpkgCommit
vcpkg triplet:    $tripletName
Visual Studio:    $vsVersion
VC++ runtime:     $crtVersion (app-local)
Vulkan SDK:       $vulkanSdk
"@ | ForEach-Object { Write-TextFile (Join-Path $stage 'BUILDINFO.txt') $_ }

# 6) Code signing (optional) -------------------------------------------------------
$signMode = $null
if ($ArtifactSigningMetadata) { $signMode = 'artifact' } elseif ($CertThumbprint) { $signMode = 'thumbprint' } elseif ($PfxPath) { $signMode = 'pfx' }
if ($signMode) {
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Sort-Object { [version]($_.Directory.Parent.Name) } -Descending | Select-Object -First 1
    if (-not $signtool) { Fail "signtool.exe not found (install the Windows SDK)." }
    # Our own binaries and the third-party DLLs we built. Never the Microsoft
    # runtime (must stay unmodified) and nothing that already carries a valid
    # signature.
    $toSign = Get-ChildItem $stage -Recurse -Include *.exe, *.dll | Where-Object {
        $_.Name -notmatch '^(msvcp140|vcruntime140|concrt140|vccorlib140)' -and
        (Get-AuthenticodeSignature $_.FullName).Status -ne 'Valid'
    }
    $common = @('sign', '/fd', 'SHA256', '/td', 'SHA256')
    switch ($signMode) {
        'artifact' {
            if (-not $ArtifactSigningDlib -or -not (Test-Path $ArtifactSigningDlib)) { Fail "-ArtifactSigningDlib (Azure.CodeSigning.Dlib.dll) is required with -ArtifactSigningMetadata." }
            if (-not $TimestampUrl) { $TimestampUrl = 'http://timestamp.acs.microsoft.com' }
            $common += '/dlib', $ArtifactSigningDlib, '/dmdf', $ArtifactSigningMetadata
        }
        'thumbprint' {
            if (-not $TimestampUrl) { $TimestampUrl = 'http://timestamp.digicert.com' }
            $common += '/sha1', $CertThumbprint
        }
        'pfx' {
            if (-not (Test-Path $PfxPath)) { Fail "-PfxPath not found: $PfxPath" }
            if (-not $TimestampUrl) { $TimestampUrl = 'http://timestamp.digicert.com' }
            $common += '/f', $PfxPath
            if ($env:VVD_SIGN_PFX_PASSWORD) { $common += '/p', $env:VVD_SIGN_PFX_PASSWORD }
        }
    }
    $common += '/tr', $TimestampUrl
    Info "Signing $($toSign.Count) files ($signMode, timestamp $TimestampUrl)..."
    & $signtool.FullName @common @($toSign | ForEach-Object FullName)
    if ($LASTEXITCODE -ne 0) { Fail "signtool sign failed." }
    foreach ($f in $toSign) {
        $sig = Get-AuthenticodeSignature $f.FullName
        if (-not $sig.SignerCertificate) { Fail "No signature on $($f.Name) after signing." }
        if ($sig.Status -ne 'Valid') { Warn "$($f.Name): signed, but not trusted on this machine ($($sig.Status))." }
    }
} else {
    Warn "No signing certificate given - the package is NOT code-signed."
}

# 7) Archives and checksums --------------------------------------------------------
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
if (-not (Test-Path $tar)) { Fail "$tar not found (Windows 10 1803 or later is required)." }
& $tar -a -c -f $zip -C $OutDir $name
if ($LASTEXITCODE -ne 0) { Fail "Creating $zip failed." }
$outputs = @($zip)
if (-not $NoSourcesArchive -and (Test-Path $srcStage) -and (Get-ChildItem $srcStage)) {
    if (Test-Path $srcZip) { Remove-Item -Force $srcZip }
    & $tar -a -c -f $srcZip -C $OutDir $srcName
    if ($LASTEXITCODE -ne 0) { Fail "Creating $srcZip failed." }
    $outputs += $srcZip
}
foreach ($o in $outputs) {
    $h = (Get-FileHash -Algorithm SHA256 $o).Hash.ToLower()
    # One "hash  name" line ending in LF, like sha256sum (files are concatenated
    # into SHA256SUMS.txt and the attestation subject list).
    Write-TextFile "$o.sha256" "$h  $(Split-Path $o -Leaf)`n"
}

Write-Host ""
Write-Host "=== Packaging complete ===" -ForegroundColor Green
foreach ($o in $outputs) {
    Write-Host ("{0}  ({1:N1} MB)" -f $o, ((Get-Item $o).Length / 1MB))
}
Write-Host "Staging folder: $stage"
