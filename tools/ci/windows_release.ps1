# The Windows release (docs/build/releases.md): builds OpenBeach and obl-convert as RelWithDebInfo without the post-build
# conversion, runs the L0 references on that build, lays out the release with tools/ci/release/stage.py, packs it and builds
# the installer from the same directory (Inno Setup 6, tools/ci/release/installer/openbeach.iss). tools/ci/windows_smoke.ps1
# then starts the unpacked archive and the silently installed game.
#   ./tools/ci/windows_release.ps1 [-Version <v>] [-SkipTests]
# Writes build/dist/openbeach-<version>-windows-x64.zip and build/dist/openbeach-<version>-windows-x64-installer.exe.
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

# ISCC.exe of Inno Setup 6: $env:ISCC, PATH, a machine install or a per-user one (winget install JRSoftware.InnoSetup).
function Find-Iscc {
    $candidates = @($env:ISCC, (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source,
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")
    $found = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
    if (-not $found) { throw 'Inno Setup 6 (ISCC.exe) not found: winget install JRSoftware.InnoSetup, or set ISCC' }
    return $found
}
$iscc = Find-Iscc

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
    $installer = Join-Path $dist "$name-installer.exe"
    Remove-Item -LiteralPath $zip, $installer -Force -ErrorAction SilentlyContinue
    $epoch = [int](git -C $repo log -1 --format=%ct)
    $binaries = @('OpenBeach.exe', 'obl-convert.exe') | ForEach-Object { Join-Path $bin $_ }
    & python (Join-Path $repo 'tools\ci\release\stage.py') --platform windows --version $Version `
        --out (Join-Path $dist $name) --zip $zip --epoch $epoch --bin @binaries
    if ($LASTEXITCODE) { throw "staging failed ($LASTEXITCODE)" }
    & $iscc /Q "/DAppVersion=$Version" "/DStageDir=$(Join-Path $dist $name)" "/DRepoDir=$repo" "/O$dist" "/F$name-installer" `
        (Join-Path $repo 'tools\ci\release\installer\openbeach.iss')
    if ($LASTEXITCODE) { throw "installer build failed ($LASTEXITCODE)" }
    Get-Item $zip, $installer | ForEach-Object { '{0}  {1:N1} MB' -f $_.Name, ($_.Length / 1MB) }
} finally {
    Pop-Location
}
