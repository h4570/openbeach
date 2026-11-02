# Validate baseline diagnostics and a direct scripted host (no autotest restore to hide writes).
param([string]$Config = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $repo 'tools/common.ps1')
. (Join-Path $repo 'tools/testrun.ps1')
function Check([bool]$Ok, [string]$Message) {
    if (-not $Ok) { throw $Message }
    Write-Host "PASS $Message"
}
$slot = Initialize-GameSlot $repo 91
$ini = Join-Path $slot 'BeachLife.ini'
$stock = [IO.File]::ReadAllBytes($ini)
$minimal = "[Beach Life Options]`nVersion=1`n[Language]`nLanguage=5`n"
$launcher = $null
$audio = [Environment]::GetEnvironmentVariable('SDL_AUDIO_DRIVER')
try {
    foreach ($fixture in @(
        @{ Text = $minimal; Error = '' },
        @{ Text = $minimal.Replace('Language=5', 'Language=0'); Error = 'Language.Language' },
        @{ Text = $minimal + "[Sound]`nPlayListCount=0`n"; Error = 'Sound.PlayListCount' },
        @{ Text = $minimal + "[Sound]`nPlayListCount=11`n"; Error = 'missing Sound.PlayListTrack0' },
        @{ Text = ''; Error = 'Beach Life Options.Version' }
    )) {
        [IO.File]::WriteAllText($ini, $fixture.Text)
        $failure = ''
        try { Assert-TestGameIni $ini } catch { $failure = $_.Exception.Message }
        Check ($failure.Contains($fixture.Error) -and ([bool]$failure -eq [bool]$fixture.Error)) "baseline diagnostic '$($fixture.Error)'"
        if ($failure) { Check ($failure.Contains('Restore the file')) 'diagnostic says what to do' }
    }
    [IO.File]::WriteAllText($ini, $minimal + "[Sound]`nPlayListCount=0`n")
    $damagedHash = (Get-FileHash $ini).Hash
    $failure = ''
    try { & (Join-Path $repo 'tools/autotest.ps1') (Join-Path $repo 'tests/music_panel.txt') -GameDir $slot *>&1 | Out-Null }
    catch { $failure = $_.Exception.Message }
    Check ($failure.Contains('Damaged test INI') -and $failure.Contains('Sound.PlayListCount')) 'autotest rejects an empty playlist before launch'
    Check ((Get-FileHash $ini).Hash -eq $damagedHash) 'rejected INI stays untouched'
    [IO.File]::WriteAllBytes($ini, $stock)
    Assert-TestGameIni $ini
    $hash = (Get-FileHash $ini).Hash
    $env:SDL_AUDIO_DRIVER = 'dummy'
    $exe = Join-Path $repo "build/x64/bin/$Config/OpenBeach.exe"
    $test = Join-Path $repo 'tests/music_panel.txt'
    $launcher = Start-Process $exe -ArgumentList @('--game-dir', "`"$slot`"", '--script', "`"$test`"", '--hidden') -WindowStyle Hidden -PassThru
    $null = $launcher.Handle
    Check ($launcher.WaitForExit(120000)) 'direct host finishes before deadline'
    Check ($launcher.ExitCode -eq 0) 'direct host succeeds'
    $log = [IO.File]::ReadAllText((Join-Path $slot 'openbeach.log'))
    Check ($log.Contains('Settings: skip persistent save in scripted host')) 'music panel attempts a persistent settings save'
    Check ($log.Contains('autotest: PASS music-panel') -and $log -notmatch 'autotest: FAILED') 'music panel behavior passes'
    Check ((Get-FileHash $ini).Hash -eq $hash) 'direct scripted host preserves every INI byte'
} finally {
    Stop-RunGame $repo $slot $launcher
    [IO.File]::WriteAllBytes($ini, $stock)
    [Environment]::SetEnvironmentVariable('SDL_AUDIO_DRIVER', $(if ($null -eq $audio) { [NullString]::Value } else { $audio }))
}
