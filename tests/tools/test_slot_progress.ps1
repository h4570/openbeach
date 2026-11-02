# Slot reset must ignore user campaign progress and discard progress written by a previous test.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\..\tools\testrun.ps1')
$root = Join-Path ([IO.Path]::GetTempPath()) ('obl-slot-progress-' + [Guid]::NewGuid())
$slot = Join-Path $root 'run-slots\1'
New-Item -ItemType Directory -Force (Join-Path $root 'run'), $slot | Out-Null
try {
    $userProgress = Join-Path $root 'run\sp.bin'
    [IO.File]::WriteAllBytes($userProgress, [byte[]](1, 2, 3))
    foreach ($attempt in 1..2) {
        [IO.File]::WriteAllBytes((Join-Path $slot 'progress.json'), [byte[]](4, 5, 6))
        Reset-GameSlot $root 1
        if (Test-Path (Join-Path $slot 'progress.json')) { throw 'Slot retained scenario progress' }
        if ([Convert]::ToHexString([IO.File]::ReadAllBytes($userProgress)) -ne '010203') { throw 'User progress changed' }
    }
    Write-Host 'PASS: slot progress baseline and user progress preservation'
} finally {
    # The generated absolute target is a unique directory directly inside the system temp directory.
    if ([IO.Path]::GetDirectoryName($root) -ne [IO.Path]::GetTempPath().TrimEnd('\', '/')) { throw 'Unexpected cleanup target' }
    Remove-Item -LiteralPath $root -Recurse -Force
}
