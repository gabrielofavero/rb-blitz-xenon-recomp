# Resolution and scale expansion

**Status: planning, 2026-10-19. No code has been changed by this document.** The measured facts it
cites are the ones already in [`settings/graphics.md`](../../settings/graphics.md); every new size in
§4 is unmeasured until the packages in §5 run.

This plan enables the ultrawide/32:9 sizes discussed on 2026-10-17/19 and makes **4:3 a first-class
render shape**. The product still offers only what the machine can take; a diagnostic CLI/environment
override exists **only** for the owner's downscaled visual validation.

## 1. Request, normalized

| Id | Requirement |
| --- | --- |
| R1 | Enable these exact render sizes where a display takes them: **3440×1440**, **3840×1600**, **5120×2160**, **3840×1080**, **5120×1440**, **7680×2160**. |
| R2 | **4:3 is a requirement.** The game must render a true 4:3 frame (no stretch, no bars inside the window), and the standard 4:3 display modes must be usable, not only 16:9-derived sizes. |
| R3 | Enable the draw scales the sizes need: **1, 2, 3 and 4**. Scales 5–7 are not needed by any target and stay unoffered (D7). |
| R4 | The in-game Resolution row keeps its truthfulness rule: only sizes the machine/display can take, plus the producible catalogue trimmed to the primary. No universal fabricated list. |
| R5 | A **test-only** override (CLI/environment) may add sizes the primary display does not advertise, so a large render can be validated on a smaller panel. It is never a user setting and never writes the profile. |
| R6 | Testing uses the existing window fit: the render size stays exact while the window is shrunk to the work area with its aspect preserved, and the presenter scales the image down. Proportions are then measured in a capture. |
| R7 | The Resolution row's labels are the resolution token only. Remove **`(wide)`** (and the matching **`(tall)`** suffix) from the existing entries. |
| R8 | Exact render dimensions only: one integer draw scale per axis. No fractional scale and no output-size substitute. |
| R9 | No SDK patch: scale 4 is inside the existing integer model. |
| R10 | A change that alters what is offered owes the documentation updates named in §9. |

## 2. What exists today

**[tree]** The planner is `RenderPathLimits` and `PlanGuestScreen` in
[`src/settings/capabilities.{h,cpp}`](../../../src/settings/capabilities.h):

- `TitleRenderPath()` has base sizes `{960×540, 1280×720}` (both 16:9), scales `{1,2,3}`,
  `offer_ceiling {3840,2160}`, forced-guest window `540..720` high, `640..2048` wide, aspect
  `1.0..3.2`.
- An exact base × scale match is tried first; otherwise the width is forced beside a height in the
  window, and both requested dimensions must divide exactly by the scale.
- The offer is `RenderPathSizes()` (the catalogue) trimmed to the primary display, plus the
  primary display's enumerated modes that the planner accepts **and** whose aspect matches the
  primary's own aspect family. The family filter exists to avoid offering modes whose only effect
  would be presenter bars.
- The row's labels come from `ResolutionChoiceLabel` in
  [`src/hooks/settings_payload.cpp`](../../../src/hooks/settings_payload.cpp), which appends
  `" (wide)"` or `" (tall)"` to non-16:9 tokens.

**[measured]** The existing evidence, all D3D12 on the development machine's 2560×1080 21:9 panel:

- 2560×1080 renders as guest 1280×540 × 2 with no bars and no stretch, and the HUD re-anchor moves
  the six anchored elements (`out/_swap/ultrawide/final-uw.log`, `hud-verify4.log`,
  `graphics.md` part 3 §1, §9.2.5).
- 1680×720 × 1 is the plain forced-width case (`wide10`).
- 3440×1440 is arithmetic only: guest 1720×720 × 2 (`graphics.md` part 3 §5). It has never been
  booted.
- The window fit already shrinks a request to the desktop with its aspect preserved:
  `FitClientToDesktop` and its 4K-on-1080p case (`src/settings/window_policy.h`,
  `tests/window_policy_tests.cpp`). The presenter then scales the exact render into the fitted
  window.

**[unproven]** Scale 4, the 32:9 shapes, 8K output, and every 4:3 guest height above 720. Guest
height 1080 is measured to corrupt title-screen artwork (Part 3 §5), so a height the title has
never laid out at is the highest risk in this plan.

## 3. Decisions

| Id | Decision |
| --- | --- |
| **D1** | Widen `RenderPathLimits`: `scales = {1,2,3,4}`; `max_guest_width = 2560`; `max_aspect_x1000 = 3600` (32:9 is 3555); `offer_ceiling = {7680,2160}`. `max_guest_height` is **not** guessed: it starts at today's 720 and is raised to the highest value P2 measures safe (target 1050, which covers the standard 4:3 modes). |
| **D2** | Add the title's own 4:3 sizes as catalogue bases: **720×540** and **960×720** beside the two 16:9 bases. Their scale products make 4:3 sizes offerable on any display, exactly as 16:9 is today. |
| **D3** | Remove the primary-aspect family filter from `SupportedResolutions`. The offer becomes every **enumerated** mode the planner accepts and that fits the primary, plus the producible catalogue. Rationale: since the P2 window work the video mode equals the render size and the window is fitted to the same aspect, a 4:3 or 3:2 mode no longer produces bars — it produces a true frame of that shape. The filter is obsolete; keeping it would hide 4:3 modes a 16:9 display advertises. |
| **D4** | Add a diagnostic size override, parsed exactly like `DiagnosticGuestBases()`: `RBBLITZ_PROBE_RESOLUTIONS=WxH[,WxH...]` (optional `--probe_resolutions=` if the boot's flag path is trivial). It appends the named sizes to the capability snapshot **after** the display trim, deduplicated and ordered. It cannot make the planner accept a size it refuses; it only bypasses "the monitor did not advertise this". It logs a warning naming every size, is unset in production, and is never written to the profile. |
| **D5** | The 4:3 modes above 720 and 3840×1600 need guest heights 768/800/864/960/1050. P2 probes them in descending order and sets `max_guest_height` to the highest that passes the title-screen art check. A mode whose height fails stays refused and is recorded in [known-issues.md](../../reference/known-issues.md); it is not guessed into the offer. |
| **D6** | `ResolutionChoiceLabel` becomes the token alone (delete the suffix branch and its comment). Extract it to a small pure helper so the rule is unit-tested; if extraction is deferred, the boot log's `Resolution offers …` line is the evidence. |
| **D7** | Scales 5–7 stay unoffered. They produce no target size within the ceiling, and every extra scale is an unmeasured render path. |
| **D8** | No SDK patch and no fractional scale. Scale 4 already fits `kMaxDrawResolutionScaleAlongAxis` (1..7) and the 3-bit resolve packing (`graphics.md` part 1 §6). |
| **D9** | `Auto` keeps its meaning: the largest offered size that fits the display. On a display that advertises a 4x size, Auto will pick it; the boot log names the plan and the cost is measured, not benchmarked. No new benchmark is introduced. |
| **D10** | The product offer remains display-driven. The override exists for validation only; a build with it unset behaves exactly as today except for the new sizes the display already advertises and the catalogue's 4:3 entries. |

## 4. Target matrix

Guest plan and current planner verdict **[tree]**; every verdict after a limit change is arithmetic
until the package boots it. "Gate" means the result depends on P2's height probe.

### 4.1 The requested ultrawide/32:9 sizes

| Request | Guest × scale | Verdict today | After D1 | Measurement |
| --- | --- | --- | --- | --- |
| 3440×1440 (21:9) | 1720×720 × 2 | **accepted** | accepted | P3 boot + HUD |
| 3840×1600 (24:10) | 1920×800 × 2 | refused: height 800 | gate (800) | P2 then P3 |
| 5120×2160 (21:9) | 1280×540 × 4 | refused: scale 4; ceiling | accepted | P3 (also proves scale 4) |
| 3840×1080 (32:9) | 1920×540 × 2 | refused: aspect 3555 > 3200 | accepted | P3 (first 32:9) |
| 5120×1440 (32:9) | 2560×720 × 2 | refused: width 2560; aspect; ceiling | accepted | P3 (32-tile pitch) |
| 7680×2160 (32:9) | 1920×540 × 4 | refused: scale 4; aspect; ceiling | accepted | P3 (8K output) |

### 4.2 The 4:3 acceptance set

The title's own 4:3 branch derives `height × 4 / 3`; the planner forces the width, so any 4:3 size
in the height window is a true forced frame. The catalogue entries (D2) are marked **C**; the rest
are standard display modes and appear when the display advertises them (or via the override).

| Request | Guest × scale | Verdict today | Needs |
| --- | --- | --- | --- |
| 720×540 **C** | 720×540 × 1 | accepted | catalogue entry |
| 960×720 **C** | 960×720 × 1 | accepted | catalogue entry |
| 1440×1080 **C** | 720×540 × 2 | accepted | catalogue entry |
| 1600×1200 | 800×600 × 2 | accepted | display mode / override |
| 1920×1440 **C** | 960×720 × 2 | accepted | catalogue entry |
| 2160×1620 **C** | 720×540 × 3 | accepted | catalogue entry |
| 2880×2160 **C** | 960×720 × 3 | accepted | catalogue entry |
| 1024×768 | 1024×768 × 1 | refused: height 768 | gate (768) |
| 2048×1536 | 1024×768 × 2 | refused: height 768 | gate (768) |
| 1152×864 | 1152×864 × 1 | refused: height 864 | gate (864) |
| 1280×960 | 1280×960 × 1 | refused: height 960 | gate (960) |
| 2560×1920 | 1280×960 × 2 | refused: height 960 | gate (960) |
| 1400×1050 | 1400×1050 × 1 | refused: height 1050 | gate (1050) |

3840×2880 (960×720 × 4) is a true 4:3 product but exceeds the 2160 offer ceiling; it is out of
scope and stays refused. 5:4 (for example 1280×1024) is not part of R2; because D3 removes the
family filter it would appear if a display advertises it and the planner accepts the shape
(1.25 is inside the aspect window) — that is a byproduct of the display-driven rule, not a target.

## 5. Packages

Order: **P0 → P1 → P2 → P3 → P4 → P5**. P1 can land before P2 with `max_guest_height = 720`;
P2 then raises it. P3 cannot run before P1 and P2.

| Package | Work and owners | Completion evidence |
| --- | --- | --- |
| **P0. Diagnostic override and downscale harness** | Add `DiagnosticResolutions()` beside `DiagnosticGuestBases()` in `src/settings/capabilities.{h,cpp}`; append it to the snapshot after `SupportedResolutions()` in `BuildCapabilitySnapshot` (or in `SettingsBoot` immediately after the snapshot, if keeping the pure compiler untouched is preferred). Log one warning naming the sizes. Add a tracked `scripts/acceptance_resolutions.ps1` (model: `out/_swap/ultrawide/run.ps1` + `scripts/acceptance/acceptance_controls_in_song.ps1`) with `-Request WxH`, `-InSong`, `-Seconds`, a disposable profile/DLC root, and the env var. Unit tests for the parser and for "appended but still refused if the planner says no". | A boot on the development machine's 2560×1080 primary requesting **3440×1440** with the override: the log shows the override, `guest render base 1720x720`, `swapdims … resolve_dest_scaled=3440x1440`, and `settings_boot: window … client … physical` fitted inside the work area; a title capture where the reference disc is round. This proves the downscale path before any refused size is enabled. |
| **P1. Planner, 4:3 catalogue, offer rule, labels** | D1 (except the height value), D2, D3, D6, D7. Owners: `src/settings/capabilities.{h,cpp}`, `src/hooks/settings_payload.cpp`, a small pure label helper if extracted, `tests/settings_graphics_tests.cpp`, `tests/window_policy_tests.cpp`. Add planner cases for all §4 sizes with the height-gated ones marked, the 4:3 catalogue, the family-filter removal, and the token-only label. | `ctest` green. With the override unset on the 2560×1080 primary, `settings_menu: Resolution offers …` contains only sizes that fit; the row's labels carry no `(wide)`/`(tall)`. A synthetic 4:3 mode list produces the 4:3 entries. |
| **P2. Height gate and 4:3 layout** | Probe guest heights **1050, 960, 864, 800, 768, 720** (descending) using `RBBLITZ_PROBE_GUEST_BASES` plus the D4 override, one boot each at the corresponding 4:3/24:10 output. Check the title screen for artwork corruption, the menu, a song, and the HUD; check `render: layout aspect` and the six HUD elements. Set `max_guest_height` to the highest height that passes and record every failure. Owners: `src/settings/capabilities.h`, `src/hooks/hud_layout_runtime.cpp` (measurement only), `docs/reference/known-issues.md`. | A table of height → verdict with evidence paths; the final constant; 4:3 HUD composition (elements keep their edge distance; negative delta moves them inward) at 720 and at the passing heights. |
| **P3. Full size matrix** | Boot every size in §4.1 and the reachable §4.2 rows through `acceptance_resolutions.ps1`, title + song, D3D12; capture the title (disc), the menu, and an in-song HUD frame. Check `swapdims`/`rtdims`, `render: layout aspect`, `settings_boot: HUD re-anchor shift`, no guest fault, and a save + **Restart now** into the same size. | One evidence directory per size under `out/_swap/resolutions/<label>/` (log, shots) plus a summary table. Scale 4 and the 32:9 shapes are the named proof points. Vulkan is attempted only if the same build is usable; otherwise it is recorded as unproven. |
| **P4. Product offer, Auto, and honesty checks** | With the override unset, verify the row on the development primary; unit-test a synthetic 4:3 display list and a synthetic 32:9 list; verify Auto picks the largest offered fitting size and logs it; verify a request for a size the display does not advertise is corrected (not silently booted) when the override is unset. | Generated-menu/snapshot tests for the three display shapes; a boot log for the real primary; a corrected-request arm in the evidence. |
| **P5. Documentation and close** | Update the documents in §9, move this plan to `in-progress/` when P0 starts and `finished/` when P3/P4 pass, update the index, run the checker. | `check_docs.py` exit 0; `git diff --check`; every evidence path in the plan exists. |

## 6. Validation matrix (per size)

| Check | What it proves | Evidence |
| --- | --- | --- |
| `settings_boot: test resolution override` + `guest render base WxH, draw scale N` | the request reached the planner and planned exactly | log |
| `swapdims … resolve_dest_scaled=WxH … draw_scale=NxN` | the resolve destination is the requested size, not an output-size substitute | log |
| `settings_boot: window … client … physical` + `SameAspect` | the window was fitted without changing the shape | log + `window_policy_tests` |
| title capture, reference disc | no stretch (round disc), no bars inside the window | PNG + `measure.ps1` |
| menu capture | the UI is laid out in the frame's aspect; no 16:9 squeeze | PNG |
| in-song capture | the 3D world expands; the six HUD elements keep their nearest-edge distance (4:3 moves inward, wide moves outward) | PNG + `hud_probe.py`/pixel columns |
| save + **Restart now** | the choice persists and the child boots the same size | log + profile |
| no `access violation` / `Unhandled guest` | the size does not fault the guest | log |

Performance at scale 4/8K is measured as a frame-time note, not a gate. The guest tick and V-Sync
are unchanged.

## 7. The test override, precisely

- **Name:** `RBBLITZ_PROBE_RESOLUTIONS` (comma-separated `WxH`). An optional
  `--probe_resolutions=` flag is added only if the existing cvar/flag path makes it free; the
  environment form is the required one because the existing diagnostic
  (`RBBLITZ_PROBE_GUEST_BASES`) already uses it.
- **Where it acts:** after the capability snapshot's truthful offer set is compiled, append the
  named sizes, deduplicate, and sort with the same order as the rest of the list. Nothing else
  changes. `PlanGuestScreen` still decides whether the size is renderable; a size it refuses stays
  refused and the boot says so.
- **What it does not do:** it does not change the planner limits, does not write the profile, does
  not become a row on a production build, and does not alter the display probe.
- **Visibility:** the row reads the snapshot, so a test build shows the overridden sizes. That is
  intentional for the owner's visual pass and is why the variable is warning-loud and unset in
  production.
- **Downscale:** `ApplyWindowPolicy` already writes the video mode = the render size and fits the
  window into the work area with the aspect preserved. On a 2560×1080 work area a 7680×2160
  request becomes a 2560×720 window; the presenter scales the 7680×2160 image into it. A capture
  therefore shows the full composition at reduced pixel size, which is exactly what validates
  proportion, HUD anchoring, and bars/stretch.

## 8. 4:3 details

- The title's own width derivation has a 4:3 branch, but the product seam forces the width for any
  size that is not 16:9 of the height (`SetBootRenderFrame`). Both mechanisms end at the same 4:3
  guest frame; the width hook is the one the boot uses.
- `WriteGuestLayoutAspect` writes `1/aspect` (0.75 for 4:3) into the two measured layout constants;
  `HudShiftUnits` is negative, so the HUD rule moves off-centre elements **inward** until they
  reach the 4:3 edge, exactly as `src/hooks/hud_layout.h` consequence 2 describes. That header's
  boundary case (an element whose authored distance is larger than the 4:3 half-width stops at the
  centre) is what P2 must check on a real frame.
- A 4:3 video mode plus a 4:3 window means no presenter bars; the game is not letterboxed inside
  its own window. The Ultimate gameplay letterbox setting is a separate feature and is not
  touched.

## 9. Documents this change owes

| When | Document | What changes |
| --- | --- | --- |
| Plan lands | [`docs/README.md`](../../README.md) | one row in the Plans table |
| P1/P3 land | [`docs/settings/graphics.md`](../../settings/graphics.md) part 3 §5 | the planner limits, the worked-examples table, the 4:3 catalogue and the offer rule |
| P2/P3 land | `graphics.md` part 3 §1, §9, §10 | measured sizes, the height verdict, HUD at 4:3/32:9, evidence index |
| P1 lands | [`docs/settings/status.md`](../../settings/status.md) | the live record's current resolution state |
| P0 lands | [`docs/engine/diagnostics.md`](../../engine/diagnostics.md) | the `RBBLITZ_PROBE_RESOLUTIONS` instrument and its limits |
| P2 lands | [`docs/reference/known-issues.md`](../../reference/known-issues.md) | any height/mode the probe refuses, with the measurement |
| If deferred | [`docs/reference/known-issues.md`](../../reference/known-issues.md) | modes left unsupported and why |
| P1 lands | [`docs/settings/plan.md`](../../settings/plan.md) §7 | the requirement record now includes 4:3 and the wider set |

No toggle is introduced, so [`docs/engine/toggles.md`](../../engine/toggles.md) is unchanged. The
`settings_payload.cpp` label comment is edited with D6.

## 10. Open questions

1. **The exact 4:3 acceptance set.** This plan proposes the standard modes in §4.2. If the owner
   only needs the display's own modes, the height gate can stop at 768 or 800; the catalogue's
   4:3 entries (720×540/960×720 families) are unaffected.
2. **`(tall)` as well as `(wide)`.** R7 removes both; if the owner wants `(tall)` kept for 4:3, it
   is a one-line difference in D6.
3. **A CLI flag in addition to the environment variable.** The environment form is required for
   the harness; the flag is added only if the boot's cvar/flag path already carries diagnostic
   values without a new cvar.
4. **How far Auto should go.** On a display that advertises an 8K-class mode, Auto will choose the
   largest offered size. If that is unwanted, the cap belongs in the Auto rule, not in the planner.

## 11. Reproduction

```powershell
# from the repository root
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
ctest --test-dir out\build\win-amd64-release
python .github/skills/documentation/scripts/check_docs.py

# one downscaled arm (the environment variable is read by the boot)
$env:RBBLITZ_PROBE_RESOLUTIONS = '3440x1440,5120x2160,7680x2160'
pwsh -NoProfile -File scripts\acceptance\acceptance_resolutions.ps1 -Label uw-3440 -Request 3440x1440
Remove-Item Env:\RBBLITZ_PROBE_RESOLUTIONS

# a 4:3 height probe (guest base + the requested output)
$env:RBBLITZ_PROBE_GUEST_BASES = '1024x768'
$env:RBBLITZ_PROBE_RESOLUTIONS = '1024x768'
pwsh -NoProfile -File scripts\acceptance\acceptance_resolutions.ps1 -Label a43-768 -Request 1024x768
Remove-Item Env:\RBBLITZ_PROBE_GUEST_BASES, Env:\RBBLITZ_PROBE_RESOLUTIONS
```

`acceptance_resolutions.ps1` is the tracked deliverable of P0; until it exists, the scratch
harness `out/_swap/ultrawide/run.ps1` is the model. Evidence goes under
`out/_swap/resolutions/<label>/` so the plan and `graphics.md` can cite it.
