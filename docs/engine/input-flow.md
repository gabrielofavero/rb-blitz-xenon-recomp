# Native menu mouse (R6)

`mouse_ui_nav=true` enables the menu cursor (default on; Controller Settings' Controller →
Mouse support setting). Hover selects a native list row directly. Left click emits
A, right click emits B, and wheel detents step non-scrolled lists (left/right for
horizontal lists like the powerup rows). A vertical list that draws a scroll rail —
the song library — follows the desktop scrollbar rule instead: the wheel, a thumb
drag and a track click all move the view and never the selection. Gameplay hides
the cursor: only the focused `pause_panel` accepts mouse input. Transitions, focus
loss and gameplay discard pending events, partial wheel detents and button pulses.

The old screenshot detector, hover aligner, relative-motion stepper and their
configuration flags have been removed. `enhancements_native_mouse` was an unused
placeholder; R6 uses `mouse_ui_nav` as its single switch.

## Retail engine path

The addresses below refer to the supported retail executable. Hooks live in
`src/input/native_menu.cpp`; host pointer handling lives in `src/input/mouse_ui.cpp`.

| Address | Role |
| --- | --- |
| `824B2958` | UIManager::Poll; consume one queued mouse event on the guest thread |
| `824B1980` | UIManager::FocusComponent; identifies the active native list |
| `824E5020` | UIListDir::Draw; scopes geometry to the focused list being rendered |
| `824E07A8` | UIListWidget::CalcXfm; identifies the row whose transform is being composed |
| `824E9A90` | UIListLabelElement::Draw; exposes the final row transform and text rectangle |
| `824BFEF0` | UIListMeshElement::Draw; captures icon-only powerup rows |
| `823F0118` | RndMesh::OnSync; retains local mesh bounds before upload releases vertices |
| `824E0900` | UIListWidget::DrawMesh; captures the native highlight cell |
| `821E14A8` | Transform a local corner to world coordinates |
| `821FF4A0` | RndCam::WorldToScreen; project through the actual rendering camera |
| `824E3008` | UIListState::Selected; read the logical selected row |
| `824C1938` | UIList::SetSelected; select the hovered logical row directly |
| `824C1DE8` | UIList::ScrollStart; notify scripts so powerup descriptions follow selection |
| `824C1F08` | UIList::ScrollComplete; update providers and notify the panel |

UIManager stores its current screen at +44 and transition state at +16. The screen's
focused panel is at +52. The focused component pointer is the UIList base itself.
UIListState is at list +176. Draw-state records are 44 bytes: valid flag +0, position
+4, alpha +20, widget state +24, component state +28, display index +32, logical
index +36 and provider data index +40. The vector begins at draw-state +56.

The label element points to its label at +8. The label's text rectangle at +144
contains local x/z and width/height. Its final transform is the Draw call's r4.
The retail bounding-box branch omits that transform, so calling CalcBoundingBox
alone gives incorrect menu hit regions. Capture reads the normal draw path and
performs isolated projection calls; it does not render extra frames or infer rows
from pixel colors. RndCam::sCurrent is at `8284A108`.

Mesh bounds are captured from CPU vertices (96-byte stride) or packed Milo vertices
(36-byte stride), before static meshes discard that data. Geometry-owner and GPU
resource identity guard the bounded cache. Icon and highlight bounds use the mesh's
current world transform, then the rendering camera. This avoids the uninitialized
box returned by retail CalcBox after CPU vertices have been discarded. The highlight
extends short labels to the full native button width; disabled artist headings still
participate in hit testing and cannot select neighboring songs.

Hover preserves UIListState's first visible row (+48, list +224), then sends both
scroll and scroll-complete notifications. It neither recenters the viewport nor
steps through intervening choices. Powerup submenus retain native A/B semantics:
click a category to open it, click a choice to equip it, or right-click to go back.

Window coordinates are mapped back through the presenter's aspect, letterbox and
overscan rectangle. Only geometry from the immediately preceding guest frame and
the same focused list can select a row. Hover never emits directional input.

## Clicks, wheel and remapping

Window events contain coordinates and actions, never guest object pointers. The
guest thread owns geometry and native UI calls. A synchronized queue bridges the
window thread to UIManager::Poll. Moves coalesce without crossing click events.
Click selection happens before the A pulse. A pulse — and its release — is gated
on delivery: the pressed state is held until the pad state filter, the last stop
before the guest (`src/input/remap.cpp`), has sampled it, and never shorter than
two UI frames; the release is then held until that filter samples it too, and
only then may the next queued event pulse. A press cannot expire before the
guest's input thread polls, and a second click cannot ride the first press's
release. Because upstream device merging keeps the largest device packet number,
an idle physical pad's counter could hide a mouse press from a guest that skips
input with an unchanged packet number; the filter therefore derives the packet
number it delivers from the state it hands over (`src/input/pad_packet.h`). Wheel bursts
preserve individual detents and fractional deltas accumulate. While the native viewport
is animating (state +48 differs from +52), the next wheel detent stays queued;
button release still runs immediately. This prevents fast bursts losing steps at
the visible edge of a song or powerup list.

On every list that is *not* a vertical scrolled list — the horizontal powerup rows,
for instance — a detent becomes a D-pad press, not stick travel. Retail steps a
list once per directional press, while a two-frame analog pulse depends on the
guest's own threshold and repeat handling — measured 2026-10-16: four one-frame
right-stick strikes on `song_select_screen` moved the library not at all, and one
wheel detent pulsed as `thumb_ly` could arrive as zero or two selection steps.
Each such detent pulses `kDown`/`kUp` (bit 2/1) through the same delivery-gated
path a click uses, so a burst of detents is a burst of native presses and the
guest's own scroll animation and end-of-list clamp apply.

A vertical list that draws a scroll rail — the song library — takes the native
scrollbar rule instead (owner decision, 2026-10-16): the wheel and the rail move
the *view* and never the selection, like a desktop scrollbar. The selection stays
where it was, even when it is scrolled out of the band; the guest reports that as
selected slot -1, which the mapping tolerates because every scroll application
passes the last row a draw positively identified (`last_selected_row`, reset on
screen or focus change) rather than the live slot. The wheel moves one list row
per detent — `firstShowing` counts rows, so a detent is one row on any library and
the fraction of the list it covers shrinks as the library grows. Successive
detents advance the *pending target* (list +228) so a burst is not lost to the
animation.

The rail itself is mapped from the thumb's own geometry (`docs` record below):
the thumb is one row pitch tall and its top crosses the rail as the first visible
row walks 0..maxFirst, where `maxFirst` is the list's own last page. It comes from
the provider's `NumData()` minus the visible row count — Blitz puts `NumData` at
vtable slot 10 of `UIListProvider` (RB3's layout has one extra virtual before it),
and the call is only made after the rebuilt slot is checked against the known
recompiled address (`822DAE10`); a refused answer leaves the gutter inert rather
than guessing. With that map a track click slides the thumb to the cursor, a press
on the thumb grabs it (no jump on release) and a drag moves the view row-for-row
with the cursor, clamped to `[0, maxFirst]`. Applications go through
UIList::SetSelected plus the same scroll-start and scroll-complete notifications
hover uses (`824C1938`, `824C1DE8`, `824C1F08`). A press on the rail is a scroll,
not a click: it never emits A. The strip is published per draw and invalidated on
a screen or focus change, so a drag can never be applied to a retired list; drag
frames coalesce to the latest position and bypass the pad pulse gates because
they do not emit pad input.

Leaving a drawn row clears the mouse's own hover (the trace logs `unhover`), and a
left click only emits A when the pointer is on a row that a draw of the currently
focused list identified in the same frame; on empty space, on a stale hover or on
the rail strip the press is swallowed (`MouseMenuScrollZone` in `mouse_ui.cpp`).
The retail selection highlight is the engine's — it follows the selected row — so
the mouse's unhover is behavioural, not cosmetic: writing the selected slot to -1
was tried and discarded because the engine recomputes the slot every update.
Screens with no captured list — the title screen's *PRESS (A) TO START* — keep
the at-principle A, so a mouse-only boot still works.

The synthetic mouse device establishes controller connectivity. Fixed mouse actions
are merged after user controller remapping, for player one only. Raw mouse remap
sources are suppressed while native menu mouse support is enabled, including during
gameplay. Keyboard and controller bindings continue through their existing path.
ImGui pointer capture and window focus loss clear the queue. Cursor visibility is
applied on the window thread through SDL.

## Checks

`rb_blitz_native_mouse_tests` covers pointer mapping, row hit tests, disabled rows,
move/click ordering, wheel bursts, fractional detents, scrollbar drag coalescing
(press/release never dropped, drags latest-wins, traffic flowing while a click
pulse waits for its release), the rail thumb math (travel per step, first-from-y
with both clamps, the on-thumb grab test), press/release delivery gating
behind a delayed input thread, per-player final packet identity and boundary
resets. `scripts/drive_ui.ps1` supports normalized `mouse:x:y`, `click:left/right`,
`drag:x1:y1:x2:y2`, `wheel:detents` and `cursor:visible/hidden` actions for live
acceptance checks.


### Live acceptance, 2026-10-13

Windows Release build, windowed 1280x720, Ultimate enabled, isolated
`out/mouse-native/user2` profile and the local `game/dlc` library. Verified with
real pointer moves, button clicks and wheel events through `scripts/drive_ui.ps1`:

- Hover selects all four powerup categories. Clicking opens the native submenu;
  hovering updates its description and clicking equips the choice. Note and
  overdrive selections were carried into the song loading screen.
- From Super Guitar, two rapid downward detents reach Super Keys, including both
  viewport animations. Six upward detents return to No Power-Up Selected. This
  specifically reproduces the previously dropped second detent.
- Song rows accept hover across the full bar, including the ratings area. Artist
  headings do not move selection. Hovering another visible song preserves the
  viewport, and wheel bursts scroll the library.
- Left click starts the selected song and dismisses the loading prompt. During
  gameplay the OS cursor is hidden; both buttons and wheel leave the game running
  with no mouse control input. Keyboard Start pauses; the cursor becomes visible
  and hover directly selects pause-menu rows. Left-click Resume and right-click
  Back were also exercised.

Latest run: `out/mouse-native/acceptance-final.log`, with captures
`track-wheel-preserved.png`, `track-wheel-start.png`, `final-library-row.png`,
`final-library-header.png`, `final-game.png` and `final-pause.png` in that directory.
These local validation artifacts are ignored by Git. The test instance was left
paused after the checks.

The native mouse executable passes 152 assertions. CTest passes all seven relevant
suites: `native_mouse`, `controller_settings_remap`, `controller_settings_launch`, `controller_settings_nav`,
`settings_service`, `settings_store` and `settings_migration`.

### Live acceptance, 2026-10-16 — click delivery

The owner reported that hovering the powerup categories worked but clicking did
not open them, while the controller did, and that confirmation prompts elsewhere
sometimes needed a second click. Two delivery defects were measured and fixed: a
press could expire after two UI frames before the guest's separate input thread
sampled the pad state, and the merged packet number could stay unchanged while a
mouse button changed — upstream merging keeps the largest device packet number,
so an idle physical pad's counter hid the mouse's smaller one — in which case the
guest's poll wrapper treated the state as unchanged (`sub_82504AA0` in
`generated/default/rb_blitz_recomp.67.cpp` compares the new packet number with
the old one) and skipped it.

Windows Release build, windowed 1280x720, Ultimate with the local `game/dlc`
library, a physical controller connected and an isolated profile
(`out/mouse-fix-verify/userdata`) — the controller is what re-created the
intermittent loss; the previous session's same-environment probe had one click
swallowed (`out/mouse-click-fix/baseline-powerups.png` still on the main menu)
and the next one acted (`baseline-click2.png`, powerup screen). Driven end to end
through `scripts/drive_ui.ps1`, one left-click (or right-click/wheel) per step,
this build:

- A single click on the title's START started the game, and a single click on
  *Play Current Song* reached the powerup screen (`06-click-play.png`).
- One click opened the Overdrive category (`08-click-overdrive.png`) and one
  click opened Note (`09-click-note.png`); right-click closed each; hovering a
  choice plus one click equipped it — the description panel read *BLAST NOTES*
  after the click (`11-cell2.png`) — and the *No Power-Up Selected* cell
  deselected it.
- Two wheel detents moved the category selection two steps, Overdrive to Track,
  while the equipped Note choice stayed (`12-wheel.png`).
- Open → right-click close → reopen delivered all three edges within ~1.5 s
  (`13-reopen.png`), the fast second-click case that used to need repeats.
- One click on *Exit Game* raised the confirmation (`16-exitconfirm.png`); one
  more click on YES closed the process ("Execution complete"), where the owner's
  pre-fix experience had been confirmation prompts needing repeat clicks.

Evidence: `out/mouse-fix-verify/` — `verify2.log` (the `native_mouse` trace,
with a screen/focus transition for every click above) and the captures named
here. One boot of the same build died in its content discovery with the guest
audio thread's null read (`Unhandled guest access violation: read of guest
0x00000000 … thread 0xF8000028`, kept as `verify-crash1.log`) — the same signature
as `out/_swap/p5b/lab1.log` (2026-10-11, thread `F8000028` is the MOGG path per
[diagnostics.md](diagnostics.md)) — and did not reproduce on the next boot; not
attributed to this change.

`rb_blitz_native_mouse_tests` passes 406 checks (its runner's own count); all 38
CTest targets pass in `out/build/win-amd64-debug` — the tree whose configure
keeps tests on, because a Vulkan-enabled configure forces `BUILD_TESTING` off
from the SDK's `thirdparty/CMakeLists.txt` and the release tree cannot run them.

### Live acceptance, 2026-10-16 — the library scrollbar behaves natively

The owner asked for the music library's scroll to behave like a desktop
scrollbar: wheel, thumb drag and track click all move the *view*, and the
selected song must not move with it (that is what the controller is for). Same
environment as the record above (Release build, windowed, local `game/dlc`
library, isolated profile under `out/mouse-fix-verify/`, driven with
`drive_ui.ps1`).

Calibration. The thumb is a light block on the rail; `thumb_scan.py` in the
evidence directory finds it in a window capture. Against the list state:
thumb top 255px at `firstShowing` 0, 457px at 15, 659px at 30, 807px at 41 with
a 62px thumb — 13.457px of thumb travel per first-row step, with the thumb one
row pitch (62px) tall. The engine's own clamp gives
`maxFirst` 41 for this 51-row library (`NumData` 51 from the provider vtable,
minus the 10 visible rows; a `first` of 41 is where repeated bottom clicks stop
and where the thumb touches the rail's bottom edge). In game units the rail is
`[0.2175, 0.8348]`, the thumb 0.0624, and travel-per-step
`(rail − thumb) / maxFirst` — 0.013534, within 0.2% of the measured slope. The
provider call is `UIListProvider::NumData` at vtable slot 10 (`822DAE10`: return
`(this+56 - this+52) / 4`; RB3's header has one extra virtual before `NumData`),
guarded by comparing the rebuilt slot against that address before calling, and
cross-checked against a base-songs-only boot (7 rows, `maxFirst` 0, hence an
unscrollable rail).

Measured across the calibration boot and the two follow-up boots (`native2.log`
+ `n*` captures; `hover5.log`; `final1.log` + `z*` captures):

- Wheel: one detent = one row of view. Three detents stepped the target
  0 → 1 → 2 → 3 with the thumb at 296px (295.4 predicted) and six detents
  reached 6 with the thumb at 336px (335.7 predicted); every apply logged
  `row=0` — the selection never moved — and the album panel still read
  *Random Song* after six rows of scrolling (`final1.log`, `z1-wheel3.png`,
  `z2-wheel6.png`).
- Track click at y=0.60: the thumb slid to 632px against 631.8 predicted, i.e.
  the thumb top lands at the cursor (`n3-trackclick.png`, re-checked in
  `z3-trackclick.png`).
- A press on the thumb does not jump: a second click at the same point left it
  at 632px (`n4-thumbclick.png`).
- Drag: y 0.50 → 0.70 moved the view +15 first-steps and clamped at the rail's
  bottom (thumb 807..867px); a further drag to y 0.95 stayed clamped
  (`n5-dragdown.png`, `n6-overscroll.png`). Dragging back 0.80 → 0.40 moved −30
  to `first` 11 (thumb 403px against 403.0 predicted) (`n7-dragup.png`); a drag
  y 0.50 → 0.62 moved 28 → 37 (8.9 rows expected) and ended at 753px
  (752.9 predicted) (`z4-drag.png`).
- A rail press never emits A — the screen stayed on the library through every
  rail interaction — and a row click afterwards still started the song (the
  `available=false` transition into `powerup_select_screen`; re-confirmed in
  `final1.log` with `z5-rowclick.png`).
- Leaving the rows logs `unhover` and unhovered clicks press nothing: after the
  pointer left the row band (x 0.30 → 0.90, `row=-1`, `unhover list=44188448`)
  a click on empty space and a click on the strip between rows and rail both
  left the screen on `song_select_screen` (`hover5.log`).
- The powerup screen (horizontal, no rail) keeps the D-pad detent path: one
  wheel detent still steps the category selection one cell
  (`nC-powerup-wheel1.png`).

Harness note for future scripted checks: `drive_ui.ps1` coordinates are
normalized to the window client, which for this configuration is wider than the
16:9 render (pillarboxed), so they map into game space as x' = 1.3345x − 0.167,
y' ≈ y (measured from the `hit=`/`railhit` traces, `scroll7.log`); the rail's
game x 0.59–0.61 is drive x ≈ 0.576.

Evidence: `out/mouse-fix-verify/` — `native2.log` (every scroll application and
the rail map), `calib.log` + `c0..c4*.png` (the firstShowing↔thumb points),
`numdata2.log` (the live `NumData`=51 check), `n0..n8*.png`, the follow-up
boots `hover5.log` (unhover, click gating) and `final1.log` + `z1..z5*.png`
(one-row detents, rail mapping re-check), and the `thumb_scan.py` that measures
the thumb. The earlier same-session probes that
projected meshes inside `UIListDir::Draw` stalled frames hard enough to desync
the GPU ring twice (the `ExecutePacketType0/3 overflow … PRIMARY RINGBUFFER`
signature) — raw-read logging boots clean and no probe ships.
`rb_blitz_native_mouse_tests` passes 428 checks (scrollbar queue coalescing,
pulse-flow interplay, and the rail thumb math: travel per step, first-from-y
with both clamps, and the on-thumb grab test); all 38 CTest targets pass in
`out/build/win-amd64-debug`.
