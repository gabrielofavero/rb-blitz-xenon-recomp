# Native menu mouse (R6)

`mouse_ui_nav=true` enables the menu cursor (default on; Controller Settings' Controller →
Mouse support setting). Hover selects a native list row directly. Left click emits
A, right click emits B, and wheel detents emit D-pad up/down (left/right for horizontal
powerup lists) — one discrete native step per detent, never an analog stick pulse. A
vertical list's scroll gutter — the strip just right of its rows, where the song library
draws its rail and thumb — takes clicks and drags. During `game_screen`, only
the focused `pause_panel` accepts mouse input. Transitions, focus loss and gameplay
discard pending events, partial wheel detents and button pulses.

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

A detent becomes a D-pad press, not stick travel. Retail steps a list once per
directional press, while a two-frame analog pulse depends on the guest's own
threshold and repeat handling — measured 2026-10-16: four one-frame right-stick
strikes on `song_select_screen` moved the library not at all, and one wheel detent
pulsed as `thumb_ly` could arrive as zero or two selection steps. Each detent now
pulses `kDown`/`kUp` (bit 2/1) through the same delivery-gated path a click uses,
so a burst of detents is a burst of native presses and the guest's own scroll
animation and end-of-list clamp apply.

The gutter beside a vertical list's rows (`MouseMenuRailHit`) is the scrollbar. A
press there is a drag, not a click: it never emits A. Dragging maps cursor travel
onto the captured row grid (`RailPitch`, `RailSlot`), one row of list per row of
mouse; a click that never moved puts the row under the cursor at the top of the
band. The selection rides with the viewport — a row of scroll is a row of
selection, so the highlighted slot stays on screen and the guest's inactive rows
(artist headings) are skipped — and the state is applied through
UIList::SetSelected plus the same scroll-start and scroll-complete notifications
hover uses, with the list clamping the value to its own last page (`824C1938`,
`824C1DE8`, `824C1F08`). The selected *slot* reads -1 while the selection sits
outside the band, which is why the drag anchors on the last row a draw positively
identified (`last_selected_row`, reset on every screen or focus change) rather
than on the live slot. The strip is published per draw and invalidated on a screen
or focus change, so a drag can never be applied to a retired list; drag frames
coalesce to the latest position and bypass the pad pulse gates because they do not
emit pad input.

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
pulse waits for its release), the rail row-grid math, press/release delivery gating
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

### Live acceptance, 2026-10-16 — wheel steps and the library scrollbar

The owner asked for the music library's scroll to behave natively: wheel
reliable, and the drawn rail/thumb clickable and draggable instead of
wheel-only. Same environment as the record above (Release, 1280x720 windowed,
local `game/dlc`, isolated profile under `out/mouse-fix-verify/`, `drive_ui.ps1`).

Wheel. Detents no longer pretend to be analog stick travel; each one pulses the
D-pad bit. The measurement that drove that: four one-frame right-stick strikes
at the song list produced no viewport or selection change at all
(`s15-dpad4.png`, no `mouse_probe` state lines), while the same physical input
as D-pad presses moved the list predictably. With the D-pad build, one detent is
exactly one song: parked on "(0.25, 0.30)", three downward detents moved the
selected *display slot* 0 → 2 → 4 → 6 → 8 and two upward detents returned 8 → 6
→ 4 — ±2 slots per song because the slot grid counts the disabled artist-heading
rows between songs (`scroll5.log`, `t0-t5*.png`). Selection slot and viewport
are separate: `firstShowing` stays 0 while the selection walks the band, which is
why the earlier readings of "two steps for one detent" were the viewport
catching up, not dropped/multiplied input.

Scrollbar. The strip is derived from what the draw captured, not from pixel
constants: the song rows' labelled bounds end at game x ≈ 0.567, the scene's
rail mesh sits at ≈ 0.595–0.606, and the published strip is `[right + .023,
right + .047]` — 0.590–0.614 — which covers the rail without touching the star
column (`railhit p=0.602 … rail=0.590,0.203,0.614,0.863 hit=true`,
`accept-final.log`). Driven with real drags and clicks:

- Rail click at y=0.35: viewport target 0 → 2, i.e. the clicked row comes to the
  top of the band (`f1-click-rail.png`).
- Drag y=0.30 → 0.50: three rows down (2 → 3 → 4 → 5); drag back y=0.50 → 0.30:
  three rows up (5 → 4 → 3 → 2) — one row of list per row of mouse
  (`f2-drag-down.png`, `f3-drag-up.png`).
- Rail click at y=0.60: +6 rows (2 → 8); click at y=0.45: +3 (8 → 11); the
  clicked row lands at the top each time (`f4-click-low.png`, `f5-click-mid.png`).
- Every apply carries the selection with it (traces show `row` moving with
  `first`), so the highlighted song and its album panel stay on screen and
  disabled artist headings are skipped. One earlier build shipped the flaw this
  fixes: passing the unchanged selection row let the guest set the selected
  slot to -1 (`selDisp=4294967295` in `scroll8.log`) and froze every following
  press; the drag now anchors on the last row a draw identified and moves the
  row and viewport together.
- Wheel still steps afterwards (±2 slots, `f6`/`f7`), and a rail press never
  emits A: the screen stayed `song_select_screen` through five rail
  interactions; one row click afterwards started the song (the same log's
  `available=false` transition into `powerup_select_screen`, `f8-row-click.png`).

Harness note for future scripted checks: `drive_ui.ps1` coordinates are
normalized to the window client, which for this configuration is wider than the
16:9 render (pillarboxed), so they map into game space as x' = 1.3345x − 0.167,
y' ≈ y (measured from the `hit=`/`railhit` traces, `scroll7.log`); the rail's
game x 0.59–0.61 is drive x ≈ 0.576.

Evidence: `out/mouse-fix-verify/` — `scroll5.log`, `scroll8.log`,
`scroll9.log`, `accept-final.log` and the captures named here. The 2026-10-16
crash probes that produced the temporary in-draw `mouse_probe` dumper are
superseded: projecting and logging inside `UIListDir::Draw` stalled frames hard
enough to desync the GPU ring twice (the `ExecutePacketType0/3 overflow …
PRIMARY RINGBUFFER` signature), a boot with raw-read-only logging was clean, and
the probe is removed from the shipped code. `rb_blitz_native_mouse_tests` now
passes 421 checks (the new cases: scrollbar queue coalescing that never drops a
press or release, scrollbar traffic flowing while a click pulse waits for its
release, and the rail row-grid math); all 38 CTest targets pass.
