# Test-run helpers shared by runtests.ps1 and runtests_worker.ps1 (dot-source after common.ps1).
#
# One game test = tools/autotest.ps1 in a game directory. The default directory is run/. Parallel runs (runtests.ps1
# -Jobs N, docs/testing/test-runner.md) use N extra directories run-slots/<n>/: a slot has its own copies of everything the game
# and the tests write (ini files, SaveGames, progress.json, log, captures, testlogs, Data/Temp) and NTFS hardlinks to the
# read-only game data of run/, so a slot costs a few KB instead of 1.5 GB. `golden` is a junction to run/golden.

# Top-level entries of run/ that are not game data (test output, debris of other tools).
$script:SlotSkipDirs = @('captures', 'testlogs', 'golden', 'SaveGames', 'world-hash-check', 'testscripts')
# Top-level files the game or the launcher need next to the exe; everything else in run/ is debris (logs, notes).
$script:SlotLinkExtensions = @('.exe', '.dll', '.htm', '.inf')
# Files the game rewrites (and tests pin values in): copied, and reset before every test (SaveGames/ is emptied instead).
$script:SlotStateFiles = @('BeachLife.ini', 'openbeach.ini')

function Get-SlotDir([string]$Repo, [int]$Slot) { Join-Path $Repo "run-slots\$Slot" }

# Hardlinks $Source at $Target unless an equal file (same size and write time) is already there.
function Sync-FileLink([string]$Source, [string]$Target) {
    $src = Get-Item -LiteralPath $Source
    $dst = Get-Item -LiteralPath $Target -ErrorAction SilentlyContinue
    if ($dst -and $dst.Length -eq $src.Length -and $dst.LastWriteTimeUtc -eq $src.LastWriteTimeUtc) { return }
    if ($dst) { Remove-Item -LiteralPath $Target -Force }
    $null = New-Item -ItemType HardLink -Path $Target -Target $Source
}

function Sync-DirLinks([string]$Source, [string]$Target, [string[]]$SkipNames = @()) {
    New-Item -ItemType Directory -Force $Target | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        if ($SkipNames -contains $item.Name) { continue }
        $to = Join-Path $Target $item.Name
        if ($item.PSIsContainer) { Sync-DirLinks $item.FullName $to } else { Sync-FileLink $item.FullName $to }
    }
}

# Creates or refreshes run-slots/<n>/ from run/ and returns its path. Cheap when nothing changed (a stat per file).
function Initialize-GameSlot([string]$Repo, [int]$Slot) {
    $run = Join-Path $Repo 'run'
    $dir = Get-SlotDir $Repo $Slot
    New-Item -ItemType Directory -Force $dir | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $run -Force) {
        $to = Join-Path $dir $item.Name
        if ($item.PSIsContainer) {
            if ($script:SlotSkipDirs -contains $item.Name) { continue }
            # Data/Temp is written by the game (map scratch file): a real directory, never linked.
            if ($item.Name -eq 'Data') { Sync-DirLinks $item.FullName $to @('Temp'); New-Item -ItemType Directory -Force (Join-Path $to 'Temp') | Out-Null }
            else { Sync-DirLinks $item.FullName $to }
        } elseif ($script:SlotLinkExtensions -contains $item.Extension.ToLowerInvariant()) {
            Sync-FileLink $item.FullName $to
        }
    }
    foreach ($name in 'captures', 'testlogs') { New-Item -ItemType Directory -Force (Join-Path $dir $name) | Out-Null }
    $golden = Join-Path $dir 'golden'
    if (-not (Test-Path $golden)) {
        # A tree without recorded references has no run/golden yet.
        New-Item -ItemType Directory -Force (Join-Path $run 'golden') | Out-Null
        New-Item -ItemType Junction -Path $golden -Target (Join-Path $run 'golden') | Out-Null
    }
    Reset-GameSlot $Repo $Slot
    $dir
}

# Puts the files a previous test may have changed back to the state of run/ (so a test does not depend on which tests
# ran before it in the same slot) and clears its log.
function Reset-GameSlot([string]$Repo, [int]$Slot) {
    $run = Join-Path $Repo 'run'
    $dir = Get-SlotDir $Repo $Slot
    foreach ($name in $script:SlotStateFiles) {
        $from = Join-Path $run $name; $to = Join-Path $dir $name
        if (Test-Path $from) { Copy-Item -LiteralPath $from -Destination $to -Force } else { Remove-Item -LiteralPath $to -Force -ErrorAction SilentlyContinue }
    }
    # An absent progress file makes Game_LoadScenarioProgress use its fresh-install baseline.
    foreach ($name in 'progress.json', 'sp.bin') { Remove-Item -LiteralPath (Join-Path $dir $name) -Force -ErrorAction SilentlyContinue }
    # Empty like a fresh install: copying run/SaveGames carried over 279 test saves once, and ui.save-game-write
    # crashes on a full list. No test depends on a pre-existing save (they write their own).
    $saves = Join-Path $dir 'SaveGames'
    if (Test-Path $saves) { Remove-Item -LiteralPath $saves -Recurse -Force }
    New-Item -ItemType Directory $saves | Out-Null
    Remove-Item -LiteralPath (Join-Path $dir 'openbeach.log') -Force -ErrorAction SilentlyContinue
}

# An explicit runner configuration wins over the fixture's @config header and extension defaults.
# Virtual .golden entries use the header of their matching .txt fixture; .ctest has no script header.
function Resolve-TestConfig([string]$Test, [string]$Config) {
    if ($Config -ne 'Auto') { return $Config }
    if (-not $Test.EndsWith('.ctest')) {
        $fixture = [IO.Path]::ChangeExtension((Join-Path $PSScriptRoot "..\tests\$Test"), '.txt')
        # Per-scenario entries without a file of their own take the header of their <family>_sN.txt template (Resolve-TestScript).
        if (-not (Test-Path -LiteralPath $fixture) -and $fixture -match '^(.+_s)\d+\.txt$') { $fixture = "$($Matches[1])N.txt" }
        if (-not (Test-Path -LiteralPath $fixture)) { $fixture = $null }
        foreach ($line in $(if ($fixture) { Get-Content -LiteralPath $fixture } else { @() })) {
            if ($line -match '^\s*#\s*@config\s+(rel|debug)\s*$') {
                return $(if ($Matches[1] -eq 'rel') { 'RelWithDebInfo' } else { 'Debug' })
            }
        }
    }
    # ctest entries other than unit (Debug: the STL's checks) run on RelWithDebInfo: the data and save-corpus cases and the
    # headless hosts are several times slower in Debug, and CI builds obl-sim with -O2 anyway.
    if ($Test.EndsWith('.golden') -or ($Test.EndsWith('.ctest') -and $Test -ne 'unit.ctest')) { 'RelWithDebInfo' } else { 'Debug' }
}

# Manifest entries tagged @headless run in the headless host (autotest.ps1 -Headless, docs/architecture/headless/index.md).
function Test-HeadlessEntry([string]$Repo, [string]$Test) {
    if ($null -eq $script:headlessEntries) {
        $script:headlessEntries = @(Get-Content (Join-Path $Repo 'tests\manifest.txt') | ForEach-Object { $_.Trim() } |
            Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { $parts = $_ -split '\s+'; if ($parts -contains '@headless') { $parts[0] } })
    }
    $script:headlessEntries -contains $Test
}

# Runs one manifest script through autotest.ps1 in $GameDir ('' = run/) and returns the outcome. Never throws.
# $Mode Auto: @headless entries headless, the rest in the hidden game; Hidden: every entry in the hidden game (time comparison).
function Invoke-GameTest([string]$Repo, [string]$Test, [string]$Config, [string]$GameDir, [bool]$UpdateGolden, [double]$TimeoutScale = 1, [string]$Mode = 'Auto') {
    $Config = Resolve-TestConfig $Test $Config
    $headless = $Mode -eq 'Auto' -and -not $Test.EndsWith('.golden') -and (Test-HeadlessEntry $Repo $Test)
    if (-not $GameDir) { $GameDir = Join-Path $Repo 'run' }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $ok = $true; $detail = ''; $output = @()
    try {
        if ($Test.EndsWith('.golden')) {
            $fixture = [IO.Path]::ChangeExtension((Join-Path $Repo "tests\$Test"), '.txt')
            & (Join-Path $Repo 'tools\world_hash_golden.ps1') $fixture -Config $Config -GameDir $GameDir -TimeoutScale $TimeoutScale *>&1 |
                ForEach-Object { $output += "$_" }
        } else {
            $output = & (Join-Path $Repo 'tools\autotest.ps1') (Join-Path $Repo "tests\$Test") -Config $Config -GameDir $GameDir -TimeoutScale $TimeoutScale -UpdateGolden:$UpdateGolden -Headless:$headless 2>&1 | ForEach-Object { "$_" }
        }
    } catch {
        $ok = $false; $detail = $_.Exception.Message
    } finally {
        Stop-RunGame $Repo $GameDir
    }
    $watch.Stop()
    # keep every test's log (run/ is git-ignored) to compare reports before/after editing a test
    $logs = Join-Path $Repo 'run\testlogs'
    New-Item -ItemType Directory -Force $logs | Out-Null
    $logName = [IO.Path]::GetFileNameWithoutExtension($Test)
    if ($Test.EndsWith('.golden')) { $logName += '.golden' }
    Copy-Item (Join-Path $GameDir 'openbeach.log') (Join-Path $logs "$logName.log") -ErrorAction SilentlyContinue
    $summary = if ($Test.EndsWith('.golden')) {
        $output | Where-Object { $_ -match '^world-hash(?: golden)?: .*IDENTICAL' } | Select-Object -Last 1
    } else {
        ($output | Where-Object { $_ -match 'summary passed=' } | Select-Object -Last 1) -replace '^.*summary ', ''
    }
    [pscustomobject]@{
        Test    = $Test
        Config  = $(if ($Config -eq 'RelWithDebInfo') { 'Rel' } else { $Config }) + $(if ($headless) { '/H' } else { '' })
        Result  = $(if ($ok) { 'PASS' } else { 'FAIL' })
        Seconds = [math]::Round($watch.Elapsed.TotalSeconds, 1)
        Summary = $(if ($ok) { $summary } else { $detail })
        Output  = @($output)
    }
}

# Headers that need a process of their own (environment, ini, exit code, oracles on the game directory, log patterns, and
# `@own-process` for scripts that count what only the process start does): such @headless entries run alone, the rest of a
# slot's @headless entries share one process (Invoke-HeadlessBatch).
$script:SoloHeaders = '^\s*#\s*@(env|gameini|expect-exit|check|expect-log|audio|own-process)\b'
function Test-BatchableEntry([string]$Repo, [string]$Test, [string]$Mode) {
    if ($Mode -ne 'Auto' -or -not $Test.EndsWith('.txt') -or -not (Test-HeadlessEntry $Repo $Test)) { return $false }
    $path = Join-Path $Repo "tests\$Test"
    (Test-Path -LiteralPath $path) -and -not (Get-Content -LiteralPath $path | Where-Object { $_ -match $script:SoloHeaders })
}

# Groups the batchable entries of $Tests (in their order) into `batch:a.txt|b.txt|...` lines of about $Seconds each (durations
# of the last run; unknown ones count 4 s) and at most $MaxScripts scripts; every other entry stays a line of its own. A batch
# holds one configuration (`# @config`): the process runs the first script's (Invoke-HeadlessBatch).
function Group-HeadlessBatches([string]$Repo, [string[]]$Tests, [hashtable]$Durations, [string]$Mode, [double]$Seconds, [int]$MaxScripts = 12) {
    $pending = [ordered]@{} # configuration -> @{ Tests; Seconds }
    $lines = [Collections.Generic.List[string]]::new()
    $flush = { param($batch) if ($batch.Tests.Count -eq 1) { $lines.Add($batch.Tests[0]) } elseif ($batch.Tests.Count) { $lines.Add('batch:' + ($batch.Tests -join '|')) } }
    foreach ($test in $Tests) {
        if (-not (Test-BatchableEntry $Repo $test $Mode)) { $lines.Add($test); continue }
        $config = Resolve-TestConfig $test 'Auto'
        if (-not $pending.Contains($config)) { $pending[$config] = @{ Tests = [Collections.Generic.List[string]]::new(); Seconds = 0.0 } }
        $batch = $pending[$config]
        $batch.Tests.Add($test)
        $batch.Seconds += $(if ($Durations.ContainsKey($test)) { $Durations[$test] } else { 4.0 })
        if ($batch.Seconds -ge $Seconds -or $batch.Tests.Count -ge $MaxScripts) { $lines.Add('batch:' + ($batch.Tests -join '|')); $pending.Remove($config) }
    }
    foreach ($batch in $pending.Values) { & $flush $batch }
    $lines
}

# Runs several @headless scripts in one headless process (`--headless <list>`, docs/architecture/headless/scripts.md "Script lists") and
# returns one result per script, like Invoke-GameTest. The game log is split at each `obl-sim: script` line; a script whose section
# has no `obl-sim: end` (the process crashed, hung or timed out on it or before it) runs again alone. Never throws.
function Invoke-HeadlessBatch([string]$Repo, [string[]]$Tests, [string]$Config, [string]$GameDir, [double]$TimeoutScale = 1) {
    if (-not $GameDir) { $GameDir = Join-Path $Repo 'run' }
    $Config = Resolve-TestConfig $Tests[0] $Config
    $paths = @{}
    $timeout = 0
    foreach ($test in $Tests) {
        $path = [IO.Path]::GetFullPath((Resolve-TestScript (Join-Path $Repo "tests\$test") $GameDir))
        $paths[$test] = $path
        $header = Get-Content -LiteralPath $path | Where-Object { $_ -match '^\s*#\s*@timeout\s+(\d+)' } | Select-Object -First 1
        $timeout += $(if ($header -and $header -match '(\d+)\s*$') { [int]$Matches[1] } else { 120 })
    }
    $logs = Join-Path $Repo 'run\testlogs'
    New-Item -ItemType Directory -Force $logs, (Join-Path $GameDir 'testlogs') | Out-Null
    $list = Join-Path $GameDir 'testlogs\batch.list'
    Set-Content -LiteralPath $list -Value @($Tests | ForEach-Object { $paths[$_] }) -Encoding utf8
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = @()
    try {
        # autotest.ps1 throws when any script failed; the per-script verdicts come from the log below.
        $output = & (Join-Path $Repo 'tools\autotest.ps1') $list -Config $Config -GameDir $GameDir -TimeoutSec $timeout -TimeoutScale $TimeoutScale -Headless 2>&1 |
            ForEach-Object { "$_" }
    } catch {
        $output += $_.Exception.Message
    } finally {
        Stop-RunGame $Repo $GameDir
    }
    $watch.Stop()
    $sections = @{}
    $current = $null
    foreach ($line in @(Get-Content -LiteralPath (Join-Path $GameDir 'openbeach.log') -ErrorAction SilentlyContinue)) {
        if ($line -match 'obl-sim: script (.+)$') {
            $current = [IO.Path]::GetFullPath($Matches[1].Trim())
            $sections[$current] = [Collections.Generic.List[string]]::new()
        }
        if ($current) { $sections[$current].Add($line) }
    }
    foreach ($test in $Tests) {
        $section = $sections[$paths[$test]]
        if (-not $section -or -not ($section -match 'obl-sim: end ')) {
            Write-Host "$test did not finish in its headless batch - running it alone"
            Invoke-GameTest $Repo $test $Config $GameDir $false $TimeoutScale 'Auto'
            continue
        }
        Set-Content -LiteralPath (Join-Path $logs "$([IO.Path]::GetFileNameWithoutExtension($test)).log") -Value $section -Encoding utf8
        $failed = @($section | Where-Object { $_ -cmatch 'autotest:.*\bFAILED\b|autotest: aborted|FatalError:' })
        $completed = [bool]($section -match 'autotest: completed')
        $summary = ($section | Where-Object { $_ -match 'autotest: summary passed=' } | Select-Object -Last 1) -replace '^.*summary ', ''
        $ms = ($section | Where-Object { $_ -match 'obl-sim: end .* ms=(\d+)' } | Select-Object -Last 1) -replace '^.* ms=(\d+).*$', '$1'
        $detail = if ($failed) { "test reported $($failed.Count) failure line(s)" } elseif (-not $completed) { 'test did not reach quit' } else { '' }
        [pscustomobject]@{
            Test    = $test
            Config  = $(if ($Config -eq 'RelWithDebInfo') { 'Rel' } else { $Config }) + '/HB'
            Result  = $(if ($detail) { 'FAIL' } else { 'PASS' })
            Seconds = [math]::Round([double]$ms / 1000, 1)
            Summary = $(if ($detail) { $detail } else { $summary })
            Output  = @($section | Where-Object { $_ -match 'autotest:|obl-sim:|FatalError' })
        }
    }
}
