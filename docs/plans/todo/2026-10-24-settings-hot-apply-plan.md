# Expand settings hot apply

Date: 2026-10-24. Status: **planning only**; no runtime behavior or strategy changes made.

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
planner and coordinate with the [resolution expansion plan](2026-10-19-resolution-scale-expansion-plan.md).

## 2. What the current implementation establishes

These are code-inspection findings, not new runtime measurements.

| Area | Current implementation | Implication |
| --- | --- | --- |
| Strategy inventory | `src/settings/schema.cpp::kStrategies` marks **seven** rows hot: V-Sync, Help & Options Labels and five RhythmVerse instrument filters. All Ultimate bindings in `src/settings/ultimate_adapter.cpp::AllBindings` are restart-scoped. | Start with a per-row inventory rather than changing a blanket default. Count player-facing rows separately from internal/retired IDs and actions. |
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
introducing handlers that dispatch across threads. Preserve the seven existing hot rows.

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

Coordinate with the [shader-cache plan](../in-progress/2026-10-10-shader-cache-plan.md): scale/AA/path
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
separate totals for immediate, menu-return, next-song and restart settings. The baseline is seven
immediate rows; the final target count is established by measured conversions, not by relabeling
saved-only settings. Display, real resolution changes, Ultimate behavior and menu refresh each need
their own acceptance verdict and any remaining limitation.
