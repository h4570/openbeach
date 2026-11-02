# One game slot of a parallel runtests.ps1 -Jobs N run (docs/testing/test-runner.md); not meant to be started by hand.
# Takes tests from the shared list in order, claiming each with an exclusive claim file (so every test runs once, in
# whichever slot is free), runs it in run-slots/<Slot>/ and writes <index>-<k>.json with the outcome to $OutDir (k > 0: the further
# scripts of a headless batch line).
param(
    [Parameter(Mandatory)][int]$Slot,
    [Parameter(Mandatory)][string]$ListFile,    # one test script name (or `batch:` line) per line, in scheduling order
    [Parameter(Mandatory)][string]$OutDir,
    [ValidateSet('Auto', 'Debug', 'RelWithDebInfo')][string]$Config = 'Auto',
    [switch]$UpdateGolden,
    [double]$TimeoutScale = 1,
    [ValidateSet('Auto', 'Hidden')][string]$Mode = 'Auto'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'testrun.ps1')
$slotDir = Initialize-GameSlot $repo $Slot
$tests = @(Get-Content $ListFile)
for ($i = 0; $i -lt $tests.Count; ++$i) {
    try { $claim = [IO.File]::Open((Join-Path $OutDir "$i.claim"), 'CreateNew', 'Write', 'None'); $claim.Dispose() } catch { continue }
    Reset-GameSlot $repo $Slot
    # `batch:a.txt|b.txt` (Group-HeadlessBatches): several @headless scripts in one headless process, one result each
    $results = if ($tests[$i].StartsWith('batch:')) {
        @(Invoke-HeadlessBatch $repo ($tests[$i].Substring(6) -split '\|') $Config $slotDir $TimeoutScale)
    } else {
        @(Invoke-GameTest $repo $tests[$i] $Config $slotDir $UpdateGolden.IsPresent $TimeoutScale $Mode)
    }
    for ($k = 0; $k -lt $results.Count; ++$k) {
        $results[$k] | Add-Member -NotePropertyName Slot -NotePropertyValue $Slot
        # written under a temporary name and renamed, so the reader never sees a half-written file
        $tmp = Join-Path $OutDir "$i-$k.tmp"
        $results[$k] | ConvertTo-Json -Depth 3 | Set-Content $tmp -Encoding utf8
        Move-Item $tmp (Join-Path $OutDir "$i-$k.json")
    }
}
