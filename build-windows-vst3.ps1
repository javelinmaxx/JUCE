<#
.SYNOPSIS
Build the Windows x64 Multi-Compressor VST3 with Visual Studio 2022.
.EXAMPLE
.\build-windows-vst3.ps1
.EXAMPLE
.\build-windows-vst3.ps1 -Parallel 8
#>
[CmdletBinding()]
param (
    [ValidateRange(1, 64)]
    [int]$Parallel = 4,
    [string]$BuildDirectory = 'build-yinyang-win'
)

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') {
    throw 'Run this script on Windows, or use the GitHub Actions workflow from your Mac.'
}
Get-Command cmake -ErrorAction Stop | Out-Null
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $PSScriptRoot $BuildDirectory
}
$source = Join-Path $PSScriptRoot 'projects/YinyangCompressor'

& cmake -S $source -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 `
    '-DYINYANG_COMPRESSOR_BUILD_TESTS=OFF' `
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>'
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed (exit $LASTEXITCODE)." }

& cmake --build $BuildDirectory --config Release --target YinyangCompressor_VST3 --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw "VST3 build failed (exit $LASTEXITCODE)." }

$bundle = Join-Path $BuildDirectory 'YinyangCompressor_artefacts/Release/VST3/Yinyang Compressor.vst3'
$binary = Join-Path $bundle 'Contents/x86_64-win/Yinyang Compressor.vst3'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "Windows VST3 binary missing: $binary"
}
Write-Host "Built: $bundle"
Write-Host 'Copy the entire .vst3 folder to C:\Program Files\Common Files\VST3\ and rescan your DAW.'
