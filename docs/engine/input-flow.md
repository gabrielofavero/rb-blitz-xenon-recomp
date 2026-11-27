# Native menu mouse (R6)

## Render scale wheel input (2026-11-26)

[tree] `src/input/native_menu.cpp` captures UISlider hit bounds after actual
`UISlider::DrawShowing` at `0x824DC1E8`, replacing the traversal hook at
`0x824DC2F0`. Step math reads the active slider's native count at offset +132;
its current index is at +128. Render Scale uses Audio's profile buttons and a
single bottom wheel. Clicks choose a stop, dragging clamps at either end, and
mouse-wheel detents adjust the value. Touch hit bounds have a minimum height of
44 window units. Pointer edits retain their intended stop through button-to-wheel
focus changes, and fresh button geometry remains available while the wheel has
focus. Screen changes discard pending pointer edits.

[unverified] These paths have host arithmetic/state checks; actual game input,
physical touch hit areas and device visual acceptance require a built-game test.

`mouse_ui_nav=true` enables the menu cursor (default on; the in-game Controls →
Mouse support setting). Hover selects a native list row directly. Left click emits
A, right click emits B, and wheel detents step non-scrolled lists (left/right for
horizontal lists like the powerup rows). A vertical list that draws a scroll rail —
the song library — follows the desktop scrollbar rule instead: the wheel, a thumb
drag and a track click all move the view and never the selection. Gameplay hides
the cursor: only the focused `pause_panel` accepts mouse input. Transitions, focus
loss and gameplay discard pending events, partial wheel detents and button pulses.

The library's visible headers (Your Songs, RhythmVerse, Messages and Recommended)
also accept left clicks. Their hit boxes come from the guest's own label draw,
projected through its current world transform and camera. A click goes directly
to the chosen tab through the authored header transition, preserving departure
work such as stopping store previews. Hidden headers and the active tab do not
navigate; a header click never presses A on the selected song.

The old screenshot detector, hover aligner, relative-motion stepper and their
configuration flags have been removed. `enhancements_native_mouse` was an unused
placeholder; R6 uses `mouse_ui_nav` as its single switch.

## Touch list scrolling

The song list, its search results, the shortcut filter list, and RhythmVerse
browsing and search results accept direct finger drags. Gesture
slop uses eight window points; travel uses the list's projected row pitch, so
portrait layouts, display density and presentation scaling share the same input
path. SDL finger timestamps determine release velocity. The primary contact owns
the gesture, and its synthesized mouse stream is consumed independently of later
touches. The desktop Mouse as Touch mode uses the same scrolling model.

Dragging changes the viewport without selecting each passing row. Release adds
exponentially decaying momentum, clamped at both ends. A touch during momentum
stops the view without activating a row; an ordinary tap activates after release
and a visible feedback frame. Holding before release suppresses stale velocity.
Window focus, screen/dialog changes, resizing and physical navigation abandon the
gesture; a suspended guest does not accumulate momentum.

RhythmVerse uses the focused vertical list on `store_menu_screen` and
`store_browser_screen`, including browsing categories and submitted search
results. The current provider determines the scroll range as result counts
change. Messages uses the same drag, momentum and wheel path for its focused
`songs.lst`; its separate reading panel stays outside that gesture area.
Loading, download and error dialogs retain their own input scope;
details screens and horizontal lists do not opt into content dragging.

Portrait store layouts refresh each list after changing its visible row count:
the native setter recreates label elements without filling them from the provider.
The private header uses the portrait font and stays outside the menu's loading,
loaded and unavailable groups so state changes preserve its centered title.
Row surfaces identify the owning `UIListLabel` provider field: the guest creates
unnamed `UILabel` copies for each visible element. The library tab titles center
between the shared marquee's settled rails, outside the content panel's safe-area
transform, matching the background panel that draws those rails and arrows.

`TouchListScroll` in `src/input/touch_scroll.h` keeps fractional viewport positions.
Whole rows use the existing native selection/scroll notifications. The
`UIListDir::BuildDrawState` hook (`824E6DC8`) temporarily supplies the fractional
progress through `UIListState` +52/+60; the native builder handles the extra edge
row, provider spacing and alpha. Navigation fields are restored before returning.
Rendering and hit testing therefore use the same guest draw state.

`DrawNativeList` lends each widget its native spare element before drawing the
extra edge row and fills that element through `UIListDir::ScrollStart`. After
each render pass it returns the last element without rotating the settled rows
or emitting selection notifications. The synthetic draw otherwise indexes past
the widget's seven elements on the eighth row; the 2026-11-25 My Songs dump
records that null dereference in `UIListLabel::Draw` (`824BF178`, row index 7).
The isolated `native_list_draw` suite checks the filled edge row, repeated render
passes, element order and ownership restoration, ordinary native scrolling, and
empty/short filtered lists. Live scrolling remains a manual validation gate.

This path is shared across SDL platforms and rendering backends. Host tests cover
tap/drag separation, fractional travel, momentum, interruption, boundaries,
resolution-independent slop and synthesized mouse suppression. Windows AMD64
Release is the build validation target; iOS, macOS and Linux device behavior still
requires manual validation.

The Achievements browser uses the same motion model in both its one-column and
two-column layouts. Its whole card viewport accepts drags, including gaps between
cards, while the native ImGui scrollbar and Back button retain their own input.
SDL finger timestamps determine momentum; duplicate mouse events cannot change
the selected card while dragging or coasting. Taps select cards on release, and
touching a coasting list only stops it. Pixel scrolling is applied before the
child window begins so hit testing follows the newly drawn positions. Resizing,
backgrounding, closing, keyboard/controller navigation, mouse-wheel input and
physical mouse use cancel touch motion. Mouse as Touch also supports this path.

## Portrait Recommended page

The native `recommendation_screen` has a portrait layout as of 2026-11-25.
`PreparePortraitRecommendations` derives its panel from the installed scene;
private carousel and compact leaderboard resources retain the native
`album_mesh`, `rec_type`, `user_name`, `rank` and `mini_score` provider fields.
The carousel presents one square album image at a time, its category icon and
caption, a position counter, and a separate song/artist/genre/reason panel.
The layout scales the original square artwork uniformly and fits measured text
within independent reading areas.

Previous/Next buttons send the native D-pad left/right actions and disappear at
the ends of the list. Their hit areas stay separate from the album's Confirm
target and the header's library-navigation arrows. The single visible card
explicitly retains its horizontal axis for mouse-wheel remapping. Sizing occurs
before native provider binding, so rebuilding the list cannot erase its freshly
filled elements or replace the saved selection.

The action footer supports all five native actions. Confirm and Back occupy the
lower row; available options, preview and leaderboard actions occupy a separate
row above it. Each action has its own surface and hit area. Caption ordering,
placement and hit testing use the same live action list, preserving the original
action IDs and input-icon styles. The shared InlineHelp draw bridge separates
five labels on this screen while retaining the smaller limits on other screens.

An empty provider shows a localized empty-state caption and hides the artwork,
metadata, counter, navigation and leaderboard container. Confirm, Preview and
Leaderboard actions are cleared in that state; Back and Options retain their
native behavior. The compact leaderboard keeps its native hidden, loading,
visible, empty and unavailable states. Its container prevents stale leaderboard
content from remaining visible after the last recommendation disappears.

The native preview, store, song-launch and challenge handlers remain in the
Recommendations record. Separate challenge and full leaderboard screens still
require their own portrait acceptance; adapting this page does not validate
those destinations or restore online services.

Validation artifacts are `out/recommendations-portrait-build.log`,
`out/recommendations-portrait-record-tests.log` and
`out/recommendations-portrait/records/`. Windows AMD64 Release, eight focused
CTest suites and 1,883 installed-record checks pass. Checks cover four phone and
tablet aspects, footer separation, provider/action preservation, empty and
boundary states, artwork sizing and incompatible source refusal. In-game
appearance, tap/wheel/controller navigation, previews and connected-service
actions remain unverified. The title code is shared across Windows, Linux,
macOS and iOS; the latter three platforms were not built or device-tested.
The documentation checker still reports 18 existing stale-link/path errors;
none refer to this section or the Recommended layout.

## Touch gameplay and calibration timing

Direct gameplay contacts retain SDL's monotonic nanosecond timestamp through
`FingerEvent` and `GameplayEvent`. The `824423F8` SwingAtLane hook subtracts the
queued touch's age from the native calibrated song time, only while dispatching
that touch's note press. Multiple taps drained in one frame keep their original
spacing. Keyboard, controller and automatic chart judgements retain their native
time, hit windows and calibration offsets. Touch presses older than 250 ms are
discarded so a stall cannot turn abandoned input into a new strike.

Notes still dispatch on down. If a swipe is already recognized in the same
buffered batch, its starting press keeps its identity but does not strike a note.
A swipe recognized after dispatch can forgive its own failed press; a successful
hit already committed by the guest is retained.

Manual touch calibration samples bypass the menu's synthetic A-button pulses.
`CalibrationPanel::Poll` (`822BCE90`) publishes a sampling target only in native
state 2 after the five countdown cycles. Raw primary-contact downs queue their
timestamps; their synthetic mouse stream is consumed through release. The guest
thread feeds each tap to `822BC0C0`, the native calibration sampler, with the user
selected by `start_test`. The `822B89A8` elapsed-time hook removes that tap's queue
age for the duration of the sample. Native outlier rejection, result storage and
hardware calibration remain in the guest. Sampling interval changes, panel exit,
focus loss and backgrounding discard queued samples.

The path serves iOS and the explicit desktop touch preview, independently of the
renderer. Host tests cover timestamp spacing, preserved calibration offsets,
buffered swipe identity and calibration contact boundaries. Windows AMD64 Release
builds and host tests validate compilation and queue rules; iOS device timing and
macOS/Linux builds require separate validation. Existing calibration values may
include the previous menu-route delay and should be measured again on the device.

With `input_source_trace=true`, `touch timing:` records each dispatched note's
token, queue age, native song time, corrected event time and hit result. `touch calibration:`
records direct calibration sample ages. These traces are disabled by default.

## iOS inactivity pause

On iOS, losing scene activation freezes guest time and latches a pause request
for an entered `game_screen`. The iOS-only `game.dtb` edit records screen
enter/exit through the builtin bridge and checks the request from `game_panel`'s
authored `poll`. Consumption waits for foreground activation and native
`can_pause`; the script then calls the existing `game_screen check_pause`.
That method guards `is_paused` and opens the title's pause panel. Returning to
the app never resumes the song automatically. Exiting the screen clears any
unconsumed request. This applies to every input method on iOS, independently
of Touch Buttons, and does not change desktop focus behavior.

The lifecycle thread only updates mutex-protected host state in
`platform/ios_runtime.mm`; it never calls guest code. `ios_lifecycle` tests cover
duplicate notifications, repeated switches, menu inactivity, song retirement
and entering a song while inactive. `menu_payload` checks the authored route
and preservation of the existing callbacks. Device interruption testing remains
necessary.

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
steps through intervening choices. Those notifications are also what lets a hovered
Help & Options row describe itself: the generated panels publish the highlighted
row's caption from `SCROLL_MSG` and `selected_sym` - the same message retail's
`powerup_select_panel` uses to update its description - so pointing at a row moves
the caption without a click or a directional press (measured 2026-10-19,
`out/live-scrollmsg/scrollmsg-hover-calibration.png`, `scrollmsg-hover-av.png` and
`scrollmsg-hover-interface.png`). Powerup submenus retain native A/B semantics:
click a category to open it, click a choice to equip it, or right-click to go back.
The open wheel's category keeps its highlight even when the pointer leaves it —
opening the wheel moves focus to it, so only the wheel's own frame is ever stripped
(owner rule, 2026-10-16: the inner options follow the hover rule; the category
follows it whenever its wheel is closed).

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

The powerup selection submenu's yellow arrows accept mouse clicks and touch taps.
`UIListWidget::DrawMesh` captures each visible `arrow_lt.mesh` / `arrow_rt.mesh`
under the focused list's draw, before its temporary slot transform is restored.
Only the preceding frame's geometry for the current focus can receive a press;
hidden arrows have no target. A press pages the native viewport by `numDisplay`,
clamped to `NumData - numDisplay`, through the same native selection/scroll
notifications used by the library rail. It preserves the equipped item and does
not emit Confirm. Mouse and touch use the same queue and presentation mapping.

Portrait powerup selection authors three large icons per page in private native
list resources. All three category providers retain their native equip and
description notifications. The song card keeps the album, song/artist labels and
all five instrument difficulty meters in a compact 3–2 grid. Back and Play Song use the existing native
footer buttons; Play Song sends Start to the same preview shutdown and purchase
statements as the category menu's launch entry. Its fourth icon is suppressed in
portrait, including its mouse bounds. Each visible choice has a separate Details
button that selects the row without confirming it and sends Option to the guest
panel. A dedicated guest view displays the localized name and wrapped description;
Close restores the selection without equipping. While details are open, the panel
blocks equip, paging and song launch. These changes apply to portrait interfaces on all host
platforms; landscape retains its original records and layout.

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
scrollbar rule instead (owner rule, 2026-10-16): the wheel and the rail move the
*view*, like a desktop scrollbar, and the hover then follows the content — once the
burst has drained and the drawn frame shows the new view, the row that came to rest
under the pointer is caught exactly as a pointer move would catch it, and a catch
that finds no row (an artist heading, empty space, the rail) leaves the hover empty.
The catch (`MouseScrollFollow` in `native_menu.h`) is armed by every applied scroll
with that scroll's own pointer (the wheel event carries its position) and retargets
to any later pointer event; the scroll has already ended the hover the mouse owned
(the lift below, so the highlight never rides the old row away), and the catch waits
for the burst because a detent applied after a catch would re-anchor the guest's
selection on the row that catch just left. With no row under the pointer the guest's
selection stays where the scroll left it, even out of the band; the guest reports
that as selected slot -1, which the mapping tolerates because every scroll
application passes the last row a draw positively identified (`last_selected_row`,
reset on screen or focus change) rather than the live slot. In Your Songs, the wheel
moves `ceil(NumData / 100)` rows per detent, clamped to one through `numDisplay`:
a small library retains single-row precision, while a large library advances up
to one visible page per notch. Other railed lists retain one row per notch. Successive
detents advance the *pending target* (list +228) so a burst is not lost to the
animation.

Header labels are captured by `UILabel::DrawShowing` (`824BC668`, label +260),
using the same text rectangle and virtual transform base as list label capture.
Only a label drawn in the preceding frame for the same screen and focus can
receive a click. The active panel's generated `poll` consumes a one-shot
`rbbz-mouse/header/<panel>/<destination>` query through the existing `file_exists`
bridge, then runs a copy of its trigger departure work with the clicked destination.
The request expires in that frame and the bridge checks focus, mouse support and
queue availability before delivering it. Header navigation uses no host overlay.

The rail uses the focused list's scene geometry: `ObjectDir::FindObject`
(`82402DE8`) resolves its sibling `rail.mesh` and `scroller.mesh`, and the mesh
bounds retained by `OnSync` are projected through the current camera. The hit area
and thumb travel therefore follow each scene's actual placement and size, including
the song library and RhythmVerse results. Lists without those meshes keep directional
wheel input. `maxFirst` comes from `UIList::NumData()` (`824C0C08`) minus the
visible row count. This uses the current provider's virtual dispatch and is refreshed
as results change, rather than accepting only the library provider's implementation.
A track press immediately centers the thumb at the cursor; a thumb press preserves
the grab offset. Both can continue into a drag, clamped to `[0, maxFirst]`.
The window captures the mouse until release so the pointer can leave the rail or
game rectangle. The release position is applied too; focus loss, screen changes,
and overlays cancel the drag and release capture. Applications go through
UIList::SetSelected plus the same scroll-start and scroll-complete notifications
hover uses (`824C1938`, `824C1DE8`, `824C1F08`). A press on the rail is a scroll,
not a click: it never emits A. The strip is published per draw and invalidated on
a screen or focus change, so a drag can never be applied to a retired list; drag
frames coalesce to the latest position and bypass the pad pulse gates because
they do not emit pad input.

Leaving a drawn row ends the mouse's own hover (the trace logs `unhover … highlight=off`
or `highlight=kept`), and a left click only emits A when the pointer is on a row
that a draw of the currently focused list identified in the same frame; on empty
space, on a stale hover or on the rail strip the press is swallowed
(`MouseMenuScrollZone` in `mouse_ui.cpp`). The unhover is visible too (owner rule,
2026-10-16): while the list still shows the row the pointer left, that frame is
drawn without the highlight — the per-draw element record the engine marked
highlighted drops to active (`+24`: `kUIListWidgetHighlight` 1 → 0), the
draw-state's highlight display slot reads -1 so highlight-only slots stop
drawing, and the shared highlight mesh (`UIListHighlight::Draw`, recognised in
the `824E0900` hook) is skipped for the frame. All three are read from the
draw-state `824E5020` receives, which the caller frees right after, so nothing
survives the frame and no guest list state is ever written to hide a highlight.
The lift ends by itself the moment `UIListState::Selected` names a *different*
row — a controller press, a screen change — or when the scroll's own catch
re-hovers one (`Hover` clears it); a scroll that pushes the lifted row out of the
band reports no selected row at all (slot -1), which is the same selection, so the
lift holds until something really replaces it. A pad's own selection is never
drawn unhighlighted. A pointer move that lands outside the
game rectangle (pillarbox, overscan cutoff) queues a row-missing move so leaving
the frame unhovers too. **Superseded:** "writing the selected slot to -1" (this
section's earlier form; tried and discarded because the engine recomputes the
slot every update) — the per-frame lift replaces it. Screens with no captured
list — the title screen's *PRESS (A) TO START* — keep the at-principle A, so a
mouse-only boot still works.

Touch list selection is feedback for a press, rather than a persistent cursor.
Idle rows, including the first row on entry and underlying unfocused lists,
draw with their normal colors. Pressing a row lights it until release; even a
tap completed between guest frames receives one highlighted draw before its
native Confirm pulse. Leaving the row, losing input focus, or changing pages
cancels that press. The release position is checked against the current row
geometry. Highlight suppression changes only per-draw records, preserving the
guest selection and full-width touch targets. Keyboard and controller activity
restores the normal persistent selection immediately. This policy is shared
across host platforms; native iOS visual and touch acceptance remain unverified.

The synthetic host device establishes controller connectivity while either host gate is
open (P2 made it follow Keyboard Support as well as Mouse Support). Fixed mouse actions
are merged after user controller remapping, for player one only. `[remap]` mouse
sources are gameplay-only raw device state (P2, below): the native pointer owns the
mouse in every menu, and a live song samples the button mask itself. Keyboard and
controller bindings continue through their existing path.
ImGui pointer capture and window focus loss clear the queue. Cursor visibility is
applied on the window thread through SDL.

## Source gates and physical provenance (P2, 2026-10-18)

P2 of [the gameplay-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md)
turns the two device switches into real gates and gives every mapping evaluator one physical
snapshot to read. The code is `src/input/source_gates.{h,cpp}` (the rules, SDK-free),
`src/input/title_input.{h,cpp}` (the title's input system), and the state filter in
`src/input/remap.cpp`.

**The two device gates are read at poll time.** `keyboard_support` off blocks every keyboard path:
General Mapping's `key:` sources, both gameplay tables' `key:` sources, and the SDK's legacy
keyboard emulation (below). `mouse_ui_nav` off blocks every mouse path: the native pointer, both
gameplay tables' `mouse:` sources, and General Mapping's `mouse:` sources. All of these are in-game
rows now (Controls → Input Types); the external settings application that once edited them retired
on 2026-10-19. The two
gameplay switches (`gameplay_mapping_controller`, `gameplay_mapping_keyboard_mouse`) that used to
be *routing* selectors were **retired on 2026-10-20** by P2 of
[the restructure plan](../plans/finished/2026-10-19-interface-gameplay-mapping-restructure-plan.md)
(D3): the Mouse & Keyboard table is always active in a song, the Controller table is active
exactly when it has a custom row, and "custom" is inferred from the table's own rows (see
§ Gameplay routing is always on, below).

**One physical snapshot per poll.** The state filter runs on the guest input thread and builds:
a pad from the SDK's *physical* drivers only (`SamplePhysicalPad`), the keyboard from the
window's own key **events** (`HostKeyDown`, a latch `mouse_ui.cpp` keeps as a
`WindowInputListener`), and the mouse button mask and window focus from the window thread's own
last report (`HostMouseButtons`/`HostInputFocused`; SDL documents `SDL_GetMouseState` and
`SDL_GetKeyboardFocus` as main-thread calls, so the sampler never asks them). An unfocused window
plays nothing.

The keyboard source is measured, not assumed: P2's first live arm ran with a heartbeat that
printed the sampler's raw key probe, and it read `sdl_focus=off keys=00` while the harness held
every bound key (2026-10-18, `out/gameplay-mapping/p0/p2-sources-e/`) — SDL's global keyboard
state array is blind in this process, while the key *events* keep delivering (the MnK emulation
navigates the menus with them). The sampler therefore reads the event latch, which is also the
shape plan D5 asks for (raw device events, not a polled aggregate): a press and release between
two guest polls still produces its edges. SDL's state array remains a fallback for a key already
held when the listener attached.

- A `pad:` gameplay source answers from the physical pad's state and nothing else. The SDK's
  legacy keyboard/mouse emulation (`mnk_mode`) synthesizes pad bits. With a present profile,
  General Mapping now reads the physical pad and the raw key events: merging the SDK's default
  `Space` → A with Interface Mapping's `Space` → Start made one press confirm and search. A
  profile-free boot retains the merged SDK state, because it has no General Mapping defaults to
  apply. A `pad:` gameplay binding cannot become active because a key raised the same bit.
- A `key:`/`mouse:` gameplay source answers from raw device state, never from the remapped or the
  synthesized pad. A gameplay mouse press therefore never enters the native-menu queue: the queue
  only accepts events while a native menu is available, and a song is not one.
- The keyboard/mouse emulation driver is installed only while `keyboard_support` is on. With the
  gate off it is absent, so no `mnk_mode`/`keybind_*` setting can synthesize a pad bit; the boot
  log says so (`title_input:` line).
- The synthetic host device enumerates while *either* host gate is open, so a keyboard-only or
  mouse-only configuration stays connected with no pad attached. It is synthetic, so the SDK parks
  it past every physical ordinal, it contributes no pad state of its own, and rumble ignores it.

**Interface Mapping's `key:`/`mouse:` context rule (P2/P3).** Menus are the interface's: its
`key:` sources drive them, and its `mouse:` sources do not (the native pointer owns menu mouse).
In a song the direct tables own keys and mouse unconditionally, so Interface Mapping's emulation
stops at the menu. The direct tables themselves act only while playing and only while they are
routed: the Mouse & Keyboard table whenever a song is current, the Controller table only when it
has a custom row (P2's always-on routing, § Gameplay routing is always on). P3's dispatch (§
Direct gameplay dispatch below) is what makes the routing real.

**Boundaries.** Every held action is dropped without an edge - not released into the guest late -
when the window loses focus, the guest screen or focus changes, input enters or leaves gameplay,
either device gate or either routing switch changes, or a pad arrives or leaves. The native
menu's screen/focus change and focus-loss paths call `InvalidateGameplayInput()`; the poll that
sees the change invalidates under its own lock. The `input_source_trace` cvar logs each sampled
source transition with its table, action, binding and whether the table was active, which is how
P2's live arm observes the sampler without a debugger.

**Checks.** `rb_blitz_input_gates_tests` (ctest `input_gates`) covers the gate matrix (keyboard
off, mouse off, both, unfocused), pad provenance (a physical state with nothing pressed answers
nothing, the pinned stick threshold), each table refusing the other input kind even when a source
is injected past the parser, one source on two actions firing both, multi-source OR and
release-one-keeps-held, the action-edge model (one press, one release, quiet polls, a boundary
that is not a release and re-arms only on the key's release), the routing table for both switches
in both contexts, the General Mapping context matrix, player-one ownership, host-device
connectivity, the cursor rule, final packet identity after a remapped key, and the no-pad startup
where every controller source is inert and the keyboard still works.

### Live acceptance, 2026-10-18 — the sampler

Release build, `--input_source_trace` on, `scripts/diagnostics/probe_gameplay.ps1` driving a song through the
song list. Evidence under `out/gameplay-mapping/p0/`.

- **The keyboard source had to be changed, and the change is measured.**
  `p2-sources-e` printed the sampler's own heartbeat over a whole song:
  `gameplay=on focused=on sdl_focus=off ... keys=00` while the harness held every bound key —
  SDL's global keyboard array is blind in this process (it also reports no keyboard focus), while
  the window's key events keep delivering. The event latch then produced, in gameplay:
  `second_note ← key:J`, `move_lane_right ← key:K`, `move_lane_left ← key:D`,
  `powerup ← key:Space`, each with a down and an up edge (`p2-sources-f`).
- **The mouse source is event-latched for the same reason.** `p2-final-mouse` logged
  `native_mouse: button=1 down mask=01` and, 4 ms later,
  `powerup ← mouse:left`; the right click produced no binding (only the left was bound in that
  arm's scratch profile). The per-frame `SDL_GetMouseState` re-read was removed because it erased
  the event-latched press between frames — that is what made the first mouse arm (`p2-mouse`)
  silent.
- **The sequencing guard is visible on every line**: those arms read
  `active=off (sequencing guard)`, because they ran before P3 built the dispatch. The guard is now
true; the same lines read `active=on` and are followed by `gameplay input:` dispatch lines (§
Direct gameplay dispatch below).
- **Keyboard Support off is total.** `p2-final-keyboard-off-b` logged
  `title_input: legacy MnK emulation is not installed - keyboard_support is off, so no
  mnk_mode/keybind_* setting can synthesize pad input`, produced **0** sampled source edges and
  **0** native dispatches while the harness pressed its keys, and the title never advanced: the
  same keys that drive the whole route with the gate on reach nothing with it off.
- Every arm ran with no physical pad (`pad_connected=off`), so the controller table's side is
  unit-tested rather than live-measured on this machine.


## Gameplay routing is always on (P2 of the restructure plan, 2026-10-20)

The two routing switches are gone (`gameplay_mapping_controller`, `gameplay_mapping_keyboard_mouse`;
see § Source gates) and `GameplayRouting` is derived from the tables themselves every poll: the
**Mouse & Keyboard** table is active whenever a song is current, and the **Controller** table is
active only when it has a custom row (`Table::Custom()`, i.e. `BoundCount() > 0`). With no custom
row the native preset drives the pad untouched (D4); with one, the direct table bypasses the
preset and every action without a custom row uses the sources of the preset the title's own
Controller Presets page selected (D4). That preset is observed from the song controller's `+80`
slot in the `NewController` hook and published into the snapshot
(`input::ObserveControllerPreset`), so the first dispatch of the song already follows it. Custom
is never read from a profile switch: an old `gameplay_mapping_*` key is preserved and inert (D15).

Consequences worth knowing:

- General Mapping's `key:`/`mouse:` emulation now stops at every menu boundary: in a song the
  direct tables own keys and mouse unconditionally (`GeneralSourceAllowed`).
- **A press a gameplay table is holding is not echoed into Interface Mapping**
  (`CollectGameplayConsumed`). Measured 2026-10-20: Escape pauses from the Mouse & Keyboard
  table, the pause menu it opens *is* the menu context, and the same still-held Escape then
  became Interface Mapping's Back and resumed the game it had just paused. The collector keeps a
  per-input-thread latch: a source is added while a table holds it in a song and is dropped only
  when the physical source is released. The latch has to outlive the context change - P3's
  in-song arm measured the pause panel appearing 22 ms after the Escape press, with the release
  after it, and the earlier per-poll set let exactly that window through. A menu key no table
  ever took is never added, so the title's A press is untouched.
- The Mouse & Keyboard defaults use First Note `Q` + left click, Second Note `W` + right
  click, Move Right `Arrow Right` + scroll down, Move Left `Arrow Left` + scroll up,
  Power-up `Space` + middle click, Pause `Escape`. The wheel directions were reversed at
  the owner's request on 2026-10-31. A wheel detent
  (`mouse:wheel_up`/`mouse:wheel_down`) is an event, not a level: `HostTakeMouseWheel` consumes
  the detents once per poll, the source reads as down for that poll, and the action hold turns it
  into one press and one release.
- A binding row's first Assign **replaces** its default list (later Assigns append); the row
  caption says `Default binding from Preset N. Click to Replace.` or `Custom binding. Click to
  add an input.`

## Direct gameplay dispatch (P3, 2026-10-18)

P3 of [the gameplay-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md)
turns a gameplay table's held action into the title's own gameplay dispatch. The code is
[src/hooks/gameplay_input.{h,cpp}](../../src/hooks/gameplay_input.cpp) (the guest adapter) and the
poll/overlay half in [src/input/remap.cpp](../../src/input/remap.cpp); the SDK-free rules
(`ActionEdges`, `ControllerOwnedButtons`, `GameplayOverlay`) live in
[src/input/source_gates.h](../../src/input/source_gates.h).

**The seam is the title's validated action boundary, not a reimplementation.** A press is
`sub_824364B0(controller, kBMAction, 0)` and a release is `sub_824367E0(controller, kBMAction)` -
the same calls the native preset's producers make (plan §10.2), so note judgement, calibration,
lane animation and the power-up rules stay the game's own. The controller object is adopted from
`sub_82435EB0` and forgotten at `sub_82443C20`; the dispatch itself runs on the live controller's
own per-frame update (`sub_82443A30`, ~30 ms), which is a guest thread. The input filter never
calls a guest function.

**The native preset is bypassed at that boundary.** While the Controller table routes gameplay
input, both boundary hooks drop every native press and release. All four producers P0 measured
(ButtonDown/Up, the keyboard producer, the explicit `send_down_action`/`send_up_action` messages and
the controller's own held-action repeat) arrive there, so one decision covers them all. The
suppressor calls the *unhooked* function for its own dispatches, so it can never eat them. Pad
state the menus, pause screen, calibration and the system still need is untouched.

**The pad overlay carries the two things the boundary cannot.** `Start` is not a `kBMAction` (P0
measured it resolving to `None` while still pausing), so a direct Pause raises the Start bit in the
delivered pad state for exactly as long as the action is held - which enters the game's own pause
path, and re-enters it only after the source releases (a boundary invalidates the hold and requires
release-before-re-arming). Everything the Controller table binds is *removed* from that state while
it routes, because the preset it would feed is bypassed; with the shipped defaults that mask is the
D-pad directions, A, X, both shoulders and Start (`0x531F`).

**While either gameplay switch routes, the guest sees the physical pad only.** The SDK's legacy
MnK emulation is merged before the filter, but a direct table must not be fed by it (R4). With a
present profile, General Mapping also starts from the physical pad and maps raw keys itself; a
profile-free boot keeps the SDK's merged state for legacy menu input. The filter hands the rewrite
the physical `PadState` - buttons, triggers **and the four stick axes**. The axes matter: P3's first
live arm found that a held `D` still produced a native `lane0` press because the title derives its
own stick-direction buttons from the axes, and the filter had only been rewriting the buttons.
Evidence and the fix are in the plan §13.3.

**Edges, not levels, between the poll and the guest.** The poll publishes one union held state per
action; `ActionEdges` remembers at most one press and one release per action and the seam drains
them in order, then reconciles against the latest published level. A tap that begins and ends
between two guest frames arrives as press-then-release; a collapsed burst cannot leave an action
stuck. The two tables are unioned per action from their own holds, so "release before re-arming"
survives the union. A boundary (focus loss, screen change, entering or leaving a song, a flipped
switch, a pad arriving or leaving, song end) drops the queue and makes the seam release everything
the guest believes is down - which is what stops a held track action's repeat.

**Boot and trace lines.** `gameplay mapping: direct dispatch on (plan P3) - … owns the pad
control(s) 0x531f in the delivered state`, and `gameplay input: song controller 0x… (vtable
0x820874c4)` when a song adopts one. With `input_source_trace` on, every direct press/release is
`gameplay input: press|release action=<name> (<kBMAction>) controller=0x…`, and every suppressed
native producer is `gameplay input: suppressed native press|release action=… …`.

### Live acceptance, 2026-10-18 — direct play

Release build, `scripts/diagnostics/probe_gameplay.ps1`, default tables (Keyboard + Mouse on, Controller off),
`--mnk_mode=1`, evidence under `out/gameplay-mapping/p0/`.

- **Keyboard play works through the shipped table.** `p3-km-song` logged a press and a release for
  `powerup` (Space), `second_note` (J), `move_lane_right` (K) and `move_lane_left` (D) from 2 s
  holds, with the song running (`03-gameplay.png`). `p3-km-axes` repeated it after the axis fix
  with **0** native dispatches in the whole song.
- **A held track action repeats natively and stops on release.** `p3-km-hold` held K for 13 s: the
  direct press at 14:46:40.606, the controller's own repeat began 5.0 s later and ran at ~108 ms
  cadence, and the 14:46:53.597 release was the last `action=4` line; D repeated the same way and
  stopped at 14:47:09.584. 152 native repeat dispatches, none after a release.
- **A direct Pause enters the game's own pause screen.** `p3-km-pause`: `action=pause down
  active=on source=key:Return` at 14:49:36.313, `panel=pause_panel` 40 ms later
  (`05-pause.png`, GAME PAUSED), the second Return handled by that screen and resuming at
  14:49:40.418, with no immediate re-pause.
- **The Controller table's bypass suppresses every producer, with no physical pad.**
  `REX_GAMEPLAY_MAPPING_CONTROLLER=true` on `p3-controller-on-b`: the probe's message and keyboard
  fixtures reached the boundary and were suppressed (`from=message`, `from=keyboard_down`), and the
  Keyboard + Mouse table still dispatched five presses beside them.
- **Not measured here:** a physical pad (none on this machine), the analog stick under a real
  thumb, an XInput boot, and the tutorial/calibration interactive segments.

## In-game edits and the capture dialog (P7, 2026-10-18)

P7 of [the gameplay-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md)
moves the settings surface into the game: the Controls menu edits the profile through the settings
bridge and the change reaches the next poll. The code is
[src/input/input_store.cpp](../../src/input/input_store.cpp) (the store),
[src/input/effective_input.cpp](../../src/input/effective_input.cpp) (the snapshot),
[src/input/input_capture.cpp](../../src/input/input_capture.cpp) (the dialog) and the filter in
[src/input/remap.cpp](../../src/input/remap.cpp). Plan §16 holds the evidence.

**One snapshot per poll replaces the launch-time read.** The filter takes
`CurrentInput()` — a `shared_ptr<const EffectiveInput>` holding both device gates, both routing
switches and whether a profile was found — and builds every evaluator from it: `Apply` gets
`config->general`, `ObserveGameplay` gets the tables, and the source gates are built from the same
snapshot. A committed edit publishes a new snapshot and calls `InvalidateGameplayInput()`, so the
next poll drops held actions and re-arms on release. The keyboard gate is total in every context:
with Keyboard Support off the SDK's MnK emulation is not even installed, so the guest cannot see a
synthesized pad bit in a menu either (`title_input:` says why). Keyboard and mouse off changes
nothing about the *menu mouse* — `SampleMenuMousePadState` is added only when no capture is armed
and is a host pointer, not a bound source.

**A capture owns raw input.** While `capture::Armed()` the guest's pad is zeroed after General
Mapping and before the gameplay overlay, the overlay is skipped entirely, and the native menu's
mouse pulses are not added; the capture itself reads the ungated devices (so a binding may be
prepared for a disabled device) and the dialog's Back is a host pad pulse
(`PulseHostPad(0x2000)`: a bit for two polls) rather than a synthetic key, so it cannot become a
binding and is not gated by Mouse Support.

**The listening window is a 3-second countdown (P5, 2026-10-20).** After the press that opened
the dialog is released (the neutral frame), the dialog counts **3, 2, 1** once per second and then
leaves with the row untouched. A valid press is captured **on its own frame** - the binding
decision does not wait for a release, so a tap and a hold both bind - and the dialog leaves through
the host Back pulse. A **wrong-kind** press (a key into the Controller table) prints which kind the
row takes and the count keeps running beside it: it is feedback, never a pause or a reset, so the
page still closes itself on time. There is no hold-to-cancel gesture and no separate 20-second
timeout (the countdown replaced both); the page has no pressable Cancel row either, because while
armed a press could not reach one and a press that did would be the very input being bound. The
remaining seconds are drawn by `capture::StatusText()` in the caption. An unfocused window captures
nothing and re-waits for neutral; a completed capture is saved through the store, and a save
failure is posted as the overlay's text and leaves the effective mapping as it was.

**What this pass did not drive:** a pad capture and the Gameplay Mapping → Controller toggle
inside a song (plan §16.5). The **in-song variant of the dialog and the Controls menu** was
closed by P8a (§ below): a paused song drives pause → Help & Options → Controls → Input Types and
a bind page → the dialog's hold-to-cancel → back to the pause panel → resume, with no guest fault
(`out/gameplay-mapping/in-game/p8a-in-song3/`, plan §18.4). The hold gesture recorded in that arm
was replaced by P5's countdown; the in-song arm was re-run on the countdown (P5 record in the
restructure plan).

## The recovery path, and why it has two observation points (P8a, 2026-10-18)

One input state has no in-game way out: a saved profile with **both host gates closed and no pad
connected**. Every ordinary source is inert there, so the Controls rows cannot be reached. P8a's
recovery path is the only exception to R1 ([src/input/source_gates.h](../../src/input/source_gates.h),
`RecoveryNeeded`/`RecoveryHold`/`RecoverKeyboard`, and
[src/input/remap.cpp](../../src/input/remap.cpp), `TickInputRecovery`):

* **Needed** means both gates off *and* no physical pad. A pad is a route even though it is not a
  host gate, so plugging one in disables recovery.
* **F10** (`kRecoveryKey`, the profile's spelling of the name) held for **2 s** fires once. The
  hold starts on a rising edge, so a key already down when the profile is read cannot trigger it,
  and any release or a poll where recovery is no longer needed starts over.
* The action is one edit through the same store the Controls rows use:
  `input.keyboard_support = true`, committed once. Nothing else is written, and a switch locked by
  a command-line/environment value is refused and logged rather than reported as success.
* The caption says so **even with Settings Labels off** - a navigation problem is not optional
  help. While recovery is needed it reads "No input is available … Hold F10 for 2 seconds …";
  after it acts, a short confirmation points at Input Types.

**Why the state filter alone is not enough (measured from the SDK source).**
`InputSystem::GetState` returns `X_ERROR_DEVICE_NOT_CONNECTED` *before* calling the state filter
when no device contributes (`rexglue-sdk/src/input/input_system.cpp`, `GetState`), and the title's
synthetic host device enumerates only while a host gate is open. A profile with both gates closed
and no pad therefore never runs the filter. `TickInputRecovery` is called from the filter *and*
from the settings overlay's per-frame `OnDraw`, which always runs; both share one edge-guarded
watch, so the second caller cannot double-fire.

**Live acceptance, 2026-10-18** (`scripts/acceptance/acceptance_recovery.ps1 -Label p8a-recovery2`): 11
steps, 0 failures, `--input_backend=none`, Settings Labels off. The boot logs the need
(`input recovery: no usable input … hold F10 for 2000 ms`), the caption OCRs with the sentence on
screen, one 2.6 s hold logs `F10 held for 2000 ms … keyboard_support is on again`, the profile
loses `keyboard_support = false` and keeps `mouse_ui_nav = false`, and a menu key then reaches
the guest (`content_pipeline: usable menu`) through General Mapping's own key defaults - the
legacy MnK emulation is not installed on that boot, so this is the reopened gate's own path.
Evidence: `out/gameplay-mapping/in-game/p8a-recovery2/`; plan §18.3.

## Checks

`rb_blitz_native_mouse_tests` covers pointer mapping, row hit tests, disabled rows,
move/click ordering, wheel bursts (each detent carrying its pointer), fractional
detents, scrollbar drag coalescing (press/release never dropped, drags
latest-wins, traffic flowing while a click pulse waits for its release), the queue
drain gate the scroll-aware catch waits on, the catch model (armed by an applied
scroll, retargeted by any later pointer event, taken once, discarded on a
boundary), the lift holding while a scroll reports no selected row, the rail thumb
math (travel per step, first-from-y with both clamps, the on-thumb grab test),
press/release delivery gating behind a delayed input thread, per-player final
packet identity and boundary resets. `scripts/diagnostics/drive_ui.ps1` supports normalized `mouse:x:y`, `click:left/right`,
`drag:x1:y1:x2:y2`, `wheel:detents` and `cursor:visible/hidden` actions for live
acceptance checks.


### Live acceptance, 2026-10-13

Windows Release build, windowed 1280x720, Ultimate enabled, isolated
`out/mouse-native/user2` profile and the local `game/dlc` library. Verified with
real pointer moves, button clicks and wheel events through `scripts/diagnostics/drive_ui.ps1`:

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
suites: `native_mouse`, `profile_remap`, `profile`, `input_gates`,
`settings_service`, `settings_store` and `settings_migration`. (The two profile suites were
`controller_settings_remap`/`controller_settings_profile` until the application retired on 2026-10-19;
the launch/nav suites went with it.)

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
through `scripts/diagnostics/drive_ui.ps1`, one left-click (or right-click/wheel) per step,
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

The v0.11.0 migration preserves the title's requested `BUILD_TESTING` value
around dependency configuration. Current Vulkan release builds can register the
title tests; the restriction above records the earlier SDK configuration.


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

### Live acceptance, 2026-10-16 — the hover follows a scroll

The owner reported the remaining glitch from the record above: with the pointer
resting on a song, a wheel scroll moved the rows out from under it but the
highlight stayed on the row it was originally hovered on — a slight scroll left
the highlight one row above the pointer, and a large one could leave it drawn
detached from any row. The scroll-aware catch above is the fix; this run replays
the report. Same kind of environment (Release build, windowed, `drive_ui.ps1`,
isolated profile under `out/_swap/scroll-follow/`), with a 60-package fixture
library (`--dlc_root` pointing at the fixture; the rail map logs 51 rows, 10
visible, `maxFirst` 41, one row pitch ≈ 0.0624 game units).

Driven on `song_select_screen` with the pointer parked at (0.35, 0.25) — the
first visible row's slot (trace `rb_blitz_012.log`; captures `10..18*.png` in
`out/_swap/scroll-follow/shots/`; the bar's pixel rows measured by `rowseek.py`
against its sampled gold `(254, 217, 20)`):

- Hover the first row → bar rows 257..302. `wheel:-2` → the trace logs
  `unhover … highlight=off (scroll)`, both detents apply (`first` 0→1→2, every
  application keeping the same anchor row), then one `scroll follow` →
  `hit row=2` → `selected row=2`; the bar's rows are still 257..302 — it stays at
  the pointer while the content scrolls, and the row under it is the one the
  wheel brought there (`11-wheel2.png`: the bar lands on *Kids in the Street*).
- `wheel:-6` → six applications in one burst, one catch (`row=8` → *Always*),
  the bar still 257..302. No per-detent re-catch: every application logged the
  same anchor row, so no frame flipped the selection back to the row a catch had
  just left.
- `wheel:1` → the bar returns on the song that scrolled back under the pointer
  (shot 14). `wheel:-1` lands the pointer on the *COLLECTIVE SOUL* heading: the
  catch reports `row=-1` and the bar is gone; scrolling further with the pointer
  on the heading (`wheel:-5`) leaves it gone (shots 13 and 15 carry no highlight
  pixels at all), which is the "stays and clips" report retired.
- A rail drag (`0.61,0.30 → 0.61,0.327`) moves the view two rows with the
  pointer on the rail: each drag frame catches `row=-1`, the bar stays gone
  (shot 16), and hovering a row afterwards brings it back on that row
  (shot 18, bar rows 319..364 — one slot down, exactly under the pointer).

The non-scrolled surfaces are untouched by construction: the catch is only armed
by an applied rail scroll, so the main menu's and the powerup lists' wheel still
steps the selection through D-pad detents exactly as before.
`rb_blitz_native_mouse_tests` passes 460 checks (the scroll-aware catch and the
wheel's pointer, on top of the record above); all 38 CTest targets pass in
`out/build/win-amd64-debug`.


## Menu-music navigator input (P2, 2026-10-26)

Player one's filter samples independent `[menu_music]` bindings from the immutable
`EffectiveInput` snapshot before General Mapping. Defaults: RB/MediaTrackNext,
LB/MediaTrackPrevious and Start/MediaPlayPause. Supported sources are consumed until
release; held inputs cannot become presses on menu entry, device enabling or binding
publication. Raw physical pad sources cannot be triggered by keyboard pad emulation.
Keys obey Keyboard Support and window focus. The splash focused option list must be drawn;
the pre-press title screen is excluded, and modal/capture owners win.

Only supported actions intercept input. Native pause/resume is owner-verified and
enabled without a trial flag: Start/MediaPlayPause toggle the owned stream between
states 3 and 5. The Play hook prevents the guest poll from undoing pause, and the
watchdog is deferred while paused. The internal `rbbz-play/next|previous|pause` command channel retains its
off gate. Mapping pages configure bindings only, without an execution button. Only the guest music poll executes requests,
revalidating producer scope before handoff. Assignment uses the existing guest capture
page, three-second cancellation countdown, profile writer and live snapshot publication.

SDK patch `0041-media-keys.patch` maps the three SDL media scancodes to existing VirtualKeys;
the host latch translates them to SDL key names for capture and `HostKeyDown`.
`drive_ui.ps1` adds media key names and E0 scan codes; the diagnostic navigator probe posts
once after a configurable delay. Host tests drive no game UI. Windows media-key arbitration
and physical-pad delivery remain unmeasured individually. P3’s host player mouse routing is
described below; the owner later accepted the full navigator. See the
[P2 record](../plans/finished/2026-10-22-background-music-navigator-plan.md) §§14 and 17.


The 2026-10-26 Previous follow-up retains rapid navigation edges while the decoder loads:
production posting accepts up to four waiting requests behind the active handoff. A Previous
made during another navigation/loading captures step-back intent at posting, rather than using
the eventual execution time to choose restart. Completion promotes the next request; failed or
cancelled execution, focus loss, modal/capture/loading ownership, teardown and session reset
clear the burst. Full queues still report busy. Host tests reproduce double/triple Previous
through a delayed restart and back to the boot song; the owner later accepted the full navigator,
without a separate burst log.
See the [follow-up](../plans/finished/2026-10-22-background-music-navigator-plan.md) §15.


### P3 player pointer ownership (2026-10-26)

`menu_music_player.cpp` draws and routes in the same 96-DPI logical viewport; physical window
coordinates are converted before hit testing. The player uses custom draw-list controls without
pad/keyboard focus or ImGui button activation. `MouseCursor` routes events before writing raw
button/wheel latches or queuing guest hover/Select/Back. The SDK ImGui listener observes mouse
events first without handling them; the native listener at priority 1 then consumes player hits
before the keyboard/mouse emulation at priority 0. This does not depend on the previous frame’s
`WantCaptureMouse` value. Hovering the panel clears native mouse queue ownership while pad and
media input remain available. Cursor visibility treats the player’s pointer ownership separately
from native mouse focus: it remains visible over the player even though the guest queue is
disabled and no ImGui window requests capture. Mouse Support off still hides it.
Outside-player events retain the native route.

Only an enabled left press and release on the same control posts a command; hold does not repeat,
and dragging out cancels activation. Right-click and wheel over the panel are swallowed. A
session/song/layout change, focus loss, modal/capture owner or hidden player cancels the armed
control while retaining an already-consumed release. Mouse Support off leaves the player visible
with disabled pointer controls. The guest poll independently rechecks scope and capabilities.
Host tests cover these policy boundaries; the owner will validate live event delivery and DPI.

### Controller icon input family (2026-10-28 host checkpoint)

`last_device::Tracker` starts in keyboard/mouse mode. `MouseCursor` supplies
real key-down, mouse button, wheel and cumulative motion events from the window
listener; `SamplePhysicalInput` supplies `SamplePhysicalPad` before the SDK
merges synthetic keyboard-to-pad state. Only a new pad activity edge changes to
controller mode. The tracker ignores motion below 12 cumulative pixels and
defers an opposite-family event inside 100 ms until that window expires; a
new active-family event cancels the pending change. Focus loss preserves
the family and clears transient pointer/edge state. The chosen controller
scheme is stored independently; selecting it does not claim a controller was
last used. `icons::CaptureActive` captures the effective binding revision and
the tracker family/layout revision for guest hint consumers. Mapping rows use
their page's source scope instead of the last-device family. P9/P10 still need
to consume this view in the guest's own UI. The owner-run D3D12 log
`out/button-schemes/p4/d3d12-20261028-152254.log` records eight alternating
family changes. A second D3D12 run
(`out/button-schemes/p4/d3d12-20261028-153751.log`) selects One Pad and
records three controller-to-keyboard round trips while the selected scheme
stays One Pad; the owner confirmed its magenta pad art remained visible.
Footer hints still use the guest's retail A/B glyphs until P10 consumes the
resolver view.

### Mapping row images (2026-10-28 host checkpoint)

`input_icon_rows::Build` uses the P8 view to emit every effective source in
saved order for Interface Mapping and the two device-specific Gameplay Mapping
lists. It keeps each action's native list position and selection route, and
replaces only the displayed binding text with a label and inline glyph codes.
`menu_payload` copies the installed Pentatonic scene as `input_icons.milo` into the generated archive
with 30 previously empty glyph records pointed at unused atlas cells. The
texture override populates those cells from built-in key/mouse images, selected
controller package slots, and omitted slots cropped from the installed button
sheet. Host tests cover ordering, rebinding, unbound rows, overflow fallback,
texture composition and untouched BC3 blocks. The owner screenshots confirm Interface Mapping
draws the images and adding a binding refreshes them. They revealed bottom
clipping on controller circles, small and offset icons, and unreadable black fills
when selected. The alpha adjustment and list palette workaround were rejected
and removed. The owner confirmed the resulting icon size and nearly correct
placement, but hover still blackened icons: `UILabel::DrawShowing` at `824BC668`
applies the selected color as a material multiplier, after vertex color markup.
The current route wraps only icons in `<alt1>` and adds a separate font resource
with its own material and the existing white `active_focused.color`. Primary
style, label geometry and every palette record remain byte-identical. The
owner confirmed the icon size, placement and hovered icon colors. A spare
compatibility style that reused the primary font caused white hovered action
text: the shared font's material was updated twice. That duplicate style is now
removed, leaving exactly two styles with distinct fonts/materials. The text
therefore retains the ordinary state tint and only the icon font stays white;
the owner accepted the selected action text on 2026-10-28 ("Perfect!", with
a screenshot showing black Confirm text, colored controller/key icons and the
original yellow highlight).
The added styles also carry one empty material-variation string each for
`UILabel::PostLoad` (`824BC3E0`). Omitting those post-load records caused the
2026-10-28 startup crash: the loader treated the next object's marker as a
string length and overran its buffer. The corrected serialization has a test
that consumes both loading phases and verifies the following object marker.
A CDB startup run reached the usable main menu and closed cleanly. The owner
subsequently accepted the mapping row's hover appearance.

Pentatonic's cell is 84.25 by 79.5 pixels. Its glyph fields are UV origin, width
factor and advance; the last field is **not height**. The renderer samples the
full 79.5-pixel height, so 64-pixel atlas rows were invalid. The new band has 15
columns with a 68-pixel stride and two 128-pixel rows. Each 64-pixel icon starts
two pixels inside its horizontal cell and eight pixels below the
cell top, aligning its center with the capital letters (retail A's visible pixels
span y=15..62). Width and advance are `64 / 84.25`. The complete 85-pixel button
source cell retains the A/B circles' lower arc. The complete padded band is
downsampled together so fractional mip coordinates retain the same image placement.
Host tests cover the full sampling window and gutters of all 30 slots. The
material isolation and stray-pixel correction have owner visual acceptance.
A page needing more than 30 images still uses its complete textual list.
The owner rejected the separate View Inputs page on 2026-10-28 because it
repeated images already visible on the mapping row and filled the rest of the
page with empty entries. The in-song list resolves row symbols from its pause
panel as well. An in-list overflow solution remains P9 work.

### Mapping font capacity (updated 2026-11-02)

The icon font is a private copy of Pentatonic. Its BC3 payload now carries one
marker bit in the old spare band, making its texture fingerprint distinct from
the retail text font before the host override runs. The override can fill the
entire private 1024-square atlas without changing ordinary text. The same
64-pixel images, 68-pixel column stride, 80-pixel row stride, glyph dimensions
and `<alt1>` tint are retained. The atlas has 180 tiles; P10 reserves 15 for
guest footer actions, leaving 165 simultaneous mapping-row images. The first
101 row glyph codes remain stable; later row icons skip the reserved tiles.
Repeated sources still share one tile. At 166 distinct visible sources, the
affected row retains its complete textual list and other rows keep images.
Host tests cover ordered 46- and 180-source lists and the 46th atlas tile;
source lists above 165 remain usable through paging. Installed-scene generation confirms only the
marker byte changes in the source BC3 chain. The paused-song and very-large-row
on-screen appearance still need owner visual checks.

### In-list image paging (2026-11-01 host implementation)

The owner accepted an eight-image Confirm row and the paused-song Interface and
Gameplay Mapping rows on 2026-10-31. Rows with more than eight bindings now show
six images plus a `(current/total)` page count. The guest's own Left/Right actions
request the preceding or next six bindings for the selected list position, then
repopulate that list without adding rows or changing its action indices. The host
stores only the display offset; it clamps that offset when bindings are removed.
The icon publication signature includes the offsets so a page turn invalidates
the composed atlas. Host tests reach binding 181 through its last window. The
owner performed the pending long-row fit and navigation visual checks on
2026-11-02 and reported that the result looks good. P9 is accepted; the
guest-hint route inventory for P10 is in
[button-hint-inventory.md](button-hint-inventory.md).

### Guest action-hint contexts (P10 checkpoint, 2026-11-04)

`InlineHelp::UpdateLabels` formats a guest footer from its action ID and
caption. The hook in `action_hint_probe.cpp` publishes the first effective
Interface Mapping source in the last real input family into an action-specific
font tile. `InlineHelp::DrawShowing` refreshes visible labels when the binding,
family or controller-layout revision changes. The loading screen's begin prompt
uses the same Interface Confirm tile; its message handler keeps that image
published while loading.

The rotating `loading_tip_03` prompt promises the Gameplay Mapping Power-up
action. It uses a separate tile, resolved from the effective gameplay table and
current preset. The splash start label promises Interface Start; `native_menu`
publishes its tile while the splash panel is active, including before the first
press. Both labels use the guest's font and locale records. The generated scene
and locale routes pass host checks, but their on-screen appearance and refresh
still need owner validation. Other actionable locale strings and tutorial or
calibration scene images remain open in the
[hint inventory](button-hint-inventory.md).

Touch instructions: `last_device` starts in touch mode on mobile.
Real touch selects that mode on desktop too. Before the guest draws a UILabel
beginning with a localized press verb and button glyph, the existing draw hook
replaces the verb and button with the localized touch verb. This includes start
prompts and powerup descriptions, preserving the complete action and explanation. The
guest's font, animation and label layout still draw the text. The original
string returns live on physical keyboard, mouse button/wheel, controller
activity, or deliberate mouse motion out of touch mode. An idle connected pad
does not change the source. Host tests cover launch defaults, source changes,
all six locale forms, exact text restoration, and scene/text replacement;
on-screen appearance remains a manual check.

### Row-geometry diagnostics, 2026-11-18

`mouse_native_trace` publishes a fresh complete row table when the focused list,
row enablement or projected bounds change. Entrance animations and native window
resizing can move rows after their first draw, so the first logged rectangle is
not necessarily the settled rectangle. Live hit testing already uses each draw's
current geometry; the diagnostic refresh keeps automation aligned with it. Bounds
changes below 0.0001 in normalized coordinates do not trigger another table.
The setting remains disabled for matched performance captures. The migration
plan records the corresponding native UI evidence and retained failed attempts.

## Current legacy-profile compatibility (2026-11-18)

The historical gated-input and F10 recovery measurements above describe the
earlier profile controls. Current KeyboardSupportEnabled and MouseSupportEnabled
always enable raw host sources, so old disabled profile switches cannot create
that lockout. The title does not require an external SDK input driver: keyboard
events continue through its UI listener and title input filter. The maintained
recovery acceptance fixture now verifies that such an old profile can reach the
guest menu with Enter, no external pad driver and no recovery gesture, while
leaving its profile bytes unchanged. Seven checks pass in
`out/rexglue-migration/post-smoke-ui-final-r18/recovery/`; the historical F10
route is not counted as exercised by that result.


## Portrait recovery dialogs (2026-11-26)

Portrait error popups use a host modal with the achievement browser's fonts,
opaque backdrop and three glowing border rails. Its wrapped message and Continue
caption are copied from the guest's localized UILabel strings. The modal fits
inside the device safe area; long text scrolls above a separate touch button.
Landscape retains the native error scene and its original button handler.

The original guest show/hide lifecycle owns the modal. A generation-tagged host
Continue request is consumed once by the error panel's authored poll method,
which executes a verbatim copy of its native Confirm branch. That branch hides
the popup and restores its return panel. Exclusive host input cannot block this
script route. Focus changes, replacement messages and close/reopen transitions
invalidate pending requests. Until localized text is available, the native panel
remains visible rather than leaving an empty modal.

The overlay resolves UIPanel's loaded directory at +8 only for message ownership
and suppressing that dialog's native draw. It does not change the directory
selection used by general portrait panel fitting, cameras, render targets or
presentation settings. The error scene has no portrait mesh or transform edits.
The separate RhythmVerse portrait dialogs retain their existing guest surfaces.

Host checks cover cross-thread publication, duplicate and stale actions,
landscape ineligibility, markup removal, native scene preservation and exact
Confirm-branch dispatch. Device visual/input acceptance requires a signed build.
