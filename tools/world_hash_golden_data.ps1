# Compact schema-3 traces. Full field snapshots remain local diagnostics, never golden data.
function Assert-GoldenHashSequence($Rows, [string]$Source) {
    if (-not $Rows.Count) { throw "no world-hash checkpoints in $Source" }
    [ulong]$previous = 0
    for ($i = 0; $i -lt $Rows.Count; ++$i) {
        if ($Rows[$i].Index -cne [string]$i -or [ulong]$Rows[$i].Steps -lt $previous -or [ulong]$Rows[$i].Fields -eq 0) {
            throw "invalid checkpoint sequence in $Source at row $i"
        }
        $previous = [ulong]$Rows[$i].Steps
    }
}

function Read-GoldenHashLog([string]$Path) {
    $rows = @(foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -notmatch 'autotest: world-hash schema=') { continue }
        if ($line -cnotmatch 'autotest: world-hash schema=3 checkpoint=(\d+) steps=(\d+) fields=(\d+) hash=([0-9a-f]{16}) file=checkpoint-(\d+)\.tsv$') {
            throw "invalid world-hash report/schema in $Path"
        }
        if ($Matches[1] -cne $Matches[5]) { throw "invalid checkpoint filename in $Path" }
        [pscustomobject]@{ Index=$Matches[1]; Steps=$Matches[2]; Fields=$Matches[3]; Hash=$Matches[4] }
    })
    Assert-GoldenHashSequence $rows $Path
    return $rows
}

function Read-GoldenHashTrace([string]$Path) {
    $lines = @(Get-Content -LiteralPath $Path)
    if ($lines.Count -lt 2 -or $lines[0] -cne 'OBL-world-v3' -or $lines[1] -cne "index`tsteps`tfields`thash") {
        throw "invalid golden hash header/schema in $Path"
    }
    $rows = @(foreach ($line in $lines | Select-Object -Skip 2) {
        if ($line -cnotmatch '^(\d+)\t(\d+)\t(\d+)\t([0-9a-f]{16})$') { throw "invalid golden hash row in $Path" }
        [pscustomobject]@{ Index=$Matches[1]; Steps=$Matches[2]; Fields=$Matches[3]; Hash=$Matches[4] }
    })
    Assert-GoldenHashSequence $rows $Path
    return $rows
}

function Write-GoldenHashTrace($Rows, [string]$Path) {
    Assert-GoldenHashSequence $Rows $Path
    $lines = @('OBL-world-v3', "index`tsteps`tfields`thash") + @($Rows | ForEach-Object { "$($_.Index)`t$($_.Steps)`t$($_.Fields)`t$($_.Hash)" })
    [IO.File]::WriteAllLines($Path, [string[]]$lines, [Text.UTF8Encoding]::new($false))
}

function Compare-GoldenHashTrace($Reference, $Candidate) {
    if ($Reference.Count -ne $Candidate.Count) { throw "golden hash checkpoint count differs: reference=$($Reference.Count), candidate=$($Candidate.Count)" }
    for ($i = 0; $i -lt $Reference.Count; ++$i) {
        foreach ($field in 'Index', 'Steps', 'Fields', 'Hash') {
            if ($Reference[$i].$field -cne $Candidate[$i].$field) {
                throw "golden hash mismatch: checkpoint=$i steps=$($Candidate[$i].Steps) $field reference=$($Reference[$i].$field) candidate=$($Candidate[$i].$field)"
            }
        }
    }
    "world-hash golden: $($Reference.Count) checkpoints, $($Candidate[-1].Steps) step frames, IDENTICAL"
}
