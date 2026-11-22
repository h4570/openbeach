# The INI helpers read BeachLife.ini as Latin-1. [Text.Encoding]::Latin1 is null in Windows PowerShell 5.1, so the
# helpers must run in both editions: this runs Assert-TestGameIni in each one that is installed.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$root = Join-Path $repo ('run/tmp/latin1-ini-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root | Out-Null
try {
    $ini = Join-Path $root 'BeachLife.ini'
    [IO.File]::WriteAllBytes($ini, [Text.Encoding]::GetEncoding(28591).GetBytes("[Beach Life Options]`r`nVersion=1`r`n[Language]`r`nLanguage=5`r`n; caf$([char]0xE9)`r`n"))
    $ran = 0
    foreach ($host_ in 'powershell', 'pwsh') {
        if (-not (Get-Command $host_ -ErrorAction SilentlyContinue)) { continue }
        $command = ". '$(Join-Path $repo 'tools/common.ps1')'; Assert-TestGameIni '$ini'"
        & $host_ -NoProfile -ExecutionPolicy Bypass -Command $command
        if ($LASTEXITCODE) { throw "Assert-TestGameIni failed in $host_" }
        $ran++
    }
    if (-not $ran) { throw 'no PowerShell edition found' }
    Write-Host "PASS latin1 ini ($ran editions)"
} finally {
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}
