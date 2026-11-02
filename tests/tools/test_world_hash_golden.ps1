# No-game contracts for compact golden traces, recording and manifest failure propagation.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $repo 'tools/world_hash_golden_data.ps1')
. (Join-Path $repo 'tools/common.ps1')
. (Join-Path $repo 'tools/testrun.ps1')
$root = Join-Path $repo ('run/testlogs/world-hash-golden-tool-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$script:passed = 0
function Check([bool]$Ok, [string]$Message) {
    if (-not $Ok) { throw $Message }
    ++$script:passed
}
function Reject([scriptblock]$Action, [string]$Reason) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $_.Exception.Message -like "*$Reason*" }
    Check $rejected "expected rejection: $Reason"
}
$trace = Join-Path $root 'golden.tsv'
$log = Join-Path $root 'game.log'
$valid = @('OBL-world-v3', "index`tsteps`tfields`thash", "0`t0`t1`t0123456789abcdef", "1`t300`t2`tfedcba9876543210")
Set-Content -LiteralPath $trace -Value $valid
Set-Content -LiteralPath $log -Value @(
    'prefix autotest: world-hash schema=3 checkpoint=0 steps=0 fields=1 hash=0123456789abcdef file=checkpoint-0.tsv',
    'autotest: world-hash off',
    'autotest: world-hash schema=3 checkpoint=1 steps=300 fields=2 hash=fedcba9876543210 file=checkpoint-1.tsv')
$a = @(Read-GoldenHashTrace $trace); $b = @(Read-GoldenHashLog $log)
Check ((Compare-GoldenHashTrace $a $b) -like '*IDENTICAL*') 'identical traces rejected'
Write-GoldenHashTrace $b $trace
Check (([IO.File]::ReadAllLines($trace) -join "`n") -ceq ($valid -join "`n")) 'compact trace roundtrip changed columns'
foreach ($field in 'Index', 'Steps', 'Fields', 'Hash') {
    $changed = @($b | ForEach-Object { $_.PSObject.Copy() })
    $changed[1].$field = if ($field -eq 'Hash') { '0000000000000000' } else { '999' }
    Reject { Compare-GoldenHashTrace $a $changed } "mismatch*$field"
}
Reject { Compare-GoldenHashTrace $a @($b[0]) } 'checkpoint count differs'
foreach ($case in @(
    @{ Text=@('BLRE-world-v1', $valid[1], $valid[2]); Reason='header/schema' },
    @{ Text=$valid[0..1]; Reason='no world-hash checkpoints' },
    @{ Text=@($valid[0..1]) + "0`t0`t1`tbogus"; Reason='invalid golden hash row' },
    @{ Text=@($valid[0..1]) + "0`t0`t1`t0123456789ABCDEF"; Reason='invalid golden hash row' },
    @{ Text=@($valid[0..1]) + "1`t0`t1`t0123456789abcdef"; Reason='invalid checkpoint sequence' },
    @{ Text=@($valid[0..1]) + @("0`t300`t1`t0123456789abcdef", "1`t0`t1`t0123456789abcdef"); Reason='invalid checkpoint sequence' },
    @{ Text=@($valid[0..1]) + "0`t0`t0`t0123456789abcdef"; Reason='invalid checkpoint sequence' }
)) {
    Set-Content -LiteralPath $trace -Value $case.Text
    Reject { Read-GoldenHashTrace $trace } $case.Reason
}
Reject { Read-GoldenHashTrace (Join-Path $root 'missing.tsv') } '*'
Set-Content -LiteralPath $log -Value 'autotest: world-hash schema=4 checkpoint=0 steps=0 fields=1 hash=0123456789abcdef file=checkpoint-0.tsv'
Reject { Read-GoldenHashLog $log } 'report/schema'
Set-Content -LiteralPath $log -Value 'autotest: world-hash schema=3 checkpoint=0 steps=0 fields=1 hash=0123456789abcdef file=checkpoint-1.tsv'
Reject { Read-GoldenHashLog $log } 'checkpoint filename'
Set-Content -LiteralPath $log -Value 'autotest: world-hash off'
Reject { Read-GoldenHashLog $log } 'no world-hash checkpoints'

# A fake autotest exercises the real CLI and manifest dispatch without launching the game.
$mock = Join-Path $root 'mock'
foreach ($directory in 'tools', 'tests/golden-hashes', 'run') { New-Item -ItemType Directory -Path (Join-Path $mock $directory) | Out-Null }
foreach ($tool in 'common.ps1', 'world_hash_compare.ps1', 'world_hash_golden.ps1', 'world_hash_golden_data.ps1') {
    Copy-Item -LiteralPath (Join-Path $repo "tools/$tool") -Destination (Join-Path $mock "tools/$tool")
}
Set-Content -LiteralPath (Join-Path $mock 'tests/fixture.txt') -Value 'quit'
@'
param($Script, $Config, $GameDir, $TimeoutScale, [switch]$UpdateGolden)
$callsFile = Join-Path $GameDir 'calls.txt'
$calls = if (Test-Path $callsFile) { [int](Get-Content $callsFile) + 1 } else { 1 }
Set-Content $callsFile $calls
$hash = if (Test-Path (Join-Path $GameDir 'mismatch')) { '0000000000000000' } else { '0123456789abcdef' }
$value = if (Test-Path (Join-Path $GameDir 'unstable')) { [string]$calls } else { '0000803f' }
"OBL-world-v3`nPlayer`tCash`t3`t$value`n" | Set-Content -LiteralPath (Join-Path $env:OBL_WORLD_HASH_DIR 'checkpoint-0.tsv')
"autotest: world-hash schema=3 checkpoint=0 steps=0 fields=1 hash=$hash file=checkpoint-0.tsv" | Set-Content -LiteralPath (Join-Path $GameDir 'openbeach.log')
'autotest: summary passed=1 failed=0'
'@ | Set-Content -LiteralPath (Join-Path $mock 'tools/autotest.ps1')
$golden = Join-Path $mock 'tests/golden-hashes/fixture.tsv'
$fixture = Join-Path $mock 'tests/fixture.txt'
$cli = Join-Path $mock 'tools/world_hash_golden.ps1'
$hashPrevious = $env:OBL_WORLD_HASH_DIR
try {
    $env:OBL_WORLD_HASH_DIR = 'inherited-directory'
    & $cli $fixture -Record | Out-Null
    Check ([int](Get-Content (Join-Path $mock 'run/calls.txt')) -eq 2) 'recording did not verify two runs'
    $result = Invoke-GameTest $mock 'fixture.golden' 'Debug' '' $true
    Check ($result.Result -eq 'PASS' -and $result.Summary -like '*IDENTICAL*' -and [int](Get-Content (Join-Path $mock 'run/calls.txt')) -eq 3) 'golden manifest should verify with one run even with -UpdateGolden'
    Check (@(Get-ChildItem (Join-Path $mock 'run/world-hash-check') -Directory).Count -eq 1) 'passing golden run left its artifacts (only the recording keeps them)'
    Set-Content -LiteralPath (Join-Path $mock 'run/mismatch') -Value ''
    $before = [IO.File]::ReadAllText($golden)
    $result = Invoke-GameTest $mock 'fixture.golden' 'Debug' '' $true
    Check ($result.Result -eq 'FAIL' -and $result.Summary -like '*golden hash mismatch*world-hash artifacts*') 'golden mismatch lost manifest failure/diagnostics'
    $artifact = Get-ChildItem (Join-Path $mock 'run/world-hash-check') -Directory -Filter 'golden-*' | Sort-Object LastWriteTime | Select-Object -Last 1
    Check ($artifact -and (Test-Path (Join-Path $artifact.FullName 'comparison.txt'))) 'failing golden run kept no artifacts'
    Check ([IO.File]::ReadAllText($golden) -ceq $before) '-UpdateGolden rewrote a golden trace'
    Check ($env:OBL_WORLD_HASH_DIR -ceq 'inherited-directory') 'golden failure leaked environment'
    Remove-Item -LiteralPath (Join-Path $mock 'run/mismatch')
    Set-Content -LiteralPath (Join-Path $mock 'run/unstable') -Value ''
    Reject { & $cli $fixture -Record } 'world states differ'
    Check ([IO.File]::ReadAllText($golden) -ceq $before) 'unstable recording overwrote golden trace'
    $callsBefore = [int](Get-Content (Join-Path $mock 'run/calls.txt'))
    Remove-Item -LiteralPath $golden
    $result = Invoke-GameTest $mock 'fixture.golden' 'Debug' '' $false
    Check ($result.Result -eq 'FAIL' -and [int](Get-Content (Join-Path $mock 'run/calls.txt')) -eq $callsBefore) 'missing golden should fail before launch'
} finally {
    $env:OBL_WORLD_HASH_DIR = $hashPrevious
}
"world-hash golden tool: $script:passed cases PASS; artifacts $root"
