# No-game regression: bound startup and trapped hosts, preserve exit codes and isolate slots/worktrees.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $repo 'tools/common.ps1')
$root = Join-Path $repo ('run/tmp/runner timeout ' + [guid]::NewGuid().ToString('N'))
$bin = Join-Path $root "build/x64/bin/Debug"  # where the copied autotest.ps1 looks
$testTools = Join-Path $root 'tools'
New-Item -ItemType Directory -Force $bin, $testTools | Out-Null
Copy-Item (Join-Path $repo 'tools/autotest.ps1'), (Join-Path $repo 'tools/common.ps1') $testTools
$exe = Join-Path $bin 'OpenBeach.exe'
$csc = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $csc /nologo /target:exe "/out:$exe" (Join-Path $PSScriptRoot 'fixtures/runner_host.cs')
if ($LASTEXITCODE) { throw 'runner fixture compilation failed' }
$pwsh = (Get-Process -Id $PID).Path
$dirs = @()
$workers = @()
$serialWorkers = @()
$sentinelLauncher = $null
$savedEnv = [Environment]::GetEnvironmentVariable('OBL_RUNNER_FIXTURE')
[Environment]::SetEnvironmentVariable('OBL_RUNNER_FIXTURE', [NullString]::Value)
function Check([bool]$Ok, [string]$What) {
    if (-not $Ok) { throw $What }
    Write-Host "PASS $What"
}
function Fixture([string]$Slot, [string]$Mode, [string]$Headers = '') {
    $dir = Join-Path $root $Slot
    New-Item -ItemType Directory -Force $dir | Out-Null
    $script:dirs += $dir
    Set-Content (Join-Path $dir 'BeachLife.ini') "[Video]`nResWidth=1280`nResHeight=720"
    Add-Content (Join-Path $dir 'BeachLife.ini') "[Beach Life Options]`nVersion=1`n[Language]`nLanguage=5`n[Sound]`nPlayListCount=11"
    foreach ($i in 0..10) {
        Add-Content (Join-Path $dir 'BeachLife.ini') "PlayListTrack$i=track$i.mp3`nPlayListDirectory$i=music/`nPlayListAttribute$i=0`nPlayListTime$i=1000"
    }
    [IO.File]::WriteAllBytes((Join-Path $dir 'progress.json'), [byte[]](1, 2, 3))
    $test = Join-Path $dir "$Mode.txt"
    Set-Content $test "# @timeout 2`n# @env OBL_RUNNER_FIXTURE=temporary`n$Headers`nquit"
    @{ Dir = $dir; Script = $test; IniHash = (Get-FileHash (Join-Path $dir 'BeachLife.ini')).Hash }
}
function Run([hashtable]$Fixture, [string]$ExpectedFailure = '') {
    $workerScript = Join-Path $Fixture.Dir 'serial-worker.ps1'
    $result = Join-Path $Fixture.Dir 'result.json'
    $workerText = @'
param($Tool, $Test, $Dir, $Result)
$failure = ''
try { & $Tool $Test -GameDir $Dir *>&1 | Out-Null }
catch { $failure = $_.Exception.Message }
@{ Failure = $failure; EnvRestored = $null -eq [Environment]::GetEnvironmentVariable('OBL_RUNNER_FIXTURE') } |
    ConvertTo-Json | Set-Content $Result
'@
    Set-Content $workerScript $workerText
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process $pwsh -ArgumentList @('-NoProfile', '-File', "`"$workerScript`"", "`"$(Join-Path $testTools 'autotest.ps1')`"", "`"$($Fixture.Script)`"", "`"$($Fixture.Dir)`"", "`"$result`"") `
        -WindowStyle Hidden -PassThru
    $null = $process.Handle
    $script:serialWorkers += $process
    Check ($process.WaitForExit(15000)) 'worker ends before the self-test safety deadline'
    $outcome = Get-Content $result -Raw | ConvertFrom-Json
    $failure = $outcome.Failure
    Check ($failure -eq $ExpectedFailure) "$(Split-Path $Fixture.Script -Leaf): outcome '$failure'"
    Check ($timer.Elapsed.TotalSeconds -lt 10) 'run ends within the external deadline margin'
    Check (-not [bool](Get-RunGame $root $Fixture.Dir)) 'no host remains in the test directory'
    Check ((Get-FileHash (Join-Path $Fixture.Dir 'BeachLife.ini')).Hash -eq $Fixture.IniHash) 'whole ini restored'
    Check ([Convert]::ToHexString([IO.File]::ReadAllBytes((Join-Path $Fixture.Dir 'progress.json'))) -eq '010203') 'scenario progress restored'
    Check $outcome.EnvRestored 'environment restored'
}
try {
    # A live host in another slot shares the executable but must never be killed by timeout cleanup.
    $other = Fixture 'other-slot' 'hang'
    $sentinelLauncher = Start-Process $exe -ArgumentList @('--game-dir', "`"$($other.Dir)`"", '--script', "`"$($other.Script)`"") -WindowStyle Hidden -PassThru
    $null = $sentinelLauncher.Handle
    $ready = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path (Join-Path $other.Dir 'openbeach.log')) -and $ready.Elapsed.TotalSeconds -lt 10) { Start-Sleep -Milliseconds 50 }
    $sentinel = @(Get-RunGame $root $other.Dir)
    Check ($sentinel.Count -eq 1) 'CIM discovers the standalone host in its slot'
    Check (-not [bool](Get-RunGame $repo $other.Dir)) 'another worktree cannot claim the fixture executable'

    Run (Fixture 'startup' 'startup-hang') 'test timed out after 2 s'
    Run (Fixture 'normal' 'success')
    Run (Fixture 'exit' 'exit7' '# @expect-exit 7')
    Run (Fixture 'unexpected' 'unexpected7') 'game exited with code 7'
    Run (Fixture 'expected-hang' 'hang' '# @expect-exit 0') 'test timed out after 2 s'

    # Run two trapped hosts in independent workers, as runtests' slots do. An outer timeout
    # makes regressions fail instead of hanging this self-test forever.
    $timer = [Diagnostics.Stopwatch]::StartNew()
    foreach ($slot in 'parallel-1', 'parallel-2') {
        $fixture = Fixture $slot 'hang'
        $worker = Join-Path $fixture.Dir 'worker.ps1'
        $output = Join-Path $fixture.Dir 'worker.log'
        $workerText = @'
param($Tool, $Test, $Dir)
try { & $Tool $Test -GameDir $Dir *>&1 | Out-Null; exit 1 }
catch { if ($_.Exception.Message -eq 'test timed out after 2 s') { exit 0 }; Write-Error $_; exit 1 }
'@
        Set-Content $worker $workerText
        $process = Start-Process $pwsh -ArgumentList @('-NoProfile', '-File', "`"$worker`"", "`"$(Join-Path $testTools 'autotest.ps1')`"", "`"$($fixture.Script)`"", "`"$($fixture.Dir)`"") `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput $output -RedirectStandardError "$output.err"
        $null = $process.Handle
        $workers += @{ Process = $process; Fixture = $fixture }
    }
    foreach ($worker in $workers) {
        $ended = $worker.Process.WaitForExit([int][math]::Max(0, 15000 - $timer.Elapsed.TotalMilliseconds))
        Check ($ended -and $worker.Process.ExitCode -eq 0) 'parallel trapped host times out'
        Check (-not [bool](Get-RunGame $root $worker.Fixture.Dir)) 'parallel timeout leaves no host'
        Check ((Get-FileHash (Join-Path $worker.Fixture.Dir 'BeachLife.ini')).Hash -eq $worker.Fixture.IniHash) 'parallel timeout restores ini'
        Check ((Get-Content (Join-Path $worker.Fixture.Dir 'fixture.env') -Raw) -eq 'temporary') 'host sees environment before cleanup'
    }
    Check (-not $sentinel[0].HasExited) 'another slot survives all timeout cleanup'
    Write-Host 'test_autotest_timeout: all checks passed'
} finally {
    foreach ($process in @($serialWorkers) + @($workers | ForEach-Object { $_.Process })) {
        if (-not $process.HasExited) { Stop-Process -InputObject $process -Force }
        $process.Dispose()
    }
    Stop-RunGame $root $other.Dir $sentinelLauncher
    # Also cover a regressed launch that never created a host. Only this fixture's unique
    # executable is eligible; cleanup still goes through the shared helper.
    Get-CimInstance Win32_Process -Filter "Name = 'OpenBeach.exe'" | Where-Object { $_.ExecutablePath -eq $exe } |
        ForEach-Object { Stop-RunGame $root $other.Dir (Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue) }
    foreach ($dir in $dirs) { Stop-RunGame $root $dir }
    if ($sentinelLauncher) { $sentinelLauncher.Dispose() }
    [Environment]::SetEnvironmentVariable('OBL_RUNNER_FIXTURE', $(if ($null -eq $savedEnv) { [NullString]::Value } else { $savedEnv }))
}
