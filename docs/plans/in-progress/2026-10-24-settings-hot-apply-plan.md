# Expand settings hot apply

Date: 2026-10-24. Status: **in progress**. The 2026-11-06–07 work below adds acknowledged
application state, observed D3D12 and Vulkan display transitions, and guarded Ultimate and menu routes.
No additional strategy was promoted.

## 0. Progress record (2026-11-06–07)

| Package | State | Evidence and next action |
| --- | --- | --- |
| P0 | Static inventory and measurement hooks delivered | `out/_swap/hot-settings/p0/inventory.json` covers all 48 player preferences, with 48/48 static consumer sites, 17/17 installed provider row macros, and baseline 9 immediate/0 menu-return/0 next-song/39 restart. The provider audit parsed 42 installed DTBs and separates read candidates from writes without publishing extracted data. Bounded request/defer/consumer/ack logs carry revision, generation, value, boundary and reason. Static sites do not prove a live consumer. |
| P1 | Core contract implemented; asynchronous edge gated | `ApplicationLedger` separates saved, running and pending values, reconciles late completions, and exposes records in snapshots. Stale no-state failures cannot erase a newer failed row, and success without a matching actual value fails. Commits revalidate pending values against the current capability revision and descriptor before disk I/O. Partial provider saves expose only durable rows; RhythmVerse filters publish one immutable group. Menu-return and next-song rows wait for authored guest completion sentinels; Controller Layout waits for a GPU-provider ticket. Controller pixels after invalidation and shutdown during queued work remain unmeasured. |
| P2 | Six transitions observed on both backends; remaining acceptance open | The authored Window Mode row previews through SDK `SetDisplayMode`, a recovery marker, and a 15-second Keep/Revert page. Disposable runs under `out/_swap/hot-settings/runtime-acceptance/` observed all six directed Windowed/Borderless/Full Screen changes with Keep on D3D12 and Vulkan, plus explicit Revert and timeout rollback to the Display page on each. Both backends restored a saved Full Screen mode on relaunch without a CLI display override; input worked and a kept return to Windowed restored the fitted client. Vulkan Full Screen desktop captures were black, so its relaunch visual remains unverified. Repeated placement/input remains open; schema remains restart-scoped. |
| P3 | Negative feasibility gate for this implementation | `sub_823F3698` allocates the guest frame; `SetBootRenderFrame` is consumed during construction. Both SDK backends capture draw scale when creating render-target/texture caches in `InitializeContext`; their `ShutdownContext` paths drain GPU work and destroy the caches as part of whole-processor teardown. The common worker has no in-loop reconstruction operation; presenter resize and video-mode updates affect output only. The guest aspect patch is one-shot. No ownership-safe live teardown of the screen object at `0x8298ACA8` or replacement boundary was established, so none of the scale/base/aspect transitions pass P3's safety preflight. Keep Resolution restart-scoped and revisit only when the guest-screen owner and an SDK processor reconfiguration boundary are proven. |
| P4 | No supported transition after P3 gate | A transactional render operation would need to reconstruct both the guest frame and backend caches coherently. Starting one from the current menu would expose stale resources or risk guest execution; no live render reconfiguration is advertised. |
| P5 | Guarded next-song and Personal Best selection routes delivered; one live Timer cycle passed | All 17 provider-file bindings retain restart strategy. The bridge serves only durably committed values for active compatible Ultimate. With `--settings_ultimate_refresh_trial=true`, seven player-facing HUD/visual rows are assigned at the start of the installed `ulti_game_enter` record, before its original construction commands. A separate guarded Personal Best assignment runs at song-selection entry before both identified consumers. Each guarded assignment reports a row receipt. A disposable run hid Timer on the next song and restored it on a third song, both with matching guest acknowledgments. Personal Best's visible effect, other provider consumers, in-game later provider saves, restart survival and Game Speed synchronization are unverified. |
| P6 | Loaded menu/header route enabled; one live row cycle passed | Every boot publishes durable visibility changes and installs loaded splash and header refresh records. Header refresh covers all eight Messages/Recommendations/RhythmVerse masks, positions, badges, LT/RT Flow and offline style. On 2026-11-07 a disposable D3D12 run hid and restored LEADERBOARDS on the first returned splash frame, with saved revisions and guest acknowledgments matching both changes. The refresh now runs after `splash_screen` calls the retail `check_trial`, which previously overwrote it. Other rows, repeated cycles, selected-row recovery, input paths and header visuals remain unverified; schema stays restart-scoped. RhythmVerse master stays boot-seeded. |
| P7 | Static candidate audit complete; no promotions | DLC Menu Songs constructs its pool and stream mounts at Configure; Scan Threads is captured by the DLC worker; Unlock All is boot-seeded; Hide Achievements is captured by dialog construction. Filtering samplers and native MSAA use backend caches, while FXAA has a live SDK method but no verified settings applier. Exact read sites are recorded below; all remain restart-scoped pending an owned live boundary. |

The D3D12 and Vulkan display runs above used a disposable profile. Repeated placement/input
and the remaining menu/provider UI acceptance remain unverified.

## 1. Outcome and priorities

Reduce settings changes that require closing and relaunching the game, prioritizing:

1. **Display and resolution.** Windowed, Borderless and Full Screen should switch in the current
   process. Resolution should change the actual rendered frame, presentation and layout together.
2. **Ultimate gameplay and HUD.** Persist the preference and update the running provider, including
   any objects that have already copied the preference.
3. **Main-menu appearance.** Returning from Help & Options should show the new rows, headers and
   visibility immediately, without relaunching.

An immediate effect is preferred. Where a setting governs resources created at song load, applying
it on the **next song load** is still an improvement over a process restart, but must be described
separately. An effect on returning to the menu likewise needs its own completion point. Neither
counts as immediate hot apply in the results.

Keep existing setting IDs, storage keys, precedence, provider persistence and restart recovery.
Changing the launch target remains a process restart. Renderer backend switching is outside the
initial conversion work. Do not expand the supported resolution catalogue here; use the existing
planner and coordinate with the [resolution expansion plan](../todo/2026-10-19-resolution-scale-expansion-plan.md).

## 2. What the current implementation establishes

These are code-inspection findings, not new runtime measurements.

| Area | Current implementation | Implication |
| --- | --- | --- |
| Strategy inventory | At plan creation, `src/settings/schema.cpp::kStrategies` marked seven rows hot: V-Sync, Help & Options Labels and five RhythmVerse instrument filters. The 2026-11-06 static recount is nine after Load Guard and Controller Layout were delivered. All Ultimate bindings in `src/settings/ultimate_adapter.cpp::AllBindings` remain restart-scoped. | Count player-facing rows separately from internal/retired IDs and actions. |
| Apply path | `src/settings/hot_apply.{h,cpp}` partitions commits; `src/hooks/hot_apply_runtime.cpp` writes two cvars or swaps the store filter snapshot. `settings_bridge_hooks.cpp` applies after commits and records failures for restart. | Reuse this entry point and extend its completion model. A successful save or cvar readback alone cannot prove a new kind of live effect. |
| Runtime baseline | `SettingsBoot::VerifyApplied` captures `boot_values_`; `RestartPending` compares current values with that baseline plus forced restart rows. | A later failed apply must be compared with the last acknowledged running value, which may differ from boot. |
| Display | `DisplayToCvars` maps one row to `fullscreen` and `borderless`. The SDK already has `Window::SetFullscreen` and a fullscreen change callback in `ReXApp::SetupPresentation`. Borderless is only read when `WindowSDL` opens. | Display needs an atomic window operation covering both settings. Full Screen currently means desktop fullscreen; preserve that meaning. |
| Resolution | `SettingsBoot::ApplyRow` calls `PlanGuestScreen` and sets `resolution_scale`; `ApplyWindowPolicy` sets the video mode and startup window dimensions. `SetBootRenderFrame` controls guest screen allocation; GPU texture/render-target caches capture draw scale at initialization. | A window resize, cvar write or updated boot atomic does not resize the existing guest frame or rebuild scale-dependent GPU resources. |
| Layout | `WriteGuestLayoutAspect` patches two constants with original-value guards and a static latch. HUD reanchoring has its own cached objects/state. | Live aspect changes need reversible patches, invalidation and recomputation from original transforms. The present one-shot path cannot simply run again. |
| Ultimate | The adapter validates installed provider shape, maps stable tokens to provider values and writes `ulti_settings.dta`. The provider reads that file at startup. | Add a guest-side runtime path; rewriting the provider file cannot update existing variables or objects. A startup file read does not establish when every consumer reads a variable. |
| Menu appearance | `settings/menu_payload.cpp` bakes row visibility, header layout/navigation and related changes into generated records. `hooks/menu_filter.cpp` captures boot settings. | Updating a snapshot or regenerating files alone cannot update panels already loaded into memory. |

Reference: [settings architecture](../../settings/architecture.md),
[graphics](../../settings/graphics.md), [menu](../../settings/menu.md),
[contracts](../../settings/contracts.md), and
[in-game settings UI rules](../../../.github/skills/in-game-settings/SKILL.md).

## 3. Application contract

### State and completion

Extend the current two-way strategy with an application boundary: **now**, **menu return**,
**next song**, or **process restart**. Keep availability/backend support separate from that boundary.
Use `ApplicationStrategy::kHot` only after an immediate runtime effect has been measured.
Version or translate bridge fields if their meaning changes; preserve stable IDs.

Maintain three distinct facts for each setting: saved request, acknowledged running value, and
pending application with its boundary/reason. Do not repurpose uncommitted edit state for deferred
runtime work. Existing `EffectiveValue`/snapshot semantics need an explicit audit and compatible
update, because policy reconciliation does not itself prove application.

An apply command carries a commit revision, generation and owned values; its result distinguishes
queued, applied, deferred, refused and failed. Acknowledgement includes the actual applied value.
Only acknowledgement advances the running baseline and clears the corresponding pending item.
Late completions cannot overwrite a newer generation. If an old operation physically completed,
record the actual state and reconcile it with the latest request rather than ignoring reality.

Preserve change-then-revert behavior and clear a previous failure when a retry succeeds. Test the
specific sequence **boot A -> apply B -> fail to apply A**: running B must remain visible and pending
A must not disappear merely because A equals the boot value. Restart prompting remains on leaving
the merged root, only for rows that still require a new process. Deferred rows show a short help
caption such as "Applies next song".

### Execution and persistence

- Keep file validation, durable commit, provider journaling and partial-save reporting. Apply only
  rows whose persistence succeeded; never apply an unsaved provider change because a native write
  succeeded. Audit both the service and provider-local bridge paths.
- Route platform window operations to the UI thread, GPU changes to the graphics owner, and DTA
  variable/object changes to a safe guest boundary. The existing cvar worker-and-join is not a
  general dispatcher for these domains. Avoid guest/UI/GPU synchronous wait cycles.
- Group dependent settings into one operation: display + resolution, menu visibility + navigation,
  provider variables + their derived state. The current per-row loop cannot provide atomicity for
  these groups. Coalesce rapid edits and execute expensive work after the interaction settles.
- Recheck capability revision, explicit CLI/environment ownership, active provider and object
  lifetime at execution. Reject stale or unsupported requests with a specific reason.
- Cancel/drain work during shutdown and restart. Keep guest pointers within the owning guest call;
  resolve objects again after scene transitions. Failures preserve the last working state and an
  honest pending/error result, with a restart fallback only when restarting can apply the request.

## 4. Conversion candidates

The following are **targets to investigate**, not established hot capabilities. P0 records the
actual consumer for every row before promotion.

| Priority | Settings | Target completion | Work required / fallback |
| --- | --- | --- | --- |
| Highest | Display | Now, brief presentation transition allowed | Implement all six directed transitions between Windowed, Borderless and Full Screen; restore window placement, decoration and client size. |
| Highest | Resolution | Now where supported; otherwise an explicit menu return boundary | Transactionally update guest frame, GPU resources, video mode, window policy, UI aspect and HUD. Prove scale-only changes before changes of guest base/aspect. Retain restart for unsupported transitions. |
| High | Hide HUD, Gold Progress, Progress Bar, Show Timer, Show PB, Show Steak, Input Viewer | Now | Set live provider values and invoke existing refresh/show/hide logic. If HUD elements are not loaded, stage the value for construction. |
| High | Camera Angle, Track Wiggles, Depth of Field | Now where consumers support it | Trace polling versus copied state; refresh active camera/track/post-effect state and restore defaults when disabling overrides. |
| High | Precision Mode | Now if timing state supports it; next song otherwise | Inspect timing/scoring consumers and caches; do not assume writing `freq_ms` updates the active judge. |
| High | Game Speed | Next song first; now only after a separate synchronization proof | Apply `speedmod` at the song construction boundary. Mid-song conversion must keep audio, transport, charts, judgment windows and pause/resume synchronized. |
| High | Main Menu visibility, Play Current Song, Refresh Songs, Download Content | Menu return, before first visible menu frame | Rebuild the loaded provider/list from a complete canonical inventory; preserve selection by symbol and keep Help & Options reachable. |
| High | Show Messages, Show Recommendations | On the next display of the affected header; now if currently visible | Refresh visibility, badge, packed animation positions, navigation ring, direction and offline styling as one group. Restore labels when toggling back On. |
| Medium | RhythmVerse master | Menu return, including next display of library/store tabs | Synchronize main-menu row, store snapshot, heading, tabs and routing. Disable/cancel obsolete requests safely; retain downloaded content and saved filters. |
| Medium | Plots, Force Plot, Force Skybox, Gems | Next song first | Values affect scene/assets selected at load. Permit current-song application only if a safe existing reload path is demonstrated. Restore default selection on Off. |
| Medium | Skip Offline Prompts | Next applicable prompt | Replace the boot-only decision with a live guest-safe snapshot; do not dismiss an unrelated open modal. |
| Medium | DLC Menu Music | Menu return / safe track boundary | Rebuild the pool without invalidating active playback or mounted packages; preserve the fixed 50-song cap and Play Current Song behavior. |
| Later | Filtering, Anti-Aliasing | Now only for independently proven submodes | Trace sampler/cache/shader dependencies; distinguish a post-process switch from sample-count/resource changes. Keep unproven submodes restart-scoped. |
| Later | DLC cache, DLC scan threads, Refresh Songs behavior, Unlock All | Next scan/menu/song as appropriate, only after audit | Separate row visibility from scan behavior. Synchronize workers/cache ownership; trace unlock-dependent song/provider caches. |
| Retained | Launch target; renderer backend | Process restart | Different content/backend initialization. Revisit backend reconstruction only as a separate project. |

Letterboxing is a legacy provider setting removed from the current authored surface. Audit it for
compatibility, but do not reintroduce its row or count it as a player-facing conversion. The online
achievement grant is an action, not a restart-to-hot conversion.

## 5. Implementation packages

### P0 — Consumer inventory and measurement hooks

Produce one machine-readable inventory from the schema and adapter, including ID, visible contexts,
current strategy, source, consumer, proposed boundary and evidence. Inspect the installed Ultimate
DTBs using the existing adapter/parser; identify every variable read and the provider's original row
callbacks. Record poll-time, panel-entry, song-load and initialization consumers separately. Extend
shape validation for any new live method/variable assumptions. Do not publish extracted game data.

Add bounded diagnostics for apply request -> execution -> consumer refresh -> acknowledgement,
with revision/generation, frame/scene, old/new values and failure reason. Record baseline counts for
immediate, menu-return, next-song and process-restart settings. This closes the consumer uncertainty
in the candidate table; file persistence alone is not evidence of runtime consumption.

### P1 — Acknowledged application and truthful state

Extend `model`, `service`, `hot_apply`, `boot`, `bridge_sink` and `settings_bridge_hooks` with the
contract in section 3. Split runtime application into cvar/store, window, render, Ultimate and menu
handlers behind the existing entry point. Add grouped operations and asynchronous completion before
introducing handlers that dispatch across threads. Preserve the currently established hot rows.

Completion: host tests cover mixed hot/restart/deferred commits, durable/partial save failures,
missing handlers, unsupported backends, superseded commands, retry success, capability changes and
the A/B/A failure case. Generated UI and snapshots agree about pending state and restart prompts.

### P2 — Hot display modes (first user-visible delivery)

Add a runtime window-mode operation in the SDK window abstraction and SDL implementation. Reuse
fullscreen support, add live border/decorations/placement handling, and apply the compound mode in
one UI-thread operation. Check the existing fullscreen cvar callback's thread ownership and avoid
two competing application paths. Persist/synchronize cvars without having their callbacks partially
apply the operation again. Add a maintained SDK patch under `patches/rexglue-sdk/`.

Capture the previous mode, display and window rectangle. Restore a fitted logical client size when
returning to Windowed; respect physical pixels/DPI and the selected monitor's usable area. Confirm
actual SDL window state and successful presentation before acknowledging. Do not claim exclusive
fullscreen or change the rendered resolution as a side effect of switching modes.

Use a short **Keep / Revert** confirmation for disruptive display changes, shared with resolution.
Implement it within the existing guest UI conventions, with a proposed 15-second timeout. Keep the
previous known-good configuration until confirmation; timeout, explicit Revert, failure or abandoned
preview restores both runtime and saved settings. Record a recoverable preview marker before any
durable trial value so a crash cannot strand startup on an unconfirmed mode. The timer must still
operate when presentation is unavailable. Ordinary toggles need no confirmation.

Completion: all six transitions pass, repeated switches preserve placement and input, and timeout
and explicit rollback work. V-Sync and the actual render size remain coherent on D3D12 and Vulkan.

### P3 — Resolution feasibility gate (investigate early)

Trace ownership of the title screen object constructed by `sub_823F3698`, its frame allocations,
viewport/camera projection and UI mapping. Identify an existing resize/reinitialize lifecycle or a
safe, narrowly scoped reconstruction path. Trace the SDK caches that capture scale and the live
video-mode/presenter update path. Establish whether graphics resources can be replaced without
destroying guest execution, audio, input or the device.

Probe in ascending cost: **same guest base/aspect, different scale**, then **different guest base,
same aspect**, then **different aspect**. Each must return to the original configuration. For each
transition, record exact resources rebuilt, quiescence/fences, observed guest/output dimensions,
layout and rollback result. Do not promote the entire Resolution row from one successful pair.

Gate decision: implement proven transitions in P4; use a menu-return boundary if that is the first
safe reconstruction point. Keep a transition-specific restart fallback for any unproven case. If
no safe in-process path is found, document the exact lifetime blocker and proceed with P5/P6;
resizing the presentation window does not satisfy the Resolution requirement.

### P4 — Transactional render reconfiguration

Implement a reconfiguration transaction using the existing `PlanGuestScreen`, capability checks
and window-fit policy. Validate/preflight first, retain the old working configuration, then
quiesce rendering at an agreed boundary. Recreate/rebind only the owned scale/frame-dependent
resources identified by P3, update guest allocation and GPU scale coherently, publish the matching
video mode, recompute projection/layout/HUD, and resume presentation. Release old resources only
after relevant GPU work completes. If a stage fails, restore all previous state before resuming.

Replace the one-shot layout latch/guards with reversible, image-validated runtime state that accepts
the last known applied value and restores 16:9 correctly. Invalidate HUD object caches on scene
replacement and compute anchors from original positions so repeated changes do not accumulate shifts.
Reconcile Auto against the active display without overwriting the saved Auto intent.

Coordinate with the [shader-cache plan](../finished/2026-10-10-shader-cache-plan.md): scale/AA/path
changes can change pipeline keys. Cancel/drain old preparation generations, prepare newly required
variants through the graphics owner, and reject stale completion. Do not invalidate unrelated cache
data or collide with the graphics-preparation work already in progress.

Extend `graphics_boot` recovery to confirmed runtime configurations and unresolved previews. First
successful presentation proves application; Keep confirms persistence. Do not record an allocation
that merely succeeded as known-good. Bound the operation and provide rollback on timeout/failure.

Completion: a supported transition changes actual guest/resolve/output sizes as planned; main menu,
song, HUD and round reference artwork remain correct; repeated forward/back transitions do not leak
resources, drift layout or leave stale preparation work. Advertise support per backend/transition.

### P5 — Ultimate live values and consumer refresh

Add a project-owned runtime command path to the generated guest payload. Reuse typed bindings and
the existing bridge; after persistence, assign only owned provider variables and run the necessary
original handlers or narrowly scoped refresh methods on the guest thread. Do not reload all
`ulti_settings.dta` or call provider-wide initialization on every edit.

Preserve conversions: Game Speed uses the provider's fractional representation; Force Plot and
Force Skybox resolve stable tokens against the validated installed choice list before assigning
the provider's positional values. Keep unowned variables and provider save behavior intact.

Deliver HUD/visual rows first, then Precision Mode if consumers permit it. Queue scene/resource
settings and Game Speed at the next song construction boundary, before dependent objects read them.
Hold deferred state until that boundary acknowledges the requested value. Attempt mid-song speed
only as a later, separately measured extension. Verify Off/Default restoration for every override.

Completion: each promoted row has a same-process before/after observation at its advertised boundary,
survives a later provider save and restart, and works from both main-menu and in-song Help & Options.
Inactive or unrecognized Ultimate installations cannot receive runtime commands.

### P6 — Refresh loaded menus and headers

Generate a complete canonical row/action inventory at boot, retaining the existing order and
compatibility decisions. Add runtime predicates and a guest method that rebuilds the loaded list
before the splash menu becomes visible after Help & Options. Rebuild from the inventory on every
generation so hidden rows can return. Preserve focus by symbol; choose the nearest valid row if
the old selection is removed. Keep Help & Options protected and refresh available through settings.

Move the boot-only visibility decisions in `settings_payload`, `menu_payload` and `menu_filter` to
the same runtime state. Include host mouse hit testing and action dispatch in the refresh so their
indices stay aligned with the guest list. Do not change focus during the pause state machine's
active list dispatch; apply at the existing safe transition boundary.

For library/store headers, switch existing baked decisions to runtime predicates. Update enabled
tabs, labels/badge, compact positions, animation keys, trigger direction, LT/RT visibility and
offline styling together. Refresh once on generation change or panel entry; avoid restarting flows
every poll. Exercise all eight RhythmVerse/Messages/Recommendations combinations, including Search
entry, where the front menu's header setup may be bypassed. Turning labels back On must restore
their drawing and transforms.

Promote RhythmVerse's master only when the main-menu row, heading, tabs, snapshot and routing agree
in one process. Deal explicitly with being inside a tab when it is disabled and with in-flight
catalogue requests/downloads. A completed download remains installed.

Completion: repeated hide/show and On/Off cycles from both settings contexts produce the right list
and header on first display, keep selection usable, preserve Back/swipe behavior and show no restart
prompt for successfully applied changes.

### P7 — Remaining candidates and close-out

Investigate DLC Menu Music, future-scan cache/thread settings, Unlock All, filtering and AA separately.
Reuse the handlers and acknowledgement contract; do not bundle them into the resolution gate. For
menu music, swap pools at a safe playback boundary and retain mounts until their active stream ends.
For graphics, measure every offered submode before relaxing its restart requirement.

Promote schema and Ultimate strategy metadata together **only with evidence**. Update the tests that
currently require rows such as the RhythmVerse master to stay restart-scoped. Update settings
contracts, architecture, graphics, menu, status and affected plan records. Leave a named reason and
revisit condition for each remaining restart setting.

**P7 static restart gates (2026-11-06).** The table below is generated by
`python scripts/settings/export_hot_apply_inventory.py --p7-markdown` from the schema and the
gate map in that exporter. It names all 39 currently restart-scoped player preferences. These are
source-level reasons and required revisit evidence; none proves a live transition. The full
per-row consumer references and boundaries are in `out/_swap/hot-settings/p0/inventory.json`.

| Setting | Reason restart remains required | Revisit when |
| --- | --- | --- |
| `target` | Launch target selects a different guest content image. | A separate supported in-process content switch replaces the process boundary. |
| `startup` | Startup behavior is consumed during boot. | A repeatable same-process startup/menu transition applies both modes. |
| `refresh_music_library` | The menu-return row rebuild lacks full owner acceptance. | Verify hide/show, selection, input and reverse transitions on menu return. |
| `dlc_scan_threads` | The DLC worker captures its scan-thread count at initialization. | An idle-worker reconfiguration is owned and tested before the next scan. |
| `dlc_menu_songs` | The menu-music pool and active stream mounts are built at Configure. | Swap the pool at a safe audio boundary without releasing active mounts. |
| `play_current_song` | The menu-return row rebuild lacks full owner acceptance. | Verify appearance, selection and reversal after returning to the menu. |
| `rhythmverse` | The store master and menu route are boot-seeded separately. | Atomically refresh store access, header, menu row and disabled-state gates. |
| `unlock_all` | Provider unlock state is seeded at boot and read by song selection. | Trace and verify the provider consumer after a same-process change and reversal. |
| `hide_achievements` | Achievement dialog behavior is captured at construction. | Verify new dialogs and reversal without retaining a stale constructed dialog. |
| `resolution` | Guest frame and backend GPU caches lack safe live reconstruction. | Prove screen ownership and transactional renderer reconfiguration per transition. |
| `renderer` | Backend selection initializes the graphics worker and caches. | A separate backend teardown/recreation project proves a safe transition. |
| `display` | Preview works, but the full transition/backend acceptance matrix is incomplete. | Verify all directed modes, rollback, placement, input and relaunch on both backends. |
| `anti_aliasing` | MSAA changes backend resources; FXAA has no verified settings applier. | Measure each offered submode and add a transactional applier where safe. |
| `filtering` | Sampler state is captured in backend caches. | Prove cache invalidation and each offered filtering step on both backends. |
| `hide_download_content` | The menu-return row rebuild lacks full owner acceptance. | Verify removal, restoration, navigation and reversal on menu return. |
| `show_messages` | Header refresh has a guest sentinel but lacks owner visual acceptance. | Verify all header combinations, badge, positions and reverse transitions. |
| `show_recommendations` | Header refresh has a guest sentinel but lacks owner visual acceptance. | Verify all header combinations, navigation and reverse transitions. |
| `show_currency` | Currency visibility is built into the loaded store header. | Add an owned header refresh and verify label and placement on reversal. |
| `hide_offline_prompts` | The menu filter captures this policy at Configure. | Add a safe prompt-policy refresh and test both prompt routes. |
| `background_music_navigation` | Music navigation spans shell and menu input state. | Refresh bindings and verify both input directions while audio plays. |
| `music_player` | Player visibility and shell availability have no acknowledged refresh. | Verify show/hide and reversal with an active stream at menu return. |
| `hide_main_menu` | The menu-return row rebuild lacks full owner acceptance. | Verify every eligible row, selected-row removal and restoration. |
| `hidden_main_menu` | The menu-return selection rebuild lacks full owner acceptance. | Verify each selected subset, selection recovery and reversal. |
| `hide_hud` | The provider constructs HUD objects at song entry. | Verify the guarded next-song assignment and both visibility directions. |
| `goldprogress` | The provider's score callback trial lacks visual acceptance. | Verify the next star update and reversal in the same song. |
| `progressbar` | The provider constructs this widget at song entry. | Verify the guarded next-song assignment and both visibility directions. |
| `show_timer` | One same-process hide/show cycle passed only under the opt-in trial; production strategy and persistence remain unvalidated. | Verify later provider save, restart persistence, both settings contexts and inactive/mismatched providers before next-song promotion. |
| `show_pb` | A guarded assignment and guest receipt now run before the installed song-selection consumers, but visible effect and persistence remain unverified. | Verify hide/show, later provider save, restart persistence and both settings contexts before next-selection promotion. |
| `show_steak` | The provider constructs this widget at song entry. | Verify the guarded next-song assignment and both visibility directions. |
| `input_viewer` | The provider constructs this widget at song entry. | Verify the guarded next-song assignment and both visibility directions. |
| `precision_mode` | The scoring configuration reads the provider global at song load. | Add a guarded load boundary and verify judgment behavior in both directions. |
| `disable_random_wiggles` | Track construction reads the provider global at song load. | Add a guarded track rebuild and verify both directions. |
| `camera_angle` | The provider binds camera animations at song entry. | Verify each offered angle and reversal after guarded next-song assignment. |
| `game_speed` | Provider speed changes can desynchronize chart and audio transport. | Verify alignment and reversal at every offered speed after a safe boundary. |
| `disable_dof` | No non-row provider read was found in the installed 42 DTBs. | Identify the actual effect consumer before attempting a live handler. |
| `disable_plots` | Plot palette expressions are consumed during song loading. | Add an owned plot rebuild and verify both directions next song. |
| `forceplot` | Plot selection is consumed during song loading. | Add an owned plot rebuild and verify every installed choice and reversal. |
| `which_skybox` | Skybox selection is consumed during song loading. | Add an owned skybox rebuild and verify every installed choice and reversal. |
| `gems` | The provider loads gem visuals during song entry. | Verify every installed gem choice and reversal after guarded next-song assignment. |

## 6. Delivery order

Recommended: **P0 -> P1 -> P2 -> P3 -> P4 -> P5 -> P6 -> P7**. Run the resolution gate immediately
after display delivery because it is the highest-risk requested feature. If P3 exposes a substantial
renderer-lifecycle blocker, deliver P5/P6 while that blocker is addressed. Their implementation does
not depend on render reconstruction.

Each package should be a reviewable change with its own acceptance record. This plan authorizes
neither a runtime strategy promotion without measurement nor navigating the game for testing.

## 7. Validation and acceptance

Build each implementation before asking for a runtime check:

```powershell
cmake --build --preset win-amd64-release
ctest --test-dir out/build/win-amd64-release --output-on-failure
python .github/skills/documentation/scripts/check_docs.py
git diff --check
```

Use the relevant existing settings, bridge, graphics, window-policy, Ultimate, menu and store tests
for each package; run the broader host suite at integration. Add tests for state transitions,
resource ownership and failure recovery, rather than tests that only mirror enum assignments.

Before anyone drives the game/UI, ask whether the owner wants to perform the tests and wait for
their answer, unless autonomous UI testing has been authorized for that work. Host inspection,
builds and tests do not depend on this choice. This planning task performs no game navigation.

| Runtime check | Required observations |
| --- | --- |
| Display | All six directed mode transitions; repeat cycles; placement/DPI; minimize/restore; focus/input; explicit and timed rollback; monitor-loss failure if reproducible. |
| Resolution | Scale-only/base/aspect transitions in both directions across supported 16:9, ultrawide and 4:3 modes; Auto; all three display modes; actual render dimensions; round artwork; HUD anchoring; failure/rollback. Use the current supported catalogue. |
| Backend | D3D12 and Vulkan separately, including supported alternate rendering paths. Unmeasured combinations remain gated rather than inheriting another path's result. |
| Ultimate | Change each candidate in the same process; verify at the advertised boundary; reverse it; resume pause; load another song; test absent/inactive/shape-mismatched provider. For speed, verify audio/chart alignment at every offered value. |
| Menu | Hide/show each eligible entry twice; hide the selected entry; protect Help & Options; verify keyboard/controller/mouse; all eight header states; Search entry; correct directions/Back/labels/badge. |
| Persistence/state | Close/reopen settings; no-op/revert edits; mixed immediate/deferred/restart edits; provider partial failure; runtime failure followed by retry; restart restores confirmed preferences. |
| Lifecycle | Shutdown during a queued apply; superseded generation; scene destruction before callback; display timeout; renderer failure; no deadlock or leaked resources across repeated changes. |

Store logs and captures under `out/_swap/hot-settings/<package>/<case>/`, with build/backend,
commit/generation, requested and acknowledged values, application boundary and verdict. Report
separate totals for immediate, menu-return, next-song and restart settings. The 2026-11-06 baseline is nine
immediate rows; the final target count is established by measured conversions, not by relabeling
saved-only settings. Display, real resolution changes, Ultimate behavior and menu refresh each need
their own acceptance verdict and any remaining limitation.

### Validation note — 2026-11-06

- `cmake --build --preset win-amd64-release`: passed. The toolchain check reported the already
  installed Clang 19.1.7 and CMake 4.4.0 differ from the frozen 23.1.1/4.4.3 set; the build
  permits this.
- `ctest --test-dir out/build/win-amd64-release -R "settings_(hot_apply|service|bridge_service)" --output-on-failure`:
  3/3 passed. The A/B/A failure case and the native-durable/provider-failed subset were asserted.
- Full `ctest --test-dir out/build/win-amd64-release --output-on-failure`: 96/100 passed. The
  shader asset resolver retail and Ultimate installed-candidate tests failed because Python could
  not import `decrypt_xex`; `path_policy` failed its nonexistent-descendant assertion at
  `tests/path_policy_tests.cpp:88` on both runs; `menu_music_startup_no_base` segfaulted in the full
  run and passed on the focused rerun. None of these tests exercise the changed settings files.
- `python .github/skills/documentation/scripts/check_docs.py`: 0 errors, 183 existing warnings
  (line endings and historical links/mentions). `git diff --check`: passed.
- The P0 exporter wrote 48 rows to `out/_swap/hot-settings/p0/inventory.json`. It is a static
  inventory, not evidence that any candidate boundary is live. P2–P7 implementation and all owner
  game/UI checks remain open; this plan stays in `in-progress/`.

### Integration follow-up — 2026-11-06

- `cmake --build --preset win-amd64-release`: passed with the display SDK patch and new runtime
  sources. An earlier concurrent build attempt hit a transient open controller PNG; a single build
  completed. `scripts/setup/apply_sdk_patches.ps1 -Check` and CTest `sdk_patch_stack` passed.
- Focused CTest selection `(settings_|menu_payload|menu_compat|display_mode|ultimate_adapter|window_policy|native_mouse|sdk_patch_stack)`:
  16/16 passed after the final build. This includes installed Ultimate payload fixtures, the
  canonical menu rebuild and SDK patch replay.
- Full CTest: 97/100 passed. `shader_asset_resolver_retail` and `_ultimate` require the unavailable
  `decrypt_xex` Python module; `path_policy` still fails the nonexistent-descendant assertion at
  `tests/path_policy_tests.cpp:88`. The prior intermittent `menu_music_startup_no_base` passed.
- Documentation checker: 0 errors, 186 warnings (line endings and historical links/mentions).
  `git diff --check`: passed.
- The owner will perform game/UI testing. The 15-second display chord and opt-in splash refresh
  are trial routes, not completed hot settings. No new strategy was promoted; immediate/menu-return/
  next-song/restart counts remain 9/0/0/39. P0 consumer evidence, P1 domain handlers, P2 preview,
  P3/P4 resource reconstruction, P5 consumer dispatch, P6 headers and P7 audit remain required.
  Keep this plan in `in-progress/` until those implementation gates are met.

### Integration follow-up — 2026-11-06 (grouped filter and trial routes)

- `cmake --build --preset win-amd64-release`: passed after correcting the display recovery value
  construction and linking the Ultimate adapter into the settings-screen test target.
- Focused CTest `(display_mode|settings_screen|menu_payload|settings_service|settings_hot_apply|store_client)`:
  6/6 passed. The display test covers recovery-marker preservation; the settings-screen test covers
  guarded main-menu Ultimate dispatch; the store test covers one-generation grouped publication and
  refusal without partial visibility. `rb_blitz_menu_payload_tests.exe check-runtime-header-records`
  checked 46 installed-record cases with zero notes (static payload check, not game observation).
- Full CTest before the final settings-screen test correction: 96/100 passed. The one changed-scope
  failure was the test's count of switch tables, which needed to account for the new Ultimate trial
  method; its focused rerun passed. The other three failures remained the two missing `decrypt_xex`
  imports and `path_policy`'s nonexistent-descendant assertion at `tests/path_policy_tests.cpp:88`.
- After the final source correction and release rebuild, CTest excluding those three established
  failures passed 97/97. This includes the settings-screen rerun with both Confirm and Left/Right
  Ultimate trial routes.
- Documentation checker: 0 errors, 186 warnings. `git diff --check`: passed. The static P0 inventory
  is at `out/_swap/hot-settings/p0/inventory.json`. No game/UI navigation was performed. P1's
  asynchronous domain contract, P2's owner UI acceptance, P3/P4 resource reconstruction and P5/P6
  consumer acknowledgment remain implementation work; counts stay 9/0/0/39. Keep the plan active.

### Display and resolution follow-up — 2026-11-06

- `src/settings/display_runtime.cpp` now removes the unconfirmed-preview marker before calling the
  durable Keep callback. A marker removal failure cannot save a mode that the next boot would
  override; a failed save rolls the running mode back. The 15-second timeout is injectable in the
  host test, which pumps the fake UI queue and verifies automatic rollback without a game frame.
  `src/settings/boot.cpp` retries removal of a recovered preview marker after a verified menu frame
  instead of permanently stopping after the first failed removal.
- `cmake --build --preset win-amd64-release`: passed with these changes. Focused CTest selection
  `(settings_|menu_payload|menu_compat|display_mode|ultimate_adapter|window_policy|native_mouse|sdk_patch_stack)`:
  16/16 passed. Direct `rb_blitz_windows_test_runner.exe rb_blitz_display_mode_tests.exe`:
  79 checks passed. These are host checks; no game/UI navigation was performed.
- P3 read-only inspection confirmed the lifetime gate: the guest constructs its frame inside
  `sub_823F3698`; post-call size-field writes were already measured ineffective in
  `docs/settings/graphics.md` part 1 §5. Both SDK backends capture draw scale while constructing
  GPU caches, and the common command-processor worker has no in-loop cache rebuild path. The
  aspect patch in `src/hooks/render_settings.cpp` accepts original bits once, so a reversible
  live aspect transition does not exist. P4 remains gated on guest-screen ownership and a safe
  renderer reconstruction boundary; Resolution remains process-restart scoped.
- P2 now routes the guest Window Mode row through unsaved preview, an authored Keep/Revert screen,
  and a durable save only after a successful presenter sequence increment and native mode check.
  `src/settings/bridge_sink.cpp` rejects stale capability or saved revisions, and
  `src/rb_blitz_app.h` acknowledges the running value to the application ledger after Keep.
  SDK patch `0049` records successful presents; the patch-stack check passes. The screen and
  bridge host tests cover the route, but owner UI acceptance, timeout behavior on screen and
  transitions in both D3D12 and Vulkan remain unverified. The 9/0/0/39 strategy counts do not
  change, and this plan stays in `in-progress/`.
- Final host validation after this wiring: `cmake --build --preset win-amd64-release` passed;
  focused CTest selection passed 16/16, including `settings_screen`, `settings_bridge_service`,
  `display_mode`, and `sdk_patch_stack`. `apply_sdk_patches.ps1 -Check` reports 48/48 applied,
  zero unexpected SDK changes. The documentation checker reports zero errors and 182 existing
  warnings. No game/UI navigation was performed.

### Integration and live menu follow-up — 2026-11-07

- The release build passed after moving `rbbz_refresh_menu` after the retail `check_trial` in
  `splash_screen` entry and adding per-row Ultimate song-entry receipts. The installed splash
  record check passed 56 checks. `scripts/setup/apply_sdk_patches.ps1 -Check` reported 48/48
  applied, zero failed and zero unexpected SDK changes.
- Full `ctest --test-dir out/build/win-amd64-release --output-on-failure`: 98/101 passed.
  `shader_asset_resolver_retail` and `_ultimate` still cannot import `decrypt_xex`; `path_policy`
  still fails its nonexistent-descendant assertion at `tests/path_policy_tests.cpp:88`. All
  changed-scope settings, menu, Ultimate, display and SDK patch tests passed.
- The disposable D3D12 profile in `out/_swap/hot-settings/runtime-acceptance/` saved
  `hidden_main_menu=[splash_leaderboard]` at revision 1. On the first returned splash frame,
  LEADERBOARDS was absent (`retest3_menu_hidden2.png`), and the guest logged
  `hidden/splash_leaderboard=true` followed by a rows-applied acknowledgment. Revision 2 saved
  `hidden_main_menu=[]`; LEADERBOARDS returned on the first frame
  (`retest3_menu_visible2.png`) with the matching false predicate and acknowledgment in
  `out/build/win-amd64-release/logs/rb_blitz_557.log`. No process restart occurred between
  those changes. The earlier failure came from refreshing the panel before the screen's
  subsequent `check_trial` rebuilt the list.
- The documentation checker reported zero errors and 180 preexisting warnings; `git diff
  --check` passed. P6 still lacks repeated cycles, other rows, focus/input and visual header
  checks. P5 still lacks visual checks beyond Timer and restart persistence. P2 still lacks repeated
  placement/input on both backends. P3's guest-frame/backend-lifetime blocker prevents P4.
  The measured strategy count remains 9 immediate/0 menu-return/0 next-song/39 restart. Keep
  this plan in `in-progress/` until those required gates are resolved or safely scoped out.

### P6 focused validation — 2026-11-07

- `rb_blitz_menu_payload_tests` built and `ctest -R '^menu_payload$'` passed. Its generated-DTA
  checks now cover all eight header masks, badge hide and retail-callback restoration path,
  all LT/RT branches and mouse destinations, every saved splash row across four visibility
  combinations, unknown-row fallback, and atomic refusal when the screen-entry `check_trial`
  call is missing or duplicated. The installed `assets/game/360` payload check passed 56 checks;
  `git diff --check` passed.
- Source inspection found that `native_menu::AnswerMenuHeaderQuery` gates target tabs with the
  same published RhythmVerse, Messages and Recommendations fields used by the DTA header mask;
  the generated mouse route targets the requested tab. These host checks do not establish
  visual restoration or navigation in the game. The one live LEADERBOARDS hide/show cycle above
  passed; repeated cycles, other rows, selected-row behavior and header visuals remain open.

### Ultimate Timer trial — 2026-11-07

- The rebuilt D3D12 game started with `--settings_ultimate_refresh_trial=true` in the same
  disposable profile; `rb_blitz_558.log` records the Ultimate song-entry refresh installed.
  One Week showed the Timer at the lower left (`ultimate_song_playing.png`). In in-song Help &
  Options, Timer was saved Hidden at revision 1 and displayed “Applies next song”
  (`ultimate_timer_hidden_setting.png`); the current song was unchanged. These Days, loaded
  without a process restart, had no Timer (`ultimate_timer_hidden_playing.png`), and the guest
  logged consumer/ack for `show_timer=false` at song entry.
- The same process saved Timer Visible at revision 2 (`ultimate_timer_visible_pending.png`).
  Death on Two Legs showed the lower-left Timer again
  (`ultimate_timer_restored_playing.png`), with a `show_timer=true` consumer/ack in
  `out/build/win-amd64-release/logs/rb_blitz_558.log`. This verifies one guarded row in both
  directions at the next-song boundary. Later provider save, restart persistence, main-menu
  settings entry, inactive/mismatched provider behavior and the other six guarded rows remain
  unverified. Keep Timer restart-scoped and keep the strategy counts at 9/0/0/39.

### Confirmed display mode relaunch — 2026-11-07

- In the disposable D3D12 profile, the first relaunch used `--fullscreen=0`. The Display page
  correctly reported “Externally configured,” so that run was excluded from persistence evidence.
  The profile was restarted without a display override, and the authored Window Mode row reported
  Windowed. Full Screen was previewed and kept; `settings.toml` saved `display = 'fullscreen'`,
  while `rb_blitz_560.log` recorded `SDL fullscreen=true` and `display.keep`.
- A second launch of the same profile, again without a display override, read `fullscreen=true` at
  boot and entered `SDL fullscreen=true` (`rb_blitz_561.log`). The splash menu rendered at desktop
  size (`display_fullscreen_relaunch_ready.png`), keyboard navigation selected Help & Options, and
  the reopened Display page showed “Window Mode: Full Screen”
  (`display_relaunch_mode_row.png`). A kept change back to Windowed restored a bordered,
  2354x993 fitted client (`display_windowed_return.png`; same log). This verifies saved Full Screen
  recovery and input through one return on D3D12. The profile was stopped after the check.
- A separate Vulkan profile started Windowed with Resolution Auto, then kept Full Screen through
  the authored confirmation. `rb_blitz_562.log` records `display.keep`, and its saved TOML recorded
  `display = 'fullscreen'`. On relaunch, `rb_blitz_563.log` read `fullscreen=true`, entered SDL
  Full Screen, and marked the menu frame presented. Keyboard navigation through Help & Options
  reached Display; a kept return to Windowed restored the visible Display page and a bordered
  2354x993 client (`vulkan_windowed_after_black.png`). Desktop captures while Vulkan was in Full
  Screen were black even after refocus, so this establishes boot state and responsive input but
  leaves the Full Screen visual unverified. Repeated placement/input checks remain open. The Vulkan
  profile was stopped after the check.

### P1 contract follow-up — 2026-11-07

- Host tests now cover a mixed immediate, next-song, and restart ledger; a stale failure after a
  newer failure; a successful retry; and a handler that omits or duplicates acknowledgments.
  The ledger preserves the newer failure and restart fallback when an older request completes
  without a new running value. A success claim without a matching running value is a failure.
- Service tests cover commit-time revalidation after a controller package disappears, an explicit
  override appears, or capabilities change. They also verify that a failed provider save cannot
  expose an uncommitted runtime value and that retrying the save preserves prior values.
- Focused Release builds of `rb_blitz_settings_hot_apply_tests` and
  `rb_blitz_settings_service_tests` passed. CTest `settings_hot_apply` and `settings_service`
  passed (2/2). These are host contract checks; controller pixels after invalidation and shutdown
  while work is queued remain unmeasured.

### Integrated validation and open gates — 2026-11-07

- After the P1, P5 and P6 follow-ups, `cmake --build --preset win-amd64-release` passed.
  Focused CTest selection for settings, menu payload, Ultimate adapter, display mode and SDK patch
  stack passed 14/14. The earlier full CTest run passed 98/101; its three failures are recorded
  above. The SDK patch replay check found 48/48 applied with no unexpected SDK changes.
  `check_docs.py` reported 611 files, 0 errors and 180 existing warnings; `git diff --check` passed.
- Keep this plan in `in-progress`. P2 still needs repeated placement/input acceptance and a visual
  Vulkan Full Screen relaunch check. P3 found no safe guest-frame/backend reconstruction boundary,
  so P4 cannot support live Resolution. P5 needs the remaining provider-row consumer observations,
  game-level later-save/restart and inactive-provider checks before any strategy promotion. P6
  needs repeated row/header cycles and navigation checks, including selected-row recovery. P7's
  remaining settings retain named restart reasons and revisit conditions.

### Additional guarded route and host corrections — 2026-11-07

- P5 now has a separate Personal Best trial at the installed song-selection panel's `enter`.
  `src/settings/ultimate_runtime.h` validates both `show_pb` consumers, inserts a guarded read of
  the durably committed provider value before either one, and requires a per-row guest receipt.
  `src/hooks/settings_payload.cpp` patches the already generated record so header and icon-font
  edits compose. `src/hooks/settings_bridge_hooks.cpp` and `src/settings/boot.cpp` queue and
  acknowledge the selection boundary independently of song entry. The deferred help caption
  names next song selection. This route requires `--settings_ultimate_refresh_trial=true` and a
  compatible active provider; its visible hide/show, later save and restart behavior are unverified.
- P6's song-selection header navigation had been overwritten by the controller icon-font pass.
  `src/settings/menu_payload.cpp` now feeds that pass the already modified record. The installed
  runtime-header payload check runs with controller layout enabled and verifies that the
  Personal Best patch can then compose; 59 checks passed.
- P1 applies the durable native rows of a mixed commit even when its provider half fails, while
  retaining the bridge error for the unsaved half. Boot now reconciles generic late completions
  through the application ledger before clearing restart fallback. Deferred menu/header captions
  name menu return rather than a restart. P2 retains rollback state until its recovery marker is
  removed, SDK patch `0050` refuses startup when SDL cannot apply the saved native mode, and
  patch `0051` requires a successful UI-thread paint after the switch for Keep.
- After these changes, `cmake --build --preset win-amd64-release` passed. Focused CTest
  (`sdk_patch_stack`, `display_mode`, `settings_screen`, `menu_payload`, `ultimate_adapter`,
  `settings_hot_apply`, `settings_bridge_service`) passed 7/7; the installed runtime-header check
  passed 59 checks. `check_docs.py` found 0 errors and 180 existing warnings; `git diff --check`
  passed. The earlier full CTest run passed 98/101, with `shader_asset_resolver_retail` and
  `_ultimate` blocked by missing `decrypt_xex`, plus the unchanged `path_policy` failure.
- The owner chose no game testing in this pass. Keep this plan in `in-progress`: Personal Best and
  the remaining Ultimate/menu routes lack visible same-process acceptance, display needs its
  remaining placement and Vulkan visual checks, and Resolution lacks a safe reconstruction path.
