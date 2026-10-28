# Button layout hot-swapping — button glyphs and controller art

**Status: in progress, created 2026-10-19; scope revised 2026-10-27. P0 delivered
2026-10-26; P1, P2 and P3 delivered 2026-10-27; P6a and P8 delivered 2026-10-28.
P4, P5, P6b, P7 and P9–P10 remain open.** The format measurement in §2.3 was made on 2026-10-19 and its decoded evidence is in
`out/button-schemes/format-2026-10-19/`; everything else marked `[tree]` is read from the current
tree, and everything marked **unproven** is not yet measured in a boot.

This plan makes the button glyphs and the controller-layout art **replaceable per scheme**, lets the
player choose a controller scheme in Help & Options, hot-swaps it without a restart, and switches
automatically between controller and keyboard/mouse art based on the last real input. It also adds
an **optional installer flow** that extracts the one platform-specific asset from the user's own
Rock Band Blitz PS3 package, and a **tracked authoring structure** under `assets/controller/`.
Both mapping screens display bound inputs as images, and the game's action hints follow the
effective bindings. Keyboard/mouse icons are built in and always available; only controller art
is selectable.

The 2026-10-26 revision supersedes the static-keyboard limitation formerly in D12, renames the
setting to **Controller Layout**, expands the controller catalog to seven families, and adds
**P8–P10** for input-icon resolution, mapping-row images, and remap-aware guest hints. Existing
prompt ids are retained; the dependency graph defines execution order. Live behavior remains
**unproven** until the corresponding implementation and acceptance prompts pass.

The owner's later 2026-10-27 UI correction supersedes the selection-page design in D3/D6/D14,
§5.3 and P3: Controls has one direct `Layout: <name>` selector. There is no layout submenu,
keyboard/mouse information row, discovery information row, or dedicated preview. Package discovery
still occurs at boot and selecting a layout still applies immediately.

The 2026-10-27 asset revision changes P2 onward: custom schemes provide individually named glyph
and pad PNGs, while omitted slots keep the corresponding art from the user's base game. Source
images are resized proportionally into measured destinations. P6a must deliver a standalone
custom-package guide in `assets/controller/README.md`. P0's local sheet exports and P1's
whole-texture override remain valid evidence and infrastructure.

## 1. Request, normalized

| Id | Requirement |
| --- | --- |
| R1 | Replace the **button glyphs** (A/B/X/Y, LB/RB, LT/RT, Start/Back, LS/RS, D-pad, and the other slots the sheet carries) and the **controller-layout art** shown on the retail Control Preset page — everywhere the title draws them, not only on that page. |
| R2 | The installer offers an **optional Rock Band Blitz PS3 `.pkg`** and extracts exactly the assets this feature needs. If the user supplies it, a **PS3 layout** appears in the in-game list. |
| R3 | A tracked structure under `assets/controller/` where the owner leaves PNGs (and a small manifest) to build a **custom scheme package**. |
| R4 | **Keyboard/mouse icons are built in, always available, and not selectable or replaceable by controller packages.** Real keyboard/mouse input switches ordinary action hints to their effective key/mouse icons; real controller input switches back to the selected controller family. |
| R5 | Help & Options → Controls has one **Layout: <name>** selector. Each accepted selection applies immediately to the game's controller art; no Apply, screen exit, restart, or subsequent controller input is needed. |
| R6 | Seven controller families: **Xbox 360** (default, base-game art), **PlayStation 3** (local import), **Xbox Series**, **PlayStation Series**, **Nintendo Switch**, **Steam Controller**, and **Steam Deck**. The latter five use owner-authored art; custom controller packages remain discoverable. |
| R7 | With no custom controller scheme installed, controller art falls back to **Xbox 360** from the base game. Built-in keyboard/mouse icons and binding-aware hints remain available while the feature is enabled. |
| R8 | No game art is committed, bundled or downloaded. PS3-extracted art lives only in the user's install or user-data folder and is never redistributed. |
| R9 | R4's existing `enhancements_icon_schemes` toggle and the settings program's contracts are honoured; the feature is documented in `engine/toggles.md` and the settings documents. |
| R10 | Tests, a live acceptance harness and the documents named in §10 are part of the change. |
| R11 | **Interface Mapping and Gameplay Mapping** render bound inputs as button/key/mouse images side by side, in saved binding order, instead of textual input names. For example, `Confirm: [A icon] [Enter keycap]` contains two images; the action label remains text. Mapping rows show all bindings; overflowing rows use the guest's layout/scrolling with no silent omission. |
| R12 | The **effective Interface Mapping dictates every interface footer and action hint**: mapping physical Y to Confirm draws Y in the selected controller family wherever Confirm is offered. For multiple controller or keyboard/mouse bindings, ordinary hints show the **first mapped source in the active device family**. Rebinding, reset, and profile changes refresh visible hints live. Gameplay hints use the effective Gameplay Mapping/preset for their context. |
| R13 | A controller scheme supplies **individual images for only the glyph and pad slots it changes**. Omitted slots retain the corresponding base-game art without requiring the author to distribute it. Source PNG dimensions may differ from the destination: scale to fit without cropping or stretching, center on a transparent slot canvas, and preserve aspect ratio. `assets/controller/README.md` must explain the complete custom-package workflow. |

### 1.1 Scope cross-check, 2026-10-26

| Owner request | Earlier coverage | Revised delivery |
| --- | --- | --- |
| Images in both mapping screens | Missing; existing scope replaced sheets only | R11, D13, P8/P9 |
| Direct `Layout: <name>` row on Controls (owner correction 2026-10-27) | Earlier revision required a Controller Layout submenu | R5, D6, P3 |
| Keyboard/mouse always present with live switching | Switching planned, keyboard depended on an authored package | R4, D4/D8, P4/P8 |
| Bindings dictate footer and game hints; first source per family | Explicitly excluded by static D12 | R12, rewritten D12, P8/P10 |
| Immediate selection effect | Hot apply planned | R5, D14, P3/P4 |
| Seven controller families | Xbox Series, PS5, Switch and an authored Keyboard package | R6, §4.6, P2/P6a/P6b |

## 2. What exists today

### 2.1 The R4 toggle and the art locations

R4 already exists as an enhancement toggle, off by default and explicitly **not implemented**:

- `src/enhancements.cpp:62` — `enhancements_icon_schemes`, described as "let the button glyphs and
  controller-layout art be replaced per scheme. Faithful: the art the title ships. Not implemented."
- `docs/engine/toggles.md` — the R4 row names the old A1/A2 research prompts.

The art the feature must replace is **scene-embedded**, not standalone archive entries:

| Slot | Scene | Builder-side record | Size | Role |
| --- | --- | --- | --- | --- |
| Button glyphs | `ui/resource/fonts/gen/buttons.milo_xbox` | `../image/icons_buttons_xbox_nomip.bmp` | 512×512, 8 bpp | every A/B/… prompt, drawn through a `Glyph` table |
| Extra icons | `ui/resource/fonts/gen/blitz_icons.milo_xbox` | `../image/blitz_icons_xbox_nomip.bmp` | 1024×512, 8 bpp | the additional icon sheet (optional in v1) |
| Pad art | `ui/controller_config/gen/controller_config.milo_xbox` | `img/xbox_0..3.png`, `img/ps_0..3.png` | 1024×1024, 8 bpp each | the four controller-layout images per family; both families ship in both builds |
| Calibration prompts | `ui/calibration/gen/cal_auto.milo_xbox` | `button_a_calibrate_icon0001/2.bmp`, `button_press_ps30001/2.bmp` | 256×256 | calibration-specific icons; **out of scope in v1** |

The settings screen routes **Help & Options → Controls → Gameplay Mapping → Controller → Controller
Presets** to retail's `controller_config_screen` (`src/ui/settings_screen.cpp`, the
`settings_controller_presets` RouteRow). Swapping the pad art must reach that routed screen.

### 2.2 The settings and input machinery the feature plugs into

`[tree]` The settings surface is data in `src/ui/settings_screen.cpp`:

- `Kind::kSelect` is the horizontal-select row; its choices are `Row::choices`, stable tokens plus
  labels, never indices (`docs/settings/plan.md` §3.3).
- `MenuConfiguration` (`src/ui/settings_screen.h`) already carries `choices` — a map from a setting
  symbol to `(token, label)` pairs — and `AllPanels()` reads it. The boot fills it before the menu
  is generated. A dynamic scheme list can use this existing seam instead of inventing one.
- `SettingsBoot` materializes descriptors; `Descriptor::choices_source` already models
  `"renderers"`, `"resolutions"`, `"provider"` and `"menu_rows"` (`src/settings/schema.cpp`).
- `HotApplier` (`src/settings/hot_apply.h`) is the live-apply seam; a row is `kHot` only with
  `strategy_evidence` naming a measured live effect. `interface.settings_labels` is the precedent.

The following input notes are the **2026-10-19 snapshot**; current mapping surfaces are called
Interface Mapping and Gameplay Mapping. P8 must inspect their effective binding APIs rather than
derive a new icon table from the old default-key list.

`[tree]` The input layer already exposes the physical devices without the SDK's synthetic
keyboard-to-pad emulation:

- `src/input/source_gates.h` — `PhysicalInput` (raw pad, key state, mouse buttons, focus) and
  `SourceHeld`; `src/input/title_input.h` — `SamplePhysicalPad` reads the physical drivers only.
- `src/profile/remap.h` — the 16 targets and the compiled defaults. The default keyboard mapping
  is relevant to the keyboard sheet: `Return`/`A`=A, `Escape`/`B`=B, `X`=X, `Y`=Y, `W/A/S/D` or
  the arrow keys=D-pad, `Space`=Start, `Backspace`=Back; LB/RB/LS/RS are pad-only and the triggers have
  no keyboard default. The keys are interface-oriented because Gameplay Mapping's Keyboard + Mouse
  table is on by default (2026-10-19).
- There is **no last-device tracker** yet; the source gates decide whether a device may act, not
  which device last acted.

`[tree, inspected 2026-10-26]` `src/profile/input_rows.{h,cpp}` owns `BindingRowLabel` and
`InterfaceTargetLabel`; `src/ui/settings_screen.cpp` has `LiveBindingRows` and separate Interface
Mapping / Gameplay Mapping pages. These are inspection starting points for P8/P9, not evidence
that either image rows or remap-aware guest hints already work.

`[tree]` The SDK's texture path is a common base plus two backends:

- `rexglue-sdk/include/rex/graphics/pipeline/texture/cache.h` — `TextureCache`, `TextureKey`
  (guest page, dimensions, tiled, format, endianness, mip count), `Texture::MakeUpToDateAndWatch`,
  `ClearCache`.
- `rexglue-sdk/src/graphics/{vulkan,d3d12}/texture_cache.cpp` — the per-backend
  `LoadTextureDataFromResidentMemoryImpl`.
- There is **no texture-override or replacement hook** today.

`[tree, inspected 2026-10-26]` Image conversion already works in both directions outside that
cache. `src/assets/texture_codec.{h,cpp}` parses/writes the 32-byte standalone `.png_xbox` /
`.png_ps3` envelope, swaps 16-bit DXT words, decodes/encodes DXT1 and DXT5, and resizes RGBA.
`src/store/album_art_service.cpp` uses WIC to decode a downloaded image to RGBA and
`BuildPlatformTexture` to serve a `.png_xbox` through the game's own album-art request.
`src/ui/menu_music_artwork.cpp` uses `DecodePlatformTexture` to turn an installed song cover into
RGBA; `src/ui/menu_music_player.cpp` uploads that RGBA with `ImGuiDrawer::CreateImageTexture` for
the **existing music-player overlay**. `scripts/hmx_tex.py` also exports/imports standalone
textures and swaps standalone archive entries. These paths prove conversion and their respective
serving/drawing routes, not replacement of a scene-embedded button sheet or live invalidation of
the guest texture cache. The music-player upload is an overlay draw path, so it is not the guest UI
route required by D13.

`[tree]` The installer is a separate, Windows-only C++ helper (`installer/src/`) driven by Inno
Setup (`installer/setup.iss`). Its commands are deliberately small (`probe-folder`, `probe-package`,
`import-game`, …); the helper already links a few SDK-free project modules from the emulator tree so
the installer and the game cannot drift. It has no PKG parser and no AES, and there is no optional
PS3 page today.

### 2.3 The scene texture format — measured 2026-10-19, offset corrected 2026-10-26

The prior record in `docs/guides/assets.md` said a UI texture could not yet be swapped because the
pixel buffer's format and offset inside a `.milo` chunk were unpinned. **That is now measured** for
the two scenes this feature needs. A `.milo` chunk carries a **23-byte texture header**
followed by **eight zero bytes** before the DXT5 payload:

```
record (23 bytes):  <4-byte per-file id> 08 00 00 00 18 00 02 00 02 00 02 00 00 00 00 00 00 00 00
zero preamble:      00 00 00 00 00 00 00 00
payload:            512 × 512 × 8 bits = 262,144 bytes, DXT5; swap 16-bit words for Xbox only
```

For `buttons.milo_xbox` the header starts at **stream offset 0x5C9** and the payload at **0x5E8**;
decoding the payload after `hmx_tex.unswap()` gives a clean 6×6 glyph sheet with **30 occupied
cells**. The PS3 `buttons.milo_ps3` is the same size and header shape (only the 4-byte id differs)
and decodes at the same offset to a **different** sheet — the PlayStation symbols — on the same 6×6
scene grid **without** a word swap. Its art has more internal components, so a component count is **not** the layout check;
P0 compares the two scenes' `Glyph` tables. The controller scene's eight pad images decode the same
way at **header + 31** as 1024×1024 DXT5 in the texture chunks (chunk 6 carries two of them); the
two scroll arrows are the small chunks.

Evidence (gitignored):

- `out/button-schemes/format-2026-10-19/buttons-360.png` (superseded misaligned decode)
- `out/button-schemes/format-2026-10-19/buttons-ps3.png` (superseded misaligned decode)
- `out/button-schemes/format-2026-10-19/buttons-grid.png` (historical overlay on misaligned decode)
- `out/button-schemes/format-2026-10-19/pads-contact.png` (superseded misaligned decode)
- `out/button-schemes/format-2026-10-19/measurements.txt` (historical commands and offsets)
- `out/button-schemes/p0/` and `assets/controller/template-360/` (correctly aligned local exports)

What this settles for the plan:

1. **The PS3 import has a concrete route.** The only platform-specific art is the button sheet; the
   pad families (`ps_0..3` and `xbox_0..3`) already ship in the 360 scene, so the PS3 layout's pad
   art does not have to come out of the PS3 package at all.
2. **Authors do not encode DXT5.** P1 proved that the project can provide canonical BC3 mip data
   to both graphics backends without rewriting the guest's DXT5 payload. P2 must decode and fit
   individual PNGs, compose them with original game glyphs, and encode the replacement texture.
3. **The slot map is a real grid.** The sheet is 6×6 (about 85×84 px per cell); the exact slot
   rectangles come from the scene's `Glyph` table, which P0 reads.

P1 measured the boot-time button and first-pad texture fingerprints. P2 must identify the
remaining pad slots before claiming independent replacement of all eight.

## 3. Decisions

| Id | Decision |
| --- | --- |
| **D1** | **Controller-art hot swap uses a host texture override.** The SDK texture cache has a generic override provider; the project supplies complete canonical BC3 mip data for a matched guest texture, keyed by a measured fingerprint (dimensions + content hash, with the scene-read order as the fallback). P9/P10 may also change authored guest UI and served labels/records to express input icons and action identity; a texture swap alone cannot describe every binding. P1 proved live replacement on D3D12 and Vulkan; the earlier scene-reload fallback is no longer needed. |
| **D2** | **A scheme is a folder with one manifest and individual PNGs.** `scheme.toml` names the id, display name, kind and any button-slot and pad-slot image paths. Every slot entry is optional, but a package must replace at least one button or pad slot; an extra-icon sheet alone is insufficient. Omitted entries retain that same slot's base-game art. `icons.png` remains an optional whole 1024×512 extra sheet. No DXT5, scene, archive surgery or copied retail pixels in custom packages. |
| **D3** | **Discovery is a boot scan, not a hot-add.** The game scans `<install>/assets/controller/schemes/` and `<user_data_root>/controller_schemes/` before authoring the menu. Any valid non-system user id shadows the same id in the install root with a log line; only `retail`, `keyboard`, and the PS3 import id are reserved. Newly dropped packages are read at the next boot. There is no Rescan row in v1. |
| **D4** | **Only the controller layout is selectable.** The page lists `Xbox 360` plus every available valid controller scheme. Keyboard/mouse icons are a built-in provider, never an authored/discoverable layout or a list choice. Boot starts in keyboard/mouse hint mode; the first real pad event selects controller hint mode. Device switching never changes the saved controller selection. The selection page's explicit controller preview is independent of ordinary last-device hints (D14). |
| **D5** | **PS3 import extracts one asset.** From a `.pkg` (plus the matching `.rap` if needed) or an extracted PS3 game folder, the installer extracts `ui/resource/fonts/gen/buttons.milo_ps3`, decodes its DXT5 sheet, and builds the PS3 scheme. The `ps_*` pad art comes from the user's 360 `controller_config.milo_xbox` via the same decoder. Nothing from the package is copied wholesale; the full ~350 MB ARK is never laid down. |
| **D6** | **One native setting: `controller.scheme`, displayed as `Layout: <name>` directly on Controls.** The stable token is `retail` or the id of any valid discovered controller package, including the optional `ps3` import. It lives in `settings.toml` through the main settings service, not in the profile's binding store. `retail` is displayed as `Xbox 360`. |
| **D7** | **`enhancements_icon_schemes` becomes the master gate and defaults on**, with `Xbox 360` (`retail`) as the compiled default. With the gate off the Layout row is omitted and faithful retail art is used. This changes the off-by-default line in `src/enhancements.cpp`; the toggle table records it. R4/R11/R12 describe enabled behavior. |
| **D8** | **Fallbacks preserve the device family and each guest slot.** Missing/invalid selected controller package → Xbox 360 art, logged and reflected in the setting. Within a valid partial package, an unlisted button or pad slot keeps the corresponding original game slot/texture; an explicitly listed but invalid file rejects the package rather than silently falling back. Keyboard/mouse → built-in keycap/mouse provider, with a built-in neutral image for an unsupported source or no binding; never controller art because a keyboard package is absent. Invalid controller packages are skipped and never listed. |
| **D9** | **Authors provide independent, variably sized PNGs.** Button art is white/grey with straight alpha because the game's material tints glyphs (colored art is allowed but multiplied by the tint). For each supplied button or pad image, the loader scales uniformly to fit the measured destination rectangle/canvas, centers it, and leaves unused space transparent; it never stretches or crops. The runtime composes the full 512×512 button texture from the user's original game art plus supplied glyphs, then encodes all required BC3 mips for the existing guest texture override. The art author never handles DXT5 or positions. |
| **D10** | **No retail/game art in the repository or the release.** The five owner-authored controller schemes and built-in keyboard/mouse art must be original or appropriately licensed. Xbox 360 art stays in the user's base game; the PS3 scheme is generated locally and is gitignored/audited like `game/` and `assets/game/`. |
| **D11** | **The retail pad page has eight independently replaceable textures.** A scheme may name any subset of `xbox_0..3` and `ps_0..3`; each omitted texture remains the corresponding original Xbox/PS game texture. An optional shared pad image can fill all eight, with explicit per-slot images taking precedence. Do not collapse distinct preset images or substitute one family's base-game pad for the other. |
| **D12** | **Effective bindings are the source of truth for icons.** A shared resolver maps `(action, interface/gameplay context, device family, effective profile/preset)` to ordered physical sources, then their icons. Mapping rows render every source; ordinary action hints render the first source in the active family. Controller glyphs use the selected family; keyboard/mouse glyphs use the built-in provider. No static default-key sheet after rebinding. Profile/preset changes, assigning, removing, reordering and resetting invalidate the relevant views. |
| **D13** | **Draw icons through the guest's UI.** Mapping rows keep their text action labels and render adjacent input images; guest footers, widgets, labels and served records carry the resolved hints. A shared 6×6 texture cannot express arbitrary simultaneous row contents or context-specific actions on its own. P9/P10 prove guest draw/label routes; no new host overlay is authorized by this plan. |
| **D14** | **Selection applies immediately.** Each accepted choice changes the Controls row value and refreshes controller art on the next presented frame, including when selected with keyboard or mouse. Ordinary hints still follow real last-device activity. The selection neither fakes controller input nor changes the saved device state. The owner removed the dedicated preview page. |

## 4. The asset contract — what to draw

### 4.1 Files and restrictions

| File | Required | Destination | Source format | Rules |
| --- | --- | --- | --- | --- |
| `scheme.toml` | yes | — | TOML | `schema_version = 1`; id, name, kind; file paths relative to the scheme folder |
| `buttons/<slot-id>.png` | any subset | that ID's rectangle in `slots.toml`, inside a 512×512 sheet | PNG, 8-bit RGBA, sRGB, straight alpha; variable dimensions | one image per supplied button slot; white/grey art is tinted by the game; no copied game art |
| `pads/<slot-id>.png` | any subset | 1024×1024 for each `xbox_0..3` / `ps_0..3` texture | PNG, 8-bit RGBA, sRGB, straight alpha; variable dimensions | optional image for that exact guest pad texture; other pads retain their original art |
| `pads/shared.png` | no | 1024×1024 for all eight pad textures | PNG, 8-bit RGBA, sRGB, straight alpha; variable dimensions | optional common image; explicit pad-slot images override it |
| `icons.png` | no | **1024×512** | PNG, 8-bit RGBA, sRGB, straight alpha | optional whole `blitz_icons` sheet; if absent, the retail sheet is used; this sheet has no measured per-glyph contract yet |
| anything else | no | — | — | the loader ignores it; the validator reports it |

Paths above are examples, not implicit discovery: every supplied image is named by its stable slot
ID in `scheme.toml`. An omitted manifest key means intentional base-game fallback for that slot.
At least one button or pad replacement image must be declared; `icons.png` alone is insufficient.
An absent file named in the manifest, an unknown
ID, a malformed PNG or a duplicate path rejects the package; there is no silent fallback for a
broken declaration. Packages do not include a copy of the base game's button sheet or pad art.

For a declared image of source size `W×H` and destination size `w×h`, resize uniformly by
`min(w/W, h/H)`, round the resulting dimensions to fit within the target (at least one pixel per
axis), and center it on a transparent `w×h` canvas. Preserve aspect ratio; never crop, stretch or
repeat. Thus a 128×128 image for a 32×32 target becomes 32×32, and a 64×50 image becomes 32×25
with transparent space above and below. The actual button target is the measured rectangle in
`slots.toml` (for example, A is 81×81), not a universal 32×32. The whole source canvas, including
any transparent margins, participates in scaling; authors should trim unintended margins. A
declared button image **replaces** its rectangle on a composite made from the user's own game
texture; its old pixels do not show through transparent parts of the new image. Clear that
glyph's 85-pixel atlas cell before fitting the image into its measured rectangle: the Xbox 360
A art has visible pixels at rows 81–82, just beyond its UV rectangle's last row 80. Preserve original
BC3 blocks where no cleared cell intersects them so omitted art is not degraded by an
unnecessary recompression. A declared pad image uses the same fit/center rule on a 1024×1024
transparent canvas; an omitted pad texture is not overridden at all. When buttons are supplied,
compose and encode the complete button BC3 texture/mips in memory or user-local cache only;
never ship or commit a
composite containing game pixels. Verify untouched rectangles and pad textures against the source.

Provisional limits (P2 may tighten with measurement): ≤ 8 MB and ≤ 2048×2048 per source PNG,
≤ 64 files per scheme, no symlinks, id `[a-z0-9][a-z0-9-]{0,31}`, display name ≤ 20 characters.
The 28-character single-line budget fits `Layout: <name>` with the 20-character value cap.
The owner chose that shorter direct row instead of a separate page.

Reserved ids: `retail` (base-game Xbox 360; no folder), `keyboard` (internal built-in provider,
not a package), and `ps3` (the installer's local import). Every other valid package id is
discoverable without a code list. A user package may shadow an installed authored layout of the
same id; it cannot replace the base-game choice, keyboard/mouse provider, or PS3 import.

### 4.2 The slot map

The retail sheet is a **6×6 grid**. The 360 sheet has 30 occupied cells; the PS3 sheet decodes on
the same scene grid with different art. P0 compared both font variants across the 360 and PS3
scenes; corresponding `Glyph` tables match. The measured grid is:

| Row (y) | Occupied columns (x) |
| --- | --- |
| 0 (≈0–84) | 0, 1, 2, 3 (four; columns 4–5 empty) |
| 1 (≈85–169) | 0–5 |
| 2 (≈170–254) | 0–5 |
| 3 (≈255–339) | 0–5 |
| 4 (≈340–424) | 0–5 |
| 5 (≈425–511) | 0, 1 (columns 2–5 empty) |

The retail slot IDs identify glyphs, **not the physical sources currently bound to an action**.
`assets/controller/slots.toml` records all 32 table rectangles, including the two empty small
glyphs in row 0. The corrected pixels identify A/B/X/Y, LT/LB/RT/RB, D-pad directions,
LS/RS and two horizontal arrows. The remaining instrument and stick-direction icons retain
character-based IDs until their action semantics are measured. This sheet has no standalone
Start or Back picture; P8/P10 must resolve those actions without assuming one exists. The
blank template draws each rectangle and label as a reference; authors save independent PNGs,
not a painted copy of the template sheet.

P8 separates this **logical action-slot map** from a **physical-source icon catalog**. A selected
controller sheet supplies physical controller icons; the resolver can draw Y for logical Confirm
without changing what Confirm does. P10 may compose action-slot textures for compatible guest
paths, but per-widget/context resolution is required wherever one shared sheet is ambiguous.

Keyboard/mouse icons are a built-in catalog or keycap renderer, not
`schemes/keyboard/buttons.png`. It covers every key, mouse button, wheel direction and other
source the current binding system accepts. Keycap legends such as `Enter` are inside rendered
images; plain binding-name strings are not the visual replacement. Unknown/unbound sources get
explicit neutral images and diagnostics, not misleading controller defaults.

P10 audits inline `<alt1>…</alt1>` prompts, footers, graphical hints, and locale strings that spell
an actionable button name. The earlier exclusion of textual button hints is superseded: all
actionable hints must resolve through the effective context, with any unmeasured route recorded
as open rather than declared complete. Decorative controller art is not an action hint.

### 4.3 `scheme.toml`

```toml
schema_version = 1
id = "xbox-series"
name = "Xbox Series"      # ≤ 20 characters; this is the row's value
kind = "controller"       # the only discoverable package kind

[buttons]
confirm_a = "buttons/a.png"
# back_b = "buttons/b.png"
# Omit every button glyph that should retain its base-game art.

# [controller]
# file = "pads/shared.png"  # Optional: fills all eight pad textures.

# [controller.slots]
# xbox_0 = "pads/xbox_0.png"  # Optional: takes precedence over shared.
# ps_0 = "pads/ps_0.png"   # Each other pad slot is independent.

# [icons]
# file = "icons.png"       # Optional whole extra-icon sheet.
```

The sample may omit `[controller]`, `[controller.slots]` or `[icons]` entirely. A button-only or
pad-only controller package is valid. `kind = "keyboard"` is rejected for packages;
keyboard/mouse resources are built with the application independently. P2 must make this exact
manifest form, key precedence and absent-versus-invalid behavior executable and tested.

### 4.4 The authoring kit (P6a, not blocked on the owner's art)

- `assets/controller/README.md` — a user-facing, step-by-step custom-template/package guide:
  accepted PNG format and size limits; slot IDs and their measured destination sizes; proportional
  fit, centering and transparent padding with numerical examples; tint/alpha behavior; manifest
  syntax and paths; button/pad omission and original-art fallback; validation, installation and
  next-boot discovery; licensing/no-game-art rule; and a minimal working package.
- `assets/controller/template/buttons.png` — a blank 6×6 grid with the slot rectangles and labels,
  generated by the P0 tool. It is a placement reference, not a required package asset; it contains
  **no retail art** and may be committed.
- `assets/controller/template/pad.png` — a blank 1024×1024 guide with the safe area marked.
- `assets/controller/template-360/` — a gitignored local reference generated from the
  owner's Xbox 360 scenes by `python scripts/export_template_360.py`; P6a updates its
  package-shaped `scheme.toml` and individual art exports to the P2 contract. `retail`
  remains the built-in choice with no package. The reference is never installed or committed;
  the blank `template/` remains the distributable guide.
- `assets/controller/template-ps3/` — the equivalent gitignored reference generated
  from the owner's PS3 scenes by `python scripts/export_template_ps3.py`; P6a updates it to
  individual PS3 controller glyphs, optional pad images and a package-shaped manifest using
  reserved id `ps3`. It is never installed or committed automatically.
- `assets/controller/slots.toml` — the tracked slot registry (id, rectangle, row/column, label).
- `assets/controller/schemes/<id>/` — installable authored schemes, discovered by folder rather
  than an ID list; the five named families live here when P6b runs.
- `assets/controller/examples/` — original example packages for the guide, outside the installer
  source tree.
- `scripts/controller_schemes.ps1 -Validate -Pack` — validates every scheme (format, bounded source
  dimensions, known slot IDs, declared paths, fit results, id rules, duplicate ids) and copies the
  valid ones into the install layout.
- A generated **`test-markers`** scheme (not shipped) with a unique solid color per slot, used by
  P1–P4 before real art exists.

### 4.5 Where packages live at runtime

| Root | Contents | Who writes it |
| --- | --- | --- |
| `<install>/assets/controller/schemes/<id>/` | every validated authored scheme plus locally imported PS3, when available | the installer/build |
| `<user_data_root>/controller_schemes/<id>/` | user packages, including a scheme the player drops in | the player |

`<user_data_root>` is the same root the game already uses for saves and `settings.toml`
(`src/rb_blitz_app.h`). For non-reserved custom ids, a user package shadows an install-root package and logs
`button_schemes: <id> shadowed by <path>`; a package that fails validation is skipped and logged with
the reason.

### 4.6 Controller catalog

| Display name | Stable token | Source / availability |
| --- | --- | --- |
| Xbox 360 | `retail` | Always available; default, original base-game art, no copied package |
| PlayStation 3 | `ps3` | Available after local PS3 import (P5); never bundled |
| Xbox Series | `xbox-series` | Owner-authored controller package |
| PlayStation Series | `playstation-series` | Owner-authored controller package; replaces the earlier planned `PS5` name/token |
| Nintendo Switch | `nintendo-switch` | Owner-authored controller package |
| Steam Controller | `steam-controller` | Owner-authored controller package |
| Steam Deck | `steam-deck` | Owner-authored controller package |

These seven are the target catalog, not evidence that art exists today. P6b requires the five
authored controller packages; PS3 remains conditional on import. Keyboard/mouse is never a choice.
P8 records coverage for each physical source supported by the mapper, including Steam/Deck
controls: art availability does not imply new input-driver support. A control the input layer
cannot bind is recorded separately from a missing icon.

## 5. The flows

### 5.1 Installer — optional PS3 import

The wizard gains one optional page after the Xbox 360 game-data page, **Controller Icons (optional)**:

1. `Skip PlayStation 3 art` (default).
2. `Use a PlayStation 3 package (.pkg)` — a path box and Browse, plus an optional `.rap` box shown
   when the package needs a licence file.
3. `Use an extracted PlayStation 3 game folder` — a path box and Browse (the folder that contains
   `gen/main_ps3.hdr`).

On Next the helper runs `probe-ps3` (read-only) so a wrong source fails on the page, not halfway
through the install. During the Progress stage `install-ps3-scheme` writes
`<install>/assets/controller/schemes/ps3/` with `scheme.toml`, individual controller glyph PNGs
and the `ps_0..3` pad PNGs, and records the source in `install-manifest.toml`
(`[controller_scheme]` table: id, source,
content id, files, bytes, sha256).

The extraction, exactly:

1. **Probe** the PKG header. The file supplied on 2026-10-19 measures 364,734,448 bytes with magic
   `7F 50 4B 47` and content id `UP8802-NPUB30749_00-RBBLITZ0000TRIAL` at `0x30`. The same folder
   has `UP8802-NPUB30749_00-RBBLITZ00000FULL.rap`; the trial package and the full-licence RAP do not
   share a content id, so P5 must either pair the matching RAP or use the folder route. This is
   **unproven** until P5 runs; the page must say which of the two it is doing.
2. **Decrypt** the PKG with Windows CNG (`BCryptDecrypt`, AES-CBC) using only key material derived
   from the user's own package and licence file (the `.rap`); no key is compiled into the helper.
   The extracted-folder route bypasses this step entirely.
3. **Read the PS3 ARK index** (`gen/main_ps3.hdr`) and extract only
   `ui/resource/fonts/gen/buttons.milo_ps3` from `gen/main_ps3_0.ark` by its offset and size. The
   full 350 MB ARK is never written.
4. **Decode** the milo chunk: find the 23-byte header and eight-byte preamble, take the
   512×512 DXT5 payload at header + 31, decode PS3 blocks directly to RGBA, and crop the
   known controller glyph rectangles from `slots.toml` to individual PNGs. Do not generate a
   distributable full sheet or replace instrument glyphs just because they occupy the same sheet.
5. **Extract the pad art from the 360 scene** (`ui/controller_config/gen/controller_config.milo_xbox`,
   already installed): decode the four `ps_*` 1024×1024 DXT5 images and write `pads/ps_0..3.png`. The
   `ps_*` family is already in the 360 dump, which the project already measured
   ([assets.md](../../guides/assets.md)); the PS3 package is not the source of the pad art.
6. **Validate** the generated scheme with the same validator the custom-package flow uses, then
   write it.

If the package cannot be decrypted, if the content id is not Rock Band Blitz, or if the sheet does
not decode, the helper refuses with a one-line reason and the wizard tells the player to use the
extracted-folder route. The PS3 scheme is **never** written into the repository or the release.

### 5.2 Custom package flow

1. The player follows `assets/controller/README.md` and drops
   `<user_data_root>/controller_schemes/<id>/` with `scheme.toml` and at least one declared PNG.
2. The next boot scans both roots, validates each package, logs one line per accepted/rejected
   scheme, and publishes the list to the settings screen.
3. The Controls row cycles through accepted controller schemes. Selecting one writes `controller.scheme` and
   applies it live.

New packages are not discovered mid-session.
The installer's built-ins are present before the first boot.

### 5.3 In-game selection and hot swap

The Controls page gains one direct select row (`Layout: <name>`, four rows total):

```
Controls
  Input Types
  Interface Mapping
  Gameplay Mapping
  Layout: Xbox 360      <- cycles through valid schemes
```

The `Layout` choices are built from the boot's registry and inserted through
`MenuConfiguration.choices`; `Xbox 360` (`retail`) is always present and is the compiled default. A custom
scheme's `name` is the value, capped at 20 characters.

Hot apply:

- The setting is a native descriptor with `ApplicationStrategy::kHot` **only after its own live
  measurement in P3**; until then the plan does not let the row claim a live effect. The final
  acceptance requires the hot classification with evidence (`strategy_evidence` names the capture
  and log).
- `HotApplier::Apply(kButtonScheme)` publishes the new active controller scheme to the scheme
  runtime and asks the texture override provider to refresh the affected slots. The invalidation is
  queued to the GPU/UI thread, not performed on the guest thread.
- Each click or accepted selector change commits and refreshes controller art on the next
  presented frame. Rapid selections resolve to the latest committed choice.
- The row commits without a restart prompt; leaving Controls and returning shows the new selection.
- The retail **Controller Presets** page uses each supplied pad image when visible and its own
  original texture for omitted slots.
- If the selected package becomes invalid between boots, the boot falls back to `retail`, logs
  `button_schemes: saved scheme <id> is missing, using retail`, and the row shows `Xbox 360`.

### 5.4 Input-aware automatic switching

A small **last-device tracker** watches the raw input that the source gates already read:

- **Controller activity**: a button press, a trigger past its threshold, or a stick past
  `kStickThreshold`, from `SamplePhysicalPad` — never from the SDK's synthetic keyboard-to-pad bits.
- **Keyboard/mouse activity**: a real key-down event, a mouse button, or cumulative mouse motion
  past a small threshold (to ignore a bumped desk), while the window has focus. Mouse motion is
  observed on the existing window/UI event path, not inferred from a synthesized pad.
- A switch is published when the device changes; a short debounce (target 50–150 ms; measured in P4)
  absorbs pad noise. The tracker does not switch on focus loss, and a switch does not change the
  selected controller scheme.
- Ordinary action hints use the built-in keyboard/mouse provider when that is the last active
  family, otherwise the selected controller family, resolving current bindings in either case.
  They start in keyboard/mouse mode. The active **pad** art and explicit Controller Layout preview
  always show the selected controller scheme. There is no timer that forces controller hints
  over keyboard/mouse after selecting a layout.
- Every switch logs `button_schemes: active <id> (device controller|keyboard)`, which is the
  acceptance hook.

### 5.5 Mapping rows and action hints

1. On either mapping surface, retain the action label and draw **all effective bound sources** as
   adjacent images in their stored order, including mixed controller/key sources on interface
   rows. Gameplay's device-specific pages show that page's sources. The row's device scope, not
   last input, determines which bindings are listed. Bound key/mouse icons stay built in when
   the controller layout changes.
2. In ordinary interface hints, filter the effective Interface Mapping to the active family and
   draw its first bound source. Example: controller `[Y, A]`, keyboard/mouse `[Enter, Space]` for
   Confirm draws Y on controller input and Enter on keyboard/mouse input. The mapping row still
   exposes every binding. Preserve stored order; do not sort by display name or button number.
3. Treat keyboard and mouse as one hint family: their combined binding order decides which icon
   is first. If that family has no binding, use an explicit unbound image instead of advertising
   an input that will not perform the action.
4. Interface Mapping controls menus, footers and pause interface hints. Gameplay Mapping and the
   active retail preset control in-song action hints. P8/P10 inspect the existing context rules;
   a gameplay A icon and an interface Confirm icon must not accidentally share a remapped value.
5. Assign, remove, reorder, reset, or switch profile/preset → publish a new effective-binding
   snapshot → refresh visible rows and action hints on the next presented frame. Never feed
   resolved icon identities back into input dispatch.

### 5.6 Acceptance authorization

These are future implementation/acceptance steps, not authorization to drive the game now.
Before any manual or automated game/UI navigation, ask whether the owner wants to perform the
test themselves and wait for the answer, unless they already authorized autonomous UI testing
for that scope. Build with `cmake --build --preset win-amd64-release` before requesting a test
unless the current binary is known to contain the latest changes. Code inspection, host-only
tests, and builds can proceed independently. New host overlays need explicit owner approval;
guest-authored rows, preview widgets and labels follow the authorized guest route.

## 6. Architecture

### 6.1 Modules and owners

| Area | File(s) | Responsibility |
| --- | --- | --- |
| Scheme registry / manifest / images | `src/controller_schemes/{registry,manifest,image,slots}.{h,cpp}` | SDK-free parsing, validation, RGBA loading, slot table; testable without a boot. The installer compiles the same manifest/slot code where it overlaps. |
| Image conversion already available | `src/assets/texture_codec.{h,cpp}`, `scripts/hmx_tex.py`; WIC use in `src/store/album_art_service.cpp` | Reuse DXT decode/word swap and RGBA conversion. The standalone texture envelope and store art serving path are not the scene-record parser or the live override. |
| Slot map / template tool | `scripts/hmx_milo.py` extension + `scripts/controller_schemes.ps1` | P0/P2 tooling: locate a named scene payload and its `Glyph` table, reuse the existing DXT decoder to export it, emit rectangles, validate a package, build the template. |
| Active scheme runtime | `src/hooks/button_schemes.{h,cpp}` | Holds selected controller layout, last input family, explicit preview state and binding generation; publishes immutable snapshots; logs; owns the registry instance. |
| Input-icon resolver | proposed `src/input/input_icons.{h,cpp}` + existing `src/profile/input_rows.{h,cpp}` / effective binding APIs | P8: ordered physical-source icons for interface/gameplay contexts, first source per hint family, preset inheritance, unbound behavior; built-in keyboard/mouse provider independent of controller packages. |
| Last-device tracker | `src/input/last_device.{h,cpp}` | Raw pad/keyboard/mouse activity, debounce, atomic `LastDevice`; called from the existing poll/event seams. |
| Texture override provider | `src/assets/texture_override.{h,cpp}` + `patches/00xx-texture-override.patch` | Registers a provider with the SDK cache; maps a guest texture fingerprint to a slot; supplies RGBA; invalidates affected textures on a generation bump. The existing `ImGuiDrawer::CreateImageTexture` path only draws in the music-player overlay. |
| Settings | `src/settings/model.h`, `schema.cpp`, `boot.cpp`, `hot_apply_runtime.cpp`, `cvars.cpp` | `SettingId::kButtonScheme`, the descriptor, the boot catalog, the `kHot` applier. |
| Screen | `src/ui/settings_screen.{h,cpp}` | Controller Layout label/select, automatic keyboard/mouse info and guest controller preview; P9 image binding rows; `MenuConfiguration.choices` drives the list. |
| Guest action hints | P10-measured guest draw/label hooks and served UI records (paths to be pinned by P10) | Contextual action identity, remap-aware footers/visual hints and refresh on binding/device/layout changes; no host overlay. |
| Tests | `tests/button_scheme_tests.cpp`, `tests/settings_screen_tests.cpp`, `tests/settings_hot_apply_tests.cpp`, `tests/settings_schema_tests.cpp`, `tests/settings_service_tests.cpp`, `tests/source_gates_tests.cpp` (or a new input tracker test) | The invariants in §8. |
| Installer | `installer/src/{pkg,ark,milo_tex}.{h,cpp}`, `commands.cpp`, `setup.iss`, `install.cpp`, `config.h` | The optional page, probe/install commands, manifest table, refusal messages. |
| Assets | `assets/controller/**` | The contract, template, slot registry, owner schemes; built-ins copied by the installer. |
| Acceptance | `scripts/acceptance_button_schemes.ps1`, `scripts/acceptance_ps3_import.ps1` | §8. |

### 6.2 The SDK texture override (the one SDK patch)

The patch is deliberately **generic and upstreamable**: "a host provider may replace the data of a
guest texture at load time". It does not know about Rock Band Blitz.

P1's interface, abbreviated:

```cpp
namespace rex::graphics {

struct TextureOverrideRequest {
  // Width, height, mip count, format, guest base address/size, tiling, endianness.
};

class TextureOverrideProvider {
 public:
  // Returns a complete canonical BC3 mip chain for a matched texture.
  // The guest base bytes are for identification, not a ready RGBA canvas.
  virtual bool GetOverride(const TextureOverrideRequest& request,
                           const uint8_t* guest_data,
                           std::vector<std::vector<uint8_t>>* host_levels,
                           bool* refresh_target) = 0;
};

// The provider is process-global because the cache is created by the GPU plugin.
void SetTextureOverrideProvider(TextureOverrideProvider* provider);
// Bumps the generation; the cache drops/reloads the textures whose overrides changed.
void InvalidateTextureOverrides();

}  // namespace rex::graphics
```

Why this works for both backends: the base `TextureCache` owns identification and invalidation;
each backend uploads the returned canonical BC3 levels into its existing compressed texture.
The SDK patch remains independent of Rock Band Blitz's slot IDs and manifest format.

Identification strategy for P2 onward, in order of preference:

1. **Content fingerprint**: hash the guest base data (or a stable 4 KB sample) and match a table of
   measured fingerprints for the button sheet and the eight pad images. The table is recorded by
   the P1 trace, not embedded in source as game data; it is a fingerprint file under
   `config/` or a small generated header, like the existing game fingerprints.
2. **Scene-read order**: hook the VFS read of `buttons.milo_xbox` / `controller_config.milo_xbox`,
   set a one-shot expectation, and match the next texture load with the expected dimensions. This
   handles a future build whose pixels differ.
3. **Descriptor match**: hook the guest texture-load function where the 23-byte record and the
   source path are still in guest memory; this is the most precise but the most invasive, and is
   only chosen if 1 and 2 both fail.

Invalidation uses the provider's generation counter. P1 proved narrow candidate invalidation and
full-mip reload on D3D12 and Vulkan. P2 must mark each identified button/pad texture as a
candidate, return no replacement for omitted pads, and rebuild the composed button texture
when the selected scheme changes. Measure the cost again with real partial packages.

### 6.3 Threading

- The active scheme is an immutable snapshot published by the settings commit / input tracker and
  read by the GPU thread. No lock is held across a draw.
- Effective bindings, context and controller layout share a versioned icon snapshot. Mapping rows,
  ordinary hints and the controller preview must not combine stale bindings with a new layout.
- The registry and image decoding are host-thread work; the decoded RGBA is uploaded on the GPU
  thread through the provider.
- The settings commit follows the existing rule: a guest thread never touches the cvar registry;
  the hot applier schedules the host write.
- The override invalidation is a queued GPU/UI-thread action, not a call from the guest thread.

## 7. Packages and prompts

Dependency graph (P8–P10 are new scope; ids are appended rather than renumbered):

```mermaid
graph TD
  P0[P0 format + slot map] --> P1[P1 texture override]
  P0 --> P2[P2 package + registry]
  P1 --> P3[P3 settings + hot apply]
  P2 --> P3
  P2 --> P8[P8 resolver + built-in key/mouse icons]
  P8 --> P3
  P2 --> P4[P4 input-aware switch]
  P1 --> P4
  P8 --> P4
  P3 --> P4
  P8 --> P9[P9 mapping-row images]
  P1 --> P9
  P3 --> P9
  P9 --> P10[P10 remap-aware guest hints]
  P4 --> P10
  P0 --> P5[P5 PS3 import]
  P2 --> P5
  P2 --> P6a[P6a authoring kit]
  P3 --> P6b[P6b owner schemes]
  P4 --> P6b
  P5 --> P6b
  P6a --> P6b
  P10 --> P6b
  P6b --> P7[P7 docs + close]
```

> **The only package blocked on the owner's manual controller art is P6b.** P0–P5, P6a and P8–P10
> use retail scenes, generated controller markers, built-in key/mouse icons and synthetic bindings;
> P5 separately requires the owner's PS3 input files. **Do not start P6b until all five authored
> controller folders contain real art.** P6a produces the template and validator; P0 produces
> the slot map the template is drawn from. The PS3 scheme (P5) is generated from the user's own
> package and is not part of P6b.

Suggested execution: P0 → P1/P2 → P8 → P3 → P4/P9 → P10 → P6b → P7, with P5 and P6a
following their own dependencies. P7 closes only after the new prompts and real-art acceptance
pass; no static keyboard-sheet exception survives as an accepted limitation.

### P0 — Scene texture decode, slot map and template

**Delivered 2026-10-26.** `hmx_milo.py texture` exports the measured DXT5 first mip
for buttons, blitz icons and controller-config textures; `slots` compares both
34-entry font variants across the 360 and PS3 scenes and writes the 32 drawable
rectangles plus transparent guides. Both platform pairs match. The two font
variants swap the `(` and `*` character assignments between two cells, recorded
as aliases in `slots.toml`. Decodes are in the ignored `out/button-schemes/p0/`.
The 2026-10-26 visual review caught an eight-byte preamble omitted from the
2026-10-19 offset. The earlier PNGs are misaligned and superseded; clean 360
exports are also in the gitignored `assets/controller/template-360/`.

**Goal.** Turn the 2026-10-19 format measurement into a repeatable tool and the authoring contract.

**Deliverable.**
- Extend `scripts/hmx_milo.py` with a `texture`/`export` command that finds a scene's 23-byte
  header and eight-byte preamble, then identifies its DXT5 payload. Reuse the existing word swap and DXT decoder in
  `scripts/hmx_tex.py` (or the C++ codec) to write a PNG; the same path handles the 512×512 sheet
  and the 1024×1024 pads. Do not implement another block codec.
- Read the `Glyph` table (the object records around the texture) and emit
  `assets/controller/slots.toml` with every slot id and rectangle; generate the blank
  `assets/controller/template/buttons.png` and `pad.png` guides.
- Add the measured format record to `docs/guides/assets.md` (a new section, dated 2026-10-19),
  mark the old "Can a UI texture be swapped today? **No**" claim as superseded, and name this plan
  as the successor. Move the format item in `docs/reference/backlog.md` to resolved/superseded.
- Copy the evidence into `out/button-schemes/format-2026-10-19/` (already done) and cite it.
- **Already landed, reuse it instead of starting over (2026-10-21):**
  `src/assets/texture_codec.{h,cpp}` has the standalone texture envelope, DXT1/DXT5 decode and
  encode, 16-bit word swap and RGBA resampling, pinned by `tests/store_art_tests.cpp`. The
  RhythmVerse art worker uses it for image-to-game conversion; the music player uses
  `DecodePlatformTexture` for game-cover-to-overlay conversion. For P5, use `DecodeDxt5`
  directly on the PS3 scene payload, and `SwapSixteenBitWords` plus `DecodeDxt5` on the
  Xbox pad scene payload, then write individual controller glyph and pad PNGs from RGBA for the
  revised P5 contract. `BuildPlatformTexture` writes a standalone `.png_xbox` / `.png_ps3`
  container, not a
  PNG, and does not parse the 23-byte scene record. `scripts/hmx_tex.py` provides the same block
  decode and PNG output offline; P0 still has to locate the scene payload and `Glyph` table.
- **Keep the DXT5 runtime question separate (measured 2026-10-21).** The codec's DXT5 output
  decodes correctly offline, but a newly encoded DXT5 standalone cover drew with wrong colours in
  the current game path (`docs/guides/assets.md`, "which formats this build actually draws"). That
  result does not prove the planned RGBA cache override works or fails. Preserve the scheme PNGs'
  straight alpha; do not force the transparent button/pad art to opaque DXT1. P1 must measure the
  actual override upload on both backends.

**Verify.** Re-running the command on the 360 and PS3 `buttons.milo_*` reproduces the two decoded
sheets at the same 6×6 grid (the 360 sheet's 30 occupied cells and the PS3 sheet's own art); the two
scenes' `Glyph` tables are compared and either match slot for slot or the mismatch is recorded as a
finding; the pad command reproduces the eight pad images; every slot rectangle lies inside 512×512
and does not overlap; the template has no retail art.

**Don't.** Do not commit the decoded retail/PS3 art. Do not generalize the 23-byte record to every
scene without testing `blitz_icons` and `controller_config`. Do not claim the DXT5 hypothesis is
proven for a scene that was not decoded.

### P1 — Texture override seam (SDK patch) and marker spike

**Status (2026-10-27): P1 complete on D3D12 and Vulkan.** Owner runs prove
visible button and pad replacement, full pad mip coverage, and live restoration
in both directions on each backend. Frame traces place both reloads before the
first post-switch `XE_SWAP` on each backend and record the observed intervals.
The noisy trace and manual flag toggle do not isolate GPU upload cost from
other frame work.

**Implementation checkpoint (2026-10-27):** `0043-texture-override.patch` and the
internal `RB_BLITZ_P1_*` probe compile with D3D12 and Vulkan. The provider returns
canonical BC3 blocks encoded from generated RGBA marker art, rather than RGBA
directly: both current backends retain a BC3 host image for these DXT5 textures,
so an RGBA upload would require changing their image/view formats. The guest
bytes and scenes remain untouched. The candidate trace below identifies likely
button and pad hashes. The marker and switch runs below supply the live
evidence that was pending at this checkpoint.

**Owner trace (2026-10-27):** `out/button-schemes/p1/d3d12-20261027-183250.log`
completed with the trace enabled and no overrides selected. Its only 512×512
BC3 candidate is `3F044908A62A89AD` (#5), the likely button sheet. At the
Controller Presets visit, 1024×1024 candidate `06FFB32889EA3A64` (#32)
appeared with two 64×128 arrow textures (#33–34), making it the likely first
pad slot; three further 1024×1024 candidates (#35–37) appeared seconds later.
Those fingerprints were hypotheses until the marker run below. The trace
has no replacement or generation timing evidence because no hashes were supplied.

**Owner marker run (2026-10-27):** `out/button-schemes/p1/d3d12-20261027-184046.log`
confirms replacements for `3F044908A62A89AD` (512×512 button sheet) and
`06FFB32889EA3A64` (1024×1024 first pad slot). Owner screenshots show the
probe's deliberate white three-stripe glyph in menu prompts and a magenta
rectangle over the controller preset art. The underlying art remains visible
in that screenshot; the reason has not been isolated. This run proves the
selected texture draws but does not yet establish a clean standalone pad image. No marker-switch
event or `Texture override generation` timing line occurred in the log, so
generation cost and retail restoration still need a live check.

**Full mip correction (2026-10-27, awaiting owner retest):** The next owner
capture (`out/button-schemes/p1/d3d12-20261027-184953.log`, recovered from the
build log) used the empty DLC roots: the scan reported zero packages. It again
showed the magenta rectangle mixed with the retail pad art. The previous P1
override uploaded BC3 level 0 while leaving guest mips in the host image, so
minification could blend both. `0043-texture-override.patch` now accepts and
uploads a complete BC3 mip chain on D3D12 and Vulkan; invalidation also reloads
both base and mip levels when retail art is restored. The release build and SDK
patch replay audit pass. A new owner screenshot and switch timing are still
needed at this checkpoint to verify the visual result and finish P1 acceptance.

**Owner retest (2026-10-27):** A screenshot from the rebuilt D3D12 probe now
shows a solid magenta 1024×1024 pad region with no retail controller art
bleeding through. `out/button-schemes/p1/d3d12-20261027-190406.log` confirms
the button sheet was replaced at one level and the pad at seven levels; the
empty DLC roots yielded a cache hit with zero packages. This confirms the
full-mip correction visually. That log had no marker-switch or generation
timing event; the later run below supplies them.

**Owner live switch (2026-10-27):** `out/button-schemes/p1/d3d12-20261027-191202.log`
records `marker switch off` at 19:12:49.592 and `marker switch on` at
19:13:10.849. Each generation invalidated the two matching texture candidates;
the texture-request CPU portion logged 6 µs and 5 µs respectively. The button
and pad replacement requests on the return to markers were logged 68 and 70 ms
after the switch event. The owner confirmed that retail art returned and that
both markers returned after removing the flag. The boot used the empty DLC roots
and loaded zero DLC packages. This establishes the live D3D12 override and
restoration without a guest scene edit. The log does not provide a rendered
frame-time sample or prove the one-frame presentation bound; Vulkan is compiled
but has no live result yet.

The acceptance script now has `-MeasureFrames`, which enables the SDK's
`[VdSwap]` and `[XE_SWAP]` interval trace for an owner-run switch. This is
diagnostic mode only; normal P1 probe runs keep the quieter log settings.

**Frame trace acceptance (2026-10-27):** The owner repeated the off/on switch
with `-MeasureFrames`; evidence is
`out/button-schemes/p1/d3d12-20261027-191829.log`. On switch-off at
19:19:28.851, the cache invalidated both candidates and loaded the retail
1024×1024/7-mip pad at 19:19:28.858 and 512×512/1-mip button sheet at
19:19:28.858, before the next `XE_SWAP` (#2746) at 19:19:28.862. On
switch-on at 19:19:34.979, both marker replacements loaded at
19:19:34.986–.989 before the next `XE_SWAP` (#2936) at 19:19:34.994.
Those two first post-switch `XE_SWAP` intervals were 13,091 µs and 15,398 µs;
the corresponding guest `VdSwap` intervals were 12,472 µs and 12,677 µs.
Candidate invalidation logged 5 µs for each change. This trace mode is noisy
and its intervals are observations, not an isolated GPU cost measurement; the
switch frames showed no spike relative to nearby frames. Only the two selected
hashes were replaced, and the owner confirmed the visible state in both
directions. The D3D12 one-frame request bound is satisfied.

**Vulkan live acceptance (2026-10-27):** The owner reported that the same
marker and retail toggle looked correct on Vulkan. Evidence is
`out/button-schemes/p1/vulkan-20261027-192321.log`: the button and pad hashes
were replaced at one and seven mip levels respectively, then both candidate
textures were invalidated on switch-off at 19:28:59.891 and switch-on at
19:29:09.428. The retail pad and button levels loaded by 19:28:59.893, before
the next `XE_SWAP` (#5502) at 19:28:59.894. Both marker levels loaded by
19:29:09.433, before the next `XE_SWAP` (#6022) at 19:29:09.448. Candidate
invalidation cost 5 and 6 µs. The first post-switch intervals were 183,012 µs
off and 21,083 µs on; nearby 60-frame pre-switch medians were 16,574 and
16,667 µs. The 183 ms interval includes about 180 ms before the off event, so
it cannot be attributed to the texture reload. This boot used empty DLC roots
with zero packages, reached normal execution completion, and logged no GPU
error. The Vulkan one-frame request bound and visible switching are accepted.

The owner-run probe is `scripts/acceptance_button_schemes_p1.ps1`. Run it first
without hashes, visit the menu prompt band and Controller Presets through the route above, close the game,
and inspect the copied `out/button-schemes/p1/d3d12-*.log` candidate lines. Once
the two slot hashes are identified from that trace, rerun with `-ButtonHash HEX
-PadHash HEX -Switchable`; creating/removing the printed `markers-off.flag`
toggles retail/marker art in the running game. Capture both states, note the
`Texture override generation` CPU timing line and frame timing, then repeat with
`-Backend vulkan` if that backend can run. The script launches the game but leaves
all navigation and screenshots to the owner. It pins `--dlc_root` and
`--dlc_library` to its empty `out/button-schemes/p1/empty-dlc` directory so
subsequent probes do not scan the owner's DLC library.

**Goal.** Prove a host texture can replace the button sheet and one pad slot in a live boot without
touching the guest scene, and measure the cost of switching it.

The store's `.png_xbox` serving path and the music player's `CreateImageTexture` upload are working
examples of conversion and rendering, but neither replaces an already loaded, scene-embedded guest
texture. P1 starts at the guest texture identity, upload and invalidation seam; it does not rebuild
the DXT codec or duplicate the overlay renderer.

**Deliverable.**
- The SDK patch under `patches/` (with `patches/README.md`), applied by
  `scripts/apply_sdk_patches.ps1`; a generic `TextureOverrideProvider` as in §6.2; both D3D12 and
  Vulkan backends compile.
- `src/assets/texture_override.{h,cpp}`: the project provider, fingerprint table and generation
  invalidation.
- A spike that replaces the button sheet with the generated `test-markers` sheet and one pad image
  with a solid marker; it is enabled by an internal cvar/env (not the final settings row) so the
  spike can be run before P3.
- A trace that identifies the slot textures (dimensions + fingerprints + scene-read order) and
  records the switch cost.

**Verify.** A boot on the menu shows the marker glyphs on the prompt band and the marker pad on the
retail Controller Presets page; a second boot without the spike shows retail art. A scheme change
replaces both within one frame after the generation bump; the frame cost is measured and recorded.
The untouched textures are unchanged (hash/screens). At least one backend is measured; the other is
recorded as unproven if it cannot be run.

**Don't.** Do not read or write game pixels from the repo. Do not hardcode guest addresses. Do not
fall back to editing the guest scene and call it a hot swap.

### P2 — Scheme package, registry and validator

**Delivered and owner-accepted on D3D12 (2026-10-27).** `src/controller_schemes/` parses and validates the
individual-slot manifest, decodes straight-alpha RGBA PNGs with WIC, fits each image into its
measured rectangle, preserves untouched canonical BC3 blocks, and composes full pad mip chains.
The boot scans install and user roots, logs its catalog/rejections, and the P1 provider routes the
identified button and eight pad fingerprints to the selected package. Until P3 supplies the
setting, normal boots select `retail`; `RB_BLITZ_SCHEME` selects a package for P2 development
acceptance. `scripts/controller_schemes.ps1` validates and packs packages. The generated
`out/button-schemes/p2/fixtures/` has all-slot markers, one-glyph, one-pad and invalid packages;
it is gitignored and contains no game pixels. `ctest -R button_scheme` pins parser, fit,
original-block preservation, the tiled/endian inverse and all ten scene fingerprints. The
owner's first one-glyph screenshot exposed retail A pixels below the measured UV rectangle;
clearing the containing atlas cell fixed that artifact. The owner retest confirmed a clean magenta
A frame, an unchanged A prompt with the pad-only package, one magenta `xbox_1` controller-preset
image and original art for the other pad images. Logs
`out/button-schemes/p2/d3d12-one-glyph-20261027-202258.log` and
`out/button-schemes/p2/d3d12-one-pad-20261027-202327.log` record the button override at one mip,
the pad override at seven mips, successful catalog discovery, deliberate rejection of the invalid
fixture, 3,811 µs button composition and 111 ms boot-time pad preparation. The owner performed
the UI checks with `scripts/acceptance_button_schemes_p2.ps1`. P2 package behavior on Vulkan
remains unmeasured; P1 already measured the underlying override seam there.

**Goal.** Make a scheme a folder the game can validate and load, before any UI exists.

**Deliverable.**
- `src/controller_schemes/`: manifest parsing, id/name validation, image loading to RGBA8, the
  slot table, discovery from the two roots, duplicate/shadow rules, fallback ordering, and a
  `Summary()` for the boot log. Parse the §4.3 individual-slot manifest. Each absent button or pad
  key intentionally keeps the corresponding original slot; an explicitly declared broken file
  rejects the entire scheme. A button-only or pad-only scheme is valid if it has replacement art.
- Implement the §4.1 fit/center transform for variable-size PNGs using the exact `slots.toml`
  rectangles. Clear a supplied button rectangle before inserting its resized image so its
  transparent pixels do not reveal the prior glyph; preserve canonical original BC3 blocks
  unaffected by any replacement rectangle instead of recompressing the entire sheet. Test that
  unlisted art still decodes like the user's original sheet. Preserve original game art only in
  memory or a user-local cache, never in a distributable package. Supply complete BC3 mip chains through
  P1's provider. For each absent pad slot, let the SDK load that original guest texture unchanged;
  for a declared pad, use the shared image or its specific image with the documented precedence.
- Pin the base-game decode/composition route in host tests: the P1 callback's `guest_data` may be
  tiled and endian-swapped, so raw bytes are not a ready RGBA canvas. Reuse the measured scene
  decoder or a proven SDK conversion route; do not assume linear BC3 input. Generate the final
  texture before GPU-thread upload where practical, then invalidate only affected textures.
- The seven controller identities from §4.6, with Xbox 360 always present, PS3 gated on local
  import, and the five authored families listed only when valid. Keyboard/mouse is a separate
  built-in dependency, never a scheme choice or user-shadowable package.
- `scripts/controller_schemes.ps1 -Validate -Pack`: validates the owner tree and copies built-ins
  into the install layout; accepts a `-Scheme <dir>` for one package.
- Tests: valid/invalid manifests, reserved ids, duplicate ids, missing/extra declared files,
  unknown slots, zero/oversized dimensions, alpha/format requirement, one-slot packages,
  intentionally omitted instrument glyphs and pads, original-block/pixel preservation,
  old-art clearing
  inside a supplied transparent slot, 128×128→32×32 and 64×50→32×25 proportional fits,
  centering/transparent padding, shared-versus-specific pad precedence, fallback ordering,
  custom-id user shadowing, reserved-family protection, and rejection of `kind = "keyboard"`.
- The generated `test-markers` scheme and a deliberately invalid fixture for the tests.

**Verify.** `ctest -R button_scheme` green; the boot log lists the schemes and the rejected
packages. A valid non-native-size PNG is accepted and fitted; an oversized or corrupt PNG is
rejected with a reason. A one-glyph scheme visibly changes only that glyph while instrument
glyphs and every omitted pad remain the user's original art; supplying one pad changes only the
named texture. If a package supplies buttons, its composed button sheet has all required mip
levels and swaps live via P1; a pad-only package leaves the button texture untouched.

**Don't.** Do not let the registry depend on SDL, the GPU or the guest; it is a host-only module and
the installer compiles its pure parts.

### P3 — In-game selector, persistence and hot apply

**Delivered and owner-accepted on Vulkan, 2026-10-27.** The first live boot showed that package discovery ran
after menu authoring, freezing the selector at Xbox 360. The owner also replaced the extra layout
page with one `Layout: <name>` row directly on Controls. A second live test confirmed cycling but
showed unchanged A/B footer glyphs: while Xbox 360 was selected, the provider returned before
marking the original button sheet as a texture-cache refresh candidate. It now tracks matching
button, pad, and icon textures even on a retail boot. The rebuilt release and five focused host
tests pass. Owner screenshots show One Glyph changing A, Test Markers changing A/B, and Xbox 360
restoring retail art; `rb_blitz_397.log` records the button-sheet refresh and no pending restart.
The owner then confirmed One Pad draws the magenta `xbox_1` image on Freakish while Default keeps
original art; `rb_blitz_398.log` records the seven-mip pad override. After saving One Glyph and
relaunching, the start prompt still displayed the magenta A; `rb_blitz_399.log` records One Glyph
selected and its button sheet composed during boot. All three logs are in
`out/build/win-amd64-release/logs/`. The in-song selector has host-test coverage; no separate
in-song live capture was required for P3 acceptance.

**Goal.** The player can choose a controller layout in Help & Options and see it without a restart.

**Deliverable.**
- `SettingId::kButtonScheme` (`controller.scheme`, value kind enum/string token) with a descriptor,
  the boot's scheme catalog, and `MenuConfiguration.choices["controller.scheme"]` from the same
  catalog.
- One `Layout: <name>` selector directly on Controls (`Xbox 360` + valid controller schemes),
  with an in-song twin. There is no layout submenu or unrelated information row.
- The `kHot` applier that publishes the active controller scheme and invalidates the overrides,
  with `strategy_evidence` filled from P3's own live measurement. Until that measurement exists, the
  row is not allowed to claim `kHot`.
- Persistence through `settings.toml`; a missing/invalid saved token falls back to `retail` with a
  log line.
- Tests: generated Controls row order in both contexts, label uniqueness, help coverage, descriptor
  choices, commit and restart classification, the hot applier called once per accepted choice,
  and rapid choices applying the latest selection.

**Verify.** Controls shows `Layout: Xbox 360` and cycles through every valid package found at boot.
Each accepted selection changes the row and controller art on the next presented frame. No Apply,
page exit, or later pad input is needed. A one-glyph scheme
changes its glyph and retains its original pad; a pad-only scheme changes its pad on the retail
Controller Presets page and retains original button glyphs. Leaving shows no restart prompt; a new
boot restores the saved selection. Choosing Xbox 360 restores base-game controller art. The boot
log names the active scheme.

**Don't.** Do not add a dead row when no scheme is installed (`Xbox 360` is always valid). Do not
persist a list index or a label; the token is the wire value. Do not mark the row `kHot` before the
live effect is measured.

### P4 — Input-aware automatic switching

**Goal.** Controller movement selects the controller art; keyboard/mouse input selects the keyboard
art; the switch is immediate, stable and logged.

**Deliverable.**
- `src/input/last_device.{h,cpp}`: raw activity detection, thresholds, debounce, focus handling,
  no synthetic pad bits.
- Integration with `src/hooks/button_schemes.cpp` and P8's resolver: ordinary hints use built-in
  keyboard/mouse icons or the selected controller family with effective bindings; pad art and the
  explicit Controller Layout preview stay on the selected controller family.
- Built-in key/mouse provider available without any controller package; D8's device-safe fallback.
- Tests: the boot's initial keyboard state, key event, mouse button, mouse motion threshold, pad
  button/trigger/stick, focus loss, no switch on the SDK's synthesized pad bits, no flapping across
  alternating inputs, no missing-keyboard-package fallback to controller art, no selectable
  keyboard layout, and a switch that does not change the selected controller scheme or preview.
- Boot/acceptance log lines.

**Verify.** In a live boot, press a key or use the mouse → ordinary hints use built-in key/mouse icons; move/press
the pad → it changes back; alternating inputs does not flicker; the controller page still shows the
selected controller pad; the log names each active scheme. A physical pad arm is recorded manually
if the harness has no virtual pad.

**Don't.** Do not use the merged guest pad state or a `key_down` that includes synthesized input.
Do not switch on focus loss or on a single mouse-motion pixel. Do not call the texture cache from
the guest thread.

### P5 — Optional PS3 import in the installer

**Goal.** A player who owns the PS3 package gets a PS3 layout in the list, generated locally, with
only the needed art extracted.

**Deliverable.**
- The **Controller Icons (optional)** wizard page with Skip / `.pkg` (+ optional `.rap`) / extracted
  PS3 folder, and a read-only probe on Next.
- Helper commands `probe-ps3` and `install-ps3-scheme`; PKG header/probe, AES-CBC via Windows CNG,
  the PS3 ARK index reader, the single `buttons.milo_ps3` extraction, the DXT5 decoder, the 360
  `ps_*` pad extraction, and the scheme writer. The validator from P2 is reused.
- Installer manifest `[controller_scheme]` table; report lines; refusal messages for a wrong
  content id, a missing licence, a decryption failure and a decode failure.
- Tests: PKG header/content id parser, AES test vector, ARK entry extraction, DXT5 decode of a tiny
  synthetic fixture, the folder route, a wrong package refused, no full-ARK output.
- An acceptance run against the user's package and the extracted `assets/game/ps3` reference.

**Verify.** The generated `ps3/buttons/<slot-id>.png` files match the corresponding PS3 reference
rectangles for the known controller glyphs; omitted instrument glyphs remain from the user's 360
base game when the package is selected. The PS3 `Glyph` table matches the 360 sheet's slot
rectangles (a mismatch stops the package as a finding, because the host override maps by slot);
`ps3/pads/ps_0..3.png` match the 360 `ps_*` art and omitted `xbox_*` pads remain original; the
install contains no game archive and no key; the PS3 scheme appears in the page and hot-swaps like
the others. The folder route passes even when the PKG decryption route cannot be exercised.

**Don't.** Do not bundle, download or log a Sony key. Do not copy the whole PS3 ARK. Do not write
the generated scheme into the repository. Do not accept a package whose content id is not a Rock
Band Blitz PS3 id without an explicit, logged override.

### P6a — Authoring kit and built-in scheme pipeline (not blocked)

**Status (2026-10-28): delivered without owner art.** The standalone guide, blank guides,
slot registry, original one-button and one-pad examples, individual-file private exporters,
fit-reporting validator, and installer staging pipeline are in the tree. Both examples validate;
the Xbox 360 private reference exports to a valid package-shaped local folder. A two-file
synthetic authored fixture was staged and compiled by Inno Setup, while a copied retail glyph
was rejected by the stage audit. The 2026-10-28 follow-up removed the five-name packaging list:
the installer now stages every validated direct folder under `assets/controller/schemes/`, while
the examples live separately under `assets/controller/examples/`. User layouts with non-system
ids may shadow installed authored layouts. The setup and updater include the guide, blank
templates, slot map, and original examples beside the game; players copy their packages into
the selected save-data root's `controller_schemes/` folder, which the game validates at boot.
The five named families remain P6b's art input.

**Goal.** Everything the owner needs to paint and drop a scheme, plus installer plumbing that
discovers any validated authored package. Built-in keyboard/mouse resources belong to P8 and
do not depend on the owner drawing a Keyboard scheme.

**Deliverable.**
- The final user-facing `assets/controller/README.md`, template, `slots.toml`, validator, and an
  example scheme with a simple procedurally drawn original icon (the `test-markers` scheme is a
  generated test fixture, not a shipped one). Write the README as a complete custom-template/package
  tutorial, not a link back
  to this plan. Cover every topic listed in §4.4 and show a minimal valid `scheme.toml` with one
  button image, then an optional pad-slot example. Explain where the user's files go and how to
  validate them before a boot. Keep it in sync with the implemented P2 parser and validator.
- Update the local export/template scripts to offer the new individual-file contract while keeping
  decoded game art in the gitignored local reference only. Provide blank individual glyph/pad
  guides or an extraction guide from `slots.toml`; no authored scheme requires a full sheet.
- The installer `[Files]` entry and build step that copy `assets/controller/schemes/` into
  `<install>/assets/controller/schemes/`; the audit rule that the folder contains only original art.
- A partial-scheme test: one original 128×128 glyph PNG for a measured button slot is accepted,
  resized and loaded without any pad PNG or copied retail glyphs. A second example replaces one
  pad while all seven other pad textures retain base-game art.

**Exit criteria.** `assets/controller/README.md` exists in the tracked authoring kit and a reader
can follow it alone to make and validate a one-button custom scheme: it specifies image format,
source size limits, manifest syntax, stable slot IDs, expected art/tint/alpha, proportion-preserving
resizing and transparent padding (including the 128×128→32×32 and 64×50→32×25 examples), optional
pads, per-slot base-game fallback, file placement, next-boot discovery and licensing. Run the
documented commands on that example and a non-square image; the validator accepts both and
reports the computed destination dimensions. An install copies exactly the authored scheme files;
the audit finds no game pixels under tracked `assets/controller/` or in the distributable.

**Don't.** Do not start from a decoded retail sheet as committed art. Do not ship a scheme the owner
has not authored or licensed.

### P6b — The owner's five controller schemes and visual acceptance (**blocked on the manual art**)

**Start condition.** `assets/controller/schemes/xbox-series/`, `playstation-series/`,
`nintendo-switch/`, `steam-controller/`, and `steam-deck/` each contain a real `scheme.toml` and at
least one original, declared glyph or pad image; the owner has confirmed the slot map. Pads and
instrument glyphs that the owner does not wish to replace may be omitted. P3/P4/P9/P10 and the
P6a pipeline pass. **This prompt cannot run before those files exist.** Earlier prompts use
generated fixtures; built-in keyboard/mouse resources never require a manual Keyboard package.

**Goal.** Integrate Xbox Series, PlayStation Series, Nintendo Switch, Steam Controller and Steam
Deck art, then accept the full controller catalog and built-in keyboard/mouse behavior.

**Deliverable.** The five validated controller schemes in `assets/controller/schemes/`, copied by the
installer; the PS3 scheme from P5 (user data, not committed); a final visual pass on every screen
that uses prompts (menu, Help & Options, gameplay HUD, pause, results) and on the retail Default
Presets page; both mapping screens' adjacent images; default and rebound controller/key/mouse
hints with first-source ordering (D12); immediate previews chosen by every input family; the
real-art acceptance captures. Xbox 360 is read from the base game, never copied into tracked art.

**Verify.** Each scheme draws every glyph and pad image it actually supplies; all intentionally
omitted glyphs and pads remain the corresponding original base-game art. Verify at least one
non-native-size, non-square source preserves its proportions and transparent margins. The
keyboard auto-switch matches the art, the selected controller scheme persists, and no supplied
slot falls back to retail unintentionally. The
acceptance captures are under `out/button-schemes/p6b/`.
Exercise all seven families when the PS3 import is available; if absent, record PS3 acceptance as
unproven rather than fabricating a package. Changing controller layout leaves key/mouse icons
unchanged. Rebinding Confirm to Y updates every sampled footer, and multiple bindings show the
first source in ordinary hints while both mapping pages retain the complete source list.

**Don't.** Do not infer a scheme's art from a screenshot; use the owner's files. Do not edit the
owner's art in the integration; report a bad slot to the owner. Do not commit the PS3-extracted art.

### P7 — Documents, audit and close

**Goal.** The change is recorded where the project looks for it.

**Deliverable.**
- `docs/engine/toggles.md`: the R4 row says implemented, names the modules and the Controller Layout
  page, and states the on-by-default decision (D7).
- `docs/settings/plan.md` §3: the Controller Layout page and the `controller.scheme` row; the page's
  row count; the hot classification and its evidence.
- `docs/settings/menu.md`: the Controls page's fourth row and the retail Controller Presets route.
- `docs/settings/status.md`: the live state and any open items.
- `docs/guides/assets.md`: the 2026-10-19 format section (P0) and the PS3 button-sheet route.
- `docs/reference/backlog.md`: the old "pin the sheet format" item is resolved by P0; any remaining
  work is named.
- `docs/reference/known-issues.md`: the PS3 trial/full RAP caveat, unsupported physical controls
  or unmeasured hint routes, and any backend left unproven. Static default-key icons after a
  rebind are a failed R12 acceptance item, not an accepted D12 limitation.
- `docs/guides/distributable.md` and `scripts/audit_distributable.ps1`: no PS3 art and no game art
  in the payload.
- `patches/README.md`: the texture override patch.
- `docs/README.md`: maintain the existing plan row; move the plan to `finished/` only when
  P8/P9/P10, P6b and P7 pass, with P5's import outcome explicitly recorded.
- Run `python .github/skills/documentation/scripts/check_docs.py` (exit 0) and `git diff --check`.

### P8 — Effective-binding icon resolver and built-in keyboard/mouse provider

**Status (2026-10-28): P8 host resolver delivered.** `src/input/input_icons.{h,cpp}` reads the
published effective-input snapshot, preserves binding order, selects the first source in the
active family, maps physical pad sources to controller slots or a built-in keycap, and renders
host key/mouse keycaps as original RGBA images. A selected package's declared button image is
returned when available; an omitted slot remains a retail-sheet reference. Snapshot revisions
change after input publication. Host tests cover reorder, family/layout choice, explicit empty,
reset, preset change, source catalog, and package fallback. The resolver is not yet consumed by
guest mapping rows or hints; P9/P10 own those rendering paths and their live acceptance.

The input parser accepts **16 pad targets plus eight stick directions**; the icon catalog
maps 14 pad targets to the measured sheet slots and renders Start, Back, and all eight stick
directions with original built-in keycaps. It covers all seven accepted mouse sources (five
buttons and two wheel directions). Key names are accepted as SDL-style strings by the profile
parser; the renderer draws printable alphanumeric legends and marks a legend it cannot render
with a neutral `?` image and a diagnostic. Steam Controller and Steam Deck back grips, touchpads
and other device-specific controls have no bindable source in the current driver, so package art
does not imply input support for them. Every controller family uses the same physical source
vocabulary and slot IDs; only its selected artwork changes.

Read/invalidation seams: `InputStore::Publish` publishes after a durable binding commit and
`ObserveControllerPreset` republishes after the retail preset changes. `icons::Capture` holds
one `CurrentInput()` pointer and its revision. `icons::Ordered` reads `general.Effective`,
`controller.Effective`, `keyboard_mouse.Effective`, or `music.Effective` directly, so a row's
saved source order and explicit empty value survive without copied defaults. A caller captures
again after a commit, reset, preset/profile publication, layout change or last-device change;
there is no separate icon cache to invalidate. `icons::First` filters the ordered list by the
active family and returns a neutral unbound image if that family has no source. Guest display
consumption remains P9/P10's responsibility.

**Goal.** One source of truth resolves physical input images for every mapping row and game hint,
without a selectable keyboard scheme or a dependency on the owner's controller art.

**Deliverable.**

- Inspect the current Interface Mapping, Gameplay Mapping, profile, default/preset inheritance,
  source-order and source-gate APIs. Record the exact read/invalidation seams and test fixtures;
  do not copy the historical §2.2 default list into a second binding implementation.
- A shared, host-testable resolver (proposed `src/input/input_icons.{h,cpp}`) exposing ordered
  icon descriptors for all sources and the first source per active family. Inputs include action
  identity, interface/gameplay context, effective profile/preset, controller family and version.
- Built-in keycap/mouse images or rendering, covering the binding system's accepted key/mouse
  sources. Resources are original/licensed, ship with the application, cannot be replaced through
  controller packages, and have neutral unsupported/unbound images with diagnostics.
- Controller physical-source icon lookup for all §4.6 families. Synthetic markers suffice for
  unprovided art. Report unsupported Steam/Deck physical controls separately; adding input-driver
  support is not implied by adding a layout.
- Invalidation on assign/remove/reorder/reset, profile/preset change, layout change and active
  input-family change, producing coherent immutable snapshots for P3/P4/P9/P10.
- Tests with synthetic bindings: Confirm controller `[Y, A]` and key/mouse `[Enter, Space]`;
  key/mouse mixed order; no binding in one family; explicit empty versus inherited bindings;
  gameplay/interface context differences; profile/reset changes; keyboard icons invariant under
  every controller choice; missing controller packages do not remove keyboard/mouse support.

**Verify.** Host tests prove full source order for mapping rows and first-source selection for
ordinary hints. Rebinding or resetting changes the resolved images without restarting. Generated
or built-in icons suffice to complete this prompt; future guest rendering remains unproven here.

**Don't.** Do not sort bindings, silently truncate them, mutate input dispatch, invent new profile
syntax, or substitute static default icons when the effective mapping differs.

### P9 — Input images in Interface Mapping and Gameplay Mapping

**Goal.** Both mapping surfaces show actual bound button/key/mouse images next to their action
labels, replacing textual binding lists.

**Deliverable.**

- A measured guest row/inline-glyph route using P8's ordered icon descriptors. Keep row identity,
  focus, Assign/Reset, capture and back targets stable while rendering source images side by side.
- Support mixed controller/key sources in Interface Mapping and the proper device scope on each
  Gameplay Mapping page. Show every effective binding in stored order; use measured guest
  wrapping/scrolling or a row detail route for long lists without hiding available bindings.
- Coverage for controller buttons, triggers/directions and accepted key/mouse inputs, explicit
  unbound rows, inherited presets/defaults, and refresh after binding edits or controller selection.
- Tests of row data, ordering, icon selection, refresh, row identity and overflow behavior, plus
  live captures of both mapping surfaces in `out/button-schemes/p9/` after authorized UI testing.

**Verify.** `Confirm: [A button image] [Enter keycap image]` contains adjacent images with a textual
action label. Rebind it to Y and a different key and see the new images immediately. Both gameplay
device pages do the same for their effective inputs. Changing the controller layout changes only
controller images. Long binding lists remain inspectable; no image overlaps the next row.

**Don't.** Do not use parenthesized button-name strings as replacement icons, show only the first
source on mapping rows, or add a host overlay to bypass the guest's row renderer.

### P10 — Remap-aware guest footers and visual action hints

**Goal.** Every actionable game hint depicts the input that actually performs that action in the
current interface/gameplay context.

**Deliverable.**

- An inventory of menu/Help & Options, pause, results, gameplay, and other actionable footer,
  widget, inline-glyph and localized-text routes, with action/context identity, draw/label seam,
  and a coverage/evidence entry for each. Calibration-specific scene art remains outside the
  v1 texture replacement scope in §2.1; any actionable calibration hints still need an explicit
  route/outcome in this inventory rather than an assumed exception to R12.
- Integration of P8's first-source-per-family result into the guest's own hints. Use contextual
  widget/label or served-record changes where global sheet remapping is ambiguous. A texture-only
  implementation is insufficient if two visible contexts need different icons for one slot.
- Next-frame refresh when bindings, profile/preset, active input family, or controller layout
  changes; no stale icons on already-visible footers. Preserve D14's explicit selection preview.
- Host/fixture tests for context and invalidation, and authorized live captures/logs in
  `out/button-schemes/p10/` covering default, rebound, multiple-source, removed, reset and
  profile-switch cases. Record any route left unproven as outstanding work.

**Verify.** Bind controller Y to interface Confirm: all Confirm footer/visual hints draw Y in
the selected controller family and input still confirms. Bind `[Y, A]` plus `[Enter, Space]`:
ordinary hints draw Y or Enter according to real last input; mapping rows retain both alternatives
per family. Reordering changes the hint; removing/resetting/profile-switching refreshes it.
Gameplay action hints follow Gameplay Mapping/presets while pause/menu hints follow Interface
Mapping. Repeat across representative controller layouts and unchanged built-in key/mouse art.

**Don't.** Do not treat a logical guest A prompt as proof that physical A is bound. Do not apply
interface remaps to gameplay blindly, advertise unbound inputs, or mark R12 complete while a
known actionable hint still shows stale/default bindings.

## 8. Validation matrix

| Check | Proves | Evidence |
| --- | --- | --- |
| `hmx_milo.py texture` on 360/PS3 `buttons` | header + 31, DXT5, Xbox word swap and grid are reproducible | decoded PNGs, 30 occupied cells |
| `controller_config` decode | the eight pad images and the per-slot mapping | `pads-contact.png` |
| Marker sheet on the menu prompt band | the host override reaches the glyph draw | capture + pixel/OCR |
| Marker pad on Controller Presets | the pad slots are overridden, not only the glyphs | capture |
| Select `test-markers` on the page | the row writes `controller.scheme` and applies live | log + capture |
| Leave the page / return | no restart prompt, selection persists | capture + `settings.toml` |
| New boot | the saved selection is the active scheme | log |
| Key/mouse event, no controller packages installed | built-in key/mouse icons are available and follow effective bindings | log + capture |
| Pad button/stick | the selected controller scheme is active | log + capture |
| Alternating inputs | no flapping beyond the debounce | log |
| `Xbox 360` selected (`retail`) | base-game controller art returns; key/mouse icons remain built in | capture |
| Missing/corrupt package | skipped with a reason; the page is not dead | boot log |
| PS3 `.pkg` | content id, decryption, one-entry extraction | helper log + `install-manifest.toml` |
| PS3 folder | the same scheme from `gen/main_ps3.hdr` | helper log |
| PS3 generated art | individual controller glyphs and `ps_*` pads match the local reference; omitted instrument/`xbox_*` slots stay original | per-slot PNG comparison + capture |
| Partial package | one changed glyph does not erase or redistribute omitted instrument glyphs; one changed pad leaves seven original pad textures | composite/texture tests + capture + distributable audit |
| Variable source dimensions | 128×128→32×32 and 64×50→32×25 fit; real slot dimensions come from `slots.toml` | transform tests + validator report + capture |
| Custom-package guide | a reader can make, validate and install a one-image scheme using `assets/controller/README.md` alone | documented example run + file audit |
| No game art in the repo/release | the audit rule | `audit_distributable.ps1` |
| Switch cost | the frame does not stutter on a scheme change | measured frame times |
| Click/select each Controller Layout choice with keyboard, mouse and controller | on-page controller icons/pad change next frame without Apply or later pad input | capture + commit/generation log |
| Confirm bound to controller `[Y, A]`, key/mouse `[Enter, Space]` | ordinary interface hints draw first source per active family; mapping row draws all sources | resolver test + footer/row captures |
| Interface Mapping image rows | action text plus ordered adjacent physical input images, including mixed device sources | guest fixture + capture |
| Both Gameplay Mapping device pages | images reflect effective gameplay bindings/preset without inheriting interface Confirm remaps | resolver/row tests + captures |
| Assign/remove/reorder/reset/profile or preset change while hints visible | next-frame rows/footers reflect the effective mapping; no stale default icons | tests + generation log/captures |
| No binding for active family | neutral unbound image; no advertised false binding | resolver test + capture |
| Long binding list | every binding is inspectable; no clipping into neighboring rows | guest row test + capture |
| Seven-family catalog / keyboard package rejection | five authored controllers, retail default, conditional PS3, and no selectable/swappable keyboard layout | registry/validator tests + choice list |
| P10 hint-route inventory | every known actionable footer/visual/text hint follows context and remaps | coverage table + captures; unmeasured routes remain open |

## 9. Risks and open questions

1. **The SDK patch is the highest risk.** A generic texture override touches both backends and the
   cache's invalidation rules. P1 must stop and write findings if it cannot be made safe; the plan
   does not accept a fake hot swap. The fallback is a scene reload, which is a worse experience and
   must be recorded as such.
2. **PKG decryption is unproven.** The supplied file is a **TRIAL** content id; the folder has a
   **FULL** `.rap`. The trial package may need a different licence, or the folder route may be the
   only working one. P5 must report exactly which route worked. The extracted
   `assets/game/ps3` tree is a reference, not a substitute for the installer flow.
3. **The 23-byte record and the slot rectangles may not be identical in every scene.** P0 tests
   `buttons`, `blitz_icons` and `controller_config`; it also compares the 360 and PS3 `Glyph`
   tables. A scene or table that differs is a finding, not a silent fallback.
4. **Auto-switch can feel noisy.** The thresholds and debounce are measured in P4; the plan does not
   invent a final number. A mouse bump must not switch the art.
5. **A sheet swap cannot resolve all contexts.** Mapping rows need multiple source images, and
   interface/gameplay hints may resolve the same guest slot differently. P8 separates physical
   sources from logical actions; P9/P10 prove guest row/label routes. Static default-key images
   after a rebind violate D12 and cannot be accepted as a limitation.
6. **Full Controller Layout label and row budget.** The old 28-character single-line budget is
   insufficient for the required name plus a value. P3 measures separate guest label/value
   placement or wrapping; the provisional 20-character value cap is validated there. Truncating
   or renaming the setting is not the resolution.
7. **The retail pad page's eight images.** The four Xbox and four PS textures may represent distinct
   presets. Explicit slot IDs replace them independently; the optional shared pad is a deliberate
   author choice. Missing slots retain the matching original texture, not a different preset.
8. **Memory and load time.** Packages can have one image or up to 32 glyph images and eight pad
   images. Retain the source game sheet once, compose only active schemes and decode lazily where
   possible. P2 measures boot, binding refresh, switch cost, source-image bounds and GPU copies.
9. **Security.** The validator clamps file sizes/dimensions, rejects symlinks and path traversal,
   and never trusts a manifest's file paths outside its folder. A malformed PNG is a rejection, not
   a crash.
10. **Selection versus last-device input.** A mouse click selecting a controller layout also
    counts as mouse input. D14's explicit guest controller preview makes the choice visible
    without falsifying last-device state or suppressing ordinary keyboard/mouse hints.
11. **Icon coverage versus driver support.** Steam Controller/Deck art does not prove every
    physical control is exposed to the existing binding system. P8 inventories accepted inputs
    and missing icons separately; no new driver implementation is silently added to this plan.

## 10. Documents this change owes

| When | Document | What changes |
| --- | --- | --- |
| Plan filed / scope revised | [`docs/README.md`](../../README.md) | maintain the existing plan row, current path and binding-aware scope |
| P0 | [`docs/guides/assets.md`](../../guides/assets.md) | the 23-byte record, DXT5 offset, unswap, the 6×6 grid, the pad decode; the old "No" is marked superseded |
| P0 | [`docs/reference/backlog.md`](../../reference/backlog.md) | the "pin the sheet format" item is resolved; the successor is this plan |
| P1 | [`patches/README.md`](../../../patches/README.md) | the texture-override patch and why it is generic |
| P6a exit criterion | `assets/controller/README.md` | a standalone custom-template/package guide, with manifest and individual-slot examples, image formats, proportional resizing, transparent padding, original-art fallback, validation and installation |
| P3 | [`docs/engine/toggles.md`](../../engine/toggles.md) | R4 is implemented; the gate's default and the page |
| P3/P9/P10 | [`docs/settings/plan.md`](../../settings/plan.md), [`menu.md`](../../settings/menu.md), [`status.md`](../../settings/status.md) | Controller Layout, full label/preview, image mapping rows, binding-aware hints, hot evidence, live state |
| P4/P5/P6b/P8/P10 | [`docs/reference/known-issues.md`](../../reference/known-issues.md) | RAP/trial caveat, unsupported inputs/hint routes, any unproven backend; no accepted static-keyboard exception |
| P5/P6a | [`docs/guides/distributable.md`](../../guides/distributable.md) | no PS3/game art in the payload; the audit rule |
| P8/P9/P10 | [`docs/engine/input-flow.md`](../../engine/input-flow.md) | resolver context, ordering and remap-aware hint seams, once measured |
| P7 | this plan | status moved to `finished/` after P8/P9/P10, P6b and P7 pass; record P5's outcome |

## 11. Reproducing the corrected measurement

```powershell
python scripts\hmx_milo.py dump assets\game\360\ui\resource\fonts\gen\buttons.milo_xbox `
    --chunk 0 --out out\button-schemes\format-2026-10-19\buttons_chunk0.bin

# Header at 0x5C9 (23 bytes), eight zero bytes, DXT5 at 0x5E8, 512x512:
python scripts\hmx_milo.py texture assets\game\360\ui\resource\fonts\gen\buttons.milo_xbox `
    --asset ../image/icons_buttons_xbox_nomip.bmp --out out\button-schemes\p0\buttons-360.png
```

Run `texture` with `assets\game\ps3\ui\resource\fonts\gen\buttons.milo_ps3` for the PS3
sheet (same offset, no word swap). The pad scene uses the same header + 31 payload offset
for its 1024×1024 DXT5 images.
