# Button icon rendering quality: evidence and options

**Status: investigation complete, 2026-11-04.** The shipped direction is the refined BC3 encoder,
premultiplied mips, one shared private 1024 atlas with 64-px tiles, and a modest increase to the
guest-authored icon sizes. §§2.6-2.16 contain the measured and live comparisons. The owner's
visual target is **not fully met**: both source-art softness and some face banding remain at the
roughly 40-px output size. The owner chose to preserve the current keycap style. This plan records
the best verified result within that constraint and the rejected alternatives; a future faithful
art redraw would be a separate design task.

The owner reports the keycap images in the mapping rows and footers still look degraded ("soft")
in the current 1024 build, and that the earlier 2048-square attempt did not satisfy either. This
plan records what each pipeline stage measurably costs, why the 2048 attempt did not deliver on
its own, and the options in rising order of cost and risk.

## 1. What the pipeline does today

| Stage | Current behaviour | Where |
| --- | --- | --- |
| Source art | 470 package PNGs under `assets/controller/`; keycaps are mostly 76x72 to 92x85 px, median 82x81. | `assets/controller/` |
| Tile fit | `FitIcon` scales source art into a 64-px private tile with premultiplied Lanczos3; 2-px gutters place it in a 68-px cell. A 32-px retail fallback remains. | `src/assets/input_font_atlas.cpp` |
| Atlas | One private 1024-square BC3 atlas; 15 columns x 13 rows of 68-px cells hold 180 mapping and footer slots. | `src/ui/input_font_scene.h` |
| Texture | The two private font scenes share a content-keyed BC3 texture. The override matches that upload by content hash and fills its mip chain; the retail texture retains a 32-px fallback band. | `src/ui/input_font_scene.cpp`, `src/assets/texture_override.cpp` |
| Compression | DXT5 (BC3) with **the best of three colour fits** (bounding box, principal axis, least-squares refinement) and 8-step alpha endpoints. Until 2026-11-02 the fit was the bounding box alone - the 1.9-3.1 dB deficit §2.1 measured. | `src/assets/texture_codec.cpp` (`EncodeDxt5`, `FitColoursBest`) |
| Mips | 7 levels (1024..16) made by an area filter on **premultiplied** alpha, after an alpha bleed into fully transparent texels. Until 2026-11-02 the filter averaged straight alpha and the padding stayed black (§2.2, §2.3). | `src/assets/texture_codec.cpp` (`ResizeRgba`, `BleedTransparentColours`), called from `src/assets/input_font_atlas.cpp` |
| Guest draw | Private glyphs select a 64-texel square. The mapping icon style is 22.3 guest units and the footer image style is 20.0, leaving the text styles unchanged (§2.16). | `src/ui/input_font_scene.cpp` |
| Presentation | On the owner's 2560x1080 display the title lays out a 1280x540 guest frame, while the SDK's 2x draw scale rasterizes a 2560x1080 render target. `rb_blitz_495.log` line 29 reports the base and scale; the render-path measurements are in [graphics.md](../../settings/graphics.md) §1. | [graphics.md](../../settings/graphics.md) §1 |

The retail reference was better provisioned than our original 32-px band: the shipped `buttons.milo` sheet is
512x512 with **85-px** cell art (the same cells `input_font_atlas.cpp` crops for retail A/B/X/Y),
sampled down into the text-sized draw. The private 64-px atlas now oversamples the same draw.

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

**Landed 2026-11-02.** `EncodeDxt1` and `EncodeDxt5` now evaluate the bounding-box, principal-axis
and refined candidates against the exact integer error of the palette they decode to and keep the
winner, so the fit cannot be worse than the bounding box that was there. Flat blocks (the empty
gutter, a keycap face) stop at the bounding box when its error is already zero. Pinned by the
`store_art` fixture "the block encoder places endpoints on an off-axis colour pair", which the
bounding box cannot meet. Cost measured 2026-11-02 on this machine (release): the `button_scheme`
suite builds about ten full 1024-square chains, and its runtime moved from 1.24-1.42 s to
1.88-1.93 s - roughly 50 ms per chain, inside a boot frame.

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

**Landed 2026-11-02.** `ResizeRgba` filters premultiplied colour and unpremultiplies the result, so
the whole atlas chain at every level, and the store's cover art, carry no darkened edge. Pinned by
the `store_art` fixture "the resampler preserves a flat field and averages a split one", whose
half-covered destination texel must stay white instead of dropping to about 128.

### 2.3 Transparent-black BC3 padding is minor

Encoding a tile whose transparent padding is `(0,0,0,0)` instead of alpha-bled colour changes
edge RGB by only **0.2 to 2.7 levels** once the nearest-palette fit is applied. Worth doing for
correctness (bleed the colour a few pixels into transparent areas before encoding), not a
priority behind 2.1 and 2.2.

**Landed 2026-11-02.** `BleedTransparentColours` copies the nearest visible colour four rings out
before every level is encoded, in both the icon atlas and `BuildPlatformTexture`; alpha is never
touched. Pinned by the `store_art` fixture "the alpha bleed fills transparent texels with visible
colour and leaves alpha alone".

### 2.4 Guest layout size and raster resolution are separate

The title is resolution-blind: its screen-size decision offers 720 and 540 and derives the
width as `height * 16 / 9`; the SDK then applies an integer **draw** scale
([graphics.md](../../settings/graphics.md) §1). At 1280x540 base and 2x draw scale, the GPU
renders the frame at 2560x1080 before presentation. This is not a 540p image stretched by the
presenter. The later pixel audit (§2.9) measured about 41x36 rendered pixels for row keycaps and
37x33 for footer keycaps. A 32-px atlas tile is therefore enlarged on the final render target;
the extra source detail of a 64-px tile could reduce that enlargement and improve the icons.

The graphics lane's 1080p guest-layout variant is a separate experiment, not an atlas
prerequisite. `RenderHeightHook` is already in `config/functions.toml`, but the current graphics
policy bounds forced guest height to 540..720: forcing 1080 was measured to corrupt title-screen
art ([graphics.md](../../settings/graphics.md), custom-resolution limits). The current 2x draw path already renders at
1080 output pixels. Any 1080 guest-layout proposal must first resolve that corruption and show
an icon advantage beyond the current 1080 raster path.

### 2.5 What the 2048 attempt actually was

There were three different builds, and the records conflate them:

1. **2048-scene attempt** (commit `1ad4e6d0`): a private 2048 BC3 texture per scene, 64-px mapping
   tiles, 96-px footer tiles, cells 68/100, distinct `Iconatonic`/`Hintatonic` identities. The owner
   accepted the footer and mapping-row appearance on 2026-11-02. That visual acceptance does not
   prove the private upload was sampled: the runtime also supplied a retail-texture fallback at
   the same normalized UVs, and its private upload was not observed in the later trace.
2. **2048 composed, 1024 uploaded**: the crash investigation downsampled both private textures
   to 1024 to save 8.4 MiB. This build had the 2048 source detail but delivered it at 1024 -
   no supersampling in the texture the guest sampled.
3. **Current**: the private identities were removed entirely; both scenes share the retail 1024
   texture and only its unused band is replaced.

Only build 1 carried 2048 guest bitmaps, while its accepted visual output may have used the
retail 1024 fallback. Its sampled mips still used straight-alpha filtering (§2.2). The owner's
latest assessment - that neither build
looks good enough - supersedes the earlier acceptance for this quality question.

### 2.6 Direct decoded-atlas record after A+B (2026-11-02)

The exact C++ `Compose` and `DecodeDxt5` path was run on the package's `enter.png`, `a.png`, and
`arrow_up.png`, using the pre-change `HEAD` (`559128d`) versions of `input_font_atlas.cpp` and
`texture_codec.cpp` for **before**, and the current working-tree versions for **after**. Both runs
used the same source RGBA and 32-px icon cell. Their decoded mip-0 and mip-1 tiles are saved
individually under `out/_swap/icon-quality/direct-atlas/`; `comparison.png` puts the six pairs
side by side at nearest-neighbour 8x magnification. The local `atlas_capture.cpp` and
`render_comparison.py` there reproduce the record. Only the icon cell was compared; the dummy
retail font and button sheets cannot affect these package-image pixels.

| Icon | Changed decoded pixels, mip 0 (32x32) | Changed decoded pixels, mip 1 (16x16) | Largest channel change |
| --- | ---: | ---: | ---: |
| Enter | 609 / 1024 | 212 / 256 | 36 at mip 0; 30 at mip 1 |
| A | 642 / 1024 | 178 / 256 | 39 at mip 0; 33 at mip 1 |
| Up Arrow | 674 / 1024 | 190 / 256 | 33 at both levels |

This is a direct record of the **encoded atlas object** for comparison with later atlas changes.
It does not measure the final guest draw: sampler choice, glyph scale, presentation and the
screen's neighbouring text are covered by the later guest-output capture in §2.7. An
isolated boot of the new release build reached a presented menu frame at 1280x540 guest /
2560x1080 output (`live-ab-20261102/game.log`), but Windows placed it on Codex's noninteractive
sandbox desktop. The live window capture and UI driver refused to claim a foreground window, so
no game screenshot or visual acceptance is claimed from that boot.

### 2.7 Same-screen guest-output record (2026-11-04)

Two release executables were built from the same tree, differing only in the pre-A+B `HEAD`
(`559128d`) and current `input_font_atlas.cpp` / `texture_codec.cpp`. The pre-change executable
was saved under `out/_swap/icon-quality/phase2-20261104-before/rb_blitz.exe`; the current
source and release executable were restored and rebuilt afterward. Both boots used the same
installed controller art, isolated user data, 2560x1080 borderless output, and a 1280x540 guest
frame at draw scale 2 (confirmed by each `game.log`). The path was Title -> Help & Options ->
Controls -> Interface Mapping. Both logs reached `settings_interface_mapping_screen` and composed
180 icon slots. The cursor was parked off the icons before each capture.

| Build | Matching full frame | Log |
| --- | --- | --- |
| Before A+B | `out/_swap/icon-quality/phase2-20261104-before/07-interface-mapping-with-assets.png` | `out/_swap/icon-quality/phase2-20261104-before/game.log` |
| After A+B | `out/_swap/icon-quality/phase2-20261104-after-borderless/08-interface-mapping-stable.png` | `out/_swap/icon-quality/phase2-20261104-after-borderless/game.log` |

`out/_swap/icon-quality/phase2-comparison.png` puts the mapping-row Enter/A, Esc/B, and footer
Enter/Esc crops side by side at nearest-neighbour 5x. Its generating script is
`out/_swap/icon-quality/phase2-20261104-before/compare.py`. The current build shows subtle
shading changes on the keycap faces and edges; the remaining block-scale bands and soft lettering
are still visible in the enlarged crops. There is no clear large halo in either guest capture.
The background lights animate between boots, so whole-frame pixel error would mix unrelated
stage changes with the icon change; the exact decoded-atlas pixel comparison remains in §2.6.
This visual record does not establish that A+B is enough to meet the owner's quality target.

### 2.8 C2 prototype record and guest binding failure (2026-11-04)

The exact C++ composition and BC3 decode path produced a private 1024-square atlas with 64-px
tiles at 68-px stride. `out/_swap/icon-quality/direct-atlas/c1-vs-c2.png` compares C1 mip 0
(32-px tile) with C2 mip 1 (32-px downsample) for Enter, A and Up Arrow. Its source captures and
`render_c2_comparison.py` are alongside it. C2 changed 816/1024, 736/1024 and 734/1024 decoded
pixels respectively at that common size. This verifies the proposed atlas object, not its guest
binding or screen quality.

Release build, focused host suites and generated-record validation passed with the prototype.
The owner authorized a live UI capture. On Interface Mapping, the prototype rendered retail font
fragments where row and footer icons belong (`out/_swap/icon-quality/phase3-c2-20261104/02-interface-mapping-stable.png`).
Renaming the copied texture/material/source records, then the font objects, then the scene path
did not correct the output (`out/_swap/icon-quality/c2-dirname-20261104/interface.png`). The final
probe log shows two separate 1024-square `Iconatonic_Regular_(8_89)4x.tex` guest bitmap allocations
at lines 361 and 1510. Its 1024-square GPU texture requests never matched the private BC3 marker;
none logged a private atlas composition. The working A+B log matched and composed the retail
texture. Both boots began with 160,000,000 guest heap bytes; the first `rays.tex` request had the
same 139,000,436 free bytes. That early heap line cannot establish the later resident cost, but
the two distinct bitmap allocations invalidate C2's one-shared-texture assumption.

Static inspection of the generated scene also finds the renamed `Iconatonic` texture referenced
by each copied material, and the renamed font material and texture-owner references. MiloLib's
full-scene parser rejected this title's `UILabelDir` while making that inspection, so the measured
strings and their locations were checked in the decoded Milo stream rather than reported as a
successful full parse. The live pointer trace below tests the binding separately.

The follow-up pointer probe narrows that failure. On Interface Mapping, the label's second style
font is the private `Iconatonic_Regular_(8_89)4x.font`, its material pointer reaches the private
`Iconatonic_Regular_(8_89)4x.mat`, and that material's texture pointer reaches the private
`Iconatonic_Regular_(8_89)4x.tex` (`font-bind-c2-20261104/game.log`, lines 1971-1980).
The retail style still points to its separate Pentatonic font/material/texture chain. The saved
C2 texture record contains the private BC3 marker, and `RndTex::Load` allocated its bitmap.
Both retail and private `RndTex` objects clear the embedded bitmap data pointer after load, so
that cleared temporary field does not explain the private upload failure
(`bitmap-compare-20261104/game.log`). The copied font-to-material-to-texture references are
therefore live; the unresolved step is how that private texture becomes a GPU texture request.

A second isolated boot served the same C2 icon scene and marked every 1024-square BC3 request
as a refresh candidate. After the mapping labels were active, it invalidated those candidates
and waited for a new frame. The private marker still never appeared in a GPU request
(`refresh-c2-20261104/game.log`, lines 148, 2070 and the 1024-square request lines); the
captured `refresh-c2-20261104/interface.png` still shows retail font fragments in icon slots.
Invalidating the override candidates alone does not repair C2. This probe used a saved scene
and temporary diagnostics, all removed after capture.

A later guest-side navigation probe reached Interface Mapping without desktop input
(`autonav-c2-requests/game.log`, line 1903). `RndText::DrawShowing` has an actual icon text run
whose font, material and texture pointers resolve to the private `Iconatonic` objects
(`autonav-c2-20261104/game.log`, `c2_draw` lines). The recompiled `sub_8229CFE0` traverses
72-byte text runs and reads each run's own font at +64 and material at font+56, so the earlier
idea that the label must draw every run with its primary material is unsupported. The private
`RndTex` has its own runtime +104 object pointer, distinct from retail, while both report a
1024x1024, 8-bit bitmap (`autonav-c2-texfields/game.log`, `c2_tex` lines). Every observed
1024-square BC3 override request in that boot starts with retail-style BC3 bytes rather than the
private marker (`autonav-c2-requests/game.log`, `c2_request` lines). The override provider only
sees eligible, unscaled 2D BC3 fetches; these logs cannot exclude a fetch in another format or
texture-key shape. No new visual result is claimed from this boot because the desktop would not
grant the capture process foreground access. All navigation and trace code was temporary.

The next isolated guest-side boot traced Interface Mapping's RndText mesh entries rather than
only its style runs (`out/_swap/icon-quality/mapping-mesh-20261104/game.log`, lines 1894-1903).
Two labels each have a private-font mesh with draw count 5 and a direct pointer to the private
material at mesh+292. Other labels have a private mesh with count 0 and skip it; the earlier
settings-screen mesh was one of these, so its pointer alone was not draw evidence. The private
material is therefore assigned to meshes that the guest does draw. The GPU cache also saw all
unique texture fetches at least 512 pixels across during that boot. A 1024-square BC3 candidate
at guest physical `0x1BF4B000` was dumped to
`out/_swap/icon-quality/mapping-mesh-20261104/gpu-firstdiff8.bc3`; none of its 65,536 BC3
blocks equals the corresponding block in the saved C2 atlas. Its identity as the private mesh's
actual sampled texture remains unproven.

A post-read bitmap probe confirmed that the private `RndBitmap` receives the intended data:
`out/_swap/icon-quality/bitmap-upload2-20261104/private-bitmap-raw.bc3` is byte-for-byte equal
to the 1,048,576-byte mip-0 payload in the saved C2 scene. The first block contains the private
marker (`bitmap-upload2-20261104/game.log`, lines 1511-1516). The failure is now bounded after
the guest reads the private bitmap and before a matching BC3 fetch reaches the texture override.
The bitmap probe was temporary. An initial probe boot used an overly broad object guard and
raised a guest access violation before reaching the menu; the corrected boot reached the bitmap
load and produced the comparison above.

A further probe corrected the interpretation of `RndTex +104`: it points to the source-path
string, not a GPU texture object. The private texture points to
`ui/resource/image/iconatonic_regular_(8_89)4x.bmp`, while the retail texture points to the
corresponding Pentatonic path (`out/_swap/icon-quality/runtime-trace-20261104/game.log`,
`c2_runtime` lines 1803-1824). The recompiled `sub_8220CCA8` reads the first byte of that
string before choosing its load path; `sub_8220C198` also reads the path. The two distinct
`+104` pointers therefore establish distinct source names only. They provide no evidence of
distinct GPU resources or fetch constants. The probe hooks and scene injection were removed.

Static guest code narrows the bind seam further: `sub_823F0810` reads the material from
`RndMesh +292` and calls `sub_822A5D80` with that material and mode 18 before the mesh draw.
`sub_822A5D80` dispatches through the active renderer's virtual method at +8; it does not itself
write a fetch constant. The private mesh's confirmed `+292` pointer can therefore be followed at
this call to inspect material state. At that stage no matching private GPU fetch had been captured.

An isolated Interface Mapping trace followed that dispatch at runtime
(`out/_swap/icon-quality/renderer-bind-nav4-20261104/game.log`, lines 1874-1880). The two
private-icon meshes both point to material `0x42DC1740`, whose texture field at +76 points to
private `RndTex` `0x42DC0E00`. Renderer mode is 0, so requested slot 18 stays active. Its
virtual +8 target is `sub_822A9460`; that function invokes the same renderer's virtual +12
target, `sub_822A7058`, with the private material. The recompiled `sub_822A7058` reads
material+76 at `0x822A713C`, but uses it only as a non-null flag while assembling render-state
bits. This proves the private material reaches the active renderer, but did not yet prove its
texture identity reaches a bind. At that stage no matching GPU fetch constant or physical texture
address had been captured, so C2 sampling remained unproven. The window could not be brought
to the interactive desktop for a valid frame capture. The scene substitution, navigation and
hook code used for this trace were removed after it.

The next isolated Interface Mapping trace found the actual bind route. `sub_822A9460` calls
`sub_822B1690` before its render-state setup; that reaches `sub_822B0F18`, which reads
material+76 and calls renderer virtual +60 (`sub_823EBF10`) with the `RndTex` pointer in slot 0.
That calls the texture's virtual +124 (`sub_823ECF98`), which submits its +128 GPU resource to
`sub_825087A8`. The mesh's virtual +64 target is `sub_823EF780`, a geometry draw. The earlier
static follow-up missed `sub_822B1690`; `sub_822A7058` and `sub_822A5F28` still only encode
texture presence and render state.

The original C2 scene's private `RndTex` and retail Pentatonic `RndTex` both had +128 resource
`0x40F24390` in `live-gpu-fetch-20261104/game.log` (lines 1577-1578, 1846-1847). Their six
fetch words were identical. A separate upload trace showed each +128 field starts at zero and
the private upload receives the already existing retail resource
(`live-upload-20261104/game.log`, lines 284-285, 1518-1519). This explains the retail fragments:
the private mesh really drew, but sampled the retail GPU texture.

The guest resource cache uses the first 32-bit word of the embedded `RndBitmap` as its lookup
key. The original retail font and private C2 bitmap both supplied `0x6F93E325` despite different
BC3 bytes (`live-cache-call-20261104/game.log`, lines 285-287, 1523-1525). That word is at
offset 4215 in the stored C2 Milo. In a disposable copy, replacing only that word with
`0xDF83E231` (CRC32 of the private mip-0 BC3 bytes) made the private upload allocate a distinct
resource `0x42DD39D0` rather than retail's `0x40F25210` (`live-hash-fixed-20261104/game.log`,
lines 285-287, 1523-1525). On Interface Mapping, the private mesh submitted fetch words
`88000002 F114B054 007FE3FF 00000D10 00000180 F10EBA00`, distinct from retail's words
starting `88000002 FBC5E054` (lines 1598, 1861-1865). The fetch constant's base-address field
decodes to `0xF114B000` for the private upload and `0xFBC5E000` for retail. This is a live
binding proof, not yet a GPU cache byte comparison or a visual acceptance capture. Windows did
not grant this run's capture process foreground access.

The original C2 prototype and its diagnostic hooks were removed after capture. The later
production builder assigns a content-derived bitmap key; §§2.11-2.12 measure its live upload,
guest-output pixels and memory lifetime. The earlier 2048 appearance still needs to be read as
fallback-assisted, not proof of a sampled private texture.

### 2.9 Guest draw-size audit from the A+B capture (2026-11-04)

`out/_swap/icon-quality/measure_draw.py` measures the dark keycap rims and bright label ink in
the saved 2560x1080 Interface Mapping frame from §2.7. The game log fixes the guest frame at
1280x540 and presenter scale at 2. The script's fixed regions and thresholds make this repeatable;
faint filtered edge pixels are excluded, so the bounds are approximate.

| Element | Output bounds | Guest bounds |
| --- | ---: | ---: |
| Mapping row Enter, A | 41x36 px each | 20.5x18 px |
| Footer Enter, Esc | 37x33 px each | 18.5x16.5 px |
| Confirm label ink height | 27 px | 13.5 px |
| SELECT label ink height | 22 px | 11 px |

The guest bounds describe layout units; the SDK rasterizes them at the 2x draw scale. Thus the
32-px source tile feeds a 41x36-px rendered row keycap, about 0.78-0.89 source texels per rendered
pixel. C2's 64-px tile would provide about 1.56-1.78 source texels per rendered pixel at the same
draw size. This makes C2's extra detail potentially useful even with a 540p guest *layout*.
This geometric audit alone cannot identify the sampler; §2.10 compares the actual tile colours
against the decoded atlas to test that separately.

### 2.10 Captured keycap fit against decoded atlas pixels (2026-11-04)

`out/_swap/icon-quality/fit_sampler.py` compares the exact decoded A+B BC3 mip-0 Enter and A
tiles from §2.6 against the same keycaps in the saved guest-output frame. It searches the tile's
output rectangle and fits one constant RGB offset per channel, scoring only opaque tile pixels so
the animated background cannot dominate. The best rectangle for both is 41x38 rendered pixels
(the dark visible rim in §2.9 is 41x36). Mean absolute RGB error per channel:

| Tile | Nearest | Bilinear | Bicubic |
| --- | ---: | ---: | ---: |
| Enter | 6.025 | **2.464** | 3.055 |
| A | 5.269 | **2.628** | 3.094 |

At that rectangle, a half-output-pixel shift in either axis raises bilinear error to at least
4.205 for Enter and 4.238 for A. The unshifted fit needs less than one RGB level of constant
correction per channel. These two draws are strongly consistent with ordinary, centered bilinear
sampling of mip 0; there is no measured half-texel defect to fix here. This image fit is not a GPU
sampler-state trace and says nothing about smaller draws that may select another mip.

### 2.11 Production C2 builder and live override check (2026-11-04)

The in-tree C2 builder now writes a separate 1024-square icon scene: 64-px tiles on a
68-px grid, distinct Tex/Mat/BMP names, a private BC3 marker, and a CRC32 bitmap key
derived from the private mip-0 bytes. The row and hint scenes carry the same key and
pixel chain. The texture override identifies the private marker and composes all 180
slots at 64 px; the retail override still uses the 32-px bottom band.

The release build and `menu_payload`/`button_scheme` tests pass. In the live D3D12
run at 2560x1080, `out/_swap/icon-quality/live-c2-nav7-20261104/game.log` records the
private marker match at line 1691 and composition at line 1692. The guest reached
`settings_interface_mapping_screen` at line 1873, and the private atlas recomposed
there at line 1883. The first guest heap allocation was 160,000,000 bytes (line 219).
The `rays.tex` 1-MiB request still had 137,909,068 contiguous bytes at line 305;
a later `rays.tex` request had 115,661,352 bytes at line 1386.

The guest loaded two distinct `Iconatonic` bitmap chains, at lines 354-359 and
1502-1507. Each seven-level BC3 chain is 1,398,016 bytes. This log alone did not
establish whether their pixel allocations overlapped or their GPU resources were
shared. No heap failure occurred. The temporary navigation and selection probes were
removed after the run. §2.12 resolves the lifetime and sharing questions.

The UI capture scripts could not bring the game window to the foreground in this
Windows session. This run's marker match established the private texture request; §2.12
adds a guest-output frame and the duplicate-chain lifetime check.

### 2.12 C2 guest-output and memory check (2026-11-04)

The release build was driven through the guest's own Title -> Help & Options -> Controls ->
Interface Mapping route in an isolated run. Windows again withheld foreground access, so a
temporary guest-side navigation probe selected the existing list rows and the D3D12 presenter's
`CaptureGuestOutput` saved the 2560x1080 frame. The probes were removed afterward. The full frame
is `out/_swap/icon-quality/live-c2-guest-capture4-20261104/interface.png`; its log reaches the
mapping screen at line 1871 and captures the frame at line 1881. `out/_swap/icon-quality/c2-screen-comparison.png`
puts its Confirm and Go Back row crops beside the A+B capture from §2.7. The footer cannot be
compared in that pair: the unfocused input probe left the active family on controller A/B, while
the earlier focused desktop capture had keyboard Enter/Esc hints.

The row keycaps have their full Enter/A/Esc/B artwork in the C2 frame, rather than the retail
fragments seen with the colliding bitmap key (§2.8). The private marker matched the override
provider during the same boot. `out/_swap/icon-quality/compare_c2_capture.py` fits the saved
decoded C1 and C2 tiles to the captured row icons with centered bilinear resizing and an RGB
offset. At the best rectangle, Enter's mean absolute RGB error per channel is 5.61 for C1,
3.83 for C2 mip 0; A's is 4.17 for C1, 3.62 for C2 mip 1. The fit, visible artwork, marker match
and earlier private-mesh bind trace together support that the final draw samples C2's replacement
pixels. This is not a direct GPU readback of the sampled texture. The enlarged crops show a
modest improvement in the row keycaps. The fitted C2 rectangle is about 43x40 rendered pixels,
versus 41x38 for C1 (§2.10): the 64/68 C2 glyph ratio is also about 6% larger than C1's 32/36,
so the visual difference includes a small size change. Whether it meets the owner's quality
target remains a visual decision.

The isolated residency trace is `out/_swap/icon-quality/live-c2-residency-20261104/game.log`.
After the first private `RndTex::Load` returned, its guest bitmap pixel address was in the heap
free list (line 369). The same was true of the second private bitmap after its load (line 1515).
The first address had been reused by the time the second upload started, so these serialized
bitmap chains do **not** leave two 1.33-MiB guest pixel allocations resident. The two distinct
`RndTex` objects both held GPU resource pointer `0x41805A00` after load (lines 369 and 1515):
the common bitmap key makes them share one GPU texture. The second upload began with 118,777,704
contiguous guest heap bytes free (line 1509) and no allocation failure. The runtime costs one
private GPU chain plus brief guest bitmap upload storage and two small scene object graphs, not
two resident GPU chains. Their exact peak overlap with other scene loads remains unmeasured.

### 2.13 Matched keyboard footer capture (2026-11-04)

The isolated C2 run reached Interface Mapping with the input family set to keyboard and captured
the 2560x1080 guest output. The game log records the mapping screen at line 1869, the keyboard
selection at line 1877, and the frame capture at line 1883 in
`out/_swap/icon-quality/live-c2-keyboard-footer-20261104/game.log`. The full frame is
`out/_swap/icon-quality/live-c2-keyboard-footer-20261104/interface.png`. The temporary guest
navigation, keyboard-selection and capture hooks were removed after the run.

`out/_swap/icon-quality/c2-keyboard-footer-comparison.png` places the mapping-row and footer
Enter/Esc crops beside the earlier A+B keyboard capture from §2.7. Both frames show the same
screen and keyboard hint family. The C2 footer keycaps retain complete Enter/Esc artwork and
look modestly clearer at the rim and lettering; visible block shading and softness remain.
The background animation differs between captures, so the crops support a visual comparison of
the keycaps, not a pixel-for-pixel screen subtraction.

`out/_swap/icon-quality/measure_c2_keyboard_footer.py` applies the same dark-rim threshold as
§2.9 to fixed footer regions. The approximate visible bounds are 37x33 output pixels for both
A+B keycaps, 39x34 for C2 Enter and 39x36 for C2 Esc. Faint filtered edges are excluded. The
slightly larger C2 glyph ratio (64/68 versus 32/36) contributes to the apparent improvement;
this capture alone does not separate that size effect from the extra source detail. The owner
has since confirmed that the result is not sufficiently sharp.

### 2.14 Residual softness and banding experiments (2026-11-04)

The owner identified both fuzzy letters/outlines and block bands on the key faces in C2. A
disposable 80% opaque-RGB unsharp pass was applied to the 64-px fitted tiles, then built and
captured on the same Interface Mapping keyboard route. The guest frame is
`out/_swap/icon-quality/live-c2-sharpen2-20261104/interface.png`; the matched crop is
`out/_swap/icon-quality/c2-sharpen-live-comparison.png`. The letters and rims change only
slightly at the measured draw size, while the flat-face banding remains. This is not a useful
enough improvement to carry into production; the temporary sharpen and capture hooks were
removed after the experiment.

`out/_swap/icon-quality/measure_c2_residual.py` compares the decoded C2 BC3 tiles with a
premultiplied-Lanczos 64-px fit of the current source PNGs, then bilinear-samples both to
43x40 output pixels. For opaque pixels the mean absolute RGB difference is 1.86/255 for Enter,
1.55 for A, and 1.56 for the up arrow. These are approximate offline comparisons, not a live
uncompressed guest capture; the C++ fit and Python fit need not round every pixel alike.
`out/_swap/icon-quality/c2-uncompressed-vs-bc3.png` shows that removing BC3 alone leaves the
small lettering and outline largely as they are. The SDK texture override currently accepts
only BC3 guest textures (`rexglue-sdk/src/graphics/pipeline/texture/cache.cpp`,
`GetTextureOverride`), so an uncompressed live variant would require a guest scene format and
override-path change for a small predicted gain.

An isolated source-art sketch under `out/_swap/icon-quality/source-art-preview/` uses larger
legends and flatter faces. Its simulated draw in `source-art-preview.png` is easier to read, but
the first sketch changes the existing keycap style too much to ship. A source-art pass needs an
agreed style direction and representative live captures before replacing the package assets.

### 2.15 Further style-preserving trials (2026-11-04)

The owner chose to keep the current keycap style. The source PNGs have essentially no transparent
margin to reclaim: at an alpha threshold of 16, Enter occupies `(1, 0)..(91, 85)` of its 92x85
image, while A, Esc and the up arrow occupy `(1, 0)..(75, 72)` of their 76x72 images. Trimming
cannot make the keycaps meaningfully larger inside the existing tiles.

An isolated BC3 block-alignment trial used a 72-px cell, 14 columns and a 4-px inset, keeping
64-px icon art and the 68-px font cell. The matching Interface Mapping capture is
`out/_swap/icon-quality/live-c2-aligned2-20261104/interface.png`, compared with C2 in
`c2-aligned-live-comparison.png`. The rims looked harsher and the face bands remained. The
geometry change was reverted.

An isolated direct-source mip trial rebuilt each smaller mip tile from the source PNG instead
of downsampling the whole private atlas. Its live capture,
`out/_swap/icon-quality/live-c2-direct-mip-20261104/interface.png`, showed displaced and sliced
mapping icons and broken footer hints (`c2-direct-mip-live-comparison.png`). This approach was
reverted; the capture does not establish that direct-source mips improve quality when correctly
packed.

The mapping label's original icon style size is 20.28 guest units; the inline-help image has a
separate size field. A disposable trial raised the mapping size to 22.3 and the help size to
21.0, about 10% each, without changing the artwork. The matched capture is
`out/_swap/icon-quality/live-c2-size-trial-20261104/interface.png`, with representative crops in
`c2-size-live-comparison.png`. Legends are slightly easier to read, but the face bands persist and
the enlarged footer icon crowds its label. The size edits and all capture hooks were reverted.
The remaining source-art path was checked in §2.16 before closing the investigation.

### 2.16 Final 2048 and style-preserving size checks (2026-11-04)

A single shared 2048 BC3 font scene with 96-px tiles (100-px cells) was built and passed the focused
host tests. The guest allocated its 2048 bitmap, but the captured Interface Mapping screen had no
mapping or footer icons. The GPU override never received a private 2048 texture request, so it
could not supply pixels. The guest capture is
`out/_swap/icon-quality/live-c3-single-2048-20261104/interface.png`. This route was reverted;
larger allocation alone is not a quality fix.

The installed Enter and Esc PNGs already have pixelated legends at their native 92x85 and 76x72
sizes. A disposable high-resolution lettering sample that kept the original silhouette and rim
is under `out/_swap/icon-quality/art-trial/`; at a 64-px tile it was thinner rather than clearer.
No source art was replaced.

The final, style-preserving change raises only the copied mapping icon style from 20.28 to 22.3
guest units and the inline-help image style from 19.09 to 20.0. The source records are checked
before editing. The live 2560x1080 Interface Mapping capture is
`out/_swap/icon-quality/live-c2-final-size2-20261104/interface.png`; representative baseline and
final crops are `c2-final-size-live-comparison.png`. Mapping legends are somewhat easier to read;
footer captions remain separated. The captured guest log confirms a matched private font texture
and composition of 180 image slots. The face bands and some softness remain, so this is the final
measured improvement rather than a claim that the owner's visual target was reached.

## 3. Options

| Id | Change | Expected gain | Cost / risk |
| --- | --- | --- | --- |
| A | Refined BC3 encoder (§2.1) | +1.9 to +3.1 dB on tiles; visibly fewer block bands | None in memory or layout; a pure host-code change, host-testable |
| B | Premultiplied mip filter + alpha bleed (§2.2, §2.3) | Removes 24-32-level edge halos; prerequisite for any minified/supersampled path | None in memory; host-testable |
| C1 | Keep the original 1024 retail-band atlas, apply A+B | Cheap improvement at 1:1; no supersampling | Superseded by C2. |
| C2 | Private **1024** atlas with 64-px tiles at 68 stride (15 x 13 = 195 slots fits exactly), one texture shared by the icon and hint scenes | More source texels for an approximately 41x36-px rendered keycap at the current 2x draw scale (§2.9); the row crop is modestly sharper (§2.12) | Landed. The content-derived key fixes the cache alias; both scenes share one GPU resource (§2.12). One BC3 chain is ~1.33 MiB. Quality improves but still misses the owner's target. |
| C3 | 2048 private atlas with 96-px tiles | No demonstrated live gain: a single-texture trial loaded its bitmap but drew no icons (§2.16) | The earlier two-texture build used ~8.4 MiB and crashed; the one-texture trial did not reach the GPU override. Rejected. |
| D | Force a 1080p guest layout through the existing `RenderHeightHook` | Could alter UI layout density; the current 2x draw path already rasterizes at 1080 output pixels | Known title-screen artwork corruption at forced guest height 1080; graphics policy limits guest height to 540..720. A separate graphics-lane fix and proof are needed. |
| E | Sampler/UV audit (filter mode, LOD bias, half-texel alignment, glyph V origin) | Captured Enter/A fit centered bilinear mip-0 sampling; no half-texel correction indicated (§2.10) | Other draw sizes and mip selection remain open; no renderer change is supported by this capture. |
| F | Restrained source-art pass within the current silhouette, rim and shading style | A faithful redraw might improve the remaining source-pixel softness | The representative lettering sample was thinner at 64 px (§2.16). No package art was replaced; this requires a separate art design and live comparison. |
| G | Uncompressed A8R8G8B8 private texture | Removes BC3 block noise; §2.14's offline draw predicts only a small visible gain for the measured keycaps | ~5.3 MiB for a 1024 chain, plus a format change in the scene record and SDK override path; larger contiguous allocation than the one that failed under fragmentation. Not justified by the current evidence. |

## 4. Decision sequence

1. **A + B in one change**, with host tests: encoder PSNR fixture, mip premultiply fixture,
   alpha-bleed fixture. This is the only set with no memory or layout risk. **Landed 2026-11-02.**
2. **Capture the same screen state before/after** (footer Enter/Esc and one mapping row at the
   owner's 2560x1080) and compare block-edge banding and edge halos, not just PSNR. If A+B is
   enough, stop there. **Recorded 2026-11-04:** §2.6 has the direct atlas pixels and §2.7 has
   matching live guest-output frames. The remaining softness keeps C2 relevant.
3. **C2**: a private 1024 atlas with 64-px tiles, **one** texture shared
   by both scenes, `ComposeRetail` retained only for scene uploads that still name the retail
   texture. Verify the resident cost against the heap trace (`bitmap_alloc: guest heap first
   allocation ...`) and the `rays.tex` heap line. **Implemented and captured; the owner still sees softness and bands.**
   §2.8 records the atlas pixels, broken original guest frame, a confirmed private bitmap load,
   a drawable private mesh and the guest cache-key collision. §2.11 records the production
   content-derived key and live private override match. §2.12 records the mapping-row guest
   capture, better fit to C2 pixels and one shared GPU resource. §2.13 records the matching
   keyboard Enter/Esc footer capture. The owner found C2 better but still below target;
   §2.14 rules out a simple sharpen pass and makes uncompressed texture a low-priority route.
   §2.15 records unsuccessful alignment and mip trials. §2.16 records the rejected 2048 and
   lettering samples and the final guest-authored icon-size adjustment. The final change improves
   readability modestly while preserving the current style.
4. **D** remains a graphics-lane issue. The existing 2x draw path already rasterizes at 1080;
   the forced 1080 guest-layout variant corrupts title-screen art (§2.4). Do not use it as an
   icon-quality shortcut without fixing and measuring that separate route.
5. **E**: one guest-output capture where an icon's drawn width in guest pixels can
   be measured against a known text height; document the actual draw size before choosing 64 vs
   96-px tiles. **Current draw measured in §2.9; Enter/A filter and UV fit recorded in §2.10.**
   Smaller-draw mip behavior was outside this screen's measured use case.

## 5. Measurement record

- The owner's footer Enter/Esc and mapping-row crop was reproduced at 2560x1080 with guest-output
  captures under `out/_swap/icon-quality/`; the final capture and comparison are in §2.16.
- Tile PSNR, banding, edge halos, guest-output pixels and heap traces were checked in §§2.1-2.16.
- Host tests landed with A/B (2026-11-02): `store_art` carries the refined-fit fixture, the
  premultiplied-mip fixture and the alpha-bleed fixture; `button_scheme` and `menu_payload` still
  pin the atlas geometry and still pass. The decoded atlas before/after record is in §2.6; the
  guest-output capture of step 2 is in §2.7.
- The owner authorized autonomous capture. A later visible-desktop boot completed the same-screen
  before/after pair on 2026-11-04 (§2.7). The C2 keyboard-family mapping and footer comparison is
  recorded in §2.13.
- The final release build passed `menu_payload`, `store_art`, `button_scheme` and
  `button_scheme_fingerprints`. The live log matched the private font atlas and composed 180 image
  slots; §2.16 records the final frame and its remaining visual limits.

## 6. Residual limitations

- §2.9 measures the 540p guest frame; higher guest layout resolutions were not measured.
- §2.10's two
  approximately 41x38-px draws fit centered bilinear sampling of the 32-px mip-0 tile, but do not
  establish the sampler state or LOD elsewhere.
- The current source art and roughly 40-px output size limit legend clarity. A faithful asset
  redraw would require a separate design pass and owner review; this investigation did not find
  a safe codec, layout or filter change that removes the remaining softness and face bands.
- §2.12 proves each
  guest bitmap pixel allocation is freed after upload and both scenes share one GPU resource,
  but it does not isolate every concurrent temporary allocation or exact peak guest heap cost.
