# Expected-crash regression checks. Unlike runtests.ps1, these pass only when autotest reports a diagnosed crash.
param(
    [ValidateSet('Debug', 'RelWithDebInfo')][string]$Config = 'Debug'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'common.ps1')
$log = Join-Path $repo 'run\openbeach.log'
$logDir = Join-Path $repo 'run\testlogs'
New-Item -ItemType Directory -Force $logDir | Out-Null
foreach ($test in @('crash_diagnostics', 'crash_diagnostics_log_lock', 'crash_diagnostics_filter')) {
    $failure = $null
    $output = [Collections.Generic.List[string]]::new()
    try {
        & (Join-Path $PSScriptRoot 'autotest.ps1') (Join-Path $repo "tests\$test.txt") -Config $Config 2>&1 |
            ForEach-Object { $output.Add("$_") }
    } catch {
        $failure = $_.Exception.Message
    } finally {
        Stop-RunGame $repo
        if (Test-Path $log) { Copy-Item $log (Join-Path $logDir "$test.log") }
    }
    $lines = Get-Content $log -ErrorAction SilentlyContinue
    if ($failure -notlike 'game crashed: code=c0000005*' -or
        -not ($lines -match 'autotest: CRASH eax=.*esp=') -or
        -not ($lines -match 'autotest: CRASH stack\[\d+\]') -or
        # Building::OnExitState is C++, so the null-this fault is raised in the game executable:
        # the fault address and the top stack frame must be in OpenBeach.exe, the access is a read near null, ecx (this) is 0.
        -not ($lines -match 'autotest: CRASH code=c0000005 at [0-9a-f]{8} OpenBeach\.exe\+0x') -or
        -not ($lines -match 'autotest: CRASH read of 0000[0-9a-f]{4}$') -or
        -not ($lines -match 'autotest: CRASH eax=.* ecx=00000000 ') -or
        -not ($lines -match 'autotest: CRASH stack\[\d+\] [0-9a-f]{8} OpenBeach\.exe\+0x')) {
        $output | ForEach-Object { Write-Host $_ }
        throw "${test}: missing crash diagnostics or wrong failure: $failure"
    }
    Write-Host "PASS $test (access violation, registers, stack, fault and stack frame attributed to the game executable)"
}

# Hang watchdog: the blocked game thread is sampled (EIP inside the sleep, stack, other threads) and the process ends.
$test = 'crash_diagnostics_hang'
$failure = $null
try {
    & (Join-Path $PSScriptRoot 'autotest.ps1') (Join-Path $repo "tests\$test.txt") -Config $Config 2>&1 | Out-Null
} catch {
    $failure = $_.Exception.Message
} finally {
    Stop-RunGame $repo
    if (Test-Path $log) { Copy-Item $log (Join-Path $logDir "$test.log") }
}
$lines = Get-Content $log -ErrorAction SilentlyContinue
$hang = $lines | Select-String 'CRASH code=HANG no frame for (\d+) ms'
$hangMs = if ($hang) { [int]$hang.Matches[0].Groups[1].Value } else { 0 }
if ($failure -notlike 'game crashed: code=HANG*' -or -not ($lines -match 'autotest: CRASH s0 at ') -or -not ($lines -match 'autotest: CRASH s0 stack\[\d+\]') -or
    -not ($lines -match 'autotest: CRASH t\d+ at ') -or $hangMs -lt 20000 -or $hangMs -ge 30000) {
    throw "${test}: missing hang diagnostics or wrong failure: $failure"
}
Write-Host "PASS $test (20 s watchdog restored after world load; sampled thread, stack and other threads)"
