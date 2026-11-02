# No-game contract tests for the comparison tool, including equal hashes with different payloads.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compare = Join-Path $repo 'tools/world_hash_compare.ps1'
$root = Join-Path $repo ('run/testlogs/world-hash-tool-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$script:passed = 0
function Fixture([string]$Name, [string]$Hash='0123456789abcdef', [int]$Steps=100, [int]$Index=0, [int]$Schema=1) {
    $dir = Join-Path $root $Name
    New-Item -ItemType Directory -Path $dir | Out-Null
    "autotest: world-hash schema=$Schema checkpoint=$Index steps=$Steps fields=1 hash=$Hash file=checkpoint-$Index.tsv" |
        Set-Content -LiteralPath (Join-Path $dir 'game.log')
    "BLRE-world-v$Schema`nPlayer`tCash`t3`t0000803f`n" | Set-Content -LiteralPath (Join-Path $dir "checkpoint-$Index.tsv")
    return $dir
}
function Reject([string]$Reference, [string]$Candidate, [string]$Reason) {
    $rejected = $false
    try { & $compare -Reference $Reference -Candidate $Candidate | Out-Null }
    catch { $rejected = $_.Exception.Message -like "*$Reason*" }
    if (-not $rejected) { throw "expected rejection: $Reason" }
    ++$script:passed
}
$a = Fixture 'reference'
$b = Fixture 'identical'
$result = & $compare -Reference $a -Candidate $b
if ($result -notlike '*IDENTICAL*') { throw 'identical snapshots were not accepted' }
++ $script:passed
$v2 = Fixture 'reference-v2' -Schema 2
$v2b = Fixture 'identical-v2' -Schema 2
$result = & $compare -Reference $v2 -Candidate $v2b
if ($result -notlike '*IDENTICAL*') { throw 'schema 2 was not accepted' }
++ $script:passed
$b = Fixture 'changed' 'fedcba9876543210'
"BLRE-world-v1`nPlayer`tCash`t3`t00000040`n" | Set-Content -LiteralPath (Join-Path $b 'checkpoint-0.tsv')
Reject $a $b 'world states differ'
if ((Get-Content -LiteralPath (Join-Path $b 'comparison.txt') -Raw) -notmatch 'Player\s+Cash') { throw 'missing field diagnosis' }
$b = Fixture 'collision'
"BLRE-world-v1`nPlayer`tCash`t3`t00000040`n" | Set-Content -LiteralPath (Join-Path $b 'checkpoint-0.tsv')
Reject $a $b 'world states differ'
$b = Fixture 'wrong-step' -Steps 101
Reject $a $b 'world states differ'
$b = Fixture 'wrong-order' -Index 1
Reject $a $b 'invalid checkpoint sequence'
$b = Fixture 'wrong-schema' -Schema 2
Reject $a $b 'invalid checkpoint sequence'
$b = Fixture 'unknown-schema' -Schema 3
Reject $a $b 'invalid checkpoint sequence'
$b = Fixture 'missing-file'
Remove-Item -LiteralPath (Join-Path $b 'checkpoint-0.tsv')
Reject $a $b 'missing snapshot'
$b = Fixture 'empty'
Set-Content -LiteralPath (Join-Path $b 'game.log') -Value ''
Reject $a $b 'no world-hash checkpoints'
$b = Fixture 'wrong-count'
Add-Content -LiteralPath (Join-Path $b 'game.log') -Value 'autotest: world-hash schema=1 checkpoint=1 steps=200 fields=1 hash=0123456789abcdef file=checkpoint-1.tsv'
Copy-Item -LiteralPath (Join-Path $b 'checkpoint-0.tsv') -Destination (Join-Path $b 'checkpoint-1.tsv')
Reject $a $b 'world states differ'
# Sections: the same records as tests/unit/world_sections_test.cpp, whose section counts the tool must reproduce.
$sectioned = @(
    "World`tclass`t0`t576f726c64", "World`tgame_time`t3`t367f1444", "World`tincome_today`t3`t00607d44",
    "World`taverage_cost`t3`t00000000", "World`ttotal_fines`t3`t00000000", "World`taverage_beauty`t3`t00000000",
    "WorldMap`tclass`t0`t576f726c644d6170", "WorldMap`tflags[]`t7`t0000", "Path`tclass`t0`t50617468",
    "ObjectManager`tclass`t0`t4f626a6563744d616e61676572", "ObjectManager`tnext_id`t4`t10000000",
    "ObjectManager`tobject.type`t9`t01", "ObjectManager`tbase.id`t4`t01000000", "ObjectManager`tobject.type`t9`t02",
    "ObjectManager`tbase.id`t4`t02000000", "ObjectManager`tbuilding.open_status`t2`t01", "ObjectManager`tobject.type`t9`t05",
    "ObjectManager`tobject.type`t9`t03", "ObjectManager`tguest.cash`t3`t00002041", "ObjectManager`tobject.type`t9`t04",
    "ObjectManager`tobject.type`t9`t0b", "ObjectManager`tstaff.wage`t3`t00002042", "ObjectManager`tobject.type`t9`t0d",
    "ObjectManager`tobject.type`t9`t14", "Player`tclass`t0`t506c61796572", "Player`tCash`t3`t0000803f",
    "FerryManager`tclass`t0`t00", "RepairManager`tclass`t0`t00", "FeedbackManager`tclass`t0`t00", "MessageHandler`tclass`t0`t00",
    "CleaningManager`tclass`t0`t00", "ConstructionManager`tclass`t0`t00", "ConstructionManager`tbuilders`t4`t01000000",
    "ConstructionManager`tlight_index`t4`t00000000", "ConstructionManager`tstate`t4`t00000000", "Mystery`tclass`t0`t00",
    "RNG`tclass`t0`t524e47", "RNG`trand-calls`t14`t0200000000000000")
function SnapshotFixture([string]$Name, [string[]]$Records, [string]$Hash) {
    $dir = Join-Path $root $Name
    New-Item -ItemType Directory -Path $dir | Out-Null
    "autotest: world-hash schema=3 checkpoint=0 steps=100 fields=$($Records.Count) hash=$Hash file=checkpoint-0.tsv" |
        Set-Content -LiteralPath (Join-Path $dir 'game.log')
    [IO.File]::WriteAllText((Join-Path $dir 'checkpoint-0.tsv'), "OBL-world-v3`n" + (($Records | ForEach-Object { "$_`n" }) -join ''))
    return $dir
}
$a = SnapshotFixture 'sections-reference' $sectioned '1111111111111111'
# Against an empty snapshot every section differs by all its records: the counts are the classification.
$b = SnapshotFixture 'sections-empty' @() '2222222222222222'
Reject $a $b 'world states differ'
$report = Get-Content -LiteralPath (Join-Path $b 'comparison.txt')
$expected = [ordered]@{ world = 3; economy = 5; map = 3; objects = 3; scenery = 2; buildings = 4; guests = 2; staff = 3; vehicles = 1; ferry = 1
    repair = 1; feedback = 1; messages = 1; cleaning = 1; construction = 2; weather = 2; rng = 2; other = 1 }
if ($report -notcontains "differing sections: $($expected.Keys -join ',')") { throw "wrong section list: $($report -join ' | ')" }
foreach ($section in $expected.Keys) {
    if (-not ($report -match "^section $section differing=$($expected[$section]) fields reference=$($expected[$section]) candidate=0 ")) {
        throw "wrong count for section $section"
    }
}
++$script:passed
# A changed guest and a new staff record: exactly those two sections, with their counts and first records.
$changed = [System.Collections.Generic.List[string]]::new([string[]]$sectioned)
$changed[$changed.IndexOf("ObjectManager`tguest.cash`t3`t00002041")] = "ObjectManager`tguest.cash`t3`t00002042"
$changed.Insert($changed.IndexOf("Player`tclass`t0`t506c61796572"), "ObjectManager`tstaff.end_time`t4`t14000000")
$b = SnapshotFixture 'sections-changed' $changed.ToArray() '3333333333333333'
Reject $a $b 'world states differ'
$report = Get-Content -LiteralPath (Join-Path $b 'comparison.txt')
if ($report -notcontains 'differing sections: guests,staff') { throw "wrong section list: $($report -join ' | ')" }
if ($report -notcontains 'section guests differing=1 fields reference=2 candidate=2 first: record 19 ObjectManager guest.cash') { throw 'wrong guests line' }
if ($report -notcontains 'section staff differing=1 fields reference=3 candidate=4 first: record 25 ObjectManager staff.end_time') {
    throw "wrong staff line: $($report -join ' | ')"
}
++$script:passed
"world-hash comparison tool: $script:passed cases PASS; artifacts $root"
