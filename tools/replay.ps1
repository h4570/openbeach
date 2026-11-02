# Reference replays (docs/testing/replays.md).
#   ./tools/replay.ps1 -Record tests/replays/<name>.record.txt [-Config RelWithDebInfo]
#       runs the recording script twice; only when both runs wrote the same replay file and identical checkpoint snapshots
#       (world_hash_compare.ps1) does it replay the new file once more and copy it to tests/replays/<name>.replay.
#       Re-record only on purpose, with the reason in the commit body.
#   ./tools/replay.ps1 -Replay tests/replays/<name>.replay [-OutDir <dir>]
#       one replay run whose log and checkpoint snapshots stay in a directory: replay the same file with a good and a bad build
#       (or two configs) and compare the directories with world_hash_compare.ps1 for the first differing field.
#   -Pack l0 replays on the own L0 pack (OBL_PACK=l0); -Record takes it from the script's `# @env OBL_PACK=` header.
param(
    [string]$Record,
    [string]$Replay,
    [ValidateSet('Debug', 'RelWithDebInfo')][string]$Config = 'Debug',
    [string]$GameDir = '',
    [string]$OutDir,
    [string]$Pack = '',
    [double]$TimeoutScale = 1
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'common.ps1')
if ([bool]$Record -eq [bool]$Replay) { throw 'pass exactly one of -Record <script> or -Replay <file>' }
if (-not $GameDir) { $GameDir = Join-Path $repo 'run' }
$GameDir = [IO.Path]::GetFullPath($GameDir)

# One hidden run writing its checkpoints to $Directory; the game log is copied next to them as game.log (world_hash_compare.ps1).
function Invoke-ReplayRun([string]$Script, [string]$Directory) {
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

# A one-line test script replaying $File (the directive takes no quoting: paths without spaces).
function New-ReplayScript([string]$File, [string]$Name) {
    if ($File -match '\s') { throw "replay paths must not contain spaces: $File" }
    $end = Select-String -LiteralPath $File -Pattern '^end (\d+)$' | Select-Object -Last 1
    if (-not $end) { throw "no 'end <tick>' line in $File" }
    $timeout = 60 + [int][math]::Ceiling([int]$end.Matches[0].Groups[1].Value / 50)
    $dir = Join-Path $GameDir 'testscripts'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $path = Join-Path $dir "replay-$Name.txt"
    $lines = @("# @timeout $timeout", "replay $File", 'quit')
    if ($Pack) { $lines = @("# @env OBL_PACK=$Pack") + $lines }
    [IO.File]::WriteAllLines($path, [string[]]$lines)
    $path
}

if ($Replay) {
    $file = (Resolve-Path -LiteralPath $Replay).Path
    $name = [IO.Path]::GetFileNameWithoutExtension($file)
    if (-not $OutDir) { $OutDir = Join-Path $GameDir ("replay-check/$name-" + [guid]::NewGuid().ToString('N')) }
    $root = [IO.Path]::GetFullPath($OutDir)
    try {
        Invoke-ReplayRun (New-ReplayScript $file $name) $root
    } finally {
        Write-Host "replay artifacts: $root"
    }
    return
}

$script = (Resolve-Path -LiteralPath $Record).Path
if ([IO.Path]::GetFileName($script) -notmatch '^(.+)\.record\.txt$') { throw "expected tests/replays/<name>.record.txt: $script" }
$name = $Matches[1]
if (-not $Pack) {
    $header = Select-String -LiteralPath $script -Pattern '^\s*#\s*@env\s+OBL_PACK=(\w+)' | Select-Object -First 1
    if ($header) { $Pack = $header.Matches[0].Groups[1].Value }
}
$target = Join-Path ([IO.Path]::GetDirectoryName($script)) "$name.replay"
# The recording script writes `record replays/<name>.replay` into the game directory.
$output = Join-Path $GameDir "replays/$name.replay"
if (-not $OutDir) { $OutDir = Join-Path $GameDir ("replay-record/$name-" + [guid]::NewGuid().ToString('N')) }
$root = [IO.Path]::GetFullPath($OutDir)
try {
    foreach ($run in 'run1', 'run2') {
        Remove-Item -LiteralPath $output -ErrorAction SilentlyContinue
        Invoke-ReplayRun $script (Join-Path $root $run)
        if (-not (Test-Path -LiteralPath $output)) { throw "the recording script did not write $output (record replays/$name.replay ... record end)" }
        Copy-Item -LiteralPath $output -Destination (Join-Path $root "$run/$name.replay")
    }
    & (Join-Path $PSScriptRoot 'world_hash_compare.ps1') -Reference (Join-Path $root 'run1') -Candidate (Join-Path $root 'run2')
    $first = [IO.File]::ReadAllText((Join-Path $root "run1/$name.replay"))
    if ($first -cne [IO.File]::ReadAllText((Join-Path $root "run2/$name.replay"))) { throw 'the two recordings differ (commands or ticks)' }
    # The recorded run mixes normal and step frames; the replay runs step frames only and must still match every checkpoint.
    $candidate = Join-Path $root "run1/$name.replay"
    Invoke-ReplayRun (New-ReplayScript $candidate $name) (Join-Path $root 'replay')
    Copy-Item -LiteralPath $candidate -Destination $target
    "replay: recorded $target"
} finally {
    Write-Host "replay artifacts: $root"
}
