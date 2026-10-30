# In-game settings — frozen contracts and integration record (P0)

Status: **P0 complete** (contracts frozen). No feature implementation, no runtime validation, no
production cutover in this pass. The native screen/ABI is **not** frozen here — that is P1/G1.

Read with: [architecture.md](architecture.md) (how the frozen shapes are actually built),
[menu.md](menu.md) (the surfaces), [graphics.md](graphics.md) (resolution and graphics), and
[status.md](status.md) (what is implemented and what is left). See [README.md](README.md) for the
whole set and the reading order.

This document is the G0 artifact required by `docs/settings/plan.md` §10.1/§11 (P0).
It records the measured baseline, the controller-UI inventory, the re-checked source map, the
frozen SDK-free interfaces, the file-ownership and provenance rules, the reserved shared files and
work directories, the development gate, and the hand-off readiness of P1/P2/P3.

Read order for an execution agent: the parent plan first (requirements, gates, ownership), then
this file for the frozen names. Where the two disagree, **this file wins for names and interfaces**
and the parent plan wins for product behavior. `docs/plans/finished/2026-09-27-launcher-plan.md` is superseded where it
disagrees about settings ownership, Controller Settings scope, resolution, or installer defaults (§1 of the
parent plan).

Evidence labels: **measured** = executed/observed in this pass; **source** = read from the
repository; **frozen** = a decision made here, binding on P1–P12.

---

## 1. Baseline record (measured, 2026-10-10)

| Item | Value |
| --- | --- |
| Commit | `1e40765` ("Enhance menu music functionality with improved song pool management") |
| Branch | `main` (`origin/main` at the same commit) |
| Worktree | **clean** — `git status --porcelain` returns nothing |
| Staged/unstaged work | **none** — the song-cache work the parent plan describes as "staged" is now committed (see `fd4b436`, `3f99544`, `7219e8b`, `1e40765`) |
| Stashes | none |
| `controller-settings/out/generated/settings_table.h` | absent — the application that generated it retired on 2026-10-19 (P8b); this row is the 2026-10-10 baseline record |

`git log --oneline -5`:

```text
1e40765 Enhance menu music functionality with improved song pool management
7219e8b Enhance DLC and song acceptance scripts with extra runtime arguments and frame pacing
3f99544 Implement in-game refresh for DLC libraries
fd4b436 Refactor DLC song handling in main menu and document shader cache findings
efaa62d Implement local save functionality for offline progress persistence
```

**The parent plan's baseline sentence ("`fd4b436`, plus the staged song-cache work already in this
checkout") is now stale.** The baseline for every work package is `1e40765` with a clean tree. P0
did not reset, stage, commit, or otherwise touch the checkout; the only change made in this pass is
this document.

### 1.1 Build and test commands (re-checked, measured)

Toolchain bootstrap is one-time per checkout (`scripts/setup/repair_flat_symlinks.ps1`, then
`scripts/setup/apply_sdk_patches.ps1` — 18 patches; it exits 1 on any **unexpected** edit inside
`rexglue-sdk/`, including untracked directories).

```powershell
# Repository root
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
ctest --test-dir out/build/win-amd64-release --output-on-failure
ctest --test-dir out/build/win-amd64-release -N     # lists the registered names

# From installer/
cmake --preset installer-release
cmake --build --preset installer-release
ctest --preset installer-release
```

`out/build/win-amd64-release` **is** configured in this checkout and `ctest -N` reports **22**
tests (measured, listed in §1.2). `out/build/*` bake in an absolute `CMAKE_HOME_DIRECTORY`; if this
checkout is moved or copied, delete and reconfigure rather than building into the stale tree.

### 1.2 Registered tests (measured — `ctest -N`, 22 total)

`local_save`, `crypto_keytable`, `payload_overlay`, `path_policy`, `fingerprint`, `ui_nav`,
`probe_trace`, `dlc_layout`, `dlc_library`, `dlc_cache`, `refresh_state`, `menu_music_pool`,
`menu_options`, `ultimate_plan`, `toolchain`, `controller_settings_profile`, `controller_settings_general`,
`controller_settings_session`, `controller_settings_remap`, `controller_settings_update`, `controller_settings_launch`, `controller_settings_nav`.

**Renamed/removed since this baseline:** the two suites that test game code kept their checks as
`profile` and `profile_remap`; `controller_settings_general`, `controller_settings_session`,
`controller_settings_launch` and `controller_settings_nav` went with the application on 2026-10-19
(P8b), as did the already-retired `controller_settings_update`.

The installer helper's `installer` test lives in the separate `installer/` preset tree and is **not**
part of the 22. Registering a new test is a change to a **shared file** (§9) and is the
coordinator's edit, not a worker's.

---

## 2. Controller-UI inventory outside `settings.toml` (measured)

> **Superseded 2026-10-19 (P8b).** This inventory described the external settings application and
> its shared modules. The application has been removed; the live inventory is the in-game Help &
> Options tree in [`settings_screen.cpp`](../../src/ui/settings_screen.cpp) and the gameplay-mapping
> plan's §17.1 row-by-row table. The section is kept as the 2026-10-10 measurement, including the
> Guide prohibition that still holds for the game's own input path.

P8 must keep the *complete* non-Guide controller functionality, so the inventory below is the
authoritative list of what "Controller Settings" owns. It is deliberately wider than the schema:
most of the controller surface is **bespoke UI**, not schema rows.

### 2.1 Schema rows (`controller-settings/config/settings.toml`)

| Key | Tab/group | Disposition (parent plan §3.5/§9.3) |
| --- | --- | --- |
| `mouse_ui_nav` | controller / Input | **Keep** (controller-owned) |
| `guide_button` | controller / Input | **Remove** row; Guide prohibited project-wide |

Controller Settings tab order is `general`, `interface`, `graphics`, `controller`; the graphics tab is
labelled "Audio / Video" and also holds the audio rows. There is deliberately no
input-form/`input_backend` row (source comment in `settings.toml`; the cvar is hand-edit only).

### 2.2 Bespoke controller UI (not schema rows) — P8 owns all of it

| Module | What it is |
| --- | --- |
| `controller-settings/src/controller_tab.{h,cpp}` | The button-mapping block: **17 controls** (`kControlRows = 17`), one *Assign* + one *Reset* per control, then *Reset all* (`kRowCount = 17*2+1 = 63`). 3 s listening window, multi-source bindings ("several inputs, one button"), rising-edge capture over keyboard/pad/mouse. Guide is a row **and** an SDL capture case (`SDL_GAMEPAD_BUTTON_GUIDE -> remap::Target::kGuide`). |
| `src/profile/remap.{h,cpp}` | The shared vocabulary Controller Settings writes and the game reads: `Target` (17 values incl. `kGuide`), `Source` (`pad:` / `key:` / `mouse:`), `Table`, `DefaultSources`, `Apply`. SDK-free and SDL-free. |
| `controller-settings/src/pad_nav.{h,cpp}` | Pad-driven focus navigation for Controller Settings window. |
| `controller-settings/src/pad_source.{h,cpp}` | `PadRegistry` — Controller Settings' opened pads (A3); the focus ring and the capture both read it. |
| `controller-settings/src/virtual_pad.{h,cpp}` | Virtual/SDL pad synthesis used by Controller Settings UI. |
| `controller-settings/src/nav.{h,cpp}`, `nav_bindings.{h,cpp}`, `nav_keys.{h,cpp}` | Controller Settings navigation rebinding (`[nav]` table), key capture, safe-mode recovery. |
| `src/input/remap.cpp` | Game side: installs the `[remap]` state filter via `InputSystem::SetStateFilter` (SDK patch `0010`), reading the *same* shared table. |
| `src/input/mouse_ui.{h,cpp}`, `ui_nav.h`, `nav_detect.h` | Mouse-as-pad navigation in the game; the cursor rule in `OnCreateDialogs`. |
| `controller-settings/src/profile_session.{h,cpp}`, `settings_edit.{h,cpp}`, `row_ui.{h,cpp}`, `focus_*` | The editing/write plumbing the reduced screen must reuse. |
| `controller-settings/README.md` §"Controller" | Documents the 17-target grammar incl. `guide`. Must be updated, not treated as code. |

### 2.3 Guide — every place it must be excluded (measured)

Guide appears in the project in exactly these places (project sources only; the SDK's own
third-party SDL and `xam_ui` stubs are out of scope and not touched):

| Location | Role |
| --- | --- |
| `controller-settings/config/settings.toml` (`guide_button`) | schema row (remove) |
| `src/profile/remap.cpp:38` | `{Target::kGuide, "guide", "Guide", 0x0400}` — vocabulary + guest bit `0x0400` |
| `src/profile/remap.h` | `Target::kGuide` enumerator; comments that Guide has no default |
| `controller-settings/src/controller_tab.cpp:44` | SDL capture maps the Guide button to `Target::kGuide` |
| `rexglue-sdk/src/input/input_system.cpp:30` | `guide_button` cvar (SDK-owned; exposed by our schema) |
| `rexglue-sdk/src/input/sdl/sdl_input_driver.cpp:580,660` | the cvar's consumers (mask/OR the guide bit into the guest state) |
| `rexglue-sdk/src/input/mnk/mnk_input_driver.cpp:59,298` | `keybind_guide` cvar (keyboard → guide bit) |
| `rexglue-sdk/src/input/xinput/xinput_input_driver.cpp:207` | uses `XInputGetStateEx` when the cvar is on |

Consequence frozen for P3/P8: the allowlist (§8) **excludes** `guide_button` and `keybind_guide`;
the migration drops legacy `guide` remap rows to inert preservation data with a diagnostic; and
**every** input path (profile present, absent, malformed, explicit legacy cvar) must clear
`X_INPUT_GAMEPAD_GUIDE` (`0x0400`) from the final guest pad state. Do not touch the OS's own Guide
behaviour or the SDK's unrelated triggers.

---

## 3. Source-map re-check (measured)

Every path in the parent plan §2.1 evidence map was checked on `1e40765`. **All exist**; the map is
accurate. Two additions worth recording because they are the real seams the contracts below use:

| Area | Verified path | Note |
| --- | --- | --- |
| Profile apply (game reads Controller Settings profile) | `src/profile/profile_apply.{h,cpp}` | Applies **all** `[settings]` rows at config rank in `OnConfigurePaths`; this is the function §8 narrows to the allowlist. |
| Profile location | `src/profile/profile_path.{h,cpp}` | `ResolveProfilePath` order: `--launcher_profile` → `RBBLITZ_CONTROLLER_SETTINGS_PROFILE` → `<settings_dir>/launcher.toml` (pointer `settings_dir.txt`) → portable marker → `%APPDATA%\rb_blitz\launcher.toml`. Frozen as the base for the native-settings sibling mapping (§5). |
| Path policy | `src/fs/path_policy.h` | `IsSameOrInside` (weakly-canonical walk; deliberately not `std::filesystem::relative`). Shared by installer and runtime validation. |
| DLC layout policy | `src/fs/dlc_layout.h`, `src/fs/dlc_library.h`, `src/fs/dlc_cache.h` | Installer validation reuses these. |
| App lifecycle | `src/rb_blitz_app.h` | `OnPreSetup`, `OnConfigurePaths`, `OnPostLoadXexImage`, `OnPreLaunchModule`, `OnPostInitLogging`, `OnCreateDialogs`. No `OnFinalizePaths`/`OnShutdown` override today. |
| SDK lifecycle | `rexglue-sdk/src/ui/rex_app.cpp` | `SetupEnvironment` (147 calls `OnConfigurePaths`) → `SetupPresentation` (314 calls `OnPreSetup`) → 92 `OnFinalizePaths` → 99 `ConstructRuntime` → 294 `OnPostLoadXexImage` → 450 `OnPreLaunchModule`; 511 `std::_Exit(0)`. Matches the parent plan §4.2. |
| SDK patches | `patches/rexglue-sdk/0001..0017` | `0010` = input state filter, `0013` = cvar config source, `0017` = the fixed-size game window. No `guide` patch exists; the cvar is SDK-source. |

`generated/default/*.cpp` are recompilation output; `generated/default/codegen.partition.json`
recovers a partition number. **Never edit generated output as the implementation** (parent plan
§2.3).

---

## 4. File ownership (frozen)

Placement and vocabulary are frozen here so no package invents a second format.

| File | Location | Writer / reader | Frozen vocabulary |
| --- | --- | --- | --- |
| `paths.toml` | Beside the installed `rb_blitz.exe` (per installation) | Installer writes initial/reinstall; user may hand-edit; game reads **before** configuring paths. No settings UI. | `schema_version = 1`; `[paths] dlc_root`, `[paths] user_data_root` as **TOML literal strings**. Empty = the platform/DLC default. Relative values resolve against the **directory containing `paths.toml`**, never the CWD. |
| `settings.toml` | The resolved settings directory (normally `%APPDATA%\rb_blitz`) | Native settings service (game). | §5.4. Holds preferences, provenance, migration version, and `[ultimate_overrides]`. |
| `launcher.toml` | Same resolved settings directory (existing filename retained) | Controller Settings writes only its owned fields; runtime reads the controller allowlist (§8). | `[remap]`, `[nav]`, `[update]`, window geometry, and controller cvars. Superseded `[settings]` game rows become **inert preservation data**. |
| `rb_blitz.toml` | Beside the executable | Developer/manual only; no user UI writes. | Unchanged. Ranks above `settings.toml`, below environment/CLI. |
| `ulti_settings.dta` | `<user_data_root>/ultimate/ulti_settings.dta` (proposed) | Ultimate's own serializer/loader through an exact-path host redirect. | The provider's own format; we never rewrite it from a subset. |

### 4.1 Native-settings sibling mapping (frozen, tested once)

Derived from `ResolveProfilePath` — the game must not start reading preferences from the save
folder when `user_data_root` changes.

| Resolved legacy profile | Native settings file |
| --- | --- |
| `<dir>/launcher.toml` (the ordinary case) | `<dir>/settings.toml` |
| `<dir>/<stem>.toml` (explicit custom profile) | `<dir>/<stem>.game-settings.toml` |

`schema_version` is 1 for both new files. A malformed or unknown-version file is **reported and left
intact**; it is never reinterpreted and never overwritten.

---

## 5. Frozen interfaces (SDK-free)

Namespace `rb_blitz::settings`. These declarations are the contract; **P3 materializes them
verbatim** in `src/settings/{model,schema,store,migration,paths,service}.h` (its owned files). P1/P5
consume them; a signature change is a coordinator-reviewed contract change (§10), not a worker
decision. The model uses `rb_blitz::profile::LaunchTarget` (`kCommon`/`kDemo`/`kUltimate`) directly
— **no second target enum** — so the stored spelling stays `common`/`demo`/`ultimate`
(`LaunchTargetName`/`ParseLaunchTarget`).

### 5.1 Values, identifiers, descriptors

**2026-10-22 extension (RhythmVerse P9-4):** append
`kShowMessagesRecommendations = 86` to the live enum in `src/settings/model.h`
(after P6's ids 80-85). Its stable token is `show_messages_recommendations`;
the native boolean persists under `[interface]`, defaults true, and requires
a restart. **Superseded 2026-10-23:** id 86 is retired and never reused. The split
preferences are `kShowMessages = 87` (`interface.show_messages`) and
`kShowRecommendations = 88` (`interface.show_recommendations`), both native
booleans, restart-scoped, with final defaults **Messages true / Recommendations
false** (owner update, 2026-10-23). A missing new key inherits the old
combined key's boolean; an explicit new key takes precedence. The fallback is
used for loading and conflict detection, and the old key remains untouched.
The snapshot below records the original frozen set.

```cpp
enum class Category : uint8_t { kGeneral = 0, kGraphics = 1, kInterface = 2, kGameplay = 3 };

// Stable on disk and on the bridge. Values are frozen; ids are never renumbered or reused.
enum class SettingId : uint16_t {
  kUnknown = 0,
  // General
  kTarget = 1, kCacheDlcLibrary = 2, kDlcScanThreads = 3, kDlcMenuSongs = 4,
  kUnlockAll = 5, kUnlockOnlineAchievements = 6,           // 6 == action, never persisted
  // Graphics
  kResolution = 20, kVsync = 21, kRenderer = 22, kAntiAliasing = 23, kFiltering = 24,
  // Interface
  kLetterbox = 40, kHideOfflinePrompts = 41, kHideMainMenu = 42, kHiddenMainMenu = 43,
  kHideHud = 44, kGoldProgress = 45, kProgressBar = 46, kShowTimer = 47,
  kShowPb = 48, kShowSteak = 49, kInputViewer = 50,           // provider rows, original spelling
  // Gameplay (Ultimate provider)
  kPrecisionMode = 60, kDisableRandomWiggles = 61, kCameraAngle = 62, kGameSpeed = 63,
  kDisableDof = 64, kDisablePlots = 65, kForcePlot = 66, kForceSkybox = 67, kGems = 68,
};

enum class ValueKind : uint8_t { kNone = 0, kBool, kInt, kEnum, kString, kList, kResolution };

// A tagged value. `kEnum`/`kString`/`kList` carry tokens (never labels, never indexes).
struct Value {
  ValueKind kind = ValueKind::kNone;
  bool boolean = false;
  int32_t integer = 0;
  std::string text;
  std::vector<std::string> list;
  int32_t width = 0;
  int32_t height = 0;
};

enum class WidgetKind : uint8_t { kBoolean, kHorizontalSelect, kSubmenu, kBooleanWithSubmenu, kAction };
enum class Provider   : uint8_t { kNative = 0, kUltimate = 1 };
enum class Availability : uint8_t { kVisible, kVisibleDisabled, kHidden };

struct Choice { std::string token; std::string label; };   // token is stable; label is localized

struct Descriptor {
  SettingId id = SettingId::kUnknown;
  Category category = Category::kGeneral;
  Provider provider = Provider::kNative;
  WidgetKind widget = WidgetKind::kBoolean;
  std::string label_key;          // project-owned localization key, English fallback
  std::string help_key;
  ValueKind value_kind = ValueKind::kBool;
  Value compiled_default;
  std::vector<Choice> choices;    // static choices; empty when choices_source is set
  std::string choices_source;     // "" | "renderers" | "resolutions" | "provider"
  bool restart_scoped = true;     // v1: true for every row (parent plan §5.4)
  Availability availability = Availability::kVisible;
  std::string disabled_reason_key;      // when kVisibleDisabled
  SettingId submenu_parent = SettingId::kUnknown;  // for kBooleanWithSubmenu -> the submenu row
};
```

### 5.2 Requested / effective / pending, and provenance

```cpp
enum class Source : uint8_t {
  kCompiledDefault = 0,     // lowest
  kRecommendedFirstRun = 1, // automatic policy (marked automatic)
  kSettingsFile = 2,        // migrated/current settings.toml
  kDeveloperConfig = 3,     // rb_blitz.toml
  kEnvironment = 4,
  kCommandLine = 5,         // highest; a row overridden here is read-only in the UI
  kProvider = 6,            // Ultimate-owned row (its own file/defaults)
};

enum class Origin : uint8_t { kDefault = 0, kAutomatic = 1, kManual = 2 };  // persisted provenance

struct FieldState {
  SettingId id = SettingId::kUnknown;
  Value requested;          // last desired value (persisted intent)
  Value effective;          // what boot actually applied / will apply
  Value pending;            // edited this session, not yet committed
  Source effective_source = Source::kCompiledDefault;
  Origin origin = Origin::kDefault;
  bool overridden = false;         // explicit CLI/env/devconfig won -> edit disabled, explain
  bool restart_required = false;
  bool corrected = false;          // effective != requested because of reconciliation
  std::string correction_reason_key;
};
```

**Non-negotiable:** `requested`, `effective` and `pending` are three separate values. A temporarily
disconnected monitor must not erase a valid manual 4K request; a CLI override shows the true
effective value; the UI disables editing an overridden row instead of saving a change that cannot
take effect.

### 5.3 Snapshot / CapabilitySnapshot

```cpp
struct DisplayMode { int32_t width = 0; int32_t height = 0; };  // physical pixels, deduped

struct RendererCapability {
  std::string token;               // "d3d12" | "vulkan" (never an index)
  bool built = false, detected = false, usable = false, active = false;
  std::string native_aa_label;     // verified label, not the device maximum
  int32_t filtering_ceiling = 0;   // highest eligible AF value this device supports
};

struct ProviderState {
  bool installed = false;          // payload present
  bool compatible = false;         // shape/fingerprint checks passed
  bool active = false;             // provider active in *this* boot
  std::string version;             // e.g. "2.11"; informational
};

struct CapabilitySnapshot {
  uint64_t revision = 1;                         // capability revision
  uint32_t logical_processors = 1;               // usable logical processors (ceiling for threads)
  std::string primary_display_id;
  DisplayMode primary_display_size;
  std::vector<DisplayMode> display_modes;        // truthful, deduped; may be empty
  std::vector<RendererCapability> renderers;     // built/detected/usable/active
  std::vector<DisplayMode> supported_resolutions;// exact render dims the render path supports
  rb_blitz::profile::LaunchTarget active_target = rb_blitz::profile::LaunchTarget::kCommon;
  ProviderState provider;
};

inline constexpr int kSnapshotSchemaVersion = 1;   // bridge payload version, independent of files

struct Snapshot {
  int32_t schema_version = kSnapshotSchemaVersion;
  uint64_t revision = 0;                 // increments only on a durable commit
  CapabilitySnapshot capabilities;
  std::vector<Descriptor> descriptors;
  std::vector<FieldState> fields;
};
```

A `Snapshot` is an **immutable by-value copy**. No host object pointer, `std::string_view` into host
storage, or reference is ever handed across the guest boundary. Opening the menu takes a fresh
snapshot; the bridge cannot mutate the service's state.

### 5.4 Native settings file (frozen shape)

```toml
schema_version = 1

[general]
target = 'common'            # common | demo | ultimate
cache_dlc_library = false
dlc_scan_threads = 1
dlc_menu_songs = false

[graphics]
resolution = [1920, 1080]    # exact render size, [w, h]
renderer = 'd3d12'           # stable token
anti_aliasing = 'native'     # none | native | fxaa | fxaa_extreme
filtering = 'native'         # native | none | 1x | 2x | 4x | 8x | 16x
vsync = true

[interface]
hide_offline_prompts = true
hide_main_menu = false
hidden_main_menu = ['splash_leaderboard', 'splash_achievements', 'splash_dlc']

[provenance]
'graphics.resolution' = 'manual'      # default | automatic | manual, per field
'graphics.renderer'   = 'automatic'

[automatic]
policy_version = 1

[ultimate_overrides]
# Only fields the provider does not persist (e.g. unlock_all). Absent when no override exists.
```

An illustrative example, **not** a set of defaults to seed at install time. The installer writes no
graphics file.

### 5.5 Path file

```cpp
struct PathEntry { std::string dlc_root; std::string user_data_root; };

struct PathFileResult {
  enum class Status : uint8_t { kOk, kMissing, kMalformed } status = Status::kMissing;
  int32_t schema_version = 1;
  PathEntry paths;
  std::string error;
};

std::filesystem::path ResolvePathFile(const std::filesystem::path& executable_dir); // <dir>/paths.toml
PathFileResult LoadPathFile(const std::filesystem::path& file);
// Transactional; committed only after successful finalization; preserves the file on failure.
bool SavePathFile(const std::filesystem::path& file, const PathEntry& entry, std::string* error);
```

### 5.6 Migration

```cpp
enum class MigrationStatus : uint8_t {
  kNoLegacy, kMigrated, kAlreadyCurrent, kInterruptedResumed, kMalformedLegacy,
};

struct MigrationStats {
  std::size_t imported = 0, translated = 0, dropped = 0, preserved = 0;
  bool target_imported = false, paths_imported = false;
};

struct MigrationResult {
  MigrationStatus status = MigrationStatus::kNoLegacy;
  MigrationStats stats;
  std::vector<std::string> diagnostics;   // includes the intentional native+FXAA loss
  std::filesystem::path backup;           // byte-identical legacy backup, before any write
  bool wrote_settings = false;            // partial-failure recovery needs both flags
  bool wrote_paths = false;
};

MigrationResult Migrate(const rb_blitz::profile::Profile& legacy,
                        Store& store, const PathEntry& existing_paths);
```

Migration is idempotent and staged. Frozen rules already written down that the interface assumes:
byte-identical backup before any write; existing valid new-format files win; import only missing
fields; path precedence `[settings] user_data_root/dlc_root` then legacy
`[launch] user_data_dir/dlc_dir`; `resolution_scale = n` → requested `[1280*n, 720*n]` (never
`2 → 1920x1080`, which was 2560x1440); AA/filtering mapping exactly as parent plan §4.3 item 6;
legacy `unlockall`/pool/Guide/monitor values do not become new active preferences.

### 5.7 Provider readiness and save completion

```cpp
enum class ProviderReadiness : uint8_t {
  kAbsent, kInactiveTarget, kCompatible, kUnrecognizedShape,
};

struct ProviderSaveResult {
  bool durable = false;      // host I/O completion confirmed against the real file
  std::string error;
};

// Save the provider's own file through the exact-path redirect; acknowledged only on durability.
ProviderSaveResult SaveProviderSettings(void* guest_thread_context);
```

A log line saying "saved" is **not** completion (parent plan §6.2). `kUnrecognizedShape` must not
crash and must keep the original mod path reachable.

### 5.8 Commit, transactions, restart

```cpp
enum class CommitStatus : uint8_t {
  kSaved = 0, kNoChanges, kConflict, kInvalid, kIoError, kPartial,
};

struct CommitResult {
  CommitStatus status = CommitStatus::kNoChanges;
  uint64_t saved_revision = 0;
  bool restart_required = false;
  bool provider_written = false;          // the Ultimate file portion is durable
  std::vector<SettingId> conflicts;
  std::vector<std::string> errors;
};

struct ValidationResult { bool ok = true; std::string reason_key; };

enum class RestartStatus : uint8_t {
  kOk = 0, kNotSaved, kStaleRevision, kChildFailed, kAlreadyRequested,
};

struct RestartResult { RestartStatus status = RestartStatus::kOk; std::string error; };

// The service (P3). Every method runs on the caller's thread; no guest pointers cross it.
class Service {
 public:
  Snapshot GetSnapshot() const;                                   // fresh by-value copy
  uint64_t BeginEdit(uint64_t base_revision);                     // 0 when stale -> re-snapshot
  ValidationResult Set(uint64_t txn, SettingId id, const Value& value, uint64_t capability_revision);
  Value CancelField(uint64_t txn, SettingId id);                  // restores the previous field value
  CommitResult Commit(uint64_t txn);
  void Cancel(uint64_t txn);
  ActionResult Invoke(SettingId action, uint64_t request_id);     // e.g. the achievements action
  RestartResult RequestRestart(uint64_t saved_revision, uint64_t request_id);
};

struct ActionResult { bool ok = false; std::string error; };
```

The lower-level bridge API in parent plan §5.3
(`GetSnapshot`/`BeginEdit`/`Set`/`CancelField`/`Commit`/`Cancel`/`Invoke`/`RequestRestart`) is the
same shape; the bridge marshals these types over whichever transport P1 selects. **P0 freezes the
types and method shapes, not the transport.**

---

## 6. Immutable snapshot ownership, revision and conflict rules (frozen)

1. **Revision.** `Snapshot::revision` is a monotonically increasing counter that advances **only**
   when a commit becomes durable. A failed, partial, or no-op commit does not advance it.
2. **Stale edit.** `BeginEdit(base_revision)` returns `0` when `base_revision` is not the current
   revision; the caller re-reads `GetSnapshot()` and starts again. A stale transaction id is
   invalid for every subsequent call.
3. **Capability staleness.** `Set(...)` takes the `capability_revision` the edit was based on. A
   changed capability revision re-validates choices in the affected group (a renderer change
   revalidates AA/filtering/resolution together); an acceptance from the old renderer's list is
   refused, never clamped by index.
4. **Conflicts.** `Commit` re-reads the document under a per-file cross-process lock (or equivalent
   version check) and merges only owned, changed keys. A key changed concurrently by another writer
   yields `kConflict` with that key listed and **no write** — never "last writer wins".
5. **Atomicity.** Write a flushed temporary sibling, then atomically replace the destination; keep
   the old file on failure. Unknown fields and comments survive. A `kNoChanges` commit does not open
   the file (mtime unchanged).
6. **Combined commit.** A native-plus-Ultimate session validates both before either write and uses a
   journal so a partial failure reports the true state and blocks Restart now. It never says
   "nothing was saved" when one replacement already succeeded.
7. **Snapshot immutability.** A published snapshot is never mutated in place. Consumers hold a
   value copy tagged with its revision. Host workers may build request data but never hold guest
   pointers across frames or mutate the cvar registry while the renderer consumes it.

---

## 7. Stable tokens, provenance, and empty selections (frozen)

**Enum tokens are never persisted as list positions and never localized.**

| Setting | Stored token(s) | Translation |
| --- | --- | --- |
| `target` | `common` \| `demo` \| `ultimate` | `ultimate_mode` + `license_mask`, applied inside the game before Ultimate configuration |
| `renderer` | `d3d12` \| `vulkan` | the backend name, straight to `gpu_backend` |
| `anti_aliasing` | `none` \| `native` \| `fxaa` \| `fxaa_extreme` | one native enum (§7.1) |
| `filtering` | `native` \| `none` \| `1x` \| `2x` \| `4x` \| `8x` \| `16x` | `anisotropic_override` −1, 0, 1, 2, 3, 4, 5 (a "16x" label is **never** written as value 16) |
| `camera_angle` | `default` \| `high` \| `low` | provider `camera_angle` int 0/1/2 |
| `game_speed` | integer percent `100`..`150` | `speedmod` as a 2-decimal string; never float-accumulate steps |
| `gems`, `which_skybox`, `forceplot` | the token the **provider** reports | provider int index, resolved at runtime (provider inventory is authoritative) |
| `hidden_main_menu` | `splash_*` tokens the game uses + the Advanced Settings entry + mod rows | `HideRows`; see below |

### 7.1 AA is one enum, not two flags

| Choice | `native_2x_msaa` | `swap_post_effect` |
| --- | --- | --- |
| none | false | `none` |
| native | true | `none` |
| fxaa | false | `fxaa` |
| fxaa_extreme | false | `fxaa_extreme` |

P2/P4 must prove the disabled native path meets the visible None/FXAA semantics before the labels
ship. P0 freezes the mapping, not the proof. Migrating a legacy `on`+`fxaa` combination records the
intentional loss of simultaneous native+FXAA and does **not** invent a fifth choice.

### 7.2 Provenance is per field

`[provenance]` records `default` | `automatic` | `manual` **per setting**, not one global "configured"
bit. A field equal to the historical default is not evidence the user never chose it. Capability
lists, CPU counts, localized strings and transient errors are **not** preferences and never
authoritative on disk.

### 7.3 Empty hidden-row selection

```cpp
// Absent key  -> the user has never chosen: the default selection is the offline trio
// Present []  -> hide nothing
// Present [..]-> hide exactly these
std::optional<std::vector<std::string>> hidden_main_menu;
```

The old free-form CSV (`enhancements_hidden_menu_options`, where empty meant "the three defaults")
cannot express "empty". The new representation **must** distinguish absent from present-empty so an
explicit empty selection cannot unexpectedly hide rows. `Advanced Settings` is always in the list
and non-hideable in v1; `splash_dlc` stays visible under the default offline filter because it is
Refresh Song Library. **Changed 2026-10-20:** the refresh entry's own row defaults **off**, so
`splash_dlc` is hidden by default; the retail *Download Content* entry is restored as
`splash_dlc_store`, behind `interface.hide_download_content` (default Hidden). The absent/present
rule above is unchanged.

---

## 8. Runtime controller allowlist (frozen)

> **Status 2026-10-19: the app is gone; the allowlist stays.** P7 moved the rows this
> section is about into Help & Options → Controls, and P8b retired
> `rb_blitz_controller_settings.exe`, its target and its packaging
> ([gameplay-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md) §17-§19).
> The allowlist and the profile's `[remap]`, `[nav]` and gameplay tables remain **frozen for
> compatibility** — an existing `launcher.toml` must keep loading byte-for-byte — and the game
> itself is now the only writer of the input rows. `[nav]` is read-by-nothing preservation data
> (the app window it described is gone); the seven host-only presentation/audio rows stay
> profile-carried with `rb_blitz.toml`/CLI as their editor (plan §17.1).

Today `ApplyProfileSettings` (`src/profile/profile_apply.cpp`) applies **every** `[settings]`
row from `launcher.toml` at the config rank. After migration that would leave retired native fields
active. Frozen:

```cpp
// The only cvars the runtime applies from the legacy Controller Settings profile once migration is complete.
// Everything else in its [settings] table is inert preservation data.
inline constexpr std::string_view kControllerCvarAllowlist[] = {
    "mouse_ui_nav",                     // controller-owned; Controller Settings writes it
    "input_backend",                    // hand-edit only (no schema row); kept for the XInput path
    "keyboard_support",                 // Controller Settings' Keyboard support row
};
```

The three gameplay-mapping rows joined the frozen list on 2026-10-18 with P1 of
[the gameplay-input-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md)
(its §D2/D6 supersede this section's earlier two-entry spelling for the switches that plan adds).
The in-game Controls store writes them, and a game started on its own has no other way to read
them; the allowlist is what makes a standalone boot see the same values as a boot from the game's
menu. None of them had a consumer before P2/P3, so the change added configuration, not behaviour.

**2026-10-20: the two gameplay switches left the list.** P2 of
[the interface/gameplay mapping restructure plan](../plans/finished/2026-10-19-interface-gameplay-mapping-restructure-plan.md)
(D3/D15) made `gameplay_mapping_controller` and `gameplay_mapping_keyboard_mouse` stop selecting
anything: the Mouse & Keyboard table is always active in a song and the Controller table acts when
it has a custom row. The cvars are no longer defined, so the allowlist names only
`mouse_ui_nav`, `input_backend` and `keyboard_support`; an old profile's two rows survive
byte-for-byte as preservation data and are never applied.

`guide_button` and `keybind_guide` are **excluded** by design (§2.3). The `[remap]`, `[nav]`,
`[gameplay_remap_controller]` and `[gameplay_remap_keyboard_mouse]` tables, window geometry, and the
`[update]` table remain controller-owned. P8/P11 narrow
`ApplyProfileSettings` to this allowlist; P3 supplies the constant. Explicit developer
config/environment/CLI overrides still win over the profile.

---

## 9. Shared files, reserved directories, and the development gate (frozen)

### 9.1 Sole owner while a wave runs

| Shared file / seam | Owner |
| --- | --- |
| Root `CMakeLists.txt` (incl. registering a new test), `src/rb_blitz_app.h`, `src/main.cpp`, `config/functions.toml`, packaging integration | **Coordinator** |
| `src/settings/{model,schema,store,migration,paths,service}.*` | P3 (API changes reviewed by coordinator) |
| `src/hooks/settings_bridge.*`, authored virtual-resource bootstrap | P1, then P5 after explicit handoff |
| Existing `Nt*` import hooks / dispatch (one strong definition per import) | Coordinator with P1+P10 input |
| Graphics SDK changes and `patches/rexglue-sdk` graphics entries | P2 then P4, sequentially |
| New native menu code and schema | P5 |
| Ultimate adapter and exact-file routing | P6 (VFS dispatch hooks coordinated centrally) |
| `installer/setup.iss`, installer helper, pins/template labels, installer tests | P7 (P9 must not repurpose installer helpers) |
| `controller-settings/` (retired 2026-10-19), `src/input/remap.*`, controller parts of `src/profile/` | P8 completed the app's retirement; the recovery/parity half was P8a |
| `src/ui/menu_options.*`, `src/hooks/menu_filter.*`, `src/hooks/shell_music.*`, relevant enhancements | P10 (respect the committed song-cache work) |
| New restart code + early bootstrap wiring | P9 (bootstrap wired by coordinator) |

### 9.2 Reserved output and test directories (no two workers share one)

- One isolated build tree per lane, e.g. `out/build/win-amd64-release-<lane>`; **no** concurrent
  codegen into `generated/` or a shared `out/` output. Delete, don't reuse, a copied tree.
- One scratch config/save root per lane under `out/_swap/<lane>/` (or an ignored `out/<lane>-*/`).
  Never point a worker at the user's live profile or save.
- Never share a mutable game/save test installation between workers.
- Work from a branch/worktree rooted at `1e40765` (clean). Do not fork from a stale `HEAD` and lose
  the committed song-cache work.

### 9.3 Development gate (retain Controller Settings until G4)

> **Closed 2026-10-19.** The gate held the external application as the default product surface
> until G4; P7 moved every input setting into Help & Options → Controls on 2026-10-18, and P8b
> removed the application on 2026-10-19. The bullets below are the historical gate text.

- The full surface (all tabs and rows) and the existing `settings.toml` schema stay the **default**
  product surface until G4. Do **not** delete Controller Settings settings UI before the coordinator approves
  G4/P11.
- The new native menu is registered behind an internal developer gate (a hidden, default-off
  developer cvar, in the spirit of the existing `--show-dev-settings`), so it can be exercised
  without cutting over.
- P8 reduces Controller Settings to Controller Settings **behind this gate** until G4; the file names
  `rb_blitz_controller_settings.exe` and `launcher.toml` are retained for shortcut/profile compatibility.

---

## 10. Owners, gates, and waves (from the parent plan, unchanged)

Work packages: P0 baseline/contracts, P1 native screen+bridge, P2 exact 1080p, P3 storage/service,
P4 graphics capabilities, P5 full native menu, P6 Ultimate adapter, P7 installer paths, P8 controller
reduction, P9 restart, P10 menu/cache cleanup, P11 integration, P12 acceptance.

Gates: **G0** contracts (this file) → **G1** native feasibility → **G2** graphics feasibility →
**G3** persistence → **G4** integration → **G5** release.

Parity: the parent plan §10.2 graph and the "three workers plus one coordinator/reviewer" table are
adopted verbatim. Wave 1 hands out **P1, P2, P3 in parallel** (all depend only on P0); Wave 2 adds
P5/P4/P7; Wave 3 adds P6/P9/P10 then P8; Wave 4 runs P12 then P11 cutover.

Handoff template (every handoff, frozen): owned files changed, contract version, behavior
implemented, commands executed, pass/fail results, remaining unknowns, evidence paths.

---

## 11. Hand-off readiness for P1 / P2 / P3

All three may start from `1e40765`; none needs another package.

| Package | Ready because | Must respect |
| --- | --- | --- |
| **P1** (native screen + bridge) | `SettingId`/`Value`/`Descriptor`/`FieldState`/`Snapshot`/`CommitResult`/`RestartResult` frozen (§5); a stub host service can satisfy the contract while P3 is unfinished. | Transport/ABI is **not** frozen — prove it. Do not edit generated output. One strong `Nt*` definition per import. Physically exclude Ultimate for the base-only proof. |
| **P2** (exact 1080p) | No contract dependency; §7.1 AA mapping and the §5.3 capability shape are what P4 will consume. | Isolated branch + build output; keep 60 Hz guest timing; no installer edits; deliver `docs/settings/graphics.md (part 1, resolution)`. |
| **P3** (storage/service) | Full frozen interfaces (§5), file ownership (§4), provenance/empty-selection rules (§7), controller allowlist (§8). | Materialize §5 verbatim; replace the truncating save with reread/merge + flushed atomic replace; keep controller fields in `launcher.toml`; tests must cover interruption between `paths.toml` and `settings.toml`. |

P1/P2/P3 are **independent** — no cross-lane file overlaps (§9.1) and no shared output dir (§9.2).

---

## 12. Deliberately NOT frozen here

- **Bridge transport / PPC ABI** — P1 proves it (builtin callback, virtual resource, or dedicated
  dispatch). P0 freezes no guessed ABI, no guest address, and no calling convention.
- **Exact render-resolution implementation route** — P2 decides between guest render-dimension hooks
  and backend arbitrary-size work, with target/viewport/resolve evidence.
- **First-run graphics recommendation policy** — P4, backed by measurements.
- **`unlockall` consumer timing** — P6 traces it; the host uses `[ultimate_overrides]` only where
  the provider's save list is insufficient.
- **Restart bootstrap mechanism** — P9 (early executable mode vs a tiny packaged helper).
- **Trial disposition** — P0/P11 reproduce the recorded pre-menu fault; it stays recorded, not
  silently re-routed.

---

## 13. G0 completion statement

- Baseline recorded: commit `1e40765`, clean worktree, no stashes, 22 registered tests, verified
  build/test commands. The parent plan's "staged song-cache work" note is superseded.
- Controller-UI inventory recorded, including the bespoke surface outside `settings.toml` and every
  Guide location to exclude.
- Source map re-checked; all parent-plan paths exist, plus the profile-apply/path-policy seams.
- Interfaces frozen (SDK-free), file ownership and sibling mapping frozen, provenance/empty-selection
  semantics frozen, controller allowlist frozen, shared files and work directories reserved,
  development gate defined.
- Not done here (correctly): no production cutover, no SDK/production code, no runtime validation,
  no transport/ABI freeze. Those belong to P1/G1 onward.
