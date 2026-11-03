# Imports the player's own copy of Beach Life into userdata\ of a Windows release (docs/build/releases.md): the converted
# pack, the window icon and cursors, and the game language in BeachLife.ini. Started by Import.cmd.
#   import.ps1 [-Source <installation, CD drive, extracted CD or .iso>] [-Language en|fr|it|de|es|pl]
# Asks for the copy when none is given, and for the language when the copy does not name one (a CD). Exit code 1 on failure.
param([string]$Source = '', [string]$Language = '')
$ErrorActionPreference = 'Stop'
$game = Split-Path $PSScriptRoot
$convert = Join-Path $game 'obl-convert.exe'
$userdata = Join-Path $game 'userdata'
$pack = Join-Path $userdata 'packs\base'
# The import writes beside the pack and replaces it only when it succeeded, so a failed one keeps the game playable.
$fresh = Join-Path $userdata 'packs\base.new'
# The game numbers its languages like BeachLife.ini's [Language] Language=<index>; the converter names them by these codes.
$languages = @('en', 'fr', 'it', 'de', 'es', 'pl')

function Stop-Import([string]$message) {
    Write-Host ''
    Write-Host "ERROR: $message" -ForegroundColor Red
    exit 1
}

# Runs obl-convert, shows its output and keeps it for the error check.
function Invoke-Convert([string[]]$arguments) {
    # Its error output must not stop the script (Windows PowerShell turns redirected native stderr into errors): the
    # message is shown and the exit code checked by the caller.
    $ErrorActionPreference = 'Continue'
    $script:convertOutput = @()
    & $convert @arguments 2>&1 | ForEach-Object {
        $line = "$_"
        $script:convertOutput += $line
        Write-Host $line
    }
    return $LASTEXITCODE
}

# Sets [Language] Language=<index> and keeps every other setting; the game reads nothing from an INI without
# [Beach Life Options] Version=1, so a new one gets it.
function Set-GameLanguage([string]$code) {
    $index = [array]::IndexOf($languages, $code)
    if ($index -lt 0) { return }
    $ini = Join-Path $userdata 'BeachLife.ini'
    if (-not (Test-Path -LiteralPath $ini)) {
        Set-Content -LiteralPath $ini -Value "[Beach Life Options]`r`nVersion=1`r`n[Language]`r`nLanguage=$index" -Encoding ascii
        return
    }
    $lines = [Collections.Generic.List[string]](Get-Content -LiteralPath $ini)
    $section = $lines.FindIndex({ param($l) $l.Trim() -ieq '[Language]' })
    if ($section -lt 0) {
        $lines.Add('[Language]')
        $lines.Add("Language=$index")
    } else {
        $end = $section + 1
        while ($end -lt $lines.Count -and -not $lines[$end].Trim().StartsWith('[')) { $end++ }
        $line = -1
        for ($i = $section + 1; $i -lt $end; $i++) { if ($lines[$i] -match '^\s*Language\s*=') { $line = $i } }
        if ($line -ge 0) { $lines[$line] = "Language=$index" } else { $lines.Insert($section + 1, "Language=$index") }
    }
    Set-Content -LiteralPath $ini -Value $lines -Encoding ascii
}

Write-Host 'OpenBeach: import your own copy of Beach Life'
Write-Host ''
if (-not $Source) {
    Write-Host 'Drag your Beach Life folder, your CD drive or your .iso file into this window, then press Enter.'
    $Source = "$(Read-Host 'Copy')"
}
# A dragged path comes in quotes; a drive root ("E:\") loses its backslash in Import.cmd and gets it back here.
$Source = $Source.Trim().Trim('"').Trim()
if ($Source -match '^[A-Za-z]:\\?\.?$') { $Source = $Source.Substring(0, 2) + '\' }
if (-not $Source) { Stop-Import 'No copy given.' }
if (-not (Test-Path -LiteralPath $Source)) { Stop-Import "Not found: $Source" }

$mounted = $null
try {
    if ((Test-Path -LiteralPath $Source -PathType Leaf) -and $Source -match '\.iso$') {
        # Windows mounts a disc image as a drive, like a double click in Explorer, and the import reads that drive.
        Write-Host "Opening the disc image $Source ..."
        try {
            $image = Mount-DiskImage -ImagePath (Resolve-Path -LiteralPath $Source).Path -Access ReadOnly -PassThru -ErrorAction Stop
            $mounted = $image.ImagePath
            $letter = ($image | Get-Volume -ErrorAction Stop).DriveLetter
        } catch {
            Stop-Import "Windows could not open the disc image ($($_.Exception.Message)). Double-click the .iso file, then drag the new drive onto Import.cmd."
        }
        if (-not $letter) { Stop-Import 'The disc image has no drive letter. Double-click the .iso file, then drag the new drive onto Import.cmd.' }
        $Source = "${letter}:\"
        Write-Host "The disc image is drive $Source"
    }
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) { Stop-Import "Not a folder, a drive or an .iso file: $Source" }

    Write-Host ''
    Write-Host "Importing $Source into $userdata"
    Write-Host 'This takes about 15 minutes and happens only once. Your copy is only read, never changed.'
    Write-Host ''
    # A new import replaces the old pack whole, so no file of another copy stays behind; saves and settings stay.
    if (Test-Path -LiteralPath $fresh) { Remove-Item -LiteralPath $fresh -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $fresh, (Join-Path $userdata 'remaster') | Out-Null
    $arguments = @('import', $Source, $fresh)
    if ($Language) { $arguments += @('--language', $Language) }
    $exit = Invoke-Convert $arguments
    # A CD names no language (an installation does, in its BeachLife.ini): ask which of the copy's languages to play.
    if ($exit -ne 0 -and -not $Language -and ($script:convertOutput -join "`n") -match 'Language') {
        Write-Host ''
        Write-Host 'Your copy does not say which language to use. Type one your copy has:'
        Write-Host '  en English (UK/US and the later English CD)'
        Write-Host '  fr French, de German, it Italian, pl Polish, es Spanish (European and French/German CD)'
        # Without a console (stdin closed) Read-Host returns nothing: the check below reports it.
        $Language = "$(Read-Host 'Language')".Trim().ToLowerInvariant()
        if ($languages -notcontains $Language) { Stop-Import "Unknown language: $Language" }
        Remove-Item -LiteralPath $fresh -Recurse -Force
        New-Item -ItemType Directory -Force -Path $fresh | Out-Null
        $exit = Invoke-Convert @('import', $Source, $fresh, '--language', $Language)
    }
    if ($exit -ne 0) { Stop-Import 'The import failed (the message is above). Nothing of your copy was changed.' }
    if (Test-Path -LiteralPath $pack) { Remove-Item -LiteralPath $pack -Recurse -Force }
    Rename-Item -LiteralPath $fresh -NewName 'base'
    $exit = Invoke-Convert @('resources', $Source, (Join-Path $userdata 'remaster'))
    if ($exit -ne 0) { Stop-Import 'Reading the window icon and the cursors failed (the message is above).' }
} catch {
    Stop-Import "$($_.Exception.Message) Nothing of your copy was changed."
} finally {
    # exit runs this too: a failed import leaves no half-written pack behind, the old one stays as it was.
    if (Test-Path -LiteralPath $fresh) { Remove-Item -LiteralPath $fresh -Recurse -Force -ErrorAction SilentlyContinue }
    if ($mounted) { Dismount-DiskImage -ImagePath $mounted -ErrorAction SilentlyContinue | Out-Null }
}

# The pack's own language, so the game starts in a language the copy has.
$manifest = Get-Content -LiteralPath (Join-Path $pack 'pack.json') -Raw | ConvertFrom-Json
if ($manifest.language) { Set-GameLanguage $manifest.language }
Write-Host ''
Write-Host "Done. Start the game with Play.cmd (language: $($manifest.language))." -ForegroundColor Green
exit 0
