# Record repeatable images of the current build (two identical runs), or verify the committed ones.
# -Pack private (default) uses the imported data and tests/golden-images/m6-gui; -Pack l0 runs the own L0 pack
# (tests/m6_gui_golden_l0.txt, OBL_PACK=l0) against tests/golden-images/m6-gui-l0. -CheckOnly without -Pack follows OBL_PACK.
param(
    [switch]$Record,
    [switch]$CheckOnly,
    [string]$GameDir = '',
    [ValidateSet('', 'private', 'l0')][string]$Pack = '',
    [ValidateSet('Debug', 'RelWithDebInfo')][string]$Config = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $GameDir) { $GameDir = Join-Path $repo 'run' }
$GameDir = [IO.Path]::GetFullPath($GameDir)
if (-not $Pack) { $Pack = if ($env:OBL_PACK -eq 'l0') { 'l0' } else { 'private' } }
$fixture = Join-Path $repo $(if ($Pack -eq 'l0') { 'tests/m6_gui_golden_l0.txt' } else { 'tests/m6_gui_golden.txt' })
$golden = Join-Path $repo $(if ($Pack -eq 'l0') { 'tests/golden-images/m6-gui-l0' } else { 'tests/golden-images/m6-gui' })
$names = @(Get-Content -LiteralPath $fixture | ForEach-Object {
    if ($_ -match '^shot (\S+)\s*$') { $Matches[1] }
})
$imgdiff = Join-Path $repo "build/x64/bin/$Config/obl_imgdiff.exe"
if ($Record -and $CheckOnly) { throw 'Record and CheckOnly are mutually exclusive' }
if (-not $names.Count) { throw 'GUI fixture has no images' }
if (-not (Test-Path -LiteralPath $imgdiff)) { throw "image comparator not built: $imgdiff" }
if (-not $Record -and -not $CheckOnly) {
    foreach ($name in $names) {
        if (-not (Test-Path -LiteralPath (Join-Path $golden "$name.png"))) {
            throw "missing GUI golden: $name (record explicitly before verification)"
        }
    }
}

function Compare-Images([string]$Expected, [string]$Actual) {
    foreach ($name in $names) {
        $reference = Join-Path $Expected "$name.png"
        $candidate = Join-Path $Actual "$name.png"
        if (-not (Test-Path -LiteralPath $reference)) { throw "missing GUI golden: $reference" }
        if (-not (Test-Path -LiteralPath $candidate)) { throw "missing GUI capture: $candidate" }
        $result = & $imgdiff $candidate $reference
        if ($LASTEXITCODE) { throw "GUI golden mismatch ${name}: $result" }
    }
}

# The fixture's post-run oracle always compares committed images, regardless of run/golden or UpdateGolden.
# During recording, the outer invocation owns the two-run comparison instead.
if ($CheckOnly) {
    $result = & python (Join-Path $PSScriptRoot 'gui_image_check.py') $GameDir --pack $Pack 2>&1
    if ($LASTEXITCODE) { throw "$result" }
    $result
    return
}
$evidence = Join-Path $GameDir ('gui-golden-check/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence | Out-Null
$settings = [ordered]@{ OBL_GUI_ORACLE_RECORDING = $(if ($Record) { '1' } else { '0' }) }
if ($Pack -eq 'l0') { $settings['OBL_PACK'] = 'l0' }
$previous = @{}
try {
    foreach ($key in $settings.Keys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key)
        [Environment]::SetEnvironmentVariable($key, [string]$settings[$key])
    }
    $settings | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'environment.json') -Encoding utf8
    $runs = if ($Record) { 2 } else { 1 }
    for ($i = 1; $i -le $runs; ++$i) {
        $out = Join-Path $evidence "run$i"
        New-Item -ItemType Directory -Path $out | Out-Null
        & (Join-Path $PSScriptRoot 'autotest.ps1') $fixture -Config $Config -GameDir $GameDir
        Copy-Item -LiteralPath (Join-Path $GameDir 'openbeach.log') -Destination (Join-Path $out 'game.log')
        foreach ($name in $names) {
            Copy-Item -LiteralPath (Join-Path $GameDir "captures/$name.png") -Destination $out
        }
    }
    if ($Record) {
        Compare-Images (Join-Path $evidence 'run1') (Join-Path $evidence 'run2')
        $compact = Join-Path $evidence 'compact'
        New-Item -ItemType Directory -Path $compact | Out-Null
        foreach ($name in $names) {
            & python (Join-Path $PSScriptRoot 'compact_png.py') (Join-Path $evidence "run1/$name.png") (Join-Path $compact "$name.png")
            if ($LASTEXITCODE) { throw "PNG compression failed: $name" }
        }
        Compare-Images (Join-Path $evidence 'run1') $compact
        New-Item -ItemType Directory -Force -Path $golden | Out-Null
        foreach ($name in $names) {
            Copy-Item -LiteralPath (Join-Path $compact "$name.png") -Destination $golden
        }
        $settings | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $golden 'environment.json') -Encoding utf8
        "GUI golden: recorded $($names.Count) identical images"
    }
} finally {
    Stop-RunGame $repo $GameDir
    foreach ($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key, $previous[$key]) }
    Write-Host "GUI golden evidence: $evidence"
}
