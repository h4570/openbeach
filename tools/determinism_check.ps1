# Runs a test script several times and compares the `autotest:` report lines (wall-clock values stripped) and the
# `shot` images pixel by pixel (obl_imgdiff, tolerance 0; docs/testing/golden-images.md).
# A deterministic script (seed + fixed-dt/step, see docs/testing/determinism.md) gives identical lines and shots on every run.
#   ./tools/determinism_check.ps1 tests/determinism.txt [-Runs 2]
# On a mismatch (or a failed run) everything needed to investigate is kept in run/determinism-check/<time>-<script>/:
# runN.log (raw `autotest:` lines of each run), runN.txt (normalized), diff.txt (all differing lines, not only the first
# 20 shown) and the shots of every run. The path is part of the thrown message, so it survives output filtering.
param(
    [Parameter(Mandatory)][string]$Script,
    [int]$Runs = 2
)
$ErrorActionPreference = 'Stop'
$autotest = Join-Path $PSScriptRoot 'autotest.ps1'
$imgdiff = Join-Path $PSScriptRoot "..\build\x64\bin\Debug\obl_imgdiff.exe"
$captures = Join-Path $PSScriptRoot '..\run\captures'
$keepRoot = Join-Path $PSScriptRoot '..\run\determinism-check'
$shotRoot = Join-Path ([IO.Path]::GetTempPath()) "obl_determinism_$PID"
$rawRuns = @{}

function Normalize([string]$line) {
    $line = $line -replace '^\[[^\]]*\]\[[^\]]*\]\s*', ''          # time stamp + thread id
    $line = $line -replace '\d+ ms\b', 'N ms'                        # "await ... met after 1063 ms"
    $line = $line -replace '\b(ms|steps-per-s|limiter-fps)=[\d.]+', '$1=N'
    $line = $line -replace '(new save slot|load-slot) \d+', '$1 N'        # every run writes a new save slot
    return $line
}

function Save-Evidence([string]$Reason) {
    $dir = Join-Path $keepRoot ("{0:yyyyMMdd-HHmmss}-{1}" -f (Get-Date), [IO.Path]::GetFileNameWithoutExtension($Script))
    New-Item -ItemType Directory -Force $dir | Out-Null
    foreach ($k in $rawRuns.Keys) {
        $rawRuns[$k] | Set-Content (Join-Path $dir "run$k.log")
        @($rawRuns[$k] | Where-Object { $_ -match 'autotest:' -and $_ -notmatch 'autotest: \d+ commands from' } | ForEach-Object { Normalize $_ }) |
            Set-Content (Join-Path $dir "run$k.txt")
    }
    $Reason | Set-Content (Join-Path $dir 'diff.txt')
    if (Test-Path $shotRoot) { Copy-Item $shotRoot (Join-Path $dir 'shots') -Recurse -ErrorAction SilentlyContinue }
    return $dir
}

$results = for ($i = 1; $i -le $Runs; $i++) {
    try {
        $out = & $autotest $Script 6>&1 | Out-String -Stream
    } catch {
        # autotest.ps1 throws on FAILED/crash/timeout: its message and the log tail are the evidence
        $rawRuns[$i] = @("run $i threw: $($_.Exception.Message)") + @(Get-Content (Join-Path $PSScriptRoot '..\run\openbeach.log') -ErrorAction SilentlyContinue)
        throw "run $i failed ($Script), evidence: $(Save-Evidence $_.Exception.Message)`n$($_.Exception.Message)"
    }
    $rawRuns[$i] = $out
    # autotest.ps1 clears run/captures at the start, so keep each run's shots aside
    $runShots = Join-Path $shotRoot "run$i"
    New-Item -ItemType Directory -Force $runShots | Out-Null
    Copy-Item (Join-Path $captures '*.png') $runShots -ErrorAction SilentlyContinue
    ,@($out | Where-Object { $_ -match 'autotest:' -and $_ -notmatch 'autotest: \d+ commands from' } | ForEach-Object { Normalize $_ })
}
$identical = $true
$report = [Collections.Generic.List[string]]::new()
for ($i = 1; $i -lt $Runs; $i++) {
    $diff = Compare-Object $results[0] $results[$i]
    if ($diff) {
        $identical = $false
        $report.Add("run 1 vs run $($i + 1): $($diff.Count) differing line(s)")
        foreach ($d in $diff) { $report.Add("$($d.SideIndicator) $($d.InputObject)") }
    }
}
$shots = @(Get-ChildItem (Join-Path $shotRoot 'run1') -Filter *.png -ErrorAction SilentlyContinue)
if ($shots -and -not (Test-Path $imgdiff)) { throw "obl_imgdiff not built: $imgdiff" }
for ($i = 2; $i -le $Runs; $i++) {
    foreach ($shot in $shots) {
        $other = Join-Path $shotRoot "run$i\$($shot.Name)"
        if (-not (Test-Path $other)) { $identical = $false; $report.Add("run $i is missing shot $($shot.Name)"); continue }
        $line = & $imgdiff $other $shot.FullName
        if ($LASTEXITCODE) { $identical = $false; $report.Add("run 1 vs run ${i}: $($shot.Name) $line") }
    }
}
if ($identical) {
    Remove-Item $shotRoot -Recurse -Force -ErrorAction SilentlyContinue
    "{0}: {1} runs, {2} report lines, {3} shots, IDENTICAL" -f (Split-Path $Script -Leaf), $Runs, $results[0].Count, $shots.Count
} else {
    $dir = Save-Evidence ($report -join "`n")
    Remove-Item $shotRoot -Recurse -Force -ErrorAction SilentlyContinue
    # The differences go into the exception text as well: a pipeline that filters or drops stdout still shows them.
    throw "not deterministic: $Script (evidence: $dir)`n$(($report | Select-Object -First 40) -join "`n")"
}
