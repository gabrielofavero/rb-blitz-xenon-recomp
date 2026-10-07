// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The installer's own configuration: the release pins (installer/config/pins.toml)
// and the two fingerprint files, all of which tools/embed_config.cpp compiles into
// generated/embedded_config.h at build time.
//
// Pins are embedded rather than read from disk because the helper runs from a
// temporary directory on a machine that has never seen this repository, and the
// Inno Setup wizard gets the same values as ISPP defines from the same file, so
// the two can never disagree about what is being installed.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::installer {

// --- a TOML subset --------------------------------------------------------

// Values are kept as raw text: strings have their quotes removed, everything
// else (integers, booleans) is stored as written, so the readers below interpret
// it. That is all pins.toml needs, and it keeps the parser small enough to trust.
struct TomlValue {
  std::string text;
};

struct TomlTable {
  std::string name;
  bool is_array = false;
  std::vector<std::pair<std::string, TomlValue>> values;

  std::optional<std::string> Get(std::string_view key) const;
  std::string GetString(std::string_view key, std::string_view fallback = {}) const;
  std::uint64_t GetUnsigned(std::string_view key, std::uint64_t fallback = 0) const;
  bool GetBool(std::string_view key, bool fallback = false) const;
};

struct TomlDocument {
  std::vector<TomlTable> tables;
  // Last table with that name wins, which mirrors what a TOML reader does for
  // the flat files this installer reads.
  const TomlTable* Find(std::string_view name) const;
};

bool ParseTomlSubset(std::string_view text, TomlDocument* out, std::string* error);

// --- pins -----------------------------------------------------------------

struct PinsInstaller {
  std::string name;
  std::string short_name;
  std::string version;
  std::string publisher;
  std::string homepage_url;
  std::string support_url;
  std::string default_dir_name;
  std::string min_windows_build;
};

struct PinsPayload {
  std::string version;
  std::string url;
  std::string sha256;  // empty means "no download pin configured yet"
  std::uint64_t size = 0;
  // Commit of the recompiled build, resolved by installer/build.ps1 before the
  // pins are compiled in; empty when the build did not record one.
  std::string commit;

  bool HasDownload() const { return !url.empty(); }
};

// How a launcher learns that a newer release exists, and where the updater that
// installs one lives (docs/plans/launcher-plan.md D19).
struct PinsUpdate {
  // Where the launcher asks. Empty means this build has no release channel and no
  // launcher compiled from it checks anything.
  std::string manifest_url;
  // The folder under the user's local application data that holds the updater.
  std::string dir_name;
  // Whether this release's payload changes what the game reads from the user's own
  // game files: `false` keeps them, `true` asks for them again.
  bool requires_game_data = false;
};

struct PinsUltimate {
  std::string version;
  std::string url;
  std::string sha256;
  std::uint64_t size = 0;
  std::string repository_url;
  std::string release_url;
  std::string archive_prefix;
  std::string destination_dir;
};

struct Pins {
  int schema_version = 0;
  PinsInstaller installer;
  PinsPayload payload;
  PinsUltimate ultimate;
  PinsUpdate update;

  // Validates what the installer depends on and reports the first problem.
  bool Validate(std::string* error) const;
};

bool ParsePins(std::string_view text, Pins* out, std::string* error);

// --- the update manifest --------------------------------------------------

// update.toml, the one file a release publishes for the launcher's update check
// and for the updater that does the work. tools/embed_config.cpp generates it from
// the pins, so the version in it cannot drift from the version the setup executable
// is named after, and the release publishes it as the asset update.toml.
//
// Two readers, one document:
//   * the launcher reads `version` and `requires_game_data` - is there an update,
//     and will it ask for the game files again;
//   * the updater reads `payload_*` - what to install, from where, and what it
//     should hash to.
//
// `payload_url` empty is the honest state of a release that was built with its
// payload embedded (installer/build.ps1 without -PayloadUrl): there is nothing to
// download, so there is nothing for an updater to do, and both readers treat the
// manifest as "no update to offer" rather than offering one that cannot happen.
struct UpdateManifest {
  int schema_version = 0;
  std::string version;
  bool requires_game_data = false;
  std::string payload_version;
  std::string payload_url;
  std::string payload_sha256;
  std::uint64_t payload_size = 0;
  std::string payload_commit;

  bool HasPayload() const { return !payload_url.empty(); }
};

// The manifest schema this build understands. A manifest that needs a newer
// updater is refused with a message naming the release page, rather than acted on.
inline constexpr int kUpdateManifestSchemaVersion = 1;

bool ParseUpdateManifest(std::string_view text, UpdateManifest* out, std::string* error);

// The embedded copies, parsed and validated once per process.
const Pins& EmbeddedPins();
const std::string& EmbeddedGameFingerprints();
const std::string& EmbeddedUltimateFingerprints();

}  // namespace rb_blitz::installer
