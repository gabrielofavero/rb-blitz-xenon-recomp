# In-game settings — status, evidence and remaining work

This is the single living record of where the in-game settings work stands: what ships today, the
owner punch list that landed on 2026-10-13, every open item by package, the 2026-10-12 black-startup
investigation, and the handover that preceded it. It consolidates three documents written in
`docs/plans/` (see [Sources](#sources)). Parts 2 and 3 keep the research history and are superseded by
Part 1 wherever they disagree.

## At a glance

- **The feature is wired through the real boot and the guest UI**: settings load in
  `OnConfigurePaths` before the GPU plugin, title data and guest rendering, the renderer is chosen
  before the plugin loads, and VSync/filtering/anti-aliasing/resolution scale apply in `OnPreSetup` —
  not when the player presses A.
- **1080p is the title's own 960×540 at backend draw scale 2**; the experimental native-1080p
  guest-height route was removed (it corrupted title-screen artwork before the menu).
- **The merged Help & Options tree works from the main menu and the in-song pause route** — the
  in-song construction-order fault was fixed by deferring those screens until the pause panels exist,
  and the in-song root, child pages, Back and resume were exercised in a running game.
- **The menu payload extends the mounted archive in place (one part in, one part out)**, so
  Ultimate-shadowed records survive; provider choices come from the installed payload, its settings
  file is redirected into user storage, `unlockall` is stored by the native service, and the
  achievements action invokes the provider action.
- **All eleven owner punch-list changes landed (2026-10-13)** — mod's own main-menu entry removed, A
  advancing/wrapping a horizontal select, the restart prompt only when leaving the root, the demo
  named in Target, the DLC page regrouped (*Refresh Music Library* now on the main menu), On/Off
  booleans, `Overscan: On`, Gameplay regrouped, Depth of Field moved to Graphics, a new Display row
  (Windowed/Borderless/Full Screen, [patch 0016](../../patches/README.md)), Video regrouped — **plus
  Controller Settings cutover (P11b)**: `kGameOwnedRows` no longer travel on the command line, the target
  moves through migration (not `--ultimate_mode`/`--license_mask`), and
  `profile_controller_rows_only` defaults true.
- **Vulkan is selectable now** (`REXGLUE_USE_VULKAN=ON` in the Windows presets; Renderer offers
  `D3D12, VULKAN` with d3d12 still the active/fallback entry), the payload ships `vulkan-1.dll` by
  owner decision, and a missing loader is refused on both sides.
- **Boot speed (2026-10-13)**: the wait is the DLC discovery, not the A/V load; the defaults now cache
  the library, zero `deferred_overlapped_delay_ms` and free the frame rate, so the walk is ~0.3 s and
  2,804 items enumerate in 39 s (was ~6 minutes).
- **Newest feature: row descriptions** — the highlighted row's canonical symbol is published over the
  settings channel, retail/in-song pages are resolved by a host observer, and a host overlay draws
  the caption (`settings_help_overlay`, default on). **Not yet seen by a human** on a running screen.
- **Verifiably broken/open: the in-song entry** — a screen of ours cannot list the in-song panels
  (`pause_panel`, `game_fade_out_panel`, `game_panel`); any such screen faults the guest with
  `read of guest 0x00000024`, and Back fails on the same limitation. It is item 13 below and the only
  open item blocking gate G1b.
- **Verifiably open: the interactive acceptance matrix has not been re-run** since the punch list, so
  items 2, 3, 6/7 and 10 are asserted by tests and the payload's records, not by a person looking at
  the screen.
- **Verifiably open: no Windows preset can build a test right now** (`REXGLUE_USE_VULKAN=ON` makes the
  SDK's glslang guard write `BUILD_TESTING=OFF` before `include(CTest)`); a Vulkan-off tree configured
  with `-DBUILD_TESTING=ON` builds everything and runs **38/38** (was 37/37 until `window_policy` was
  added on 2026-10-14). Building that tree **clobbers the Vulkan-on SDK objects** — the two configure
  into the same `rexglue-sdk/out/win-amd64` — so re-run `cmake --preset win-amd64-release` before the
  next release build.
- **The window follows the resolution now (P2 of the installed-game review, 2026-10-14)**: the boot
  writes `video_mode_width/height` (the presenter scales into a region of the frame's own shape),
  `window_width/height` when the decorated window would not fit the desktop's usable area, and
  `window_resizable = false` ([patch 0017](../../patches/rexglue-sdk/0017-fixed-size-window.patch)) —
  measured: a first run with **nothing stored** reaches 2560x1080 on this machine's 21:9 primary with
  a 2354x993 client and no `WS_THICKFRAME`. The game window is deliberately one size; the Controller
  Settings utility is untouched. See [graphics.md](graphics.md) part 3 §11.
- **The restart confirmation can be left now (P3 of the installed-game review, 2026-10-14)**: its own
  Cancel returned to the **merged root it was leaving**, so Help & Options could not be left at all —
  every leave re-opened the confirmation — and a restart refused before the boot's handler ran left
  `restart.ok` true, so *Restart now* was a silent no-op. The confirmation now inherits the leave
  target the root was about to use, every answer resolves that bit, and a failed save/lock names the
  path and the OS reason instead of "another writer or access denied". Driven before and after in
  `out/_swap/p3/` (`repro3-close` vs `after-close`, `after-restart`); the host suite is still 38/38.
- **Verifiably fixed**: the 2026-10-12 black startup (the generated payload made the patch archive
  two-part, which this title never reads — Part 2) and graphics recovery (the marker, fallback and
  safe mode all ran; older statements, including item 11 below, are wrong).
- **The next three things to do**: (a) drive the merged menu once after the punch list (A
  advance/wrap, On/Off in every language, Borderless) — the restart prompt's leave path is measured
  now (the P3 bullet above), its in-song twin is not; (b) the in-song
  entry (item 13) — merge into the pause panel's inline list and build the pause-context child
  screens; (c) make a preset that can build a test again, so 38/38 is reproducible from the presets.

## Part 1 — Current status and remaining work (was `docs/plans/in-game-settings-remaining-work.md`)

> **Update 2026-10-13 (latest).** The owner drove the merged Help & Options surface in a running game
> and gave an eleven-item punch list; all of it is implemented, and so is Controller Settings cutover it
> required (P11b) and the retirement of the second main-menu entry to the mod's own screen. The
> release tree is **37/37 green** (`ctest --test-dir out\build\win-amd64-release`), subject to the
> preset caveat below; the interactive acceptance matrix has still not been re-run.
>
> Statements this session measured to be wrong: graphics recovery **is** wired (the marker, the
> fallback and safe mode all ran); the Ultimate boot **does** present and **does** carry the merged
> root. The feature has not passed final acceptance.
>
> **On the numbered list in §3.** "What is left, by package" is the whole plan's open-item ledger: one
> number per item, cited elsewhere by number (item 13 is the in-song entry, the one open item blocking
> gate G1b), so the numbers are kept exactly as written and open items are not deleted. The item's own
> italic citations name documents now consolidated as [plan.md](plan.md),
> [architecture.md](architecture.md), [menu.md](menu.md), [graphics.md](graphics.md) and
> [contracts.md](contracts.md).

### Implemented

The settings feature is wired through the real boot and guest UI:

* Native settings are read from `settings.toml` in `OnConfigurePaths`, before the GPU plugin, title
  data and guest rendering (the path file and legacy migration in the same early stage), and renderer
  selection precedes the plugin load. After the plugin registers its cvars, VSync, filtering,
  anti-aliasing and the resolution scale are applied in `OnPreSetup`, before presentation and render
  resources are initialized.
* Resolution uses the measured 960×540 or 1280×720 title guest modes times an integer backend draw
  scale (1920×1080 is 960×540 at 2×); the experimental native 1080p guest-height route was removed
  because it corrupted title-screen artwork before the menu.
* The generated Help & Options tree works from the main menu and the in-song pause route — the in-song
  construction-order fault was fixed by deferring those screens until the pause panels are available,
  and the in-song root, child pages, Back and resume were exercised in a running game.
* The menu payload extends the mounted archive, so Ultimate-shadowed records are retained; provider
  choices come from the installed payload, its settings file is redirected into user storage,
  `unlockall` is stored by the native service, and the achievements action invokes the provider action.
* Save errors, restart confirmation and restart coordination are connected; the child command builder
  no longer duplicates the game-data-root argument (a previous launch exposed that defect).

### Owner review punch list (2026-10-13)

The owner drove the merged surface in a running game (Ultimate installed, live DLC library) and gave
eleven changes. All eleven are in `src/ui/settings_screen.{h,cpp}` and the records it edits, with the
game-side and Controller Settings-side consequences listed after them:

1. **The main-menu entry to the mod's own screen is gone.** "Help & Options is enough": with a
   compatible provider installed, the payload's own `mod_settings` row is *removed* from the splash
   root (`RemoveRow`, [menu_payload.cpp](../../src/settings/menu_payload.cpp)) instead of being
   rerouted, so the only way to the provider's pages is the merged Help & Options tree. A payload
   that never had the row refuses the removal and that refusal is not fatal.
2. **A horizontal select advances and wraps.** A (`kAction_Confirm`) on a row whose widget is
   `kHorizontalSelect` moves to the next choice and wraps to the first after the last
   (`(index + 1) % choices`), commits it through the bridge, and shows the new value — the arrows
   keep their own step-and-commit behaviour.
3. **The restart prompt only opens when the player leaves Help & Options.** The root screen's Cancel
   checks the bridge's sticky `restart.pending` flag, which is set when a commit succeeded and the
   row needs a restart ([bridge_sink.cpp](../../src/settings/bridge_sink.cpp)); a change that needs
   one no longer interrupts the player mid-edit, and leaving with nothing pending does not ask.
4. **Target lists the demo by name.** "Rock Band Blitz Demo" is a choice again
   ([settings_payload.cpp](../../src/hooks/settings_payload.cpp)); the recorded trial-boot fault is a
   separate matter, and Controller Settings' radio says the same thing now
   ([general_tab.cpp](../../controller-settings/src/general_tab.cpp)).
5. **The DLC page and the refresh action.** *Cache DLC Library* reads `On`/`Off` instead of the
   "Externally configured" marker it used to show — Controller Settings' `--enhancements_dlc_cache` row
   outranked it, which made it read-only, and the cutover below removed that flag; *Refresh
   Downloadable Content* left the settings tree to become **Refresh Music Library** on the main menu.
   **Superseded 2026-10-14:** *DLC Menu Songs* was retired here (the pool always took what the library
   yielded) and is a **visible row again** — `general.dlc_menu_songs`, on by default, carrying
   `enhancements_menu_dlc_songs` (the owner asked for the R9 toggle in-game, and the *Play Current
   Song* row beside it is gated on it); the *pool size* row (`enhancements_menu_dlc_song_count`) stays
   retired and its description says it is ignored. See
   [play-current-song.md](../plans/in-progress/2026-10-14-play-current-song.md) D1.
6. **Boolean rows read On / Off, not the locale's ENABLED / DISABLED.** The payload overrides the two
   locale tokens the retail checkboxes use (`LocaleOverrides()`), per language, without touching any
   other string.
7. **Audio & Video's two banners became suffixes.** Overscan and Bass Boost read `Overscan: On` /
   `Bass Boost: On` — the `| ENABLED` spelling is gone — by the same locale-override mechanism plus
   the label rule that appends `: On`/`: Off`.
8. **Gameplay regrouped**: Camera Angle, Game Speed, **Tracks & Gems** (Precision Mode, Disable
   Random Track Wiggles, Gems), Plots & Skyboxes.
9. **Depth of Field** moved to Audio & Video → Video → Graphics (it is a graphics row, not a
   gameplay one).
10. **A Display row exists** (Windowed / Borderless / Full Screen) — a new setting
    (`SettingId::kDisplay`), a new SDK cvar and the third window mode ([patch 0016](../../patches/README.md)),
    applied through `DisplayToCvars`.
11. **Video regrouped**: the Display group (Display, Resolution, V-Sync), the Graphics group
    (Anti-Aliasing, Anisotropic Filtering, Depth of Field), then Renderer.

#### Renderer: Vulkan is an option now (owner request, 2026-10-13, later the same day)

Two things were missing and both are in:

* The build sets `REXGLUE_USE_VULKAN=ON` in the Windows presets (`windows-base`); the SDK's own
  Windows default is **OFF**, so the measured tree was D3D12-only and every Renderer row offered one
  choice.
* The game's row had hardcoded one backend (`"not probed: the one backend this project ships"`); the
  boot now builds one probe per `RBBLITZ_BACKENDS` entry ([CMakeLists.txt](../../CMakeLists.txt), the
  same switches the plugin and Controller Settings' `--backends=` read) and checks for `vulkan-1.dll` at run
  time. d3d12 stays the active/fallback entry — Vulkan is offered, not recommended — and a missing
  loader means the row is not offered and `--gpu_backend=vulkan` / a stored `vulkan` choice refuses
  and boots D3D12.
* **The payload ships the loader (owner decision)**: `installer/tools/make_payload.ps1` copies it
  (`-VulkanLoaderDir`, else `$env:VULKAN_SDK\Bin`, `C:\VulkanSDK\*\Bin`, `$env:WINDIR\System32`; a
  miss is fatal unless `-AllowMissingVulkanLoader`). Measured: 9-file snapshot,
  `vulkan-1.dll 1,452,408 bytes`, `payload renderer backends: d3d12, vulkan`; the game logs
  `settings_menu: Renderer offers 2: D3D12, VULKAN`.

#### Renderer row: drawn only when there is a backend to choose (owner decision, 2026-10-14)

The owner asked for the row to be hidden where it has nothing to offer, after an installed build appeared to
offer D3D12 alone. That appearance was a **packaging** fault rather than the machine's: the setup
executable had been built from `installer/out/payload` as it stood before `REXGLUE_USE_VULKAN=ON`, so it
shipped a one-backend `rexgpu-xenos.dll`. `build.ps1` now verifies a snapshot against the build tree
before reusing it, so that cannot happen silently again
([installed-game-fix-prompts.md](../plans/in-progress/2026-10-14-installed-game-fix-prompts.md) P1).
The row rule is kept on its own merits:

* `src/ui/settings_screen.cpp` drops the row from the Video page when the configured renderer list has
  fewer than two entries. It is *not* drawn as `Renderer: Unavailable` (that reads as a failure) and not
  left as a one-choice select, which is what `graphics.md` §10 originally specified — that entry is
  superseded.
* The state is not silent: `src/hooks/settings_payload.cpp` still logs the list, and says what happened to
  the row — `settings_menu: Renderer offers 1: D3D12 (the row is hidden: one backend is not a choice)`.
* Measured on this machine, where both backends are usable, nothing changes: the reinstalled copy logs
  `settings_menu: Renderer offers 2: D3D12, VULKAN`, creates the Vulkan device and presents a menu frame
  (`out/_swap/p1/installed2-vulkan.log`). The hidden case is pinned by `TestRendererRowVisibility`
  (`tests/settings_screen_tests.cpp`); `settings_screen` is 27,772 checks green and the test-capable tree
  is 38/38 (`out/build/rename-tests` — the Windows release preset cannot build a test at all, §"How the
  test tree stands").

#### Controller Settings cutover (P11b)

Items 4 and 10 are only honest if the in-game rows own their settings, so on the owner's decision
("Controller Settings stops passing game-owned settings; only paths/target/profile/controller flow"):

* `kGameOwnedRows` ([game_launch.cpp](../../controller-settings/src/game_launch.cpp)) — sixteen keys from
  `enhancements_dlc_cache`/`dlc_scan_threads` through `resolution_scale`, `fullscreen`, `vsync`,
  `gpu_backend`, `swap_post_effect`, `native_2x_msaa`, `anisotropic_override` and `present_letterbox`
  — no longer travels on the command line (the tabs still write them to the profile), and the target
  pair (`--ultimate_mode`/`--license_mask`) is replaced by migration of `[launch] target` on the first
  boot (only when it differs from the compiled default `common`), which keeps the in-game Target row
  editable.
* `profile_controller_rows_only` now defaults **true**
  ([profile_apply.cpp](../../src/profile/profile_apply.cpp)): the game applies the controller rows and
  the host-only host rows (monitor, safe areas, dither, overscan cutoff, two audio rows — no
  in-game descriptor) and logs the rest as inert preservation data. Removing those tabs is P8's.
* [capture_controller_settings.ps1](../../scripts/capture_controller_settings.ps1) and
  [acceptance_controller_settings.ps1](../../scripts/acceptance_controller_settings.ps1) assert the exact command line, and
  each leg deletes its fixture's `<profile stem>.game-settings.toml` first (the Ultimate leg's licence
  line reads `(configured)`).

#### Boot speed and one frozen picture (owner report, 2026-10-13, later)

* **The freeze** (seen once) is a GPU command-processor packet desync: the log ends with
  `ExecutePacketType3 overflow …` / `**** PRIMARY RINGBUFFER: Failed to execute packet.` and no frame
  is presented again. The session had the audit harness pressing keys through the boot and the start
  while discovery ran, and the title re-entered the A/V load three times (13–24 s apart); five later
  boots, two replaying that route, completed — a **rare desync**, not a settings bug
  ([known-issues.md](../reference/known-issues.md) has the signature).
* **The wait was the DLC discovery, not the A/V load** (~2 s on an empty library; ~6 minutes on the
  owner's 1,402 packages, because of the SDK's 100 ms emulated mount latency per completion, a full
  host walk every boot, and one-item-per-frame enumeration). All three are now fast by default:
  `enhancements_dlc_cache` **on**, `deferred_overlapped_delay_ms` **0 every boot**
  (`src/hooks/dlc.cpp`), `enhancements_dlc_uncap_enumeration` **on** — measured walk ~0.3 s and
  **39 s for 2,804 items** with the frame rate free.

#### Row descriptions — a caption for the highlighted row (owner request, 2026-10-13, latest)

While a row is highlighted, a one-or-two-line description appears at the bottom of the screen (white
text in a translucent black box) for **every** row — merged, Ultimate and the adopted retail pages
alike. It is an event, not a guess: the generated DTA publishes the highlighted row's canonical symbol
over the settings channel (`rbbz-settings/help/<symbol>/-`, the new one-way `help` op; `help/-/-` when
the page is left), so the caption cannot lag the highlight.

* **Our pages**: the text lives beside the row definition (~70 descriptions in
  [settings_screen.cpp](../../src/ui/settings_screen.cpp), as `screen::HelpTextFor`), authored into the
  generated tree (`HelpWrite`/`EnterHelpWrite`).
* **Retail and in-song pages**: [help_observer.cpp](../../src/hooks/help_observer.cpp) reads the
  frame's screen and focused list once per frame (state [native_menu.cpp](../../src/input/native_menu.cpp)
  already publishes) and resolves it against [help_topics.cpp](../../src/ui/help_topics.cpp); it clears
  only captions it published, so the guest's own entry event is never erased.
* **The overlay**: [help_overlay.cpp](../../src/ui/help_overlay.cpp), an SDK `ImGuiDialog` (the
  achievement toast's mechanism) bottom-centre, wrapped at 72% of the display size, with
  `settings_help_overlay` (default on) re-read every frame.

Evidence: `settings_screen` **26,518 checks** and `settings_bridge` **343 checks**, both exit 0.
**Not yet seen by a human**: the caption still has to be driven on a running screen.

#### How the test tree stands after the Vulkan preset change (measured 2026-10-13, later)

* **No Windows preset can build a test right now**: `REXGLUE_USE_VULKAN=ON` makes the SDK's glslang
  guard (`set(BUILD_TESTING OFF CACHE BOOL "" FORCE)` in
  [rexglue-sdk/thirdparty/CMakeLists.txt](../../rexglue-sdk/thirdparty/CMakeLists.txt)) write
  `BUILD_TESTING=OFF` into the cache, and the SDK is configured
  ([CMakeLists.txt](../../CMakeLists.txt) line 36) *before* `include(CTest)` (line 341), so **37/37
  cannot be reproduced from the presets**; a Vulkan-off tree configured with `-DBUILD_TESTING=ON`
  builds every target and runs **37/37**.
* **Three settings tests were stale about `general.cache_dlc_library` (fixed)**: the row defaults to
  true ([schema.cpp](../../src/settings/schema.cpp)) and a commit writing the value a row already
  holds is `kNoChanges` ([service.cpp](../../src/settings/service.cpp), line 549), so `settings_service`,
  `settings_migration` and `settings_bridge_service` now stage the row's *other* value (the migration
  case expects one more import). `settings_bridge_service` used to index an empty change list and
  **abort**; it now passes with 91 checks.
* **One flake**: `game_restart`'s child-wait case measured 0 ms once during a parallel build and
  passed 185/185 twice on an idle machine.

### 3. What is left, by package

#### P1 — retail screen + bridge feasibility

1. **The `read_file` response channel is not built.** The guest can be asked questions today;
   producing a *string*/*array* answer needs the guest DataNode build API and its allocator/refcount
   rules, which are untraced — the largest ABI risk left in the transport. *`architecture.md` §5,
   §10 step 5* — do not build it until `sub_824193A8`'s return path is traced and exercised with a
   disposable guest call.
2. **The row-widget binding is unstarted** — which `list_*.milo_xbox` row type draws each of the four
   row kinds. *`architecture.md` §10 step 4; `architecture.md` §9* — no step recorded.
3. **Appearance parity with Ultimate's own layout is a spike, not a result** — whether retail list
   resources can reproduce it without its custom MILO. *`plan.md` §2.2, §13*.

#### P2 — exact render-resolution feasibility

4. **No GPU capture**, and none taken since: it is the independent confirmation the instrumentation
   substitutes for. *`graphics.md` §7 ("Not done")*.
5. **Vulkan is instrumented but unproven** — every P2 measurement is D3D12; Vulkan is not even built
   here (`REXGLUE_USE_VULKAN=OFF`). *`graphics.md` §7; `graphics.md` §9*.
6. **A plugin-owned cvar cannot be set from `rb_blitz.toml` or the CLI** (config is applied before
   `rexgpu-xenos.dll` registers), which is why `resolution_scale` travels through the environment or a
   deferred write. *`graphics.md` §7* — P4/P11 own the route.

#### P4 — graphics capabilities, exact modes, first-run policy

7. **Variant B's `[[midasm_hook]]` is one coordinator entry away from proof**: `config/functions.toml`
   needs the entry at guest `0x823F382C` (`li r11,720` → 1080) plus the `RenderHeightHook` handler,
   then `out\_swap\p4\run-probe.ps1 -Label variantb` should show `guest_output=1920x1080`,
   `draw_scale=1x1`. *`graphics.md` §7, §9*.
8. **No host probe writes the capability snapshot** — the code that fills `CapabilityInputs` (logical
   processors, SDL display query, renderer build/detect/usable) does not exist; the interface is
   frozen in `capabilities.h`. *`graphics.md` §9; `architecture.md` §6* — coordinator, root
   `CMakeLists.txt`.
9. **The AA/FXAA visual matrix was never run**, so "what None/FXAA actually look like at 1080p" is
   unproved, and the AA row's labels rest on the policy, not on a capture. *`graphics.md` §9;
   `plan.md` §3.3*.
10. **No song was played at 1080p.** The 1080p evidence is the title screen and the draw stream; a
    song and a restart at 1080p are not captured. *`graphics.md` §9; `graphics.md` §5.2*.
11. **Graphics recovery is policy plus a marker shape, not a boot** — `RecoverGraphics` /
    `GraphicsBootMarker` are tested, but nothing writes the marker at boot yet. *`graphics.md` §9*.
12. **The per-backend AA/filtering ceiling matrix and the full menu/song/AA/restart matrix are
    untaken.** *`graphics.md` §5.2; `graphics.md` §9*.

#### P5b — the merge into Help & Options

13. **The in-song entry (§2).** §3.3's order: (a) narrow the panel-set limitation with one variant
    hosting retail's `controller_config_panel` beside the in-song panels; (b) then Back —
    `{ui pop_screen}` if the panels can be hosted, otherwise Cancel calling the resume retail's own
    `pause_panel` calls (`{game_screen resume}`); (c) only then re-point `pause_options` in the
    shipped payload. *`menu.md` §3.2, §3.3*.
14. **The in-song child pages are unverified** — General/Interface/etc. inside a song have never been
    seen on screen; they inherit whatever (a) decides. *same doc, §3.3*.
15. **G1b's other half is only partly proven for the *in-song* context** — "Back reaches the correct
    entry screen from both entries" is measured for the main menu only. *`plan.md` §10.4*.

#### P6 — Ultimate adapter and saved settings

16. **The achievements action is not wired.** `general.unlock_online_achievements` is a one-shot guest
    action; the bridge answers `invoke` without calling the provider and says so at boot. It must not
    be presented as working until it is. *`architecture.md` §6.1* — blocked by P1 item 1 (a
    host-initiated guest call needs the allocator/refcount contract).
17. **The combined payload's shadowed records.** P5b's generator edits base `ui/gen/ui.dtb` and
    `ui/splash/gen/splash.dtb`, while the payload also carries its own copies; a patch record built
    from the base ark would wipe the mod's edits. The record must be extended from the **mounted**
    payload. *`architecture.md` §6.2; `plan.md` §2.4, §11*.
18. **`forceplot` / `which_skybox` choice tokens are placeholders** — the screen's static list is not
    the payload's inventory. *`architecture.md` §6.2; `plan.md` §3.4*.
19. **`unlockall`'s write path does not exist** — its value belongs under native
    `[ultimate_overrides]`, which the bridge does not own; `EncodeProviderValues` skips it by design.
    *`architecture.md` §6.2; `architecture.md` §4, §6* — P11 owns it.
20. **Seven provider rows have no provider value** (no default, no file pair) and are reported absent
    — inventing one would overrule the payload. *`architecture.md` §6.2* — P11 decides whether the
    compiled default is seeded through the service.
21. **A merged install (mod files copied over the game root) is reported as absent** — it has no
    payload directory to inspect and no overlay to redirect through. *`architecture.md` §6.2*.
22. **The provider's second writer is unimplemented** — `ulti/gen/ulti_funcs.dtb`'s
    `ulti_settings_writer` writes a different shape to a path variable nothing inspected ever sets.
    *`architecture.md` §6.2*.
23. **A guest-side write through the redirect is unproven in a boot** (proven host-side and by
    construction). *`architecture.md` §5.2* — waits on the combined payload (item 17).
24. **The "verbatim" claim is not byte-exact for a nested-array value** (whitespace normalized); every
    measured file is atoms/symbols/strings/floats and stays byte-exact. *`architecture.md` §6.2*.

#### P7 — installer paths and terminology

25. **The wizard page needs eyes** — the two new rows' geometry was never looked at at 100/200/300 %
    DPI; fix `LayoutLocationFields` if the page overflows. *`menu.md` §5 item 1*.
26. **The updater leg was not run** — it needs the release manifest over the network, so "an update
    preserves an existing `paths.toml`" is unproven. *`menu.md` §5 item 2*.
27. **`settings::ResolvePathValue` decodes a stored path as ANSI** (`fs::path(value)`) while the
    helper writes `paths.toml` as UTF-8, so a non-ASCII save folder resolves differently in game vs
    installer. One-line fix: `fs::path(Widen(value))` in P3's file. *`menu.md` §5 item 3;
    `plan.md` §10.1 (P7)*.
28. **The acceptance scripts' output-directory assumptions are unre-checked** against the merged
    screen. *`menu.md` §5 item 4*.

**2026-10-14, after the owner's installer review** (not a numbered item: the numbers are the standing
list). The installer is administrative and defaults to `C:\Program Files\Rock Band Blitz`, so
`scripts/acceptance_controller_settings.ps1` now passes `/CURRENTUSER` to stay unattended; both folder
boxes default to `Documents\Rock Band Blitz\...` and cannot be left empty; the wizard creates two
shortcuts, both the game, so `startmenucontroller` / `controllericon` / `/LAUNCHERICON` /
`/CONTROLLERICON` are gone; and the mod page says how to add the mod later. Item 25 got *worse* rather
than better - the page gained a wider header gap, a wider status gap and a note label without the DPI
check it asks for ever having been run - and item 26 (the updater leg) now also has to answer a UAC
prompt, because the updater is administrative too. The narrowing of the legacy-profile prefill is in
[`../reference/known-issues.md`](../reference/known-issues.md); the new shape is in
`../../installer/README.md`.

#### P8 — Controller Settings reduction and Guide removal

29. **The full surface is still the default surface**; the reduced one is behind `--controller-only`,
    and the cutover is P11's. *`contracts.md` §9.3; `menu.md` (Status)*.
30. **The non-controller tabs/rows are not deleted** (the reduced surface merely draws none of them).
    *`menu.md` §1, §3.1; `plan.md` §10.1 (P8)*.
31. **The Guide filter is not runtime-proven** — the change is in the game executable, and the owner's
    running session held `rb_blitz.exe` open so it could not be relinked. *`menu.md` §4.5* — rebuild
    and run the filter.

#### P9 — hot apply and restart coordinator

32. ~~**The authored restart prompt does not exist**~~ — **built, and repaired 2026-10-14 (P3 of the
    installed-game review)**: `settings_restart_screen` exists, the merged root's Cancel asks
    `snapshot/restart.pending` to open it, and its *Restart now* row is `Kind::kRestart`. P3 fixed the
    two defects in the first version: the confirmation's Cancel returned to the **root it was leaving**
    (so Help & Options could not be left — reproduced in `out/_swap/p3/repro3-close/`, fixed in
    `after-close/`) and a refused restart left `restart.ok` true, which made the row silently inert.
    *`installed-game-fix-prompts.md` P3; `architecture.md` §6.1*. **Still open: only the main-menu
    context has been driven** — the in-song twin uses the same path but no boot has been taken from a
    song, and a *genuine* refusal (a child that cannot be started) is pinned by host tests rather than
    by a run.
33. **The Target row's soft restart is offered by leaving, not by the row** — the row commits and
    leaves; the prompt then appears when Help & Options is left, which is now a path the player can
    complete (item 32) — so item 32 unblocked this one in the practical sense, and the row still does
    not prompt by itself. *`installed-game-fix-prompts.md` P3; `plan.md` §3.1*.
34. ~~**`src/main.cpp` never calls `platform::RunBootstrap`, and nothing calls
    `RestartCoordinator::Request`.**~~ — **both wired** (P11a): `RestartBootstrap` runs before
    `wWinMain`, and the boot's restart handler drains the DLC refresh, requests the child on the UI
    thread and defers the close. Verified driven 2026-10-14 (`out/_swap/p3/after-restart/`): one child,
    the parent exits 0.2 s later, a new boot 0.4 s after that. *`architecture.md` §6.2*.
35. **The live apply still runs against P1's `MemorySink`**; it must move onto
    `Service::last_commit_changes()` now that the real store is wired. *`architecture.md` §6.3*.
36. **Coordinator limits to state honestly:** the parent/child handshake proves only that the spawn
    succeeded; `WaitForExit` is bounded at 30 s and a timeout refuses the start; a reused pid is
    refused by comparing creation time. *`architecture.md` §6.4*.

#### P10 — merged-menu compatibility

37. **Controller Settings-side rows are still there** — the retired rename toggle and the pool-size row live
    on in `controller-settings/config/settings.toml`, inert in game and removed from neither. *`menu.md` §7;
    `plan.md` §10.1 (P10)* — waits on P8/P11.
38. **The alternate in-menu refresh action is unbuilt** — hiding `splash_dlc` while it carries the
    refresh leaves the refresh with no main-menu row (the boot warns). *`menu.md` §7; `plan.md` §3.5*
    — a row under Downloadable Content calling the same sentinel.
39. **The descriptor list is never filled at runtime** — `MenuInventory` is tested but uncalled, so
    the Hide Items menu has never drawn from the live menu. *`menu.md` §7; `architecture.md` §6* —
    the caller must hold `menu_filter.cpp`'s read buffer; P11 wires it.
40. **A 50-package pool has never been booted** since the cap moved from 3 to 50, so package-handle
    lifetime at that size is unproven. *`menu.md` §7; `plan.md` §10.1 (P10)*.

#### P11 — integration and default cutover

41. **P11b is the whole remaining cutover** — the new path becomes the default, the old full surface
    UI goes, packaging/patch-record generation, and consistent boot/settings/installer behaviour.
    *`plan.md` §10.1 (P11), §13* — P5b–P10 first.
42. **The merged root's delivery is not packaged**: the patch record must be generated by extending an
    installed retail record, and the list resource must survive a base-only install with no Ultimate
    folder. *`plan.md` §11 (P11)* — blocked by item 17.
43. **Resolution and Anti-Aliasing are stored but not applied at boot**, `unlock_all` is not written,
    and the prompt never issues a restart request — the boot log says each of these out loud.
    *`architecture.md` §4, §6* — unblocked by items 7, 8, 19, 32.

#### P12 — release acceptance and documentation

44. **Nothing in the acceptance section is marked passed.** The §12.1 host matrix and the §12.2
    runtime/installation matrix (merged root, hot vs restart rows, exact graphics, Ultimate
    persistence, restart A/B, installer, Controller Settings, portable, integrity) are all unrun.
    *`plan.md` §12* — blocked by P11; the doc already fixes the record shape (command, build/config
    identity, expected/actual, evidence path).

#### Cross-cutting, not owned by a package

45. **Trial disposition** — prior work reports Trial crashing before menu entry; it must be
    reproduced, recorded, and either repaired or kept unavailable, never silently shipped.
    *`plan.md` §3.1, §13; `contracts.md` §12*.
46. **Locales** — the existing transforms are English-specific; project-owned keys need English
    fallbacks and the supported locales need a run with no missing-token crash. *`plan.md` §13*.
47. **Three placement decisions to confirm** (each has a documented default): the **Renderer** row
    (omit from the tree, or keep in Video as `restart`), **Overscan** (moved into Video; record the
    deviation if the retail page cannot lose it cleanly), and the **achievements action's** placement
    (keep both in the same submenu). *`plan.md` §3.7* — owner decision.
48. **AA translation on both backends, and the Disable-Plots vs Force-Plot precedence** — to be
    verified against provider behaviour before any different rule. *`plan.md` §3.3, §3.4*.
49. **The first-run recommendation policy is still policy, not a measurement** — a versioned policy
    backed by repeatable measurements is what makes the feature finished. *`plan.md` §7.4*.

### Remaining acceptance work

1. **Run the final acceptance matrix when requested.** The suites are **37/37 green** (2026-10-13,
   later) in the Vulkan-off Debug tree the note above describes — captions and the three repaired
   settings tests included — but cannot be measured from the presets as they stand. What has not
   happened is the *interactive* pass: no one has driven the merged menu since the punch list, so
   items 2, 3, 6/7 and 10 are asserted by tests and records, not by a person at the screen. The matrix
   still needs both base and Ultimate launches, settings save and direct-boot readback, restart A/B,
   target change through the in-game row, and the 1080p path.
2. **Graphics recovery is not wired.** The first-run and last-known-good policy functions exist, but
   boot does not maintain a durable "attempted/presented" marker, so a failed graphics boot cannot
   automatically restore the safe 720p/no-AA combination.
3. **Release checks remain.** Packaged archive generation against base-only and Ultimate installs,
   installer path handling, Controller Settings launch/update behavior, and the no-copied-content
   constraint on the final distributable — integration checks, not blockers to the early settings
   load path.
4. ~~**Refresh-row edge case.**~~ **Superseded 2026-10-13**: the refresh action is the main menu's
   *Refresh Music Library* row now (owner decision), not a settings row, so "hiding `splash_dlc` still
   leaves the settings-side refresh reachable" is no longer a question — action and row are the same
   row. What still needs a populated-library look is that the row refreshes the library after the
   *DLC Menu Songs* retirement — **superseded 2026-10-14**: the row is no longer retired (§5), so the
   look is owed on a boot whose DLC page shows both rows
   ([play-current-song.md](../plans/in-progress/2026-10-14-play-current-song.md)).

### Boot ownership

| When | Work |
| --- | --- |
| `OnConfigurePaths` | Resolve identity and path roots; migrate/load the store; apply target, renderer selection, and native settings. |
| `OnPreSetup` | Load the selected GPU plugin, apply its saved flags, plan the title guest mode and draw scale, and set the guest-height hook before graphics setup. |
| `OnPostLoadXexImage` | Inspect the mounted provider, install the generated menu archive, configure the bridge, and attach restart handling. |
| Guest menu interaction | Read/edit settings through the typed channel; commit durably before acknowledging; invoke live VSync or offer restart for restart-scoped changes. |

The retail "Loading Rock Band Blitz audio/video settings…" string belongs to the game's own menu
flow. This feature does not introduce an A-button-time settings-load screen: the native store is
already loaded before game processing begins.

For historical experiments and acceptance scripts, see [plan.md](plan.md), [menu.md](menu.md),
[graphics.md](graphics.md) and [architecture.md](architecture.md).

## Part 2 — Session status, 2026-10-12 (was `docs/plans/in-game-settings-status-2026-10-12.md`)

Supersedes nothing; complements the handover in Part 3. That document's ledger is still accurate
except where this one says otherwise.

**Headline: the black startup was a two-part archive.** The generated Help & Options payload grew the
title's patch archive by *adding a second part*, and no archive this title ships or reads has ever had
more than one. The guest then re-read the generated part forever. Extending the mounted part in place
instead — one part in, one part out — fixes it: **an Ultimate install now boots and opens the merged
Help & Options screen with the provider active.**

### What this session changed (working tree, uncommitted)

| File | Change |
| --- | --- |
| `src/ui/settings_screen.cpp` | `RequestPaths()` lists `rbbz-settings/cancel/-/-` (fixes `settings_screen`). |
| `tests/controller_settings_general_tests.cpp` | `Scratch::Make()` returns `.lexically_normal()` (fixes `controller_settings_general`). |
| `src/hooks/menu_filter.cpp` | `HandleRead()` captures `length`/`buffer`/`offset_ptr` **before** the original `NtReadFile` (caller-saved registers; the scan is linear in `length`). |
| `src/settings/menu_payload.{h,cpp}` | `ExtendMenuArchive()` carries the mounted part's bytes verbatim, appends our records and keeps **one part** (same name, new size, existing offsets unchanged); with no mounted part it appends a part, as before. |
| `src/hooks/settings_payload.cpp` | Reads the mounted part through the mount view; writes the extended part where the archive names it. |
| `tests/menu_payload_tests.cpp` + `CMakeLists.txt` | New `menu_payload` ctest (35 checks): one part in/out, verbatim carry, every record inside its part, mismatched sizes left alone — parsing the header with the product's own v6 reader. |

`ctest` is **36/36 green** on the current sources, and the game and Controller Settings link.

### The black startup: cause and fix

#### Reproduction (disposable, no real profile touched)

`out/_swap/settings-audit/stall-probe.ps1` boots the current build against a fresh
`--user_data_root`, an **empty pinned** `--launcher_profile`, and empty DLC roots, then samples the
game's threads once the log goes quiet. `boot-check.ps1` is the same shape with window captures, and
`menu-check.ps1` drives the merged screen.

```powershell
pwsh -File out\_swap\settings-audit\stall-probe.ps1 -Target ultimate -Resolution 1280x720 -Label x
pwsh -File out\_swap\settings-audit\menu-check.ps1  -Target ultimate
```

**`--launcher_profile` is not optional in a disposable arm.** Without it the boot loads the
developer's own `%APPDATA%\rb_blitz\launcher.toml`, whose save-location row outranks
`--user_data_root` — the first run here read *and rewrote* the real `settings.toml` (a saved
resolution switched to Auto; the file is intact otherwise, and Auto resolves to the same pixels on
this display). Always pin an empty profile.

#### The isolation

| Arm | Result |
| --- | --- |
| base, 720p and Auto | presents the title screen, ~53 s |
| Ultimate, 720p and Auto | never presents; kills at 200–300 s |
| Ultimate, all four menu-filter toggles off | still never presents |
| Ultimate, provider-row reroute disabled | still never presents |
| Ultimate, each edited record left installed in turn | still never presents |
| Ultimate with the menu payload install skipped | presents in 47 s |
| **Ultimate with the payload installed as one extended part** | **presents in 50 s, merged root opens** |

So the install step was the cause and the part count the defect inside it. The base game installs the
same 14 records over an empty provider index — one part either way — and was never affected.

#### What the guest does while it hangs

The guest thread spins at ~85% of a core while it hangs. Resolving sampled RIPs against a link map
(`/MAP`, added temporarily and reverted; helper `out/_swap/settings-audit/resolve_map.py`) put the loop
inside our own `rb_blitz::menu_options::LooksLikeDtb`, called from `menu_filter`'s `HandleRead`;
instrumenting the hook showed the shape: **~540,000 reads, ~65 KiB each, 34 GB scanned**, one call
site, one file handle and a *constant* byte offset per arm (`0`, `1114112` = `0x110000`, inside the
generated `settings_xbox_0.ark`, 1,138,458 bytes). The title re-reads one record of the extended
archive forever and never reaches `VdSetInterruptCallback`. Two separate things follow:

1. **The guest's retry loop is the bug** — Ultimate-only, and it appeared with the settings payload.
2. **Our scan multiplies it** — `HandleRead` tests every byte with a non-inlined call, ~0.7 ms per
   64 KiB read: invisible in a healthy boot, fatal under a retry loop, which is why the symptom
   looked like a total hang rather than a slow boot.

#### Why one part

Every archive this title reads declares exactly one part (measured 2026-10-12): `main_xbox.hdr` is
`ARK v6, 1 part(s), 1613 entries` (`main_xbox_0.ark`, 361,769,177 bytes) and the provider's
`patch_xbox.hdr` is `ARK v6, 1 part(s), 62 entries` (`patch_xbox_0.ark`, 14,171,207 bytes) —
`python scripts/hmx_ark.py list --hdr <file>` prints both. The runtime's reader
(`settings/ultimate_adapter.cpp`) and the offline tool both implement the multi-part *layout*
correctly, and every count echoed in our header was well-formed, so the defect was never a malformed
header: the title's patch loader does not follow a second part, and a boot that offers it one cannot
finish. The base-mode payload (`out/_swap/p5b/make_payload.py`) had always written a one-part archive;
only the runtime extension introduced a second. After the change:

```
cache/settings-menu/<pid>/gen/patch_xbox.hdr    ARK v6, 1 part(s), 73 entries
cache/settings-menu/<pid>/gen/patch_xbox_0.ark  15309674 bytes   (14171207 mounted + 1138467 ours)
```

73 rather than 76 because three of our records already exist in the provider's payload and are replaced
in place; every record fits inside the single part, which is what the new test pins.

#### Not the fix (kept as hardening)

`HandleRead()` used to read `ctx.r8/r9/r10` *after* calling the original `NtReadFile` — caller-saved
registers, and the scan is quadratic in `length`, so a stale `0xFFFFFFFF` there is a
four-billion-iteration loop. Capturing them before the call is correct regardless, but it is not what
fixed this boot (the hang was unchanged, and the measured reads were a legitimate 64 KiB each).

### What is verified now

* 36/36 ctest on the current sources; `rb_blitz` and `rb_blitz_controller_settings` link.
* **Base mode** presents in ~54 s with the payload installed
  (`settings_menu: mounted 14 generated records over 0 provider records`).
* **Ultimate mode** presents in ~50 s over the provider's own archive
  (`mounted 14 generated records over 62 provider records in 411 ms`), the provider still reported as
  `6/6 records, 8 panels, 10 values` and the bridge live (`typed guest channel on`,
  `provider active - 9 row value(s) seeded`).
* **The merged Help & Options screen opens in Ultimate**: from the title screen, `HELP & OPTIONS`
  lands on the merged root with all six rows — General, Controls, Calibration, Audio/Video, Gameplay,
  Interface — General selected (`out/_swap/settings-audit/menu-ultimate/shots/04-merged-root.png`);
  moving down and pressing A enters the page and Back is offered (`05-child.png`: retail's
  `CONTROLLER PRESETS`, which is what the Controls row reaches).
* The early-resolution path still runs before graphics setup (`resolution_scale`, `native_2x_msaa`,
  `swap_post_effect`, `anisotropic_override` written pre-presentation; the graphics-boot marker ends
  `pending = false` with `last_good` filled in), and the safe-graphics route and recovery marker
  behave as Part 3 describes.

### What is NOT verified (and is the remaining work)

Ordered by what blocks a release:

1. **The interactive matrix** — nothing has been driven in a running game in either target:
   horizontal-selector editing and the editing indicator, boolean save, live VSync, the restart
   prompt's close/accept/failure paths, target changes, direct-boot readback, provider saves,
   Unlock All timing, and in-song navigation through every child screen and back. `menu-check.ps1` is
   the template, `scripts/drive_ui.ps1` + `capture_window.ps1` the tools.
2. **In-song entry** — the pause context is the one P5b measured as not re-pointable; the pause record
   is still left pristine.
3. **Restart input guarding** — the prompt defaults to Close, not the planned A=restart / B=close with
   a release guard.
4. **Auto vs restart-needed semantics** — `requested Auto` is `0x0` while the boot-effective size is
   non-zero, so a naive comparison can mark Auto as needing a restart; review with the guard code.
5. **Provider transaction/recovery audit** — same-key conflict checking, merged installs,
   inactive-provider detection, and whether the production bridge consults
   `Service::RequestRestart()`.
6. **Safe-graphics recovery with an induced failure** (no endless retries, no preference destruction,
   no false recovery after an ordinary crash or a clean close).
7. **Product cleanup** — Controller Settings cutover and dead UI/schema, labels/localisation,
   hidden-DLC refresh access, installer/distribution checks; the ~15 MB generated part written into
   the cache root per boot is deliberate (no retail bytes shipped) but belongs in a performance pass.
8. **Docs** — the tracker still carried claims this session contradicts (it said graphics recovery
   "is not wired"; it is).

### Housekeeping

* The real `%APPDATA%\rb_blitz\settings.toml` was touched by one early misconfigured arm: a saved
  manual resolution was switched to `auto` with automatic provenance and the
  `.graphics-boot.toml`/lock siblings were created. On this 2560x1080 display Auto resolves to the
  same pixels; set it back by hand if a manual entry is wanted.
* Temporary investigation code (a `/MAP` link option, the `RBBZ_SKIP_*` gates, the read/open
  diagnostics) was **removed**. The two ctest fixes and the `HandleRead` capture are committed as
  `460c01e`; the part-count fix, the new `menu_payload` test and the corrections to these docs are
  the uncommitted diff.
* Scratch: `out/_swap/settings-audit/` (one directory per arm with its log and captures). Nothing
  here is acceptance evidence.

## Part 3 — Handover, 2026-10-12 (was `docs/plans/in-game-settings-handover-2026-10-12.md`)

**This part is superseded by Part 1 wherever the two disagree** (the two ctests it lists as failing
were fixed, the Ultimate boot hang was fixed at its cause, and the merged screen now opens). Its
still-true content — the owner's intent, the operational rules, the verification limitations and the
order to continue in — is kept below.

**The feature is unfinished. Do not report release acceptance or a clean test suite.** The user
explicitly requested this handover because the session was running low on credits. Stop
implementation for this handover; the next agent should resume from the working tree.

### User intent and latest decisions

The original request was to finish the in-game settings feature described by the planning MDs and make
startup-critical settings apply before game processing; the user noticed the retail "Loading settings"
message *after* pressing A and questioned whether resolution was applied too late. This audit found
substantial missing wiring and bugs. The user then specified the resolution behavior, superseding the
old benchmark requirement:

> Default resolution is Auto. At startup, determine the best resolution for the current display and
> apply it immediately. Respect a compatible manual choice on later boots. If that choice becomes
> incompatible (for example, 4K saved, now a 1080p display), switch back to Auto.

No GPU benchmark is wanted — a display compatibility policy, not a performance claim — and manual
graphics preferences still survive temporary recovery/fallback. Another session's ultrawide work in
this workspace must be **preserved**, and edits were committed/staged while this audit ran: do not
reset, unstage, or replace the working tree from an older commit.

### Workspace and operational details

- Repository: `<checkout>`; PowerShell
  (`exec_command` with `login: false` avoids a slow shell startup).
- HEAD at handover: `a3fd698` (ultrawide resolution/diagnostics), preceded by `ebd7c27` and `6df0359`
  (graphics boot management/recovery) — recheck before editing, HEAD moved externally.
- Tests are authorized for this task (an old "only test when requested" sentence in the tracker is
  stale). No applicable AGENTS.md was found; do not spawn subagents without explicit authorization.
- Never use the real save/profile for testing; scratch lives in `out/_swap/settings-audit/`. Game data
  is `game/`, the separate Ultimate payload `game/ultimate/`. Both test processes this session
  launched (22600, 20760) were stopped — check processes before a new run and stop only your own.
- Do not edit generated recompiled sources; production hooks belong in handwritten sources/config.

### What changed in this audit

This audit's per-area changes (early boot / resolution / recovery, persistence and service, UI and
provider integration) are the implementation state Part 1 records; the file-level detail is in the
source documents consolidated as [architecture.md](architecture.md) and [menu.md](menu.md). The
still-true points Part 1 does not carry: preferences load in `OnConfigurePaths` and plugin flags in
`OnPreSetup`; `FinishInit()` moved before menu authoring and now scans rows without a saved preference,
decoding compound target/resolution/filtering/AA cvars as groups (**needs runtime checking**); Auto is
a zero-size `Value` on disk as `resolution = 'auto'`, resolved to the largest supported size that fits
the display with an incompatible manual choice converted and persisted under automatic provenance;
first-run policy version 3 (no benchmark); `.graphics-boot.toml` carries last-good graphics plus a
lifetime lock. **Runtime-unverified**: the nonblack `CaptureGuestOutput()` success poll
(blocking/threading), `--settings_safe_graphics=true`, the partial-save/replay/revision service test,
and the store edit rejecting nonpositive/overflowing resolution arrays. **Needs an audit**:
interruption/concurrent-writer recovery, provider same-key conflict checking, and whether the
production bridge consults `Service::RequestRestart()`.

### Verification state — exact limitations

#### Build and host tests

Build directory `out/build/win-amd64-release`:

```powershell
cmake --build out/build/win-amd64-release --config Release -j 4
ctest --test-dir out/build/win-amd64-release -C Release --output-on-failure
```

The game and Controller Settings built successfully, but **the source is newer than that build** (last service
test, store validation, live-apply changes). One intervening link failed only because the test game
still held the executable open — not a compile error. The toolchain warns that installed Clang
19.1.7/CMake 4.4.0 differ from frozen 23.1.1/4.4.3; the build allows this and **no strict-toolchain
pass is claimed**.

Latest full CTest at handover: **33/35** — `settings_screen` (`RequestPaths()` omitted the newly
emitted `rbbz-settings/cancel/-/-`) and `controller_settings_general` (the deeply nested empty-root fixture's
mixed separators, `bare/a/b/...` vs backslashes); both were fixed on 2026-10-12 (Part 2). The earlier
`path_policy` failure was sandbox temporary-path containment, not a product bug; migration now expects
legacy explicit 720p to be imported because new profiles default to Auto. Logs:
`out/_swap/settings-audit/{build,tests}.log` (overwritten by later runs).

#### Runtime checks did NOT pass yet

Native settings read/application **did** happen early (the marker recorded `resolution = 'auto'`
resolving to **2560x1080** before graphics setup on this display), which proves the selection path ran
but **not** correct visible rendering. The latest screenshot, `runtime-ui/menu.png`, is a **black game
window** and the log was still around Ultimate overlay initialization, so investigate whether startup
is stalled, slow or failing — do not assume a successful launch or blame the UI edits without
isolating it. Compare base vs Ultimate, Auto vs explicit 720p and the last working build; then check
provider attachment/journal locking and the new presentation probe; capture debugger stacks/logs if
necessary. (The black window was later explained by the two-part archive — Part 2.)

**Order to continue in:** resume from the working tree rather than a clean checkout, settle the runtime
checks above first (base vs Ultimate, Auto vs explicit 720p, the last working build), then the
provider-attachment/journal-locking review and the presentation probe. The terminated test processes
left their graphics markers pending, so use a fresh scratch profile or account for the recovery.

Windows automation details: a sandbox-launched game lived on a different desktop (escalated automation
could not see its SDL window; sandbox automation could not foreground it), so drive it on the
interactive desktop with scratch profile/user/cache/log paths and `--mnk_mode=true
--fullscreen=false`; Windows mistook an IME overlay for `Process.MainWindowHandle`, so
`drive_ui.ps1`/`capture_window.ps1` enumerate the process's `SDL_app` window, and `drive_ui.ps1` maps
A to Space, B to Backspace and navigation to `lstick_*` (it picks the first rb_blitz process — run only
one test game).

## Sources

| Part | Original document | Date | Status |
| --- | --- | --- | --- |
| 1 | `docs/plans/in-game-settings-remaining-work.md` | 2026-10-13 | Live tracker; the eleven punch-list items and the 49-item ledger are kept intact |
| 2 | `docs/plans/in-game-settings-status-2026-10-12.md` | 2026-10-12 | History; superseded where Part 1 says otherwise |
| 3 | `docs/plans/in-game-settings-handover-2026-10-12.md` | 2026-10-12 | History; superseded by Part 1 where they disagree |

Part 1's numbered ledger ("What is left, by package", items 1–49) is the open-item list from the
preceding revision of `docs/settings/status.md (part 1, current status)` (2026-10-12), whose numbers are
cited by other documents (item 13 is the in-song entry); the current revision of that path carries the
rest of Part 1.
