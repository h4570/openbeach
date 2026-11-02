# Checks runner configuration precedence without building or launching the game.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'testrun.ps1')
$fixture = Join-Path $PSScriptRoot '../tests/_runner_config_fixture.txt'
if (Test-Path -LiteralPath $fixture) { throw "fixture already exists: $fixture" }
function Check-Config([string]$Entry, [string]$Requested, [string]$Expected) {
    $actual = Resolve-TestConfig $Entry $Requested
    if ($actual -ne $Expected) { throw "$Entry / $Requested : expected $Expected, got $actual" }
    Write-Host "PASS $Entry / $Requested -> $actual"
}
try {
    Set-Content -LiteralPath $fixture -Value '# no configuration header'
    Check-Config '_runner_config_fixture.txt' Auto Debug
    Check-Config '_runner_config_fixture.golden' Auto RelWithDebInfo
    Check-Config 'unit.ctest' Auto Debug
    Set-Content -LiteralPath $fixture -Value @('# ordinary comment', '  # @config rel  ', 'quit')
    Check-Config '_runner_config_fixture.txt' Auto RelWithDebInfo
    Check-Config '_runner_config_fixture.txt' Debug Debug
    Set-Content -LiteralPath $fixture -Value '# @config debug'
    Check-Config '_runner_config_fixture.txt' Auto Debug
    Check-Config '_runner_config_fixture.golden' Auto Debug
    Check-Config '_runner_config_fixture.golden' RelWithDebInfo RelWithDebInfo
    Check-Config 'unit.ctest' RelWithDebInfo RelWithDebInfo
    Set-Content -LiteralPath $fixture -Value '# @configuration rel'
    Check-Config '_runner_config_fixture.txt' Auto Debug
    Check-Config '_runner_config_fixture.txt' RelWithDebInfo RelWithDebInfo
    Set-Content -LiteralPath $fixture -Value @('# @config debug', '# @config rel')
    Check-Config '_runner_config_fixture.txt' Auto Debug
    Push-Location ([IO.Path]::GetTempPath())
    try {
        Check-Config '_runner_config_fixture.golden' Auto Debug
    } finally {
        Pop-Location
    }
} finally {
    Remove-Item -LiteralPath $fixture -Force
}
