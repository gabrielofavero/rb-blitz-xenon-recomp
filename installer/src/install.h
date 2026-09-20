// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The install itself: where files go, and the four things the installer moves.
//
//   1. the payload  - the recompiled runtime (rb_blitz.exe + the ReXGlue DLLs).
//                     Shipped inside the setup executable, or fetched from the
//                     pinned release URL when the payload is too large for that.
//   2. the game data - the user's own dump, imported either from an extracted
//                     folder or straight out of the Xbox 360 STFS package. Both
//                     are verified against config/game_fingerprints.toml before
//                     they are put in place, so a wrong revision is rejected
//                     while it is still in staging.
//   3. the Ultimate mod - optional, never redistributed, taken from the pinned
//                     upstream release or from an archive/folder the user has.
//   4. the manifest and report - what was installed, from where, and the log.
//
// Everything below runs inside the install directory the wizard chose, and each
// step stages and verifies before it commits: a component that fails its
// fingerprint check leaves the previous install untouched.
//
// Layout of an installed {app} directory:
//
//   {app}/rb_blitz.exe                 payload, copied by Inno Setup [Files]
//   {app}/payload-manifest.toml        payload, hashes of the files above
//   {app}/game/default.xex             imported dump
//   {app}/game/gen/main_xbox*.ark
//   {app}/game/ultimate/gen/*.hdr      optional Ultimate overlay
//   {app}/install-manifest.toml        written by FinalizeInstall
//   {app}/install-report.txt           written by FinalizeInstall
//   {app}/.staging/                    transient, always removed on the way out

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "stfs.h"

namespace rb_blitz::installer {

// --- layout ---------------------------------------------------------------

inline constexpr std::string_view kGameDirName = "game";
inline constexpr std::string_view kUltimateDirName = "ultimate";
inline constexpr std::string_view kStagingDirName = ".staging";
inline constexpr std::string_view kPayloadManifestName = "payload-manifest.toml";
inline constexpr std::string_view kInstallManifestName = "install-manifest.toml";
inline constexpr std::string_view kInstallReportName = "install-report.txt";
inline constexpr std::string_view kInstallLogName = "install.log";
inline constexpr std::string_view kRuntimeExeName = "rb_blitz.exe";
inline constexpr std::string_view kHelperExeName = "rb_blitz_setup_helper.exe";

// The roles in config/game_fingerprints.toml that identify a Rock Band Blitz
// dump. All three are mandatory: the archive trio is what proves the user has
// the revision the recompiled code was generated for.
inline constexpr std::string_view kGameRoles[] = {"entrypoint", "archive-header", "archive-data"};

// Roles in installer/config/ultimate_fingerprints.toml that must be present for
// the mod to be usable. `ultimate-entrypoint-rejected` is deliberately absent:
// the payload's own default.xex is never installed (src/hooks/ultimate.cpp
// reproduces its 12-byte delta on the host), so it cannot be laid down and
// verified the same way.
inline constexpr std::string_view kUltimateRoles[] = {"ultimate-archive-header",
                                                      "ultimate-archive-data"};

// Dump files the runtime never opens, but that belong to a complete game root.
// Copied when the source has them, so an imported root is indistinguishable from
// the dump the user would have made by hand.
inline constexpr std::string_view kOptionalGameFiles[] = {"ArcadeInfo.xml", "charnames.zbm",
                                                          "nxeart"};

inline constexpr std::string_view kHelperVersion = "1.0.0";

// Long steps write one integer percent per line, plus an optional label line, for
// the wizard's progress page to poll. An empty `file` disables reporting.
struct ProgressSink {
  std::filesystem::path file;

  void Report(int percent) const;
  void Report(int percent, std::string_view detail) const;
};

// --- paths ----------------------------------------------------------------

std::filesystem::path GameRoot(const std::filesystem::path& app_dir);
std::filesystem::path StagingRoot(const std::filesystem::path& app_dir);

// --- game data ------------------------------------------------------------

enum class GameSourceKind { kFolder, kPackage };

// One file the import knows about. `required` entries come from the embedded
// fingerprint (they have a size and a hash); optional ones are the dump extras
// above and are only checked for existence.
struct GameFilePlan {
  std::string path;
  std::uint64_t size = 0;
  std::string sha256;
  bool required = false;
  bool present = false;
};

// What a source folder or package holds, before anything is written. Planning is
// read-only, so the wizard can show the user what it found and refuse to start
// an install that could not succeed.
struct GameSourcePlan {
  GameSourceKind kind = GameSourceKind::kFolder;
  std::filesystem::path location;  // folder: the resolved game root; package: the file
  std::vector<GameFilePlan> files;
  StfsInfo volume;                 // package only
  std::size_t package_entries = 0;  // package only, file table entries
  std::string description;          // one line, for the report and the log

  std::size_t RequiredCount() const;
  std::size_t PresentCount() const;
  std::uint64_t PresentBytes() const;
  // Missing mandatory file paths, empty when the plan can be imported.
  std::vector<std::string> Missing() const;
  bool Complete() const { return Missing().empty(); }
};

// Accepts the game root itself or a directory containing it (a folder holding
// "Rock Band Blitz/" and readmes, for instance).
bool PlanGameFolder(const std::filesystem::path& folder, GameSourcePlan* out, std::string* error);
bool PlanGamePackage(const std::filesystem::path& package, GameSourcePlan* out, std::string* error);

// Copies every present file into <game_root>/.staging, verifies the mandatory
// ones there, and only then moves them into place. `game_root` is created when
// missing. On failure nothing is committed and the staging directory is removed.
bool ImportGame(const GameSourcePlan& plan, const std::filesystem::path& game_root,
                const ProgressSink& progress, std::string* error);

// Re-checks an installed game root against the embedded fingerprint, filling
// `evidence` (when not null) with one line per checked file.
bool VerifyGameTree(const std::filesystem::path& game_root, std::string* evidence,
                    std::string* error);

// --- payload --------------------------------------------------------------

// Where a component's files come from.
//   kEmbedded - already copied into the install directory by the wizard; only
//               verified, never moved.
//   kUrl      - downloaded, size and hash checked, then extracted.
//   kZip      - the user's own archive of the payload.
//   kFolder   - the user's own payload directory (a local build).
enum class SourceKind { kEmbedded, kUrl, kZip, kFolder };

struct SourceRef {
  SourceKind kind = SourceKind::kEmbedded;
  std::filesystem::path path;  // kZip/kFolder: what the user picked
  std::string url;             // kUrl
  std::string sha256;          // empty: the source is trusted but still fingerprinted
  std::uint64_t size = 0;
  std::string version;         // recorded in the manifest, not verified
};

std::string DescribeSource(const SourceRef& source);
bool IsRemote(const SourceRef& source);

struct ComponentResult {
  std::string version;
  std::size_t files = 0;
  std::uint64_t bytes = 0;
  std::string source;               // DescribeSource()
  bool fingerprints_matched = true;
  std::vector<std::string> notes;   // warnings worth keeping in the report
};

// kEmbedded: verifies the payload the wizard copied into `app_dir` against
// `app_dir/payload-manifest.toml`. `pins` only supplies the version for the
// report and the manifest; the hashes come from the manifest itself.
bool VerifyPayload(const std::filesystem::path& app_dir, const Pins& pins, ComponentResult* out,
                   std::string* error);

// kUrl/kZip/kFolder: materialises the payload into `app_dir`. The download and
// the extraction both go through `app_dir/.staging`, and the payload is
// verified against its manifest before anything is copied into place.
bool InstallPayload(const SourceRef& source, const std::filesystem::path& app_dir,
                    const Pins& pins, const ProgressSink& progress, ComponentResult* out,
                    std::string* error);

// --- the Ultimate mod -----------------------------------------------------

// kUrl/kZip/kFolder: installs the mod under <game_root>/ultimate. The payload's
// own default.xex is skipped, and a mismatch against the embedded Ultimate
// fingerprints is recorded as a warning instead of a failure: the overlay is
// data the runtime can read, and an unexpected version is still likely to load.
bool InstallUltimate(const SourceRef& source, const std::filesystem::path& game_root,
                     const Pins& pins, const ProgressSink& progress, ComponentResult* out,
                     std::string* error);

// --- finishing up ---------------------------------------------------------

struct InstallSummary {
  std::filesystem::path install_dir;
  std::filesystem::path game_dir;
  std::string game_source;      // GameSourcePlan::description
  std::string game_evidence;    // fingerprint check lines, one per line
  ComponentResult payload;
  bool ultimate_installed = false;
  ComponentResult ultimate;
  std::string installer_version;
};

// Writes install-manifest.toml (machine-readable, for a future upgrade or
// uninstaller) and install-report.txt (what the user can attach to a bug).
bool FinalizeInstall(const InstallSummary& summary, std::string* error);

// Removes everything the installer created and Inno Setup's own uninstall log
// does not cover: the imported game data (unless the user asked to keep it), the
// payload build itself, the manifest, the report, the log and any leftover
// staging. The payload is removed even when Inno Setup tracks it, since deleting
// a file twice is harmless and a downloaded payload would otherwise be orphaned.
bool UninstallCleanup(const std::filesystem::path& app_dir, bool keep_game_data,
                      std::vector<std::string>* removed, std::string* error);

}  // namespace rb_blitz::installer
