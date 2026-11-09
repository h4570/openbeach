# Automated visual check: starts the game hidden (off-screen, never focused), captures the game
# window at the given times (seconds after start) and closes the game.
#   ./tools/capture.ps1                          # 5s and 35s -> run/captures/shot_05.png, shot_35.png
#   ./tools/capture.ps1 -At 3,10,40 -Visible     # normal visible window
param(
    [int[]]$At = @(5, 35),
    [string]$OutDir = (Join-Path $PSScriptRoot '..\run\captures'),
    [ValidateSet('Debug', 'RelWithDebInfo')][string]$Config = 'Debug',
    [switch]$Visible
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
. (Join-Path $PSScriptRoot 'common.ps1')
$launcher = Join-Path $repo "build\x64\bin\$Config\OpenBeach.exe"

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class OblCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@

function Save-WindowShot([string]$path) {
    $p = Get-RunGame $repo | Select-Object -First 1
    if (-not $p -or $p.MainWindowHandle -eq 0) { Write-Warning "no game window for $path"; return }
    $r = New-Object OblCapture+RECT
    [void][OblCapture]::GetWindowRect($p.MainWindowHandle, [ref]$r)
    $bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $r.R - $r.L)), ([Math]::Max(1, $r.B - $r.T))
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][OblCapture]::PrintWindow($p.MainWindowHandle, $hdc, 2) # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "captured $path"
}

Stop-RunGame $repo
New-Item -ItemType Directory -Force $OutDir | Out-Null
# A normal session saves settings (including its playlist); capture must leave the test baseline intact.
$ini = Join-Path $repo 'run/BeachLife.ini'
$iniBytes = if (Test-Path -LiteralPath $ini) { [IO.File]::ReadAllBytes($ini) } else { $null }
$launcherProcess = $null
try {
    $launchArgs = @('--no-wait', '--keep-saves', '--no-settings', '--game-dir', "`"$(Join-Path $repo 'run')`"")
    if (-not $Visible) { $launchArgs += '--hidden' }
    # Do not attach the native launcher/host to PowerShell's pipeline handles. Keep diagnostics in files.
    $launcherProcess = Start-Process $launcher -ArgumentList $launchArgs -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $OutDir 'launcher.stdout.log') `
        -RedirectStandardError (Join-Path $OutDir 'launcher.stderr.log')
    $null = $launcherProcess.Handle
    if (-not $launcherProcess.WaitForExit(30000)) { throw 'launcher timed out after 30 s' }
    if ($launcherProcess.ExitCode) { throw "launcher failed ($($launcherProcess.ExitCode))" }

    $start = Get-Date
    foreach ($t in ($At | Sort-Object)) {
        $wait = $t - ((Get-Date) - $start).TotalSeconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
        Save-WindowShot (Join-Path $OutDir ('shot_{0:d2}.png' -f $t))
    }
} finally {
    try { Stop-RunGame $repo -LauncherProcess $launcherProcess }
    finally {
        if ($null -ne $iniBytes) { [IO.File]::WriteAllBytes($ini, $iniBytes) }
        elseif (Test-Path -LiteralPath $ini) { Remove-Item -LiteralPath $ini }
    }
}
