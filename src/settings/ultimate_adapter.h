// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P6 — the Rock Band Blitz Ultimate **provider adapter**
// (docs/settings/plan.md §3 for where its rows go, §6 for the integration
// boundary, §5.5 for the per-row application strategy).
//
// The adapter is the only place that knows what the installed mod *looks like*. It does four
// things, in this order, and each is a pure function so the whole module is host-testable:
//
//   1. **Inspect the installation** (`InspectInstallation`): read the payload's ark index and
//      report which records the integration needs are present. A payload is not "installed"
//      because `gen/patch_xbox.hdr` exists — that only says the title will mount something.
//   2. **Check the shape** (`CheckShape`): parse the provider's *own* definitions out of the
//      archive and verify the objects, panels, rows, row kinds, persisted variable list and
//      compiled defaults this integration depends on. This is the plan's second compatibility
//      level, and it is the one that has to carry the weight: the mod's `default.xex` is
//      byte-identical in every release (docs/guides/ultimate-compat.md §3), so *the release is not
//      identifiable from the executable* and a version comparison would prove nothing.
//   3. **Bind the rows** (`RowBindings`): the frozen row identity P5b handed over — the
//      screen's stable `<category>.<id>` token, the provider's own variable name, the widget
//      kind, and the application strategy with the evidence behind it.
//   4. **Read and merge the provider's file** (`ParseProviderFile` / `WriteProviderFile`):
//      `ulti_settings.dta` as the provider's own `save_settings` writes it, preserving every
//      pair this build does not own and the file's own order.
//
// ---------------------------------------------------------------------------------------------
// Why this file exists on its own and is SDK-free
// ---------------------------------------------------------------------------------------------
// Everything above is bytes and strings: an ark index, a compiled-DTA tree, a text file. None of
// it needs a window, a guest or a boot, so tests/ultimate_adapter_tests.cpp can pin the rules
// against *the real installed payload* (`game/ultimate`) rather than a fixture - and a payload
// this build has never seen can be run through it before any of it is wired into the game.
//
// The one hosting decision that is NOT here: where the adapter runs. The redirect that makes the
// provider's own file writable (src/fs/overlay_merge.h + src/fs/payload_overlay.cpp) and the
// boot wiring (src/hooks/ultimate.cpp, src/hooks/settings_bridge_hooks.cpp) are the runtime half
// and live in the files that own those seams.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "settings/model.h"
#include "settings/service.h"  // ProviderReadiness: P3's frozen §5.7 enum

namespace rb_blitz::settings::ultimate_adapter {

// ------------------------------------------------------------------------------------------------
// 1. The records the integration needs, and why each one is needed
// ------------------------------------------------------------------------------------------------
// A named list rather than "does the archive have anything in it": the point of the inventory is
// that a *partial* payload (an older release without the input viewer, a hand-trimmed archive, a
// half-copied install) is reported as missing exactly what is missing, and the integration then
// refuses rather than calling objects that are not there.

enum class Record : uint8_t {
  kModSettings = 0,  // the panels: which rows exist, their kind, their variable
  kModSettingsRw,    // the persisted variable list and the save/load statements
  kUltiMacros,       // the row-kind macros and the settings-file path
  kUltiFuncs,        // row formatting/selection and the second writer
  kUltiInit,         // the compiled defaults
  kListResource,     // the mod's own list presentation for the pause scene
};

inline constexpr size_t kRecordCount = 6;

struct RecordSpec {
  Record id;
  std::string_view path;   // ark-relative, as the payload's index spells it
  std::string_view kind;   // "dtb" | "milo_xbox"
  std::string_view why;    // what stops working without it
};

const RecordSpec& RecordAt(size_t index);
std::string_view RecordName(Record record);

// ------------------------------------------------------------------------------------------------
// 2. The ark index
// ------------------------------------------------------------------------------------------------
// An independent reader for the payload's own container format (Harmonix ARK v6, the same one
// `scripts/hmx_ark.py` reads: a Park-Miller LCG stream cipher over the header, then a versioned
// index). Only what this module needs is implemented - the index, never the entry bodies beyond
// the one slice a record occupies - and nothing here is a port of the Python tool.

struct ArkEntry {
  std::string path;     // "ulti/gen/mod_settings.dtb", as stored (folder/name)
  uint64_t offset = 0;  // into the ark part the index names
  uint64_t size = 0;    // stored size
  uint64_t inflate = 0; // decompressed size, 0 when stored as-is
  bool compressed() const { return inflate != 0 && inflate != size; }
};

struct ArkIndex {
  uint32_t version = 0;
  std::vector<uint8_t> extra;
  std::vector<uint32_t> part_sizes;
  std::vector<uint32_t> checksums;
  std::vector<std::string> parts;  // the ark part names the header lists
  std::vector<ArkEntry> entries;

  const ArkEntry* Find(std::string_view path) const;
};

// Parses a decrypted-or-encrypted `.hdr`. False + `error` (never a partial index) when the
// version is not implemented, the header is truncated, or a count is absurd for the file size.
bool ReadArkIndex(const uint8_t* bytes, size_t size, ArkIndex* out, std::string* error);

// The whole file, or false + `error`.
bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>* out, std::string* error);

// ------------------------------------------------------------------------------------------------
// 3. Installation and shape
// ------------------------------------------------------------------------------------------------

struct Installation {
  bool header = false;    // gen/patch_xbox.hdr
  bool archive = false;   // gen/patch_xbox_0.ark
  bool index_read = false;
  uint32_t ark_version = 0;
  std::vector<Record> present;
  std::vector<Record> missing;  // records the index does not carry, or a malformed entry
  std::string error;

  // A payload the title will mount as one unit. Note what this does *not* claim: that the
  // records inside are shaped the way this integration needs - that is `CheckShape`.
  bool Mountable() const { return header && archive && index_read; }
};

Installation InspectInstallation(const std::filesystem::path& payload_root, std::string* error);

// The provider's own definition bytes, read out of the payload. Kept as bytes: the compiled-DTA
// cipher is the parser's business (ui::dtb::Parse), not this module's.
struct RecordBytes {
  std::vector<uint8_t> mod_settings;
  std::vector<uint8_t> mod_settings_rw;
  std::vector<uint8_t> ulti_macros;
  std::vector<uint8_t> ulti_funcs;
  std::vector<uint8_t> ulti_init;
};

bool ReadProviderRecords(const std::filesystem::path& payload_root, RecordBytes* out,
                         std::string* error);

// The row kinds, named the way the provider's own macros name them. `kUnknown` is a row the
// integration must not draw: an unrecognized kind is what an unrecognized payload looks like.
enum class RowKind : uint8_t {
  kSubmenu = 0,  // uNormal  - enters another screen
  kBool,         // uOnOFF   - a boolean the row toggles in place
  kSelect,       // uFakeSld - a horizontal selector over an inline array
  kSlider,       // uSld     - a numeric slider with a step and a range
  kUnknown,
};

const char* RowKindName(RowKind kind);
std::string_view RowKindMacro(RowKind kind);

struct RowShape {
  std::string symbol;      // "os_precision"
  RowKind kind = RowKind::kUnknown;
  std::string macro;       // "uOnOFF" - kept verbatim so an unknown kind is reportable
  std::string variable;    // "freq_ms", empty for a row that owns no variable
  bool reboot_tagged = false;  // the provider marks this row as needing a restart
  std::string target_screen;   // a submenu's `sel_script` destination, when it has one
  std::vector<std::string> choices;  // an inline `(array ...)`'s symbols, for a kSelect
};

struct PanelShape {
  std::string name;   // "mod_game_settings_panel"
  std::string title;  // "mod_game" - a locale key
  bool draws_through_retail_list = false;  // the panel carries SETTINGS_LIST_FILE
  std::vector<RowShape> rows;
  // Two of the provider's panels (`mod_force_plot_panel`, `mod_force_skybox_panel`) declare
  // their choices instead of their rows: `(data (off plot_bodega_a ...))` is the list itself, and
  // the screen's own SELECT_MSG stores the selected *position*. So the choice set is the panel's,
  // not the row's - and it is the payload's inventory, never ours (plan §3.4).
  std::vector<std::string> flat_choices;
};

struct ShapeReport {
  bool ok = false;
  std::vector<std::string> problems;  // why this payload is not the shape we integrate with
  std::vector<std::string> notes;     // real observations that are not failures

  std::vector<PanelShape> panels;
  std::vector<std::string> persisted;  // mod_settings_rw's `vars`, in the provider's order
  std::vector<std::string> macros;     // ulti_macros' own #define names
  std::map<std::string, std::string> defaults;  // ulti_init's `{set $x V}` defaults
  std::string settings_path_macro;              // "GAME:/ulti_settings.dta"

  // A choice list a panel declared for a row whose choices live in the payload rather than in
  // our schema (`(data (off plot_... ))` for Force Plot, `(array (default freq ...))` for Gems).
  std::map<std::string, std::vector<std::string>> choices;  // variable -> tokens

  const PanelShape* Panel(std::string_view name) const;
  const RowShape* Row(std::string_view symbol) const;
  bool HasProblem() const { return !problems.empty(); }
};

ShapeReport CheckShape(const RecordBytes& records);

// §5.7 readiness, from the three facts the plan's §3 requires and nothing else: is a payload
// present, is the *active target* the one that carries it, and did the shape check pass.
ProviderReadiness Readiness(bool payload_present, bool active_target, const ShapeReport& shape);

// ------------------------------------------------------------------------------------------------
// 4. The rows: P5b's frozen identity, bound to the provider's own vocabulary
// ------------------------------------------------------------------------------------------------
// §5.5: `kHot` requires an observed live effect in a boot; nothing is measured for a provider row
// today, so every one of them is `kRestart` and the table says so with an empty evidence string.
// The direction of travel is hot-first, and moving a row means measuring it, not asserting it.

enum class Strategy : uint8_t { kHot, kRestart };

struct RowBinding {
  SettingId id = SettingId::kUnknown;
  std::string_view token;     // "gameplay.precision_mode" - the screen's stable token
  std::string_view variable;  // "freq_ms" - the provider's own variable name
  RowKind kind = RowKind::kUnknown;
  Strategy strategy = Strategy::kRestart;
  std::string_view evidence;  // what was observed to justify kHot; empty = nothing yet
  // The payload's **boolean-with-submenu** pair (`os_force_plot` -> `mod_force_plot_panel`): the
  // row itself is a toggle, its choices live in a panel of their own, and that panel's SELECT_MSG
  // stores the selected **position** (`{set $which_skybox {$component selected_pos}}`). This build
  // exposes the whole choice set as one select over the provider's own tokens (plan §3.4), so the
  // codec kind is `kSelect` while the payload's row stays a boolean - and the provider's *file*
  // carries the position, which is what this flag says. The token/position translation lives in
  // `ConfigureProvider` (read) and the provider sink's `Save` (write), both against the payload's
  // own inventory, never a list of ours.
  bool position_value = false;
};

const std::vector<RowBinding>& RowBindings();
const RowBinding* BindingForToken(std::string_view token);
const RowBinding* BindingForVariable(std::string_view variable);

// Provider rows whose value is a **persisted pair in the provider's file**. `unlock_all` is
// deliberately absent: it is the documented exception, persisted under native
// `[ultimate_overrides]` because the provider's saved-vars list does not carry it.
const std::vector<RowBinding>& FileBackedBindings();

// A provider row whose *default* is still the provider's even though its value is not persisted
// in the provider's file (plan §6.2's `unlockall` exception). Read by `DefaultsFromShape`.
struct NativeDefault {
  SettingId id = SettingId::kUnknown;
  std::string_view variable;
  RowKind kind = RowKind::kUnknown;
};

const std::vector<NativeDefault>& NativeDefaultBindings();

// One immediate action rather than a stored value (the achievements grant).
inline constexpr SettingId kAchievementAction = SettingId::kUnlockOnlineAchievements;
inline constexpr char kAchievementVariable[] = "os_online_acc";

// ------------------------------------------------------------------------------------------------
// 5. The provider's own settings file
// ------------------------------------------------------------------------------------------------
// The mod writes `GAME:/ulti_settings.dta` - `MOD_SETTINGS_PATH` in its own macros - and it is the
// only mutable file the payload has. The game mount is read-only by construction (§6.2: do not
// make the whole game mount writable), so the runtime redirects exactly this record to user
// storage. These are the names that redirect is spelled with.

inline constexpr char kSettingsFileName[] = "ulti_settings.dta";
inline constexpr char kUserSubdirectory[] = "ultimate";
// The guest path the provider's save statement names, and the overlay-relative record our
// redirect rule matches (`<payload-or-game-root>/ulti_settings.dta`).
inline constexpr char kProviderGuestPath[] = "GAME:/ulti_settings.dta";
inline constexpr char kProviderRecord[] = "ulti_settings.dta";
// The provider's legacy file, read once as an import source and never written.
inline constexpr char kLegacySettingsRecord[] = "ulti_settings.ini";

// `<user_data_root>/ultimate`, and the file inside it. Empty root -> empty path.
std::filesystem::path ProviderSettingsDirectory(const std::filesystem::path& user_data_root);
std::filesystem::path ProviderSettingsPath(const std::filesystem::path& user_data_root);

// The provider's file, as its own writer produces it: an outer array whose first element is a
// version marker and whose remaining elements are `(name value)` pairs.
//
// Parsing keeps the file's **own order and its unknown elements**: the provider persists values
// this build does not draw (`server_url`, `server_ssl`, `p9dlc`, `controller_mapping`), and
// rewriting the file from our own subset would silently destroy them. So every element is kept
// beside the pairs this build interprets - and an element whose value serialized as something
// other than a single atom is preserved without ever being read as a value. An element is
// re-emitted from its own text, so an atom, a symbol and a quoted string come back byte for byte
// (a nested array comes back with normalized whitespace).
struct ProviderFile {
  bool parsed = false;
  std::string version_marker;                 // the leading element's text ("0")
  std::vector<std::string> names;             // pair names, in file order
  std::map<std::string, std::string> values;  // name -> the value's own text, verbatim
  std::vector<std::string> elements;          // every element, verbatim, in file order
  std::vector<std::string> element_names;     // the pair name each element is, or empty
  std::string error;
};

bool ParseProviderFile(std::string_view text, ProviderFile* out, std::string* error);

// The whole file again, with `updates` (variable -> verbatim value text) applied to the pairs
// they name. Every other pair, and the order, is preserved byte for byte.
std::string WriteProviderFile(const ProviderFile& file,
                              const std::map<std::string, std::string>& updates);

// The provider's current values for the rows this build draws. A pair the file does not carry,
// and a value this build cannot parse, are both simply absent: the caller falls back to the
// provider's own defaults rather than inventing one.
std::map<SettingId, Value> ValuesFromProviderFile(const ProviderFile& file);

// The provider's compiled defaults (`ulti_init`), for the rows the file does not carry.
std::map<SettingId, Value> DefaultsFromShape(const ShapeReport& shape);

// The verbatim file text for one of our rows, or false when the value does not belong to it.
bool EncodeProviderValue(const RowBinding& binding, const Value& value, std::string* out);

// The provider's own vocabulary, applied to our rows: `values` is keyed by our SettingIds and the
// result names the provider's variables and spells each value the way the provider's own writer
// does. This is the function a commit path should use, because the alternative - building the pair
// names by hand - is how a file ends up carrying a pair the provider does not own. A row whose
// value does not belong to it is refused, and `error` names it; the other rows are still encoded.
bool EncodeProviderValues(const std::map<SettingId, Value>& values,
                          std::map<std::string, std::string>* updates, std::string* error);

// The value a provider variable's text means, for the rows this build draws. False when the
// variable is not ours or the text does not parse as that row's kind.
bool DecodeProviderValue(const RowBinding& binding, std::string_view text, Value* out);

// True when the payload's own macro names the settings file this build redirects - which is a
// *separate* question from whether the whole payload is one we integrate with. It is what makes
// the redirect safe to apply on an unrecognized payload: the provider's own save starts working
// again, and the merged screen simply declines to draw provider rows.
bool PayloadWritesSettingsFile(const ShapeReport& shape);

// ------------------------------------------------------------------------------------------------
// 6. The boot's provider state
// ------------------------------------------------------------------------------------------------
// Everything above, gathered once. `ConfigureProvider` runs on the host thread at
// OnPostLoadXexImage - nothing here touches the cvar registry, a window or a guest - and the guest
// hooks only read the result.

struct ProviderRuntime {
  bool considered = false;         // ConfigureProvider ran at all
  bool payload_present = false;    // an inspectable payload is on disk
  bool active_target = false;      // this boot is the one that carries the payload
  Installation installation;
  ShapeReport shape;
  std::map<SettingId, Value> values;  // the provider file, over the payload's own defaults
  std::map<std::string, std::vector<std::string>> choices;
  ProviderReadiness readiness = ProviderReadiness::kAbsent;
  std::filesystem::path user_root;      // <user_data_root>/ultimate
  std::filesystem::path settings_path;  // <user_root>/ulti_settings.dta
  std::string summary;                  // one line for the boot log

  bool Active() const { return readiness == ProviderReadiness::kCompatible; }
  // The provider's current value for a row, or null when the payload does not carry one.
  const Value* ValueFor(SettingId id) const {
    const auto found = values.find(id);
    return found == values.end() ? nullptr : &found->second;
  }
  const std::vector<std::string>* ChoicesFor(std::string_view variable) const {
    const auto found = choices.find(std::string(variable));
    return found == choices.end() ? nullptr : &found->second;
  }
};

// Reads the installation, checks the shape, reads the player's provider file over the payload's
// compiled defaults, and computes readiness. `active_target` is the caller's answer to "is this
// the boot that carries the payload"; it is passed in rather than derived, because that is the
// game's own decision (plan §4.2) and the adapter must not hold a second opinion about it.
//
// Never throws and never partial: a missing payload, an unreadable archive or an unparseable
// definition all end in a runtime whose `readiness` says so.
void ConfigureProvider(ProviderRuntime* runtime, const std::filesystem::path& payload_root,
                       const std::filesystem::path& user_data_root, bool active_target);

// Reads the player's provider file (if any), applies `updates` (variable -> verbatim value text),
// and replaces the file with a flushed temporary sibling - the same durability the native store
// asks of itself, because the provider's own loader is what reads it next boot. False + `error`
// on any failure, with the previous file left as it was. No file and no updates writes nothing and
// reports success; the directory is created when it is missing.
bool SaveProviderFile(const std::filesystem::path& path,
                      const std::map<std::string, std::string>& updates, std::string* error);

}  // namespace rb_blitz::settings::ultimate_adapter
