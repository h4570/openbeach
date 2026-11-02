# Verify the system-memory display, JPEG decoding and final renderer cleanup.
param()
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'common.ps1')
try {
    New-Item -ItemType Directory -Force (Join-Path $repo 'run/testlogs') | Out-Null
    & (Join-Path $PSScriptRoot 'autotest.ps1') (Join-Path $repo 'tests/windowed_display.txt')
    $lines = Get-Content -LiteralPath (Join-Path $repo 'run/openbeach.log')
    if (-not ($lines -match 'autotest: PASS display-destroyed')) { throw 'renderer cleanup was not verified' }
    & python (Join-Path $PSScriptRoot 'check_display_jpegs.py')
    if ($LASTEXITCODE) { throw 'JPEG verification failed' }
    Copy-Item -LiteralPath (Join-Path $repo 'run/openbeach.log') -Destination (Join-Path $repo 'run/testlogs/display.log')
} finally {
    Stop-RunGame $repo
}
