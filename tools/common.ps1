# Helpers shared by tools/*.ps1 (dot-source: . "$PSScriptRoot\common.ps1").
# Several checkouts can run tests at once (docs/testing/test-runner.md), each with its own run/.
# Processes are therefore always identified by the exe path inside THIS checkout's run/, never by name only,
# otherwise one checkout's test run would kill the game of another. Parallel test runs (runtests.ps1 -Jobs N) use extra
# game directories (run-slots/<n>/, tools/testrun.ps1): pass that directory as $GameDir and only its game is touched.

function Get-RunGame([string]$Repo, [string]$GameDir = '') {
    if (-not $GameDir) { $GameDir = Join-Path $Repo 'run' }
    $exe = [IO.Path]::GetFullPath((Join-Path $GameDir 'BeachLife.exe'))
    Get-Process BeachLife -ErrorAction SilentlyContinue | Where-Object {
        try { $_.Path -eq $exe } catch { $false }
    }
    # System.Diagnostics.Process has no CommandLine property. CIM provides the host's arguments,
    # including its directory when several slots share the same executable.
    $dir = [IO.Path]::GetFullPath($GameDir).TrimEnd('\')
    $hosts = @(
        (Join-Path $Repo 'build/x64/bin/Debug/OpenBeach.exe'),
        (Join-Path $Repo 'build/x64/bin/RelWithDebInfo/OpenBeach.exe'),
        (Join-Path $GameDir 'OpenBeach.exe')
    ) | ForEach-Object { [IO.Path]::GetFullPath($_) }
    Get-CimInstance Win32_Process -Filter "Name = 'OpenBeach.exe'" | Where-Object {
        $hosts -contains $_.ExecutablePath -and
        $_.CommandLine -match '(?:^|\s)--standalone-host\s+--game-dir "([^"]+)"(?:\s|$)' -and
        [IO.Path]::GetFullPath($Matches[1]).TrimEnd('\') -eq $dir
    } | ForEach-Object {
        Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue
    }
}

function Stop-RunGame([string]$Repo, [string]$GameDir = '', [Diagnostics.Process]$LauncherProcess = $null) {
    # Stop the retained launcher first so it cannot create a new host after the directory scan.
    # Callers pass only the handle returned by their own Start-Process, never a name-based lookup.
    if ($LauncherProcess -and -not $LauncherProcess.HasExited) {
        Stop-Process -InputObject $LauncherProcess -Force -ErrorAction SilentlyContinue
    }
    Get-RunGame $Repo $GameDir | Stop-Process -Force -ErrorAction SilentlyContinue
}

# A per-scenario script tests/<family>_s<n>.txt without a file of its own is generated from the family template
# tests/<family>_sN.txt, every `{scenario}` replaced by <n> (docs/testing/test-runner.md). The copy is written to
# <GameDir>/testscripts/ (per game slot, git-ignored) and its full path returned; an existing script comes back as it is.
function Resolve-TestScript([string]$Script, [string]$GameDir) {
    $full = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Script)
    if (Test-Path -LiteralPath $full) { return $full }
    $name = [IO.Path]::GetFileName($full)
    if ($name -notmatch '^(.+_s)(\d+)\.txt$') { throw "test script not found: $Script" }
    $family = $Matches[1]; $scenario = $Matches[2]
    $template = Join-Path ([IO.Path]::GetDirectoryName($full)) "${family}N.txt"
    if (-not (Test-Path -LiteralPath $template)) { throw "test script not found: $Script (no template ${family}N.txt)" }
    $text = [IO.File]::ReadAllText($template)
    if ($text -notmatch '\{scenario\}') { throw "template $template has no {scenario}" }
    $dir = Join-Path $GameDir 'testscripts'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $path = Join-Path $dir $name
    [IO.File]::WriteAllText($path, ($text -replace '\{scenario\}', $scenario))
    $path
}

function Assert-TestGameIni([string]$Path) {
    $values = @{}; $section = ''
    if (Test-Path -LiteralPath $Path) {
        foreach ($line in [IO.File]::ReadAllLines($Path, [Text.Encoding]::Latin1)) {
            if ($line -match '^\s*\[([^]]+)\]\s*$') { $section = $Matches[1] }
            elseif ($line -match '^\s*([^=]+?)\s*=(.*)$') { $values["$section.$($Matches[1])"] = $Matches[2].Trim() }
        }
    }
    $problems = @()
    foreach ($pair in @(@('Beach Life Options.Version', '1'), @('Language.Language', '5'))) {
        if ($values[$pair[0]] -ne $pair[1]) { $problems += "$($pair[0]): expected $($pair[1]), got '$($values[$pair[0]])'" }
    }
    # No count selects the built-in soundtrack (the installation baseline). An explicit zero empties it.
    if ($values.ContainsKey('Sound.PlayListCount')) {
        if ($values['Sound.PlayListCount'] -ne '11') { $problems += "Sound.PlayListCount: expected 11, got '$($values['Sound.PlayListCount'])'" }
        foreach ($i in 0..10) {
            foreach ($key in 'PlayListTrack', 'PlayListDirectory', 'PlayListAttribute') {
                if ([string]::IsNullOrWhiteSpace($values["Sound.$key$i"])) { $problems += "missing Sound.$key$i" }
            }
        }
    }
    if ($problems) {
        throw "Damaged test INI '$Path': $($problems -join '; '). Restore the file before running tests. Test @gameini overrides are applied after this check."
    }
}
