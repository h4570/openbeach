# Exercise the committed-image oracle without starting the game.
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$checker = Join-Path $repo 'tools/gui_image_golden.ps1'
$temp = Join-Path $repo ('run/tmp/gui-oracle-test-' + [guid]::NewGuid().ToString('N'))
$captures = Join-Path $temp 'captures'
$previous = $env:OBL_GUI_ORACLE_RECORDING
$env:OBL_GUI_ORACLE_RECORDING = '0'
$passed = 0
function Must-Fail([string]$Pattern) {
    try {
        & $checker -CheckOnly -Pack $pack -GameDir $temp
    } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        ++$script:passed
        return
    }
    throw "oracle did not fail: $Pattern"
}
try {
    New-Item -ItemType Directory -Path $captures | Out-Null
    foreach ($set in @(@('private', 'm6-gui', 'm6_gui_golden.txt'), @('l0', 'm6-gui-l0', 'm6_gui_golden_l0.txt'))) {
        $pack, $dir, $fixture = $set
        $golden = Join-Path $repo "tests/golden-images/$dir"
        Get-ChildItem -LiteralPath $captures | Remove-Item
        $images = @(Get-ChildItem -LiteralPath $golden -Filter *.png)
        $shots = @(Get-Content -LiteralPath (Join-Path $repo "tests/$fixture") | Where-Object { $_ -match '^shot \S+\s*$' }).Count
        if ($images.Count -ne $shots) { throw "expected $shots committed GUI images (one per shot of $fixture), found $($images.Count)" }
        $images | Copy-Item -Destination $captures
        & $checker -CheckOnly -Pack $pack -GameDir $temp
        ++$passed
        $first = Join-Path $captures $images[0].Name
        Remove-Item -LiteralPath $first
        Must-Fail 'missing GUI capture'
        Copy-Item -LiteralPath $images[1].FullName -Destination $first
        Must-Fail 'GUI golden mismatch'
        Copy-Item -LiteralPath $images[0].FullName -Destination $first
        [IO.File]::WriteAllText($first, 'invalid PNG')
        Must-Fail 'GUI golden mismatch'
    }
    "GUI oracle tool: $passed checks PASS"
} finally {
    $env:OBL_GUI_ORACLE_RECORDING = $previous
    # Only remove this invocation's GUID directory beneath this checkout's ignored run/tmp.
    $allowed = [IO.Path]::GetFullPath((Join-Path $repo 'run/tmp')) + [IO.Path]::DirectorySeparatorChar
    if (-not [IO.Path]::GetFullPath($temp).StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw "temporary directory outside run/tmp: $temp"
    }
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
