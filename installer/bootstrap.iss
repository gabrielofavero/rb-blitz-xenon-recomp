; ---------------------------------------------------------------------------
; Rock Band Blitz - the two bootstrap setup executables.
;
; Three products are built from this directory (README.md, section "The three
; products"), and this script is the two that install nothing themselves:
;
;   RockBandBlitzSetup-<version>.exe   the complete package (setup.iss). The
;                                      recompiled build travels inside it, so it
;                                      installs with no network access at all.
;                                      This is the *pinned* build: a release,
;                                      named after its version.
;
;   RockBandBlitzSetup-latest.exe      this script, -DChannelName=latest. It
;                                      carries no payload and installs nothing:
;                                      it asks GitHub for the newest *release* of
;                                      this repository, downloads the one file on
;                                      it, checks it, and runs it.
;
;   RockBandBlitzSetup-nightly.exe     the same script, -DChannelName=nightly. It
;                                      asks for the newest build of *main* - the
;                                      newest release the repository marks as a
;                                      prerelease - and does the same with it.
;
; So "latest" always means a release somebody cut, and "nightly" always means the
; newest thing main produced, whether or not it was released. A release publishes
; exactly one file, which is why the two channels are told apart by GitHub's own
; `prerelease` flag rather than by a naming convention in the tags.
;
; A release publishes exactly one file - its setup executable - so the newest
; release's file is exactly what a bootstrap needs: there is no manifest to
; publish, no second asset to keep in step with the first, and no bootstrap that
; can disagree with the installer it starts.
;
; It is deliberately its own script rather than a third mode of setup.iss. The
; wizard is a page-by-page question about where the game goes; this has no
; questions, no install folder and no uninstaller, and every line of the wizard's
; sequencing is a line this would have to be compiled out of. What the two share
; is the helper - the same rb_blitz_setup_helper.exe the wizard runs - and the
; pins, which are read here for the same reason they are read there: the version,
; the name and the homepage come from one file.
;
; Build it with build.ps1 -Latest or -Nightly, which compile the helper first and
; then call ISCC on this script with the channel's defines.
; ---------------------------------------------------------------------------

#define InstallerDirPath AddBackslash(SourcePath)
#define ProjectDirPath AddBackslash(AddBackslash(SourcePath) + "..")

#ifndef GeneratedDir
  #define GeneratedDir AddBackslash(SourcePath) + "out\generated"
#endif
#ifndef DistDir
  #define DistDir AddBackslash(SourcePath) + "out\dist"
#endif
#ifndef ArtDir
  #define ArtDir ProjectDirPath + "assets\installer"
#endif

; Which channel this is. build.ps1 passes all three, so the facts about a channel
; live in one place (the script that also builds the helper) and this file only
; spends them.
#ifndef ChannelName
  #error ChannelName is undefined: build this with build.ps1 -Latest or -Nightly, which pass it.
#endif
#ifndef ChannelLabel
  #error ChannelLabel is undefined: build this with build.ps1 -Latest or -Nightly, which pass it.
#endif
#ifndef ChannelWhat
  #error ChannelWhat is undefined: build this with build.ps1 -Latest or -Nightly, which pass it.
#endif

#define GeneratedDirPath AddBackslash(GeneratedDir)
#define DistDirPath AddBackslash(DistDir)
#define ArtDirPath AddBackslash(ArtDir)

#define HelperExeName "rb_blitz_setup_helper.exe"

#if !FileExists(GeneratedDirPath + "pins.iss")
  #error pins.iss is missing from the generated directory: run build.ps1 (or the helper build) first.
#endif
#include GeneratedDirPath + "pins.iss"

#if !FileExists(GeneratedDirPath + HelperExeName)
  #error the helper is missing from the generated directory: build.ps1 copies it there.
#endif

; Setup's version resource needs four components; the app version has three.
#define SetupVersionQuad AppVersion + ".0"

[Setup]
; A different AppId from the installer's, and from the other bootstrap's: neither
; of these ever registers an installation, and neither must be able to touch the
; uninstall entry of the installation whose setup it is about to run.
AppId={{5B0F7C62-91D8-4A3E-8E27-C4D2A9F60B18}-{#ChannelName}
AppName={#AppName}
; The title bar and Add/Remove Programs say which build this is, not which version of
; the project it was compiled from: "Rock Band Blitz latest" and "Rock Band Blitz
; nightly" are what somebody downloaded, and 0.1.0 is a version the bootstrap does not
; install - the build it fetches has its own. AppVersion stays the pins' version, so
; the version resource and the update-check-free comparisons stay well-formed.
AppVerName={#AppName} {#ChannelName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppHomepageUrl}
AppSupportURL={#AppSupportUrl}
AppUpdatesURL={#AppHomepageUrl}
VersionInfoVersion={#SetupVersionQuad}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} ({#ChannelLabel})
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
; Nothing is installed here: no folder, no files, no uninstaller. The release's
; own setup is what installs, and it asks its own questions - this one only has
; to fetch it.
CreateAppDir=no
Uninstallable=no
DefaultDirName={tmp}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
DisableWelcomePage=yes
AllowNoIcons=yes
ShowLanguageDialog=no
; No elevation here: the download goes to the user's own temp folder, and the
; release's setup asks for whatever it needs when it runs. A bootstrap that
; required an administrator for a file download would ask for rights it does not
; use.
PrivilegesRequired=lowest
MinVersion={#AppMinWindowsBuild}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#DistDirPath}
OutputBaseFilename=RockBandBlitzSetup-{#ChannelName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
SetupIconFile={#ProjectDirPath}assets\icon\blitz.ico
; The side image is optional here for the same reason it is in the wizard: the
; art is not committed and its absence is a supported configuration.
#if FileExists(ArtDirPath + "wizard-large-202x386.bmp")
WizardImageFile={#ArtDirPath}wizard-large-*.bmp
#endif
WizardSmallImageFile={#ProjectDirPath}assets\icon\blitz.png

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Only the helper. It is what talks to GitHub and hashes what arrived; Pascal
; Script can do neither. `dontcopy` because there is no install folder to copy it
; to - ExtractTemporaryFile below is the only way it is needed.
Source: "{#GeneratedDirPath}{#HelperExeName}"; Flags: dontcopy

[Code]

const
  // 150 ms x 2400 = six minutes, which is a slow line downloading a ~100 MB setup
  // with room to spare. A download that has not finished by then is one the user
  // should retry rather than watch: the progress bar keeps moving while it runs.
  kPollIntervalMs = 150;
  kPollLimit = 2400;
  // The helper's file name, for the sentences in [Code] (the [Files] entry uses the
  // ISPP define of the same name, which is not visible here).
  HelperFileName = 'rb_blitz_setup_helper.exe';
  // The two file names a bootstrap publishes under. A bootstrap installed by a
  // bootstrap would loop for ever, so an asset called either of these is refused
  // by name - which is the one mistake that gets here, and the one worth naming.
  LatestSetupName = 'rockbandblitzsetup-latest.exe';
  NightlySetupName = 'rockbandblitzsetup-nightly.exe';

var
  StagePage: TOutputProgressWizardPage;
  // Where this bootstrap keeps its own working files: %APPDATA%\rb_blitz\bootstrap.
  // It is the user's files rather than a release asset or a file next to the setup
  // on purpose - what this build resolved is a record of an install, and a record
  // belongs with the user who installed it (the same rule the install manifest
  // follows). The helper's log and summary survive in there, so a failed download
  // can be looked at afterwards instead of vanishing with a temporary folder. The
  // file names carry the channel, so a nightly run and a latest run do not
  // overwrite each other's evidence.
  gWorkDir: String;
  gSummaryFile: String;
  gLogFile: String;
  gProgressFile: String;
  // The downloaded release's own setup. A 100 MB installer does not belong in the
  // user's application data, so it goes to the temporary folder and is removed
  // again once it has installed successfully.
  gDownloadDir: String;
  gAssetPath: String;
  gAssetName: String;
  gAssetSha256: String;
  gAssetSize: String;
  gTagName: String;
  // Which commit the build is from, and main's own tip when the channel asked (the
  // nightly one does): "nightly" without a commit is not an answer anybody can act
  // on, and "is this the newest commit?" is the question of that channel.
  gCommit: String;
  gHeadCommit: String;
  gAtTip: String;
  gChannelName: String;
  // What the release's own setup answered. Zero until it has run, which is also
  // the value this process exits with when it did run and succeeded.
  gChildExitCode: Integer;
  gResolveError: String;

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
  Result := root;
  if (Result <> '') and (Result[Length(Result)] <> '\') then
    Result := Result + '\';
  Result := Result + leaf;
end;

// The helper writes `key=value;key=value` on one line. Split on ';' first,
// because a value (a path, a sentence) may contain '='.
function SummaryText(const file, key, fallback: String): String;
var
  lines: TArrayOfString;
  rest, item, name: String;
  separator: Integer;
  index: Integer;
begin
  Result := fallback;
  if not FileExists(file) then
    Exit;
  if not LoadStringsFromFile(file, lines) then
    Exit;
  rest := '';
  for index := 0 to GetArrayLength(lines) - 1 do
  begin
    if index > 0 then
      rest := rest + #13#10;
    rest := rest + lines[index];
  end;
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

// `ok` is the last key the helper writes, so the summary file doubles as the
// completion signal - which is what makes a poll loop possible without a process
// handle: there is nothing else to ask about a helper that has been started with
// ewNoWait.
function HelperFinished(const file: String): Boolean;
begin
  Result := SummaryText(file, 'ok', '') <> '';
end;

// Why a process could not be started, as a plain clause for a sentence, or '' when
// Windows has nothing to say. CreateProcess leaves nothing in the last-error slot
// when the file it was given is not there, so SysErrorMessage(0) would announce
// "the operation completed successfully" in front of a failure - which is worse
// than saying nothing. The callers check that the file exists first, so this is
// only for the cases Windows really does explain.
function StartFailureClause: String;
var
  code: Integer;
begin
  code := DLLGetLastError;
  if code = 0 then
    Result := 'no reason given by Windows'
  else
    Result := SysErrorMessage(code);
end;

function HelperPath: String;
begin
  // The helper is extracted into the setup's own temporary folder (InitializeWizard),
  // which is where it runs from. It is deliberately not copied anywhere else: this
  // bootstrap installs nothing, so the only file it needs is the one it just
  // unpacked.
  Result := JoinPath(ExpandConstant('{tmp}'), HelperFileName);
end;

// The progress bar's percent, from the helper's progress file: a step counter, not
// a byte count, because what the helper is doing (asking, then downloading) is
// what the user is waiting for.
function ProgressPercent: Integer;
var
  text: String;
  lines: TArrayOfString;
begin
  Result := -1;
  if not FileExists(gProgressFile) then
    Exit;
  if not LoadStringsFromFile(gProgressFile, lines) then
    Exit;
  if GetArrayLength(lines) = 0 then
    Exit;
  text := Trim(lines[0]);
  if text = '' then
    Exit;
  Result := StrToIntDef(text, -1);
end;

// Inno Setup does not pump messages while [Code] is running, so the page would
// otherwise not repaint while the download runs: TControl.Refresh is the only way
// to make it follow the helper. (The wizard's own stage page does the same thing,
// for the same reason - setup.iss, PumpStagePaint.)
procedure PumpPaint;
begin
  StagePage.Msg1Label.Refresh;
  StagePage.Msg2Label.Refresh;
  StagePage.ProgressBar.Refresh;
end;

procedure Say(const headline, detail: String);
begin
  StagePage.SetText(headline, detail);
  PumpPaint;
end;

// The release API this run asks, or '' for the one the pins name. A custom
// parameter - /LATEST_API=http://127.0.0.1:8731/releases/latest - rather than a
// setting, because it is a test hook and nothing else: it is what lets the whole
// path (ask, download, verify, run) be exercised against a server on this machine
// without publishing a release to do it with, which is the same reason the helper
// accepts --api (README.md, section "The three products").
function ApiOverride: String;
begin
  Result := ExpandConstant('{param:LATEST_API|}');
end;

// --- what the newest build of this channel is -----------------------------

// Runs the helper's `resolve-build --channel <this channel>`, which asks GitHub
// for the newest build of the channel and downloads the one file on it, checking
// the size and the sha256 the release resource reports. On success gAssetPath
// names the file to run; on failure gResolveError says why, in a sentence to show
// the user.
function ResolveBuild: Boolean;
var
  waited: Integer;
  percent: Integer;
  code: Integer;
  reason: String;
  api: String;
  helperArgs: String;
begin
  Result := False;
  gResolveError := '';
  gAssetPath := '';
  gAssetName := '';
  gTagName := '';

  DeleteFile(gSummaryFile);
  DeleteFile(gProgressFile);
  DeleteFile(gLogFile);

  Say('Getting {#ChannelWhat}', 'Asking GitHub which build is the newest one...');

  if not FileExists(HelperPath) then
  begin
    ExtractTemporaryFile(HelperFileName);
    if not FileExists(HelperPath) then
    begin
      gResolveError := 'The helper (' + HelperFileName + ') could not be unpacked from this ' +
                       'setup, so {#ChannelWhat} could not be fetched. Build it again with ' +
                       'build.ps1 {#ChannelBuildSwitch}.';
      Exit;
    end;
  end;

  helperArgs := 'resolve-build --channel {#ChannelName}' +
                ' --dest ' + QuoteArg(gDownloadDir) +
                ' --summary ' + QuoteArg(gSummaryFile) +
                ' --log ' + QuoteArg(gLogFile) +
                ' --progress ' + QuoteArg(gProgressFile);
  api := ApiOverride;
  if api <> '' then
    helperArgs := helperArgs + ' --api ' + QuoteArg(api);

  if not Exec(HelperPath, helperArgs, gWorkDir, SW_HIDE, ewNoWait, code) then
  begin
    gResolveError := 'Could not start the helper (' + StartFailureClause + '), so ' +
                     '{#ChannelWhat} could not be fetched. The helper is ' + HelperPath + '.';
    Exit;
  end;

  waited := 0;
  while (not HelperFinished(gSummaryFile)) and (waited < kPollLimit) do
  begin
    percent := ProgressPercent;
    if percent >= 0 then
      StagePage.SetProgress(percent, 100);
    if gAssetName <> '' then
      Say('Getting {#ChannelWhat}', 'Downloading ' + gAssetName)
    else
      Say('Getting {#ChannelWhat}', 'Asking GitHub which build is the newest one...');
    Sleep(kPollIntervalMs);
    waited := waited + 1;
  end;

  if not HelperFinished(gSummaryFile) then
  begin
    gResolveError := 'The download did not finish in time. Check the connection and run this ' +
                     'setup again; nothing has been installed or changed.';
    Exit;
  end;

  // Written before the failure is looked at, so the message can name the release
  // that was being fetched when it went wrong.
  gAssetName := SummaryText(gSummaryFile, 'asset_name', '');
  gTagName := SummaryText(gSummaryFile, 'tag_name', '');

  if not (SummaryText(gSummaryFile, 'ok', '0') = '1') then
  begin
    reason := Trim(SummaryText(gSummaryFile, 'error', ''));
    if reason = '' then
      reason := 'the helper did not say what went wrong';
    gResolveError := 'Could not get {#ChannelWhat}: ' + reason;
    Exit;
  end;

  gAssetName := SummaryText(gSummaryFile, 'asset_name', '');
  gAssetPath := SummaryText(gSummaryFile, 'path', '');
  gAssetSha256 := SummaryText(gSummaryFile, 'asset_sha256', '');
  gAssetSize := SummaryText(gSummaryFile, 'asset_size', '');
  gCommit := SummaryText(gSummaryFile, 'commit', '');
  gHeadCommit := SummaryText(gSummaryFile, 'head_commit', '');
  gAtTip := SummaryText(gSummaryFile, 'at_tip', '');
  if gAssetPath = '' then
  begin
    gResolveError := 'The newest build named no file to run, so there is nothing to install.';
    Exit;
  end;

  // A path the helper reports is a path on the same machine, so it is absolute: a
  // relative one means the helper was handed a relative --dest, and running it from
  // here would look for the file under *this* process's working directory. Refused
  // with the path in the message, because "could not be started (no reason given by
  // Windows)" is what that failure looks like otherwise.
  if (Length(gAssetPath) < 3) or (gAssetPath[2] <> ':') then
  begin
    gResolveError := 'The download went somewhere this setup cannot run it from: ' + gAssetPath +
                     ' (a relative path, so the folder it was downloaded into did not resolve). ' +
                     'Nothing has been installed or changed.';
    Exit;
  end;
  Log('bootstrap: resolved ' + gAssetName + ' (' + gAssetSize + ' bytes) at ' + gAssetPath);

  // A bootstrap installing a bootstrap would loop for ever: the asset this
  // downloads has to be a release's own complete installer. Publishing a
  // bootstrap as the newest build's file is the one mistake that gets here, and
  // refusing it by name is what names the fix.
  if (LowerCase(gAssetName) = LatestSetupName) or (LowerCase(gAssetName) = NightlySetupName) then
  begin
    gResolveError := 'The newest build (' + gTagName + ') publishes a bootstrap (' +
                     gAssetName + '), which is itself a downloader. Publish a complete ' +
                     'setup for it instead - build.ps1 without -Latest or -Nightly.';
    Exit;
  end;

  Result := True;
end;

// What this bootstrap resolved, written where the user's own files are. It is a
// record rather than a setting: it is what makes "which build did this machine
// install" answerable after the fact, next to the install manifest the release's
// own setup writes - and it is the only thing this executable leaves behind.
//
// `commit` and `at_tip` are the nightly channel's own answers (for `latest` they are
// left out entirely rather than written as empty): which commit was installed, and
// whether main had moved on by the time it was).
procedure WriteBootstrapRecord;
var
  text: String;
begin
  text := '# Written by RockBandBlitzSetup-{#ChannelName}.exe. Do not edit.' + #13#10 +
          '# What the last run of this bootstrap resolved and installed.' + #13#10 +
          'channel = "{#ChannelName}"' + #13#10 +
          'tag_name = "' + gTagName + '"' + #13#10 +
          'asset_name = "' + gAssetName + '"' + #13#10 +
          'asset_sha256 = "' + gAssetSha256 + '"' + #13#10 +
          'asset_size = ' + gAssetSize + #13#10;
  if gCommit <> '' then
    text := text + 'commit = "' + gCommit + '"' + #13#10;
  if gHeadCommit <> '' then
    text := text + 'head_commit = "' + gHeadCommit + '"' + #13#10;
  if gAtTip <> '' then
    text := text + 'at_tip = ' + gAtTip + #13#10;
  text := text + 'fetched_at = "' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + '"' + #13#10;
  SaveStringToFile(JoinPath(gWorkDir, '{#ChannelName}.toml'), text, False);
end;

// What to call the build in a sentence: "nightly 96dce40" when the release names a
// commit, the tag otherwise.
function InstallLabel: String;
begin
  if gCommit <> '' then
    Result := '{#ChannelName} ' + gCommit
  else
    Result := gTagName;
end;

// The release's own setup is the same wizard a user would otherwise have
// downloaded, so it takes the same parameters: rebuilding the line from ParamStr
// forwards /SILENT, /DIR=, /CURRENTUSER and everything else without this script
// having to know what any of them mean.
function ForwardedParams: String;
var
  index: Integer;
begin
  Result := '';
  for index := 1 to ParamCount do
    Result := Result + ' ' + QuoteArg(ParamStr(index));
end;

// --- the wizard --------------------------------------------------------

function InitializeSetup: Boolean;
begin
  gChannelName := '{#ChannelName}';
  // One folder for both channels, one set of files per channel: a machine can run
  // the nightly build and the latest one, and neither should erase the other's
  // record of what it installed.
  gWorkDir := JoinPath(ExpandConstant('{userappdata}'), 'rb_blitz\bootstrap');
  ForceDirectories(gWorkDir);
  // `{localappdata}\Temp` and not `{%TEMP%}`: the environment form expanded to
  // nothing in a silent run (measured 2026-10-15 - the helper then wrote a
  // *relative* path, and the run died at "the downloaded installer could not be
  // started" with the file sitting in the bootstrap folder), and Inno's own
  // constants never have that problem. The Log line below is what turns any repeat
  // into one line of the setup log.
  gDownloadDir := JoinPath(ExpandConstant('{localappdata}'), 'Temp\rb-blitz-' + gChannelName);
  ForceDirectories(gDownloadDir);
  Log('bootstrap: channel ' + gChannelName + ', working files in ' + gWorkDir +
      ', downloading into ' + gDownloadDir);
  gSummaryFile := JoinPath(gWorkDir, gChannelName + '-summary.txt');
  gLogFile := JoinPath(gWorkDir, gChannelName + '-resolve.log');
  gProgressFile := JoinPath(gWorkDir, gChannelName + '-progress.txt');
  gChildExitCode := 0;
  gResolveError := '';
  gCommit := '';
  gHeadCommit := '';
  gAtTip := '';
  Result := True;
end;

procedure InitializeWizard;
begin
  ExtractTemporaryFile('{#HelperExeName}');
  StagePage := CreateOutputProgressPage('Getting {#ChannelWhat}',
    'This finds {#ChannelWhat} published for Rock Band Blitz, checks it and runs it. The ' +
    'installer itself then asks its own questions.');
  StagePage.SetProgress(0, 100);
  Say('Getting {#ChannelWhat}', 'Starting...');
end;

// Every page is skipped: this executable asks nothing. The progress page above is
// not part of the wizard's page order - it is shown by this script - which is what
// makes a wizard with no pages possible at all.
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  code: Integer;
begin
  if CurStep <> ssInstall then
    Exit;

  StagePage.Show;
  if not ResolveBuild then
  begin
    // The exit code has to say "failed" as well as the message does: a person
    // double-clicking sees the box, and anything driving this setup silently sees
    // a non-zero exit. SuppressibleMsgBox and not MsgBox, because /SUPPRESSMSGBOXES
    // silences this one: a bootstrap driven by a harness must never sit waiting for
    // somebody to click OK (measured 2026-10-15: a MsgBox here left a modal on
    // screen for a run whose harness had already been killed).
    gChildExitCode := 1;
    StagePage.Hide;
    SuppressibleMsgBox(gResolveError, mbError, MB_OK, IDOK);
    Exit;
  end;

  Say('Installing ' + InstallLabel, 'Running ' + gAssetName + ' - it asks its own questions from here.');
  // A nightly that is not main's tip is worth saying out loud before it installs:
  // the user asked for the newest build of main, and what is published is the newest
  // build of main - not necessarily main itself.
  if gAtTip = '0' then
    Say('Installing ' + InstallLabel, 'Note: main has commits newer than this build (' +
        gHeadCommit + '). Running ' + gAssetName + ' anyway.');
  StagePage.SetProgress(0, 100);
  WriteBootstrapRecord;

  if not Exec(gAssetPath, ForwardedParams, '', SW_SHOWNORMAL, ewWaitUntilTerminated, code) then
  begin
    gChildExitCode := 1;
    StagePage.Hide;
    SuppressibleMsgBox('The downloaded installer could not be started (' + StartFailureClause +
                       '). Nothing has been installed or changed.', mbError, MB_OK, IDOK);
    Exit;
  end;

  gChildExitCode := code;
  Log('bootstrap: ' + gAssetName + ' exited with ' + IntToStr(code));
  // A successful install is the one case the download is removed: a failed or
  // cancelled one keeps it, so running this setup again does not fetch 100 MB a
  // second time and so a support question can be answered with "run the file in
  // %LOCALAPPDATA%\Temp\rb-blitz-{#ChannelName}".
  if code = 0 then
    DeleteFile(gAssetPath);
  StagePage.Hide;
end;

// The child's own answer, passed through: 0 when it installed, 1 when it failed,
// 5 when the user cancelled it, and 1 for a failure this script itself found. A
// caller that ran this setup silently can therefore tell what happened without
// reading a log.
function GetCustomSetupExitCode: Integer;
begin
  Result := gChildExitCode;
end;
