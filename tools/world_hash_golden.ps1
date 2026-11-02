# One run of a fixture against its committed world-hash trace (tests/golden-hashes/<name>.tsv). -Record re-records the
# trace from the current build: two runs whose snapshots must match field by field (world_hash_compare.ps1), so a
# nondeterministic fixture never becomes a reference. Re-record only on purpose, with the reason in the commit body.
# -Tables picks the table set: private (default) = the imported-pack tables, which are never re-recorded; l0 = the
# fixture's L0 variant (golden_hash_s1 -> golden_hash_l0_s1, OBL_PACK=l0 in its header) and its own table
# (docs/build/ci-l0.md).
param(
    [Parameter(Mandatory)][string]$Script,
    [string]$Golden,
    [ValidateSet('Debug', 'RelWithDebInfo')][string]$Config = 'Debug',
    [string]$GameDir = '',
    [string]$OutDir,
    [double]$TimeoutScale = 1,
    [switch]$Record,
    [ValidateSet('private', 'l0')][string]$Tables = 'private',
    # Keep the default artifact directory after a passing run (also env OBL_KEEP_WORLD_HASH_ARTIFACTS=1).
    [switch]$KeepArtifacts
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'world_hash_golden_data.ps1')
if (-not $GameDir) { $GameDir = Join-Path $repo 'run' }
$GameDir = [IO.Path]::GetFullPath($GameDir)
$name = [IO.Path]::GetFileNameWithoutExtension($Script)
if ($Tables -eq 'l0' -and $name -notmatch '_l0_s\d+$') {
    if ($name -notmatch '_s\d+$') { throw "-Tables l0 needs a scenario fixture name (<family>_s<N>): $name" }
    $name = $name -replace '_s(\d+)$', '_l0_s$1'
    $Script = [IO.Path]::Combine([IO.Path]::GetDirectoryName($Script), "$name.txt")
}
if (-not $Golden) { $Golden = Join-Path $repo "tests/golden-hashes/$name.tsv" }
$Golden = [IO.Path]::GetFullPath($Golden)
$Script = Resolve-TestScript $Script $GameDir
# Only failures, recordings and an explicit -OutDir keep their artifacts (each run is 10-35 MB of snapshots).
$removeOnPass = -not $OutDir -and -not $Record -and -not $KeepArtifacts -and $env:OBL_KEEP_WORLD_HASH_ARTIFACTS -ne '1'
if (-not $OutDir) { $OutDir = Join-Path $GameDir ('world-hash-check/golden-' + $name + '-' + [guid]::NewGuid().ToString('N')) }
$root = [IO.Path]::GetFullPath($OutDir)
if (Test-Path -LiteralPath $root) { throw "output directory already exists: $root" }
# Fail before launching when the golden file is missing or invalid; never silently regenerate it.
$reference = if ($Record) { $null } else { @(Read-GoldenHashTrace $Golden) }
New-Item -ItemType Directory -Path $root | Out-Null

# One hidden run writing its checkpoints to $Directory; the game log is copied next to them as game.log.
function Invoke-HashRun([string]$Directory) {
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    $previous = $env:OBL_WORLD_HASH_DIR
    try {
        $env:OBL_WORLD_HASH_DIR = $Directory
        & (Join-Path $PSScriptRoot 'autotest.ps1') $Script -Config $Config -GameDir $GameDir -TimeoutScale $TimeoutScale
    } finally {
        $env:OBL_WORLD_HASH_DIR = $previous
        Stop-RunGame $repo $GameDir
        Copy-Item -LiteralPath (Join-Path $GameDir 'openbeach.log') -Destination (Join-Path $Directory 'game.log') -ErrorAction SilentlyContinue
    }
}

try {
    if ($Record) {
        Invoke-HashRun (Join-Path $root 'run1')
        Invoke-HashRun (Join-Path $root 'run2')
        & (Join-Path $PSScriptRoot 'world_hash_compare.ps1') -Reference (Join-Path $root 'run1') -Candidate (Join-Path $root 'run2')
        $rows = @(Read-GoldenHashLog (Join-Path $root 'run1/game.log'))
        New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($Golden)) | Out-Null
        Write-GoldenHashTrace $rows $Golden
        "world-hash golden: recorded $($rows.Count) checkpoints to $Golden"
        return
    }
    Invoke-HashRun $root
    $candidate = @(Read-GoldenHashLog (Join-Path $root 'game.log'))
    $summary = Compare-GoldenHashTrace $reference $candidate
    $summary | Set-Content -LiteralPath (Join-Path $root 'comparison.txt') -Encoding utf8
    $summary
} catch {
    $_.Exception.Message | Set-Content -LiteralPath (Join-Path $root 'comparison.txt') -Encoding utf8
    throw "$($_.Exception.Message); world-hash artifacts: $root"
} finally {
    Write-Host "world-hash artifacts: $root"
}
if ($removeOnPass) {
    Remove-Item -LiteralPath $root -Recurse -Force
    Write-Host "world-hash artifacts removed after the passing run (-KeepArtifacts keeps them)"
}
