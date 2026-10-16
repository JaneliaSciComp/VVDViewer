<#
.SYNOPSIS
    Installs a pinned LunarG Vulkan SDK on a Windows CI runner and exports
    VULKAN_SDK for the following GitHub Actions steps.

.EXAMPLE
    .\packaging\ci\install-vulkan-sdk.ps1 -Version 1.4.350.0
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Root = "C:\VulkanSDK\$Version"
)
$ErrorActionPreference = 'Stop'

$tmp = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$installer = Join-Path $tmp "vulkansdk-windows-X64-$Version.exe"
$url = "https://sdk.lunarg.com/sdk/download/$Version/windows/vulkansdk-windows-X64-$Version.exe"
Write-Host "Downloading $url"
& curl.exe -fL --retry 3 --retry-delay 5 -o $installer $url
if ($LASTEXITCODE -ne 0) { throw "Download failed: $url" }

# Qt Installer Framework CLI: default components (headers, loader, glslang /
# SPIRV-Tools release libraries) into $Root, no prompts.
Write-Host "Installing Vulkan SDK $Version into $Root"
$p = Start-Process -FilePath $installer -Wait -PassThru -NoNewWindow -ArgumentList @(
    '--root', $Root, '--accept-licenses', '--default-answer', '--confirm-command', 'install')
if ($p.ExitCode -ne 0) { throw "Vulkan SDK installer failed with exit code $($p.ExitCode)" }
foreach ($f in 'Include\vulkan\vulkan.h', 'Lib\vulkan-1.lib', 'Lib\glslang.lib', 'Lib\SPIRV-Tools-opt.lib') {
    if (-not (Test-Path (Join-Path $Root $f))) { throw "Vulkan SDK install is missing $f" }
}

if ($env:GITHUB_ENV) {
    "VULKAN_SDK=$Root" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
    (Join-Path $Root 'Bin') | Out-File -FilePath $env:GITHUB_PATH -Append -Encoding utf8
}
Write-Host "VULKAN_SDK=$Root"
