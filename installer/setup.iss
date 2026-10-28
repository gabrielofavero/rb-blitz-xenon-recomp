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
;   about removed. Nothing starts it since Controller Settings retired
;   (2026-10-19); it is built so the update machinery stays working, and it
;   never carries a payload and never touches the install folder's shortcuts.
;
; The directories below can be overridden from the command line so a release can
; point at artefacts built elsewhere:
;   /DGeneratedDir=<dir>  holds pins.iss, update.toml, a copy of the helper and
;                         the updater the setup embeds
;   /DPayloadDir=<dir>    holds the recompiled build that gets embedded
;   /DDistDir=<dir>       receives the setup executable (and the updater)
;   /DArtDir=<dir>        optional side wizard image (default: the repository's
;                         assets\installer\ directory; see tools/make_art.ps1)
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
  #define ArtDir ProjectDirPath + "assets\installer"
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

; The settings executable an earlier release installed. This build does not ship
; it any more (its player settings are in-game Help & Options rows), so
; [InstallDelete] takes the installer-owned copy and the shortcuts an older
; release could have created; nothing else in the install folder is touched.
#define ObsoleteControllerSettingsExeName "rb_blitz_controller_settings.exe"

; The updater the setup executable places under the user's local application data
; (see "Updating" in README.md). build.ps1 compiles it from this same script with
; /DUpdaterMode=1 before the setup, so the setup can embed it - a build without it
; is refused below rather than shipping an update path that cannot work.
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

; The shortcut an earlier release could have created for the settings application
; ([InstallDelete]). It is named after the program plus its own suffix, so the
; same AppShortName the game's shortcut uses keeps the pair distinguishable in a
; Start-menu list. Defined here rather than above because AppShortName comes from
; pins.iss (measured: ISCC refuses an undeclared identifier at the use site).
#define ObsoleteControllerSettingsShortcutName AppShortName + " Controller Settings"

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

; Only the game is given a shortcut (owner decision, 2026-10-14): the wizard
; installs one program for the user to run, and a second Start-menu entry for the
; settings application read as a second program beside it. The settings
; executable is gone from this release too (2026-10-19), so a shortcut that an
; older release created is removed by [InstallDelete] rather than re-pointed.

[Setup]
AppId={{92C7B4E1-3F5A-4D2E-9B18-7A6C4E0D5F31}
AppName={#AppName}
; What the wizard calls this build: the product plus the version of the recompiled
; build inside it. It is the [installer] version unless build.ps1 was told to
; record another payload version (tools/embed_config.cpp derives it). AppVersion
; stays the numeric pins version, which is what the version resource needs.
AppVerName={#AppVerName}
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
; {autopf} is the machine-wide Program Files here, because the install below is
; administrative; a /CURRENTUSER install resolves it to that user's own Programs
; folder instead, so the one directive covers both.
DefaultDirName={autopf}\{#AppDefaultDirName}
DisableProgramGroupPage=yes
AllowNoIcons=yes
; The install folder page is shown even when an install is already registered, and
; it is where the three locations are asked for: the folder the game goes in, the
; DLC library and the save folder (parent plan §9.1). Inno Setup's own default
; ("auto") hides the page in exactly that case, which would leave a reinstall with
; no way to see or change the two external folders. Inno Setup still prefills the
; folder from the install it found - UsePreviousAppDir does that - so a reinstall
; lands on the same one unless the user says otherwise.
DisableDirPage=no
; The Shortcuts page always starts from no answer: an install that already exists
; must not silently re-create a shortcut the user has since deleted, and the
; declared defaults below are unchecked for the same reason. An explicit
; /TASKS or /MERGETASKS still decides.
UsePreviousTasks=no
; An administrative install, because that is the mode autopf resolves to the system
; programs folder in: the default destination is the Rock Band Blitz folder there, where a
; game belongs, and the setup, the uninstaller and the updater ask for the
; elevation the files there need. /CURRENTUSER is still honoured and installs under
; the user's own profile instead, which is what an unattended run that may not
; elevate has to pass.
;
; Nothing the *game* writes lives in the install folder: the DLC library and the
; saved games are asked for on the folder page and default to the user's Documents,
; so a machine-wide install does not put a player's saves under Program Files. The
; one exception is deliberate and predates this: paths.toml sits beside the
; executable, which an elevated install writes and a later hand edit may need the
; same rights for (parent plan §9.1).
PrivilegesRequired=admin
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
SetupIconFile={#ProjectDirPath}assets\icon\blitz.ico
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
WizardSmallImageFile={#ProjectDirPath}assets\icon\blitz.png

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Welcome to the {#AppName} setup
WelcomeLabel2=This is a PC decompilation project of the Xbox 360 version of Rock Band Blitz.%n%nIt installs a PC-enhanced build of the game and takes the data from your own Xbox 360 copy. You will need to provide either the XBLA package or a folder containing the extracted files. Keep in mind this is a fan-made project, not official in any way.
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
; created - and nothing is created unless the user asks. The declarations below are
; the state a silent install starts from too (it has no page to ask on), so a
; silent install creates no shortcut at all: no previous selection is restored
; (UsePreviousTasks=no), and /TASKS or /MERGETASKS is what opts one back in.
;
; Two shortcuts, both the game, and neither label says who it is for: the game is
; the only program this wizard offers to run, so "for the game" would only raise
; the question of what the other one was. The two icon parameters that used to ask
; for the settings application's desktop icon (/LAUNCHERICON, /CONTROLLERICON) are
; gone with the task they named; a script that still passes one gets no shortcut
; and no error, which is what it would have got from the page anyway.
;
; The updater has no shortcuts page and creates none: the install it is updating
; already has whatever the user asked for, and re-creating a shortcut the user has
; since deleted is not an update's job.
#ifndef UpdaterMode
Name: "startmenu"; Description: "Create a &Start menu shortcut"; Flags: unchecked
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked
#endif

[Files]
; The wizard needs the helper while it is still on screen (to check the user's
; choices and the free space), so it is extracted to the temporary directory as
; well. Temporary files must sit at the very top when solid compression is on.
Source: "{#GeneratedDirPath}{#HelperExeName}"; DestDir: "{tmp}"; Flags: dontcopy noencryption
; This authoring kit is available beside an installed build. Example art is
; original and lives outside the runtime's scanned schemes directory.
Source: "{#ProjectDirPath}assets\controller\README.md"; DestDir: "{app}\assets\controller"; Flags: ignoreversion
Source: "{#ProjectDirPath}assets\controller\slots.toml"; DestDir: "{app}\assets\controller"; Flags: ignoreversion
Source: "{#ProjectDirPath}assets\controller\template\*.png"; DestDir: "{app}\assets\controller\template"; Flags: ignoreversion
Source: "{#ProjectDirPath}assets\controller\examples\*"; DestDir: "{app}\assets\controller\examples"; Flags: ignoreversion recursesubdirs createallsubdirs
; Generated from every validated authored scheme folder. An empty include is
; expected when no authored layouts are supplied with this build.
#include GeneratedDirPath + "controller_schemes.iss"
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
; folder: the install folder is the game's own (the payload audit lists what may
; ship in it). It is small - a wizard with no payload in it - and
; [UninstallDelete] takes it away again.
Source: "{#UpdaterExePath}"; DestDir: "{localappdata}\{#UpdateDirName}\update"; Flags: ignoreversion
  #if PayloadHasDownload == 0
; The recompiled build and its runtime DLLs, straight into the install folder.
; The payload directory is a plain snapshot of the release build (see
; tools/make_payload.ps1); its dev-only configuration file is not part of it.
Source: "{#PayloadDirPath}*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
  #endif
#endif

[Icons]
; The two shortcuts, each behind the [Tasks] entry the Shortcuts page shows. Both
; are the game, and both keep their --game_data_root argument: the game has to stay
; runnable on its own, and it reads its own input profile (the game's settings live
; in Help & Options, not in a second program).
#ifndef UpdaterMode
Name: "{autoprograms}\{#AppShortName}"; Filename: "{app}\{#GameExeName}"; Parameters: "--game_data_root=""{app}\{#GameDirName}"""; WorkingDir: "{app}"; Tasks: startmenu
Name: "{autodesktop}\{#AppShortName}"; Filename: "{app}\{#GameExeName}"; Parameters: "--game_data_root=""{app}\{#GameDirName}"""; WorkingDir: "{app}"; Tasks: desktopicon
#endif

[InstallDelete]
; An upgrade removes the settings executable and the shortcuts only an older
; installer created. Both entries are exact paths this installer owned; profiles,
; paths.toml, saves and the game data are never touched by this section.
Type: files; Name: "{app}\{#ObsoleteControllerSettingsExeName}"
Type: files; Name: "{autoprograms}\{#ObsoleteControllerSettingsShortcutName}.lnk"
Type: files; Name: "{autodesktop}\{#ObsoleteControllerSettingsShortcutName}.lnk"

; D8 replaced the single postinstall [Run] entry with the finish page's two-way
; choice (see [Code] below): Inno can run at most one entry with a checkbox, and
; the wizard now asks launch the game / do nothing.

[UninstallDelete]
; Files the helper wrote that Inno Setup never saw: the ones it downloaded, the
; manifests and the log. Anything the installer placed itself is already tracked
; by the uninstaller, so these entries only cost a failed delete.
Type: files; Name: "{app}\{#HelperExeName}"
Type: files; Name: "{app}\{#GameExeName}"
; The settings executable an earlier release installed is listed in [InstallDelete]
; so an upgrade removes it; this entry covers an install that is uninstalled
; without ever being upgraded. launcher.toml and paths.toml are deliberately not
; listed: they are the user's own settings - and the two folders the user chose -
; and they survive an uninstall, like the game data (Contract 4). The helper's own
; cleanup keeps them for the same reason.
Type: files; Name: "{app}\{#ObsoleteControllerSettingsExeName}"
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\payload-manifest.toml"
Type: files; Name: "{app}\install-manifest.toml"
Type: files; Name: "{app}\install-report.txt"
Type: files; Name: "{app}\game-source-details.txt"
Type: files; Name: "{app}\install.log"
Type: filesandordirs; Name: "{app}\.staging"
Type: filesandordirs; Name: "{app}\{#GameDirName}\.staging"
; The updater the setup placed under the user's local application data, and the
; folder it lives in. It is outside the install folder on purpose (D19): a second
; copy of an installer inside the game's own directory would be one more file the
; payload audit has to allow.
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

  { Where each stage sits on the single progress bar, out of 1000. The two folder
    choices are written last, after the install and its manifest are on disk. }
  PayloadStart = 0;
  PayloadEnd = 200;
  GameStart = 200;
  GameEnd = 700;
  UltimateStart = 700;
  UltimateEnd = 880;
  FinalizeStart = 880;
  FinalizeEnd = 950;
  PathsStart = 950;
  PathsEnd = 1000;

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
    puts it under the user's local application data. Nothing starts it since
    Controller Settings retired (2026-10-19) - it is the update machinery's own
    entry point now - so the folder is the updater's own and nobody else reads it. }
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
  RunAtEndParam = 'RUNATEND';
  { The two external folders (parent plan §9.1). Given, they answer for a silent
    install the way the folder page does; not given, whatever the installation's own
    paths.toml or the user's old settings profile says is kept. Given *empty*, they
    mean "the game's own folder", which is a different answer from "not given". }
  DlcDirParam = 'DLCDIR';
  SaveDirParam = 'SAVEDIR';

  { The two folders' defaults, under the user's Documents, and the leaf the game's
    own data lives in there. The save root is the one the game itself means by an
    empty one (its own settings row says "Empty uses Documents\Rock Band Blitz"), so
    prefilling it records the answer the runtime would have fallen back to. The DLC
    root is this installer's choice: the game's own fallback is the dlc directory
    inside the read-only game data, which is nothing to ask a player for, so what is
    offered is the library folder beside the saves. Both are the user's to change on
    the page. }
  DataDirName = 'Rock Band Blitz';
  DlcDirLeaf = 'Downloaded Content';
  SaveDirLeaf = 'Saved Games';

  { The sentinel that a param expansion returns when the parameter was not given at
    all. Inno Setup cannot tell "absent" from "present and empty" any other way, and
    that difference is the whole of §9.1's omitted-versus-empty rule. No braces are
    written here on purpose: a comment cannot contain one, because the closing brace
    would end the comment. }
  ParamAbsent = '@absent@';

  { D8: what the finish page can do. Strings rather than indexes, because the same
    values are what /RUNATEND accepts on a silent install (README.md).'game' and
    'none' are the two answers since Controller Settings retired: there is no
    settings application left to open. }
  RunAtEndGame = 'game';
  RunAtEndNothing = 'none';

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
  { The release manifest the updater installs from. Empty in a build that has no
    release channel, which the updater then says out loud. }
  UpdateManifestUrl = '{#UpdateManifestUrl}';

var
  MethodPage: TInputOptionWizardPage;
  UltimatePage: TInputOptionWizardPage;
  StagePage: TOutputProgressWizardPage;

  { The file and folder pickers sit on the option page that asks the question they
    belong to: the radio says which kind of source it is, and the prompt line, the
    edit and its Browse button appear under the radios. No answer costs a page of
    its own, so the wizard stays one question per step. }
  MethodPromptLabel: TNewStaticText;
  MethodPathEdit: TNewEdit;
  MethodBrowseButton: TNewButton;
  UltimatePromptLabel: TNewStaticText;
  UltimatePathEdit: TNewEdit;
  UltimateBrowseButton: TNewButton;
  { The mod page's second occupant of the picker's band: what to do instead when the
    answer is "do not install it". It and the picker are never visible together. }
  UltimateNoteLabel: TNewStaticText;

  { The two extra locations on the install folder page. They belong there rather than
    on a page of their own because they are part of the same answer - where this
    installation keeps its things - and both end up in paths.toml beside the game's
    own executable (§9.1). }
  DlcPromptLabel: TNewStaticText;
  DlcPathEdit: TNewEdit;
  DlcBrowseButton: TNewButton;
  SavePromptLabel: TNewStaticText;
  SavePathEdit: TNewEdit;
  SaveBrowseButton: TNewButton;
  LocationStatusLabel: TNewStaticText;
  gPrefillingLocations: Boolean;
  gDlcAnswered: Boolean;
  gSaveAnswered: Boolean;
  gLocationAnswerFound: Boolean;
  { True once the folder page's two rows exist. Every touch of those controls checks
    it, so a run that never builds the wizard (a silent install, where nothing on the
    page can be answered or shown) reads its answers from the globals instead. }
  gLocationsUi: Boolean;
  gDlcValue: String;
  gSaveValue: String;

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
  { The stage's own sentence - what it is doing, why it takes a while - which is the
    progress page's second line whenever the helper has no detail of its own to put
    there. }
  gStageSubtitle: String;
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

// The two folders the folder page offers when nothing else has an answer. Both are
// under the user's Documents, which is where the game keeps its own data: its save
// root's documented default is Documents\Rock Band Blitz, so prefilling these
// records a place the player can find rather than a folder inside the install.
function DefaultDlcDir: String;
begin
  Result := JoinPath(JoinPath(ExpandConstant('{userdocs}'), DataDirName), DlcDirLeaf);
end;

function DefaultSaveDir: String;
begin
  Result := JoinPath(JoinPath(ExpandConstant('{userdocs}'), DataDirName), SaveDirLeaf);
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

// The two external folders (§9.1). `...Given` is what tells a silent install that
// nobody answered - which keeps the installation's own paths.toml - from the answer
// "the game's own folder", which is `/DLCDIR=` with no value.
function ParamDlcDir: String;
begin
  Result := RemoveQuotes(Trim(ExpandConstant('{param:DLCDIR|}')));
end;

function ParamDlcDirGiven: Boolean;
begin
  Result := ExpandConstant('{param:DLCDIR|' + ParamAbsent + '}') <> ParamAbsent;
end;

function ParamSaveDir: String;
begin
  Result := RemoveQuotes(Trim(ExpandConstant('{param:SAVEDIR|}')));
end;

function ParamSaveDirGiven: Boolean;
begin
  Result := ExpandConstant('{param:SAVEDIR|' + ParamAbsent + '}') <> ParamAbsent;
end;

// D8: the finish page's two-way choice. The game is the interactive default - the
// install exists to be played - and a silent install does nothing unless /RUNATEND
// asks for one, as the old skipifsilent [Run] entry did. The radios set gRunAtEnd;
// DeinitializeSetup runs it after the wizard closes.
function ParamRunAtEnd: String;
begin
  Result := Lowercase(Trim(ExpandConstant('{param:RUNATEND|}')));
end;

procedure ResolveRunAtEnd;
var
  requested: String;
begin
  // An update ends at the game's own folder: the user asked for a new build, and
  // launching it is the one thing left to do. The finish-page radios are not shown
  // for it either (CurPageChanged), so this is the only thing that decides.
  if gUpdater then
  begin
    gRunAtEnd := RunAtEndGame;
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
  if (gRunAtEnd <> RunAtEndGame) and (gRunAtEnd <> RunAtEndNothing) then
    Result := RunAtEndParam + ' must be ' + RunAtEndGame + ' or ' +
              RunAtEndNothing + ', not ''' + gRunAtEnd + '''.';
end;

procedure EndGameRadioClick(Sender: TObject);
begin
  gRunAtEnd := RunAtEndGame;
end;

procedure EndNothingRadioClick(Sender: TObject);
begin
  gRunAtEnd := RunAtEndNothing;
end;

// The finished label's height depends on its text and the DPI, so the radios are
// placed under it when the page is shown rather than at start-up. They read top
// to bottom in the order they are listed here, and the game is named once: with
// the mod installed the install folder is still the same game, so "Launch
// Rock Band Blitz" is the honest caption for it.
procedure LayoutFinishChoices;
var
  top, step, left, width: Integer;
begin  left := WizardForm.FinishedLabel.Left;
  width := WizardForm.FinishedLabel.Width;
  step := ScaleY(21);
  top := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(8);

  EndGameRadio.Caption := 'Launch {#AppShortName}';
  EndNothingRadio.Caption := 'Do nothing';

  EndGameRadio.Left := left;
  EndGameRadio.Top := top;
  EndGameRadio.Width := width;
  EndGameRadio.Height := step;
  EndNothingRadio.Left := left;
  EndNothingRadio.Top := top + step;
  EndNothingRadio.Width := width;
  EndNothingRadio.Height := step;
end;

// Inno Setup leaves Back and Next touching and puts Next against Cancel. The row
// reads evenly when all three carry the same gap, so they are re-placed here - on
// every page change, because the wizard hides and shows Back as it goes and each
// page is laid out again when it appears.
procedure LayoutWizardButtons;
var
  gap, left: Integer;
begin
  gap := ScaleX(10);
  left := WizardForm.CancelButton.Left - gap - WizardForm.NextButton.Width;
  WizardForm.NextButton.Left := left;
  WizardForm.BackButton.Left := left - gap - WizardForm.BackButton.Width;
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
// answer, and the updater can be run again from the release page.
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

// The page has two lines and both say something: the stage and how far it has got,
// then what the helper is doing inside it. They used to be the stage title twice -
// "Installing Rock Band Blitz Ultimate (71%)" over "Installing Rock Band Blitz
// Ultimate" - because the second line was set to the first one's own text (owner
// report, 2026-10-14). The detail the helper writes is what goes on the second line,
// and a stage that has not reported yet keeps the sentence it was introduced with, so
// the two lines are never the same string.
procedure StageUpdate(const percent: Integer; const detail: String);
var
  headline, second: String;
  overall: Integer;
begin
  headline := gStageTitle;
  if percent >= 0 then
  begin
    overall := percent div 10;
    if overall > 100 then
      overall := 100;
    StagePage.SetProgress(overall, 100);
    headline := headline + '  (' + Format('%d', [overall]) + '%)';
  end;
  second := detail;
  if second = '' then
    second := gStageSubtitle;
  StagePage.SetText(headline, second);
  PumpStagePaint;
end;

procedure ShowStage(const title, subtitle: String);
begin
  gStageTitle := title;
  gStageSubtitle := subtitle;
  StagePage.SetProgress(0, 100);
  StagePage.SetText(title, subtitle);
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

// --- the picker that shares its page with the choice ----------------------

// The option list is drawn at the very left of the page, where the radios' own
// marker sits against the page edge; it is indented so the marker has room. The
// band that leaves under the list is where the picker for the chosen kind of
// source goes, which is why the list is shortened here rather than sized by Inno -
// and it is a wide band on purpose: the radios and the picker they control read as
// one question only while the gap between them is clearly larger than the one
// between the radios themselves (owner request, 2026-10-14).
//
// `Note` is the other thing that band can hold. A page whose choice has an answer
// with nothing to pick (the mod's "do not install") explains itself in the same
// space instead, so the band is never empty and the two never share it.
procedure PlacePicker(const Page: TInputOptionWizardPage; const Prompt: TNewStaticText;
                      const PathEdit: TNewEdit; const Browse: TNewButton;
                      const Note: TNewStaticText);
var
  list: TNewCheckListBox;
begin
  list := Page.CheckListBox;
  list.Left := ScaleX(8);
  list.Width := list.Width - ScaleX(8);
  list.Height := list.Height - ScaleY(72);

  // The prompt's own height is fixed here rather than measured: it is one line, and
  // the edit below it is placed from it before any caption has been set.
  Prompt.Parent := list.Parent;
  Prompt.AutoSize := False;
  Prompt.WordWrap := False;
  Prompt.Left := list.Left;
  Prompt.Top := list.Top + list.Height + ScaleY(20);
  Prompt.Width := list.Width;
  Prompt.Height := ScaleY(15);

  PathEdit.Parent := list.Parent;
  PathEdit.Left := list.Left;
  PathEdit.Top := Prompt.Top + Prompt.Height + ScaleY(4);
  PathEdit.Width := list.Width - ScaleX(100);
  PathEdit.Height := ScaleY(23);

  Browse.Parent := list.Parent;
  Browse.Left := PathEdit.Left + PathEdit.Width + ScaleX(8);
  Browse.Top := PathEdit.Top - ScaleY(2);
  Browse.Width := ScaleX(92);
  Browse.Height := ScaleY(27);

  // The note is wrapped and given three lines' worth of height: it is a sentence,
  // not a caption, and 300% needs the room the band already has.
  if Note <> nil then
  begin
    Note.Parent := list.Parent;
    Note.AutoSize := False;
    Note.WordWrap := True;
    Note.Left := list.Left;
    Note.Top := Prompt.Top;
    Note.Width := list.Width;
    Note.Height := ScaleY(45);
  end;
end;

procedure UpdateMethodPicker;
var
  method: Integer;
begin
  method := MethodPage.SelectedValueIndex;
  MethodPromptLabel.Visible := method <> MethodInstalled;
  MethodPathEdit.Visible := method <> MethodInstalled;
  MethodBrowseButton.Visible := method <> MethodInstalled;
  if method = MethodFolder then
    MethodPromptLabel.Caption := 'Extracted game folder:'
  else
    MethodPromptLabel.Caption := 'Xbox 360 package:';
end;

procedure UpdateUltimatePicker;
var
  choice: Integer;
begin
  choice := UltimatePage.SelectedValueIndex;
  UltimatePromptLabel.Visible := (choice = UltimateZip) or (choice = UltimateFolder);
  UltimatePathEdit.Visible := (choice = UltimateZip) or (choice = UltimateFolder);
  UltimateBrowseButton.Visible := (choice = UltimateZip) or (choice = UltimateFolder);
  if choice = UltimateFolder then
    UltimatePromptLabel.Caption := 'Ultimate folder:'
  else
    UltimatePromptLabel.Caption := 'Ultimate zip file:';

  // "Do not install the mod" leaves the band the picker would have used, and what
  // goes in it is what to do instead: the mod installs by dropping its files into
  // the game folder (docs/guides/ultimate-compat.md), so the second half of that
  // answer is something a user can do without this wizard at all (owner request,
  // 2026-10-14).
  UltimateNoteLabel.Visible := choice = UltimateNothing;
  if UltimateNoteLabel.Visible then
    UltimateNoteLabel.Caption := 'You can add the mod later: run this installer again, or copy ' +
      'the release''s Xbox folder into game\ultimate in the install folder.';
end;

procedure MethodChoiceChanged(Sender: TObject);
begin
  RefreshMethodPage;
  UpdateMethodPicker;
end;

procedure UltimateChoiceChanged(Sender: TObject);
begin
  UpdateUltimatePicker;
end;

procedure MethodBrowseClick(Sender: TObject);
var
  path: String;
begin
  path := Trim(MethodPathEdit.Text);
  if MethodPage.SelectedValueIndex = MethodFolder then
  begin
    if BrowseForFolder('Choose the folder that holds the extracted game files.', path, False) then
      MethodPathEdit.Text := path;
  end
  else if GetOpenFileName('Choose the Xbox 360 package.', path, '', 'All files|*.*', '') then
    MethodPathEdit.Text := path;
end;

procedure UltimateBrowseClick(Sender: TObject);
var
  path: String;
begin
  path := Trim(UltimatePathEdit.Text);
  if UltimatePage.SelectedValueIndex = UltimateFolder then
  begin
    if BrowseForFolder('Choose the folder that holds the Ultimate release.', path, False) then
      UltimatePathEdit.Text := path;
  end
  else if GetOpenFileName('Choose the Rock Band Blitz Ultimate zip file.', path, '',
                          'Zip archives|*.zip|All files|*.*', '.zip') then
    UltimatePathEdit.Text := path;
end;

// --- the install folder page's own header ---------------------------------

// Inno Setup gives this page two text blocks - the folder icon with "Setup will
// install ... into the following folder" beside it, then "To continue, click Next
// ..." - and puts 13 pixels between them, which at any DPI reads as one paragraph
// (owner report, 2026-10-14). The second block and everything under it move down by
// the extra gap here, and the DLC and save rows follow automatically because
// LayoutLocationFields places them from WizardForm.DirEdit. This runs once, at the
// wizard's creation: Inno lays the page out once, and shifting it per page change
// would walk it off the bottom of the page.
procedure LayoutSelectDirHeader;
var
  extra: Integer;
begin
  extra := ScaleY(10);
  WizardForm.SelectDirBrowseLabel.Top := WizardForm.SelectDirBrowseLabel.Top + extra;
  WizardForm.DirEdit.Top := WizardForm.DirEdit.Top + extra;
  WizardForm.DirBrowseButton.Top := WizardForm.DirBrowseButton.Top + extra;
  WizardForm.DiskSpaceLabel.Top := WizardForm.DiskSpaceLabel.Top + extra;
end;

// --- the two extra folders on the install folder page ---------------------
//
// The install folder page asks three questions, not one: the folder the game goes
// in (Inno Setup's own row), the DLC library and the save folder. The last two end
// up in paths.toml beside rb_blitz.exe, one file per installation, which is why
// they are asked for here rather than on a page of their own (§9.1).

// Inno Setup's directory page has one row and Inno places its own controls, so the
// two rows are built here, under that row, and the page's free-space line - which is
// a remark about the folder above rather than about the ones below - is moved to the
// bottom. Every measurement goes through ScaleX/ScaleY, so the rows are the same
// fraction of the page at 100% and at 300%.
procedure LayoutLocationFields;
var
  labelWidth: Integer;
begin
  if not gLocationsUi then
    Exit;
  labelWidth := ScaleX(132);

  DlcPromptLabel.Left := WizardForm.DirEdit.Left;
  DlcPromptLabel.Top := WizardForm.DirEdit.Top + WizardForm.DirEdit.Height + ScaleY(10);
  DlcPromptLabel.Width := labelWidth;
  DlcPromptLabel.Height := ScaleY(23);

  DlcPathEdit.Left := DlcPromptLabel.Left + labelWidth + ScaleX(4);
  DlcPathEdit.Top := DlcPromptLabel.Top;
  DlcPathEdit.Width := WizardForm.DirEdit.Width - labelWidth - ScaleX(4) - ScaleX(8) -
                       WizardForm.DirBrowseButton.Width;
  DlcPathEdit.Height := ScaleY(23);

  DlcBrowseButton.Left := DlcPathEdit.Left + DlcPathEdit.Width + ScaleX(8);
  DlcBrowseButton.Top := DlcPathEdit.Top - ScaleY(1);
  DlcBrowseButton.Width := WizardForm.DirBrowseButton.Width;
  DlcBrowseButton.Height := WizardForm.DirBrowseButton.Height;

  SavePromptLabel.Left := DlcPromptLabel.Left;
  SavePromptLabel.Top := DlcPromptLabel.Top + ScaleY(27);
  SavePromptLabel.Width := labelWidth;
  SavePromptLabel.Height := ScaleY(23);

  SavePathEdit.Left := DlcPathEdit.Left;
  SavePathEdit.Top := SavePromptLabel.Top;
  SavePathEdit.Width := DlcPathEdit.Width;
  SavePathEdit.Height := ScaleY(23);

  SaveBrowseButton.Left := DlcBrowseButton.Left;
  SaveBrowseButton.Top := SavePathEdit.Top - ScaleY(1);
  SaveBrowseButton.Width := DlcBrowseButton.Width;
  SaveBrowseButton.Height := DlcBrowseButton.Height;

  LocationStatusLabel.Left := WizardForm.DirEdit.Left;
  // What the two answers turned out to be is a line of its own, not a caption: the
  // save row ends and this begins a clear line below it (owner report, 2026-10-14),
  // because read any closer the folder a Browse button picked looks like part of the
  // line that reports it.
  LocationStatusLabel.Top := SavePathEdit.Top + SavePathEdit.Height + ScaleY(16);
  LocationStatusLabel.Width := WizardForm.DirEdit.Width;
  LocationStatusLabel.Height := ScaleY(15);

  // What the DLC folder turned out to be, and where the saves will go, is more use
  // than Inno Setup's own free-space sentence - which this release already replaces
  // in [Messages] because it counts only the files Setup itself places. That line
  // keeps the last position when there is room and is dropped when there is not:
  // it is a remark, and the two answers above it are not.
  WizardForm.DiskSpaceLabel.Top := LocationStatusLabel.Top + LocationStatusLabel.Height + ScaleY(8);
  if WizardForm.DiskSpaceLabel.Top + WizardForm.DiskSpaceLabel.Height >
     WizardForm.DiskSpaceLabel.Parent.Height - ScaleY(6) then
    WizardForm.DiskSpaceLabel.Visible := False;
end;

// The two answers. The fields are what the user sees and can change, so they are
// what an interactive install writes; a silent one has no page to change them on and
// reads what the prefill resolved instead.
function ChosenDlcDir: String;
begin
  if gLocationsUi and (not WizardSilent) then
    Result := Trim(DlcPathEdit.Text)
  else
    Result := gDlcValue;
end;

function ChosenSaveDir: String;
begin
  if gLocationsUi and (not WizardSilent) then
    Result := Trim(SavePathEdit.Text)
  else
    Result := gSaveValue;
end;

// Whether the installer is entitled to write the two folders at all. The folder page
// always answers - even "the game's own folder" is an answer - while a silent
// install that was asked nothing keeps what the installation already has, and an
// update has no folder page at all (§9.1's omitted-versus-empty rule).
function PathsAnswered: Boolean;
begin
  if gUpdater then
    Result := False
  else if not WizardSilent then
    Result := True
  else
    Result := ParamDlcDirGiven or ParamSaveDirGiven or gLocationAnswerFound;
end;

// What the last validate-paths said, in one line for the page. Only called when the
// probe succeeded, so every key is present.
function LocationStatusText: String;
var
  layout, count: String;
begin
  if SummaryFlag(gProbeSummaryFile, 'dlc_default') then
    Result := 'DLC: the game''s own folder'
  else
  begin
    layout := SummaryText(gProbeSummaryFile, 'dlc_layout', '');
    count := SummaryText(gProbeSummaryFile, 'dlc_packages', '0');
    if layout = 'structured' then
      Result := 'DLC: ' + count + ' package(s) in the title/content layout'
    else if layout = 'flat' then
      Result := 'DLC: ' + count + ' file(s), read as a library'
    else if layout = 'empty' then
      Result := 'DLC: empty, so the game starts with no songs'
    else
      Result := 'DLC: not there yet, so the game starts with no songs';
  end;
  if SummaryFlag(gProbeSummaryFile, 'save_default') then
    Result := Result + ';  saves: the game''s own folder'
  else
    Result := Result + ';  saves: ' + SummaryText(gProbeSummaryFile, 'save_dir', '');
  if SummaryText(gProbeSummaryFile, 'note_count', '0') <> '0' then
    Result := Result + '  (' + SummaryText(gProbeSummaryFile, 'note_0', '') + ')';
  if gLocationsUi then
    LocationStatusLabel.Caption := Result;
end;

// Fills the two fields. The order is parent plan §9.1's: the unattended parameter,
// the installation's own paths.toml, the profile the game used to read, then the
// installer's own default - Documents\Rock Band Blitz\Downloaded Content and
// ...\Saved Games, which is the rung that changed on 2026-10-14. It used to be an
// empty field, meaning "the game's own folder", and the page offered that as an
// answer; both boxes now always name a folder, so the same two are also what a silent
// install that was asked nothing writes. A field the user has already answered is
// never overwritten, so browsing for an install folder does not undo a DLC folder
// they typed a moment ago.
procedure PrefillLocations;
var
  args, dlc, save, dir: String;
begin
  if gDlcAnswered and gSaveAnswered then
    Exit;

  dlc := ParamDlcDir;
  save := ParamSaveDir;
  gLocationAnswerFound := ParamDlcDirGiven or ParamSaveDirGiven;

  if (not ParamDlcDirGiven) or (not ParamSaveDirGiven) then
  begin
    dir := RemoveBackslashUnlessRoot(Trim(WizardDirValue));
    if dir <> '' then
    begin
      args := 'read-paths --install-dir ' + QuoteArg(dir);
      // The old settings profile is only consulted when this install is not running
      // elevated: an administrator's own profile is not the player's, and the file
      // it names would be imported into the wrong person's install (§9.1).
      if not IsAdminInstallMode then
        args := args + ' --legacy-appdir ' + QuoteArg(dir);
      if RunQuietProbe(args) then
      begin
        // A paths.toml this build can read answers for both fields, including when
        // its answer is "the default" - which is why the legacy profile is only the
        // fallback and not a second opinion.
        if SummaryFlag(gProbeSummaryFile, 'file_valid') then
        begin
          if not ParamDlcDirGiven then
            dlc := SummaryText(gProbeSummaryFile, 'dlc_root', '');
          if not ParamSaveDirGiven then
            save := SummaryText(gProbeSummaryFile, 'save_root', '');
        end
        else if SummaryFlag(gProbeSummaryFile, 'legacy_found') then
        begin
          if not ParamDlcDirGiven then
            dlc := SummaryText(gProbeSummaryFile, 'legacy_dlc', '');
          if not ParamSaveDirGiven then
            save := SummaryText(gProbeSummaryFile, 'legacy_save', '');
        end;
      end;
    end;
  end;
  // The last rung: what nobody answered still has an answer, so an install always
  // records two folders. A silent install with no parameters therefore writes the
  // same two the page would have shown, and an explicitly *empty* /DLCDIR or
  // /SAVEDIR is still the different thing §9.1 makes it - the game's own folder.
  if dlc = '' then
    dlc := DefaultDlcDir;
  if save = '' then
    save := DefaultSaveDir;
  gLocationAnswerFound := True;
  gDlcValue := dlc;
  gSaveValue := save;

  if not gLocationsUi then
    Exit;
  gPrefillingLocations := True;
  if not gDlcAnswered then
    DlcPathEdit.Text := dlc;
  if not gSaveAnswered then
    SavePathEdit.Text := save;
  gPrefillingLocations := False;
end;

// The helper's check, run on Next and again from PrepareToInstall, so a silent
// install - which never sees the page - is checked the same way. Returns an empty
// string when the answers can be used. `inspectOnly` is the check that runs while
// the page is merely shown: it reports the same things without creating the save
// folder or writing a probe into it, because showing a page must not change the
// machine - and a path the user is halfway through typing must not leave a folder
// behind when they correct it.
function CheckLocations(const inspectOnly: Boolean): String;
var
  args, dir: String;
begin
  Result := '';
  if gLocationsUi then
    LocationStatusLabel.Caption := '';

  // Both boxes are answers, not extras (owner request, 2026-10-14): what the page
  // shows is what gets recorded, so an empty one means the question is not answered
  // yet. A *silent* install is the different case, and keeps §9.1's rule - an
  // explicitly empty /DLCDIR or /SAVEDIR still means the game's own folder - and so
  // is an update, which has no page and writes no folder keys at all.
  if gLocationsUi and (not WizardSilent) and (not gUpdater) and
     ((ChosenDlcDir = '') or (ChosenSaveDir = '')) then
  begin
    Result := 'both boxes have to name a folder, and one of them is empty';
    Exit;
  end;

  dir := RemoveBackslashUnlessRoot(Trim(WizardDirValue));
  if dir = '' then
    Exit;

  args := 'validate-paths --install-dir ' + QuoteArg(dir) +
          ' --dlc-dir ' + QuoteArg(ChosenDlcDir) +
          ' --save-dir ' + QuoteArg(ChosenSaveDir);
  if inspectOnly then
    args := args + ' --no-touch';
  if not RunQuietProbe(args) then
  begin
    Result := HelperError(gProbeSummaryFile);
    Exit;
  end;
  LocationStatusText;
end;

procedure DlcBrowseClick(Sender: TObject);
var
  path: String;
begin
  path := Trim(DlcPathEdit.Text);
  if BrowseForFolder('Choose the folder that holds your Rock Band DLC.', path, False) then
  begin
    gDlcAnswered := True;
    DlcPathEdit.Text := path;
  end;
end;

procedure SaveBrowseClick(Sender: TObject);
var
  path: String;
begin
  path := Trim(SavePathEdit.Text);
  if BrowseForFolder('Choose the folder the game should keep its saved games in.',
                     path, True) then
  begin
    gSaveAnswered := True;
    SavePathEdit.Text := path;
  end;
end;

// A field the user typed in is theirs from then on: the prefill may still run for
// the other one, and Next is what puts the answers in place.
procedure LocationFieldChanged(Sender: TObject);
begin
  if gPrefillingLocations then
    Exit;
  if Sender = DlcPathEdit then
    gDlcAnswered := True
  else if Sender = SavePathEdit then
    gSaveAnswered := True;
  LocationStatusLabel.Caption := '';
end;

// --- the Shortcuts page starts from no answer -----------------------------

// Inno Setup ticks whatever /TASKS named before this runs, so an explicit request is
// honoured; everything else starts unchecked, which is also the declaration in
// [Tasks] and what a silent install gets (§9.2). `!name` in /MERGETASKS means "do not
// create it", which is the state this leaves it in anyway. Both shortcuts are the
// game's now, so the two desktop-icon parameters that used to ask for Controller
// Settings' one are gone rather than kept as aliases: there is nothing left for them
// to ask for.
function TaskRequested(const name: String): Boolean;
var
  text, item: String;
  separator: Integer;
begin
  Result := False;
  text := Lowercase(ExpandConstant('{param:TASKS|}') + ',' +
                    ExpandConstant('{param:MERGETASKS|}'));
  while text <> '' do
  begin
    separator := Pos(',', text);
    if separator > 0 then
    begin
      item := Copy(text, 1, separator - 1);
      text := Copy(text, separator + 1, Length(text) - separator);
    end
    else
    begin
      item := text;
      text := '';
    end;
    if Trim(item) = name then
      Result := True;
  end;
end;

procedure ApplyTaskDefaults;
var
  names: array of String;
  index, position: Integer;
begin
  SetLength(names, 2);
  names[0] := 'startmenu';
  names[1] := 'desktopicon';
  for index := 0 to 1 do
  begin
    position := WizardForm.TasksList.Items.IndexOf(names[index]);
    if position >= 0 then
      WizardForm.TasksList.Checked[position] := TaskRequested(names[index]);
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
    Result := Trim(MethodPathEdit.Text);
end;

function ChosenGamePackage: String;
begin
  if WizardSilent then
    Result := ParamGamePackage
  else
    Result := Trim(MethodPathEdit.Text);
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

  // An update never reinstalls or removes the mod, and it has no page to ask on:
  // its Ultimate pages are skipped, so the page's preselected answer must not be
  // read as one the user gave.
  if gUpdater then
    Exit;

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
      gUltimateArchive := Trim(UltimatePathEdit.Text);
    end
    else if UltimatePage.SelectedValueIndex = UltimateFolder then
    begin
      mode := 'folder';
      gUltimateFolder := Trim(UltimatePathEdit.Text);
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
  gLocationsUi := False;
  gDlcAnswered := False;
  gSaveAnswered := False;
  gLocationAnswerFound := False;
  gDlcValue := '';
  gSaveValue := '';
  InitTempPaths;

#ifdef UpdaterMode
  // The updater's first question is the release manifest, not the user: it is what
  // says which build to install and whether this update needs the game files again.
  // Both answers decide which pages exist for this run, so it is fetched before the
  // first one is shown.
  gUpdater := True;
  // The updater has no finish page: it installs a new build over an existing
  // install, so it returns to the game when it is done. ResolveRunAtEnd says the
  // same thing - this is only the state before it runs.
  gRunAtEnd := RunAtEndGame;
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
begin
  gAppDir := '';

  MethodPage := CreateInputOptionPage(wpSelectDir,
    'Xbox 360 Game Files',
    'Rock Band Blitz needs the files from your own Xbox 360 copy. Choose the way you want to provide them, then give the location on this page.',
    '',
    True, False);
  MethodPage.Add('Use the Xbox 360 package (GOD/STFS file)');
  MethodPage.Add('Use an extracted game folder');
  MethodPage.Add('Use the game data already in the install folder');
  MethodPage.SelectedValueIndex := MethodPackage;

  UltimatePage := CreateInputOptionPage(MethodPage.ID,
    'Rock Band Blitz Ultimate',
    'It''s strongly recommended to use this community mod by ultimate-mods-rb. It restores features from online mode and adds many customization options.',
    'Choose if you want to install it, and how.',
    True, False);
  UltimatePage.Add('Download Rock Band Blitz Ultimate {#UltimateVersion} from GitHub');
  UltimatePage.Add('Install from a zip file');
  UltimatePage.Add('Install from a folder');
  UltimatePage.Add('Do not install the mod');
  // The download is the answer this page is here to offer, so it is the one the
  // page opens on - unless this build pinned no release to download, in which case
  // the item is disabled and the choice falls back to not installing the mod.
  UltimatePage.SelectedValueIndex := UltimatePinned;
  if '{#UltimateUrl}' = '' then
  begin
    UltimatePage.CheckListBox.ItemEnabled[UltimatePinned] := False;
    UltimatePage.SelectedValueIndex := UltimateNothing;
  end;

  // The picker each option page owns: a prompt, an edit and a Browse button, placed
  // under the radios by PlacePicker. The edit is what ChosenGameFolder,
  // ChosenGamePackage and ResolveUltimateChoice read, so the wizard and a silent
  // install answer the same question in the same place.
  MethodPromptLabel := TNewStaticText.Create(WizardForm);
  MethodPathEdit := TNewEdit.Create(WizardForm);
  MethodPathEdit.OnChange := @MethodChoiceChanged;
  MethodBrowseButton := TNewButton.Create(WizardForm);
  MethodBrowseButton.Caption := 'Browse...';
  MethodBrowseButton.OnClick := @MethodBrowseClick;
  PlacePicker(MethodPage, MethodPromptLabel, MethodPathEdit, MethodBrowseButton, nil);

  UltimatePromptLabel := TNewStaticText.Create(WizardForm);
  UltimatePathEdit := TNewEdit.Create(WizardForm);
  UltimatePathEdit.OnChange := @UltimateChoiceChanged;
  UltimateBrowseButton := TNewButton.Create(WizardForm);
  UltimateBrowseButton.Caption := 'Browse...';
  UltimateBrowseButton.OnClick := @UltimateBrowseClick;
  UltimateNoteLabel := TNewStaticText.Create(WizardForm);
  // Hidden until UpdateUltimatePicker says the answer is "do not install it": a
  // control created at run time is visible by default, and an empty one would sit over
  // the picker's row until the first page change.
  UltimateNoteLabel.Visible := False;
  PlacePicker(UltimatePage, UltimatePromptLabel, UltimatePathEdit, UltimateBrowseButton,
              UltimateNoteLabel);

  MethodPage.CheckListBox.OnClick := @MethodChoiceChanged;
  UltimatePage.CheckListBox.OnClick := @UltimateChoiceChanged;

  // The install folder page asks for three locations, so it grows two rows: the DLC
  // library and the save folder. They are created here rather than on a page of their
  // own because they belong to the same answer as the install folder - they are
  // stored in paths.toml inside it - and Inno Setup's page keeps its own row first.
  DlcPromptLabel := TNewStaticText.Create(WizardForm);
  DlcPromptLabel.Parent := WizardForm.DirEdit.Parent;
  DlcPromptLabel.Caption := 'DLC folder:';
  DlcPathEdit := TNewEdit.Create(WizardForm);
  DlcPathEdit.Parent := WizardForm.DirEdit.Parent;
  DlcPathEdit.OnChange := @LocationFieldChanged;
  DlcBrowseButton := TNewButton.Create(WizardForm);
  DlcBrowseButton.Parent := WizardForm.DirEdit.Parent;
  DlcBrowseButton.Caption := 'Browse...';
  DlcBrowseButton.OnClick := @DlcBrowseClick;

  SavePromptLabel := TNewStaticText.Create(WizardForm);
  SavePromptLabel.Parent := WizardForm.DirEdit.Parent;
  SavePromptLabel.Caption := 'Saved games:';
  SavePathEdit := TNewEdit.Create(WizardForm);
  SavePathEdit.Parent := WizardForm.DirEdit.Parent;
  SavePathEdit.OnChange := @LocationFieldChanged;
  SaveBrowseButton := TNewButton.Create(WizardForm);
  SaveBrowseButton.Parent := WizardForm.DirEdit.Parent;
  SaveBrowseButton.Caption := 'Browse...';
  SaveBrowseButton.OnClick := @SaveBrowseClick;

  LocationStatusLabel := TNewStaticText.Create(WizardForm);
  LocationStatusLabel.Parent := WizardForm.DirEdit.Parent;
  gLocationsUi := True;

  StagePage := CreateOutputProgressPage('Installing',
    'The installer is building your game folder. This takes a few minutes for the game data.');

  RefreshMethodPage;
  UpdateMethodPicker;
  UpdateUltimatePicker;
  LayoutSelectDirHeader;
  LayoutLocationFields;
  // The page the folder starts on, and whatever paths.toml or the user's input
  // profile already say about the other two. A silent install has no page to change
  // them on, so this is also what it will write.
  PrefillLocations;

  // Nothing is preselected on the Shortcuts page unless the command line asked for
  // it: the [Tasks] declarations are unchecked, UsePreviousTasks=no keeps an earlier
  // install's answer from reappearing, and /TASKS or /MERGETASKS is what ticks one.
  // A silent install has no page to ask on, so this is also what it creates.
  ApplyTaskDefaults;

  // D8: the finish page's choice. The [Run] entry is gone; these two radios
  // replace it, and DeinitializeSetup runs whichever is selected. RunList is
  // hidden because it would otherwise list the [Run] entries that no longer
  // exist.
  EndGameRadio := TNewRadioButton.Create(WizardForm);
  EndGameRadio.Parent := WizardForm.FinishedPage;
  EndGameRadio.OnClick := @EndGameRadioClick;

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
  // The game-data page is the exception: this release may need the user's files
  // again, and then they are the whole reason the update cannot simply run.
  if (PageID = wpWelcome) or (PageID = wpSelectDir) or (PageID = wpSelectTasks) or
     (PageID = wpReady) or (PageID = UltimatePage.ID) then
  begin
    Result := True;
    Exit;
  end;
  if (not UpdateNeedsGamePages) and (PageID = MethodPage.ID) then
  begin
    Result := True;
    Exit;
  end;
#endif
  if WizardSilent then
  begin
    Result := (PageID = MethodPage.ID) or (PageID = UltimatePage.ID);
    Exit;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  answer: Integer;
  problem: String;
begin
  Result := True;
  if WizardSilent then
    Exit;

  // The install folder page asks three questions, and the two new ones are checked
  // the same way the game-files page checks its source: before the wizard moves on,
  // so a folder the game could not use is refused on the page rather than after the
  // install has spent minutes on it.
  if CurPageID = wpSelectDir then
  begin
    RefreshMethodPage;
    PrefillLocations;
    problem := CheckLocations(False);
    if problem <> '' then
    begin
      SuppressibleMsgBox('The DLC folder or the save folder cannot be used:' + kCrLf + kCrLf +
                         problem + kCrLf + kCrLf +
                         'Pick a folder for each one; neither box can be left empty.',
                         mbError, MB_OK, IDOK);
      Result := False;
    end;
    Exit;
  end;

  // The game files question and its location are the same page now, so both are
  // settled here: the answer is checked against the machine before the wizard
  // moves on, which is what the follow-on page used to do.
  if CurPageID = MethodPage.ID then
  begin
    RefreshMethodPage;
    if MethodPage.SelectedValueIndex = MethodInstalled then
      Exit;
    if Trim(MethodPathEdit.Text) = '' then
    begin
      if MethodPage.SelectedValueIndex = MethodFolder then
        SuppressibleMsgBox('Choose the folder that holds the extracted game files.',
                           mbError, MB_OK, IDOK)
      else
        SuppressibleMsgBox('Choose the Xbox 360 package file.', mbError, MB_OK, IDOK);
      Result := False;
      Exit;
    end;
    if MethodPage.SelectedValueIndex = MethodFolder then
    begin
      ProbeGameFolder(Trim(MethodPathEdit.Text));
      if gFailureText <> '' then
      begin
        SuppressibleMsgBox('That folder cannot be used:' + kCrLf + kCrLf + gFailureText + kCrLf + kCrLf +
                           'Pick the folder that holds default.xex and the gen folder, or a folder above it.',
                           mbError, MB_OK, IDOK);
        MethodPromptLabel.Caption := '';
        Result := False;
        Exit;
      end;
    end
    else
    begin
      ProbeGamePackage(Trim(MethodPathEdit.Text));
      if gFailureText <> '' then
      begin
        SuppressibleMsgBox('That file cannot be used:' + kCrLf + kCrLf + gFailureText + kCrLf + kCrLf +
                           'Check that it is the package holding Rock Band Blitz.',
                           mbError, MB_OK, IDOK);
        MethodPromptLabel.Caption := '';
        Result := False;
        Exit;
      end;
    end;
    MethodPromptLabel.Caption := 'Ready: ' + gGameSourceDescription;
    MethodPromptLabel.Update;
    Exit;
  end;

  // The mod's question and its location share a page too; the zip and the folder
  // are told apart by the radio that was chosen.
  if CurPageID = UltimatePage.ID then
  begin
    if UltimatePage.SelectedValueIndex = UltimateZip then
    begin
      if not ProbeUltimateArchive(Trim(UltimatePathEdit.Text)) then
      begin
        answer := SuppressibleMsgBox('That zip does not look like the Rock Band Blitz Ultimate release:' +
                                     kCrLf + kCrLf + gFailureText + kCrLf + kCrLf +
                                     'Install it anyway? The installer will refuse it if its files are wrong.',
                                     mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO);
        if answer <> IDYES then
        begin
          UltimatePromptLabel.Caption := '';
          Result := False;
          Exit;
        end;
        UltimatePromptLabel.Caption := 'Not verified: the archive does not look like the Ultimate release.';
      end
      else
        UltimatePromptLabel.Caption := 'Looks right: ' + Trim(SummaryText(gProbeSummaryFile, 'description', 'the archive'));
      UltimatePromptLabel.Update;
    end
    else if UltimatePage.SelectedValueIndex = UltimateFolder then
    begin
      if not LooksLikeUltimateTree(Trim(UltimatePathEdit.Text)) then
      begin
        answer := SuppressibleMsgBox('That folder does not look like the Rock Band Blitz Ultimate release:' +
                                     kCrLf + kCrLf + 'it holds no ' + UltimateHeaderRel + '.' + kCrLf + kCrLf +
                                     'Install it anyway? The installer will refuse it if its files are wrong.',
                                     mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO);
        if answer <> IDYES then
        begin
          UltimatePromptLabel.Caption := '';
          Result := False;
          Exit;
        end;
        UltimatePromptLabel.Caption := 'Not verified: no ' + UltimateHeaderRel + ' found.';
      end
      else
        UltimatePromptLabel.Caption := 'Looks right: ' + UltimateHeaderRel + ' is there.';
      UltimatePromptLabel.Update;
    end;
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

  // The folder page's two answers are checked once more here, because a silent
  // install never saw that page and because the folder may have changed since it did.
  if PathsAnswered then
  begin
    problem := CheckLocations(False);
    if problem <> '' then
    begin
      Result := 'The DLC folder or the save folder cannot be used: ' + problem;
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
                         'updater again.', mbError, MB_OK, IDOK);
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

  // The two folder answers are recorded in the manifest and the report as the user
  // gave them. The file the game reads is written by the stage after this one, once
  // the install and this record are on disk.
  if PathsAnswered then
    args := args + ' --dlc-dir ' + QuoteArg(ChosenDlcDir) +
            ' --save-dir ' + QuoteArg(ChosenSaveDir);

  if not RunStage('Finishing up', 'Writing the install manifest and the report...', args,
                  FinalizeStart, FinalizeEnd) then
  begin
    problem := gFailureText;
    SuppressibleMsgBox('The install manifest could not be written:' + kCrLf + kCrLf + problem +
                       kCrLf + kCrLf + 'The game itself is installed; see ' + gLogFile + '.',
                       mbError, MB_OK, IDOK);
  end;

  // The two folders go into paths.toml only now, after the install and its manifest
  // succeeded: cancelling or failing earlier must leave the previous configuration
  // exactly as it was (§9.1). Nothing is written when nobody answered - a silent
  // install that was asked nothing, or an update, which has no folder page - so a
  // configuration the user already has is never replaced by an empty one.
  if PathsAnswered then
  begin
    args := 'write-paths --dest ' + QuoteArg(gAppDir) +
            ' --dlc-dir ' + QuoteArg(ChosenDlcDir) +
            ' --save-dir ' + QuoteArg(ChosenSaveDir);
    if not RunStage('Saving your folders',
                    'Writing paths.toml, the file the game reads both locations from...', args,
                    PathsStart, PathsEnd) then
      SuppressibleMsgBox('Your folder choices could not be saved:' + kCrLf + kCrLf + gFailureText +
                         kCrLf + kCrLf + 'The game is installed, but it will use its own folders ' +
                         'until this is fixed. See ' + gLogFile + '.', mbError, MB_OK, IDOK);
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

  lines := lines + NewLine;
  // The two locations the folder page asked for, so the last look before anything is
  // written names them (§9.2). "The game's own folder" is what an empty answer means,
  // and an update - which was asked nothing - says nothing.
  if PathsAnswered then
  begin
    if ChosenDlcDir = '' then
      lines := lines + Space + 'DLC: the game''s own folder' + NewLine
    else
      lines := lines + Space + 'DLC: ' + ChosenDlcDir + NewLine;
    if ChosenSaveDir = '' then
      lines := lines + Space + 'Saved games: the game''s own folder' + NewLine
    else
      lines := lines + Space + 'Saved games: ' + ChosenSaveDir + NewLine;
  end;

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
  LayoutWizardButtons;
  // The two rows this release adds to the install folder page are prefilled and
  // checked every time the page appears: the install folder may have changed since
  // it was last shown, and what paths.toml already says is a function of it.
  if CurPageID = wpSelectDir then
  begin
    LayoutLocationFields;
    PrefillLocations;
    CheckLocations(True);
  end;
  // The game files page and the mod's page own their pickers: which one is shown,
  // and what it is called, follows the radio that is selected, and the installed
  // data item only stays offered while there is data to keep.
  if CurPageID = MethodPage.ID then
  begin
    RefreshMethodPage;
    UpdateMethodPicker;
  end
  else if CurPageID = UltimatePage.ID then
    UpdateUltimatePicker;

  if CurPageID <> wpFinished then
    Exit;
  if gUpdater or gFailed or WizardSilent then
  begin
    EndGameRadio.Visible := False;
    EndNothingRadio.Visible := False;
    Exit;
  end;
  gFinishReached := True;
  LayoutFinishChoices;
  EndGameRadio.Checked := gRunAtEnd = RunAtEndGame;
  EndNothingRadio.Checked := gRunAtEnd = RunAtEndNothing;
end;

// D8: the chosen action, after the wizard has closed. The game is started with
// the same argument its shortcut carries, and it reads its own settings and input
// profile (Contract 3), so this honours the target the install left behind without
// a second process.
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
  else
    Exit;
  if not Exec(exePath, parameters, ExpandConstant('{app}'), SW_SHOWNORMAL, ewNoWait, code) then
    Log('run at end: could not start ' + exePath + ': ' + SysErrorMessage(code));
end;
