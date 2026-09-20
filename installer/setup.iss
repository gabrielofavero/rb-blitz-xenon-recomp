; ---------------------------------------------------------------------------
; Rock Band Blitz (Xenon recomp) - installer wizard.
;
; This script is presentation and sequencing only. Everything that touches game
; data, downloads or hashes files is done by rb_blitz_setup_helper.exe (see
; src/), because Pascal Script has no filesystem, HTTP or crypto support. The
; wizard collects the user's answers, runs the helper once per stage, and maps
; the helper's progress file onto the progress page.
;
; Build it with build.ps1, which compiles the helper, refreshes the payload
; snapshot and calls ISCC. See README.md.
;
; The directories below can be overridden from the command line so a release can
; point at artefacts built elsewhere:
;   /DGeneratedDir=<dir>  holds pins.iss and a copy of the helper
;   /DPayloadDir=<dir>    holds the recompiled build that gets embedded
;   /DDistDir=<dir>       receives the setup executable
;   /DArtDir=<dir>        optional wizard images (see tools/make_art.ps1)
; ---------------------------------------------------------------------------

#define InstallerDirPath AddBackslash(SourcePath)
#define ProjectDirPath AddBackslash(AddBackslash(SourcePath) + "..")

#ifndef GeneratedDir
  #define GeneratedDir AddBackslash(SourcePath) + "out\generated"
#endif
#ifndef PayloadDir
  #define PayloadDir AddBackslash(SourcePath) + "out\payload"
#endif
#ifndef DistDir
  #define DistDir AddBackslash(SourcePath) + "out\dist"
#endif
#ifndef ArtDir
  #define ArtDir AddBackslash(SourcePath) + "assets"
#endif

#define GeneratedDirPath AddBackslash(GeneratedDir)
#define PayloadDirPath AddBackslash(PayloadDir)
#define DistDirPath AddBackslash(DistDir)
#define ArtDirPath AddBackslash(ArtDir)

; Names this script needs on both the [Files]/[Icons] side and in [Code]. The
; files of the recompiled build itself are not listed here: they come from the
; payload manifest, which the helper reads back at the end of the install.
#define HelperExeName "rb_blitz_setup_helper.exe"
#define GameExeName "rb_blitz.exe"
#define GameDirName "game"

#if !FileExists(GeneratedDirPath + "pins.iss")
  #error pins.iss is missing from the generated directory: run build.ps1 (or the helper build) first.
#endif
#include GeneratedDirPath + "pins.iss"

#if !FileExists(GeneratedDirPath + HelperExeName)
  #error the helper is missing from the generated directory: build.ps1 copies it there.
#endif

; Without a pinned download the recompiled build has to travel inside the setup
; executable, so the payload directory must have been made first. With a pinned
; download it is fetched at install time and nothing is embedded.
#if (PayloadHasDownload == 0) && (!FileExists(PayloadDirPath + "payload-manifest.toml"))
  #error no payload to embed: pass /DPayloadUrl and /DPayloadSha256, or create the payload snapshot first.
#endif

; Setup's version resource needs four components; the app version has three.
#define SetupVersionQuad AppVersion + ".0"

[Setup]
AppId={{92C7B4E1-3F5A-4D2E-9B18-7A6C4E0D5F31}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppHomepageUrl}
AppSupportURL={#AppSupportUrl}
AppUpdatesURL={#AppHomepageUrl}
VersionInfoVersion={#SetupVersionQuad}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} setup
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright={#AppPublisher}
DefaultDirName={autopf}\{#AppDefaultDirName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
; No administrator rights are needed: the build, the game data and the mod all
; live under the user's own profile by default. An administrator can still force
; a machine-wide install with /ALLUSERS on the command line.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
MinVersion={#AppMinWindowsBuild}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#DistDirPath}
OutputBaseFilename=RockBandBlitzSetup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
DisableWelcomePage=no
ShowLanguageDialog=no
SetupLogging=yes
LicenseFile={#ProjectDirPath}LICENSE
SetupIconFile={#ProjectDirPath}blitz.ico
UninstallDisplayIcon={app}\{#GameExeName}

; The side image is optional: it only appears when tools/make_art.ps1 produced it.
; The art is not committed (see .gitignore), and the licence of the artwork is
; not ours, so its absence is a supported configuration. The area the image has to
; fill grows with the user's DPI setting, so make_art.ps1 writes the whole ladder
; of sizes Inno Setup documents; the wildcards below let Setup pick, on the
; machine it runs on, the file that best matches the area it has to fill.
; WizardImageStretch is deliberately left at its default (yes): on a DPI setting
; that is not one of the documented ones it fills the area instead of leaving bars.
#if FileExists(ArtDirPath + "wizard-large-202x386.bmp")
WizardImageFile={#ArtDirPath}wizard-large-*.bmp
#endif
#if FileExists(ArtDirPath + "wizard-small-58x58.bmp")
WizardSmallImageFile={#ArtDirPath}wizard-small-*.bmp
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Welcome to the {#AppName} setup
WelcomeLabel2=This wizard puts a ready-to-play build of Rock Band Blitz on your PC, without compiling anything.%n%nIt needs the game data taken from an Xbox 360 copy that you own - either a game folder you have already extracted or the package you downloaded. No game files are included or downloaded by this installer, and it is not affiliated with or endorsed by the game's publisher.%n%nContinue when the folder or the package is at hand.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
; The wizard needs the helper while it is still on screen (to check the user's
; choices and the free space), so it is extracted to the temporary directory as
; well. Temporary files must sit at the very top when solid compression is on.
Source: "{#GeneratedDirPath}{#HelperExeName}"; DestDir: "{tmp}"; Flags: dontcopy noencryption
Source: "{#GeneratedDirPath}{#HelperExeName}"; DestDir: "{app}"; Flags: ignoreversion
#if PayloadHasDownload == 0
; The recompiled build and its runtime DLLs, straight into the install folder.
; The payload directory is a plain snapshot of the release build (see
; tools/make_payload.ps1); its dev-only configuration file is not part of it.
Source: "{#PayloadDirPath}*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Icons]
Name: "{autoprograms}\{#AppShortName}"; Filename: "{app}\{#GameExeName}"; Parameters: "--game_data_root=""{app}\{#GameDirName}"""; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppShortName}"; Filename: "{app}\{#GameExeName}"; Parameters: "--game_data_root=""{app}\{#GameDirName}"""; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#GameExeName}"; Parameters: "--game_data_root=""{app}\{#GameDirName}"""; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#AppShortName}}"; Flags: postinstall nowait skipifsilent; Check: InstallSucceeded

[UninstallDelete]
; Files the helper wrote that Inno Setup never saw: the ones it downloaded, the
; manifests and the log. Anything the installer placed itself is already tracked
; by the uninstaller, so these entries only cost a failed delete.
Type: files; Name: "{app}\{#HelperExeName}"
Type: files; Name: "{app}\{#GameExeName}"
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\payload-manifest.toml"
Type: files; Name: "{app}\install-manifest.toml"
Type: files; Name: "{app}\install-report.txt"
Type: files; Name: "{app}\game-source-details.txt"
Type: files; Name: "{app}\install.log"
Type: filesandordirs; Name: "{app}\.staging"
Type: filesandordirs; Name: "{app}\{#GameDirName}\.staging"
Type: dirifempty; Name: "{app}"

[Code]

const
  { Inno's preprocessor reads a line whose first non-blank character is a hash as
    a directive, so a newline escape cannot start a continuation line. Messages that
    span lines concatenate this constant instead. }
  kCrLf = #13#10;

  { How often the wizard looks at the helper's progress file, and how long it
    waits: a quiet helper for ten minutes means something is stuck, and an hour
    is the ceiling for the whole stage. }
  PollSleepMs = 120;
  StagePollLimit = 30000;
  SilencePollLimit = 5000;

  { Where each stage sits on the single progress bar, out of 1000. }
  PayloadStart = 0;
  PayloadEnd = 200;
  GameStart = 200;
  GameEnd = 700;
  UltimateStart = 700;
  UltimateEnd = 900;
  FinalizeStart = 900;
  FinalizeEnd = 1000;

  { Space planning. The helper measures the free space; these numbers decide how
    much room the install is allowed to want. }
  MegabyteBytes = 1048576;
  FreeSpaceSlackMiB = 64;
  PayloadSlackMiB = 64;
  PayloadDownloadFallbackMiB = 201;
  InstalledDataSlackMiB = 256;

  { What is installed, and where. }
  HelperExeName = 'rb_blitz_setup_helper.exe';
  GameExeName = 'rb_blitz.exe';
  GameDirName = 'game';
  GameEntryPointRel = 'default.xex';
  GameArchiveDataRel = 'gen\main_xbox_0.ark';
  UltimateHeaderRel = 'gen\patch_xbox.hdr';
  UltimateWrapperDir = 'Xbox';

  { Silent-mode parameters (see README.md). }
  MethodParam = 'GAMEMETHOD';
  GameFolderParam = 'GAMEFOLDER';
  GamePackageParam = 'GAMEPACKAGE';
  UltimateSourceParam = 'ULTIMATESOURCE';
  UltimateZipParam = 'ULTIMATEZIP';
  UltimateFolderParam = 'ULTIMATEFOLDER';

  { Page indexes. }
  MethodPackage = 0;
  MethodFolder = 1;
  MethodInstalled = 2;
  UltimateNothing = 0;
  UltimatePinned = 1;
  UltimateZip = 2;
  UltimateFolder = 3;

  { Values the build script pinned. }
  PayloadSizeBytes = {#PayloadSize};
  PayloadIsDownload = {#PayloadHasDownload};
  UltimateArchiveBytes = {#UltimateSize};

var
  MethodPage: TInputOptionWizardPage;
  FolderPage: TInputDirWizardPage;
  PackagePage: TInputFileWizardPage;
  UltimatePage: TInputOptionWizardPage;
  UltimateZipPage: TInputFileWizardPage;
  UltimateFolderPage: TInputDirWizardPage;
  StagePage: TOutputProgressWizardPage;

  gTempDir: String;
  gAppDir: String;
  gStageSummaryFile: String;
  gProbeSummaryFile: String;
  gProbeDetailsFile: String;
  gProbeLogFile: String;
  gProgressFile: String;
  gLogFile: String;
  gGameDetailsFile: String;

  gStageActive: Boolean;
  gStageTitle: String;
  gFailed: Boolean;
  gFailureText: String;

  gGameSourceDescription: String;
  gGameSourceBytes: Int64;
  gPayloadDownload: Boolean;
  gPayloadSource: String;
  gPayloadVersion: String;
  gUltimateActive: Boolean;
  gUltimateMode: String;
  gUltimateArchive: String;
  gUltimateFolder: String;
  gUltimateSourceDescription: String;
  gUltimateVersion: String;

// --- small helpers --------------------------------------------------------

// Quotes an argument for CreateProcess. The helper parses its command line with
// CommandLineToArgvW, where a backslash in front of the closing quote would
// escape it, so trailing backslashes are doubled.
function QuoteArg(const value: String): String;
var
  index: Integer;
  trailing: String;
begin
  index := Length(value);
  trailing := '';
  while (index > 0) and (value[index] = '\') do
  begin
    trailing := trailing + '\';
    index := index - 1;
  end;
  Result := '"' + value + trailing + '"';
end;

function JoinPath(const root, leaf: String): String;
begin
  if root = '' then
    Result := leaf
  else
    Result := AddBackslash(root) + leaf;
end;

// Inno Setup only expands constants that are written out literally, hence one
// accessor per parameter instead of a generic lookup.
function ParamMethod: String;
begin
  Result := Trim(ExpandConstant('{param:GAMEMETHOD|}'));
end;

function ParamGameFolder: String;
begin
  Result := RemoveQuotes(Trim(ExpandConstant('{param:GAMEFOLDER|}')));
end;

function ParamGamePackage: String;
begin
  Result := RemoveQuotes(Trim(ExpandConstant('{param:GAMEPACKAGE|}')));
end;

function ParamUltimateSource: String;
begin
  Result := Trim(ExpandConstant('{param:ULTIMATESOURCE|}'));
end;

function ParamUltimateZip: String;
begin
  Result := RemoveQuotes(Trim(ExpandConstant('{param:ULTIMATEZIP|}')));
end;

function ParamUltimateFolder: String;
begin
  Result := RemoveQuotes(Trim(ExpandConstant('{param:ULTIMATEFOLDER|}')));
end;

function ReadAllText(const path: String): String;
var
  lines: TArrayOfString;
  index: Integer;
begin
  Result := '';
  if not FileExists(path) then
    Exit;
  if not LoadStringsFromFile(path, lines) then
    Exit;
  for index := 0 to GetArrayLength(lines) - 1 do
  begin
    if index > 0 then
      Result := Result + kCrLf;
    Result := Result + lines[index];
  end;
end;

// The helper writes `key=value;key=value` on a single line. Split on ';' first,
// because a value (a path, a sentence) may contain '='.
function SummaryText(const file, key, fallback: String): String;
var
  rest, item, name: String;
  separator: Integer;
begin
  Result := fallback;
  rest := ReadAllText(file);
  while rest <> '' do
  begin
    separator := Pos(';', rest);
    if separator > 0 then
    begin
      item := Copy(rest, 1, separator - 1);
      rest := Copy(rest, separator + 1, Length(rest) - separator);
    end
    else
    begin
      item := rest;
      rest := '';
    end;
    separator := Pos('=', item);
    if separator > 0 then
    begin
      name := Copy(item, 1, separator - 1);
      if name = key then
      begin
        Result := Copy(item, separator + 1, Length(item) - separator);
        Exit;
      end;
    end;
  end;
end;

function SummaryFlag(const file, key: String): Boolean;
begin
  Result := Trim(SummaryText(file, key, '0')) = '1';
end;

function SummaryBytes(const file, key: String): Int64;
var
  value: String;
begin
  Result := 0;
  value := Trim(SummaryText(file, key, ''));
  if value = '' then
    Exit;
  Result := StrToInt64(value);
end;

// `ok` is the last key the helper writes, so it is present exactly when the
// helper has finished: the summary file doubles as the completion signal.
function HelperFinished(const file: String): Boolean;
begin
  Result := SummaryText(file, 'ok', '') <> '';
end;

// Prefers the helper's own explanation, then whatever the polling loop noticed.
function HelperError(const file: String): String;
begin
  Result := Trim(SummaryText(file, 'error', ''));
  if Result = '' then
    Result := gFailureText;
  if Result = '' then
    Result := 'the helper did not say what went wrong';
end;

// The wizard talks to the helper in the install folder once it is there, and to
// the extracted copy before that.
function HelperPath: String;
begin
  if FileExists(JoinPath(gAppDir, HelperExeName)) then
    Result := JoinPath(gAppDir, HelperExeName)
  else
    Result := JoinPath(gTempDir, HelperExeName);
end;

procedure EnsureHelper;
begin
  if not FileExists(HelperPath) then
    ExtractTemporaryFile(HelperExeName);
end;

// --- talking to the helper ------------------------------------------------

function RunHelper(const args, summaryFile, logFile, workDir: String; const wait: Boolean): Integer;
var
  code: Integer;
begin
  EnsureHelper;
  DeleteFile(summaryFile);
  DeleteFile(gProgressFile);
  if wait then
    Exec(HelperPath, args + ' --summary ' + QuoteArg(summaryFile) + ' --log ' + QuoteArg(logFile),
         workDir, SW_HIDE, ewWaitUntilTerminated, code)
  else
    Exec(HelperPath, args + ' --summary ' + QuoteArg(summaryFile) + ' --log ' + QuoteArg(logFile),
         workDir, SW_HIDE, ewNoWait, code);
  Result := code;
end;

// A probe is short and quiet: run it to completion and read its summary.
// `--details` is only accepted by the probes that describe files, so the ones
// that answer a plain question go through RunQuietProbe instead.
function RunProbe(const args: String): Boolean;
begin
  RunHelper(args + ' --details ' + QuoteArg(gProbeDetailsFile), gProbeSummaryFile, gProbeLogFile,
            gTempDir, True);
  Result := SummaryFlag(gProbeSummaryFile, 'ok');
end;

function RunQuietProbe(const args: String): Boolean;
begin
  RunHelper(args, gProbeSummaryFile, gProbeLogFile, gTempDir, True);
  Result := SummaryFlag(gProbeSummaryFile, 'ok');
end;

function ProgressLine(const index: Integer): String;
var
  lines: TArrayOfString;
begin
  Result := '';
  if not FileExists(gProgressFile) then
    Exit;
  if not LoadStringsFromFile(gProgressFile, lines) then
    Exit;
  if GetArrayLength(lines) > index then
    Result := Trim(lines[index]);
end;

// --- the progress page ----------------------------------------------------

// Inno Setup does not pump messages while [Code] is running, so the wizard
// would otherwise not repaint during a stage: UpdateWindow through
// TControl.Refresh is the only way to make the page follow the helper.
procedure PumpStagePaint;
begin
  StagePage.Msg1Label.Refresh;
  StagePage.Msg2Label.Refresh;
  StagePage.ProgressBar.Refresh;
end;

procedure StageUpdate(const percent: Integer; const detail: String);
var
  text: String;
  overall: Integer;
begin
  text := detail;
  if text = '' then
    text := gStageTitle;
  if percent >= 0 then
  begin
    overall := percent div 10;
    if overall > 100 then
      overall := 100;
    StagePage.SetProgress(overall, 100);
    text := text + '  (' + Format('%d', [overall]) + '%)';
  end;
  StagePage.SetText(text, '');
  StagePage.Msg2Label.Caption := gStageTitle;
  PumpStagePaint;
end;

procedure ShowStage(const title, subtitle: String);
begin
  gStageTitle := title;
  StagePage.SetProgress(0, 100);
  StagePage.SetText(title, '');
  StagePage.Msg2Label.Caption := subtitle;
  PumpStagePaint;
end;

procedure HideStage;
begin
  StagePage.SetProgress(0, 100);
end;

procedure PollStage(const startPercent, endPercent: Integer);
var
  waited: Integer;
  quiet: Integer;
  percent: Integer;
  detail: String;
begin
  waited := 0;
  quiet := 0;
  while waited < StagePollLimit do
  begin
    if HelperFinished(gStageSummaryFile) then
      Exit;
    percent := -1;
    detail := '';
    if FileExists(gProgressFile) then
    begin
      quiet := 0;
      percent := StrToIntDef(ProgressLine(0), -1);
      detail := ProgressLine(1);
    end
    else
      quiet := quiet + 1;
    if percent < 0 then
      StageUpdate(-1, detail)
    else
      StageUpdate(startPercent + (percent * (endPercent - startPercent)) div 100, detail);
    if quiet > SilencePollLimit then
    begin
      gFailureText := 'the helper stopped reporting progress';
      Exit;
    end;
    Sleep(PollSleepMs);
    waited := waited + 1;
  end;
  gFailureText := 'the helper did not finish in time';
end;

// Runs one stage of the install on the progress page. The helper is launched
// without waiting: the wizard watches its progress file instead, which is what
// keeps the page updating at all - Inno Setup cannot pump messages while
// CurStepChanged is running.
function RunStage(const title, subtitle, args: String; const startPercent,
                  endPercent: Integer): Boolean;
begin
  gFailureText := '';
  ShowStage(title, subtitle);
  RunHelper(args + ' --progress ' + QuoteArg(gProgressFile), gStageSummaryFile, gLogFile, gAppDir,
            False);
  PollStage(startPercent, endPercent);
  Result := SummaryFlag(gStageSummaryFile, 'ok');
  if Result then
    gFailureText := ''
  else
    gFailureText := HelperError(gStageSummaryFile);
  HideStage;
end;

// --- what each stage reports back ----------------------------------------

procedure NoteSource(const summaryFile: String; const sourceKey, versionKey: String;
                     var source, version: String);
begin
  source := Trim(SummaryText(summaryFile, sourceKey, ''));
  version := Trim(SummaryText(summaryFile, versionKey, ''));
  if version = 'unknown' then
    version := '';
end;

// --- judging the user's answers -------------------------------------------

function GameDataDirectory: String;
begin
  Result := JoinPath(WizardDirValue, GameDirName);
end;

function GameDataPresent: Boolean;
begin
  Result := False;
  if WizardDirValue = '' then
    Exit;
  if not DirExists(GameDataDirectory) then
    Exit;
  Result := FileExists(JoinPath(GameDataDirectory, GameEntryPointRel)) and
            FileExists(JoinPath(GameDataDirectory, GameArchiveDataRel));
end;

procedure ProbeGameFolder(const folder: String);
var
  args: String;
begin
  args := 'probe-folder --folder ' + QuoteArg(folder);
  if RunProbe(args) then
  begin
    gGameSourceDescription := Trim(SummaryText(gProbeSummaryFile, 'description', 'folder ' + folder));
    gGameSourceBytes := SummaryBytes(gProbeSummaryFile, 'bytes');
  end
  else
    gFailureText := HelperError(gProbeSummaryFile);
end;

procedure ProbeGamePackage(const package: String);
begin
  if RunProbe('probe-package --package ' + QuoteArg(package)) then
  begin
    gGameSourceDescription := Trim(SummaryText(gProbeSummaryFile, 'description', 'package ' + package));
    gGameSourceBytes := SummaryBytes(gProbeSummaryFile, 'bytes');
  end
  else
    gFailureText := HelperError(gProbeSummaryFile);
end;

function ProbeUltimateArchive(const archive: String): Boolean;
begin
  Result := RunProbe('probe-archive --archive ' + QuoteArg(archive) + ' --kind ultimate');
  if not Result then
    gFailureText := HelperError(gProbeSummaryFile);
end;

// Cheap look for the mod's archive header, in the shapes the release ships in:
// the folder itself, an Xbox wrapper, or a version folder that holds either.
function LooksLikeUltimateTree(const folder: String): Boolean;
var
  find: TFindRec;
  child: String;
  found: Boolean;
begin
  Result := False;
  if not DirExists(folder) then
    Exit;
  if FileExists(JoinPath(folder, UltimateHeaderRel)) then
  begin
    Result := True;
    Exit;
  end;
  if FileExists(JoinPath(JoinPath(folder, UltimateWrapperDir), UltimateHeaderRel)) then
  begin
    Result := True;
    Exit;
  end;
  found := False;
  if FindFirst(JoinPath(folder, '*'), find) then
  begin
    repeat
      if (find.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
      begin
        child := JoinPath(folder, find.Name);
        if FileExists(JoinPath(child, UltimateHeaderRel)) then
          found := True;
        if FileExists(JoinPath(JoinPath(child, UltimateWrapperDir), UltimateHeaderRel)) then
          found := True;
      end;
    until found or (not FindNext(find));
    FindClose(find);
  end;
  Result := found;
end;

// --- resolving the answers, in the wizard or from the command line --------

procedure RefreshMethodPage;
begin
  if GameDataPresent then
    MethodPage.CheckListBox.ItemEnabled[MethodInstalled] := True
  else
  begin
    MethodPage.CheckListBox.ItemEnabled[MethodInstalled] := False;
    if MethodPage.SelectedValueIndex = MethodInstalled then
      MethodPage.SelectedValueIndex := MethodPackage;
  end;
end;

// 0 package, 1 folder, 2 the data that is already installed.
function ChosenGameMethod: Integer;
var
  method, folder, package: String;
begin
  if not WizardSilent then
  begin
    Result := MethodPage.SelectedValueIndex;
    if (Result < 0) or (not MethodPage.CheckListBox.ItemEnabled[Result]) then
      Result := MethodPackage;
    Exit;
  end;

  method := ParamMethod;
  folder := ParamGameFolder;
  package := ParamGamePackage;
  if method <> '' then
  begin
    if method = 'package' then
      Result := MethodPackage
    else if method = 'folder' then
      Result := MethodFolder
    else if method = 'installed' then
      Result := MethodInstalled
    else
      Result := -1;
    Exit;
  end;
  if folder <> '' then
    Result := MethodFolder
  else if package <> '' then
    Result := MethodPackage
  else
    Result := -1;
end;

function ChosenGameFolder: String;
begin
  if WizardSilent then
    Result := ParamGameFolder
  else
    Result := Trim(FolderPage.Values[0]);
end;

function ChosenGamePackage: String;
begin
  if WizardSilent then
    Result := ParamGamePackage
  else
    Result := Trim(PackagePage.Values[0]);
end;

// Fills in gUltimate*; returns an empty string when the choice is usable.
function ResolveUltimateChoice: String;
var
  mode: String;
begin
  Result := '';
  gUltimateMode := 'none';
  gUltimateActive := False;
  gUltimateArchive := '';
  gUltimateFolder := '';

  if WizardSilent then
  begin
    mode := ParamUltimateSource;
    if mode = '' then
      mode := 'none';
    if mode = 'pin' then
    begin
      gUltimateArchive := '';
    end
    else if mode = 'zip' then
      gUltimateArchive := ParamUltimateZip
    else if mode = 'folder' then
      gUltimateFolder := ParamUltimateFolder
    else if mode <> 'none' then
    begin
      Result := 'ULTIMATESOURCE must be none, pin, zip or folder, not ''' + mode + '''.';
      Exit;
    end;
  end
  else
  begin
    if UltimatePage.SelectedValueIndex = UltimatePinned then
      mode := 'pin'
    else if UltimatePage.SelectedValueIndex = UltimateZip then
    begin
      mode := 'zip';
      gUltimateArchive := Trim(UltimateZipPage.Values[0]);
    end
    else if UltimatePage.SelectedValueIndex = UltimateFolder then
    begin
      mode := 'folder';
      gUltimateFolder := Trim(UltimateFolderPage.Values[0]);
    end
    else
      mode := 'none';
  end;

  if mode = 'none' then
    Exit;

  if mode = 'pin' then
  begin
    if '{#UltimateUrl}' = '' then
    begin
      Result := 'This build of the installer has no Rock Band Blitz Ultimate download pinned.';
      Exit;
    end;
    gUltimateMode := 'pin';
  end
  else if mode = 'zip' then
  begin
    if gUltimateArchive = '' then
    begin
      Result := 'No zip file was given for Rock Band Blitz Ultimate.';
      Exit;
    end;
    if not FileExists(gUltimateArchive) then
    begin
      Result := gUltimateArchive + ' does not exist.';
      Exit;
    end;
    gUltimateMode := 'zip';
  end
  else
  begin
    if gUltimateFolder = '' then
    begin
      Result := 'No folder was given for Rock Band Blitz Ultimate.';
      Exit;
    end;
    if not DirExists(gUltimateFolder) then
    begin
      Result := gUltimateFolder + ' does not exist.';
      Exit;
    end;
    gUltimateMode := 'folder';
  end;
  gUltimateActive := True;
end;

function UltimateSourceArgs: String;
begin
  Result := '';
  if not gUltimateActive then
    Exit;
  if gUltimateMode = 'pin' then
    Result := ' --from-pinned'
  else if gUltimateMode = 'zip' then
    Result := ' --from-zip ' + QuoteArg(gUltimateArchive)
  else if gUltimateMode = 'folder' then
    Result := ' --from-dir ' + QuoteArg(gUltimateFolder);
end;

// --- how much room the install needs --------------------------------------

// Multiplication that cannot overflow on the way: the payload of a real release
// is large enough that three copies of it as 32-bit integers would wrap.
function Times(const amount: Integer; const count: Integer): Int64;
var
  index: Integer;
begin
  Result := 0;
  for index := 1 to count do
    Result := Result + amount;
end;

function RequiredBytes: Int64;
begin
  Result := gGameSourceBytes + FreeSpaceSlackMiB * MegabyteBytes;
  if gPayloadDownload then
  begin
    if PayloadSizeBytes > 0 then
      Result := Result + Times(PayloadSizeBytes, 3)
    else
      Result := Result + PayloadDownloadFallbackMiB * MegabyteBytes;
  end
  else
    Result := Result + PayloadSlackMiB * MegabyteBytes;
  if gUltimateActive then
    Result := Result + Times(UltimateArchiveBytes, 3);
end;

// ==========================================================================
// wizard events
// ==========================================================================

function InitializeSetup: Boolean;
begin
  gFailed := False;
  gFailureText := '';
  gGameSourceBytes := 0;
  gUltimateActive := False;
  gUltimateMode := 'none';
  gPayloadDownload := PayloadIsDownload <> 0;
  Result := True;
end;

procedure InitializeWizard;
begin
  gTempDir := ExpandConstant('{tmp}');
  gAppDir := '';
  gStageSummaryFile := JoinPath(gTempDir, 'rbblitz-stage-summary.txt');
  gProbeSummaryFile := JoinPath(gTempDir, 'rbblitz-probe-summary.txt');
  gProbeDetailsFile := JoinPath(gTempDir, 'rbblitz-probe-details.txt');
  gProbeLogFile := JoinPath(gTempDir, 'rbblitz-probe.log');
  gProgressFile := JoinPath(gTempDir, 'rbblitz-progress.txt');

  MethodPage := CreateInputOptionPage(wpSelectDir,
    'Where does the game data come from?',
    'Rock Band Blitz needs the files from your own Xbox 360 copy.',
    'Choose the way you want to provide them. Nothing is uploaded anywhere, and the folder or package is only read.',
    True, False);
  MethodPage.Add('Use the Xbox 360 package I downloaded (GOD/STFS file)');
  MethodPage.Add('Use a game folder I have already extracted');
  MethodPage.Add('Keep the game data that is already installed in the folder above');
  MethodPage.SelectedValueIndex := MethodPackage;

  FolderPage := CreateInputDirPage(MethodPage.ID,
    'Where is the extracted game folder?',
    'Point at the folder that holds the game files (default.xex and the gen folder).',
    'The folder may be the extract root or a folder above it: the installer looks for the game files, it does not need them at the top.',
    False, '');
  FolderPage.Add('Extracted game folder:');

  PackagePage := CreateInputFilePage(MethodPage.ID,
    'Where is the Xbox 360 package?',
    'Point at the package file you downloaded from your console or from the store.',
    'This is the container file that holds the game data. It is read once, and only the game files inside it are copied.');
  PackagePage.Add('Xbox 360 package:', 'All files|*.*', '');

  UltimatePage := CreateInputOptionPage(PackagePage.ID,
    'Rock Band Blitz Ultimate',
    'An optional community mod that adds the songs and the modes of the console versions.',
    'It is not required to play, and this installer does not distribute it: it downloads the pinned release from the mod''s own project page, or uses a copy you provide.',
    True, False);
  UltimatePage.Add('Do not install the mod');
  UltimatePage.Add('Download Rock Band Blitz Ultimate {#UltimateVersion} from its GitHub release');
  UltimatePage.Add('Install from a zip file I have');
  UltimatePage.Add('Install from a folder I have');
  UltimatePage.SelectedValueIndex := UltimateNothing;
  if '{#UltimateUrl}' = '' then
    UltimatePage.CheckListBox.ItemEnabled[UltimatePinned] := False;

  UltimateZipPage := CreateInputFilePage(UltimatePage.ID,
    'Which zip file?',
    'Point at the Rock Band Blitz Ultimate zip you downloaded.',
    'The archive must hold the mod''s gen/patch_xbox.hdr and gen/patch_xbox_0.ark files.');
  UltimateZipPage.Add('Ultimate zip file:', 'Zip archives|*.zip|All files|*.*', '.zip');

  UltimateFolderPage := CreateInputDirPage(UltimateZipPage.ID,
    'Which folder?',
    'Point at the folder that holds the extracted Rock Band Blitz Ultimate files.',
    'The folder may be the extract root or a folder above it: the installer looks for the mod''s gen/patch_xbox.hdr file.',
    False, '');
  UltimateFolderPage.Add('Ultimate folder:');

  StagePage := CreateOutputProgressPage('Installing',
    'The installer is building your game folder. This takes a few minutes for the game data.');

  RefreshMethodPage;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if WizardSilent then
  begin
    Result := (PageID = MethodPage.ID) or (PageID = FolderPage.ID) or (PageID = PackagePage.ID) or
              (PageID = UltimatePage.ID) or (PageID = UltimateZipPage.ID) or
              (PageID = UltimateFolderPage.ID);
    Exit;
  end;
  if PageID = FolderPage.ID then
    Result := (MethodPage.SelectedValueIndex <> MethodFolder) or
              (not MethodPage.CheckListBox.ItemEnabled[MethodFolder])
  else if PageID = PackagePage.ID then
    Result := (MethodPage.SelectedValueIndex <> MethodPackage) or
              (not MethodPage.CheckListBox.ItemEnabled[MethodPackage])
  else if PageID = UltimateZipPage.ID then
    Result := UltimatePage.SelectedValueIndex <> UltimateZip
  else if PageID = UltimateFolderPage.ID then
    Result := UltimatePage.SelectedValueIndex <> UltimateFolder;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  answer: Integer;
begin
  Result := True;
  if WizardSilent then
    Exit;

  if CurPageID = wpSelectDir then
  begin
    RefreshMethodPage;
    Exit;
  end;

  if CurPageID = FolderPage.ID then
  begin
    if Trim(FolderPage.Values[0]) = '' then
    begin
      SuppressibleMsgBox('Choose the folder that holds the extracted game files.',
                         mbError, MB_OK, IDOK);
      Result := False;
      Exit;
    end;
    ProbeGameFolder(Trim(FolderPage.Values[0]));
    if gFailureText <> '' then
    begin
      SuppressibleMsgBox('That folder cannot be used:' + kCrLf + kCrLf + gFailureText + kCrLf + kCrLf +
                         'Pick the folder that holds default.xex and the gen folder, or a folder above it.',
                         mbError, MB_OK, IDOK);
      FolderPage.SubCaptionLabel.Caption := '';
      Result := False;
      Exit;
    end;
    FolderPage.SubCaptionLabel.Caption := 'Ready: ' + gGameSourceDescription;
    FolderPage.SubCaptionLabel.Update;
    Exit;
  end;

  if CurPageID = PackagePage.ID then
  begin
    if Trim(PackagePage.Values[0]) = '' then
    begin
      SuppressibleMsgBox('Choose the Xbox 360 package file.', mbError, MB_OK, IDOK);
      Result := False;
      Exit;
    end;
    ProbeGamePackage(Trim(PackagePage.Values[0]));
    if gFailureText <> '' then
    begin
      SuppressibleMsgBox('That file cannot be used:' + kCrLf + kCrLf + gFailureText + kCrLf + kCrLf +
                         'Check that it is the package holding Rock Band Blitz.',
                         mbError, MB_OK, IDOK);
      PackagePage.SubCaptionLabel.Caption := '';
      Result := False;
      Exit;
    end;
    PackagePage.SubCaptionLabel.Caption := 'Ready: ' + gGameSourceDescription;
    PackagePage.SubCaptionLabel.Update;
    Exit;
  end;

  if CurPageID = UltimateZipPage.ID then
  begin
    if not ProbeUltimateArchive(Trim(UltimateZipPage.Values[0])) then
    begin
      answer := SuppressibleMsgBox('That zip does not look like the Rock Band Blitz Ultimate release:' +
                                   kCrLf + kCrLf + gFailureText + kCrLf + kCrLf +
                                   'Install it anyway? The installer will refuse it if its files are wrong.',
                                   mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO);
      if answer <> IDYES then
      begin
        UltimateZipPage.SubCaptionLabel.Caption := '';
        Result := False;
        Exit;
      end;
      UltimateZipPage.SubCaptionLabel.Caption := 'Not verified: the archive does not look like the Ultimate release.';
    end
    else
      UltimateZipPage.SubCaptionLabel.Caption := 'Looks right: ' + Trim(SummaryText(gProbeSummaryFile, 'description', 'the archive'));
    UltimateZipPage.SubCaptionLabel.Update;
    Exit;
  end;

  if CurPageID = UltimateFolderPage.ID then
  begin
    if not LooksLikeUltimateTree(Trim(UltimateFolderPage.Values[0])) then
    begin
      answer := SuppressibleMsgBox('That folder does not look like the Rock Band Blitz Ultimate release:' +
                                   kCrLf + kCrLf + 'it holds no ' + UltimateHeaderRel + '.' + kCrLf + kCrLf +
                                   'Install it anyway? The installer will refuse it if its files are wrong.',
                                   mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO);
      if answer <> IDYES then
      begin
        UltimateFolderPage.SubCaptionLabel.Caption := '';
        Result := False;
        Exit;
      end;
      UltimateFolderPage.SubCaptionLabel.Caption := 'Not verified: no ' + UltimateHeaderRel + ' found.';
    end
    else
      UltimateFolderPage.SubCaptionLabel.Caption := 'Looks right: ' + UltimateHeaderRel + ' is there.';
    UltimateFolderPage.SubCaptionLabel.Update;
    Exit;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  method: Integer;
  problem: String;
  needed: Int64;
begin
  NeedsRestart := False;
  gAppDir := WizardDirValue;
  gLogFile := JoinPath(gAppDir, 'install.log');
  gGameDetailsFile := JoinPath(gAppDir, 'game-source-details.txt');
  gFailureText := '';

  method := ChosenGameMethod;
  if method < 0 then
  begin
    Result := 'No game data source was given. Run the installer normally, or pass GAMEFOLDER=... or ' +
              'GAMEPACKAGE=... (see README.md for the silent install parameters).';
    Exit;
  end;

  if method = MethodInstalled then
  begin
    if not GameDataPresent then
    begin
      Result := gAppDir + ' holds no game data yet, so there is nothing to keep. Choose the package or a ' +
                'game folder instead.';
      Exit;
    end;
    gGameSourceDescription := 'the game data already installed in ' + GameDataDirectory;
    gGameSourceBytes := InstalledDataSlackMiB * MegabyteBytes;
  end
  else if method = MethodFolder then
  begin
    if ChosenGameFolder = '' then
    begin
      Result := 'GAMEFOLDER was given without a folder.';
      Exit;
    end;
    ProbeGameFolder(ChosenGameFolder);
    if gFailureText <> '' then
    begin
      Result := 'That game folder cannot be used: ' + gFailureText;
      Exit;
    end;
  end
  else
  begin
    if ChosenGamePackage = '' then
    begin
      Result := 'GAMEPACKAGE was given without a file.';
      Exit;
    end;
    ProbeGamePackage(ChosenGamePackage);
    if gFailureText <> '' then
    begin
      Result := 'That package cannot be used: ' + gFailureText;
      Exit;
    end;
  end;

  problem := ResolveUltimateChoice;
  if problem <> '' then
  begin
    Result := problem;
    Exit;
  end;

  needed := RequiredBytes;
  if not RunQuietProbe('check-space --dest ' + QuoteArg(gAppDir) + ' --required-bytes ' +
                       Int64ToStr(needed)) then
  begin
    Result := 'Not enough room for this install: ' + HelperError(gProbeSummaryFile) +
              '. Go back and choose another destination folder, or free up some space.';
    Exit;
  end;

  Result := '';
end;

// Every stage runs after Inno Setup has copied the payload into the install
// folder: the embedded build is only verifiable there, and a failure here leaves
// a folder the user can see the state of.
procedure CurStepChanged(CurStep: TSetupStep);
var
  args: String;
  method: Integer;
  problem: String;
begin
  if CurStep <> ssPostInstall then
    Exit;

  gAppDir := WizardDirValue;
  gLogFile := JoinPath(gAppDir, 'install.log');
  gGameDetailsFile := JoinPath(gAppDir, 'game-source-details.txt');
  gFailed := False;
  gStageActive := True;

  // 1. the recompiled build
  if gPayloadDownload then
  begin
    args := 'install-payload --dest ' + QuoteArg(gAppDir) + ' --from-pinned';
    if not RunStage('Downloading the recompiled build',
                    'Fetching the build pinned by this installer and checking it...', args,
                    PayloadStart, PayloadEnd) then
    begin
      gFailed := True;
      SuppressibleMsgBox('The recompiled build could not be installed:' + kCrLf + kCrLf + gFailureText +
                         kCrLf + kCrLf + 'Nothing else was installed. Check your internet connection and ' +
                         'run the installer again.', mbError, MB_OK, IDOK);
      Exit;
    end;
    NoteSource(gStageSummaryFile, 'source', 'version', gPayloadSource, gPayloadVersion);
  end
  else
  begin
    if not RunStage('Checking the recompiled build',
                    'Verifying the files the installer placed, one hash at a time...',
                    'verify-payload --dest ' + QuoteArg(gAppDir), PayloadStart, PayloadEnd) then
    begin
      gFailed := True;
      SuppressibleMsgBox('The recompiled build does not match what this installer should have placed:' +
                         kCrLf + kCrLf + gFailureText + kCrLf + kCrLf +
                         'Nothing else was installed. Run the installer again.', mbError, MB_OK, IDOK);
      Exit;
    end;
    NoteSource(gStageSummaryFile, 'source', 'version', gPayloadSource, gPayloadVersion);
    if gPayloadSource = '' then
      gPayloadSource := 'embedded in this installer';
  end;

  // 2. the game data
  // Nothing to import when the user keeps what is already there.
  method := ChosenGameMethod;
  if method <> MethodInstalled then
  begin
    if method = MethodFolder then
      args := 'import-game --dest ' + QuoteArg(gAppDir) + ' --folder ' + QuoteArg(ChosenGameFolder)
    else
      args := 'import-game --dest ' + QuoteArg(gAppDir) + ' --package ' + QuoteArg(ChosenGamePackage);
    if not RunStage('Importing the game data',
                    'Copying your game files into the install folder and checking them...', args,
                    GameStart, GameEnd) then
    begin
      gFailed := True;
      SuppressibleMsgBox('The game data could not be imported:' + kCrLf + kCrLf + gFailureText +
                         kCrLf + kCrLf + 'The recompiled build is in place, but the game cannot run ' +
                         'without the data. Run the installer again with a working source.',
                         mbError, MB_OK, IDOK);
      Exit;
    end;
  end;

  // 3. the mod, if it was asked for. The game is playable without it, so a
  //    failure here is loud but not fatal.
  if gUltimateActive then
  begin
    args := 'install-ultimate --dest ' + QuoteArg(gAppDir) + UltimateSourceArgs;
    if RunStage('Installing Rock Band Blitz Ultimate',
                'Adding the mod''s songs and modes to the game folder...', args, UltimateStart,
                UltimateEnd) then
    begin
      NoteSource(gStageSummaryFile, 'source', 'version', gUltimateSourceDescription, gUltimateVersion);
      if gUltimateVersion = '' then
        gUltimateVersion := '{#UltimateVersion}';
    end
    else
    begin
      SuppressibleMsgBox('Rock Band Blitz Ultimate could not be installed:' + kCrLf + kCrLf +
                         gFailureText + kCrLf + kCrLf +
                         'The game itself was installed and can be played without the mod.',
                         mbError, MB_OK, IDOK);
      gUltimateActive := False;
    end;
  end;

  // 4. the record of what is on disk
  args := 'finalize --dest ' + QuoteArg(gAppDir) +
          ' --game-source ' + QuoteArg(gGameSourceDescription) +
          ' --installer-version ' + QuoteArg('{#AppVersion}');
  if gPayloadSource <> '' then
    args := args + ' --payload-source ' + QuoteArg(gPayloadSource);
  if gPayloadVersion <> '' then
    args := args + ' --payload-version ' + QuoteArg(gPayloadVersion);
  if gUltimateActive then
  begin
    args := args + ' --ultimate 1';
    if gUltimateSourceDescription <> '' then
      args := args + ' --ultimate-source ' + QuoteArg(gUltimateSourceDescription);
    if gUltimateVersion <> '' then
      args := args + ' --ultimate-version ' + QuoteArg(gUltimateVersion);
  end
  else
    args := args + ' --ultimate 0';

  if not RunStage('Finishing up', 'Writing the install manifest and the report...', args,
                  FinalizeStart, FinalizeEnd) then
  begin
    problem := gFailureText;
    SuppressibleMsgBox('The install manifest could not be written:' + kCrLf + kCrLf + problem +
                       kCrLf + kCrLf + 'The game itself is installed; see ' + gLogFile + '.',
                       mbError, MB_OK, IDOK);
  end;

  gStageActive := False;
end;

function InstallSucceeded: Boolean;
begin
  Result := not gFailed;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  appDir, helper, args, tempDir: String;
  keepData: Boolean;
  code: Integer;
begin
  if CurUninstallStep <> usUninstall then
    Exit;

  appDir := ExpandConstant('{app}');
  helper := JoinPath(appDir, HelperExeName);
  // Without the helper there is nothing to ask, and the files it wrote are
  // covered by [UninstallDelete] instead.
  if not FileExists(helper) then
    Exit;

  tempDir := ExpandConstant('{tmp}');
  args := 'uninstall-cleanup --dest ' + QuoteArg(appDir);
  // A silent uninstall keeps the game data: it cannot ask, and deleting a
  // several-hundred-megabyte import that the user may still want is the worse
  // of the two mistakes.
  if UninstallSilent then
    keepData := True
  else
    keepData := SuppressibleMsgBox('Delete the imported Rock Band Blitz game data as well?' + kCrLf +
                                   kCrLf + 'Answer No to keep it, for example to reinstall later ' +
                                   'without the disc or the package again.',
                                   mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDNO;
  if keepData then
    args := args + ' --keep-game-data';

  Exec(helper, args + ' --summary ' + QuoteArg(JoinPath(tempDir, 'rbblitz-uninstall-summary.txt')) +
       ' --log ' + QuoteArg(JoinPath(tempDir, 'rbblitz-uninstall.log')),
       appDir, SW_HIDE, ewWaitUntilTerminated, code);
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if (not gStageActive) or Cancel then
    Exit;
  Confirm := False;
  if SuppressibleMsgBox('The installer is still working on your game folder.' + kCrLf + kCrLf +
                        'Stopping now can leave the install half finished. Stop anyway?',
                        mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
    Cancel := True;
end;

function UpdateReadyMemo(const Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
                         MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  lines: String;
  method: Integer;
begin
  lines := MemoDirInfo + NewLine;
  method := ChosenGameMethod;
  if method = MethodInstalled then
    lines := lines + Space + 'Game data: keep what is already installed' + NewLine
  else if method = MethodFolder then
    lines := lines + Space + 'Game data: ' + ChosenGameFolder + NewLine
  else
    lines := lines + Space + 'Game data: ' + ChosenGamePackage + NewLine;

  if gUltimateActive then
  begin
    if gUltimateMode = 'pin' then
      lines := lines + Space + 'Rock Band Blitz Ultimate ' + '{#UltimateVersion}' +
               ': download from the mod''s release page' + NewLine
    else if gUltimateMode = 'zip' then
      lines := lines + Space + 'Rock Band Blitz Ultimate: ' + gUltimateArchive + NewLine
    else
      lines := lines + Space + 'Rock Band Blitz Ultimate: ' + gUltimateFolder + NewLine;
  end
  else
    lines := lines + Space + 'Rock Band Blitz Ultimate: not installed' + NewLine;

  if gPayloadDownload then
    lines := lines + Space + 'Recompiled build: downloaded from the pinned release' + NewLine
  else
    lines := lines + Space + 'Recompiled build: included in this installer' + NewLine;

  Result := lines + MemoGroupInfo + MemoTasksInfo;
end;

function GetCustomSetupExitCode: Integer;
begin
  if gFailed then
    Result := 9
  else
    Result := 0;
end;
