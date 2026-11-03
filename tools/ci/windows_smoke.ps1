# Smoke test of an unpacked Windows release (docs/build/releases.md): starts game\OpenBeach.exe hidden on a copy of its
# L0 game directory with the dummy audio driver, the way Demo.cmd does, waits until the main loop presents frames, then
# stops the host. The default game directory is checked too: without --game-dir the program must pick game\userdata.
#   ./tools/ci/windows_smoke.ps1 <unpacked release directory>
param([Parameter(Mandatory)][string]$Release)
$ErrorActionPreference = 'Stop'
$game = Join-Path (Resolve-Path $Release) 'game'
$exe = Join-Path $game 'OpenBeach.exe'
$work = Join-Path ([IO.Path]::GetTempPath()) "obl-smoke-$PID"
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
# The game writes its ini, log and saves into the game directory: a copy keeps the unpacked release untouched.
Copy-Item -LiteralPath (Join-Path $game 'l0') -Destination $work -Recurse
$log = Join-Path $work 'openbeach.log'

function Stop-Host([string]$dir) {
    $full = [IO.Path]::GetFullPath($dir).TrimEnd('\')
    Get-CimInstance Win32_Process -Filter "Name = 'OpenBeach.exe'" | Where-Object {
        $_.ExecutablePath -eq $exe -and $_.CommandLine -match '--standalone-host\s+--game-dir "([^"]+)"' -and
        [IO.Path]::GetFullPath($Matches[1]).TrimEnd('\') -eq $full
    } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

$env:OBL_PACK = 'l0'
$env:SDL_AUDIO_DRIVER = 'dummy'
$launcher = Start-Process -FilePath $exe -ArgumentList '--hidden', '--game-dir', "`"$work`"" -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $work 'stdout.log') -RedirectStandardError (Join-Path $work 'stderr.log')
try {
    # The intro movie, then the first frame of the menu: the presenter's texture is made by the first presented frame.
    $ready = $false
    for ($i = 0; $i -lt 600 -and -not $ready; $i++) {
        Start-Sleep -Milliseconds 100
        if ($launcher.HasExited) { break }
        if (Test-Path -LiteralPath $log) {
            $text = Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue
            $ready = $text -match 'RendererConstruct' -and $text -match 'sdl presenter: texture'
        }
    }
    if (-not $ready) {
        Get-Content -LiteralPath (Join-Path $work 'stdout.log'), (Join-Path $work 'stderr.log'), $log -ErrorAction SilentlyContinue
        throw 'the game did not reach its main loop'
    }
    Start-Sleep -Seconds 3 # a few seconds of frames in the menu
    if ($launcher.HasExited) { throw "the game stopped in the menu (exit code $($launcher.ExitCode))" }
    Select-String -LiteralPath $log -Pattern 'sdl window|sdl presenter|init done' | ForEach-Object { $_.Line }
} finally {
    Stop-Host $work
    if (-not $launcher.HasExited) { Stop-Process -InputObject $launcher -Force -ErrorAction SilentlyContinue }
    Remove-Item Env:OBL_PACK, Env:SDL_AUDIO_DRIVER -ErrorAction SilentlyContinue
}

# Without --game-dir the program picks game\userdata\ (empty until an import) and says the pack is missing.
$usage = & $exe --help 2>&1 | Out-String
if ($usage -notmatch 'userdata') { throw "the usage does not name the userdata default: $usage" }
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
Write-Host 'smoke test passed: window, frames in the menu'
exit 0 # --help leaves its own exit code 1 behind
