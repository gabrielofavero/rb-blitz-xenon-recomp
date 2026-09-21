# In-game Audio/Video settings — scoping plan

**Status: scoping only.** No code in this document. Nothing here is a commitment, and nothing here
gates the Windows recompilation milestones.

**Evidence convention**, same as [vr-port-plan.md](./vr-port-plan.md):

- **[tree]** — read directly out of this working tree, with `file:line`.
- **[cited]** — observed from a running artifact: the built game, its logs, its save container, its
  captures. Reproduce with `scripts/drive_ui.ps1`.
- **[assumed]** — reasoned, not yet verified. Every **[assumed]** is a task for milestone AV0.

---

## 1. The request

Settings that today only exist in the host config — resolution, vsync, scaling, audio level, and so
on — should be adjustable **from inside the game's own Audio/Video screen**, so a player never has
to leave the game, find a TOML file, or know that a host process exists.

To plan that honestly we first have to describe what "inside the game" means here, because the
project currently has **two settings universes that do not know about each other**.

### 1.1 Universe A — the host cvar registry

The host owns a flat registry of named configuration variables, persisted as TOML next to the
executable (`out/build/win-amd64-release/rb_blitz.toml` **[tree]**). In the current build only
non-default entries are written, e.g. `mnk_mode`, `monitor`, `log_level`, and the `[log.levels]`
table.

There are exactly two ways a value gets in or out:

| Path | Applies | Notes |
| --- | --- | --- |
| Load at startup | yes | `rex::cvar::LoadConfig` from the executable directory. |
| F4 settings overlay → "Save to config" | yes | The **only** writer: `SaveConfig(config_path_)` at `rexglue-sdk/src/ui/overlay/settings_overlay.cpp:506` **[tree]**. Nothing is saved on exit. |

So today a player changes a setting by pressing **F4** — a host overlay drawn over the guest — or by
hand-editing TOML. Both are outside the game.

### 1.2 Universe B — the guest's Audio/Video screen

The game has its own Audio/Video screen, driven entirely by guest-side data. Captured from the
running build **[cited]** (`out/drive-ui/`, gitignored; re-capture with
`scripts/drive_ui.ps1 -Actions "wait:6,key:start,wait:1,key:down,wait:1,a"` and friends):

- Header string `AUDIO/VIDEO`, footer `SELECT` / `BACK`.
- Exactly **four** rows, no more:

| Row | Rendered value | Guest data behind it | Resembles |
| --- | --- | --- | --- |
| Overscan | `ENABLED` | `overscan`, `overscan_enable` | a toggle |
| Active Track Boost | (slider) | `track_sound_slider.sld` | a slider |
| Sound Effects Volume | (slider) | `sound_effects_slider.sld` | a slider |
| Bass Boost | `DISABLED` | `bass_boost`, `bass_boost_enable` | a toggle |

- Layout metrics measured from frame diffs: ~56 px row pitch, ~50 px highlight band.
- Left/right stick produced **bit-identical** frames — no horizontal adjustment on these rows
  **[cited]**. `B` returns to the `HELP & OPTIONS` list. What `A` does per row is **not**
  established; the one `A` press we captured produced no visible sub-screen.

The screen is not code in this repository — it is Flow/Liquid script plus data inside the shipped
archive `game/gen/main_xbox_0.ark` **[tree]**. Scanning that archive's name table yields
`AV_Options.flow`, `AV_Options1.flow`, `AV_Options_bak.flow`, `AV_Options_bak1.flow`,
`options_audio_video.ep` (property block with keys `which_av_option`, `overscan`,
`overscan_enable`, `bass_boost`, `bass_boost_enable`, `value`), the two `*.sld` sliders, the
`glow_1/2.mesh` highlights, and `.lst`/`.lbl` definitions. Labels are assets, not literals.

**Guest persistence.** The guest keeps its own save container:

```
<user_data_root>\<profile>\5841122D\00000001\globaloptions\globaloptions      (1024 bytes)
                                                                            \.header
```

**[cited]** — the guest registers the content symbolic link `globaloptions:` → `\Device\Content\2\`
at boot and reads this blob before the AV screen is ever opened; its last modification time
predates the session we observed it in, i.e. the guest is the author and we are not. The blob is
opaque binary: no printable strings, and none of the screen's own key names appear as plain bytes in
the container.

Crucially, this behaviour is **already characterised, and there is already a working test harness
for it**, because milestone 4 had to prove persistence ([bringup-log.md](./bringup-log.md) § Verification,
[known-issues.md](./known-issues.md); reproduce with
`scripts/acceptance_persistence.ps1 -Only <case>` against an isolated `--user_data_root` **[tree]**):

| Case | What the guest does |
| --- | --- |
| `fresh` | Authors both files itself: `globaloptions` 1024/1024 bytes over one open, `songcache` 16/16 bytes, 4 `NtWriteFile` calls. Nothing is read back. |
| `restart` | Reads both back byte-identical and writes **nothing**. |
| `missing` | Behaves like `fresh` — the defaults are re-authored. |
| `corrupt-payload` | **Loads the garbage as-is**: no fatal, no repair, no write. |
| `corrupt-header` | Ignores the header and **rewrites the 1024-byte default payload**. |

Three consequences shape this whole plan:

1. **The 1024-byte payload is not integrity-checked.** That retires the "it might be checksummed, so
   writing it is impossible" objection — but it also means a bad write yields a game that boots
   happily with nonsense options rather than failing loudly, so any write path (AV3) needs its own
   verification.
2. **The guest owns the format** and rewrites the file wholesale, so the layout has to be learned by
   observation, never assumed.
3. **The container is entirely SDK content-path code** — no project code writes it — so a host-side
   bridge has to go through the content device layer, not around it.

**A third settings surface exists, and it is not retail.** Running this build with the Ultimate
payload installed adds a `MOD SETTINGS` entry to the main menu **[cited]** — the payload's own
guest-side menu, whose three pages (`Gameplay Settings`, `Visual Settings`, `Other Settings`) are
checkboxes and `Name: VALUE` cycling rows, with nested sub-pages (`HUD Settings`) and values such as
`Camera Angle: DEFAULT` and `Game Speed: 100%`. It is backed by the payload's own live
`game:\ulti_settings.dta` / `ulti_settings.ini` requests ([bringup-log.md](./bringup-log.md)), i.e. it
is **content the payload ships**, not content we ship ([ultimate-compat.md](./ultimate-compat.md)).

Two reasons it matters here. It is the closest existing precedent for what AV1 wants to draw — a real
in-game settings list of toggles and cycling values, in the game's own visual language rather than an
ImGui table. And it is **not available to us as a delivery route**: the payload is user-supplied
content the project deliberately does not distribute, so a vanilla install has no `MOD SETTINGS` entry
to extend. It appears as option F in §4 and as a non-goal in §6.

### 1.3 Why the two universes do not meet

Neither side can see the other:

- The host does not know the guest's AV screen exists, what state it is in, or that the guest just
  changed "Overscan".
- The guest does not know the host exists, cannot read host cvars, and writes only to its own
  container.
- Nothing reconciles them: setting `present_safe_area_x` in `rb_blitz.toml` today has **no effect**
  on the guest's Overscan row, and toggling Overscan in-game has no effect on the cvar.

The whole plan below is about building exactly one bridge between these two, and about which
direction is worth building first.

---

## 2. What the SDK already gives us

This section exists so the milestones can be costed. The short version: *the settings UI is nearly
free; reaching it from the game's screen, and making changes actually apply live, are the work.*

### 2.1 Registry and definition

- `REXCVAR_DEFINE_BOOL/_INT32/_INT64/_UINT32/_UINT64/_DOUBLE/_STRING/_COMMAND/_COMMAND_ARGS(name,
  default, category, description)` self-register at static-init time, then chain `.range()`,
  `.allowed({...})`, `.lifecycle()`, `.validator()`
  (`rexglue-sdk/include/rex/cvar.h:373+` **[tree]**).
- Lifecycle is three-valued: `kInitOnly` (read-only at runtime, rendered read-only in the overlay),
  `kHotReload` (badge "Live — changes apply immediately"), `kRequiresRestart` (badge
  " [restart]"). **`kHotReload` is the default** (`cvar.h:135-140`, `:171`).
- This repo already defines its own flags — the precedent to copy: `ultimate_mode`,
  `ultimate_payload_root`, `ultimate_patches` in category `Compatibility`
  (`src/hooks/ultimate.cpp:100-115` **[tree]**).

### 2.2 The lifecycle label is a label

`RegisterChangeCallback` exists (`cvar.h:250-253` **[tree]**) but has only **two** real call sites
in the entire SDK: `log_level` (`rexglue-sdk/src/util/logging.cpp:406`) and `fullscreen`
(`rexglue-sdk/src/ui/rex_app.cpp:353`) **[tree]**.

**Consequence: for almost every flag, "Live" is decoration.** A `kHotReload` flag changes the
stored value and the TOML, and nothing else, until something re-reads it. Making a setting genuinely
live is per-setting wiring work, and this is the single largest hidden cost in the plan. Milestone
AV2 exists solely for it.

### 2.3 UI surfaces that already exist

| Surface | Where | What it gives us |
| --- | --- | --- |
| F4 settings overlay | `rexglue-sdk/src/ui/overlay/settings_overlay.cpp` | Enumerates **every registered cvar**, category tree, search, per-flag widgets, lifecycle badges, tooltips. A new cvar appears here for free. |
| Hotkey registration | `ReXApp::SetupOverlays`, `rexglue-sdk/src/ui/rex_app.cpp:387-427` | `rex::ui::RegisterBind(name, key, description, cb)`; existing binds are F3 debug, Backtick console, F4 settings, F7 achievements. Unregistered at shutdown (`:561-564`). |
| App dialog hook | `rex_app.h:108` → called at `rex_app.cpp:427` | `virtual void OnCreateDialogs(ui::ImGuiDrawer*)` — the documented insertion point for additional host dialogs. |
| Dialog base | `rex::ui::ImGuiDialog` | `OnShow` / `OnClose` / `OnDraw`. |

This repo's app overrides only `OnPreSetup` (`src/rb_blitz_app.h:36`), `OnConfigurePaths` (`:50`) and
`OnPostLoadXexImage` (`:73`) **[tree]** — `OnCreateDialogs` is unused, so a custom in-game dialog
costs no new plumbing beyond implementing one virtual.

### 2.4 Audio: the gap is real and cheap to close

The entire `Audio` category is two flags — `audio_mute` (`rexglue-sdk/src/audio/sdl/sdl_audio_driver.cpp:27`)
and `audio_maxqframes` (`rexglue-sdk/src/audio/audio_system.cpp:28`) **[tree]**. There is **no volume
flag**, and the backend is hard-coded: `config_.audio_factory =
REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem)` (`rexglue-sdk/src/ui/rex_app.cpp:310`) **[tree]**.

But the output stage already exposes a master gain in a public header:
`rex::audio::SetOutputGain(float linear)` / `GetOutputGain()`
(`rexglue-sdk/include/rex/audio/downmix.h:60-63` **[tree]**), snapshotted once per callback at
`rexglue-sdk/src/audio/sdl/sdl_audio_driver.cpp:180` **[tree]**.

So **`audio_volume` is a small, genuinely useful, genuinely live deliverable**: define the flag, add
the change callback, call `SetOutputGain`. It is the best first proof that the bridge works
end-to-end.

### 2.5 Video: a build-flag constraint to respect

`present_effect`, `present_cas_additional_sharpness` and the `present_fsr_*` family live in
`rexglue-sdk/src/ui/presenter.cpp:29-86` **[tree]**, but in **this** build
`REXGLUE_ENABLE_FIDELITYFX:BOOL=OFF` (`out/build/win-amd64-release/CMakeCache.txt` **[tree]**), so
those branches are compiled out and `present_effect` is effectively locked to `bilinear`
**[assumed]** — verify by reading the flag's allowed set at runtime in AV0.

Implication: "add sharpening / FSR / upscaling choices to the menu" is **not a UI task**, it is a
build-configuration task first. Any AV menu designed today should either hide those rows when the
feature is off, or be honest that they are restart-and-rebuild-level options.

### 2.6 Prior art this repo already catalogued

[rb3-references.md](./rb3-references.md) records what the Rock Band 3 port solved on this engine
**[tree]**. Three entries bear directly on this plan:

| Technique | What it does | Why it matters here |
| --- | --- | --- |
| `OptionBool` / `OptionStr` | "Injects host `argv` into guest DTA options, tracking consumed args." | Direct prior art for host→guest settings injection — the same shape as §4's option C — and [prompts/04-menus-content-input-saves.md](../prompts/04-menus-content-input-saves.md) already flags it as a *deterministic alternative* to keystroke injection. |
| `Rnd__PreInit` (`rnd_this + 0xf0` sync override) | "Forces vertical sync behaviour." | Confirms v-sync is guest-visible, not purely a host present-mode choice. |
| `StreamChecksum__ValidateChecksum` → `1` | "Skips stream checksum validation." Flagged **P1**: "any asset we decrypt, repack or edit will otherwise fail validation." | **The gate on AV4.** Any shadowed `AV_Options*.flow` is subject to this — and the repo already knows the neighbouring lesson, that the mod's `blr` on `PlatformMgr::SetDiskError` is what keeps the guest out of the disc-error state machine that payload content drives. |

Two further assets already exist and should be reused rather than rebuilt:

- **Payload DTA content is live, not inert.** The guest requests `game:\ulti_settings.dta` (and
  tolerates `ulti_settings.ini`, `game:\scores`) straight out of the payload directory
  ([bringup-log.md](./bringup-log.md) **[cited]**), and those names exist nowhere in the base image as
  plain bytes — they come from the payload's own script. So *guest script content layered in through
  the payload already reaches the running game*, which makes §4's option D less speculative than it
  first looks, and suggests a cheaper variant of C (§4, **C2**).
- **A frame-pacing measurement rig.** Milestone 5 ran the same input route with `vsync` on and off and
  produced `out/m5-pacing/vsync-{on,off}/pacing-input.{json,md}` **[cited]**. Any claim in this plan
  that "the setting took effect" should be proven with that rig, not with a screenshot.

---

## 3. Candidate settings

Everything a player would plausibly want on an in-game Audio/Video screen, with the truth about
each. "Live?" is *today's* answer, not what AV2 could achieve.

| Player-facing row | cvar | Declared in | Live today? | Notes |
| --- | --- | --- | --- | --- |
| Resolution | `video_mode_width`, `video_mode_height` (1280×720) | `rexglue-sdk/src/ui/window.cpp:50-63` | no — restart | Also a `resolution` string preset, and `video_mode_refresh_rate` (60.0). |
| Window size | `window_width`, `window_height` (0 = auto) | `rexglue-sdk/src/ui/window.cpp:24-45` | no — restart | Meaningless in exclusive fullscreen. |
| Fullscreen | `fullscreen` (true) | same | **yes** | The one display flag with a change callback (`rex_app.cpp:353`). The template to copy for AV2. |
| Monitor | `monitor` (0) | same | no — restart | |
| V-Sync | `vsync` (true) | `rexglue-sdk/src/graphics/command_processor.cpp:38` | **effect verified, live apply unverified** | Declared with **no** lifecycle tag, so it *claims* `kHotReload`. Its effect is real and already measured: milestone 5 ran the same route with it on and off and the guest is vblank-locked only when allowed to be (68.3 fps / 14.34 ms median with it off) — [bringup-log.md](./bringup-log.md), `out/m5-pacing/vsync-{on,off}/`. Whether it applies *without a restart* is still an AV0 experiment. |
| Anti-aliasing | `native_2x_msaa` | `rexglue-sdk/src/graphics/flags.cpp:18` | no — restart | Affects pipeline creation. |
| Anisotropic filtering | `anisotropic_override` (3) | `rexglue-sdk/src/graphics/cache.cpp:52` | no — restart | Sampler-state cache. |
| Render scale | `draw_resolution_scale_x/y` (1) | `rexglue-sdk/src/graphics/cache.cpp:66,70` | no — restart | Does not resize the swap chain. |
| Resolution scale | `resolution_scale` (1) | `rexglue-sdk/src/graphics/cache.cpp:74` | no — restart | |
| Letterbox | `present_letterbox` (true) | `rexglue-sdk/src/ui/presenter.cpp:29-86` | no — restart | |
| Safe area / overscan | `present_safe_area_x/y` (90, range 0–100) | same | no — restart | **The one row that already exists in both universes and means the same thing.** Prime AV3 target. |
| Overscan cutoff | `present_allow_overscan_cutoff` (false) | same | no — restart | |
| Dithering | `present_dither` (false) | same | no — restart | |
| Sharpening / upscaling | `present_effect`, `present_cas_additional_sharpness`, `present_fsr_*` | same | no — restart | **Compiled out in this build** (§2.5). |
| Mute | `audio_mute` (false) | `sdl_audio_driver.cpp:27` | **unknown** | Needs the same "is it re-read?" audit as `vsync`. |
| **Master volume** | *does not exist* | — | — | Add `audio_volume` → `SetOutputGain` (§2.4). Recommended first build. |
| Audio buffer | `audio_maxqframes` (8) | `audio_system.cpp:28` | no — restart | Debug-ish, not a player setting. |

**Deliberately excluded here:** mouse/keyboard and bind flags (an Input screen, not an AV screen),
`ultimate_*` compatibility flags (diagnostics), and anything about log verbosity. Frame rate /
unlocking is deferred by the project itself — see [DECOMPILATION_PLAN.md](../DECOMPILATION_PLAN.md)
§ Deferred until after "working" — so a "frame rate cap" row should not be designed into AV1.

---

## 4. Design options

Five families, ordered cheapest-to-most-expensive. They are not mutually exclusive; the
recommendation at the end is a sequence, not a single pick.

| # | Option | How it would work | Cost | Risk | Honest verdict |
| --- | --- | --- | --- | --- | --- |
| **A** | Document the F4 overlay | Nothing new. Tell players the settings already exist behind F4. | ~0 | none | **Not what was asked.** But it is the correct fallback if AV0's kill gate fails, and it should ship as documentation regardless. |
| **B** | Context-sensitive **host** dialog | Implement `OnCreateDialogs`, register a keybind, draw an ImGui table of §3. Optionally *auto-show* it when the guest AV screen is active, detected by sniffing VFS asset opens for `options_audio_video.ep` / `AV_Options*.flow` through the existing device layer (`src/fs/payload_overlay.h` **[tree]** sits in the same path). | Low–medium | Input arbitration (who owns the pad while the dialog is open?); detection false-positives. | **The realistic majority of the value.** "In-game" in the sense of *not leaving the game*, reachable and visible while the guest AV screen is on. No guest assets touched, no Reverse engineering of guest option storage required. |
| **C** | Bridge the guest's **existing** rows | Find where the guest stores its `overscan` / `bass_boost` option values and make the Overscan row drive `present_safe_area_x/y` / `present_allow_overscan_cutoff`, and/or mirror host values back into the guest's storage. | Medium–high | The guest's storage location is **unknown** (§1.2); writing it while the guest is live could corrupt saves — though it is *not* integrity-checked, so corruption is silent rather than fatal. | Highest payoff per row (it is a *real* in-game setting), highest discovery risk. Justified **only** for the rows that already exist — see AV3's kill gate. |
| **C2** | Inject the guest's **script** options (DTA) | Rather than touching the binary blob, determine whether the AV screen's options are fed from a `game:\*.dta` that the read-only payload overlay could shadow — the same mechanism that already delivers the mod's live `ulti_settings.dta` (§2.6). | Low–medium **if** the screen reads DTA at all | Depends entirely on an unverified premise; a shadowed DTA is subject to checksum validation. | **Test it in AV0 — it is cheap and it would make C almost free.** This is the exact shape of the RB3 port's `OptionBool`/`OptionStr` patch. |
| **D** | Extend the guest's **own** UI | Author new rows in `AV_Options*.flow` + `.ep`/`.lst`/`.lbl`/`.sld`, and shadow the retail files through the existing read-only payload union device (`src/fs/payload_overlay.h`, `src/fs/overlay_merge.h`, tested by `tests/payload_overlay_tests.cpp` **[tree]**). | High | Requires external Milo/Flow tooling the project does not have; overriding shipped UI data is exactly the class of change [ultimate-compat.md](./ultimate-compat.md) is cautious about; asset redistribution rules apply. | **Optional, last, and only after the delivery pipeline is proven with a byte-identical shadow.** |
| **E** | Hook guest option get/set functions | Hook the guest's option read/write entry points directly (no asset or memory editing). | Medium, but **blocked on addresses** | Needs guest function addresses for a subsystem we have not touched. | Blocked, not unplannable — and the unblocking workflow is routine in this repo. An unregistered guest address surfaces as `[FATAL] Call to invalid or unregistered function at guest address 0x…`; the fix is to add `[functions."0x…"]` to `config/functions.toml`, re-run codegen, rebuild (consumed via `rb_blitz_manifest.toml`'s `includes`). [prompts/04-menus-content-input-saves.md](../prompts/04-menus-content-input-saves.md) states the expectation: "one of these per newly-reached UI path". Record as a later refinement of C. |
| **F** | Extend the payload's **`MOD SETTINGS`** menu | Add AV rows to the Ultimate payload's own guest settings pages (§1.2). | Low **for payload users only** | The host menu is user-supplied content we do not distribute, so a vanilla install has no such entry; and anything we add there is redistributed content rather than our own. | **Not a delivery route — but it is the proof that the guest can render a settings list of toggles and cycling values**, which is what AV1 is trying to reproduce. Relevant as evidence, not as a plan. |

### 4.1 Recommendation

**B → AV2 → C → (D only if the guest-asset pipeline is proven).**

Do B first because it delivers the user-visible feature with zero risk to game data and zero
dependency on unfinished reverse engineering. Then do the live-apply wiring, because a menu full of
rows that silently require a restart is worse than no menu. Only then spend discovery budget on
bridging the guest's four existing rows, and treat extending those rows as a separate, optional
project behind a kill gate.

**C2 is not a separate milestone — it is the cheapest test in AV0.** If the AV screen's options turn
out to come from a DTA that the payload can shadow, most of C's cost evaporates and the guest's own
rows become host-controlled without reverse engineering the blob. If it does not, C2 costs an
afternoon and is discarded.

**F is excluded as a route**, despite being the closest visual precedent (§1.2): the menu only exists
for players who installed the payload, and adding rows to it would mean redistributing someone else's
content. Keep it as evidence of what the guest can render, and as a reference screenshot for AV1.

### 4.2 Input arbitration — the open question in every option

The console overlay is evidence that a host ImGui surface can be drawn over a running guest
(`rex_app.cpp:387-427` **[tree]** **[assumed]** — its input behaviour was not tested). What is *not*
known is whether the guest keeps consuming gamepad input while a host dialog has focus. If it does,
a B-style dialog needs its own explicit input-grab, or the guest will navigate its menus underneath
our table. This is AV1's first task, not a footnote.

This question is shared with [button-mapping-plan.md](./button-mapping-plan.md), which needs the same
answer in order to *swallow* the press it just captured rather than merely avoid double-handling it —
a stricter requirement, and therefore a better test of the arbitration. Answer it once, in AV0/IM0,
and record it in both documents.

---

## 5. Milestones

Each milestone has a kill gate. A failed gate means **stop and report**, not "push on and hope".

### AV0 — Inventory and audit (discovery only, no product code)

1. **Guest interaction model.** Finish what §1.2 started: what `A` does on each of the four rows;
   whether any row opens a sub-screen; whether `SELECT` does anything; whether values are persisted
   immediately or at exit. Deliverable: an annotated capture set plus an updated §1.2 table.
2. **Live-apply audit.** For each row in §3 with an "unknown" live status (`vsync`, `audio_mute`)
   and for one representative "no — restart" flag, determine **experimentally** whether changing the
   cvar at runtime changes behaviour. Use the existing pacing rig (`out/m5-pacing/`) for anything
   that is visible in frame timing — it already produces `pacing-input.{json,md}` and is stronger
   evidence than a screenshot. Deliverable: §3's "Live today?" column replaced with verified answers
   and the read site for each.
3. **Persistence audit.** Confirm the one case the milestone-4 matrix does not cover: is
   `globaloptions` rewritten **immediately** when an AV option changes in-game, or only at profile
   save? Hash before/after; `scripts/acceptance_persistence.ps1` already isolates a writable root.
4. **Script-content audit (the C2 test).** Determine whether the AV screen's option values are fed
   from any `game:\*.dta` — the payload's live `ulti_settings.dta` (§2.6) proves the scripting layer
   reads such files. A positive answer makes option C2 the cheapest bridge in §4.
5. **Capture the payload's `MOD SETTINGS` pages as the design reference (§1.2).** It is the only
   settings list in this build that already renders toggles and cycling values in the game's own
   visual language, so it is what AV1 should look like — but it exists only for payload users and
   extending it is option F, which §4.1 excludes. Capture it; do not plan around it.

**Kill gate:** if nothing on the guest AV screen is host-owned *and* the guest ignores or fights a
host overlay, the "inside the AV screen" framing collapses to "a host menu reachable with a hotkey".
Report that before building anything.

### AV1 — A settings surface you can reach without leaving the game

- Implement `OnCreateDialogs` in `src/rb_blitz_app.h`; add one `ImGuiDialog` rendering §3's
  candidates with lifecycle badges.
- Register a keybind via `RegisterBind` alongside the existing F3/F4/F7 set.
- Auto-show the dialog when the guest's AV screen is detected via VFS asset-open sniffing; leave the
  keybind as the manual fallback.
- Solve input arbitration (§4.2) explicitly.

**Exit criterion:** a setting changed from that dialog is **observable in the running game** —
`present_safe_area_x` visibly re-crops the frame, `audio_mute` silences it — with captures in
`out/drive-ui/` proving before/after, *without a restart* for at least the two flags that already
have live paths.

### AV2 — Make the settings actually live

- Define `audio_volume` (category `Audio`, `.range(0.0, 1.0)`) and wire its change callback to
  `SetOutputGain`. Verify during playback.
- Wire the display/presenter flags that can be re-read cheaply (target: `present_safe_area_x/y`,
  `present_letterbox`, `present_dither`).
- For every flag that genuinely cannot apply live, stop pretending: mark it `kRequiresRestart` so the
  overlay badge is truthful, and give the dialog an explicit "restart to apply" affordance.
- Prove each one with artefacts of the milestone-5 pacing variety (`out/m5-pacing/` style), not with
  a screenshot that may or may not show a difference.

**Kill gate:** if a flag cannot be made live without restructuring the graphics device, it ships as
restart-required and the plan says so out loud. Fabricating a "Live" badge would be a regression.

### AV3 — Bridge the guest's existing rows

- Use AV0's discovery to locate the guest's option storage for `overscan` / `overscan_enable`
  (and, if cheap, `bass_boost` / `bass_boost_enable`).
- Make the guest's **Overscan** row and the host's `present_safe_area_x/y` +
  `present_allow_overscan_cutoff` agree, in the direction that survives the guest rewriting its own
  container.

**Kill gate:** if the guest's option values cannot be located or safely written, keep the two
universes separate, document that they are separate, and do **not** half-wire it. A player who
toggles Overscan and sees no effect has a worse experience than a player who never sees the row.

### AV4 — Optional: extend the guest's own UI *(only if AV1–AV3 succeeded)*

1. Prove the pipeline first by shadowing one **unused** retail file through the payload overlay and
   confirming byte-identically that the guest loads the shadow. Nothing new authored yet.
2. Only then attempt a modified `AV_Options*.flow` with additional rows.

**Kill gate:** if step 1 cannot be demonstrated, AV4 is cancelled permanently. This milestone must
never be the reason game data ends up in the repository.

---

## 6. Non-goals

- **No game data in this repository.** AV4 ships *patches the player applies to data they own*, never
  the data. This follows [ultimate-compat.md](./ultimate-compat.md) and the repository's GPL-2.0-only
  posture.
- Not a content/DLC/entitlement UI, not a mod loader, not a general plugin API.
- Not an unlocked frame rate (project-deferred; see [DECOMPILATION_PLAN.md](../DECOMPILATION_PLAN.md)).
- Not an Input or accessibility screen — that is its own plan:
  [button-mapping-plan.md](./button-mapping-plan.md). That plan also owns the input-arbitration question
  §4.2 raises here, since it depends on the answer more than this one does.
- Not an extension of the payload's `MOD SETTINGS` menu (§1.2, option F). It is user-supplied content;
  we neither distribute nor depend on it.
- Not a replacement for the F4 overlay. The F4 overlay stays the complete, searchable, developer-facing
  surface; the in-game menu is a curated player-facing subset.

---

## 7. Risks

| # | Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- | --- |
| R1 | "Live" badges are decorative for most flags; a menu built on them misleads players | **certain** (§2.2) | high | AV2 makes badges truthful; AV0 audits read sites before UI is designed. |
| R2 | Guest keeps consuming input under a host dialog, making the dialog unusable | medium | high | AV1 solves arbitration first, before polishing any dialog. |
| R3 | Guest AV screen detection (asset sniffing) false-positives or misses | medium | low | Detection is a convenience; the keybind is the guarantee. |
| R4 | Writing `globaloptions` corrupts the player's save | low | **severe** | Narrowed by discovery: the payload is loaded as-is with no integrity check, so a bad write is survivable but **silent** — nothing will tell the player. AV3: back up, round-trip, verify by observation; never write without a verified format. |
| R5 | AV4 tempts us into committing or redistributing game assets | low | severe | Kill gate requires a byte-identical proof step; non-goal §6 is explicit. |
| R6 | `vsync` / `audio_mute` turn out to be init-only despite claiming to be live | medium | medium | Already scoped as an AV0 experiment; correct lifecycle tags are the fix. |
| R7 | Scope creep into a full settings front-end | medium | medium | §3's table is the menu. Anything not in it is a new plan. |
| R8 | A shadowed guest asset or DTA is rejected by stream checksum validation | high *if* AV4/C2 are attempted | medium | Already known and already catalogued as P1 (`StreamChecksum__ValidateChecksum → 1`). AV4's first step is a byte-identical shadow, which isolates this from authoring mistakes. |
| R9 | The C2 premise is false and discovery time is spent for nothing | medium | low | Bound it to a single AV0 task with an explicit time box, and accept a negative result as an answer. |

---

## 8. Questions only the running game can answer

1. What does `A` do on each of the four AV rows? Is there a sub-screen at all?
2. ~~Is `globaloptions` checksummed?~~ **Answered: no** — the payload is loaded as-is, never repaired,
   never rewritten. What remains open: does an in-game AV change rewrite it immediately, or only at
   profile save? (The milestone-4 `restart` case shows no write when nothing changed, and a later
   session that changed an in-game option and then exited **gracefully** also left the container's
   timestamp untouched — though that session ran offline, so it does not settle the question.)
3. Are the guest's option values present, findable, in guest memory in a stable layout?
4. Is `vsync` re-read per present? Is `audio_mute` re-read per callback?
5. Does the guest keep consuming the pad while a host ImGui dialog is visible?
6. Does the F4 overlay already satisfy the actual need, making AV1–AV4 unnecessary?

Question 6 is the cheapest possible answer and should be asked before AV1 is scheduled.

---

## 9. Sources

**Read in this tree [tree]**

- `rexglue-sdk/include/rex/cvar.h` — lifecycle enum, default lifecycle, registry APIs, change
  callbacks, `REXCVAR_DEFINE_*` macros.
- `rexglue-sdk/include/rex/rex_app.h` — app hooks, including `OnCreateDialogs`.
- `rexglue-sdk/src/ui/rex_app.cpp` — `SetupOverlays`, `RegisterBind` set, `OnCreateDialogs` call
  site, shutdown unregister, `fullscreen` change callback, hard-coded audio backend.
- `rexglue-sdk/src/ui/overlay/settings_overlay.cpp` — category tree, badges, `SaveConfig`.
- `rexglue-sdk/src/ui/window.cpp`, `rexglue-sdk/src/ui/presenter.cpp` — window, display and presenter
  cvars.
- `rexglue-sdk/src/graphics/command_processor.cpp`, `flags.cpp`, `cache.cpp` — GPU cvars.
- `rexglue-sdk/src/audio/audio_system.cpp`, `sdl_audio_driver.cpp`,
  `include/rex/audio/downmix.h` — audio cvars, gain API, per-callback gain snapshot.
- `src/hooks/ultimate.cpp` — this repo's own cvar definitions, the pattern to copy.
- `src/rb_blitz_app.h` — which hooks this app currently overrides.
- `src/fs/payload_overlay.h`, `src/fs/overlay_merge.h`, `tests/payload_overlay_tests.cpp` — the
  read-only payload union device (AV4's only delivery route).
- `out/build/win-amd64-release/CMakeCache.txt` — `REXGLUE_ENABLE_FIDELITYFX=OFF`.
- `out/build/win-amd64-release/rb_blitz.toml` — the effective host config.
- `docs/vr-port-plan.md` — document structure and evidence convention.
- `docs/ultimate-compat.md` — the project's posture on payloads and derived assets.
- `DECOMPILATION_PLAN.md` — milestone framing and the deferred-items list.
- `docs/bringup-log.md` — the persistence matrix, the milestone-5 vsync pacing runs, and the payload's
  live DTA requests (`ulti_settings.dta`).
- `docs/button-mapping-plan.md` — the sibling scoping plan that owns input arbitration and documents
  the guest's own `Controls` screen (§1.3) and the host's input surface (§2).
- `docs/known-issues.md` — "a corrupt settings payload is loaded as-is", and the content-header case.
- `docs/rb3-references.md` — borrowed-hook catalogue: `OptionBool`/`OptionStr`,
  `Rnd__PreInit` sync override, `StreamChecksum__ValidateChecksum`.
- `docs/build-and-run.md` — the cvar TOML's location and its "the in-game settings overlay edits the
  same file" note.
- `prompts/04-menus-content-input-saves.md` — the `functions.toml` address-registration workflow and
  the `OptionBool`/`OptionStr` alternative to keystroke injection.
- `rb_blitz_manifest.toml` — `includes = ["config/functions.toml"]`, how guest addresses are fed to
  codegen.
- `scripts/acceptance_persistence.ps1` — the existing five-case storage harness to extend.

**Observed from the running build [cited]**

- Captures in `out/drive-ui/` (gitignored): the AV screen, its four rows, header/footer strings,
  highlight movement, frame-identical left/right input, `B`-returns-to-list behaviour, and the
  post-`refocus` input-revival behaviour.
- `game/gen/main_xbox_0.ark` name table: `AV_Options*.flow`, `options_audio_video.ep`, `*.sld`,
  `*.lst`, `.lbl`, `glow_*.mesh`.
- Boot log registering the `globaloptions:` content symlink.
- The save container under `Documents\rb_blitz\…\5841122D\00000001\globaloptions\`.
- Main-menu inventory, including the payload's `MOD SETTINGS` entry and its three pages: row types
  (checkbox, `Name: VALUE` cycle), nesting, and sample values.
- The milestone-5 pacing runs `out/m5-pacing/vsync-{on,off}/pacing-input.{json,md}` — the evidence
  that `vsync` genuinely changes frame pacing.
- The payload's `game:\ulti_settings.dta` request, evidence that payload DTA content is live.

**Tooling used**

- `scripts/drive_ui.ps1`, `scripts/ocr_image.ps1`, `scripts/frame_diff.ps1`,
  `scripts/capture_window.ps1`, `scripts/acceptance_persistence.ps1`.
