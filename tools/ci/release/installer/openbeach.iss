; The Windows installer (docs/build/releases.md): installs the staged release (tools/ci/release/stage.py) for the current
; user, asks for the .iso of the player's Beach Life CD and the game language, imports the copy with game\scripts\import.ps1
; and opens the README in the game's language. Built by tools/ci/windows_release.ps1:
;   ISCC /DAppVersion=<v> /DStageDir=<staged release> /DRepoDir=<checkout> /O<dir> /F<name> openbeach.iss
; Command line: /ISO=<file> imports that image; /GAMELANG=en|fr|it|de|es|pl picks the language. A silent run without /ISO
; installs only (CI smoke test), so the player imports later with Import.cmd.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #error StageDir must name the staged release directory
#endif
#ifndef RepoDir
  #define RepoDir "..\..\..\.."
#endif

[Setup]
; Never change the AppId: an update installs over the same entry and directory.
AppId={{C49A68C4-9BB9-4346-B4F2-37F2BC40A82C}
AppName=OpenBeach
AppVersion={#AppVersion}
AppVerName=OpenBeach {#AppVersion}
AppPublisher=OpenBeach contributors
AppPublisherURL=https://openbeach.app
AppSupportURL=https://github.com/h4570/openbeach/issues
VersionInfoVersion={#Copy(AppVersion, 1, Pos("-", AppVersion + "-") - 1)}
; The game writes saves, settings and its log into game\userdata beside the program, so it must be a directory the
; player can write: a per-user install ({autopf} is %LOCALAPPDATA%\Programs), never Program Files.
PrivilegesRequired=lowest
DefaultDirName={autopf}\OpenBeach
DisableProgramGroupPage=yes
DefaultGroupName=OpenBeach
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile={#RepoDir}\assets\logo\openbeach.ico
UninstallDisplayIcon={app}\game\OpenBeach.exe
UninstallDisplayName=OpenBeach
Compression=lzma2/max
SolidCompression=yes
; The language dialog appears only when Windows' language is none of the game's.
ShowLanguageDialog=auto

; The order of the game's languages, BeachLife.ini's [Language] Language=<index>: en fr it de es pl.
[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"

[CustomMessages]
en.IsoCaption=Your copy of Beach Life
en.IsoDescription=Where is the disc image of your game?
en.IsoSubCaption=OpenBeach plays on the files of your own copy and converts them once, which takes about 15 minutes. Your copy is only read, never changed.%n%nChoose the disc image (.iso) of your Beach Life CD. When you update OpenBeach and your copy is already imported, you can leave the field empty.
en.IsoPrompt=Disc image (.iso):
en.IsoFilter=Disc images (*.iso)|*.iso|All files (*.*)|*.*
en.IsoRequired=Choose the disc image (.iso) of your Beach Life CD.
en.IsoNotFound=This file does not exist:
en.LangCaption=Game language
en.LangDescription=In which language do you want to play?
en.LangSubCaption=The game comes in the languages of your copy: English on the UK/US and the later English CD; French, German, Italian, Polish and Spanish on the European and the French/German CD. If your copy does not have the language you choose, the import takes one it has.
en.ReadyIso=Disc image:
en.ReadyLanguage=Game language:
en.Importing=Importing your copy of Beach Life, about 15 minutes. The progress is in the console window.
en.ImportFailed=The import of your copy failed; the message was in its window. OpenBeach is installed: start "Import Beach Life" in the Start menu to try again, or try the demo.
en.OpenReadme=Open the README
en.ImportShortcut=Import Beach Life
en.DemoShortcut=OpenBeach demo
en.ReadmeShortcut=README

fr.IsoCaption=Votre copie de Beach Life
fr.IsoDescription=Où se trouve l'image disque de votre jeu ?
fr.IsoSubCaption=OpenBeach joue avec les fichiers de votre propre copie et les convertit une seule fois, ce qui prend environ 15 minutes. Votre copie est seulement lue, jamais modifiée.%n%nChoisissez l'image disque (.iso) de votre CD de Beach Life. Si vous mettez OpenBeach à jour et que votre copie est déjà importée, vous pouvez laisser le champ vide.
fr.IsoPrompt=Image disque (.iso) :
fr.IsoFilter=Images disque (*.iso)|*.iso|Tous les fichiers (*.*)|*.*
fr.IsoRequired=Choisissez l'image disque (.iso) de votre CD de Beach Life.
fr.IsoNotFound=Ce fichier n'existe pas :
fr.LangCaption=Langue du jeu
fr.LangDescription=Dans quelle langue voulez-vous jouer ?
fr.LangSubCaption=Le jeu existe dans les langues de votre copie : l'anglais sur les CD britannique/américain et sur le CD anglais plus récent ; le français, l'allemand, l'italien, le polonais et l'espagnol sur le CD européen et le CD français/allemand. Si votre copie n'a pas la langue choisie, l'importation en prend une qu'elle a.
fr.ReadyIso=Image disque :
fr.ReadyLanguage=Langue du jeu :
fr.Importing=Importation de votre copie de Beach Life, environ 15 minutes. La progression s'affiche dans la fenêtre de console.
fr.ImportFailed=L'importation de votre copie a échoué ; le message était dans sa fenêtre. OpenBeach est installé : lancez « Import Beach Life » depuis le menu Démarrer pour réessayer, ou essayez la démo.
fr.OpenReadme=Ouvrir le fichier README
fr.ImportShortcut=Import Beach Life
fr.DemoShortcut=Démo OpenBeach
fr.ReadmeShortcut=README

it.IsoCaption=La tua copia di Beach Life
it.IsoDescription=Dov'è l'immagine disco del tuo gioco?
it.IsoSubCaption=OpenBeach gioca con i file della tua copia e li converte una sola volta, in circa 15 minuti. La tua copia viene solo letta, mai modificata.%n%nScegli l'immagine disco (.iso) del tuo CD di Beach Life. Se aggiorni OpenBeach e la tua copia è già importata, puoi lasciare vuoto il campo.
it.IsoPrompt=Immagine disco (.iso):
it.IsoFilter=Immagini disco (*.iso)|*.iso|Tutti i file (*.*)|*.*
it.IsoRequired=Scegli l'immagine disco (.iso) del tuo CD di Beach Life.
it.IsoNotFound=Questo file non esiste:
it.LangCaption=Lingua del gioco
it.LangDescription=In quale lingua vuoi giocare?
it.LangSubCaption=Il gioco è disponibile nelle lingue della tua copia: inglese sui CD UK/US e sul CD inglese successivo; francese, tedesco, italiano, polacco e spagnolo sul CD europeo e sul CD francese/tedesco. Se la tua copia non ha la lingua scelta, l'importazione ne prende una che ha.
it.ReadyIso=Immagine disco:
it.ReadyLanguage=Lingua del gioco:
it.Importing=Importazione della tua copia di Beach Life, circa 15 minuti. L'avanzamento è nella finestra della console.
it.ImportFailed=L'importazione della tua copia non è riuscita; il messaggio era nella sua finestra. OpenBeach è installato: avvia "Import Beach Life" dal menu Start per riprovare, oppure prova la demo.
it.OpenReadme=Apri il file README
it.ImportShortcut=Import Beach Life
it.DemoShortcut=Demo di OpenBeach
it.ReadmeShortcut=README

de.IsoCaption=Ihre Kopie von Beach Life
de.IsoDescription=Wo ist das Disc-Image Ihres Spiels?
de.IsoSubCaption=OpenBeach spielt mit den Dateien Ihrer eigenen Kopie und wandelt sie einmal um, das dauert etwa 15 Minuten. Ihre Kopie wird nur gelesen, nie verändert.%n%nWählen Sie das Disc-Image (.iso) Ihrer Beach-Life-CD. Wenn Sie OpenBeach aktualisieren und Ihre Kopie schon importiert ist, können Sie das Feld leer lassen.
de.IsoPrompt=Disc-Image (.iso):
de.IsoFilter=Disc-Images (*.iso)|*.iso|Alle Dateien (*.*)|*.*
de.IsoRequired=Wählen Sie das Disc-Image (.iso) Ihrer Beach-Life-CD.
de.IsoNotFound=Diese Datei gibt es nicht:
de.LangCaption=Sprache des Spiels
de.LangDescription=In welcher Sprache möchten Sie spielen?
de.LangSubCaption=Das Spiel gibt es in den Sprachen Ihrer Kopie: Englisch auf den UK/US-CDs und der späteren englischen CD; Französisch, Deutsch, Italienisch, Polnisch und Spanisch auf der europäischen und der französisch/deutschen CD. Hat Ihre Kopie die gewählte Sprache nicht, nimmt der Import eine, die sie hat.
de.ReadyIso=Disc-Image:
de.ReadyLanguage=Sprache des Spiels:
de.Importing=Ihre Kopie von Beach Life wird importiert, etwa 15 Minuten. Der Fortschritt steht im Konsolenfenster.
de.ImportFailed=Der Import Ihrer Kopie ist fehlgeschlagen; die Meldung stand in seinem Fenster. OpenBeach ist installiert: Starten Sie „Import Beach Life“ im Startmenü für einen neuen Versuch, oder probieren Sie die Demo.
de.OpenReadme=README öffnen
de.ImportShortcut=Import Beach Life
de.DemoShortcut=OpenBeach-Demo
de.ReadmeShortcut=README

es.IsoCaption=Tu copia de Beach Life
es.IsoDescription=¿Dónde está la imagen de disco de tu juego?
es.IsoSubCaption=OpenBeach juega con los archivos de tu propia copia y los convierte una sola vez, lo que tarda unos 15 minutos. Tu copia solo se lee, nunca se modifica.%n%nElige la imagen de disco (.iso) de tu CD de Beach Life. Si actualizas OpenBeach y tu copia ya está importada, puedes dejar el campo vacío.
es.IsoPrompt=Imagen de disco (.iso):
es.IsoFilter=Imágenes de disco (*.iso)|*.iso|Todos los archivos (*.*)|*.*
es.IsoRequired=Elige la imagen de disco (.iso) de tu CD de Beach Life.
es.IsoNotFound=Este archivo no existe:
es.LangCaption=Idioma del juego
es.LangDescription=¿En qué idioma quieres jugar?
es.LangSubCaption=El juego está en los idiomas de tu copia: inglés en los CD de Reino Unido/EE. UU. y en el CD inglés posterior; francés, alemán, italiano, polaco y español en el CD europeo y en el CD francés/alemán. Si tu copia no tiene el idioma elegido, la importación toma uno que tenga.
es.ReadyIso=Imagen de disco:
es.ReadyLanguage=Idioma del juego:
es.Importing=Importando tu copia de Beach Life, unos 15 minutos. El progreso aparece en la ventana de consola.
es.ImportFailed=La importación de tu copia ha fallado; el mensaje estaba en su ventana. OpenBeach está instalado: inicia «Import Beach Life» en el menú Inicio para volver a intentarlo, o prueba la demo.
es.OpenReadme=Abrir el archivo README
es.ImportShortcut=Import Beach Life
es.DemoShortcut=Demo de OpenBeach
es.ReadmeShortcut=README

pl.IsoCaption=Twoja kopia Beach Life
pl.IsoDescription=Gdzie jest obraz płyty Twojej gry?
pl.IsoSubCaption=OpenBeach korzysta z plików Twojej własnej kopii i raz je konwertuje, co trwa około 15 minut. Twoja kopia jest tylko czytana, nigdy zmieniana.%n%nWybierz obraz płyty (.iso) Twojego CD z Beach Life. Gdy aktualizujesz OpenBeach, a kopia jest już zaimportowana, możesz zostawić to pole puste.
pl.IsoPrompt=Obraz płyty (.iso):
pl.IsoFilter=Obrazy płyt (*.iso)|*.iso|Wszystkie pliki (*.*)|*.*
pl.IsoRequired=Wybierz obraz płyty (.iso) Twojego CD z Beach Life.
pl.IsoNotFound=Ten plik nie istnieje:
pl.LangCaption=Język gry
pl.LangDescription=W jakim języku chcesz grać?
pl.LangSubCaption=Gra jest dostępna w językach Twojej kopii: angielskim na płytach UK/US i późniejszej angielskiej; francuskim, niemieckim, włoskim, polskim i hiszpańskim na płycie europejskiej i francusko-niemieckiej. Jeśli Twoja kopia nie ma wybranego języka, import weźmie taki, który ma.
pl.ReadyIso=Obraz płyty:
pl.ReadyLanguage=Język gry:
pl.Importing=Importowanie Twojej kopii Beach Life, około 15 minut. Postęp widać w oknie konsoli.
pl.ImportFailed=Import Twojej kopii się nie udał; komunikat był w jego oknie. OpenBeach jest zainstalowany: uruchom „Import Beach Life” z menu Start, aby spróbować ponownie, albo wypróbuj demo.
pl.OpenReadme=Otwórz plik README
pl.ImportShortcut=Import Beach Life
pl.DemoShortcut=Demo OpenBeach
pl.ReadmeShortcut=README

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
Name: "{app}\game\userdata"

; OpenBeach.exe is a console program: minimized, its console stays out of the way like Play.cmd's `start /min`.
[Icons]
Name: "{group}\OpenBeach"; Filename: "{app}\game\OpenBeach.exe"; WorkingDir: "{app}\game"; Flags: runminimized
Name: "{group}\{cm:ImportShortcut}"; Filename: "{app}\game\Import.cmd"; WorkingDir: "{app}\game"; IconFilename: "{app}\game\OpenBeach.exe"
Name: "{group}\{cm:DemoShortcut}"; Filename: "{app}\game\Demo.cmd"; WorkingDir: "{app}\game"; IconFilename: "{app}\game\OpenBeach.exe"; Flags: runminimized
Name: "{group}\{cm:ReadmeShortcut}"; Filename: "{app}\README.{code:ReadmeLanguage}.txt"
Name: "{group}\{cm:UninstallProgram,OpenBeach}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\OpenBeach"; Filename: "{app}\game\OpenBeach.exe"; WorkingDir: "{app}\game"; Flags: runminimized; Tasks: desktopicon

[Run]
Filename: "{app}\README.{code:ReadmeLanguage}.txt"; Description: "{cm:OpenReadme}"; Flags: postinstall shellexec skipifsilent nowait

; The converted copy is not one of the installed files; saves and settings stay.
[UninstallDelete]
Type: filesandordirs; Name: "{app}\game\userdata\packs"
Type: filesandordirs; Name: "{app}\game\userdata\remaster"

[Code]
const
  GameLanguages = 'en,fr,it,de,es,pl';

var
  IsoPage: TInputFileWizardPage;
  LanguagePage: TInputOptionWizardPage;
  LanguageFromIni: Boolean;

function LanguageCode(Index: Integer): String;
begin
  Result := Copy(GameLanguages, Index * 3 + 1, 2);
end;

function LanguageIndex(Code: String): Integer;
var
  I: Integer;
begin
  Result := -1;
  for I := 0 to 5 do
    if CompareText(LanguageCode(I), Code) = 0 then
      Result := I;
end;

function GameIni(): String;
begin
  Result := ExpandConstant('{app}\game\userdata\BeachLife.ini');
end;

// The language the game starts in: import.ps1 writes the pack's language there.
function IniLanguage(): Integer;
begin
  Result := -1;
  if FileExists(GameIni()) then
    Result := GetIniInt('Language', 'Language', -1, 0, 5, GameIni());
end;

function IsoPath(): String;
begin
  if WizardSilent() then
    Result := ExpandConstant('{param:ISO|}')
  else
    Result := IsoPage.Values[0];
  Result := Trim(RemoveQuotes(Trim(Result)));
end;

function ChosenLanguage(): String;
begin
  if WizardSilent() then begin
    Result := ExpandConstant('{param:GAMELANG|}');
    if LanguageIndex(Result) < 0 then
      Result := ActiveLanguage();
  end else
    Result := LanguageCode(LanguagePage.SelectedValueIndex);
end;

function HasImportedCopy(): Boolean;
begin
  Result := FileExists(AddBackslash(WizardDirValue()) + 'game\userdata\packs\base\pack.json');
end;

// The README of the language the game will start in, which may differ from the choice when the copy lacks it.
function ReadmeLanguage(Param: String): String;
var
  Index: Integer;
begin
  Index := IniLanguage();
  if Index >= 0 then
    Result := LanguageCode(Index)
  else
    Result := ChosenLanguage();
end;

procedure InitializeWizard();
begin
  IsoPage := CreateInputFilePage(wpSelectDir, CustomMessage('IsoCaption'), CustomMessage('IsoDescription'),
    CustomMessage('IsoSubCaption'));
  IsoPage.Add(CustomMessage('IsoPrompt'), CustomMessage('IsoFilter'), '.iso');
  IsoPage.Values[0] := ExpandConstant('{param:ISO|}');

  LanguagePage := CreateInputOptionPage(IsoPage.ID, CustomMessage('LangCaption'), CustomMessage('LangDescription'),
    CustomMessage('LangSubCaption'), True, False);
  // The languages' own names, the same in every installer language.
  LanguagePage.Add('English');
  LanguagePage.Add('Français');
  LanguagePage.Add('Italiano');
  LanguagePage.Add('Deutsch');
  LanguagePage.Add('Español');
  LanguagePage.Add('Polski');
  if LanguageIndex(ExpandConstant('{param:GAMELANG|}')) >= 0 then
    LanguagePage.SelectedValueIndex := LanguageIndex(ExpandConstant('{param:GAMELANG|}'))
  else
    LanguagePage.SelectedValueIndex := LanguageIndex(ActiveLanguage());
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  // Keeping an imported copy imports nothing, so there is no language to choose.
  Result := (PageID = LanguagePage.ID) and (IsoPath() = '');
end;

procedure CurPageChanged(CurPageID: Integer);
var
  Index: Integer;
begin
  // An update preselects the language the game already plays in.
  if (CurPageID = LanguagePage.ID) and not LanguageFromIni then begin
    LanguageFromIni := True;
    Index := -1;
    if FileExists(AddBackslash(WizardDirValue()) + 'game\userdata\BeachLife.ini') then
      Index := GetIniInt('Language', 'Language', -1, 0, 5, AddBackslash(WizardDirValue()) + 'game\userdata\BeachLife.ini');
    if Index >= 0 then
      LanguagePage.SelectedValueIndex := Index;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  // A silent run passes every page through here too: it never asks, and without /ISO it imports nothing.
  if WizardSilent() then
    Exit;
  if CurPageID = IsoPage.ID then begin
    if IsoPath() = '' then begin
      if not HasImportedCopy() then begin
        MsgBox(CustomMessage('IsoRequired'), mbError, MB_OK);
        Result := False;
      end;
    end else if not FileExists(IsoPath()) then begin
      MsgBox(CustomMessage('IsoNotFound') + #13#10 + IsoPath(), mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo,
  MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo;
  if IsoPath() <> '' then begin
    Result := Result + NewLine + NewLine + CustomMessage('ReadyIso') + NewLine + Space + IsoPath();
    Result := Result + NewLine + NewLine + CustomMessage('ReadyLanguage') + NewLine + Space +
      LanguagePage.CheckListBox.ItemCaption[LanguagePage.SelectedValueIndex];
  end;
  if MemoTasksInfo <> '' then
    Result := Result + NewLine + NewLine + MemoTasksInfo;
end;

// The import runs in its own console window: the converter prints its steps there, and import.ps1 keeps the window open
// on a failure so the player can read why.
procedure ImportCopy();
var
  Arguments: String;
  Show, ResultCode: Integer;
begin
  if not FileExists(IsoPath()) then begin
    Log('import skipped, no such file: ' + IsoPath());
    SuppressibleMsgBox(CustomMessage('IsoNotFound') + #13#10 + IsoPath(), mbError, MB_OK, IDOK);
    Exit;
  end;
  Arguments := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\game\scripts\import.ps1') +
    '" -Source "' + IsoPath() + '" -PreferLanguage ' + ChosenLanguage();
  Show := SW_HIDE;
  if not WizardSilent() then begin
    Arguments := Arguments + ' -PauseOnError';
    Show := SW_SHOWNORMAL;
  end;
  WizardForm.StatusLabel.Caption := CustomMessage('Importing');
  WizardForm.FilenameLabel.Caption := '';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  try
    if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Arguments, ExpandConstant('{app}\game'),
        Show, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then begin
      Log(Format('import failed (exit code %d)', [ResultCode]));
      SuppressibleMsgBox(CustomMessage('ImportFailed'), mbError, MB_OK, IDOK);
    end;
  finally
    WizardForm.ProgressGauge.Style := npbstNormal;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and (IsoPath() <> '') then begin
    ImportCopy();
    // [Icons] made the README shortcut before the import chose the language; point it at the game's.
    CreateShellLink(ExpandConstant('{group}\' + CustomMessage('ReadmeShortcut') + '.lnk'), '',
      ExpandConstant('{app}\README.') + ReadmeLanguage('') + '.txt', '', '', '', 0, SW_SHOWNORMAL);
  end;
end;
