# Custom resolutions (ultrawide and taller-than-16:9) — findings

Status: **the render path renders arbitrary aspect ratios correctly.** A 2560x1080 boot on this
machine's own 21:9 primary monitor presents a true 2560x1080 image that fills the window - no black
bars, no stretch - and the UI is laid out in the frame's own aspect. What is **not** done: the
in-game/HUD half of the request, the optional black-bars menu mode, the `Resolution` submenu and its
rows, and the per-layer menu-background policy. Those are listed with their next step in §7.

Evidence labels: **measured** = executed in this pass and read out of a log or a capture;
**source** = read from the repository; **static** = read from the recompiled guest, not observed to
execute.

Read with: `docs/plans/in-game-settings-plan.md` §7 (the resolution requirement),
`docs/plans/in-game-settings-resolution-findings.md` (P2: the title is resolution-blind and derives
its width from its height), `docs/plans/in-game-settings-graphics-findings.md` (P4's capability and
policy shapes).

---

## 1. Verdict

Three separate facts had to be changed for one non-16:9 frame to render correctly. The first two were
already known or already implemented; the third is what this pass found.

| # | What | Where | Before | After |
| --- | --- | --- | --- | --- |
| 1 | The frame the guest allocates | `sub_823F3698` chooses a height and derives the width as `height*16/9` | 1280x720 | **any WxH** (`RenderHeightHook` + `RenderFrameWidthHook`) |
| 2 | The space the UI is laid out in | two adjacent `9/16` floats in the image's literal pool, guest `0x8201E4CC` / `0x8201E4D0` | 0.5625 (16:9) | **1/aspect** (`WriteGuestLayoutAspect`) |
| 3 | How the guest image reaches the monitor | the presenter letterboxes with the *video mode* aspect | 1280x720 | the requested size (`video_mode_width/height`) |

Miss #2 and the frame is drawn *stretched* into a 16:9-shaped layout - which is exactly what the
first attempt at a wider frame produced, and what the owner rejected. Miss #3 and the presenter
squashes the (correct) guest image into a 16:9-shaped region.

Measured, one boot each, frame 1680x720 in a 2560x1080 window, the title screen's own green button
disc used as the ruler (a disc is round only if the projection followed the frame's aspect):

| Arm | Frame | Layout aspect | Video mode | Capture | Green disc | Content box |
| --- | --- | --- | --- | --- | --- | --- |
| `control` | 1280x720 | 16:9 | 1280x720 | 2560x1080 | **44x42** (1.048) | 1920x1080 pillarboxed |
| `wide1b` | 1680x720 | 16:9 | 1680x720 | 2560x1080 | **58x42** (1.381) | 2520x1080 |
| `wide4` | 1280x720 | 16:9 | 1680x720 | 2560x1080 | 58x42 | 2520x1080 (presenter stretch) |
| `wide8` | 1680x720 | `0x82078CC8`=1/aspect | 1680x720 | 2560x1080 | 58x42 | 2520x1080 |
| `wide9` | 1680x720 | `0x8210DBF8`=1/aspect | 1680x720 | 2560x1080 | 58x42 | 2520x1080 |
| **`wide10`** | 1680x720 | **`0x8201E4CC/D0`=1/aspect** | 1680x720 | 2560x1080 | **44x42** (1.048) | 2520x1080 |
| **`uw-native`** | **1280x540 x2 = 2560x1080** | 1/2.370 | 2560x1080 | 2560x1080 | - | **2560x1080, no bars** |

`wide1b` against `control` is the defect quantified: **58/44 = 1.318**, while the frame grew by
1680/1280 = **1.3125**, with the height *unchanged* at 42. That is a pure horizontal stretch, not a
zoom, not a crop - so the layout space, not the projection's scale, is what has to change.

The last row is the product path with no diagnostic flag: the planner chose guest 1280x540 at draw
scale 2 from a requested 2560x1080, and the log reads

```text
settings_boot: guest render base 1280x540, draw scale 2 (before graphics setup)
swapdims d3d12: guest_output=2560x1080 packet=1280x540 packet_unscaled=1280x540
                resolve_dest_scaled=2560x1080 display=2560x1080 draw_scale=2x2 source_is_scaled=true
render: layout aspect 0x8201E4CC 0X3F100000 -> 0X3ED80000 (1/2.3703704 at 1280x540)
render: layout aspect 0x8201E4D0 0X3F100000 -> 0X3ED80000 (1/2.3703704 at 1280x540)
```

(`0x3ED80000` is 0.421875 = 1/2.3703704, the guest's own single-precision value. The two lines are
verbatim from `out/_swap/ultrawide/final-uw.log`.)

## 2. Why the title can only ever be 16:9 (already measured by P2, restated here because everything
below depends on it)

`sub_823F3698` (guest `0x823F3698`) is where the title decides its own screen: it stores a height at
`+68` - `li r11,720`, or `540` when bit 0 of its own `+796` is set - and then *derives* the width at
`+64` as `height * 16 / 9` (or `* 4 / 3` for a 4:3 display). The frame's render targets are built
inside that function from the height it chose, which is why poking `+64`/`+68` afterwards changes
nothing (P2, `out/_swap/p2/screen1080.log`). The video mode is never read by game code (P2 §4).

Two levers were added to that one function, both as `[[midasm_hook]]` entries in
`config/functions.toml`:

- `RenderHeightHook` at `0x823F382C` (`after_instruction`, `r11`) - already existed; P11a uses it for
  the 540 half-resolution mode. It can now carry any height, not just 540/720.
- **`RenderFrameWidthHook` at `0x823F3878`** (before the `stw r7,64(r30)`, registers `r7` and
  `r29`) - new. `r7` is the width field; `r29` is the same width carried into the two-rect table the
  function builds immediately after, so both have to agree or the frame and its viewport disagree.
  Setting both to an arbitrary width is what makes a non-16:9 frame exist at all: measured, the
  guest then reports `chose 1680x720` and the SDK allocates a 21-tile (1680 px) pitch render target.

`SetBootRenderFrame(width, height)` is the product seam; a width the title would have derived itself
(zero, or exactly 16:9 of the height) is deliberately **not** applied, so every existing 16:9 boot -
including the title's 4:3 branch - is untouched.

## 3. The layout space: the stretch, and what fixes it

### 3.1 What was measured

The guest is told the frame is 1680x720 (the per-draw viewport log shows
`viewport d3d12: x=0 y=0 w=1680 h=720`), and it draws the full frame - but it draws it *as if the
screen were 1280x720*: a UI disc comes out 1.3125x wider than tall. So the engine maps a fixed
1280x720-shaped space onto the frame, and the width it maps is `height * 16/9`, computed *before*
anything this project controls.

Three candidate mechanisms were tested and two were falsified:

| Hypothesis | Test | Result |
| --- | --- | --- |
| The guest derives its layout width from the height somewhere else | every `rlwinm ...4,0,27` (x16) followed by a `divw` in the recompiled image | **exactly one site in the image** - the frame width this project already hooks. The layout width is not arithmetic. |
| The engine's 16/9 literal in read-only data, guest `0x82078194` | wrote `2.3333333` there for one boot (`probe_aspect`) | no change (and the write is refused as a *guest* access violation until the page is opened - the image really is read-only) |
| The layout size is stored as a `1280x720` pair | scanned the loaded image for an int pair and a float pair, poking each per frame (`probe_screen_fields`) | **no such pair exists** |

The scan that *did* find it looked for the *inverse* aspect (`9/16 = 0.5625`) and found five sites;
poking them all fixed the stretch, and narrowing to one address each identified the pair:

| Address | `wide10`-style result |
| --- | --- |
| `0x82078194` (16/9) | no effect (alone) |
| `0x82078CC8` | no effect (`wide8`) |
| `0x8210DBF8` | no effect (`wide9`) |
| **`0x8201E4CC` and `0x8201E4D0`** | **disc 44x42 - the stretch is gone (`wide10`)** |

The two are adjacent 4-byte-aligned `0.5625` floats, i.e. a literal pool entry used by two sites of
the same code region, and they are the factor that maps a screen-space x into the 16:9 space. Writing
`1/aspect` into both is the whole fix.

### 3.2 Why this is the honest fix rather than a hack around the engine

The engine has exactly two aspect modes - widescreen (16/9) and 4/3 - and it expresses the
widescreen one as this constant. A display whose aspect is neither is a case the title never had, so
the constant is the only place the aspect exists. Writing the *user's real aspect* there is the same
change as telling the engine what the display is; nothing is scaled, cropped or resized, and the
content of the scene is drawn at its own proportions (measured: the disc is round, and the frame's
content box is exactly the frame's own aspect).

## 4. Presentation: the video mode is not optional

The SDK's presenter builds its letterbox region from the **video mode** aspect, not from the guest
frame's (P2 §3; confirmed again here: the `wide4` arm, whose guest frame was a correct 16:9 but whose
video mode was 2.33, was squashed by the *presenter* into 58x42 - the same numbers `wide1b` produced
for a different reason). So a boot that renders a non-16:9 frame must also set
`video_mode_width/height` to that size. That is not "changing the reported mode to fake a
resolution" - the title does not read it (P2 §4) - it is telling the presenter what shape its input
is.

## 5. The planner: which requested sizes are offered, and which are refused

`RenderPathLimits` (source) now carries the window a *forced* guest size has to stay inside, and
`PlanGuestScreen` (moved to `settings/capabilities.{h,cpp}` so the offer set and the boot answer "can
this size be rendered?" with one rule) has two paths:

1. **The title's own sizes.** An exact `base x scale` match - the catalogue is `{960x540, 1280x720}`
   at scales `{1,2,3}`, so every shipped 16:9 size keeps the title's own layout and behaviour.
2. **A forced guest size.** Both dimensions must divide exactly by one offered scale (the render
   target *is* the frame times one integer multiplier - an approximate frame is the output-size
   substitute this project forbids), the guest height must be one the layout is measured at
   (**540..720**: 720 is the shipping size, 540 the title's own half-resolution mode; a height the
   title never lays out at is what corrupts its title-screen artwork - 1080 was measured), the guest
   width must be inside the measured EDRAM pitch class (**640..2048**; 21 tiles = 1680 px measured
   working), and the aspect must be inside **1.0:1 .. 3.2:1**.

Worked examples (the thresholds are deliberately conservative; each one below is arithmetic, only
2560x1080 and 1680x720 are measured in a boot):

| Requested | Plan | Why |
| --- | --- | --- |
| 2560x1080 (21:9) | guest 1280x540 x2 | **measured working** (`uw-native`) |
| 1680x720 | guest 1680x720 x1 | **measured working** (`wide10`) |
| 3440x1440 (21:9) | guest 1720x720 x2 | 1720 <= 2048, height 720 |
| 1920x1200 (16:10) | guest 960x600 x2 | height 600 inside the window |
| 3840x2160 | guest 1280x720 x3 | the catalogue's own match |
| 2560x1600 (16:10) | **refused** | needs guest 1280x800 (height above the measured window) or 853x533 (below it) |

`SupportedResolutions` now also offers a **display's own mode** when the planner can produce it, so a
21:9 primary gets its native 2560x1080 in the list - which is what made the `uw-native` arm work with
no diagnostic flag at all. With no display enumeration the catalogue is published unchanged (the
existing rule 3 is preserved).

## 6. What the menu background actually is (the owner's question)

**Measured/decoded:** the main menu is not a set of layered 2D images stacked by the UI system. It is
a **3D scene** with several `RndDir`s that the shell renders through one camera:

| Scene | What it carries |
| --- | --- |
| `ui/background/gen/background_city.milo_xbox` (2.0 MB, 30k names) | the sky/gradient (`sky.tex`, `bg.mat`), the city meshes (`city.tex`, `graphic_city*.tex`), rays/vignette, and the moving `tracks_*` sub-scenes |
| `ui/background/gen/foreground_city.milo_xbox` (2.6 MB) | the foreground silhouettes |
| `ui/background/gen/tracks*.milo_xbox` (6 files) | the animated track/rail textures |
| `ui/anim_logo/gen/anim_logo.milo_xbox` (1.4 MB) | the logo |
| `ui/background/gen/background.dtb` | the `background_panel` script (headers, nav arrows, inbox) |

Consequences, all of them measured:

- **Nothing is scaled or zoomed by widening the frame.** With the layout aspect fixed, the camera's
  view simply covers more of the same scene: the gradient, the moving rails, the city and the logo
  all keep their own proportions and their own sizes on screen. That is why the disc measured round
  and the logo did not grow - the "expand the gradient and the animated texture, do not scale the
  elements" requirement is satisfied by *not* stretching, without any per-layer work.
- If the owner wants *more* of the gradient/moving texture than the widened camera reveals (a
  deliberate per-layer zoom or a stretched background quad), that is a scene-level change: those
  objects are `RndMesh`/`RndMat` entries inside the scenes above, and the hook for it is the same
  delivery route P5b already uses (a project-owned record riding inside a retail DTB). It is **not**
  needed for a correct, unstretched ultrawide menu, and nothing in this pass did it.

## 7. What is done, and what is not

**Done (this pass)**

- `RenderFrameWidthHook` (`config/functions.toml` + `src/hooks/render_settings.cpp`): the frame's
  width is no longer the title's own to choose, for the first time.
- `SetBootRenderFrame()` writes both halves of the guest size; a 16:9 width is left alone, so no
  existing boot changes behaviour.
- `WriteGuestLayoutAspect()` writes `1/aspect` into the two measured layout constants, once per boot
  per value, and only when the frame is one the title would not have chosen.
- The planner is aspect-aware and the offer set now includes a display's own mode when the render
  path can produce it; the boot writes the frame + scale + video mode. Verified end to end at
  2560x1080 with no diagnostic flag (`out/_swap/ultrawide/uw-native.log`, `shots-uw-native/`).
- Spike harness and instruments: `out/_swap/ultrawide/run.ps1` (arms, screenshots, log extraction),
  `measure.ps1` (content box + a UI disc's roundness - the measurement that distinguishes "expanded"
  from "stretched"), `scan_aspect.py` (constant hunt), and the gated diagnostics
  `probe_aspect` / `probe_screen_fields` / `probe_screen_fields_filter`.

**Not done — in the order I would pick them up**

1. **In-game 3D and the HUD.** Everything above is measured on the shell (title/menu). The gameplay
   camera uses the same engine space, so the aspect fix should already apply - but that is a
   hypothesis, not a measurement: it needs a song boot at 2560x1080 with the note highway and the HUD
   in frame. The HUD is the part the owner called out: with the layout space widened, a HUD element
   authored at the old 16:9 left edge now sits *inside* the frame, so those elements have to be moved
   to the new edges. Nothing in this pass touched HUD layout.
2. **The black-bars menu mode** (`Use black bars on menu`, default off). Not started. It needs the
   menu's own rendering to keep the 16:9 layout while gameplay uses the full frame - the natural
   implementation is the same two constants set to 16/9 for menu frames and 1/aspect for gameplay
   frames, which is a state machine over "which screen is up", not a new render path.
3. **The settings surface.** The owner's suggestion (`Resolution` submenu under Video, or a
   `Graphics` submenu) is the right shape, because Video would otherwise carry Resolution, V-Sync,
   Renderer, Anti-Aliasing, A. Filtering and Overscan. Rows needed: the resolution list itself (now
   including the display's ultrawide modes) and the black-bars boolean. That is `src/settings/schema.cpp`
   plus P5b's declarations in `src/ui/settings_screen.cpp` and the locale keys - a mechanical but
   non-trivial edit in the 78 KB / 13k-check file, which is why it is not in this pass.
4. **Thresholds worth measuring rather than guessing.** The planner's window (guest height 540..720,
   width 640..2048, aspect 1.0..3.2) is conservative by construction. A boot at a guest width of
   1920 (24 tiles) and one at a guest height of 600 would say whether those bounds can move out.
5. **Coordinator items.** (a) The aspect write's guest-side call site is the `sub_823F3698` override
   in `src/diag/probe_points.cpp`, which is a *diagnostic* translation unit; the coordinator should
   either bless that as a hook home or move the two calls to a product hook (two overrides of one
   guest function cannot coexist, so it cannot be a second `REX_HOOK_RAW`). (b) The two layout
   constants are version-sensitive. A shape guard is in place - the field must read `0.5625`
   (`0x3F100000`) before the write, otherwise the write is refused with a warning naming the
   address and the value found (`render: 0x... holds ... not the measured ...`), and the latch is
   only set once both writes landed - but the *addresses* themselves still have to be re-measured if
the supported retail image ever changes.
6. **Two pre-existing failures in the tree, not from this pass.** `src/settings/service.cpp` did not
   compile at HEAD (`ValueKind::kStringList`, `kAction` - fixed here to `kList` /
   `WidgetKind::kAction`), and ctest's `settings_screen` fails one check of 17,709
   (`has_op(Op::kInvoke)`) which is unrelated to the render path. Both were in the checkout before
   this pass started.

## 8. Reproduction

### 8.1 Making an arm fast: skip the content discovery

The title enumerates Xbox content **one item per presented frame**, so the developer's own library
(1,402 packages = 2,804 items) is 47 s of frames at 720p and ~90 s at 2560x1080 - enough to swallow a
driving route. Two launch flags make an arm hermetic and take the discovery down to seconds:

```text
--dlc_library=<empty dir>      # the flat library the mod presents to the title
--dlc_root=<empty dir>         # the root paths.toml names; a CLI value outranks that file
```

Both matter. `--dlc_library` alone only *adds* a root (`dlc: scanning 2 library root(s)`), because the
DLC root comes from `paths.toml` next to the executable; only overriding `--dlc_root` as well removes
the 1,402 packages. `run.ps1` creates `_scratch/emptydlc` and passes both, and an in-song arm then
reaches a playing song in about three minutes.

### 8.2 The two corner blocks, and why they were there

A 2560x1080 boot used to draw two ~120 px white blocks in the bottom corners of the window
(x 0..128 and 2448..2560, y 960..1072 in the capture; crop `crops/corner-left.png`). They are **not**
this project's debug markers - nothing in `src/` draws a rectangle, and the capture tool draws nothing
either.

Measured, one scan per arm (`out/_swap/ultrawide/find_white_blocks.ps1`):

| Arm | Guest frame | Blocks |
| --- | --- | --- |
| `control` | 1280x720 | none |
| `wide10` | 1680x720 | none |
| `regress-1080` | 960x540 x2 | none |
| `song-720b` | 1280x720 | none |
| `uw-native`, `final-uw`, `song-uw2` | **1280x540 x2** | **two, at the bottom corners** |
| `nosd` (height forced, flag clear) | **1280x540 x2** | **none** |

The pair that separates the last two rows is `probe_halfres_flag`: every arm above reached 540 by
pressing the title's own half-resolution flag (bit 0 of `+796`), which is what P2 measured and P11a
shipped, and that flag's own SD path draws the blocks. Forcing the **height** alone - the
`RenderHeightHook` sets `r11` and the flag stays clear - renders the same 2560x1080 frame with no
blocks at all (`nosd.log`: `guest_output=2560x1080`, `resolve_dest_scaled=2560x1080`, layout aspect
written). The height-only route is therefore the default now (`probe_halfres_flag` defaults to
`false`; setting it true restores the P11a route for comparison), and every frame of the in-song
ultrawide arm (`shots-song-uw3/`) scans clean.

### 8.3 Arms

```powershell
# The real product path at this machine's own ultrawide mode (no diagnostic flags):
pwsh -File out\_swap\ultrawide\run.ps1 -Label uw-native -Request 2560x1080

# The defect and its fix, frame by frame (the disc is the ruler):
pwsh -File out\_swap\ultrawide\run.ps1 -Label wide1b -Request 1680x720 -Bases 1680x720
pwsh -File out\_swap\ultrawide\run.ps1 -Label wide10 -Request 1680x720 -Bases 1680x720 `
    -ScreenFields -ScreenFieldFilter "0x8201E4CC,0x8201E4D0"
pwsh -File out\_swap\ultrawide\measure.ps1 out\_swap\ultrawide\shots-wide10\wide10-01-title.png
```

`-Bases` is the *spike's* way of offering a guest size to the planner before the aspect-aware rule
existed; the live planner (§5) needs no flag for a size a real display has.

Every arm boots against the disposable root `out\_swap\ultrawide\_scratch` with an empty legacy
profile; it never reads or writes the owner's profile, save, DLC library or song cache. Each arm also
clears `settings.toml.graphics-boot.toml`, because an arm killed before the menu leaves a *pending*
graphics attempt behind and the next boot then deliberately (and correctly) falls back to the
measured-safe floor - which silently masked two arms before that was understood.

## 9. Evidence index

| Path | What it proves |
| --- | --- |
| `out/_swap/ultrawide/control.log`, `shots-control/` | the 16:9 reference: disc 44x42, 1920x1080 pillarboxed |
| `out/_swap/ultrawide/wide1b.log`, `shots-wide1b/` | frame 1680x720 with the layout left at 16:9: disc 58x42 - the stretch |
| `out/_swap/ultrawide/wide8.log`, `wide9.log` | the two falsified constants (no effect alone) |
| `out/_swap/ultrawide/wide10.log`, `shots-wide10/` | **the fix**: `0x8201E4CC`/`0x8201E4D0` at `1/aspect`, disc 44x42 |
| `out/_swap/ultrawide/uw-native.log`, `shots-uw-native/` | **the product path**: 2560x1080 rendered and presented, no bars, planner-chosen 1280x540 x2 |
| `out/_swap/ultrawide/run.ps1`, `measure.ps1`, `scan_aspect.py` | the harness, the measuring instrument, the constant hunt |
| `out/_swap/ultrawide/find_white_blocks.ps1` | the corner-block scan (§8.2) |
| `out/_swap/ultrawide/shots-song-uw3/`, `shots-song-720b/` | in-game at 2560x1080 vs 16:9: the 3D world expands on both sides and the HUD markers sit at the wide frame's own edges |
| `config/functions.toml`, `src/hooks/render_settings.{h,cpp}` | the three levers and their measured evidence |
| `src/settings/capabilities.{h,cpp}`, `src/settings/graphics_policy.cpp` | the aspect-aware planner and the offer-set rule |
