# In-game settings — P3 storage, migration and service (implementation record)

Status: **P3 complete** as a host-tested lane. `src/settings/{model,schema,store,migration,paths,service}.{h,cpp}`
materialize the frozen G0 interfaces and pass 205 host checks across three new ctest tests. **No
production code was wired into boot** — the integration steps are named in §6 and the coordinator
owns `src/rb_blitz_app.h`, `src/main.cpp` and the `RBBLITZ_SOURCES` list. No cutover, no launcher
change, no installer change, no UI.

Read order: `docs/plans/in-game-settings-contracts.md` (frozen names) → this file (what P3 built,
where it deviates, and how to wire it) → the parent plan for product behaviour.

Evidence labels: **measured** = executed in this pass; **frozen** = G0 decision; **P3 addition** =
a delta this pass had to make to a frozen shape, recorded here for coordinator review.

---

## 1. Baseline and build record (measured, 2026-10-10)

| Item | Value |
| --- | --- |
| Baseline | `1e40765`, branch `p2-exact-1080p` (P1/P2 share this worktree) |
| New files | `src/settings/model.{h,cpp}`, `schema.{h,cpp}`, `store.{h,cpp}`, `paths.{h,cpp}`, `migration.{h,cpp}`, `cvars.{h,cpp}`, `service.{h,cpp}` |
| New tests | `tests/settings_store_tests.cpp`, `tests/settings_migration_tests.cpp`, `tests/settings_service_tests.cpp` |
| Shared file touched | root `CMakeLists.txt` — three `add_executable`/`add_test` blocks inside `BUILD_TESTING` only (the coordinator's edit, performed here because no other actor can register a test) |
| Tests | **26/26 passed** (`ctest --test-dir out/build/win-amd64-release --output-on-failure`, 3.27 s) |
| New check counts | store 60, migration 61, service 84 = **205 checks** |

The three targets are SDK-free and compile from `src/` plus `src/launcher/profile.{cpp}` and
`src/launcher/profile_path.cpp` (the migration consumes the profile model). Nothing in
`src/settings/` includes `rex/*`, `<windows.h>` or SDL, so a boot is not required for any of it.

## 2. What each file is (frozen names materialized)

| File | Contents |
| --- | --- |
| `model.h/.cpp` | §5.1–§5.3 and §5.8 verbatim: `Category`, `SettingId` (unchanged, no new ids), `ValueKind`, `Value`, `WidgetKind`, `Provider`, `Availability`, `Choice`, `Descriptor`, `Source`, `Origin`, `FieldState`, `DisplayMode`, `RendererCapability`, `ProviderState`, `CapabilitySnapshot`, `kSnapshotSchemaVersion`, `Snapshot`, `CommitStatus`, `CommitResult`, `ValidationResult`, `RestartStatus`, `RestartResult`, `ActionResult`, plus `SettingIdToken`, `StorageKey`, `ValueEquals`, `ValueToString`, `IsAction` and the token tables. |
| `schema.h/.cpp` | The row table in parent-plan §3 presentation order (target, DLC group, achievements group, graphics, letterbox, HUD group, hide-items group, gameplay), each row's storage key and submenu group, `BuildDescriptors(caps)` (resolves `renderers`/`resolutions`/`processor_count`/`menu_rows` and sets provider-row availability), `ParseTokenToValue`, `LabelForToken`, `StorageKeyFor`. |
| `store.h/.cpp` | `toml::Document` (comment/unknown-preserving line editor) + the typed `Document`, `StorePaths`, `DocumentLoad`, `SaveRequest`, `SaveOutcome`, `Store`, `ReadFileText`, `WriteFileAtomic`. |
| `paths.h/.cpp` | §5.5 verbatim (`PathEntry`, `PathFileResult`, `ResolvePathFile`, `LoadPathFile`, `SavePathFile`) plus `ResolvePathValue`, `SettingsFileForProfile` (§4.1 sibling mapping) and `ResolveSettingsFile` (adapts `ResolveProfilePath`). |
| `migration.h/.cpp` | §5.6 verbatim (`MigrationStatus`, `MigrationStats`, `MigrationResult`, the 3-argument `Migrate`) plus a `MigrationOptions` overload. |
| `cvars.h/.cpp` | §8's `kControllerCvarAllowlist`, `TargetToCvars`, `AntiAliasingToCvars` (§7.1), `FilteringToCvarValue` (§3.2), `CvarForSetting`, `ProviderCvarFor`, `SourceRank`, `OverrideExplanation`. |
| `service.h/.cpp` | §5.7's `ProviderReadiness`/`ProviderSaveResult`, the `ProviderSink` and `Reconciler` seams, and `Service` with the eight frozen methods verbatim plus `Boot`, `SetCapabilities`, `SetReconciler`, `SetProviderSink`, `SetProviderValues`, `SetOverrides`, `SetMenuRows`, `DescriptorFor`, `RequestedValue`, `EffectiveValue`, `EffectiveSource`, `IsOverridden`, `revision`, `capability_revision`. |

## 3. P3 additions to frozen shapes (coordinator review)

Four deltas were unavoidable; none changes a frozen id, token or method signature.

1. **Submenu containers carry no `SettingId`.** §5.1's frozen enum has ids for the leaf rows but not
   for *Downloadable Content*, *Achievements & Unlocks*, *HUD*, *Hide Items* or *Plots & Skyboxes*.
   Rather than invent ids, `SchemaRow::group` (a P3-only field on the P3-only `SchemaRow`, not on the
   frozen `Descriptor`) names the container. `Descriptor::submenu_parent` is still used where the
   frozen shape intends it (`kHideMainMenu` → `kHiddenMainMenu`).
2. **`choices_source` gains two tokens.** §5.1 froze `"" | "renderers" | "resolutions" | "provider"`.
   The DLC scan-thread ceiling depends on the machine (`"processor_count"`, §3.1) and the Hide Main
   Menu row list is P10's seam (`"menu_rows"`). Both are resolved in `BuildDescriptors` /
   `Service::SetMenuRows`; an unknown source leaves the static choices alone.
3. **`settings.toml` gains `[migration] imported = true`.** §4.1 says the file holds a "migration
   version"; §5.4's illustrative shape does not spell the key. The marker is what makes migration
   idempotent and what distinguishes `kAlreadyCurrent` from `kInterruptedResumed`.
4. **`Migrate` also has a `MigrationOptions` overload.** The 3-argument frozen form delegates to it.
   `apply_paths=false` implements "match the old install identity before applying path fields"
   (§4.3 item 3) without changing the frozen signature.

## 4. Behaviour implemented (frozen rules, measured by the tests)

**Write path (§4.4).** A save re-reads the destination, merges only the keys the caller changed,
and refuses a key whose on-disk value differs from the loaded base (`kConflict`, the key listed, *no
write*). It writes a flushed temporary sibling (`fflush` + `_commit` on Windows) and atomically
replaces the destination (`fs::rename`, which is `MoveFileExW(REPLACE_EXISTING)` on Windows), keeping
the old bytes on failure. A save that would render identical text never opens the file (mtime
unchanged). Comments, unknown keys and unknown tables survive byte for byte.

**Provenance (§7.2).** `[provenance]` is per field, keyed `'<section>.<key>'` (`'general.target'`,
`'graphics.resolution'`). A row is never inferred from value equality; only the rows a save names are
touched, so a save that does not manage provenance cannot erase a marker.

**Empty hidden-row selection (§7.3).** Absent key → the default offline trio applies. Present `[]` →
hide nothing. Present `[…]` → hide exactly these. The old free-form CSV cannot express "empty", so
migration imports nothing when the CSV is empty *or* equals the legacy default trio.

**Migration (§4.3).** Byte-identical backup (`<legacy>.pre-migration.bak`) before the first write;
import only missing fields; target imported (`common|demo|ultimate` preserved through
`LaunchTargetName`); `resolution_scale=n → [1280*n, 720*n]` (never `2 → 1920x1080`); filtering
`−1/0/1/2/3/4/5 → native|none|1x|2x|4x|8x|16x`; the AA pair (off+none → none, on+none → native, any
FXAA → that FXAA choice with the intentional native+FXAA loss recorded as a diagnostic); paths from
`[settings] user_data_root/dlc_root` then legacy `[launch] user_data_dir/dlc_dir`; retired keys
(rename, pool size, monitor, fullscreen, `present_*`, `audio_*`, `guide_button`, `keybind_guide`,
`unlockall`) dropped; controller rows preserved in `launcher.toml`. Cross-file interruption is
recovered: settings written + paths missing → `kInterruptedResumed` and the paths finish on the next
run without erasing the settings.

**A value equal to the compiled default is not written.** This keeps `settings.toml` minimal and
keeps P4's first-run automatic policy meaningful (origin, not presence, says "the user chose this").
A legacy row at the *new* model's default therefore imports as `default`; one that differs imports as
`manual`, so an existing manual choice is never reset on value equality. Measured consequence: a
default launcher profile imports exactly two rows — the target (`ultimate`) and the legacy
`anisotropic_override = 3` (`4x`) — which is the filtering the game actually ran with.

**Service (§5.2/§6).** `requested` / `effective` / `pending` are three values. `BeginEdit` on a stale
revision returns 0; `Set` refuses a stale `capability_revision` (never clamps by index), a hidden or
disabled row, an action, and an overridden row. `Commit` advances `revision` only on a durable
write; a combined native+provider commit writes a `<settings>.commit-journal`, validates both
portions before writing either, and reports `kPartial` (never "nothing was saved") when only one part
is durable. `RequestRestart` reports `kStaleRevision` / `kAlreadyRequested`.

**Provider seam (§5.7/§6.2).** `ProviderSink` is P6's; with no sink a commit that touches a provider
row is `kPartial` with `settings.provider_unavailable` — P3 never reports an Ultimate save as durable
on its own word. `kUnlockAll` is the one provider row persisted in `settings.toml`
(`[ultimate_overrides] unlock_all`), per §6.2.

**Reconciliation seam.** `Reconciler` is P4's. The default only enforces the row's kind and its
choice set; P4 supplies the graphics policy. Tested: a stored `dlc_scan_threads = 16` on a machine
now exposing 4 logical processors keeps the request 16 and shows effective 4 with a correction
reason — and the correction is not written back.

## 5. Evidence

```powershell
# Repository root
cmake --preset win-amd64-release
cmake --build out/build/win-amd64-release --target rb_blitz_settings_store_tests `
      rb_blitz_settings_migration_tests rb_blitz_settings_service_tests
ctest --test-dir out/build/win-amd64-release --output-on-failure     # 26/26 pass
```

`ctest -N` now reports **26** tests: the previous 22, P1's `settings_bridge`, and `settings_store`,
`settings_migration`, `settings_service`. The new tests also build and pass standalone under
`clang++ -std=c++23 -Isrc` (60 + 61 + 84 checks).

What the tests pin, case by case: a no-op patch renders identical text; a changed key keeps its
inline comment and an unknown table survives; arrays and array-of-strings read back; a missing file
is not an error, a file with no `schema_version` is malformed and untouched, an unknown version is
refused; provenance and the absent-vs-empty list both round trip; a conflict writes nothing; a
malformed destination and a read-only destination both keep their bytes; two disjoint writers both
survive; the §4.1 sibling mapping and the portable-marker/pointer settings location; the migration
translations and the interruption recovery; the override/stale-capability/hidden-row refusals; the
partial provider commit; the journal being cleared on success; the restart handshake; the cvar
tables.

## 6. Boot wiring recommendations (coordinator; §4.2 ordering)

The service is host-side and needs no guest. The five insertion points, with the exact calls:

1. **`OnConfigurePaths`** — resolve identity and migrate before anything else reads settings.
   ```cpp
   launcher::ProfilePathInputs inputs =
       launcher::ProfilePathInputsFromEnvironment(rex::filesystem::GetExecutableFolder());
   inputs.command_line_value = REXCVAR_GET(launcher_profile);       // existing cvar
   settings::StorePaths sp;
   sp.settings_file  = settings::ResolveSettingsFile(inputs);
   sp.paths_file     = settings::ResolvePathFile(inputs.executable_dir);
   sp.legacy_profile = launcher::ResolveProfilePath(inputs);
   const settings::PathFileResult paths = settings::LoadPathFile(sp.paths_file);   // may be missing
   std::string why;
   auto legacy = launcher::ReadLauncherProfile(&why);               // existing helper
   settings::Service service(sp);
   settings::MigrationResult migration;
   service.Boot(legacy ? &*legacy : nullptr, paths.paths, &migration, legacy.has_value());
   ```
   Queue `migration.diagnostics` and any `LoadPathFile` error until logging exists.
2. **Before backend/window allocation** (the `OnPreSetup` path, not `OnFinalizePaths`) — after P2/P4
   have probed, `service.SetCapabilities(caps)` then apply the effective graphics rows at the config
   rank. Use `TargetToCvars` (before `ultimate::Configure`), `AntiAliasingToCvars`,
   `FilteringToCvarValue`, and `CvarForSetting` for the simple rows. `Set` a value with
   `SetFlagFromConfig`, never `SetFlagByName` at runtime rank.
3. **`OnFinalizePaths`** — recompute the roots from the final cvar sources, so a pre-config snapshot
   cannot win: `ResolvePathValue(entry.dlc_root, sp.paths_file, <game_data_root>/dlc)` and the same
   for `user_data_root`.
4. **Around `ultimate::Configure`** — translate the effective target and license policy; after it
   returns, `service.SetProviderSink(<P6 sink>)` (which also pulls `ReadValues()`), and
   `service.SetProviderValues(...)` if the provider is read separately.
5. **Before `FinalizeInit`** — finish every init-only decision. Menu edits after that change
   persisted/pending state, not init-only cvars.

Also: `service.SetOverrides(...)` with the values `rb_blitz.toml` / environment / CLI won, so an
overridden row is shown disabled with `OverrideExplanation`. P8 narrows
`ApplyLauncherProfileSettings` to `settings::IsControllerCvar`. P9 consumes
`Service::RequestRestart` after `CommitResult::saved_revision`. The UI gets an immutable by-value
`Snapshot` from `GetSnapshot()` and never holds a host reference across a frame.

## 7. Handoff

| Package | What P3 supplies |
| --- | --- |
| P4 | `Reconciler` (implement `Validate`/`Reconcile`), `CapabilitySnapshot`, `BuildDescriptors`, `SourceRank`, `OverrideExplanation`. |
| P5 | `Service` (snapshot + transactions), `schema::Rows` (order, groups, label keys), `SetMenuRows`. |
| P6 | `ProviderSink`, `ProviderReadiness`, `ProviderSaveResult`, the `[ultimate_overrides]` path for `unlock_all`, `ProviderCvarFor`. |
| P7 | `paths.h` (`PathEntry`, `SavePathFile`, `ResolvePathValue`) and the §4.1 sibling mapping. |
| P8 | `kControllerCvarAllowlist`, `IsControllerCvar`, the migration's Guide drop, the preserved controller rows. |
| P9 | `CommitResult::saved_revision`, `Service::RequestRestart`, the commit journal. |
| P10 | `SettingId::kHiddenMainMenu` as a three-state list row and `SetMenuRows`. |

## 8. Deliberately not done here

- No boot wiring, no `RBBLITZ_SOURCES` entry, no `rb_blitz_app.h`/`main.cpp` change (coordinator).
- No real provider save, no real graphics policy, no real UI — those are P6, P4 and P5.
- No new cvar is invented: the license masks are passed in as `LicensePolicy`, because §12 keeps the
  bit values out of P0.
- The `[provenance]`, `[automatic]` and `[migration]` tables are written only when they have content,
  so a fresh `settings.toml` is not seeded with guessed machine defaults.
