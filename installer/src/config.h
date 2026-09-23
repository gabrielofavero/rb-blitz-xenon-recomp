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

  // Validates what the installer depends on and reports the first problem.
  bool Validate(std::string* error) const;
};

bool ParsePins(std::string_view text, Pins* out, std::string* error);

// The embedded copies, parsed and validated once per process.
const Pins& EmbeddedPins();
const std::string& EmbeddedGameFingerprints();
const std::string& EmbeddedUltimateFingerprints();

}  // namespace rb_blitz::installer
