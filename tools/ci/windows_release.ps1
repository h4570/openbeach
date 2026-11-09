# The Windows release archive (docs/build/releases.md): builds OpenBeach and obl-convert as RelWithDebInfo without the
# post-build conversion, runs the L0 references on that build, lays out the release with tools/ci/release/stage.py and packs
# it. tools/ci/windows_smoke.ps1 then starts the unpacked archive.
#   ./tools/ci/windows_release.ps1 [-Version <v>] [-SkipTests]
# Writes build/dist/openbeach-<version>-windows-x64.zip.
param(
    [string]$Version = '',
    # Only for a local packaging check: the release job always runs the references.
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if (-not $Version) { $Version = (Get-Content (Join-Path $repo 'version.txt') -Raw).Trim() }
$Version = $Version.TrimStart('v')
$name = "openbeach-$Version-windows-x64"
$dist = Join-Path $repo 'build\dist'
$bin = Join-Path $repo 'build\x64\bin\RelWithDebInfo'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { throw "CMake not found in Visual Studio installation: $vs" }

Push-Location $repo
try {
    # The release has no run/ to refresh; the game's default game dir falls back to userdata/ beside the program.
    & $cmake --preset x64 -DOBL_CONVERT_ON_BUILD=OFF
    if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
    & $cmake --build --preset x64-release -- /m
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }

    if (-not $SkipTests) {
        # The world hashes are the behaviour contract: the optimized build must reproduce the L0 references (the same set
        # as the Linux release, tools/ci/linux_release.sh).
        & (Join-Path $repo 'tools\runtests.ps1') -Pack public -Config RelWithDebInfo -NoBuild -Filter `
            l0_boot, l0_gameplay, l0_guests, l0_goal, l0_builders, headless_mismatch, 'replay_l0_*', 'golden_hash_l0_*'
        if ($LASTEXITCODE) { throw "L0 references failed ($LASTEXITCODE)" }
    }

    New-Item -ItemType Directory -Force $dist | Out-Null
    $zip = Join-Path $dist "$name.zip"
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    $epoch = [int](git -C $repo log -1 --format=%ct)
    $binaries = @('OpenBeach.exe', 'obl-convert.exe') | ForEach-Object { Join-Path $bin $_ }
    & python (Join-Path $repo 'tools\ci\release\stage.py') --platform windows --version $Version `
        --out (Join-Path $dist $name) --zip $zip --epoch $epoch --bin @binaries
    if ($LASTEXITCODE) { throw "staging failed ($LASTEXITCODE)" }
    Get-Item $zip | ForEach-Object { '{0}  {1:N1} MB' -f $_.Name, ($_.Length / 1MB) }
} finally {
    Pop-Location
}
