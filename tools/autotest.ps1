# Runs a test script (see src/testing/autotest.cpp) in the hidden game and waits for it to finish.
# Screenshots land in run/captures/<name>.png. Fails (throws) on FAILED lines, FatalError, a crash or a timeout.
#   ./tools/autotest.ps1 tests/menu.txt [-TimeoutSec 120] [-MaxFps 60]
# tests/<family>_s<n>.txt without its own file is generated from the template tests/<family>_sN.txt (Resolve-TestScript, common.ps1).
# Script headers (comment lines anywhere in the file) set defaults for the run:
#   # @timeout 90     wall-clock limit in seconds (the -TimeoutSec parameter wins)
#   # @env NAME=VALUE default environment variable for the game (an already set variable wins; restored afterwards), e.g. OBL_WINDOWED=0
#   # @maxfps 60      frame cap (scripts run uncapped by default: the game's dt comes from real time, so the cap
#   #                 only matters for tests of the limiter itself; the -MaxFps parameter wins)
#   # @gameini Section.Key=Value   value written to run/BeachLife.ini before the start (the game remembers e.g. its
#   #                 resolution there), on top of the test baseline below
#   # (automatic)  test baseline of the ini: Video.ResWidth/ResHeight=800x600 and no [Dialog] section (window positions are absolute
#   #                 pixels, docs/testing/golden-images.md B2/B5); the whole ini is put back after the game exits (discarding test overrides), and
#   #                 SaveGames files the run created are deleted, so a test never sees what an earlier one (or a manual session) left
#   # @audio device  the game's SDL audio uses the real default device; by default every run gets SDL_AUDIO_DRIVER=dummy, so a
#   #                 batch of parallel games stays silent (an @env SDL_AUDIO_DRIVER, an already set variable or -Audible win)
#   # @check python tools/x.py {run}   oracle run after a passing game run (repeatable); {run} = the game directory,
#   #                 {repo} = the repository; a non-zero exit fails the test (in-game dumps checked against tools/*.py)
#   # @expect-exit 0  the game must end by itself with this process exit code before the script reaches `quit` (FatalError,
#   #                 ShowErrorAndExit); its `FatalError:` log line is then expected, not a failure. Reaching `quit` fails the test
#   # @windows-only  ignored here; tools/obl_sim_tests.py (Linux obl-sim) skips the script (it tests SEH or another Windows-only part)
#   # @expect-log REGEX  some line of run/openbeach.log must match (repeatable; case-sensitive .NET regex, rest of the line)
# -UpdateGolden: every `expect-shot` records its frame as run/golden/<name>.png instead of comparing (docs/testing/golden-images.md).
# -Headless: the script runs in the headless host (`--headless <script>`: no window, renderer output, audio or input; directives that
# need them are refused) instead of the hidden game; runtests.ps1 passes it for manifest entries tagged @headless
# (docs/architecture/headless/scripts.md, "Test scripts"). @maxfps and -Visible do not apply there.
param(
    [Parameter(Mandatory)][string]$Script,
    [int]$TimeoutSec = 120,
    [int]$MaxFps = -1,
    [ValidateSet('Debug', 'RelWithDebInfo')][string]$Config = 'Debug',
    [switch]$Visible,
    [switch]$UpdateGolden,
    [switch]$Audible,  # real audio device for every script (default: the dummy SDL driver unless the script asks for `# @audio device`)
    [string]$GameDir = '',  # game directory to run in (default run/); runtests.ps1 -Jobs passes its run-slots/<n>/
    [double]$TimeoutScale = 1,  # multiplies the wall-clock limit (parallel runs: slower games, same test)
    [switch]$Headless
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
. (Join-Path $PSScriptRoot 'common.ps1')
# The standalone host: the game built only from our own code.
$launcher = Join-Path $repo "build\x64\bin\$Config\OpenBeach.exe"
if (-not $GameDir) { $GameDir = Join-Path $repo 'run' }
$GameDir = [IO.Path]::GetFullPath($GameDir)
$log = Join-Path $GameDir 'openbeach.log'
$captures = Join-Path $GameDir 'captures'
$scriptPath = Resolve-TestScript $Script $GameDir

$envOverrides = @{}
$gameIni = [ordered]@{}
$checks = @()
$expectExit = $null
$expectLog = @()
foreach ($line in Get-Content $scriptPath) {
    if ($line -match '^\s*#\s*@expect-exit\s+(-?\d+)\s*$') { $expectExit = [int]$Matches[1] }
    if ($line -match '^\s*#\s*@expect-log\s+(.+?)\s*$') { $expectLog += $Matches[1] }
    if ($line -match '^\s*#\s*@env\s+(\w+)=(\S*)') { if ([string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable($Matches[1]))) { $envOverrides[$Matches[1]] = $Matches[2] } }
    if ($line -match '^\s*#\s*@timeout\s+(\d+)') { if (-not $PSBoundParameters.ContainsKey('TimeoutSec')) { $TimeoutSec = [int]$Matches[1] } }
    if ($line -match '^\s*#\s*@maxfps\s+(\d+)') { if ($MaxFps -lt 0) { $MaxFps = [int]$Matches[1] } }
    if ($line -match '^\s*#\s*@gameini\s+([^.\s]+)\.([^=\s]+)=(\S*)') { $gameIni["$($Matches[1]).$($Matches[2])"] = $Matches[3] }
    if ($line -match '^\s*#\s*@check\s+(\S+)\s*(.*)$') { $checks += , @($Matches[1], $Matches[2]) }
}
$realAudio = $Audible -or [bool](Get-Content $scriptPath | Select-String '^\s*#\s*@audio\s+device\b')
if (-not $realAudio -and -not $envOverrides.ContainsKey('SDL_AUDIO_DRIVER') -and
    [string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable('SDL_AUDIO_DRIVER'))) {
    $envOverrides['SDL_AUDIO_DRIVER'] = 'dummy'
}
$TimeoutSec = [int][math]::Ceiling($TimeoutSec * $TimeoutScale)
$envOverrides['OBL_UPDATE_GOLDEN'] = $(if ($UpdateGolden) { '1' } else { '0' })

# Sets [Section] Key=Value in the game's ini (adding the key/section when missing) and returns the previous value or $null.
# Latin-1 keeps every byte of the file as it was.
$iniPath = Join-Path $GameDir 'BeachLife.ini'
Assert-TestGameIni $iniPath
function Set-GameIniValue([string]$Section, [string]$Key, $Value) {  # untyped: [string] would turn $null (remove the key) into ""
    $latin1 = [Text.Encoding]::Latin1
    $lines = [Collections.Generic.List[string]]::new()
    if (Test-Path $iniPath) { $lines.AddRange([IO.File]::ReadAllLines($iniPath, $latin1)) }
    $inSection = $false; $sectionEnd = -1; $previous = $null; $found = $false
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        if ($lines[$i] -match '^\s*\[(.*)\]\s*$') {
            if ($inSection) { break }
            $inSection = $Matches[1] -eq $Section
            if ($inSection) { $sectionEnd = $i + 1 }
            continue
        }
        if (-not $inSection) { continue }
        if ($lines[$i].Trim()) { $sectionEnd = $i + 1 }
        if ($lines[$i] -match "^\s*$([regex]::Escape($Key))\s*=(.*)$") {
            $previous = $Matches[1]; $found = $true
            if ($null -eq $Value) { $lines.RemoveAt($i) } else { $lines[$i] = "$Key=$Value" }
            break
        }
    }
    if (-not $found -and $null -ne $Value) {
        if ($sectionEnd -lt 0) { $lines.Add("[$Section]"); $sectionEnd = $lines.Count }
        $lines.Insert($sectionEnd, "$Key=$Value")
    }
    [IO.File]::WriteAllLines($iniPath, $lines, $latin1)
    return $previous
}

# Drops a whole [Section] (header and keys). Manual sessions save positions here; tests start without them.
function Remove-GameIniSection([string]$Section) {
    if (-not (Test-Path $iniPath)) { return }
    $latin1 = [Text.Encoding]::Latin1
    $kept = [Collections.Generic.List[string]]::new()
    $skip = $false
    foreach ($line in [IO.File]::ReadAllLines($iniPath, $latin1)) {
        if ($line -match '^\s*\[(.*)\]\s*$') { $skip = $Matches[1] -eq $Section }
        if (-not $skip) { $kept.Add($line) }
    }
    [IO.File]::WriteAllLines($iniPath, $kept, $latin1)
}
# Values every test starts from unless it declares its own @gameini: tests assume the stock 800x600 (camera, culling and window
# tests have no @gameini), but a manual session at another resolution leaves it in run/BeachLife.ini. The language is the
# supported build's Polish: an L0 test's Language=0 stayed in the file and turned every later base-pack run French.
$testBaselineIni = [ordered]@{ 'Video.ResWidth' = '800'; 'Video.ResHeight' = '600'; 'Language.Language' = '5' }


# the pause lets a killed leftover release its files; nothing to wait for when no game is running
if (Get-RunGame $repo $GameDir) {
    Stop-RunGame $repo $GameDir
    Start-Sleep -Milliseconds 500
}
# A killed game may retain its append handle briefly during teardown. Never scan a previous run's crash as this test's result.
for ($logAttempt = 0; $logAttempt -lt 50; ++$logAttempt) {
    try {
        if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -ErrorAction Stop }
        break
    } catch {
        if ($logAttempt -eq 49) { throw }
        Start-Sleep -Milliseconds 100
    }
}
Remove-Item (Join-Path $captures '*.png') -ErrorAction SilentlyContinue

if ($Headless) {
    $launchArgs = @('--game-dir', $GameDir, '--headless', $scriptPath)
} else {
    $launchArgs = @('--game-dir', $GameDir, '--script', $scriptPath)
    if (-not $Visible) { $launchArgs += '--hidden' }
    if ($MaxFps -ge 0) { $launchArgs += @('--max-fps', $MaxFps) }
}
$watch = [Diagnostics.Stopwatch]::StartNew()
# Whole-file snapshot: baseline edits and @gameini overrides must be restored byte-for-byte, including on failure.
$iniSnapshot = if (Test-Path $iniPath) { [IO.File]::ReadAllBytes($iniPath) } else { $null }
$progressPath = Join-Path $GameDir 'progress.json'
$progressSnapshot = if (Test-Path -LiteralPath $progressPath) { [IO.File]::ReadAllBytes($progressPath) } else { $null }
# The legacy sp.bin seeds a missing progress.json (all islands unlocked in run/), so it is hidden as well.
$seedPath = Join-Path $GameDir 'sp.bin'
$seedSnapshot = if (Test-Path -LiteralPath $seedPath) { [IO.File]::ReadAllBytes($seedPath) } else { $null }
$savesDir = Join-Path $GameDir 'SaveGames'
$savesBefore = @(Get-ChildItem -LiteralPath $savesDir -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
$restoreIni = {
    if ($null -ne $progressSnapshot) { [IO.File]::WriteAllBytes($progressPath, $progressSnapshot) } else { Remove-Item -LiteralPath $progressPath -Force -ErrorAction SilentlyContinue }
    if ($null -ne $seedSnapshot) { [IO.File]::WriteAllBytes($seedPath, $seedSnapshot) } else { Remove-Item -LiteralPath $seedPath -Force -ErrorAction SilentlyContinue }
    if ($null -ne $iniSnapshot) { [IO.File]::WriteAllBytes($iniPath, $iniSnapshot) } else { Remove-Item $iniPath -Force -ErrorAction SilentlyContinue }
    # saves the test wrote (a full SaveGames/ crashes ui.save-game-write); pre-existing ones stay
    foreach ($f in @(Get-ChildItem -LiteralPath $savesDir -Recurse -File -ErrorAction SilentlyContinue)) {
        if ($savesBefore -notcontains $f.FullName) { Remove-Item -LiteralPath $f.FullName -Force -ErrorAction SilentlyContinue }
    }
}
foreach ($name in $testBaselineIni.Keys) {
    $section, $key = $name -split '\.', 2
    $null = Set-GameIniValue $section $key $testBaselineIni[$name]
}
Remove-GameIniSection 'Dialog'
foreach ($name in $gameIni.Keys) {
    $section, $key = $name -split '\.', 2
    $null = Set-GameIniValue $section $key $gameIni[$name]
}
$savedEnv = @{}
foreach ($name in $envOverrides.Keys) {
    $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name)
    [Environment]::SetEnvironmentVariable($name, $envOverrides[$name])
}
# Keep overrides until both launcher and host end, including startup and timeout cleanup.
$restoreEnv = { foreach ($name in $savedEnv.Keys) { [Environment]::SetEnvironmentVariable($name, $(if ($null -eq $savedEnv[$name]) { [NullString]::Value } else { $savedEnv[$name] })) } }
$deadline = (Get-Date).AddSeconds($TimeoutSec)
$exitCode = $null
$launcherProcess = $null
$timedOut = $false
try {
    if (Test-Path -LiteralPath $progressPath) { Remove-Item -LiteralPath $progressPath -Force -ErrorAction Stop }
    if (Test-Path -LiteralPath $seedPath) { Remove-Item -LiteralPath $seedPath -Force -ErrorAction Stop }
    # A native pipeline can wait on the host's inherited output even with --no-wait. Start a hidden
    # launcher asynchronously and bound the whole run, including startup, by its retained handle.
    # The launcher waits for its child and returns the child's exit code in both test modes.
    $quoted = @($launchArgs | ForEach-Object { $a = "$_"; if ($a -match '\s') { "`"$a`"" } else { $a } })
    $launcherProcess = Start-Process -FilePath $launcher -ArgumentList $quoted -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $GameDir 'launcher.stdout.log') `
        -RedirectStandardError (Join-Path $GameDir 'launcher.stderr.log')
    $null = $launcherProcess.Handle # retains the exit code and prevents PID reuse until cleanup
    $timedOut = -not $launcherProcess.WaitForExit([int][math]::Max(0, ($deadline - (Get-Date)).TotalMilliseconds))
    if ($timedOut) {
        Write-Warning "timeout after $TimeoutSec s - killing the game"
    } else {
        $exitCode = $launcherProcess.ExitCode
    }
} finally {
    try { Stop-RunGame $repo $GameDir $launcherProcess } finally {
        if ($launcherProcess) { $launcherProcess.Dispose() }
        $watch.Stop()
        & $restoreEnv
        & $restoreIni
    }
}
if (-not $timedOut -and $null -eq $expectExit -and $exitCode) {
    Get-Content (Join-Path $GameDir 'launcher.stderr.log') -ErrorAction SilentlyContinue
}

Get-Content $log -ErrorAction SilentlyContinue |
    Select-String 'autotest:|FatalError|suppressed|Game_ChangeResolution|input:' |
    Where-Object { $_.Line -notmatch 'autotest: CRASH' } |
    ForEach-Object { $_.Line }

# Keep the crash reporter's module offsets and PDB names; symbols of retired builds are no longer available.
$crashLines = @(Get-Content $log -ErrorAction SilentlyContinue | Where-Object { $_ -match 'autotest: CRASH' })
$crashSummary = $null
if ($crashLines) {
    foreach ($line in $crashLines) {
        if (-not $crashSummary -and $line -match 'CRASH code=') { $crashSummary = ($line -replace '^.*autotest: CRASH ', '') }
        $line
    }
}
Get-ChildItem $captures -Filter *.png -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
$lines = Get-Content $log -ErrorAction SilentlyContinue
$failurePattern = if ($null -ne $expectExit) { 'autotest:.*\bFAILED\b|autotest: aborted' } else { 'autotest:.*\bFAILED\b|autotest: aborted|FatalError:' }
$failed = @($lines | Where-Object { $_ -cmatch $failurePattern })
$completed = [bool]($lines -match 'autotest: completed')
$summary = ($lines | Where-Object { $_ -match 'autotest: summary' } | Select-Object -Last 1)
"{0}{1}: {2:N1} s{3}" -f (Split-Path $scriptPath -Leaf), $(if ($Headless) { ' (headless)' } else { '' }), $watch.Elapsed.TotalSeconds, $(if ($summary) { ' - ' + ($summary -replace '^.*autotest: summary ', '') } else { '' })
if ($crashSummary) { throw "game crashed: $crashSummary" }
if ($timedOut) { throw "test timed out after $TimeoutSec s" }
$missingLog = @($expectLog | Where-Object { $pattern = $_; -not ($lines | Where-Object { $_ -cmatch $pattern }) })
if ($null -ne $expectExit) {
    if ($failed) { throw "test reported $($failed.Count) failure line(s); see log above" }
    if ($completed) { throw "test reached quit, but the game was expected to exit by itself with code $expectExit" }
    if ($null -eq $exitCode) { throw "exit code of the game unknown (the launcher did not end), expected $expectExit" }
    if ($exitCode -ne $expectExit) { throw "game exited with code $exitCode, expected $expectExit" }
} else {
    if (-not $completed -and -not $failed) { throw 'test did not reach quit (crash or script error)' }
    if ($failed) { throw "test reported $($failed.Count) failure line(s); see log above" }
    if (-not $completed) { throw 'test did not complete' }
    if ($exitCode -ne 0) { throw "game exited with code $exitCode" }
}
if ($missingLog) { throw "log has no line matching: $($missingLog -join ' | ')" }
# the Summary column of runtests.ps1 (testrun.ps1 takes the last `summary passed=` line): the exit code and each @expect-log
if ($null -ne $expectExit) { "expected exit: summary passed=$(1 + $expectLog.Count) failed=0 exit=$exitCode" }
foreach ($check in $checks) {
    # split before substituting, so paths with spaces stay one argument
    $checkArgs = @($check[1] -split '\s+' | Where-Object { $_ } | ForEach-Object { $_.Replace('{run}', $GameDir).Replace('{repo}', "$repo") })
    Push-Location $repo
    try { & $check[0] @checkArgs; $checkExit = $LASTEXITCODE } finally { Pop-Location }
    if ($checkExit) { throw "check failed ($checkExit): $($check[0]) $($checkArgs -join ' ')" }
}
