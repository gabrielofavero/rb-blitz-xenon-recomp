// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 — native settings persistence (docs/settings/contracts.md §4.1/§4.4/§5.4).
//
// Two layers live here:
//
//   1. `toml::Document`, a comment- and unknown-key-preserving line editor. It parses the
//      scalar and single-value array lines the settings files use, and patches only the keys
//      it is asked to change. Everything else — comments, unknown tables, unknown keys,
//      spacing it did not touch — is carried through byte for byte, so a load-then-save with
//      no edits renders the identical text and the caller can skip the write entirely.
//
//   2. `Document`/`Store`, the typed settings model on top of it. `Store` re-reads the file
//      at save time, merges only the keys the caller changed, refuses a key another writer
//      changed concurrently (never "last writer wins"), and writes through a flushed
//      temporary sibling that atomically replaces the destination.
//
// SDK-free on purpose: the game, the installer and the tests all compile this, and the tests
// run it without a boot (parent plan §4.4 asks for exactly that).

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "settings/model.h"

namespace rb_blitz::settings::toml {

enum class Style { kBare, kBasic, kLiteral };

// One key as read: a scalar, a string array, an opaque array/inline table, or nothing.
struct Item {
  bool found = false;
  bool scalar = false;     // false = array or inline table (never mutated as a scalar)
  Style style = Style::kBare;
  std::string value;       // a scalar's unquoted text / a bare token
  std::vector<std::string> list;
  bool list_ok = false;    // true when an array parsed as a list of strings
};

// Renders a value the way the file spells it.
std::string RenderBare(std::string_view value);
std::string RenderBasic(std::string_view value);
std::string RenderLiteral(std::string_view value);
std::string RenderStringList(const std::vector<std::string>& values);

// A key is written back with its original spelling; a *new* key that is not bare-safe
// (anything but [A-Za-z0-9_-]) is quoted as a literal string, the form the plan's
// `'graphics.resolution'` provenance key uses.
std::string QuoteKeyIfNeeded(std::string_view key);

bool ParseBareInt(std::string_view text, int64_t* out);
bool ParseBareBool(std::string_view text, bool* out);

class Document {
 public:
  Document() = default;

  // Parses `text`. A document that does not parse has parsed() == false and an error; the
  // caller must then leave the file alone rather than write a reinterpretation of it.
  static Document Parse(const std::string& text, bool* ok, std::string* error);

  const std::string& text() const { return text_; }
  const std::string& original() const { return original_; }
  bool parsed() const { return parsed_; }
  const std::string& error() const { return error_; }
  bool dirty() const { return text_ != original_; }

  bool Has(std::string_view section, std::string_view key) const;
  Item Get(std::string_view section, std::string_view key) const;
  std::vector<std::string> Keys(std::string_view section) const;
  bool HasSection(std::string_view section) const;

  // Replaces the key's value with `rendered_value` (already in file spelling), keeping the
  // key's own spelling, spacing and trailing comment. Creates the key and, if needed, its
  // section when the key is new.
  void Set(std::string_view section, std::string_view key, std::string_view rendered_value);
  // Appends `rendered_value` under an existing key only when the key is absent; a no-op when
  // the key is already there, so an imported-once value is never duplicated.
  bool SetIfAbsent(std::string_view section, std::string_view key, std::string_view rendered_value);
  void Erase(std::string_view section, std::string_view key);

  std::string Render() const { return text_; }

 private:
  std::string text_;
  std::string original_;
  std::string error_;
  bool parsed_ = true;
  bool reparse(std::string next, std::string* error);
};

}  // namespace rb_blitz::settings::toml

namespace rb_blitz::settings {

// --- the typed settings document -------------------------------------------

struct Document {
  int32_t schema_version = 1;
  std::map<SettingId, Value> values;    // requested values actually persisted
  std::map<SettingId, Origin> origins;  // [provenance]
  int32_t automatic_policy_version = 1;
  bool migration_imported = false;                  // [migration] imported
  std::map<std::string, Value> ultimate_overrides;  // [ultimate_overrides]
};

enum class DocumentStatus { kOk, kMissing, kMalformed, kUnsupportedVersion };

struct DocumentLoad {
  DocumentStatus status = DocumentStatus::kMissing;
  std::string error;
  Document document;
  bool usable() const {
    return status == DocumentStatus::kOk || status == DocumentStatus::kMissing;
  }
};

// Reads `<file>`. A missing file is not an error (the compiled defaults apply). A malformed
// file, or one whose schema_version this build does not know, is reported and never
// reinterpreted — the caller leaves it intact and refuses to save over it.
DocumentLoad LoadSettingsDocument(const std::filesystem::path& file);

// The value that would be written for a row, or false when the row has no persisted value.
bool PersistedValue(SettingId id, const Value& value, std::string* rendered, toml::Style* style);

struct SaveRequest {
  // The values as loaded (absent == the key was absent on disk). Used to detect a concurrent
  // writer: a key whose on-disk value differs from `base` is a conflict, never overwritten.
  std::map<SettingId, Value> base;
  std::map<SettingId, Value> changed;
  std::map<SettingId, Origin> origins;  // provenance for every persisted row we own
  std::map<std::string, Value> overrides_changed;
  std::vector<std::string> overrides_removed;
  bool set_policy_version = false;
  int32_t policy_version = 1;
  bool set_migration_imported = false;
};

struct SaveOutcome {
  CommitStatus status = CommitStatus::kNoChanges;
  bool wrote = false;
  std::vector<SettingId> conflicts;
  std::vector<std::string> errors;
};

// Applies `request` to `file` with reread/merge/conflict/atomic-replace semantics.
SaveOutcome SaveSettingsDocument(const std::filesystem::path& file, const SaveRequest& request);

// --- the Store facade ------------------------------------------------------

struct StorePaths {
  std::filesystem::path settings_file;   // <dir>/settings.toml
  std::filesystem::path paths_file;      // <install>/paths.toml
  std::filesystem::path legacy_profile;  // <dir>/launcher.toml (may be empty)
};

// Owns the loaded document and the staged changes a boot or a menu session wants written.
// Migrate() drives it; the service drives it for commits.
class Store {
 public:
  explicit Store(StorePaths paths);

  const StorePaths& paths() const { return paths_; }
  DocumentLoad Load();
  const DocumentLoad& load() const { return load_; }

  Document& document() { return document_; }
  const Document& document() const { return document_; }

  void MarkChanged(SettingId id, const Value& value, Origin origin);
  void MarkOverride(std::string key, const Value& value);
  void RemoveOverride(std::string_view key);
  void MarkPolicyVersion(int32_t version);
  void MarkMigrationImported();

  bool has_changes() const;
  SaveOutcome Save();

 private:
  StorePaths paths_;
  DocumentLoad load_;
  Document document_;
  std::map<SettingId, Value> base_;
  std::map<SettingId, Value> changed_;
  std::map<SettingId, Origin> origins_;
  std::map<std::string, Value> overrides_changed_;
  std::vector<std::string> overrides_removed_;
  bool set_policy_version_ = false;
  bool set_migration_imported_ = false;
};

// Reads a whole file into a string; false when it cannot be read. An absent file reads as an
// empty string with `existed` set false.
bool ReadFileText(const std::filesystem::path& file, std::string* text, bool* existed);

// Writes `text` to a flushed temporary sibling, then atomically replaces `file`. Creates the
// parent directory. On failure the destination keeps its old bytes.
bool WriteFileAtomic(const std::filesystem::path& file, const std::string& text, std::string* error);

}  // namespace rb_blitz::settings
