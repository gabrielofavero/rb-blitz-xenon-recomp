# In-game settings — current status and remaining work

> **Update 2026-10-13 (latest):** the owner drove the merged Help & Options surface in a running
> game and gave an eleven-item punch list; all of it is implemented, and so is the launcher cutover
> it required (P11b) and the retirement of the second main-menu entry to the mod's own screen. The
> release tree is **37/37 green** (`ctest --test-dir out\build\win-amd64-release`). See
> [§ Owner review punch list](#owner-review-punch-list-2026-10-13) below; the interactive acceptance
> matrix has still not been re-run.
>
> **Two handovers, both current (2026-10-12):** [the implementation handover](in-game-settings-handover-2026-10-12.md)
> has the changes, the Auto-resolution decision, and the runtime blockers;
> **[the evening status](in-game-settings-status-2026-10-12.md) explains the black startup and what
> this session changed** — the two failing ctests are fixed (36/36 green), the Ultimate boot hang is
> fixed at its cause (the generated payload was making a two-part archive, which this title cannot
> read), and the merged Help & Options screen now opens with the provider active. Read that one
> second and last.
>
> Statements below that this session measured to be wrong: graphics recovery **is** wired (the
> marker, the fallback and safe mode all ran); the Ultimate boot **does** present and **does**
> carry the merged root; and the interactive matrix has still not been driven.
> The feature has not passed final acceptance.

**Updated 2026-10-13.** This status supersedes the older open-item ledger below in its previous
revision and the dated “open” statements in the handoff documents. Those documents retain the
research history; use this file for the current implementation state.

## Implemented

The settings feature is wired through the real boot and guest UI:

* Native settings are read from `settings.toml` during `OnConfigurePaths`, before the GPU plugin,
  title data, and guest rendering begin. The small path file and legacy migration are handled in the
  same early stage.
* Renderer selection is chosen before the GPU plugin loads. After the plugin registers its cvars,
  VSync, filtering, anti-aliasing, and the resolution scale are applied in `OnPreSetup`, before
  presentation and render resources are initialized. This is not work deferred until the player
  presses A on the menu.
* Resolution uses the measured 960×540 or 1280×720 title guest modes multiplied by an integer
  backend draw scale. In particular, 1920×1080 is 960×540 at 2×. The experimental native 1080p
  guest-height route was removed because it corrupted title-screen artwork before the menu.
* The generated Help & Options tree works from the main menu and the in-song pause route. The
  in-song construction-order fault was fixed by deferring those screens until the pause panels are
  available; the in-song root, child pages, Back path, and resume path were exercised in a running
  game.
* The menu payload extends the mounted effective archive, so Ultimate-shadowed records are retained.
  Provider choices come from the installed payload, its settings file is redirected into user
  storage, `unlockall` is stored by the native service, and the achievements action invokes the
  provider action.
* Save errors, restart confirmation, and restart coordination are connected. The child command
  builder no longer duplicates the game-data-root argument. A previous launch exposed that defect;
  it has been corrected.

## Owner review punch list (2026-10-13)

The owner drove the merged surface in a running game (Ultimate installed, live DLC library) and
gave eleven changes. All eleven are in `src/ui/settings_screen.{h,cpp}` and the records it edits,
with the game-side and launcher-side consequences listed after them:

1. **The main-menu entry to the mod's own screen is gone.** "Help & Options is enough": with a
   compatible provider installed, the payload's own `mod_settings` row is *removed* from the splash
   root (`RemoveRow`, [menu_payload.cpp](../../src/settings/menu_payload.cpp)) instead of being
   rerouted, so the only way to the provider's pages is the merged Help & Options tree. A payload
   that never had the row refuses the removal and that refusal is not fatal.
2. **A horizontal select advances and wraps.** A (`kAction_Confirm`) on a row whose widget is
   `kHorizontalSelect` moves to the next choice and wraps to the first after the last
   (`(index + 1) % choices`), commits it through the bridge, and shows the new value —
   the arrows keep their own step-and-commit behaviour.
3. **The restart prompt only opens when the player leaves Help & Options.** The root screen's
   Cancel checks the bridge's sticky `restart.pending` flag, which is set when a commit succeeded
   and the row needs a restart ([bridge_sink.cpp](../../src/settings/bridge_sink.cpp)); a change
   that needs one no longer interrupts the player mid-edit, and leaving with nothing pending does
   not ask.
4. **Target lists the demo by name.** "Rock Band Blitz Demo" is a choice again
   ([settings_payload.cpp](../../src/hooks/settings_payload.cpp)); the recorded trial-boot fault is
   a separate matter, and the launcher's radio says the same thing now
   ([general_tab.cpp](../../launcher/src/general_tab.cpp)).
5. **The DLC page and the refresh action.** *Cache DLC Library* reads `On`/`Off` instead of the
   "Externally configured" marker it used to show — the launcher's `--enhancements_dlc_cache` row
   outranked it, which made it read-only, and the cutover below removed that flag; *DLC Menu Songs*
   is **retired** — the menu music pool always takes what the library yields, so there is no row
   (`SettingId::kDlcMenuSongs` is kept as `Availability::kHidden` so old files and bridge tokens
   still resolve, and R9 is always on); and *Refresh Downloadable Content* left the settings tree to
   become **Refresh Music Library** on the main menu.
6. **Boolean rows read On / Off, not the locale's ENABLED / DISABLED.** The payload overrides the
   two locale tokens the retail checkboxes use (`LocaleOverrides()`), per language, without
   touching any other string.
7. **Audio & Video's two banners became suffixes.** Overscan and Bass Boost read `Overscan: On` /
   `Bass Boost: On` — the `| ENABLED` spelling is gone — by the same locale-override mechanism plus
   the label rule that appends `: On`/`: Off`.
8. **Gameplay regrouped**: Camera Angle, Game Speed, **Tracks & Gems** (Precision Mode, Disable
   Random Track Wiggles, Gems), Plots & Skyboxes.
9. **Depth of Field** moved to Audio & Video → Video → Graphics (it is a graphics row, not a
   gameplay one).
10. **A Display row exists** (Windowed / Borderless / Full Screen) — a new setting
    (`SettingId::kDisplay`), a new SDK cvar and the third window mode
    ([patch 0016](../../patches/README.md)), applied through `DisplayToCvars`.
11. **Video regrouped**: the Display group (Display, Resolution, V-Sync), the Graphics group
    (Anti-Aliasing, Anisotropic Filtering, Depth of Field), then Renderer.

### Renderer: Vulkan is an option now (owner request, 2026-10-13, later the same day)

The owner asked for Vulkan to be selectable. Two things were missing and both are in:

* **The build**: `REXGLUE_USE_VULKAN=ON` is in the Windows presets (`windows-base`). The SDK's own
default on Windows is **OFF**, so the tree this feature was measured on had a D3D12-only
`rexgpu-xenos.dll` — every *Renderer* row in the product (the launcher's and the game's) therefore
offered one choice, however many the code could describe.
* **The game's own row**: the in-game *Renderer* row read a capability snapshot that hardcoded one
backend (`"not probed: the one backend this project ships"`). The boot now builds one probe per
compiled backend, from `RBBLITZ_BACKENDS` ([CMakeLists.txt](../../CMakeLists.txt), the same two
`REXGLUE_USE_*` switches the plugin is built from and the launcher's `--backends=` reads), and asks
the machine about Vulkan the way a boot does: is the loader (`vulkan-1.dll`, opened by the SDK at
run time) there at all. D3D12 stays the default/active entry, so nothing switches by itself —
Vulkan is offered, not recommended.

At run time a missing loader is handled on both sides: the row is not offered, and
`--gpu_backend=vulkan`/a stored `vulkan` choice refuses and boots D3D12
(`RbBlitzApp::SelectGpuBackend`).
**Answered later the same day (owner decision): the payload carries the loader.** A machine with a
GPU driver but no `vulkan-1.dll` would have been offered Vulkan and then failed inside presentation
setup, so `installer/tools/make_payload.ps1` now ships a copy whenever the build offers Vulkan —
`-VulkanLoaderDir` pins the source, otherwise `$env:VULKAN_SDK\Bin`, `C:\VulkanSDK\*\Bin` (newest
first) and `$env:WINDIR\System32` are tried in that order, and a miss is fatal unless
`-AllowMissingVulkanLoader` is passed. Measured on this machine: a 9-file snapshot with
`vulkan-1.dll  1,452,408 bytes` and `payload renderer backends: d3d12, vulkan`. The two surfaces were
measured too — the game logs `settings_menu: Renderer offers 2: D3D12, VULKAN` and a boot with
`REX_GPU_BACKEND=vulkan` presents a menu frame with `Vulkan device 'NVIDIA GeForce GTX 970'` in the
log; the launcher's row comes from the same two switches by way of `--backends=`.

### The launcher cutover (P11b)

Items 4 and 10 are only honest if the in-game rows own their settings, and a launcher-passed flag
is rank 1 — the row would have read as permanently external. So the launcher cut over on the
owner's decision ("Launcher stops passing game-owned settings (DLC, graphics, interface); only
paths/target/profile/controller flow"):

* `kGameOwnedRows` ([game_launch.cpp](../../launcher/src/game_launch.cpp)) — sixteen keys, from
  `enhancements_dlc_cache` and `dlc_scan_threads` through `resolution_scale`, `fullscreen`,
  `vsync`, `gpu_backend`, `swap_post_effect`, `native_2x_msaa`, `anisotropic_override` and
  `present_letterbox` — is skipped when the command line is built. They are still written to the
  profile by the launcher's tabs; they simply do not travel.
* The **target** pair is gone from the command line as well (`--ultimate_mode` /
  `--license_mask`): the profile's `[launch] target` is imported into the game's own settings on
  the first boot instead (migration, and only when it differs from the compiled default `common`),
  which is what keeps the in-game Target row editable.
* `profile_controller_rows_only` now defaults **true**
  ([profile_apply.cpp](../../src/launcher/profile_apply.cpp)): the game applies the controller rows
  (`mouse_ui_nav`, `input_backend`) and the **launcher-only host rows** — the monitor pick, the two
  safe areas, dither, the overscan cutoff and the two audio rows, none of which the in-game tree can
  express (they have no descriptor at all) — and logs everything else as inert preservation data, so
  a launcher tab row no longer outranks the game's own file. Removing those tabs is P8's Controller
  Settings, not this cutover.
* The exact command line is asserted by
  [capture_launcher.ps1](../../scripts/capture_launcher.ps1) and
  [acceptance_launcher.ps1](../../scripts/acceptance_launcher.ps1): no target flags, no
  game-owned row, and `--dlc_root` / `--user_data_root` still travel. Each acceptance leg deletes
  the `<profile stem>.game-settings.toml` beside its fixture first, so the target migration is
  exercised per leg rather than claimed by a file from an earlier run; the Ultimate leg's licence
  line therefore reads `(configured)`, not `(default; the title is treated as purchased)`.

## Remaining acceptance work

1. **Run the final acceptance matrix when requested.** The unit and integration suites are
   **37/37 green** as of 2026-10-13 (release preset), including the rewritten UI expectations of the
   punch list above. What has not happened is the *interactive* pass over the new surface: no one
   has driven the merged menu since the punch list landed, so items 2 (A advancing and wrapping),
   3 (the restart prompt only on leaving), 6/7 (On/Off and `: On` in every language) and 10
   (Borderless) are asserted by tests and by the payload's own records, not yet by a person looking
   at the screen. The final matrix still needs both base and Ultimate launches, settings save and
   direct-boot readback, restart A/B, target change through the in-game row, and the 1080p path.
2. **Graphics recovery is not wired.** The policy functions for first-run recommendations and
   last-known-good recovery exist, but boot does not yet maintain a durable “attempted/presented”
   marker. A failed graphics boot therefore cannot automatically restore the safe 720p/no-AA
   combination.
3. **Release checks remain.** Recheck packaged archive generation against base-only and Ultimate
   installs, installer path handling, Controller Settings launch/update behavior, and the no-copied-
   content constraint on the final distributable. These are integration checks, not blockers to the
   early settings load path.
4. ~~**Refresh-row edge case.**~~ **Superseded 2026-10-13**: the refresh action is the main
   menu's *Refresh Music Library* row now (owner decision), not a settings row, so "hiding
   `splash_dlc` still leaves the settings-side refresh reachable" is no longer a question — the
   action and the row it is drawn on are the same row. What does need a populated-library look is
   that the row still refreshes the library after the retirement of the *DLC Menu Songs* row.

## Boot ownership

| When | Work |
| --- | --- |
| `OnConfigurePaths` | Resolve identity and path roots; migrate/load the store; apply target, renderer selection, and native settings. |
| `OnPreSetup` | Load the selected GPU plugin, apply its saved flags, plan the title guest mode and draw scale, and set the guest-height hook before graphics setup. |
| `OnPostLoadXexImage` | Inspect the mounted provider, install the generated menu archive, configure the bridge, and attach restart handling. |
| Guest menu interaction | Read/edit settings through the typed channel; commit durably before acknowledging; invoke live VSync or offer restart for restart-scoped changes. |

The retail “Loading Rock Band Blitz audio/video settings…” string belongs to the game's own menu
flow. This feature does not introduce an A-button-time settings-load screen: the native store is
already loaded before game processing begins.

For historical experiments and acceptance scripts, see [the main plan](in-game-settings-plan.md),
[merged-menu notes](in-game-settings-merged-menu.md), [graphics findings](in-game-settings-graphics-findings.md),
and [boot-wiring notes](in-game-settings-boot-wiring.md).
