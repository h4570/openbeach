# Configures and builds the project with the CMake bundled with Visual Studio 2026
# (the generator "Visual Studio 18 2026" needs CMake >= 4.2).
#   ./build.ps1                 -> x64 Debug (build/x64)
#   ./build.ps1 -Config release -> RelWithDebInfo
#   ./build.ps1 -Test           -> build, then run the unit tests (ctest, no game; docs/testing/unit-tests.md)
param(
    [ValidateSet('debug', 'release')][string]$Config = 'debug',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { throw "CMake not found in Visual Studio installation: $vs" }

Push-Location $PSScriptRoot
try {
    & $cmake --preset x64
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & $cmake --build --preset "x64-$Config"
    if ($LASTEXITCODE -or -not $Test) { exit $LASTEXITCODE }
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --preset "x64-$Config"
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
