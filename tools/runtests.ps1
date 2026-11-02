# Runs every test of tests/manifest.txt (or the ones matching -Filter) hidden through autotest.ps1 and prints a
# PASS/FAIL table with the times. Exit code 1 when anything failed. Use -Filter for affected tests before a commit;
# reserve the full suite for changes with broad impact.
#   ./tools/runtests.ps1                    all tests (rebuilds first when the DLL is older than the sources)
#   ./tools/runtests.ps1 -Filter power*     only tests whose file name matches
#   ./tools/runtests.ps1 -NoBuild           never build; refuses when a needed configuration is older than the sources
#   ./tools/runtests.ps1 -Config Debug      every entry on Debug (default Auto: @config header, then .golden on RelWithDebInfo, the rest Debug;
#                                           only the configurations the selected entries use are built)
#   ./tools/runtests.ps1 -Verbose           print the full log excerpt of failed tests (default: last lines)
#   ./tools/runtests.ps1 -UpdateGolden      record run/golden/*.png from this build instead of comparing (expect-shot)
#   ./tools/runtests.ps1 -Jobs 6            run the game tests in 6 isolated game directories at once (run-slots/<n>/,
#                                           docs/testing/test-runner.md); results are listed in manifest order. Default: see -Jobs.
#   ./tools/runtests.ps1 -CleanSlots        delete run-slots/ (no tests run)
#   ./tools/runtests.ps1 -List              print the selected manifest entries (with -Filter/-Full) and exit
#   ./tools/runtests.ps1 -Hidden            run the @headless entries in the hidden game instead (time and behavior comparison)
#   ./tools/runtests.ps1 -Pack public       only the manifest lines tagged @l0 (own L0 pack or no game data; the public set);
#                                           -Pack private (default) runs every entry, the imported-pack checks included
#   ./tools/runtests.ps1 -Full              also the manifest lines tagged @full (golden hashes of more scenarios); it also overrides
#                                           a local refusal of the whole suite (tools/suite_policy.ps1)
# Game tests run in the standalone host OpenBeach.exe, built only from our own code.
# Manifest lines ending in .ctest are ctest tests without the game (docs/testing/unit-tests.md): unit.ctest runs
# `ctest -R ^unit$`. A ctest skip (run/ not prepared) is reported as SKIP and does not fail the run.
# Virtual .golden entries run the matching .txt once against tests/golden-hashes/<name>.tsv.
# Lines tagged @headless run in the headless host (autotest.ps1 -Headless; Config column `Debug/H`, docs/architecture/headless/index.md); those
# without a header that needs its own process share one (`Debug/HB`, Invoke-HeadlessBatch in testrun.ps1). Lines tagged @hidden
# run in the hidden game like untagged ones: they were checked and only test drawing, window, input or audio.
param(
    [string[]]$Filter = @('*'),
    # Auto: fixture @config rel|debug, then .golden on RelWithDebInfo, otherwise Debug; Debug or RelWithDebInfo
    # forces one configuration for every entry (a full Debug run: -Config Debug).
    [ValidateSet('Auto', 'Debug', 'RelWithDebInfo')][string]$Config = 'Auto',
    [switch]$NoBuild,
    [switch]$Verbose,
    [switch]$UpdateGolden,
    # Parallel game slots. 0 (default) = automatic: 1 for up to 3 selected game tests (a slot is not worth starting),
    # otherwise half the logical cores (at most 8: the 10-core dev machine stops scaling there); 1 = sequential in run/, like before.
    [int]$Jobs = 0,
    [switch]$CleanSlots,
    [switch]$List,
    [switch]$Full,
    # Parallel runs multiply every test's wall-clock limit (# @timeout) by this: the games share the CPU and the disk.
    [double]$TimeoutScale = 2,
    # @headless entries in the hidden game instead of the headless host (both run the same script).
    [switch]$Hidden,
    # Only the @headless entries (of the -Filter selection): the per-change check without a game window.
    [switch]$Headless,
    # public: only @l0 entries (no imported data, the future public CI); private: every entry (docs/build/ci-l0.md).
    [ValidateSet('public', 'private')][string]$Pack = 'private'
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'testrun.ps1')
if ($CleanSlots) {
    # Remove-Item deletes the golden junction of a slot without following it into run/golden.
    Remove-Item (Join-Path $repo 'run-slots') -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host 'run-slots/ removed'
    return
}
function Test-NameMatch([string]$name, [string[]]$patterns) {
    @($patterns | Where-Object { $name -like $_ -or $name -like "$_.txt" -or $name -like "$_.ctest" -or $name -like "$_.golden" }).Count -gt 0
}
# A manifest line tagged `@full` (golden hashes of the other scenarios) is skipped unless -Full is given or a filter
# without wildcards names it: the default run keeps three scenarios per family (docs/testing/test-runner.md).
$entries = Get-Content (Join-Path $repo 'tests\manifest.txt') |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') } |
    ForEach-Object { $parts = $_ -split '\s+'; [pscustomobject]@{ Name = $parts[0]; FullOnly = $parts -contains '@full'
        Headless = $parts -contains '@headless'; HiddenOnly = $parts -contains '@hidden'; L0 = $parts -contains '@l0' } }
$both = @($entries | Where-Object { $_.Headless -and $_.HiddenOnly } | ForEach-Object Name)
if ($both) { throw "manifest entries tagged both @headless and @hidden: $($both -join ', ')" }
$matched = @($entries | Where-Object { (Test-NameMatch $_.Name $Filter) -and (-not $Headless -or $_.Headless) -and ($Pack -eq 'private' -or $_.L0) })
if (-not $matched) { throw "no test matches: $Filter$(if ($Headless) { ' (-Headless: @headless entries only)' })$(if ($Pack -eq 'public') { ' (-Pack public: @l0 entries only)' })" }
$named = @($Filter | Where-Object { -not [Management.Automation.WildcardPattern]::ContainsWildcardCharacters($_) })
$byLevel = @($matched | Where-Object { $Full -or -not $_.FullOnly -or (Test-NameMatch $_.Name $named) })
$skippedFull = $matched.Count - $byLevel.Count
$tests = @($byLevel | ForEach-Object Name)
$mode = if ($Hidden) { 'Hidden' } else { 'Auto' }
if (-not $tests) { throw "only @full entries match: $Filter (add -Full)" }
if ($List) { $tests; return }
# A checkout can add its own rule against whole-suite runs by accident.
$suitePolicy = Join-Path $PSScriptRoot 'suite_policy.ps1'
if (-not $Full -and -not $Headless -and $Pack -ne 'public' -and @($Filter | Where-Object { $_ -eq '*' }).Count -gt 0 -and (Test-Path $suitePolicy)) { & $suitePolicy $repo }

# Only the configurations the selected entries use are built, including fixture @config headers.
$configs = @($tests | ForEach-Object { Resolve-TestConfig $_ $Config } | Sort-Object -Unique)

# -NoBuild still checks staleness: a stale RelWithDebInfo DLL (Debug rebuilt, Release not) silently ran the .golden
# entries on pre-merge code more than once, so -NoBuild refuses instead of testing old code.
$newest = Get-ChildItem -LiteralPath (Join-Path $repo 'src'), (Join-Path $repo 'tests\unit'), (Join-Path $repo 'CMakeLists.txt'), (Join-Path $repo 'CMakePresets.json') -Recurse -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime | Select-Object -Last 1
$newestUnit = Get-ChildItem -LiteralPath (Join-Path $repo 'src\common'), (Join-Path $repo 'tests\unit'), (Join-Path $repo 'CMakeLists.txt'), (Join-Path $repo 'CMakePresets.json') -Recurse -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime | Select-Object -Last 1
foreach ($cfg in $configs) {
    $built = @(
        (Join-Path $repo "build\x64\bin\$cfg\OpenBeach.exe"),
        (Join-Path $repo "build\x64\bin\$cfg\obl_unit_tests.exe"))
    $stale = @($built | Where-Object { -not (Test-Path $_) }).Count -gt 0
    if (-not $stale) {
        # MSBuild relinks each binary only for its own inputs: OpenBeach.exe (the whole game) is checked against
        # everything, obl_unit_tests.exe against src/common and tests/unit (a src/dll header never relinks it).
        $stale = $newest -and $newest.LastWriteTime -gt (Get-Item $built[0]).LastWriteTime
        if (-not $stale -and $newestUnit) { $stale = $newestUnit.LastWriteTime -gt (Get-Item $built[1]).LastWriteTime }
    }
    if (-not $stale) { continue }
    if ($NoBuild) { throw "$cfg build is missing or older than $($newest.Name); run without -NoBuild (or ./build.ps1 -Config $(if ($cfg -eq 'Debug') { 'debug' } else { 'release' }))" }
    Write-Host "$cfg build is missing or older than $($newest.Name) - building"
    & (Join-Path $repo 'build.ps1') -Config $(if ($cfg -eq 'Debug') { 'debug' } else { 'release' })
    if ($LASTEXITCODE) { throw "$cfg build failed ($LASTEXITCODE)" }
}

# ctest ships with the CMake bundled in Visual Studio (see build.ps1).
function Find-CTest {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $path = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
    if (-not (Test-Path $path)) { throw "ctest not found in Visual Studio installation: $vs" }
    $path
}

function Get-CTestArgs([string]$test) {
    $name = [IO.Path]::GetFileNameWithoutExtension($test)
    @('--test-dir', (Join-Path $repo "build\x64"), '-C', (Resolve-TestConfig $test $Config), '-R', "^$name`$", '-V')
}

function Invoke-CTest([string]$test) {
    if (-not $script:ctest) { $script:ctest = Find-CTest }
    $ctestArgs = Get-CTestArgs $test
    $output = & $script:ctest @ctestArgs 2>&1 | ForEach-Object { "$_" }
    Read-CTestOutput $output $LASTEXITCODE
}

function Read-CTestOutput([string[]]$output, [int]$exit) {
    # The harness prints "N passed, N failed, N skipped"; ctest exits 0 for a skip too.
    $counts = $output | Where-Object { $_ -match '\d+ passed, \d+ failed, \d+ skipped' } | Select-Object -Last 1
    $counts = if ($counts) { $Matches[0] } else { '' }
    [pscustomobject]@{
        Ok      = $exit -eq 0 -and [bool]($output -match '100% tests passed')
        Skipped = [bool]($output -match '\*\*\*Skipped')
        Summary = $counts
        Output  = $output
    }
}

$gameTests = @($tests | Where-Object { -not $_.EndsWith('.ctest') })
if ($gameTests.Count -gt 0) {
    # Refresh even with an up-to-date build or -NoBuild, before parallel slots copy run/.
    # Replacing the directory also removes files deleted from the source pack.
    $l0Source = Join-Path $repo 'data\packs\l0'
    $l0Installed = Join-Path $repo 'run\packs\l0'
    if (-not (Test-Path -LiteralPath $l0Source -PathType Container)) { throw "L0 pack missing: $l0Source" }
    New-Item -ItemType Directory -Path (Join-Path $repo 'run\packs') -Force | Out-Null
    if (Test-Path -LiteralPath $l0Installed) { Remove-Item -LiteralPath $l0Installed -Recurse -Force }
    Copy-Item -LiteralPath $l0Source -Destination $l0Installed -Recurse -Force
    # The public set needs no imported installation: a tree without one gets the settings Assert-TestGameIni expects.
    $gameIni = Join-Path $repo 'run\BeachLife.ini'
    if ($Pack -eq 'public' -and -not (Test-Path -LiteralPath $gameIni)) {
        Set-Content -LiteralPath $gameIni -Value "[Beach Life Options]`nVersion=1`n[Language]`nLanguage=5" -Encoding ascii
    }
}
if ($Jobs -le 0) { $Jobs = if ($gameTests.Count -le 3) { 1 } else { [math]::Max(1, [math]::Min(8, [int]([Environment]::ProcessorCount / 2))) } }
$Jobs = [math]::Min($Jobs, [math]::Max(1, $gameTests.Count))

function Write-TestResult($r, [bool]$Verbose) {
    Write-Host ("{0,-4} {1,-36} {2,-5} {3,6:N1} s  {4}" -f $r.Result, $r.Test, $r.Config, $r.Seconds, $r.Summary)
    if ($r.Result -eq 'FAIL') {
        $shown = if ($Verbose) { $r.Output } else { $r.Output | Select-Object -Last 15 }
        $shown | ForEach-Object { Write-Host "     | $_" }
    }
}

function New-CTestResult([string]$test, $run, [double]$seconds) {
    [pscustomobject]@{
        Test    = $test
        Config  = $(if ((Resolve-TestConfig $test $Config) -eq 'Debug') { 'Debug' } else { 'Rel' })
        Result  = $(if (-not $run.Ok) { 'FAIL' } elseif ($run.Skipped) { 'SKIP' } else { 'PASS' })
        Seconds = [math]::Round($seconds, 1)
        Summary = $(if ($run.Ok) { $run.Summary } else { "ctest failed $($run.Summary)" })
        Output  = @($run.Output)
    }
}

function Invoke-CTestEntry([string]$test) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $run = Invoke-CTest $test
    $watch.Stop()
    New-CTestResult $test $run $watch.Elapsed.TotalSeconds
}

# A ctest entry of a parallel run: its own ctest process with the output in a file, collected by Complete-CTestProcess.
function Start-CTestProcess([string]$test, [string]$outDir) {
    if (-not $script:ctest) { $script:ctest = Find-CTest }
    $log = Join-Path $outDir "ctest-$([IO.Path]::GetFileNameWithoutExtension($test)).log"
    $quoted = Get-CTestArgs $test | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }
    $process = Start-Process $script:ctest -ArgumentList $quoted -RedirectStandardOutput $log -RedirectStandardError "$log.err" `
        -WindowStyle Hidden -PassThru
    $null = $process.Handle # keeps the exit code readable after the process ends
    [pscustomobject]@{ Test = $test; Process = $process; Log = $log; Watch = [Diagnostics.Stopwatch]::StartNew() }
}

function Complete-CTestProcess($job) {
    $job.Watch.Stop()
    $output = @(Get-Content $job.Log -ErrorAction SilentlyContinue) + @(Get-Content "$($job.Log).err" -ErrorAction SilentlyContinue)
    New-CTestResult $job.Test (Read-CTestOutput $output $job.Process.ExitCode) $job.Watch.Elapsed.TotalSeconds
}

# Durations of the previous run order the parallel schedule (longest first keeps every slot busy until the end).
$durationsFile = Join-Path $repo 'run\testlogs\durations.json'
$durations = @{}
if (Test-Path $durationsFile) { (Get-Content $durationsFile -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $durations[$_.Name] = [double]$_.Value } }

$byTest = @{}
$total = [Diagnostics.Stopwatch]::StartNew()
if ($skippedFull) { Write-Host "$skippedFull @full entries not selected (extended matrix: -Full runs them)" }
# Batchable @headless entries share a process (`batch:` lines, docs/architecture/headless/scripts.md "Script lists"): all of them in one
# sequential run, about 15 s of tests per batch in a parallel one so the slots stay balanced.
# Several ctest entries alone (-Filter unit*) take the parallel path without game slots, unless -Jobs 1 was asked for.
$ctestOnly = -not $gameTests -and $tests.Count -gt 1 -and -not $PSBoundParameters.ContainsKey('Jobs')
if ($Jobs -le 1 -and -not $ctestOnly) {
    foreach ($line in @(Group-HeadlessBatches $repo $tests $durations $mode 1e9)) {
        $results = if ($line.EndsWith('.ctest')) { @(Invoke-CTestEntry $line) }
                   elseif ($line.StartsWith('batch:')) { @(Invoke-HeadlessBatch $repo ($line.Substring(6) -split '\|') $Config '' 1) }
                   else { @(Invoke-GameTest $repo $line $Config '' $UpdateGolden.IsPresent 1 $mode) }
        foreach ($r in $results) {
            $byTest[$r.Test] = $r
            Write-TestResult $r $Verbose
        }
    }
} else {
    Write-Host "running $($gameTests.Count) game tests in $(if ($gameTests) { $Jobs } else { 0 }) parallel slots (run-slots/), ctest entries beside them"
    $outDir = Join-Path $repo 'run-slots\results'
    Remove-Item $outDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $outDir | Out-Null
    # unknown durations first (assume long), then longest first; ties keep manifest order. A batch line lasts as long as its scripts.
    $lineSeconds = { param($line) $sum = 0.0; foreach ($t in $(if ($line.StartsWith('batch:')) { $line.Substring(6) -split '\|' } else { @($line) })) { $sum += $(if ($durations.ContainsKey($t)) { $durations[$t] } else { 1e9 }) }; $sum }
    $order = @(Group-HeadlessBatches $repo $gameTests $durations $mode 15 | ForEach-Object -Begin { $i = 0 } -Process { [pscustomobject]@{ Test = $_; Index = $i++; Seconds = (& $lineSeconds $_) } } |
        Sort-Object @{ Expression = 'Seconds'; Descending = $true }, Index | ForEach-Object Test)
    $listFile = Join-Path $outDir 'tests.txt'
    Set-Content $listFile $order
    $pwsh = (Get-Process -Id $PID).Path
    $workers = if ($gameTests) { foreach ($slot in 1..$Jobs) {
        $workerArgs = @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'runtests_worker.ps1'), '-Slot', $slot, '-ListFile', $listFile, '-OutDir', $outDir, '-Config', $Config, '-TimeoutScale', $TimeoutScale, '-Mode', $mode)
        if ($UpdateGolden) { $workerArgs += '-UpdateGolden' }
        Start-Process $pwsh -ArgumentList $workerArgs -WindowStyle Hidden -PassThru
    } }
    # ctest entries need no game slot: they run beside the workers in their own ctest processes, longest first. Every host
    # entry (headless_*, standalone_*) plays in run/ itself, where the instance lock allows one game at a time, so those
    # form one serial lane; the unit entries fill up to $ctestJobs processes next to it.
    $ctestQueue = [Collections.Generic.List[string]]@(@($tests | Where-Object { $_.EndsWith('.ctest') }) |
        Sort-Object @{ Expression = { if ($durations.ContainsKey($_)) { $durations[$_] } else { 1e9 } }; Descending = $true })
    $ctestJobs = 3
    $ctestRunning = [Collections.Generic.List[object]]::new()
    $usesGameDir = { param($t) -not $t.StartsWith('unit') }
    $reported = @{}
    while ($true) {
        foreach ($job in @($ctestRunning | Where-Object { $_.Process.HasExited })) {
            $null = $ctestRunning.Remove($job)
            $r = Complete-CTestProcess $job
            $byTest[$r.Test] = $r
            Write-TestResult $r $Verbose
        }
        while ($ctestRunning.Count -lt $ctestJobs) {
            $laneBusy = @($ctestRunning | Where-Object { & $usesGameDir $_.Test }).Count -gt 0
            $next = $ctestQueue | Where-Object { -not ($laneBusy -and (& $usesGameDir $_)) } | Select-Object -First 1
            if (-not $next) { break }
            $null = $ctestQueue.Remove($next)
            $ctestRunning.Add((Start-CTestProcess $next $outDir))
        }
        $alive = @($workers | Where-Object { -not $_.HasExited }).Count + $ctestRunning.Count + $ctestQueue.Count
        foreach ($file in Get-ChildItem $outDir -Filter '*.json') {
            if ($reported.ContainsKey($file.Name)) { continue }
            $reported[$file.Name] = $true
            $r = Get-Content $file.FullName -Raw | ConvertFrom-Json
            $r.Output = @($r.Output)
            $byTest[$r.Test] = $r
            Write-TestResult $r $Verbose
        }
        if ($alive -eq 0) { break }
        Start-Sleep -Milliseconds 200
    }
    # a worker that died (killed, crashed) leaves its claimed test without a result
    foreach ($test in $gameTests) {
        if (-not $byTest.ContainsKey($test)) {
            $byTest[$test] = [pscustomobject]@{ Test = $test; Config = '?'; Result = 'FAIL'; Seconds = 0; Summary = 'no result (worker died)'; Output = @() }
            Write-TestResult $byTest[$test] $Verbose
        }
    }
    Remove-Item $outDir -Recurse -Force -ErrorAction SilentlyContinue
    # A failure under parallel load (also from other checkouts' runs; frame-rate and timing tests are the usual victims) is
    # not a regression until it repeats alone in run/. A pass on retry stays visible in the summary column.
    foreach ($test in @($gameTests | Where-Object { $byTest[$_].Result -eq 'FAIL' })) {
        Write-Host "retrying $test alone after a failure under parallel load"
        $first = $byTest[$test].Summary
        # the retry rewrites run/testlogs/<name>.log: keep the parallel run's log as the evidence of a flaky entry
        $log = Join-Path $repo "run\testlogs\$([IO.Path]::GetFileNameWithoutExtension($test)).log"
        if (Test-Path $log) { Copy-Item $log ($log -replace '\.log$', '.parallel.log') -Force; $first = "$first; log *.parallel.log" }
        $r = Invoke-GameTest $repo $test $Config '' $UpdateGolden.IsPresent 1 $mode
        if ($r.Result -eq 'PASS') { $r.Summary = "$($r.Summary) (passed on retry; parallel run: $first)" }
        $byTest[$test] = $r
        Write-TestResult $r $Verbose
    }
}
$total.Stop()

# only passing results feed the schedule of the next run (a timeout would distort it)
foreach ($r in $byTest.Values) { if ($r.Result -eq 'PASS') { $durations[$r.Test] = $r.Seconds } }
New-Item -ItemType Directory -Force (Split-Path $durationsFile) | Out-Null
$durations | ConvertTo-Json | Set-Content $durationsFile -Encoding utf8

$results = @($tests | ForEach-Object { $byTest[$_] })
""
($results | Format-Table Test, Config, Result, Seconds, Summary -AutoSize | Out-String).TrimEnd() | Write-Host
$failed = @($results | Where-Object Result -eq 'FAIL')
$skippedCount = @($results | Where-Object Result -eq 'SKIP').Count
"{0} tests, {1} passed, {2} failed, {3} skipped, {4:N1} s{5}" -f $results.Count, ($results.Count - $failed.Count - $skippedCount), $failed.Count, $skippedCount, $total.Elapsed.TotalSeconds, $(if ($Jobs -gt 1) { " ($Jobs slots)" } else { '' })
if ($failed) { exit 1 }
