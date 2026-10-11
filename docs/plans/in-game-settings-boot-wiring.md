# In-game settings P11a — the boot wiring (implementation record)

Status: **P11a executed** (2026-10-11). The game boots on the real settings store: the path file and
the legacy migration run, the rows the game reads at init are applied at the config rank, the guest
channel's edits go through P3's service into `settings.toml`, the provider's values reach the store,
and the restart bootstrap is in the executable's entry translation unit. 35/35 `ctest` green, one new
target (`settings_bridge_service`, 81 checks), and five boot arms in `out/_swap/p11a/` plus one
merged-menu arm.

Parent plan: [in-game-settings-plan.md](in-game-settings-plan.md) §4.2 (ordering), §5.3 (channel),
§5.5 (strategies), §7 (graphics), §8 (restart), §10.1 (P11).
Baseline: `d9e0127` plus this working tree. Nothing committed.

Read with: [in-game-settings-storage.md](in-game-settings-storage.md) §6 (P3's insertion points),
[in-game-settings-hot-apply-restart.md](in-game-settings-hot-apply-restart.md) §6 (P9's two open
coordinator items), [in-game-settings-menu-compat.md](in-game-settings-menu-compat.md) §7 (the
descriptor/menu-row seam), [in-game-settings-ultimate-adapter.md](in-game-settings-ultimate-adapter.md)
§6 (the provider's row routing).

---

## 1. What this package is

P3 built the store, the path file and the service as a host-tested lane and left the five insertion
points to the coordinator. P9 built the application-strategy seam and the restart coordinator and
left the channel's store swap and the executable's bootstrap to the same place. P10 built the hide
matrix and the menu inventory and left the seam that fills it from the live menu. P11a is those
three handovers and nothing else: no new product rule, no new row, no new file format.

| Piece | File | What it owns |
| --- | --- | --- |
| The boot wiring | `src/settings/boot.{h,cpp}` (new; the only SDK-using file in the pair) | The four insertion points, the config-rank writes and their read-back verification, the capability snapshot the boot can honestly build, and the process-wide store the hooks reach |
| The channel's real store | `src/settings/bridge_sink.{h,cpp}` (new, SDK-free) | P1's frames and one-bit answers, with the native rows going through P3's `Service` and the provider's rows (and the gate) kept locally |
| The seam that lets them swap | `src/hooks/settings_bridge.h` | `SinkQueries` (the read/ack half, separate from `RequestSink`) and `Dispatcher::SetSink` |
| The hook side | `src/hooks/settings_bridge_hooks.{h,cpp}` | `InstallSink`; the provider seeding, the write-through and the live apply now go through whichever store is installed |
| The vocabulary | `src/settings/schema.{h,cpp}` | `IdForToken` moved here from P9's runtime applier, because the SDK-free sink needs it and there must stay exactly one spelling |
| The entry point | `src/main.cpp` | The restart bootstrap: a static object's constructor that waits for the parent's verified identity *before* the SDK's entry point runs |
| The app's own hooks | `src/rb_blitz_app.h` | Calls the boot at `OnConfigurePaths`, `OnPostInitLogging`, `OnPostLoadXexImage` and `OnPreLaunchModule` |

## 2. The one new seam: `SinkQueries`

P1's `RequestSink` carries a request *to* a store. The guest's read and acknowledgement are not
requests to a store - they are queries `file_exists` answers with one bit - and until this pass they
were reached by the hook code holding a `MemorySink` directly. That is the coupling P11a had to
break, so the query half is now its own interface and both stores implement it:

```text
SinkQueries:  Matches(request) | Acknowledged(id) | Get(token, out) | last_commit_changes()
```

`Dispatcher::SetSink` (new, inline) points the channel at another store without forgetting the
idempotency window, because that window belongs to the *guest session*, not to a store.

### 2.1 Which rows go where, and why

| Token space | Example | Where its value lives | Why |
| --- | --- | --- | --- |
| A native row | `general.dlc_menu_songs`, `graphics.vsync`, `interface.hidden_main_menu`, `general.unlock_all` | The `Service` (a transaction, `settings.toml`, `[provenance]`, the journal) | They are *our* preferences, and P3's store is what owns them |
| A provider row | `interface.hide_hud`, `gameplay.precision_mode`, `interface.letterbox` | The sink's local space | The installed payload's own file is their source of truth and P6's write-through is what persists them; routing them through the service as well would mean the service's commit *and* the write-through wrote the same values, and the seeding commit would rewrite the player's `ulti_settings.dta` on every boot |
| The provider gate | `provider.active` | The sink's local space | It is a view of the adapter's readiness, not a preference |
| Anything else | `nonsense.row` | refused (`kInvalid`) | A stale screen must not be able to stage a change that lands nowhere |

The classification is `IsLocalToken`, and it is *not* a fallback: a token the schema describes with a
native storage key goes to the service; a token the schema describes as the provider's, an action, or
the gate, stays here; and an unknown token is refused rather than accepted.

### 2.2 What the sink adds on top of P1's stand-in

* **A real transaction.** `begin` opens one at the service's revision (or at the guest's own
  `i:<n>` base, which `BeginEdit` refuses when it is stale); a `set` without a `begin` opens one
  implicitly, so the text form a DTA script writes needs no session bookkeeping. `cancel-field` calls
  `Service::CancelField`; `cancel` calls `Service::Cancel`.
* **Validation.** A value of the wrong kind is refused by the row's own descriptor before it reaches
  the service, and the service then applies *its* rules: availability, overrides, capability
  revision, choice sets.
* **Honest commit status.** `kSaved` → ok, `kNoChanges` → ok, `kConflict`/`kInvalid`/`kIoError` →
  the same, and `kPartial` → an error whose message names the state (plan §6.3 forbids a blanket
  "nothing was saved" when one part is durable).
* **P9's no-op rule, properly.** The service reports the rows the transaction *touched*; the sink
  compares each against the value the row held before the session and reports only the ones that
  really moved, so a commit that writes a value back unchanged cannot re-apply a hot row. The
  reported token is the spelling the *guest* used, so a log line and the hot-apply lookup see the
  screen's own name.
* **The restart op.** `restart/-/-` is recorded (`restart_requested`, `restart_request_id`) and
  answered ok; starting the child is the boot's job, because that is where the process identity and
  the launch context live.

## 3. Boot order, with the reason for each position

| # | Hook | What happens | Why there |
| --- | --- | --- | --- |
| 1 | `OnConfigurePaths` | identity → `paths.toml` → legacy profile → migrate → load `settings.toml`; then the capability snapshot, then the target/licence, the renderer, the DLC and Interface rows and the two path roots, at the config rank | It runs after the legacy profile's rows (which it migrates and then outranks at equal rank) and *before* the SDK loads the game's own `rb_blitz.toml` - which is a later write at the same rank and therefore still beats it (§4.2) |
| 2 | `OnPostInitLogging` | the store's boot lines: the file and its status, the migration, the rows applied, the rows an explicit source owns, and the two open items | The first point where a line goes anywhere; the same reason the launcher's profile report lives here |
| 3 | `OnPostLoadXexImage` (after `ultimate::Configure`, before `bridge::Configure`) | the provider's values reach the service; the GPU plugin's own cvars are written now that it has registered them; the channel is pointed at the service | The provider's state is only known after `ultimate::Configure`; the plugin's cvars (`vsync`, `anisotropic_override`, `native_2x_msaa`, `swap_post_effect`, `resolution_scale`) do not exist at step 1; and `bridge::Configure` *seeds* through the channel, so the store must already be installed |
| 4 | `OnPreLaunchModule` (before `FinalizeInit`) | read every applied cvar back; a row whose value did not stand is recorded as overridden | `SetFlagFromConfig` returning true does not prove the value won (§4.2): a later write at the same rank - `rb_blitz.toml` - beats it, and a row the player cannot change has to say so |

The path roots are the one thing the app finishes: `ConfigurePaths` returns them and
`OnConfigurePaths` puts them into its `PathConfig` (after the profile and after the cvar re-read, so
they replace the legacy path fields, exactly as §9.1 asks). No `OnFinalizePaths` override was needed:
the SDK passes the hook-modified config to it, so the app's own values already survive.

## 4. What is applied, and what is deliberately not

| Row | At boot | Note |
| --- | --- | --- |
| Target | `ultimate_mode` + `license_mask`, as a pair | The licence masks are the game's own (`1` purchased, `0` trial); an explicit `--ultimate_mode` outranks the pair and the row is reported as overridden (measured: the merged-menu arm passes one) |
| Renderer | `gpu_backend` | Written before `OnPreSetup` reads it, which is the whole reason the renderer row is applied at step 1 |
| DLC group | `enhancements_dlc_cache`, `dlc_scan_threads`, `enhancements_menu_dlc_songs` | Read by the mod's own `Configure()` calls later in the same hook |
| Interface group | `enhancements_skip_offline_dialog`, `enhancements_hide_menu_options`, `enhancements_hidden_menu_options` | The hidden-menu list is written *including the explicitly empty list*, which is the product spelling §3.5 asks for and which a command line cannot express (see §5.4) |
| V-Sync, Filtering | `vsync`, `anisotropic_override` | Written at step 3, because those cvars belong to `rexgpu-xenos.dll`; the deferred write is reported and verified like any other |
| Resolution | **not applied** | The exact-render route is P4's Variant B guest-height hook, which is not compiled in, and the plugin's `resolution_scale` cannot be set from a config-rank write (P2). The value is stored for the boot that has the hook |
| Anti-Aliasing | **not applied** | The two mechanisms are known (P2/P4) but the native path's *sample semantics* have never been verified on this device, and §7.1 forbids shipping an unproved mapping. The value is stored |
| Provider rows | none at boot | Their values live in the payload's own file, which its own loader reads before the first frame - which is exactly why every one of them is a `restart` row (§5.5) |
| `unlock_all` | none | P6's open item: its native `[ultimate_overrides]` write is still not made, and this pass did not pretend otherwise |

Two rules make the whole table safe to reason about:

* **A row nobody chose is not written.** `Source::kCompiledDefault` means "never chosen", so the
  game's own default stands. That is what keeps an absent hidden-row selection from becoming an
  explicit one (the defect class P10 named) and it is why a fresh install writes no file at all.
* **Every write is verified.** `FinishInit` reads each applied cvar back; a mismatch is logged as a
  warning and recorded with `Service::SetOverrides`, so the UI can disable the row with an
  explanation instead of saving a value that cannot take effect.

The capability snapshot the boot publishes is deliberately thin, and the log says so: logical
processors from the host, **no display enumeration** (P4's probe needs SDL on the UI thread at a
proven lifecycle point), the producible size set (guest 1280x720 x the offered scales), and one
renderer - the single backend this project ships and has run - with an honest filtering ceiling and
an **empty** native-AA label. P4's own rules do the rest: an unproved label is simply not offered, and
a missing display enumeration publishes the producible set unchanged rather than erasing a request.

## 5. Evidence (2026-10-11)

Harness: `out\_swap\p11a\run.ps1 -Label <name> [-Diagnostic] [-Reuse]`. It boots `rb_blitz.exe`
against a disposable `--user_data_root` with a legacy launcher profile written in it, waits for the
boot's own last line (`N of M applied value(s) stood`), stops the game, prints the lines that matter
and dumps `settings.toml`.

### 5.1 Migration and the config-rank writes

| Arm | Log | Observation |
| --- | --- | --- |
| `migrate2` | `out/_swap/p11a/migrate2/migrate2.log` | `migration done - 2 row(s) imported, 1 translated, 1 carried over`; the legacy profile was backed up byte-identically; `2 row(s) applied at the config rank` (`ultimate_mode = 1`, `license_mask = 1`); `1 cvar(s) belong to the GPU plugin`; then `anisotropic_override = 3 (applied once the GPU plugin registered it)` and `3 of 3 applied value(s) stood`. `settings.toml` was created with `[general] target`, `[graphics] filtering = '4x'`, both provenances and `[migration] imported = true` |

The two imported rows are P3's measured outcome for a legacy profile (`target`, from the profile's own
structured field - whose default is `ultimate`, which is what the retired launcher meant by a default
profile - and `anisotropic_override = 3` as `4x`). The `[settings] target = "common"` row the arm
writes is *not* the structured field, so it is preserved verbatim as an unknown row; the migration's
own rule ("import only missing fields, preserve everything else") is what puts it back unchanged.

### 5.2 The guest channel writes through the real store

| Arm | Log | Observation |
| --- | --- | --- |
| `diagnostic` (`REX_SETTINGS_HOT_APPLY_DIAGNOSTIC=1`) | `out/_swap/p11a/diagnostic/diagnostic.log` | `settings_bridge: store the native settings service (settings.toml)`; `hot-apply diagnostic committed 'graphics.vsync' -> ok`; `settings_bridge: applied 'graphics.vsync' to the running game`; `hot_apply: vsync = false (was true), applied to the running game` - and **`settings.toml` gained `vsync = false` with `'graphics.vsync' = 'manual'`**. The commit went through the service: a transaction, a durable write, the journal's rule, the provenance |
| `readback` (a later boot on that file) | `out/_swap/p11a/readback/readback.log` | `settings '…' (loaded)`; `migration already current`; `vsync = false (applied once the GPU plugin registered it)`; `4 of 4 applied value(s) stood`. A value the *menu* wrote is consumed by the next boot |

### 5.3 The merged screen reads the real store

P5b's own harness (`out\_swap\p5b\run.ps1`) driven over a `settings.toml` written **before** the boot:

```toml
[interface]
hide_main_menu = true
hidden_main_menu = []          # the explicitly empty selection
```

`-Route basescroll` (the route that opens Interface → Hide Items → Main Menu) then shows, in
`out/_swap/p5b/p11a-store.log`:

```text
settings_boot:   enhancements_hide_menu_options = true
settings_boot:   enhancements_hidden_menu_options =
menu_filter: R5 on: the selection is empty; the master is on and nothing is hidden
settings_bridge: read 'interface.hide_main_menu' -> true
settings_bridge: read 'interface.hidden_main_menu.splash_exit' -> false
settings_boot: 3 of 3 applied value(s) stood; 1 row(s) are owned by an explicit source
```

Three things at once: the master row's state came from the *file* (not from a compiled default), every
row choice on that page was answered by the service, and **P10's "not measured in a boot" item - the
explicitly empty selection - is now measured**, in the product spelling the store gives it. The
warning is the CLI's `--ultimate_mode=2` correctly outranking the stored target.

Zero guest faults in every arm (`violation`/`guest access`/`fatal`: no hits), and the installed
retail/Ultimate records are untouched - nothing in this pass writes to the game tree.

## 6. What this pass did not do

* **P4's two open items** (the display probe and the Variant B guest-height hook) and the AA sample
  verification: the Resolution and Anti-Aliasing rows are stored but not applied, and the boot says so
  in its own log line. Switching them on means the probes and the `[[midasm_hook]]` entry, not a
  change here.
* **The menu-rows seam is wired but not filled.** `SettingsBoot::SetMenuRows` forwards to both tables
  (`schema::SetMenuRows` and `Service::SetMenuRows`), and `boot::SettingsBoot::Current()` is the
  handle a hook needs - but the *caller* has to be the one that holds the splash record's bytes, which
  is `src/hooks/menu_filter.cpp`'s read buffer, not this module. `MenuInventory` is still uncalled at
  runtime.
* **The restart prompt and the Target soft restart** (P9 §6.1 / the plan's §8 prompt): the request
  path now exists end to end (`ServiceSink` records the guest's `restart` op; P9's coordinator is
  ready), but nothing calls `RestartCoordinator::Request` yet, because the prompt that decides *when*
  is authored DTA work in the settings screen. The bootstrap half is done and measured by P9.
* **The provider's `unlock_all` native write** and **the achievements action**: both stay P6's open
  items, and the channel answers the action with `kUnsupported` rather than a cheerful lie.
* **The launcher rows' removal, the in-song merge and the cutover** - P8/P5b-r2/P11b.
* **A 50-package menu pool boot**, the in-song entry, and the trial target: unchanged open items.

## 7. Tests

```text
ctest --test-dir out/build/win-amd64-release --output-on-failure     # 35/35
```

New target `settings_bridge_service` (81 checks, `tests/settings_bridge_service_tests.cpp`), the
whole of it against a **real** `Service` over a temporary directory: the text form a DTA script writes
and the framed form a host caller writes both reach the same store; a commit is durable and
acknowledged only when the service wrote, and a replayed id does not reach it twice; a value written
back unchanged is not reported (both the "already the same on disk" and the change-then-revert case);
the provider's own rows never appear in `settings.toml`; the gate is local; an unknown setting, a
wrong-kind value and an unknown op are refused; cancelling restores the field and writes nothing; the
restart op is recorded; and the wire/model conversions refuse a mismatched kind while a resolution
round-trips as `1920x1080`.

Also updated by this pass: `settings_hot_apply` (the token vocabulary moved, the test still passes),
`menu_compat` (unchanged), and the whole suite as a regression check.

## 8. Reproduce

```powershell
cmake --preset win-amd64-release
cmake --build out\build\win-amd64-release
ctest --test-dir out\build\win-amd64-release --output-on-failure            # 35/35

pwsh -NoProfile -File out\_swap\p11a\run.ps1 -Label migrate                 # migration + config-rank writes
pwsh -NoProfile -File out\_swap\p11a\run.ps1 -Label diagnostic -Diagnostic -Reuse
pwsh -NoProfile -File out\_swap\p11a\run.ps1 -Label readback -Reuse         # the value the menu wrote

# The merged screen reading the store needs P5b's payload and harness:
pwsh -NoProfile -File out\_swap\p5b\run.ps1 -Label p11a-store -Route basescroll
```

## 9. Handoff

| Package | What P11a supplies |
| --- | --- |
| P5b-r2 (the in-song merge) | Nothing new to change, but it now runs with the store live: a screen edit writes `settings.toml` |
| P9-ui (the restart prompt) | `ServiceSink::restart_requested()` / `restart_request_id()` is the guest's request; `Service` + P9's `RestartCoordinator` are both reachable from the boot. The prompt decides when to call `Request` |
| P11b (the cutover) | The store is the default source, so `profile_controller_rows_only` can finally be flipped; the launcher's `[settings]` rows other than the controller allowlist become inert preservation data |
| P10's follow-up | `SetMenuRows` + `Current()` are the seam; the caller is the menu filter, which is where the record bytes are |
| P4's follow-up | `SettingsBoot::AfterLoadXexImage` is where a real display probe and the Variant B hook would land; the two rows' "not applied" branches in `ApplyRow` are the switch |
