// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 — the frozen, SDK-free settings model (docs/settings/contracts.md §5).
//
// This header materializes the G0 contract verbatim: the stable identifiers, the typed
// value, the descriptor, the requested/effective/pending field state, the capability
// snapshot, and the commit/restart results. P1/P5/P6 consume these names; a signature
// change here is a coordinator-reviewed contract change, not a worker decision.
//
// SDK-free on purpose, for the same reason controller-settings/profile.h is: the game and the
// Controller Settings both compile it and tests/settings_*_tests.cpp exercise it without a boot.
// The one cross-namespace dependency is rb_blitz::profile::LaunchTarget — deliberately,
// so there is no second target enum and the stored spelling stays common/demo/ultimate.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "profile/profile.h"

namespace rb_blitz::settings {

// --- identifiers -----------------------------------------------------------

enum class Category : uint8_t { kGeneral = 0, kGraphics = 1, kInterface = 2, kGameplay = 3 };

// Stable on disk and on the bridge. Values are frozen; ids are never renumbered or reused.
// P3 addition to §5.1: no enum value is invented beyond the frozen list. Submenu container
// rows (Downloadable Content, Achievements & Unlocks, HUD, Hide Items, Plots & Skyboxes)
// carry no SettingId; the schema names them with a group string instead (§schema.h).
enum class SettingId : uint16_t {
  kUnknown = 0,
  // General
  kTarget = 1,
  kCacheDlcLibrary = 2,
  kDlcScanThreads = 3,
  kDlcMenuSongs = 4,
  kUnlockAll = 5,
  kUnlockOnlineAchievements = 6,  // 6 == action, never persisted
  kPlayCurrentSong = 7,           // the main menu's second entry (owner feature, 2026-10-14)
  // Graphics
  kResolution = 20,
  kVsync = 21,
  kRenderer = 22,
  kAntiAliasing = 23,
  kFiltering = 24,
  kDisplay = 25,  // windowed / borderless / full screen (owner decision, 2026-10-13)
  // Interface
  kLetterbox = 40,
  kHideOfflinePrompts = 41,
  kHideMainMenu = 42,
  kHiddenMainMenu = 43,
  kHideHud = 44,
  kGoldProgress = 45,
  kProgressBar = 46,
  kShowTimer = 47,
  kShowPb = 48,
  kShowSteak = 49,
  kInputViewer = 50,  // provider rows, original spelling
  // Gameplay (Ultimate provider)
  kPrecisionMode = 60,
  kDisableRandomWiggles = 61,
  kCameraAngle = 62,
  kGameSpeed = 63,
  kDisableDof = 64,
  kDisablePlots = 65,
  kForcePlot = 66,
  kForceSkybox = 67,
  kGems = 68,
};

enum class ValueKind : uint8_t { kNone = 0, kBool, kInt, kEnum, kString, kList, kResolution };

// A tagged value. kEnum/kString/kList carry tokens (never labels, never indexes).
struct Value {
  ValueKind kind = ValueKind::kNone;
  bool boolean = false;
  int32_t integer = 0;
  std::string text;
  std::vector<std::string> list;
  int32_t width = 0;
  int32_t height = 0;
};

// Auto is intent, not a render size. The boot reconciler resolves it using the current display.
inline bool IsAutoResolution(const Value& value) {
  return value.kind == ValueKind::kResolution && value.width == 0 && value.height == 0;
}

enum class WidgetKind : uint8_t {
  kBoolean,
  kHorizontalSelect,
  kSubmenu,
  kBooleanWithSubmenu,
  kAction,
};

enum class Provider : uint8_t { kNative = 0, kUltimate = 1 };

enum class Availability : uint8_t { kVisible, kVisibleDisabled, kHidden };

struct Choice {
  std::string token;  // stable; never localized
  std::string label;  // localized (English fallback here)
};

// §5.5: how a *committed* value reaches the running game. P9 replaces the v1 constant
// (`restart_scoped = true` for every row) with this per-row classification, so the UI, the
// acceptance matrix and the docs read one table instead of three prose claims.
//
//   kHot      the value can be applied to the running process with a **measured** live effect.
//   kRestart  it is persisted and takes effect in a new process. This is the default, and the
//             honest answer for a row nothing has been measured for.
//
// `kHot` is never a design wish: a row carries it only with `strategy_evidence` non-empty and
// naming what was observed (a log line, a capture, a game-state read). The direction of travel
// is hot-first, and moving a row means measuring it.
enum class ApplicationStrategy : uint8_t { kHot = 0, kRestart = 1 };

struct Descriptor {
  SettingId id = SettingId::kUnknown;
  Category category = Category::kGeneral;
  Provider provider = Provider::kNative;
  WidgetKind widget = WidgetKind::kBoolean;
  std::string label_key;  // project-owned localization key, English fallback
  std::string help_key;
  ValueKind value_kind = ValueKind::kBool;
  Value compiled_default;
  std::vector<Choice> choices;  // static choices; empty when choices_source is set
  std::string choices_source;   // "" | "renderers" | "resolutions" | "provider" | "menu_rows"
  // §5.5's per-row strategy, and the evidence that justifies it. `strategy_evidence` is what
  // was OBSERVED for a `kHot` row and is deliberately empty for a `kRestart` row: an empty
  // string is "nothing measured", which is not the same claim as "nothing can be measured".
  ApplicationStrategy strategy = ApplicationStrategy::kRestart;
  std::string strategy_evidence;
  // Convenience for the callers that ask the yes/no question. The source of truth is `strategy`.
  bool restart_scoped() const { return strategy == ApplicationStrategy::kRestart; }
  Availability availability = Availability::kVisible;
  std::string disabled_reason_key;                 // when kVisibleDisabled
  SettingId submenu_parent = SettingId::kUnknown;  // for kBooleanWithSubmenu -> the submenu row
};

// --- provenance and field state -------------------------------------------

enum class Source : uint8_t {
  kCompiledDefault = 0,      // lowest
  kRecommendedFirstRun = 1,  // automatic policy (marked automatic)
  kSettingsFile = 2,         // migrated/current settings.toml
  kDeveloperConfig = 3,      // rb_blitz.toml
  kEnvironment = 4,
  kCommandLine = 5,  // highest; a row overridden here is read-only in the UI
  kProvider = 6,     // Ultimate-owned row (its own file/defaults)
};

enum class Origin : uint8_t { kDefault = 0, kAutomatic = 1, kManual = 2 };  // persisted provenance

struct FieldState {
  SettingId id = SettingId::kUnknown;
  Value requested;  // last desired value (persisted intent)
  Value effective;  // what boot actually applied / will apply
  Value pending;    // edited this session, not yet committed
  Source effective_source = Source::kCompiledDefault;
  Origin origin = Origin::kDefault;
  bool overridden = false;  // explicit CLI/env/devconfig won -> edit disabled, explain
  bool restart_required = false;
  bool corrected = false;  // effective != requested because of reconciliation
  std::string correction_reason_key;
};

// --- capabilities ----------------------------------------------------------

struct DisplayMode {
  int32_t width = 0;
  int32_t height = 0;
};  // physical pixels, deduped

struct RendererCapability {
  std::string token;  // "d3d12" | "vulkan" (never an index)
  bool built = false;
  bool detected = false;
  bool usable = false;
  bool active = false;
  std::string native_aa_label;    // verified label, not the device maximum
  int32_t filtering_ceiling = 0;  // highest eligible AF value this device supports
};

struct ProviderState {
  bool installed = false;  // payload present
  bool compatible = false; // shape/fingerprint checks passed
  bool active = false;     // provider active in *this* boot
  std::string version;     // e.g. "2.11"; informational
};

struct CapabilitySnapshot {
  uint64_t revision = 1;  // capability revision
  uint32_t logical_processors = 1;  // usable logical processors (ceiling for threads)
  std::string primary_display_id;
  DisplayMode primary_display_size;
  std::vector<DisplayMode> display_modes;           // truthful, deduped; may be empty
  std::vector<RendererCapability> renderers;        // built/detected/usable/active
  std::vector<DisplayMode> supported_resolutions;   // exact render dims the render path supports
  // Populated only by a measured performance profile for this device/backend.
  // Display size or CPU count alone is never performance evidence.
  DisplayMode verified_sustainable_resolution;
  rb_blitz::profile::LaunchTarget active_target = rb_blitz::profile::LaunchTarget::kCommon;
  ProviderState provider;
};

inline constexpr int kSnapshotSchemaVersion = 1;  // bridge payload version, independent of files

struct Snapshot {
  int32_t schema_version = kSnapshotSchemaVersion;
  uint64_t revision = 0;  // increments only on a durable commit
  CapabilitySnapshot capabilities;
  std::vector<Descriptor> descriptors;
  std::vector<FieldState> fields;
};

// --- commit / restart ------------------------------------------------------

enum class CommitStatus : uint8_t {
  kSaved = 0,
  kNoChanges,
  kConflict,
  kInvalid,
  kIoError,
  kPartial,
};

struct CommitResult {
  CommitStatus status = CommitStatus::kNoChanges;
  uint64_t saved_revision = 0;
  bool restart_required = false;
  bool provider_written = false;  // the Ultimate file portion is durable
  std::vector<SettingId> conflicts;
  std::vector<std::string> errors;
};

struct ValidationResult {
  bool ok = true;
  std::string reason_key;
};

enum class RestartStatus : uint8_t {
  kOk = 0,
  kNotSaved,
  kStaleRevision,
  kChildFailed,
  kAlreadyRequested,
};

struct RestartResult {
  RestartStatus status = RestartStatus::kOk;
  std::string error;
};

struct ActionResult {
  bool ok = false;
  std::string error;
};

// --- free helpers shared by every consumer --------------------------------

// The stable token for an id, independent of the file's section layout ("target",
// "resolution", ...). Never localized; safe on the bridge and in diagnostics.
std::string_view SettingIdToken(SettingId id);

// The stable section+key the id is persisted under in settings.toml, or an empty pair when
// the row is not a settings.toml preference (provider-owned rows and actions).
struct StorageKey {
  std::string_view section;
  std::string_view key;
  bool persisted() const { return !section.empty() && !key.empty(); }
};
StorageKey StorageKeyFor(SettingId id);  // defined in schema.cpp

// A tagged value compared field by field (kind plus the field that kind uses).
bool ValueEquals(const Value& lhs, const Value& rhs);

// A canonical single-line rendering used by diagnostics and by the bridge's structured
// error text. Never the storage spelling; the store renders TOML itself.
std::string ValueToString(const Value& value);

// True when the id is an immediate action rather than a persisted row.
bool IsAction(SettingId id);

// "true"/"false" for a bool kind; the integer in decimal; tokens verbatim for the rest.
std::string_view CategoryToken(Category category);
std::string_view ProviderToken(Provider provider);
std::string_view WidgetToken(WidgetKind widget);
std::string_view ApplicationStrategyToken(ApplicationStrategy strategy);
std::string_view SourceToken(Source source);
std::string_view OriginToken(Origin origin);
std::string_view CommitStatusToken(CommitStatus status);

}  // namespace rb_blitz::settings
