# Local post-merge Linux gate. Requires WSL Ubuntu and the dependencies in docs/build/ci.md.
# Default: the fast compile gate (Clang unit tests of the portable part without the data. cases, the obl-sim build without
# conversion, then tools/wasm_linux.sh: the Emscripten synthetic unit tests and L0 scripts under Node; needs emsdk in
# ~/emsdk or $EMSDK). Everything native is x86-64.
# -Full: what the CI workflow runs on main (docs/build/ci.md, "Local post-merge check"): GCC and Clang
# synthetic unit cases, both sanitizer builds, clang-tidy on the lines changed since -TidyBase, and tools/linux_sim_check.sh (L0
# scripts, a converted copy of run/, data. oracles on that copy, the headless suite and the bots)
# and the wasm checks with their data steps on that converted copy. Main checkout only:
# WSL git cannot open a worktree's .git file (a Windows path).
param(
    [string]$Distribution = 'Ubuntu',
    [ValidateSet('gcc', 'clang', 'all')][string]$Compiler = 'clang',
    [switch]$Full,
    [string]$TidyBase = 'origin/main'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$logs = Join-Path $root "run/testlogs/linux-check/$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force $logs | Out-Null
$linuxRoot = & wsl.exe -d $Distribution -- wslpath -a -u $root.Replace('\', '/')
if ($LASTEXITCODE) { throw 'Cannot resolve the checkout in WSL' }
$linuxRoot = $linuxRoot.Trim()
# Include the checkout path so concurrent worktrees never share CMake caches.
$key = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($root))).Substring(0, 16)
$native = "/tmp/obl-linux-check-$key"
& wsl.exe -d $Distribution -- mkdir -p $native
if ($LASTEXITCODE) { throw 'Cannot create the native Linux build directory' }

$common = @("OBL_SOURCE_ROOT=$linuxRoot", "OBL_UNIT_BUILD_ROOT=$native/unit", "OBL_SIM_BUILD=$native/sim")
# ccache when installed: CMake takes the launcher from these variables when it configures a new build tree. The cache
# (~/.cache/ccache) is shared by every checkout; paths below the checkout hash relative, the build directory not at all.
& wsl.exe -d $Distribution --exec sh -c 'command -v ccache >/dev/null'
if (-not $LASTEXITCODE) {
    $common += @('CMAKE_C_COMPILER_LAUNCHER=ccache', 'CMAKE_CXX_COMPILER_LAUNCHER=ccache', "CCACHE_BASEDIR=$linuxRoot",
              'CCACHE_NOHASHDIR=1', 'CCACHE_MAXSIZE=20G')
}
# The Windows checkout has CRLF files; WSL git compares them with core.autocrlf so they do not all show as changed.
$common += @('GIT_CONFIG_COUNT=1', 'GIT_CONFIG_KEY_0=core.autocrlf', 'GIT_CONFIG_VALUE_0=true')

if ($Full) {
    $checks = @(
        @{ Name = 'unit'; Script = 'unit_linux.sh'; Args = @('all'); Env = @('OBL_UNIT_SKIP_DATA=1', "OBL_GAME_DIR=$native/game") },
        @{ Name = 'x64'; Script = 'unit_linux.sh'; Args = @('clang-x64'); Env = @('OBL_UNIT_SKIP_DATA=1') },
        @{ Name = 'sanitize'; Script = 'unit_sanitize.sh'; Args = @('all', '--', '-data.') },
        @{ Name = 'tidy'; Command = "cd '$linuxRoot' && python3 tools/tidy.py --base '$TidyBase' -j `$(nproc)" },
        @{ Name = 'sim'; Script = 'linux_sim_check.sh'; Args = @(); Env = @("OBL_SIM_GAME=$native/game", "OBL_L0_GAME_DIR=$native/l0-run") },
        @{ Name = 'wasm'; Script = 'wasm_linux.sh'; Args = @(); Env = @("OBL_WASM_BUILD=$native/wasm", "OBL_WASM_GAME=$native/game") })
} else {
    $common += @('OBL_UNIT_SKIP_DATA=1', 'OBL_SIM_SKIP_CONVERSION=1')
    $checks = @(@{ Name = 'x64'; Script = 'unit_linux.sh'; Args = @("$Compiler-x64".Replace('all-x64', 'clang-x64')) },
                @{ Name = 'unit'; Script = 'unit_linux.sh'; Args = @($Compiler) },
                @{ Name = 'sim'; Script = 'obl_sim_linux.sh'; Args = @() },
                @{ Name = 'wasm'; Script = 'wasm_linux.sh'; Args = @(); Env = @("OBL_WASM_BUILD=$native/wasm") })
}

$failed = $false
foreach ($check in $checks) {
    $log = Join-Path $logs "$($check.Name).log"
    $vars = $common + @($check.Env | Where-Object { $_ })
    if ($check.Command) {
        & wsl.exe -d $Distribution --exec env @vars bash -c $check.Command *> $log
    } else {
        $copy = Join-Path $logs $check.Script
        [IO.File]::WriteAllText($copy, ([IO.File]::ReadAllText((Join-Path $PSScriptRoot $check.Script))).Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
        $linuxCopy = & wsl.exe -d $Distribution -- wslpath -a -u $copy.Replace('\', '/')
        if ($LASTEXITCODE) { throw 'Cannot resolve the LF script copy in WSL' }
        & wsl.exe -d $Distribution --exec env @vars bash $linuxCopy.Trim() @($check.Args) *> $log
    }
    $code = $LASTEXITCODE
    if ($code) { $failed = $true }
    $result = if ($code) { 'FAIL' } else { 'PASS' }
    Write-Host "$result Linux $($check.Name) (exit $code): $log"
}
if ($failed) { exit 1 }
exit 0
