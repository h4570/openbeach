# Compare ordered checkpoints; at the first differing one show the first differing serialized field and every differing section with
# its differing record count (docs/testing/world-hashes.md). No game process required.
param(
    [Parameter(Mandatory)][string]$Reference,
    [Parameter(Mandatory)][string]$Candidate
)
$ErrorActionPreference = 'Stop'
function Checkpoints([string]$Directory) {
    $pattern = 'autotest: world-hash schema=(\d+) checkpoint=(\d+) steps=(\d+) fields=(\d+) hash=([0-9a-f]{16}) file=(checkpoint-\d+\.tsv)$'
    $result = @(Get-Content -LiteralPath (Join-Path $Directory 'game.log') | ForEach-Object {
        if ($_ -match $pattern) {
            [pscustomobject]@{ Schema=$Matches[1]; Index=$Matches[2]; Steps=$Matches[3]; Fields=$Matches[4]; Hash=$Matches[5]; File=$Matches[6] }
        }
    })
    if (-not $result.Count) { throw "no world-hash checkpoints in $Directory" }
    for ($i = 0; $i -lt $result.Count; ++$i) {
        if ($result[$i].Schema -notin @('1', '2', '3') -or $result[$i].Schema -ne $result[0].Schema -or $result[$i].Index -ne [string]$i) { throw "invalid checkpoint sequence/schema in $Directory" }
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $result[$i].File))) { throw "missing snapshot $($result[$i].File) in $Directory" }
    }
    return $result
}
# The section of every record, as obl::SectionClassifier (src/game/world/world_sections.cpp) assigns it; the shared fixture in
# tests/unit/world_sections_test.cpp and tests/tools/test_world_hash_compare.ps1 keeps the two in step.
function SectionOf([string]$Class, [string]$Field, [string]$Value, [hashtable]$State) {
    if ($Field -ceq 'class' -or $Class -cne $State.Class) { $State.Class = $Class; $State.Object = 'objects'; $State.Weather = $false }
    switch -CaseSensitive ($Class) {
        'World' { if ($Field -cmatch 'income|cost|profit|fine') { return 'economy' } return 'world' }
        'Player' { return 'economy' }
        'WorldMap' { return 'map' }
        'Path' { return 'map' }
        'ObjectManager' {
            if ($Field -ceq 'object.type') {
                $State.Object = 'objects'
                if ($Value -match '^[0-9a-f]{2}$') {
                    $type = [Convert]::ToInt32($Value, 16)
                    $State.Object = if ($type -eq 1) { 'scenery' } elseif ($type -in 2, 5) { 'buildings' } elseif ($type -eq 3) { 'guests' } `
                        elseif ($type -eq 4) { 'vehicles' } elseif ($type -ge 6 -and $type -le 20 -and $type -ne 13) { 'staff' } else { 'objects' }
                }
            }
            return $State.Object
        }
        'ConstructionManager' { if ($Field -ceq 'light_index') { $State.Weather = $true }; if ($State.Weather) { return 'weather' } return 'construction' }
        'FerryManager' { return 'ferry' }
        'RepairManager' { return 'repair' }
        'FeedbackManager' { return 'feedback' }
        'MessageHandler' { return 'messages' }
        'CleaningManager' { return 'cleaning' }
        'RNG' { return 'rng' }
    }
    return 'other'
}
$script:SectionOrder = @('world', 'economy', 'map', 'objects', 'scenery', 'buildings', 'guests', 'staff', 'vehicles', 'ferry', 'repair',
    'feedback', 'messages', 'cleaning', 'construction', 'weather', 'rng', 'other')
# Records of a snapshot grouped by section (record 0, the schema header, belongs to none): section -> list of [record index, line].
function SectionRecords([string[]]$Lines) {
    $result = @{}
    $state = @{ Class = $null; Object = 'objects'; Weather = $false }
    for ($j = 1; $j -lt $Lines.Count; ++$j) {
        if ($Lines[$j] -eq '') { continue }
        $columns = $Lines[$j] -split "`t"
        $section = SectionOf $columns[0] $(if ($columns.Count -gt 1) { $columns[1] } else { '' }) $(if ($columns.Count -gt 3) { $columns[3] } else { '' }) $state
        if (-not $result.ContainsKey($section)) { $result[$section] = [System.Collections.Generic.List[object]]::new() }
        $result[$section].Add(@($j, $Lines[$j]))
    }
    return $result
}
# One line per differing section: its differing record count (compared position by position inside the section, plus the records one side
# lacks), both field counts and its first differing record.
function SectionReport([string[]]$FieldsA, [string[]]$FieldsB) {
    $left = SectionRecords $FieldsA; $right = SectionRecords $FieldsB
    $lines = @(); $names = @()
    foreach ($section in $script:SectionOrder) {
        # Plain assignments: a list returned from an if-expression would be unrolled by the pipeline.
        $ra = $left[$section]; if ($null -eq $ra) { $ra = [System.Collections.Generic.List[object]]::new() }
        $rb = $right[$section]; if ($null -eq $rb) { $rb = [System.Collections.Generic.List[object]]::new() }
        $differing = [Math]::Abs($ra.Count - $rb.Count); $first = $null
        for ($k = 0; $k -lt [Math]::Min($ra.Count, $rb.Count); ++$k) {
            if ($ra[$k][1] -ceq $rb[$k][1]) { continue }
            ++$differing
            if ($null -eq $first) { $first = $ra[$k] }
        }
        if ($null -eq $first -and $differing) {
            if ($ra.Count -gt $rb.Count) { $first = $ra[$rb.Count] } else { $first = $rb[$ra.Count] }
        }
        if (-not $differing) { continue }
        $names += $section
        $columns = $first[1] -split "`t"
        $lines += "section $section differing=$differing fields reference=$($ra.Count) candidate=$($rb.Count) first: record $($first[0]) $($columns[0]) $($columns[1])"
    }
    return @("differing sections: $(if ($names) { $names -join ',' } else { 'none (order across sections)' })") + $lines
}
function Fail([string[]]$Lines) {
    $Lines | Set-Content -LiteralPath (Join-Path $Candidate 'comparison.txt') -Encoding utf8
    $Lines | ForEach-Object { Write-Host $_ }
    throw 'world states differ; see comparison.txt and checkpoint snapshots'
}
$a = @(Checkpoints $Reference)
$b = @(Checkpoints $Candidate)
if ($a.Count -ne $b.Count) { Fail @("checkpoint count differs: reference=$($a.Count), candidate=$($b.Count)") }
for ($i = 0; $i -lt $a.Count; ++$i) {
    $left = $a[$i]; $right = $b[$i]
    if ($left.Schema -ne $right.Schema) { throw 'invalid checkpoint sequence/schema: profiles use different schemas' }
    if ($left.Steps -ne $right.Steps) { Fail @("checkpoint $i step differs: reference=$($left.Steps), candidate=$($right.Steps)") }
    $textA = [IO.File]::ReadAllText((Join-Path $Reference $left.File))
    $textB = [IO.File]::ReadAllText((Join-Path $Candidate $right.File))
    if ($left.Hash -eq $right.Hash -and $left.Fields -eq $right.Fields -and $textA -ceq $textB) { continue }
    $lines = @("first mismatch: checkpoint=$i steps=$($left.Steps) reference=$($left.Hash) candidate=$($right.Hash)")
    $fieldsA = $textA -split "`n"; $fieldsB = $textB -split "`n"
    for ($j = 0; $j -lt [Math]::Max($fieldsA.Count, $fieldsB.Count); ++$j) {
        $rowA = if ($j -lt $fieldsA.Count) { $fieldsA[$j] } else { '<missing>' }
        $rowB = if ($j -lt $fieldsB.Count) { $fieldsB[$j] } else { '<missing>' }
        if ($rowA -ceq $rowB) { continue }
        $lines += "record $j (schema header is record 0)"
        $lines += 'reference: ' + $rowA.Substring(0, [Math]::Min(240, $rowA.Length))
        $lines += 'candidate: ' + $rowB.Substring(0, [Math]::Min(240, $rowB.Length))
        # Keep the byte location useful even for large arrays whose common prefix exceeds the preview.
        $offset = 0
        while ($offset -lt [Math]::Min($rowA.Length, $rowB.Length) -and $rowA[$offset] -ceq $rowB[$offset]) { ++$offset }
        $lines += "first differing character=$offset; full records in $($left.File)"
        break
    }
    $lines += SectionReport $fieldsA $fieldsB
    Fail $lines
}
$summary = "world-hash: $($a.Count) checkpoints, $($a[-1].Steps) step frames, IDENTICAL (hashes and field snapshots)"
$summary | Set-Content -LiteralPath (Join-Path $Candidate 'comparison.txt') -Encoding utf8
$summary
