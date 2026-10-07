; ---------------------------------------------------------------------------
; Rock Band Blitz Xenon Recomp - installer wizard.
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
; The same script is compiled twice, and /DUpdaterMode decides which of the two
; builds the result is (README.md, section "Updating"):
;
;   the setup executable, as before - it embeds (or downloads) the payload and
;   asks all four questions, and it puts RockBandBlitzUpdater.exe under the
;   user's local application data;
;
;   the updater - the same wizard with the payload taken from the release
;   manifest instead of its own build, and with the pages it has no question
;   about removed. The launcher runs it when the user accepts an update, so it
;   never carries a payload and never touches the install folder's shortcuts.
;
; The directories below can be overridden from the command line so a release can
; point at artefacts built elsewhere:
;   /DGeneratedDir=<dir>  holds pins.iss, update.toml, a copy of the helper and
;                         the updater the setup embeds
;   /DPayloadDir=<dir>    holds the recompiled build that gets embedded
;   /DDistDir=<dir>       receives the setup executable (and the updater)
;   /DArtDir=<dir>        optional side wizard image (default: the repository's
;                         assets\ directory; see tools/make_art.ps1)
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
  #define ArtDir ProjectDirPath + "assets"
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
#define LauncherExeName "rb_blitz_launcher.exe"
#define GameDirName "game"

; The updater the setup executable places under the user's local application data
; (see "Updating" in README.md). build.ps1 compiles it from this same script with
; /DUpdaterMode=1 before the setup, so the setup can embed it - a build without it
; is refused below rather than shipping a launcher whose Update button cannot work.
#ifndef UpdaterExeName
  #define UpdaterExeName "RockBandBlitzUpdater.exe"
#endif
#ifndef UpdaterExePath
  #define UpdaterExePath GeneratedDirPath + UpdaterExeName
#endif

#if !FileExists(GeneratedDirPath + "pins.iss")
  #error pins.iss is missing from the generated directory: run build.ps1 (or the helper build) first.
#endif
#include GeneratedDirPath + "pins.iss"

#if !FileExists(GeneratedDirPath + HelperExeName)
  #error the helper is missing from the generated directory: build.ps1 copies it there.
#endif

#ifndef UpdaterMode
  #if !FileExists(UpdaterExePath)
    #error the updater is missing from the generated directory: build.ps1 compiles it before the setup.
  #endif
#endif

; Without a pinned download the recompiled build has to travel inside the setup
; executable, so the payload directory must have been made first. With a pinned
; download it is fetched at install time and nothing is embedded. The updater
; never embeds one: it installs the payload its release manifest names.
#ifndef UpdaterMode
  #if (PayloadHasDownload == 0) && (!FileExists(PayloadDirPath + "payload-manifest.toml"))
    #error no payload to embed: pass /DPayloadUrl and /DPayloadSha256, or create the payload snapshot first.
  #endif
#endif

; Setup's version resource needs four components; the app version has three.
#define SetupVersionQuad AppVersion + ".0"

; D9 names the two Start-menu shortcuts so a list can tell them apart: the game
; keeps AppShortName, the launcher appends " Launcher" (pins.iss defines
; AppShortName, so this has to come after the include above).
#define LauncherShortcutName AppShortName + " Launcher"

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
#ifndef UpdaterMode
DisableWelcomePage=no
#endif
ShowLanguageDialog=no
SetupLogging=yes
SetupIconFile={#ProjectDirPath}assets\blitz.ico
; Add/Remove Programs names the program, not the build: this is the one place the
; name a user sees is the short one, because "{#AppName} {#AppVersion}" there reads
; like the game has a "(Xenon Recomp) version 0.1.0" edition. The GPL text no
; longer has a page of its own either - nothing in this wizard is agreed to, so
; the licence lives with the sources (see README.md).
UninstallDisplayName={#AppShortName}
UninstallDisplayIcon={app}\{#GameExeName}

; The side image is optional: it only appears when tools/make_art.ps1 produced it.
; The art is not committed (see .gitignore), and the licence of the artwork is
; not ours, so its absence is a supported configuration. The area the image has to
; fill grows with the user's DPI setting, so make_art.ps1 writes the whole ladder
; of sizes Inno Setup documents; the wildcard below lets Setup pick, on the
; machine it runs on, the file that best matches the area it has to fill.
; WizardImageStretch is deliberately left at its default (yes): on a DPI setting
; that is not one of the documented ones it fills the area instead of leaving bars.
#if FileExists(ArtDirPath + "wizard-large-202x386.bmp")
WizardImageFile={#ArtDirPath}wizard-large-*.bmp
#endif

; The corner badge is the app's own icon art, so unlike the side image it is
; committed next to blitz.ico and needs no guard. It is stored at the largest size
; the badge area reaches (159x159 at 250% scaling), so it is only ever shrunk.
WizardSmallImageFile={#ProjectDirPath}assets\blitz.png

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Welcome to the {#AppName} setup
WelcomeLabel2=This is a PC decompilation project of the Xbox 360 version of Rock Band Blitz.%n%nIt installs a ready-to-play build and takes the game data from your own Xbox 360 copy: an extracted game folder or the package. No game files are included or downloaded, and this is not affiliated with or endorsed by the game's publisher.%n%nContinue when the folder or the package is at hand.
WizardSelectTasks=Shortcuts
SelectTasksDesc=Choose which shortcuts to create.
SelectTasksLabel2=Select the shortcuts you would like Setup to create, then click Next.
; Inno Setup counts only the files it places itself - the recompiled build, about
; 67 MB - while the game data the user supplies is several hundred megabytes more
; and is not known until the next page. Its own sentence would therefore be a
; confidently wrong answer to "is there room?"; the wizard measures the real
; requirement before the install starts instead.
DiskSpaceMBLabel=The game data you provide is not counted here; the free space is checked again before the install starts.

[Tasks]
; Every shortcut is a task, so this page is the one place that decides what is
; created. The Start menu pair is checked, as those two have always been created;
; the launcher's desktop shortcut is checked (D9) and the game's desktop shortcut
; stays unchecked, as it always has been. A silent install creates every checked
; task, except the launcher's desktop shortcut, which only /LAUNCHERICON=1 asks
; for (see README.md).
;
; The updater has no shortcuts page and creates none: the install it is updating
; already has whatever the user asked for, and re-creating a shortcut the user has
; since deleted is not an update's job.
#ifndef UpdaterMode
Name: "startmenu"; Description: "Create a &Start menu shortcut for the game"
Name: "startmenulauncher"; Description: "Create a Start menu shortcut for the &launcher"
Name: "launchericon"; Description: "Create a &desktop shortcut for the launcher"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked
#endif

[Files]
; The wizard needs the helper while it is still on screen (to check the user's
; choices and the free space), so it is extracted to the temporary directory as
; well. Temporary files must sit at the very top when solid compression is on.
Source: "{#GeneratedDirPath}{#HelperExeName}"; DestDir: "{tmp}"; Flags: dontcopy noencryption
#ifndef UpdaterMode
; The helper lands in the install folder with the rest of the wizard's own files.
Source: "{#GeneratedDirPath}{#HelperExeName}"; DestDir: "{app}"; Flags: ignoreversion
#else
; The updater repairs a missing helper but never replaces one that is there. Its own
; helper is the build of whatever release last put this updater in place, which can be
; older than the helper a later release left behind; copying it over would be an update
; that moved a file backwards. A full install is what refreshes the helper.
Source: "{#GeneratedDirPath}{#HelperExeName}"; DestDir: "{app}"; Flags: onlyifdoesntexist
#endif
#ifndef UpdaterMode
; The updater, under the user's local application data rather than in the install
; folder: the launcher looks for it there, and the install folder is the game's own
; (the payload audit lists what may ship in it). It is small - a wizard with no
; payload in it - and [UninstallDelete] takes it away again.
Source: "{#UpdaterExePath}"; DestDir: "{localappdata}\{#UpdateDirName}\update"; Flags: ignoreversion
  #if PayloadHasDownload == 0
; The recompiled build and its runtime DLLs, straight into the install folder.
; The payload directory is a plain snapshot of the release build (see
; tools/make_payload.ps1); its dev-only configuration file is not part of it.
Source: "{#PayloadDirPath}*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
  #endif
#endif

[Icons]
; Four shortcuts, each behind the [Tasks] entry the Shortcuts page shows, with
; names a Start-menu list can tell apart. The game's keeps its --game_data_root
; argument: it must stay runnable without the launcher (Contract 4). The
; launcher's takes none; it finds the game beside itself.
#ifndef UpdaterMode
Name: "{autoprograms}\{#AppShortName}"; Filename: "{app}\{#GameExeName}"; Parameters: "--game_data_root=""{app}\{#GameDirName}"""; WorkingDir: "{app}"; Tasks: startmenu
Name: "{autoprograms}\{#LauncherShortcutName}"; Filename: "{app}\{#LauncherExeName}"; WorkingDir: "{app}"; Tasks: startmenulauncher
Name: "{autodesktop}\{#AppShortName}"; Filename: "{app}\{#GameExeName}"; Parameters: "--game_data_root=""{app}\{#GameDirName}"""; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{autodesktop}\{#LauncherShortcutName}"; Filename: "{app}\{#LauncherExeName}"; WorkingDir: "{app}"; Tasks: launchericon
#endif

; D8 replaced the single postinstall [Run] entry with the finish page's three-way
; choice (see [Code] below): Inno can run at most one entry with a checkbox, and
; the wizard now asks open game / open launcher / do nothing.

[UninstallDelete]
; Files the helper wrote that Inno Setup never saw: the ones it downloaded, the
; manifests and the log. Anything the installer placed itself is already tracked
; by the uninstaller, so these entries only cost a failed delete.
Type: files; Name: "{app}\{#HelperExeName}"
Type: files; Name: "{app}\{#GameExeName}"
; The launcher travels in the payload (see tools/make_payload.ps1) and is deleted
; with the rest of it. launcher.toml is deliberately not listed: it is the user's
; own settings and survives an uninstall, like the game data (Contract 4).
Type: files; Name: "{app}\{#LauncherExeName}"
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\payload-manifest.toml"
Type: files; Name: "{app}\install-manifest.toml"
Type: files; Name: "{app}\install-report.txt"
Type: files; Name: "{app}\game-source-details.txt"
Type: files; Name: "{app}\install.log"
Type: filesandordirs; Name: "{app}\.staging"
Type: filesandordirs; Name: "{app}\{#GameDirName}\.staging"
; The updater the setup placed under the user's local application data, and the
; folder it lives in. It is outside the install folder on purpose (D19): the
; launcher runs it, and a second copy of an installer inside the game's own
; directory would be one more file the payload audit has to allow.
Type: filesandordirs; Name: "{localappdata}\{#UpdateDirName}\update"
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

  { The updater: a second build of this script (/DUpdaterMode=1) that carries no
    payload and installs the build the release manifest names. The setup executable
    puts it under the user's local application data, where the launcher looks for it
    (launcher/src/update_launcher.cpp resolves the same folder). }
  UpdaterDirName = '{#UpdateDirName}';
  UpdaterLeafDir = 'update';
  UpdateManifestRel = 'rbblitz-update.toml';

  { Silent-mode parameters (see README.md). }
  MethodParam = 'GAMEMETHOD';
  GameFolderParam = 'GAMEFOLDER';
  GamePackageParam = 'GAMEPACKAGE';
  UltimateSourceParam = 'ULTIMATESOURCE';
  UltimateZipParam = 'ULTIMATEZIP';
  UltimateFolderParam = 'ULTIMATEFOLDER';
  LauncherIconParam = 'LAUNCHERICON';
  RunAtEndParam = 'RUNATEND';

  { D8: what the finish page can do. Strings rather than indexes, because the same
    values are what /RUNATEND accepts on a silent install (README.md). }
  RunAtEndGame = 'game';
  RunAtEndLauncher = 'launcher';
  RunAtEndNothing = 'none';

  { The launcher's desktop icon: the [Tasks] name, shared by the [Icons] entry
    and the code that applies D9's default and the silent switch above. }
  LauncherIconTask = 'launchericon';

  { Page indexes. }
  MethodPackage = 0;
  MethodFolder = 1;
  MethodInstalled = 2;
  { The mod's choices in the order the page lists them: the pinned download first
    and "do not install" last, with the user's own copy in between. }
  UltimatePinned = 0;
  UltimateZip = 1;
  UltimateFolder = 2;
  UltimateNothing = 3;

  { The wizard's own registration key: the AppId from [Setup], which Inno Setup
    registers an installation under. Read back at start-up to find a game that is
    already installed, so this is also the one place that GUID would have to
    change if AppId ever does. }
  AppIdGuid = '{92C7B4E1-3F5A-4D2E-9B18-7A6C4E0D5F31}';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + AppIdGuid + '_is1';

  { Values the build script pinned. }
  PayloadSizeBytes = {#PayloadSize};
  PayloadIsDownload = {#PayloadHasDownload};
  UltimateArchiveBytes = {#UltimateSize};
  { The release manifest a launcher checks and the updater installs from. Empty in
    a build that has no release channel, which the updater then says out loud. }
  UpdateManifestUrl = '{#UpdateManifestUrl}';

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
  gPayloadSize: Int64;
  gUltimateActive: Boolean;
  gUltimateMode: String;
  gUltimateArchive: String;
  gUltimateFolder: String;
  gUltimateSourceDescription: String;
  gUltimateVersion: String;

  gInstalled: Boolean;
  gFinishReached: Boolean;
  gRunAtEnd: String;
  gStageVisible: Boolean;

  { The updater's own state (see "Updating" in README.md). It is initialised either
    way - the setup executable reads none of it - so the same code compiles into
    both builds without a second set of globals. }
  gUpdater: Boolean;
  gUpdateManifestFile: String;
  gUpdateTargetDir: String;
  gUpdateVersion: String;
  gUpdateRequiresGameData: Boolean;
  gUpdatePayloadVersion: String;
  gUpdatePayloadUrl: String;
  gUpdatePayloadSha256: String;
  gUpdatePayloadCommit: String;
  gUpdatePayloadSize: Int64;

  EndGameRadio: TNewRadioButton;
  EndLauncherRadio: TNewRadioButton;
  EndNothingRadio: TNewRadioButton;

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

// The scratch files every stage and probe writes. Resolved before the first page
// rather than in InitializeWizard, because the updater reads the release manifest
// from here while Setup is still deciding which pages to show.
procedure InitTempPaths;
begin
  gTempDir := ExpandConstant('{tmp}');
  gStageSummaryFile := JoinPath(gTempDir, 'rbblitz-stage-summary.txt');
  gProbeSummaryFile := JoinPath(gTempDir, 'rbblitz-probe-summary.txt');
  gProbeDetailsFile := JoinPath(gTempDir, 'rbblitz-probe-details.txt');
  gProbeLogFile := JoinPath(gTempDir, 'rbblitz-probe.log');
  gProgressFile := JoinPath(gTempDir, 'rbblitz-progress.txt');
  gUpdateManifestFile := JoinPath(gTempDir, UpdateManifestRel);
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

function ParamLauncherIcon: String;
begin
  Result := Trim(ExpandConstant('{param:LAUNCHERICON|}'));
end;

// D8: the finish page's three-way choice. The game is the interactive default; a
// silent install does nothing unless /RUNATEND asks for one, as the old
// skipifsilent [Run] entry did. The radios set gRunAtEnd; DeinitializeSetup runs
// it after the wizard closes.
function ParamRunAtEnd: String;
begin
  Result := Lowercase(Trim(ExpandConstant('{param:RUNATEND|}')));
end;

procedure ResolveRunAtEnd;
var
  requested: String;
begin
  // An update ends where it started: the launcher is what asked the user, and
  // coming back to it - one release newer - is what the user agreed to. The three
  // finish-page radios are not shown for it either (CurPageChanged), so this is the
  // only thing that decides.
  if gUpdater then
  begin
    gRunAtEnd := RunAtEndLauncher;
    Exit;
  end;
  requested := ParamRunAtEnd;
  if requested <> '' then
    gRunAtEnd := requested
  else if WizardSilent then
    gRunAtEnd := RunAtEndNothing
  else
    gRunAtEnd := RunAtEndGame;
end;

function RunAtEndProblem: String;
begin
  Result := '';
  if (gRunAtEnd <> RunAtEndGame) and (gRunAtEnd <> RunAtEndLauncher) and
     (gRunAtEnd <> RunAtEndNothing) then
    Result := RunAtEndParam + ' must be ' + RunAtEndGame + ', ' + RunAtEndLauncher + ' or ' +
              RunAtEndNothing + ', not ''' + gRunAtEnd + '''.';
end;

procedure EndGameRadioClick(Sender: TObject);
begin
  gRunAtEnd := RunAtEndGame;
end;

procedure EndLauncherRadioClick(Sender: TObject);
begin
  gRunAtEnd := RunAtEndLauncher;
end;

procedure EndNothingRadioClick(Sender: TObject);
begin
  gRunAtEnd := RunAtEndNothing;
end;

// The finished label's height depends on its text and the DPI, so the radios are
// placed under it when the page is shown rather than at start-up. They read top
// to bottom in the order they are listed here, and the game is named once: with
// the mod installed the install folder is still the same game, so "Launch
// Rock Band Blitz" is the honest caption for both.
procedure LayoutFinishChoices;
var
  top, step, left, width: Integer;
begin
  left := WizardForm.FinishedLabel.Left;
  width := WizardForm.FinishedLabel.Width;
  step := ScaleY(21);
  top := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(8);

  EndLauncherRadio.Caption := 'Open the launcher';
  EndGameRadio.Caption := 'Launch {#AppShortName}';
  EndNothingRadio.Caption := 'Do nothing';

  EndLauncherRadio.Left := left;
  EndLauncherRadio.Top := top;
  EndLauncherRadio.Width := width;
  EndLauncherRadio.Height := step;
  EndGameRadio.Left := left;
  EndGameRadio.Top := top + step;
  EndGameRadio.Width := width;
  EndGameRadio.Height := step;
  EndNothingRadio.Left := left;
  EndNothingRadio.Top := top + step * 2;
  EndNothingRadio.Width := width;
  EndNothingRadio.Height := step;
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

// --- the release manifest (the updater) -----------------------------------

// Fetches update.toml - what the newest release is, where its payload is and
// whether it needs the user's game files again - and fills the gUpdate* globals.
// Runs before the first page is chosen, because both answers decide which pages
// this wizard shows at all, and a wizard that asked for the package and then
// found out it was not needed would have asked a question for nothing.
//
// Failing here ends Setup: the updater has no payload of its own, so there is
// nothing it could install without the manifest, and saying so is the only honest
// answer. The launcher's Update button is still there, so the user can try again.
function FetchUpdateManifest: Boolean;
var
  reason: String;
begin
  Result := False;
  gUpdateRequiresGameData := False;
  gUpdatePayloadVersion := '';
  gUpdatePayloadUrl := '';
  gUpdatePayloadSha256 := '';
  gUpdatePayloadCommit := '';
  gUpdatePayloadSize := 0;

  if UpdateManifestUrl = '' then
  begin
    SuppressibleMsgBox('This updater was built without an update channel, so it cannot find ' +
                       'out what to install.' + kCrLf + kCrLf +
                       'Download the newest setup from the release page instead.',
                       mbError, MB_OK, IDOK);
    Exit;
  end;

  DeleteFile(gUpdateManifestFile);
  if not RunQuietProbe('fetch-update --url ' + QuoteArg(UpdateManifestUrl) +
                       ' --dest ' + QuoteArg(gUpdateManifestFile)) then
  begin
    reason := HelperError(gProbeSummaryFile);
    SuppressibleMsgBox('The newest release could not be checked:' + kCrLf + kCrLf + reason + kCrLf + kCrLf +
                       'Check the internet connection and try the update again.',
                       mbError, MB_OK, IDOK);
    Exit;
  end;

  gUpdateVersion := Trim(SummaryText(gProbeSummaryFile, 'version', ''));
  if gUpdateVersion = '' then
  begin
    SuppressibleMsgBox('The release manifest does not say which version it is, so nothing was ' +
                       'installed.' + kCrLf + kCrLf +
                       'Download the newest setup from the release page instead.',
                       mbError, MB_OK, IDOK);
    Exit;
  end;

  gUpdateRequiresGameData := SummaryFlag(gProbeSummaryFile, 'requires_game_data');
  gUpdatePayloadVersion := Trim(SummaryText(gProbeSummaryFile, 'payload_version', gUpdateVersion));
  gUpdatePayloadUrl := Trim(SummaryText(gProbeSummaryFile, 'payload_url', ''));
  gUpdatePayloadSha256 := Trim(SummaryText(gProbeSummaryFile, 'payload_sha256', ''));
  gUpdatePayloadCommit := Trim(SummaryText(gProbeSummaryFile, 'payload_commit', ''));
  gUpdatePayloadSize := SummaryBytes(gProbeSummaryFile, 'payload_size');
  if gUpdatePayloadVersion = '' then
    gUpdatePayloadVersion := gUpdateVersion;
  Result := True;
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

// A progress page is not part of the wizard's page order: nothing displays it
// unless this does, and without it the whole post-install half of the install
// runs behind Inno Setup's own bar - which is still sitting at 100% from the
// payload, so the wizard looks frozen for the minutes the game data takes.
//
// Shown once for the whole install rather than per stage, so the page does not
// blink back to the installing page between stages; the stages are consecutive.
procedure BeginStages;
begin
  if not gStageVisible then
  begin
    StagePage.Show;
    gStageVisible := True;
  end;
end;

procedure EndStages;
begin
  if gStageVisible then
  begin
    StagePage.Hide;
    gStageVisible := False;
  end;
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

// The two files whose presence means "there is a usable game root in this folder".
// Written once because three callers ask it about three different folders: the
// wizard's destination, and - in the updater - the install the registry names.
function GameDataIn(const dir: String): Boolean;
begin
  Result := False;
  if dir = '' then
    Exit;
  Result := FileExists(JoinPath(JoinPath(dir, GameDirName), GameEntryPointRel)) and
            FileExists(JoinPath(JoinPath(dir, GameDirName), GameArchiveDataRel));
end;

function GameDataPresent: Boolean;
begin
  Result := GameDataIn(WizardDirValue);
end;

// Whether the updater still has to ask where the game data comes from: the release
// says its payload changes what the game reads out of it, or the data is not in the
// install folder any more (deleted, or the install was moved by hand). Both are the
// same question to the wizard - the game-data pages - so it is asked once, here.
function UpdateNeedsGamePages: Boolean;
begin
  Result := gUpdateRequiresGameData or (not GameDataIn(gUpdateTargetDir));
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
  if gUpdater and (not UpdateNeedsGamePages) then
  begin
    // The game-data pages were not shown, so there is no answer to read: the data
    // that is already in the install folder is kept, and UpdateNeedsGamePages has
    // just checked it is complete.
    Result := MethodInstalled;
    Exit;
  end;

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
    if gPayloadSize > 0 then
      Result := Result + Times(gPayloadSize, 3)
    else
      Result := Result + PayloadDownloadFallbackMiB * MegabyteBytes;
  end
  else
    Result := Result + PayloadSlackMiB * MegabyteBytes;
  if gUltimateActive then
    Result := Result + Times(UltimateArchiveBytes, 3);
end;

// --- a game that is already installed -------------------------------------

// The next dotted component of a version string, as a number. Anything that is
// not a number counts as 0 - which is also what a component the string does not
// have counts as, so "1.0" and "1.0.0" compare equal.
function NextVersionPart(const text: String; var index: Integer): Integer;
var
  part: String;
begin
  part := '';
  while (index <= Length(text)) and (text[index] <> '.') do
  begin
    part := part + text[index];
    index := index + 1;
  end;
  if (index <= Length(text)) and (text[index] = '.') then
    index := index + 1;
  Result := StrToIntDef(Trim(part), 0);
end;

// -1, 0 or 1: which of two dotted numeric versions is newer.
function CompareVersions(const left, right: String): Integer;
var
  leftIndex, rightIndex, leftPart, rightPart: Integer;
begin
  leftIndex := 1;
  rightIndex := 1;
  repeat
    leftPart := NextVersionPart(left, leftIndex);
    rightPart := NextVersionPart(right, rightIndex);
    if leftPart <> rightPart then
    begin
      if leftPart < rightPart then
        Result := -1
      else
        Result := 1;
      Exit;
    end;
  until (leftIndex > Length(left)) and (rightIndex > Length(right));
  Result := 0;
end;

// One branch of the registry, whose Add/Remove Programs entry Inno Setup wrote
// when it installed the game. `dir` is empty when there is no usable one: a
// registration whose folder the user has since deleted is not an install to warn
// about, since the wizard would simply install into it again.
function QueryPreviousInstall(const root: Integer; var dir, version: String): Boolean;
begin
  dir := '';
  version := '';
  Result := RegQueryStringValue(root, UninstallKey, 'InstallLocation', dir) and (dir <> '');
  if not Result then
    Exit;
  if not DirExists(dir) then
  begin
    Result := False;
    Exit;
  end;
  RegQueryStringValue(root, UninstallKey, 'DisplayVersion', version);
  dir := RemoveBackslashUnlessRoot(dir);
end;

function PreviousInstall(var dir, version: String): Boolean;
begin
  Result := QueryPreviousInstall(HKCU, dir, version) or
            QueryPreviousInstall(HKLM, dir, version);
end;

// The flow when the game is already installed. The same version is a reinstall;
// a lower one is an update and a higher one a downgrade, and either direction can
// change what the game writes to its saves, so both say so. Answering No leaves
// the machine exactly as it was, before the first page is shown.
function ConfirmExistingInstall: Boolean;
var
  dir, version, question: String;
  comparison: Integer;
begin
  Result := True;
  if WizardSilent then
    Exit;
  if not PreviousInstall(dir, version) then
    Exit;

  comparison := CompareVersions(version, '{#AppVersion}');
  if version = '' then
    question := 'Rock Band Blitz is already installed in:' + kCrLf + kCrLf + dir + kCrLf + kCrLf +
                'Continue and install it again?'
  else if comparison = 0 then
    question := 'Rock Band Blitz ' + version + ' is already installed in:' + kCrLf + kCrLf + dir +
                kCrLf + kCrLf +
                'Install it again, replacing the game files? Your game data and settings are kept.'
  else if comparison < 0 then
    question := 'Rock Band Blitz ' + version + ' is installed in:' + kCrLf + kCrLf + dir + kCrLf + kCrLf +
                'This will update it to {#AppVersion}. Saves made with the older version may not ' +
                'load afterwards.' + kCrLf + kCrLf + 'Continue?'
  else
    question := 'Rock Band Blitz ' + version + ' is installed in:' + kCrLf + kCrLf + dir + kCrLf + kCrLf +
                'This installer is older ({#AppVersion}) and will downgrade it. Saves made with ' +
                'the newer version may not load afterwards.' + kCrLf + kCrLf + 'Continue?';

  Result := MsgBox(question, mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

// The version an install records in the Add/Remove Programs entry. Inno Setup
// writes its own AppVersion there, which is the version of the *executable* that
// ran - and an updater is an older build than the release it installs, so it has
// to correct the entry afterwards (D19). Without this, a machine that has been
// updated twice would report the version of the first updater it ever ran, and the
// next real install would offer to "update" an install that is already newer.
procedure WriteInstalledVersion(const version: String);
var
  root: Integer;
begin
  if version = '' then
    Exit;
  if IsAdminInstallMode then
    root := HKLM
  else
    root := HKCU;
  if not RegWriteStringValue(root, UninstallKey, 'DisplayVersion', version) then
    Log('could not record the installed version ' + version + ' in the uninstall entry');
end;

// ==========================================================================
// wizard events
// ==========================================================================

function InitializeSetup: Boolean;
var
  targetDir, installedVersion: String;
begin
  gFailed := False;
  gFailureText := '';
  gInstalled := False;
  gFinishReached := False;
  gStageVisible := False;
  gGameSourceBytes := 0;
  gUltimateActive := False;
  gUltimateMode := 'none';
  gPayloadDownload := PayloadIsDownload <> 0;
  gPayloadSize := PayloadSizeBytes;
  gUpdater := False;
  gUpdateTargetDir := '';
  InitTempPaths;

#ifdef UpdaterMode
  // The updater's first question is the release manifest, not the user: it is what
  // says which build to install and whether this update needs the game files again.
  // Both answers decide which pages exist for this run, so it is fetched before the
  // first one is shown.
  gUpdater := True;
  gRunAtEnd := RunAtEndLauncher;
  if not FetchUpdateManifest then
  begin
    Result := False;
    Exit;
  end;
  // What it installs over has to be there: an updater has no destination page and
  // no payload of its own, so with no install to update it would write into the
  // default folder as if it were a fresh install.
  if not PreviousInstall(targetDir, installedVersion) then
  begin
    SuppressibleMsgBox('This updater updates an installation that is already on this machine, ' +
                       'and there is none.' + kCrLf + kCrLf +
                       'Run the setup executable once to install Rock Band Blitz first.',
                       mbError, MB_OK, IDOK);
    Result := False;
    Exit;
  end;
  gUpdateTargetDir := targetDir;
  Log('updater: ' + installedVersion + ' -> ' + gUpdateVersion + ' in ' + targetDir);
  // The payload is always downloaded: an updater carries none of its own, and the
  // release manifest names the one this version installs.
  gPayloadDownload := True;
  gPayloadSize := gUpdatePayloadSize;
  Result := True;
#else
  // Asked before the first page: an install that is already on the machine is the
  // one thing the wizard has to settle before the user starts answering, and No
  // ends setup without touching anything.
  Result := ConfirmExistingInstall;
#endif
end;

procedure InitializeWizard;
var
  index: Integer;
begin
  gAppDir := '';

  MethodPage := CreateInputOptionPage(wpSelectDir,
    'Xbox 360 Game Files',
    'Rock Band Blitz needs the files from your own Xbox 360 copy. Choose the way you want to provide them.',
    '',
    True, False);
  MethodPage.Add('Use the Xbox 360 package (GOD/STFS file)');
  MethodPage.Add('Use a game folder I have already extracted');
  MethodPage.Add('Use the game data already installed in the install folder');
  MethodPage.SelectedValueIndex := MethodPackage;

  // PackagePage is created after FolderPage, not after MethodPage: Inno Setup
  // inserts a page directly behind the page it is given, so two pages sharing one
  // parent come out in reverse - and the mod's page would land between the folder
  // question and the folder it asks about.
  FolderPage := CreateInputDirPage(MethodPage.ID,
    'Game folder location',
    '',
    '',
    False, '');
  FolderPage.Add('Extracted game folder:');

  PackagePage := CreateInputFilePage(FolderPage.ID,
    'Xbox 360 package location',
    '',
    '');
  PackagePage.Add('Xbox 360 package:', 'All files|*.*', '');

  UltimatePage := CreateInputOptionPage(PackagePage.ID,
    'Rock Band Blitz Ultimate',
    'A community mod by ultimate-mods-rb that adds the songs and the modes of the console versions. Recommended: it complements this recompilation.',
    'Choose if you want to install it, and how.',
    True, False);
  UltimatePage.Add('Download Rock Band Blitz Ultimate {#UltimateVersion} from its GitHub release');
  UltimatePage.Add('Install from a zip file I have');
  UltimatePage.Add('Install from a folder I have');
  UltimatePage.Add('Do not install the mod');
  UltimatePage.SelectedValueIndex := UltimateNothing;
  if '{#UltimateUrl}' = '' then
    UltimatePage.CheckListBox.ItemEnabled[UltimatePinned] := False;

  UltimateZipPage := CreateInputFilePage(UltimatePage.ID,
    'Ultimate zip location',
    '',
    '');
  UltimateZipPage.Add('Ultimate zip file:', 'Zip archives|*.zip|All files|*.*', '.zip');

  UltimateFolderPage := CreateInputDirPage(UltimateZipPage.ID,
    'Ultimate folder location',
    '',
    '',
    False, '');
  UltimateFolderPage.Add('Ultimate folder:');

  StagePage := CreateOutputProgressPage('Installing',
    'The installer is building your game folder. This takes a few minutes for the game data.');

  RefreshMethodPage;

  // The wizard offers the launcher's desktop shortcut checked (D9), which is the
  // default the task was declared with. A silent install is conservative: it
  // creates no desktop shortcut unless /LAUNCHERICON=1 asks (README.md).
  if WizardSilent and (ParamLauncherIcon <> '1') then
  begin
    index := WizardForm.TasksList.Items.IndexOf(LauncherIconTask);
    if index >= 0 then
      WizardForm.TasksList.Checked[index] := False;
  end;

  // D8: the finish page's choice. The [Run] entry is gone; these three radios
  // replace it, and DeinitializeSetup runs whichever is selected. RunList is
  // hidden because it would otherwise list the [Run] entries that no longer
  // exist.
  EndGameRadio := TNewRadioButton.Create(WizardForm);
  EndGameRadio.Parent := WizardForm.FinishedPage;
  EndGameRadio.OnClick := @EndGameRadioClick;

  EndLauncherRadio := TNewRadioButton.Create(WizardForm);
  EndLauncherRadio.Parent := WizardForm.FinishedPage;
  EndLauncherRadio.OnClick := @EndLauncherRadioClick;

  EndNothingRadio := TNewRadioButton.Create(WizardForm);
  EndNothingRadio.Parent := WizardForm.FinishedPage;
  EndNothingRadio.OnClick := @EndNothingRadioClick;

  WizardForm.RunList.Visible := False;
  ResolveRunAtEnd;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
#ifdef UpdaterMode
  // The updater asks as little as it can, and nothing it can answer itself: no
  // welcome, no folder (Inno Setup keeps the one the install was registered with),
  // no shortcuts (the install already has them), no Ultimate pages (the mod is
  // neither reinstalled nor removed), and no summary - the answers are the
  // wizard's own, so there is nothing for the user to confirm.
  //
  // The game-data pages are the exception: this release may need the user's files
  // again, and then they are the whole reason the update cannot simply run.
  if (PageID = wpWelcome) or (PageID = wpSelectDir) or (PageID = wpSelectTasks) or
     (PageID = wpReady) or (PageID = UltimatePage.ID) or (PageID = UltimateZipPage.ID) or
     (PageID = UltimateFolderPage.ID) then
  begin
    Result := True;
    Exit;
  end;
  if (not UpdateNeedsGamePages) and ((PageID = MethodPage.ID) or (PageID = FolderPage.ID) or
                                     (PageID = PackagePage.ID)) then
  begin
    Result := True;
    Exit;
  end;
#endif
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
  // The updater has no destination page, so the folder the install is registered
  // with is the destination - the same one Inno Setup would have pre-selected, read
  // here so the install cannot land beside an installation it is updating.
  if gUpdater and (gUpdateTargetDir <> '') then
    gAppDir := gUpdateTargetDir;
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

  problem := RunAtEndProblem;
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
  installerVersion: String;
begin
  // The registry entry Inno Setup wrote carries the version of the executable that
  // ran, which for an updater is older than the release it just installed. Corrected
  // after Setup's own work, so this is the value the next install reads back.
  if CurStep = ssDone then
  begin
    if gUpdater and gInstalled then
      WriteInstalledVersion(gUpdateVersion);
    Exit;
  end;

  if CurStep <> ssPostInstall then
    Exit;

  gAppDir := WizardDirValue;
  gLogFile := JoinPath(gAppDir, 'install.log');
  gGameDetailsFile := JoinPath(gAppDir, 'game-source-details.txt');
  gFailed := False;
  gStageActive := True;
  BeginStages;

  // 1. the recompiled build
  if gUpdater then
  begin
    // An updater has no payload of its own: the release manifest names the build
    // this version installs, and the helper downloads and verifies it the same way
    // it does for a pinned release.
    args := 'install-payload --dest ' + QuoteArg(gAppDir) +
            ' --from-url ' + QuoteArg(gUpdatePayloadUrl) +
            ' --sha256 ' + QuoteArg(gUpdatePayloadSha256) +
            ' --size ' + Int64ToStr(gUpdatePayloadSize) +
            ' --version ' + QuoteArg(gUpdatePayloadVersion);
    if not RunStage('Installing the new build',
                    'Downloading the recompiled build this release ships and checking it...',
                    args, PayloadStart, PayloadEnd) then
    begin
      gFailed := True;
      EndStages;
      SuppressibleMsgBox('The new build could not be installed:' + kCrLf + kCrLf + gFailureText +
                         kCrLf + kCrLf +
                         'Nothing was changed. Check your internet connection and run the ' +
                         'update again from the launcher.', mbError, MB_OK, IDOK);
      Exit;
    end;
    NoteSource(gStageSummaryFile, 'source', 'version', gPayloadSource, gPayloadVersion);
  end
  else if gPayloadDownload then
  begin
    args := 'install-payload --dest ' + QuoteArg(gAppDir) + ' --from-pinned';
    if not RunStage('Downloading the recompiled build',
                    'Fetching the build pinned by this installer and checking it...', args,
                    PayloadStart, PayloadEnd) then
    begin
      gFailed := True;
      EndStages;
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
      EndStages;
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
      EndStages;
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
  if gUpdater then
    installerVersion := gUpdateVersion
  else
    installerVersion := '{#AppVersion}';
  args := 'finalize --dest ' + QuoteArg(gAppDir) +
          ' --game-source ' + QuoteArg(gGameSourceDescription) +
          ' --installer-version ' + QuoteArg(installerVersion);
  if gPayloadSource <> '' then
    args := args + ' --payload-source ' + QuoteArg(gPayloadSource);
  if gPayloadVersion <> '' then
    args := args + ' --payload-version ' + QuoteArg(gPayloadVersion);
  // The pinned commit is the build this *updater* was built from; the manifest's is
  // the build it just installed, which is the one the install manifest should name.
  if gUpdater and (gUpdatePayloadCommit <> '') then
    args := args + ' --payload-commit ' + QuoteArg(gUpdatePayloadCommit);
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

  EndStages;
  gStageActive := False;
  gInstalled := not gFailed;
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
    keepData := SuppressibleMsgBox('Delete the imported Rock Band Blitz game data as well?',
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
  ignored: String;
begin
  // Inno Setup's MemoDirInfo does not end with a line break, so both breaks here
  // are wanted: one to close its last line, one to leave the blank line that
  // separates every section of this memo. Without the second, the lines below
  // read as more sub-items of the destination folder.
  lines := MemoDirInfo + NewLine + NewLine;
  // The mod line has to say what is about to happen, and until this call the
  // answers have not been resolved: PrepareToInstall does that, and it runs after
  // this page was shown. Answering nothing here is deliberate - a choice
  // PrepareToInstall cannot use is its to report, on its own page.
  ignored := ResolveUltimateChoice;
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
    lines := lines + Space + 'Recompiled build: downloaded from the pinned release'
  else
    lines := lines + Space + 'Recompiled build: included in this installer';
  // Recorded, not verified: the commit the recompiled build was built from.
  if '{#PayloadCommit}' <> '' then
    lines := lines + ' (commit ' + Copy('{#PayloadCommit}', 1, 12) + ')';

  // Inno Setup's own Memo* strings carry no line break of their own at either
  // end, so every section boundary is written here: one break to close the last
  // line, one to leave the blank line that keeps the sections apart. Without them
  // the whole memo reads as a single block of settings.
  lines := lines + NewLine;
  if MemoGroupInfo <> '' then
    lines := lines + NewLine + MemoGroupInfo;
  if MemoTasksInfo <> '' then
    lines := lines + NewLine + MemoTasksInfo;

  Result := lines;
end;

function GetCustomSetupExitCode: Integer;
begin
  if gFailed then
    Result := 9
  else
    Result := 0;
end;

// D8: place and arm the finish page's choice. A failed install has nothing to
// run, a silent one has no page to ask on, and an update has already been told
// what to do - so the radios only appear on a successful interactive install.
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID <> wpFinished then
    Exit;
  if gUpdater or gFailed or WizardSilent then
  begin
    EndGameRadio.Visible := False;
    EndLauncherRadio.Visible := False;
    EndNothingRadio.Visible := False;
    Exit;
  end;
  gFinishReached := True;
  LayoutFinishChoices;
  EndGameRadio.Checked := gRunAtEnd = RunAtEndGame;
  EndLauncherRadio.Checked := gRunAtEnd = RunAtEndLauncher;
  EndNothingRadio.Checked := gRunAtEnd = RunAtEndNothing;
end;

// D8: the chosen action, after the wizard has closed. The game is started with
// the same argument its shortcut carries, and it reads the launcher's profile on
// its own (Contract 3), so this honours the target the install left behind
// without a second process; the launcher's entry is the launcher itself.
procedure DeinitializeSetup;
var
  exePath, parameters: String;
  code: Integer;
begin
  if not gInstalled then
    Exit;
  // An interactive install runs the choice only once the finish page was shown;
  // a silent one has no page, so gInstalled is enough.
  if (not WizardSilent) and (not gFinishReached) then
    Exit;
  if gRunAtEnd = RunAtEndGame then
  begin
    exePath := ExpandConstant('{app}\{#GameExeName}');
    parameters := '--game_data_root=' + QuoteArg(ExpandConstant('{app}\{#GameDirName}'));
  end
  else if gRunAtEnd = RunAtEndLauncher then
  begin
    exePath := ExpandConstant('{app}\{#LauncherExeName}');
    parameters := '';
  end
  else
    Exit;
  if not Exec(exePath, parameters, ExpandConstant('{app}'), SW_SHOWNORMAL, ewNoWait, code) then
    Log('run at end: could not start ' + exePath + ': ' + SysErrorMessage(code));
end;
