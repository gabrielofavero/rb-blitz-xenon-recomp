# Button icon rendering quality: evidence and options

**Status: planning, 2026-11-02. No production change has been made.** The owner reports the
keycap images in the mapping rows and footers still look degraded ("soft") in the current
1024 build, and that the earlier 2048-square attempt did not satisfy either. This plan records
what each pipeline stage measurably costs, why the 2048 attempt did not deliver on its own, and
the options in rising order of cost and risk.

## 1. What the pipeline does today

| Stage | Current behaviour | Where |
| --- | --- | --- |
| Source art | 470 package PNGs under `assets/controller/`; keycaps are mostly 76x72 to 92x85 px, median 82x81. | `assets/controller/` |
| Tile fit | `FitIcon` scales the source into a 32-px tile with premultiplied Lanczos3; the tile sits centered in a 36-px cell (2-px gutters). | `src/assets/input_font_atlas.cpp` |
| Atlas | 1024-square; 28 columns x 7 rows of 36-px cells in the retail Pentatonic texture's unused bottom band from row 768. 180 tile slots hold 165 mapping icons and 15 footer hints. | `src/ui/input_font_scene.h` |
| Texture | The scenes keep the retail BC3 texture bytes and names, so the guest shares the retail upload. The override matches that upload by content hash and replaces the icon band in every mip. | `src/ui/input_font_scene.cpp`, `src/assets/texture_override.cpp` |
| Compression | DXT5 (BC3) with a **bounding-box** colour fit (block min/max endpoints, nearest palette entry) and 8-step alpha endpoints. | `src/assets/texture_codec.cpp` (`EncodeDxt5`) |
| Mips | 7 levels (1024..16) made by an **area filter on straight alpha**. | `src/assets/input_font_atlas.cpp` -> `src/assets/texture_codec.cpp` (`ResizeRgba`) |
| Guest draw | The glyph selects a 32-texel square with the cell metrics written into the four copied Glyph tables; the label's own size places it. | `src/ui/input_font_scene.cpp` |
| Presentation | On the owner's 2560x1080 display the title renders a 1280x540 guest frame and the host presents it at draw scale 2. `rb_blitz_495.log` line 29: `settings_boot: guest render base 1280x540, draw scale 2`. | [graphics.md](../../settings/graphics.md) §1 |

The retail reference is better provisioned than our band: the shipped `buttons.milo` sheet is
512x512 with **85-px** cell art (the same cells `input_font_atlas.cpp` crops for retail A/B/X/Y),
sampled down into the text-sized draw. Our band is 32-px art for a 32-px draw - no oversampling.

## 2. Measured stage costs

All numbers below come from disposable experiments under `out/_swap/icon-quality/`
(`measure2.py`, `mips.py`, `bleed.py`; owner-independent, no boot). They decode with an exact
DXT5 decoder and compare against the ideal Lanczos resize at the same size.

### 2.1 The encoder is the largest avoidable loss

Three representative keycaps, tile drawn 1:1, tile PSNR against the ideal tile:

| Icon | Tile | Bounding box (current) | Least-squares refinement | Gain |
| --- | --- | --- | --- | --- |
| `enter.png` (92x85) | 48 | 33.06 dB | **36.20 dB** | +3.14 dB |
| `enter.png` | 96 | 37.85 dB | **39.71 dB** | +1.86 dB |
| `a.png` (76x72) | 32 | 34.27 dB | **36.41 dB** | +2.14 dB |
| `a.png` | 64 | 34.43 dB | **37.11 dB** | +2.68 dB |
| `arrow_up.png` (76x72) | 32 | 34.39 dB | **36.83 dB** | +2.44 dB |
| `arrow_up.png` | 64 | 35.08 dB | **37.87 dB** | +2.79 dB |

Roughly halving the squared error is visible on flat keycap faces, where 4x4 block boundaries
show as horizontal bands. The same defect appears in the owner's 2026-11-02 crop
(`out/button-schemes/enter-48-zoom.png`, from the 1024 build): the keycap face carries regular
horizontal stripes that the source does not have.

The fix is standard: initialise endpoints from the colour cloud's principal axis, then iterate
(assign nearest palette entry, least-squares solve for the endpoints, re-quantise to RGB565)
and keep the best of the bounding-box and refined candidates. `stb_dxt.h`'s refine mode is the
usual public-domain reference; a few dozen lines in `EncodeDxt5` match it. The same encoder
serves the store's album art, so the gain is not icon-specific.

### 2.2 The mip filter mixes transparent black into icon edges

`ResizeRgba` averages RGBA independently, so a mip texel that straddles an icon edge averages
the icon colour with transparent **black**. Measured on the same keycaps at mip 1 (64 -> 32):
edge pixels are **24.3 to 32.5 levels (of 255) darker** than the same filter run premultiplied.
That is a dark halo around every icon whenever a mip is sampled - minification, trilinear
blending, or any draw smaller than the tile.

This matters most to the 2048 attempt: a 64-px tile drawn at ~34 px samples mip 1 with
trilinear blending, i.e. exactly the level whose edges were darkened. Fix: premultiply before the
resize and unpremultiply after (the source `ResizeIcon` already does this; only the atlas mip
step does not). A unit test can pin it: a white edge on transparent black must stay white.

### 2.3 Transparent-black BC3 padding is minor

Encoding a tile whose transparent padding is `(0,0,0,0)` instead of alpha-bled colour changes
edge RGB by only **0.2 to 2.7 levels** once the nearest-palette fit is applied. Worth doing for
correctness (bleed the colour a few pixels into transparent areas before encoding), not a
priority behind 2.1 and 2.2.

### 2.4 Guest resolution caps the achievable sharpness

The title is resolution-blind: its screen-size decision offers 720 and 540 and derives the
width as `height * 16 / 9`; the host then applies an integer draw scale
([graphics.md](../../settings/graphics.md) §1). On the owner's display that is a 1280x540 guest
frame presented at 2x - so the UI, the retail text and our icons are all rasterized at 540p-class
and doubled by the presenter. A 32-px tile is drawn into a 32-px area of that frame and the
presenter stretches it to 64 output pixels. **No atlas size can recover detail the guest frame
never contained.** A larger tile helps antialiasing and block noise, not output resolution.

Variant B of the graphics lane - the title's `720` constant at guest `0x823F382C` becoming
`1080`, one `[[midasm_hook]]` entry in `config/functions.toml` - would make the title lay out at
1920x1080 with draw scale 1. At that point the icons draw at 64 px and a 64/96-px tile becomes
necessary rather than cosmetic. That is the only route to genuinely crisp UI, and it is a
graphics-lane decision, not an atlas change.

### 2.5 What the 2048 attempt actually was

There were three different builds, and the records conflate them:

1. **True 2048** (commit `1ad4e6d0`): a private 2048 BC3 texture per scene, 64-px mapping tiles,
   96-px footer tiles, cells 68/100, distinct `Iconatonic`/`Hintatonic` identities. The owner
   accepted the footer and mapping-row appearance on 2026-11-02.
2. **2048 composed, 1024 uploaded**: the crash investigation downsampled both private textures
   to 1024 to save 8.4 MiB. This build had the 2048 source detail but delivered it at 1024 -
   no supersampling in the texture the guest sampled.
3. **Current**: the private identities were removed entirely; both scenes share the retail 1024
   texture and only its unused band is replaced.

So "2048 was tried" is true only of build 1, and even that could not beat the 540p guest frame
(§2.4) and sampled straight-alpha mips (§2.2). The owner's latest assessment - that neither
build looks good enough - supersedes the earlier acceptance for this quality question.

## 3. Options

| Id | Change | Expected gain | Cost / risk |
| --- | --- | --- | --- |
| A | Refined BC3 encoder (§2.1) | +1.9 to +3.1 dB on tiles; visibly fewer block bands | None in memory or layout; a pure host-code change, host-testable |
| B | Premultiplied mip filter + alpha bleed (§2.2, §2.3) | Removes 24-32-level edge halos; prerequisite for any minified/supersampled path | None in memory; host-testable |
| C1 | Keep the current 1024 retail-band atlas, apply A+B | Cheap improvement at 1:1; no supersampling | None |
| C2 | Private **1024** atlas with 64-px tiles at 68 stride (15 x 13 = 195 slots fits exactly), one texture shared by the icon and hint scenes | ~2x texel density at the same draw size; antialiasing and band noise improve; retail texture untouched | One extra guest BC3 chain, ~1.33 MiB with mips (not the 2048 attempt's 8.4 MiB for two textures). Needs the private texture record and override match, and a heap trace check |
| C3 | Restore build 1 (2048, 64/96-px tiles, two private textures) | Same as C2 but more texels than the guest draw can use at 540p | ~8.4 MiB; this is the allocation that fed the 2026-11-02 crash; not recommended without §2.4 |
| D | Guest render at 1080p (variant B, `0x823F382C` 720 -> 1080) | The only change that sharpens the whole UI, text included; then C2/C3 become worthwhile | Coordinator-owned `config/functions.toml` entry; layout, HUD and performance effects unmeasured; its own graphics-lane plan |
| E | Sampler/UV audit (filter mode, LOD bias, half-texel alignment, glyph V origin) | Unknown until measured; possible cheap win if a half-texel defect exists | No change until a capture proves a defect; needs one guest-output capture with a known ruler |
| F | Source art pass (cap tile at source resolution; re-source keycaps at >=128 px) | Removes the upscaling that 96-px footer tiles applied to 76-px sources | Asset work only; new art licensing/authorship to check |
| G | Uncompressed A8R8G8B8 private texture | Removes BC3 block noise entirely | ~5.3 MiB for a 1024 chain, plus a format change in the scene record and override; larger contiguous allocation than the one that failed under fragmentation. Not justified while §2.4 stands |

## 4. Recommended order

1. **A + B in one change**, with host tests: encoder PSNR fixture, mip premultiply fixture,
   alpha-bleed fixture. This is the only set with no memory or layout risk.
2. **Capture the same screen state before/after** (footer Enter/Esc and one mapping row at the
   owner's 2560x1080) and compare block-edge banding and edge halos, not just PSNR. If A+B is
   enough, stop there.
3. **C2** only if banding remains: a private 1024 atlas with 64-px tiles, **one** texture shared
   by both scenes, `ComposeRetail` retained only for scene uploads that still name the retail
   texture. Verify the resident cost against the heap trace (`bitmap_alloc: guest heap first
   allocation ...`) and the `rays.tex` heap line.
4. **D** as its own plan if the owner wants real sharpness; it is the only route past the
   540p guest frame and should not be smuggled into an atlas change.
5. **E** in parallel: one guest-output capture where an icon's drawn width in guest pixels can
   be measured against a known text height; document the actual draw size before choosing 64 vs
   96-px tiles.

## 5. Measurement plan

- Reproduce the owner's crop (the 2026-11-02 footer Enter/Esc and one mapping row) at 2560x1080
  with the guest-output capture, and keep it in `out/_swap/icon-quality/`.
- Score each build with: tile PSNR against the ideal (encoder), a block-edge discontinuity ratio
  (banding), edge halo levels (mip filter), text capture unchanged, and the heap trace lines.
- Host tests to add with A/B: encoder better-or-equal than bounding box on a fixture; mip edge
  premultiplied; alpha-bleed keeps transparent RGB from darkening an edge. `button_scheme` and
  `menu_payload` already pin the atlas geometry and must keep passing.
- No game/UI route is driven by this plan without the owner's approval.

## 6. Open questions

- What is the icon's actual drawn size in guest pixels at 540p and at 720p? The glyph geometry
  implies 32 px for the row tiles, but no capture has measured it against a ruler.
- Does the guest sample the font texture with mips/trilinear? The 7-level chain implies mips are
  authored for it, but the sampler state is not proven.
- Is the private-texture route still needed now that the Xbox heap is 160,000,000 bytes? C2 costs
  ~1.33 MiB; measure it against the first-allocation and `rays.tex` heap lines rather than assume.
- Does the font texture's shared retail name cause the guest to upload the retail copy before the
  override can replace it (the 2026-11-02 first 2048 attempt saw the retail texture being sampled
  at the new UVs)? The current build matches the retail upload, so the answer only matters for
  C2/C3.
