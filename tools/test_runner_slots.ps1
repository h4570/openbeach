# Checks of the parallel test runner (docs/testing/test-runner.md): game slots are isolated copies that share the game data,
# and `runtests.ps1 -Jobs N` runs tests in the slots, never in run/, and lists them in manifest order. Not part of the
# manifest (it runs runtests.ps1 itself); run it after changing runtests*.ps1, testrun.ps1 or autotest.ps1.
param(
    [ValidateSet('Debug', 'RelWithDebInfo')][string]$Config = 'Debug'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'testrun.ps1')
$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Ok, [string]$What) {
    Write-Host ("{0} {1}" -f $(if ($Ok) { 'PASS' } else { 'FAIL' }), $What)
    if (-not $Ok) { $failures.Add($What) }
}

if (-not (Test-Path (Join-Path $repo 'run\BeachLife.exe'))) { throw 'run/ has no imported game data' }
$runIni = Join-Path $repo 'run\BeachLife.ini'
$runLog = Join-Path $repo 'run\openbeach.log'
$iniBefore = (Get-FileHash $runIni).Hash
$logBefore = (Get-Item $runLog -ErrorAction SilentlyContinue).LastWriteTimeUtc

# --- slot layout ---
$slot = Initialize-GameSlot $repo 1
$exeLinks = @(fsutil hardlink list (Join-Path $slot 'BeachLife.exe') | Where-Object { $_ })
Check ($exeLinks.Count -ge 2) 'slot BeachLife.exe is a hardlink of run/BeachLife.exe (game data is shared, not copied)'
$sprites = Get-ChildItem (Join-Path $repo 'run\Data') -Recurse -File | Select-Object -First 1
$slotSprite = Join-Path $slot ($sprites.FullName.Substring((Join-Path $repo 'run\').Length))
Check ((Test-Path $slotSprite) -and @(fsutil hardlink list $slotSprite | Where-Object { $_ }).Count -ge 2) 'slot Data files are hardlinks'
Check (@(fsutil hardlink list (Join-Path $slot 'BeachLife.ini') | Where-Object { $_ }).Count -eq 1) 'slot BeachLife.ini is its own file'
Check ((Get-Item (Join-Path $slot 'golden')).LinkType -eq 'Junction') 'slot golden/ is a junction to run/golden'
Check ((Get-Item (Join-Path $slot 'Data\Temp')).LinkType -ne 'Junction' -and -not (Get-Item (Join-Path $slot 'Data\Temp')).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) 'slot Data/Temp is a real directory'
Set-Content (Join-Path $slot 'BeachLife.ini') 'garbage'
Set-Content (Join-Path $slot 'SaveGames\save0.dat') 'leftover'
Reset-GameSlot $repo 1
Check ((Get-FileHash (Join-Path $slot 'BeachLife.ini')).Hash -eq $iniBefore) 'Reset-GameSlot restores the ini from run/'
Check (@(Get-ChildItem (Join-Path $slot 'SaveGames') -Recurse -File).Count -eq 0) 'Reset-GameSlot empties SaveGames (whatever run/SaveGames holds)'

# --- autotest.ps1 hygiene (docs/testing/test-runner.md): ini baseline, whole-ini restore, saves the run wrote are removed ---
$slotIni = Join-Path $slot 'BeachLife.ini'
$slotSaves = Join-Path $slot 'SaveGames'
$polluted = (Get-Content $slotIni -Raw) -replace 'ResWidth=\d+', 'ResWidth=1280' -replace 'ResHeight=\d+', 'ResHeight=720'
$polluted = ($polluted -replace '(?ms)^\[Dialog\].*?(?=^\[|\z)', '') + "[Dialog]`r`nDialogCount=21`r`nDialogXPos19=1000`r`nDialogYPos19=700`r`n"
Set-Content $slotIni $polluted -NoNewline
Set-Content (Join-Path $slotSaves 'keep.dat') 'pre-existing'
$pollutedHash = (Get-FileHash $slotIni).Hash
$hygiene = @()
try {
    $hygiene = @(& (Join-Path $PSScriptRoot 'autotest.ps1') (Join-Path $repo 'tests\runner_hygiene.txt') -Config $Config -GameDir $slot -TimeoutScale 2 *>&1 | ForEach-Object { "$_" })
} catch { $hygiene = @("FAILED: $($_.Exception.Message)") }
Check (-not [bool]($hygiene -cmatch 'FAILED|autotest: aborted')) 'polluted ini (1280x720, DialogCount=21 with one entry): game starts at 800x600 and writes a save'
Check ([bool]($hygiene -match 'new save slot 0')) 'the fixture run wrote save0.dat (so the cleanup check below is not vacuous)'
Check ((Get-FileHash $slotIni).Hash -eq $pollutedHash) 'autotest.ps1 puts the whole ini back after applying the test baseline'
Check ((Test-Path (Join-Path $slotSaves 'keep.dat')) -and @(Get-ChildItem $slotSaves -Recurse -File).Count -eq 1) 'autotest.ps1 removes the saves the run created and keeps the pre-existing ones'
Reset-GameSlot $repo 1
Check ((Get-RunGame $repo $slot).Count -eq 0) 'no game runs in the slot'

# --- parallel run ---
$filter = 'menu.txt', 'sandbox.txt', 'power.txt', 'speed.txt', 'savegame_managers.txt', 'construction_load.txt'
$manifest = @(Get-Content (Join-Path $repo 'tests\manifest.txt') | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { ($_ -split '\s+')[0] })
$expected = @($manifest | Where-Object { $filter -contains $_ })
$output = & (Join-Path $PSScriptRoot 'runtests.ps1') -Filter $filter -Config $Config -NoBuild -Jobs 3 *>&1 | ForEach-Object { "$_" -split "`r?`n" }
$exit = $LASTEXITCODE
$table = @($output | Where-Object { $_ -match '^(\S+\.txt)\s+(Debug|Rel)(?:/HB?)?\s+(PASS|FAIL|SKIP)\s' } | ForEach-Object { $Matches[1] })
# the live lines come first (completion order), the table after them (manifest order): take the last block
$tableOrder = @($table | Select-Object -Last $expected.Count)
Check ($exit -eq 0) 'runtests.ps1 -Jobs 3 exits 0'
Check (($tableOrder -join ',') -eq ($expected -join ',')) 'result table is in manifest order'
Check ([bool]($output -match 'in 3 parallel slots')) 'the run used 3 slots'
$slotLogs = @(1..3 | ForEach-Object { Join-Path (Get-SlotDir $repo $_) 'openbeach.log' } | Where-Object { Test-Path $_ })
Check ($slotLogs.Count -ge 2) 'at least two slots ran a game'
$inSlot = $slotLogs | ForEach-Object { [bool](Select-String -Path $_ -SimpleMatch "game directory $([IO.Path]::GetDirectoryName($_))\, host " -Quiet) }
Check (@($inSlot | Where-Object { -not $_ }).Count -eq 0) 'slot hosts report their own game directory'
Check (((Get-Item $runLog -ErrorAction SilentlyContinue).LastWriteTimeUtc) -eq $logBefore) 'run/ was not used by the parallel run (log untouched)'
Check ((Get-FileHash $runIni).Hash -eq $iniBefore) 'run/BeachLife.ini unchanged'
Check ((Get-RunGame $repo).Count -eq 0 -and (1..3 | ForEach-Object { (Get-RunGame $repo (Get-SlotDir $repo $_)).Count } | Measure-Object -Sum).Sum -eq 0) 'no game left running'

if ($failures) { throw "$($failures.Count) check(s) failed" }
Write-Host 'test_runner_slots: all checks passed'
