# No-game check with a host that mutates its INI and stays alive until capture stops it.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $repo 'tools/common.ps1')
$root = Join-Path $repo ('run/tmp/capture-check-' + [guid]::NewGuid().ToString('N'))
$bin = Join-Path $root 'build/x64/bin/Debug'
$toolDir = Join-Path $root 'tools'
$game = Join-Path $root 'run'
New-Item -ItemType Directory -Force $bin, $toolDir, $game | Out-Null
Copy-Item (Join-Path $repo 'tools/capture.ps1'), (Join-Path $repo 'tools/common.ps1') $toolDir
$csc = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $csc /nologo /target:exe "/out:$(Join-Path $bin 'OpenBeach.exe')" (Join-Path $PSScriptRoot 'fixtures/capture_host.cs')
if ($LASTEXITCODE) { throw 'fixture compilation failed' }
$ini = Join-Path $game 'BeachLife.ini'
Set-Content $ini "[Beach Life Options]`r`nVersion=1`r`n[Language]`r`nLanguage=5`r`n; preserved bytes: café"
$hash = (Get-FileHash $ini).Hash
$worker = $null
try {
    foreach ($mode in 'pipe', 'redirect', 'failure') {
        Remove-Item (Join-Path $game 'ready') -ErrorAction SilentlyContinue
        $driver = Join-Path $root "$mode.ps1"
        $invoke = "& '$toolDir/capture.ps1' -At 0 -OutDir '$root/$mode'"
        if ($mode -eq 'pipe') { $invoke += ' | Out-String | Write-Output' }
        else { $invoke += " *> '$root/capture.log'" }
        if ($mode -eq 'failure') { $invoke = "`$env:CAPTURE_FAIL='1'; $invoke" }
        Set-Content $driver "`$ErrorActionPreference='Stop'; $invoke"
        $psi = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        foreach ($arg in @('-NoProfile', '-File', $driver)) { $psi.ArgumentList.Add($arg) }
        $worker = [Diagnostics.Process]::Start($psi)
        $stdout = $worker.StandardOutput.ReadToEndAsync()
        $stderr = $worker.StandardError.ReadToEndAsync()
        if (-not $worker.WaitForExit(45000)) { throw "$mode capture did not return" }
        if (-not $stdout.Wait(2000) -or -not $stderr.Wait(2000)) { throw "$mode child holds caller output handle" }
        if (($worker.ExitCode -eq 0) -ne ($mode -ne 'failure')) { throw "$mode failed: $($stderr.Result)" }
        if (-not (Test-Path (Join-Path $game 'ready'))) { throw "$mode did not launch the mutating host" }
        if ($mode -eq 'failure' -and $stderr.Result -notmatch 'launcher failed \(7\)') { throw 'launch failure diagnostic missing' }
        if ((Get-FileHash $ini).Hash -ne $hash) { throw "$mode changed INI bytes" }
        Assert-TestGameIni $ini
        if (Get-RunGame $root) { throw "$mode left a host running" }
        Write-Host "PASS $mode returns, closes handles, preserves runner-compatible INI"
    }
} finally {
    if ($worker -and -not $worker.HasExited) { $worker.Kill() }
    Stop-RunGame $root
}

