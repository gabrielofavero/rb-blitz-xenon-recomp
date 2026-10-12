# In-game settings — what is left

**Written 2026-10-11, after P5b-r2.** This is the single list of everything still open between the
current tree and "the plan is finished": one entry per item, with what is missing, where the owning
document records it, and the next concrete step where one exists. It collects items that are
otherwise scattered across the plan's §10.1 table and eleven handoff docs — it does **not** replace
them, and it does not restate anything they already state precisely.

How to read an entry:

> **N. What is missing.** Why it matters / what would prove it. *`doc.md` §section* — blocked by; next step.

What is green in the tree today (all measured, not assumed):

* **35/35 ctest**; `settings_screen` = 13,285 checks (host-only, no boot needed).
* The **merged Help & Options main-menu entry works**: own list resource, five base rows (six with a
  provider), retail pages routed from their new parents, Back correct, eight-row list scrolls.
* The **boot runs on the real store** (P11a): identity, `paths.toml`, migration, `settings.toml`, the
  config-rank writes with read-back, the guest channel editing through the `Service`.
* The **Ultimate adapter** reads the payload's own records, verifies their shape, and redirects the
  provider's one writable file into user storage.
* The **in-song entry does not work yet** — see §2, which is the most recent measurement in the whole
  feature and the reason G1b is not met.

Uncommitted work in the tree at the time of writing: `src/ui/settings_screen.{h,cpp}`,
`tests/settings_screen_tests.cpp`, `docs/plans/in-game-settings-{merged-menu,plan}.md`. Nothing has
been committed for P5b-r2 or P11a.

---

## 1. Where the plan stands against its own gates

| Gate | Requirement | State |
| --- | --- | --- |
| G0 contracts | frozen IDs/types/locations/migration/ownership | **met** (`in-game-settings-contracts.md`) |
| G1 native feasibility | base-only runtime opens the native screen; four row kinds navigate; an edit round-trips; installed files unchanged | **met** (P1-c; the shared-list defect it carried was fixed by P5b) |
| G1b merged root | one owned screen with its own list resource; six rows (five on base); **Back correct from both the main menu and the in-song pause menu**; nothing dispatches through retail `options.lst`; a list longer than five rows navigates | **not met** — the main-menu half is done and measured; the **in-song half faults** (§2) |
| G2 graphics feasibility | real 1920x1080 proven with target/viewport/resolve evidence | **met** at the guest-size level (P4: `guest_output=1920x1080` at `resolution_scale=1` needs Variant B; the route is chosen and the per-draw evidence exists) |
| G3 persistence | direct boot consumes the same settings; atomic writes, malformed files, interrupted migration, concurrent edits | **met** host-side (P3/P9) and **wired** (P11a); the real save/relaunch check is P12's |
| G4 integration | both targets pass the full menu/save/restart flow; installer and Controller Settings work without the old settings UI | **not met** — needs §3's P9/P10/P11 items and the in-song entry |
| G5 release | the acceptance matrix; graphics recovery and real-mode rendering; no copied content in the distributable; docs updated | **not met** — P12 has run nothing yet |

---

## 2. P5b-r2's findings — the in-song entry, measured

The whole measurement, arm by arm, is in **`in-game-settings-merged-menu.md` §3.2**; the next
experiment is **§3.3**. What it established:

1. **The dispatch, the merge and the route are all sound.** Retail's in-song HELP & OPTIONS row can
   be given our merged row list, our appended cases fire (their markers log), and a jump from that
   list to a **retail** in-song page opens cleanly (`shots-v2/07-row1-pressed.png`).
2. **The forward half of the feature is reached.** A screen of ours carrying the **menu** panel set
   (`background_night_panel shell_music_panel menu_cache_panel` + our panel) **draws the merged root
   inside a song**, entered by pressing retail's own HELP & OPTIONS row, game still behind it
   (`shots-lab1/06-variant-a.png`).
3. **The blocker: a screen of ours cannot list the in-song panels** (`pause_panel`,
   `game_fade_out_panel`, `game_panel`). Any such screen faults the guest with
   `read of guest 0x00000024` *while it is constructed* — its own panel's `{enter}` never runs, so no
   bridge read from that panel is ever logged, whereas the working shape reads its gate immediately.
   Measured for: our in-song panel + in-song set (the twins), our **menu** panel + in-song set, the
   in-song set minus `game_panel`, and the twin declared in `ui/pause/gen/pause.dtb` instead of the
   menu record. Retail's own in-song screens list those exact panels and open fine, so the limitation
   is ours; which of the two sides is responsible is not yet known.
4. **Back fails on the same limitation.** With those panels not hosted, the game screen's panels are
   torn down, so retail's own back target `{ui goto_screen game_screen}` faults in turn
   (`read of guest 0x00000000`). Keeping the pause state alive across the page is exactly what
   hosting those panels is for, and it is why retail's in-song pages do it.
5. **Consequence, honest:** the shipped payload leaves `ui/pause/gen/pause.dtb` pristine
   (`make_payload.py` writes it only under `--pause`), so the in-song page is still retail's own.
   **No user-visible regression, and no progress either** — the in-song entry is where it was.

Tooling produced here and worth reusing (all host-side, no boot): `dump-def <name>` and
`dump-record <file.dtb> [needle]` print any definition — ours *or retail's*, because `Parse`
decrypts the stream while the compiled files are not greppable — and `TestEveryDestinationExists`
fails if a screen hosts an unknown panel, focuses one it does not host, or a row jumps to an unknown
screen.

---

## 3. What is left, by package

### P1 — retail screen + bridge feasibility

1. **The `read_file` response channel is not built.** The guest can be asked questions today; producing
   a *string*/*array* answer needs the guest DataNode build API and its allocator/refcount rules,
   which are untraced — the largest ABI risk left in the transport. *`in-game-settings-bridge-findings.md`
   §5, §10 step 5* — do not build it until `sub_824193A8`'s return path is traced and exercised with a
   disposable guest call.
2. **The row-widget binding is unstarted** — which `list_*.milo_xbox` row type draws each of the four
   row kinds. *`in-game-settings-panel-api.md` §10 step 4; `bridge-findings.md` §9* — no step recorded.
3. **Appearance parity with Ultimate's own layout is a spike, not a result** — whether retail list
   resources can reproduce it without its custom MILO. *`in-game-settings-plan.md` §2.2, §13*.

### P2 — exact render-resolution feasibility

4. **No GPU capture**, and none taken since: it is the independent confirmation the instrumentation
   substitutes for. *`in-game-settings-resolution-findings.md` §7 ("Not done")*.
5. **Vulkan is instrumented but unproven** — every P2 measurement is D3D12; Vulkan is not even built
   here (`REXGLUE_USE_VULKAN=OFF`). *`resolution-findings.md` §7; `graphics-findings.md` §9*.
6. **A plugin-owned cvar cannot be set from `rb_blitz.toml` or the CLI** (config is applied before
   `rexgpu-xenos.dll` registers), which is why `resolution_scale` travels through the environment or a
   deferred write. *`resolution-findings.md` §7* — P4/P11 own the route.

### P4 — graphics capabilities, exact modes, first-run policy

7. **Variant B's `[[midasm_hook]]` is one coordinator entry away from proof**: `config/functions.toml`
   needs the entry at guest `0x823F382C` (`li r11,720` → 1080) plus the `RenderHeightHook` handler, then
   `out\_swap\p4\run-probe.ps1 -Label variantb` should show `guest_output=1920x1080`, `draw_scale=1x1`.
   *`graphics-findings.md` §7, §9*.
8. **No host probe writes the capability snapshot** — the code that fills `CapabilityInputs` (logical
   processors, SDL display query, renderer build/detect/usable) does not exist; the interface is frozen
   in `capabilities.h`. *`graphics-findings.md` §9; `in-game-settings-boot-wiring.md` §6* — coordinator,
   root `CMakeLists.txt`.
9. **The AA/FXAA visual matrix was never run**, so "what None/FXAA actually look like at 1080p" is
   unproved, and the AA row's labels rest on the policy, not on a capture. *`graphics-findings.md` §9;
   `plan §3.3`*.
10. **No song was played at 1080p.** The 1080p evidence is the title screen and the draw stream; a song
    and a restart at 1080p are not captured. *`graphics-findings.md` §9; `resolution-findings.md` §5.2*.
11. **Graphics recovery is policy plus a marker shape, not a boot** — `RecoverGraphics` /
    `GraphicsBootMarker` are tested, but nothing writes the marker at boot yet. *`graphics-findings.md` §9*.
12. **The per-backend AA/filtering ceiling matrix and the full menu/song/AA/restart matrix are untaken.**
    *`resolution-findings.md` §5.2; `graphics-findings.md` §9*.

### P5b — the merge into Help & Options

13. **The in-song entry (§2).** §3.3's order: (a) narrow the panel-set limitation with one variant
    hosting retail's `controller_config_panel` beside the in-song panels; (b) then Back — `{ui pop_screen}`
    if the panels can be hosted, otherwise Cancel calling the resume retail's own `pause_panel` calls
    (`{game_screen resume}`); (c) only then re-point `pause_options` in the shipped payload.
    *`in-game-settings-merged-menu.md` §3.2, §3.3*.
14. **The in-song child pages are unverified** — General/Interface/etc. inside a song have never been
    seen on screen; they inherit whatever (a) decides. *same doc, §3.3*.
15. **G1b's other half is only partly proven for the *in-song* context** — "Back reaches the correct
    entry screen from both entries" is measured for the main menu only. *`plan §10.4`*.

### P6 — Ultimate adapter and saved settings

16. **The achievements action is not wired.** `general.unlock_online_achievements` is a one-shot guest
    action; the bridge answers `invoke` without calling the provider and says so at boot. It must not be
    presented as working until it is. *`in-game-settings-ultimate-adapter.md` §6.1* — blocked by P1 item 1
    (a host-initiated guest call needs the allocator/refcount contract).
17. **The combined payload's shadowed records.** P5b's generator edits base `ui/gen/ui.dtb` and
    `ui/splash/gen/splash.dtb`, while the payload also carries its own copies; a patch record built from
    the base ark would wipe the mod's edits. The record must be extended from the **mounted** payload.
    *`ultimate-adapter.md` §6.2; `plan §2.4, §11`*.
18. **`forceplot` / `which_skybox` choice tokens are placeholders** — the screen's static list is not the
    payload's inventory. *`ultimate-adapter.md` §6.2; `plan §3.4`*.
19. **`unlockall`'s write path does not exist** — its value belongs under native `[ultimate_overrides]`,
    which the bridge does not own; `EncodeProviderValues` skips it by design. *`ultimate-adapter.md` §6.2;
    `boot-wiring.md` §4, §6* — P11 owns it.
20. **Seven provider rows have no provider value** (no default, no file pair) and are reported absent —
    inventing one would overrule the payload. *`ultimate-adapter.md` §6.2* — P11 decides whether the
    compiled default is seeded through the service.
21. **A merged install (mod files copied over the game root) is reported as absent** — it has no payload
    directory to inspect and no overlay to redirect through. *`ultimate-adapter.md` §6.2*.
22. **The provider's second writer is unimplemented** — `ulti/gen/ulti_funcs.dtb`'s
    `ulti_settings_writer` writes a different shape to a path variable nothing inspected ever sets.
    *`ultimate-adapter.md` §6.2*.
23. **A guest-side write through the redirect is unproven in a boot** (proven host-side and by
    construction). *`ultimate-adapter.md` §5.2* — waits on the combined payload (item 17).
24. **The "verbatim" claim is not byte-exact for a nested-array value** (whitespace normalized); every
    measured file is atoms/symbols/strings/floats and stays byte-exact. *`ultimate-adapter.md` §6.2*.

### P7 — installer paths and terminology

25. **The wizard page needs eyes** — the two new rows' geometry was never looked at at 100/200/300 %
    DPI; fix `LayoutLocationFields` if the page overflows. *`in-game-settings-installer.md` §5 item 1*.
26. **The updater leg was not run** — it needs the release manifest over the network, so "an update
    preserves an existing `paths.toml`" is unproven. *`installer.md` §5 item 2*.
27. **`settings::ResolvePathValue` decodes a stored path as ANSI** (`fs::path(value)`) while the helper
    writes `paths.toml` as UTF-8, so a non-ASCII save folder resolves differently in game vs installer.
    One-line fix: `fs::path(Widen(value))` in P3's file. *`installer.md` §5 item 3; `plan §10.1 (P7)`*.
28. **The acceptance scripts' output-directory assumptions are unre-checked** against the merged screen.
    *`installer.md` §5 item 4*.

### P8 — Controller Settings reduction and Guide removal

29. **The full launcher is still the default surface**; the reduced one is behind `--controller-only`,
    and the cutover is P11's. *`contracts.md` §9.3; `controller-settings.md` (Status)*.
30. **The non-controller tabs/rows are not deleted** (the reduced surface merely draws none of them).
    *`controller-settings.md` §1, §3.1; `plan §10.1 (P8)`*.
31. **The Guide filter is not runtime-proven** — the change is in the game executable, and the owner's
    running session held `rb_blitz.exe` open so it could not be relinked. *`controller-settings.md` §4.5*
    — rebuild and run the filter.

### P9 — hot apply and restart coordinator

32. **The authored restart prompt does not exist** (§8's native error/confirmation resources, "A Restart
    now / B Close"). The seam it will call is already there (the guest writes `rbbz-settings/restart/-/-`),
    and the commit's restart set decides whether it appears. *`hot-apply-restart.md` §6.1;
    `boot-wiring.md` §6*.
33. **The Target row's soft restart is not offered** — the row commits and leaves. *`hot-apply-restart.md`
    §6.1; `plan §3.1`* — blocked by item 32.
34. **`src/main.cpp` never calls `platform::RunBootstrap`, and nothing calls
    `RestartCoordinator::Request`.** Two exact insertions (an early bootstrap mode before
    `REX_DEFINE_APP`; the request after the save barrier on the UI thread) with the code shown in the doc.
    *`hot-apply-restart.md` §6.2; `boot-wiring.md` §6* — coordinator.
35. **The live apply still runs against P1's `MemorySink`**; it must move onto
    `Service::last_commit_changes()` now that the real store is wired. *`hot-apply-restart.md` §6.3`*.
36. **Coordinator limits to state honestly:** the parent/child handshake proves only that the spawn
    succeeded; `WaitForExit` is bounded at 30 s and a timeout refuses the start; a reused pid is refused
    by comparing creation time. *`hot-apply-restart.md` §6.4`*.

### P10 — merged-menu compatibility

37. **The launcher-side rows are still there** — the retired rename toggle and the pool-size row live on
    in `launcher/config/settings.toml`, inert in game and removed from neither. *`menu-compat.md` §7;
    `plan §10.1 (P10)`* — waits on P8/P11.
38. **The alternate in-menu refresh action is unbuilt** — hiding `splash_dlc` while it carries the
    refresh leaves the refresh with no main-menu row (the boot warns). *`menu-compat.md` §7; `plan §3.5`*
    — a row under Downloadable Content calling the same sentinel.
39. **The descriptor list is never filled at runtime** — `MenuInventory` is tested but uncalled, so the
    Hide Items menu has never drawn from the live menu. *`menu-compat.md` §7; `boot-wiring.md` §6* — the
    caller must hold `menu_filter.cpp`'s read buffer; P11 wires it.
40. **A 50-package pool has never been booted** since the cap moved from 3 to 50, so package-handle
    lifetime at that size is unproven. *`menu-compat.md` §7; `plan §10.1 (P10)`*.

### P11 — integration and default cutover

41. **P11b is the whole remaining cutover** — the new path becomes the default, the old full launcher UI
    goes, packaging/patch-record generation, and consistent boot/settings/installer behaviour.
    *`plan §10.1 (P11), §13`* — P5b–P10 first.
42. **The merged root's delivery is not packaged**: the patch record must be generated by extending an
    installed retail record, and the list resource must survive a base-only install with no Ultimate
    folder. *`plan §11 (P11)`* — blocked by item 17.
43. **Resolution and Anti-Aliasing are stored but not applied at boot**, `unlock_all` is not written, and
    the prompt never issues a restart request — the boot log says each of these out loud. *`boot-wiring.md`
    §4, §6* — unblocked by items 7, 8, 19, 32.

### P12 — release acceptance and documentation

44. **Nothing in the acceptance section is marked passed.** The §12.1 host matrix and the §12.2
    runtime/installation matrix (merged root, hot vs restart rows, exact graphics, Ultimate persistence,
    restart A/B, installer, Controller Settings, portable, integrity) are all unrun. *`plan §12`* —
    blocked by P11; the doc already fixes the record shape (command, build/config identity,
    expected/actual, evidence path).

### Cross-cutting, not owned by a package

45. **Trial disposition** — prior work reports Trial crashing before menu entry; it must be reproduced,
    recorded, and either repaired or kept unavailable, never silently shipped. *`plan §3.1, §13;
    contracts.md §12`*.
46. **Locales** — the existing transforms are English-specific; project-owned keys need English
    fallbacks and the supported locales need a run with no missing-token crash. *`plan §13`*.
47. **Three placement decisions to confirm** (each has a documented default): the **Renderer** row (omit
    from the tree, or keep in Video as `restart`), **Overscan** (moved into Video; record the deviation
    if the retail page cannot lose it cleanly), and the **achievements action's** placement (keep both in
    the same submenu). *`plan §3.7`* — owner decision.
48. **AA translation on both backends, and the Disable-Plots vs Force-Plot precedence** — to be verified
    against provider behaviour before any different rule. *`plan §3.3, §3.4`*.
49. **The first-run recommendation policy is still policy, not a measurement** — a versioned policy
    backed by repeatable measurements is what makes the feature finished. *`plan §7.4`*.

---

## 4. The critical path

Ordered by what unblocks what; everything else is independent and can be taken in any order.

1. **P5b item 13** (narrow the in-song panel-set limitation → Back → re-point `pause_options`). It is
   the only thing standing between the tree and **G1b**, and G1b's wording ("Back … from both the main
   menu and the in-song pause menu") means the gate cannot be declared met around it.
2. **The coordinator's five small shared-file items**, which unblock four lanes at once: 7 (Variant B
   hook), 8 (capability probe), 27 (`Widen` one-liner), 34 (restart bootstrap + request), 39
   (menu-rows caller).
3. **P6 items 16–19** (achievements action, combined payload, real tokens, `unlock_all`) once item 1 of
   P1 (the array/refcount ABI) is traced — the achievements action and the combined payload both ride
   on it.
4. **P9 items 32–33** (the authored prompt) once 34 lands, so a restart can actually be requested from
   the screen.
5. **P11b (41, 42, 43)** — the cutover and the packaged record set.
6. **P12 (44)** — run the acceptance matrix on the frozen P11 candidate and record it.

## 5. Definition of done

* G1b met: the in-song entry opens the merged root, and Back returns to the pause menu from it and from
  every child page.
* G4 met: both supported targets (base, Ultimate) pass menu → edit → save → restart end to end, and the
  installer plus Controller Settings work with the old settings UI gone.
* G5 met: `plan §12`'s two matrices are filled with commands, config identity and evidence paths; the
  distributable carries no copied payload/script/asset; the user documents match what the build does.
* The delivery is generated, not stored: a payload built from the *installed* records at package time
  reproduces every edit in this plan (base-only and Ultimate), and the installed archives are
  byte-identical after every boot (`records-before.sha256` vs a re-hash).

## 6. Evidence and tooling index

* In-song entry: `out/_swap/p5b/` — `v2.log` (retail page opens), `v6.log` (`s1` without `s2`),
  `lab1.log` + `shots-lab1/06-variant-a.png` (our page draws in song), `lab2.log`, `lab3.log`,
  `split1.log`; payloads `payload-q4b`, `payload-r2lab*`, `payload-split`; routes `p11a-v`, `p11a-5`,
  `p11a-lab*`.
* Definition reading: `rb_blitz_settings_screen_tests.exe dump-def <name>` / `dump-record <file> [needle]`.
* P11a boot wiring: `out/_swap/p11a/` plus `out/_swap/p5b/p11a-store.log` (the empty-selection rule,
  measured).
* Graphics: `out/_swap/p4/` (`report.ps1`, `run-probe.ps1`).
* Restart/hot apply: `out/_swap/p9/run.ps1 -Label {control,hot,full}`.
* Menu compat: `out/_swap/p10/run.ps1 -Label {custom,protected,refresh,poollgcy,empty}`.
* Item 17's blocker is the reason P5b's and P6's payloads have not been combined; the first arm that
  does so should be `out/_swap/p5b/make_payload.py` reading the **mounted** records.
