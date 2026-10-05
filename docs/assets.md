# Assets — what the title ships, and how to get it out

Rock Band Blitz keeps its whole content set in one Harmonix ARK archive pair, and the
optional **Rock Band Blitz Ultimate** payload ships a second pair that overlays it. Both
live in the dumped game folder:

| Source | Files | Entries | Data |
| --- | --- | --- | --- |
| base title | `gen\main_xbox.hdr` + `gen\main_xbox_0.ark` | 1613 | 361,769,177 B |
| Ultimate overlay | `ultimate\gen\patch_xbox.hdr` + `ultimate\gen\patch_xbox_0.ark` | 62 | 14,171,207 B |

The rest of the dump is loose and needs no unpacking: `default.xex` (the executable),
`nxeart` (dashboard art), `ArcadeInfo.xml` and `charnames.zbm`. The DLC packages under
`dlc\` are STFS containers that the title mounts where they lie — see [dlc.md](dlc.md);
they are not ARK archives and this tool does not open them.

## Extracting

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\extract_assets.ps1
```

The base archive is unpacked into `assets\game\360\` first and the Ultimate patch is then
written over it in place, so the result is the file tree the title reads with Ultimate
installed. The folder is gitignored (`/assets/game/`) — like `game\`, it is retail content
and is never committed. Flags: `-Clean` (wipe the tree first), `-SkipUltimate`,
`-GameRoot` / `-OutDir` (point at another dump or output path), `-DryRun`.

The reusable half is [scripts/hmx_ark.py](../scripts/hmx_ark.py), a dependency-free
reader for these archives that takes any `.hdr`:

```powershell
python scripts\hmx_ark.py list    --hdr game\gen\main_xbox.hdr           # index, by type
python scripts\hmx_ark.py list    --hdr game\gen\main_xbox.hdr -v        # every entry
python scripts\hmx_ark.py extract --hdr game\gen\main_xbox.hdr --out assets\game\360 `
                                  --manifest assets\game\360\_ark_main_xbox.json
```

`extract` writes the index as JSON (`--manifest`), replaces existing files only with
`--overwrite`, and reports what it would do with `--dry-run`. It reads the archive
header of versions 2–6 and 9 (Rock Band / Guitar Hero / Amplitude family); this title is
version 6.

## What lands where

| Root | Files | Content |
| --- | --- | --- |
| `songs\` | 963 | per-song `.milo_xbox` scenes and `.mid` charts; the small `.moggs` |
| `track\` | 278 | note-highway, venue and world scenes |
| `ui\` | 274 | menus, results, calibration scenes |
| `config\` | 41 | engine configuration DTAs, input maps, scoring tables |
| `sfx\`, `world\`, `flow\` | 17 | sound effects, world data, script flow |
| `dotdot\` | 41 | entries whose in-archive path starts with `../..` (see below) |
| `ulti\` | 30 | the Ultimate payload's own scenes and configuration |

`xbox_shaders` and `xbox_preinit_shaders` sit at the root because that is where the ark
puts them, and `_ark_main_xbox.json` / `_ark_patch_xbox.json` are the two indexes.

## Format notes

- **The index is encrypted, the payload is not.** A `.hdr` starts with a little-endian
  dword that seeds the same Park–Miller stream cipher the title uses elsewhere
  (16807 · *key* mod 2³¹−1, writing successive low bytes). Everything after that dword
  is XORed with the stream; the decoder picks the complemented stream when the version
  field comes out negative. An unencrypted header starts with its version (`≤ 10`) and is
  read as-is.
- **Version 6 layout.** After the version: a 20-byte hash block, the ark part count and
  sizes, the part paths, a per-part checksum block, the string blob, the string-offset
  table, then one fixed-size entry per file (offset, name index, folder index, stored
  size, decompressed size). Entries whose decompressed size is non-zero are deflated;
  Blitz has none, so every extracted file is a straight copy of its ark bytes.
- **Strange entry paths.** Two entries start with `./`, and 41 more carry the build
  tree's `../../system/run/...` prefix. A `..` component is written as `dotdot\` — the
  convention other ARK extractors use — so an entry can never escape the extraction
  root while nothing is dropped and every path stays unique.
- **Not everything is readable yet.** `.mid`, `.mogg` and `.bik` come out as plaintext and
  are immediately usable, and the `.png_xbox` / `.bmp_xbox` textures have a decoded
  envelope (the section below). `.dtb` (compiled DTA) still carries the engine-side
  `dtbcrypt` layer, and the pixel data inside a `.milo_xbox` is chunked *and* compressed
  — see "Texture swapping" for exactly how far that goes.

## Textures

A `*.png_xbox` / `*.bmp_xbox` entry is a 32-byte envelope followed by the mip chain:

| Offset | Meaning |
| --- | --- |
| 0 | envelope version (1 or 2) |
| 1–4 | per-file id (version 2 only) |
| 5 | format word: `0x0804` = DXT1, `0x1808` = DXT5, `0x0824` = two-channel normal |
| 10 | mip level count − 1 |
| 11, 13 | width, height |
| 16 | linear size of the chain |

The data itself is the usual DXT block layout with every 16-bit word byte-swapped — the
Xbox 360 GPU reads it that way. [scripts/hmx_tex.py](../scripts/hmx_tex.py) decodes and
re-encodes that: pure-Python DXT1/DXT5 with mip generation, no third-party codecs.

```powershell
python scripts\hmx_tex.py info   assets\game\360\ui\image\gen\esrb_keep.bmp_xbox
python scripts\hmx_tex.py export assets\game\360\ui\image\gen\esrb_keep.bmp_xbox art.png
python scripts\hmx_tex.py export assets\game\360\ui\image\gen\esrb_keep.bmp_xbox art.dds
python scripts\hmx_tex.py import art.png --template assets\game\360\ui\...\esrb_keep.bmp_xbox `
                                      --out new.bmp_xbox
```

`.dds` export/import is bit-exact (the raw block data with a DDS header), so it is the
lossless round trip; `.png` is for painting in an image editor and lands around 33–39 dB
PSNR on round-tripped art. `swap` is `import` plus the archive write:

```powershell
# small overlay archive: gen\patch_xbox.hdr + _0.ark, the title's own patch-ark route
python scripts\hmx_tex.py swap art.png --template assets\game\360\...\esrb_keep.bmp_xbox `
                                       --hdr game\gen\main_xbox.hdr --patch-dir out\mod
# full-size copy of the base archive with just this entry replaced
python scripts\hmx_tex.py swap art.png --template assets\game\360\...\esrb_keep.bmp_xbox `
                                       --hdr game\gen\main_xbox.hdr --out out\main_xbox_0.ark
```

A swap keeps the envelope, the format, the size and the mip count — the entry has to stay
byte-for-byte the same length, because the archive index records both its offset and its
size. Only the replaced entry's bytes are rewritten in the full-size copy; every other
entry is copied untouched.

Both routes reach the title: replacing `gen\main_xbox_0.ark` in the dump, or dropping the
same file into a payload directory the overlay merges (its boot line then reports
`1 payload copies preferred`, and a trace shows the title reading the merged path).

What these entries buy you is narrower than it looks. Replacing all 241 of them with
solid magenta leaves the title screen, the main menu and the song list **pixel-identical**,
because the screens you can reach draw almost none of them — the menu, button and layout
art belongs to `ui/**/*.milo_xbox` scenes instead, and the next two sections are about
those.

### Which textures the title actually draws

Verified by replacing **all 241** `*.png_xbox` / `*.bmp_xbox` entries of the base archive
with solid magenta — in the archive the dump actually serves — then booting to the title
screen, the main menu and the song list and comparing screenshots: **pixel-identical**
(not one pixel differs by more than 40/255 on any of the three). A trace of the same route
(`--log_noisy=true --log_level=trace`) explains why — of the 165 distinct archive entries
the title reads, only four are standalone textures:

| Entry | Read as |
| --- | --- |
| `ui/image/gen/esrb_keep.bmp_xbox` | 1024×512 DXT1, ESRB badge |
| `ui/upsell/img/gen/upsell_04_keep.bmp_xbox` | 1024×1024 DXT5, store upsell art |
| `ui/powerup_select/img/gen/synchrony_keep.png_xbox` | 256×256 DXT5, power-up icon |
| `ui/recommendations/img/gen/icon_more_stars_keep.png_xbox` | 512×512 DXT1, panel icon |

The rest of what is on screen comes out of `ui/**/*.milo_xbox` scenes. A scene is a
`ChunkStream`: a 0x810-byte little-endian `ChunkInfo` block (stream id, chunk count,
largest decompressed chunk, one word per chunk) followed by the chunk payloads, where
bit 24 of a chunk word means "stored decompressed" and everything else decodes as
**raw deflate with its size in front**. `splash.milo_xbox` is 2.3 MB on disk and
10.4 MB decompressed; its first chunk is stored plaintext and holds the scene's object
directory — `Tex` and its 13 `.tex` assets (`splash_logo.tex`, `list_panel.tex`, …),
`Mat` and the materials, `Group` and the groups — which is what tells you which screen
owns which texture. [scripts/hmx_milo.py](../scripts/hmx_milo.py) reads all of that:

```powershell
python scripts\hmx_milo.py info assets\game\360\ui\splash\gen\splash.milo_xbox --out-dir out\milo
python scripts\hmx_milo.py list assets\game\360\ui\splash\gen\splash.milo_xbox --textures
python scripts\hmx_milo.py dump assets\game\360\ui\splash\gen\splash.milo_xbox --chunk 3 --out chunk3.bin
python scripts\hmx_milo.py records assets\game\360\ui --root assets\game\360   # the map below
```

### The map: which scene owns which UI art

Every texture in a scene is serialised as a length-prefixed **builder-side source
path** followed by its dimensions — three big-endian 16-bit values, width, height and
row bytes, with the bits per pixel following from `row_bytes * 8 / width`. `records`
walks any scene (or a whole tree) and prints them, which answers the one question a
reskin starts with: *which file is this screen's art?* Across the dump it finds
**706 texture records in 151 scenes**; the ones the UI work cares about:

| Scene | Texture | Size |
| --- | --- | --- |
| `ui/controller_config/gen/controller_config` | `img/xbox_0.png` … `xbox_3.png` | 1024×1024, 8bpp |
| `ui/controller_config/gen/controller_config` | `img/ps_0.png` … `ps_3.png` | 1024×1024, 8bpp |
| `ui/resource/fonts/gen/buttons` | `../image/icons_buttons_xbox_nomip.bmp` | 512×512, 8bpp |
| `ui/resource/fonts/gen/blitz_icons` | `../image/blitz_icons_xbox_nomip.bmp` | 1024×512, 8bpp |
| `ui/calibration/gen/cal_auto` | `../image/button_a_calibrate_icon0001/2.bmp`, `button_press_ps30001/2.bmp` | 256×256, 8bpp |
| `ui/store/gen/store` | `img/ps_1080.bmp` | 512×128, 8bpp |
| `track/cached_subdirs/gen/track_shared_textures` | `icon_*` / `*_chevron` / `*_watermark` (guitar, drums, bass, keys, mic), `help_arrow`, `multiplier_arrow` | 128², 128×256, 256×64, 64×64 |

So the A/B prompts on every menu come from **`buttons.milo_xbox`**, the controller
diagrams from **`controller_config.milo_xbox`**, and the in-game lane icons from
**`track_shared_textures.milo_xbox`** — each an 8-bit (one byte per pixel) image, and
each reachable by editing its scene inside the archive.

### Where the art would have to live, and a retraction

**Earlier revisions of this document claimed a working reskin here. That claim is
withdrawn.** The section below records what was actually measured, what was wrong with
the measurement, and what is left standing.

The withdrawn claim was that zeroing the region below removed the button prompts, and
that writing the PS3 build's sheet over it changed the A glyph. Both were pixel-diff
comparisons between two runs, and both were false positives: **the title's backgrounds
are animated**, so two runs of the *same* build differ by 3.5?5.9% of the frame, and the
prompt band they were reading is the noisiest part of it. Re-run properly ? a same-session
baseline, the region zeroed, and OCR of the prompt band as the control ? the prompts are
unchanged (`SELECT BACK` reads the same in both runs). The apparent glyph change was the
aurora behind it drifting.

Two lessons worth keeping, because they cost a whole round:

- A run-to-run pixel diff is only evidence if the noise floor is known. Measure it by
  running the *same* build twice; on these screens it is 3.5?5.9% of the frame for a
  capture taken just after a screen is entered, and ~0.14% for one taken after the screen
  has settled for a few seconds.
- The prompt band is the worst possible place to compare, because the animated background
  runs through it. OCR of the static text is immune to that and should be the control
  (`scripts/ocr_image.ps1`), with the pixel diff as corroboration rather than proof.

What *is* established, by the run logs rather than by screenshots:

| | |
| --- | --- |
| Delivery route | A payload directory with `gen/main_xbox_0.ark` **is** preferred over the game's ? `ultimate: overlay ? 1 payload copies preferred`. This is the route a reskin must use. |
| Not the patch ark | A payload carrying only `gen/patch_xbox.hdr` + `gen/patch_xbox_0.ark` mounts but logs **0 payload copies preferred**; on its own it does not override the scene. Rock Band Blitz Ultimate ships both, so the full-ark copy is what does the work. |
| The region is not the prompts | Zeroing `buttons.milo_xbox` bytes `0xDFC`?`0x40DFC` changes nothing on screen (same-session A/B, OCR identical, ~the 0.14% noise floor in the band). Whatever that region is, it is not what draws the footer prompts. |
| The two builds differ here | Comparing the 360 and PS3 scenes byte for byte, the 360 `buttons.milo_xbox` and its PS3 twin differ across **stream bytes `0x5C9`?`0x405DF` ? 262,167 bytes, which is a 512?512 sheet plus 23**. That span is the only large platform-varying region in the scene, and it is where a reskin has to aim. |

Note the units: that span is in *stream* coordinates (the concatenation of the scene's
decompressed chunks). Chunk 0 is stored and begins at file offset `0x810`, so the span is
file `0xDD9`?`0x40DEF`. Earlier revisions compared a file offset against a stream offset
here, which is part of how the wrong region was chosen.

What the bytes in that span *are* is still open, and the honest state is that the pixel
region has to be pinned by a method that does not rely on a screenshot diff:

- The span is a 512?512 sheet by size (262,144 bytes), reached from the record whose
  builder source path is `../image/icons_buttons_xbox_nomip.bmp`, declared 512 ? 512.
- It is **not** a linear 8-bit image: a byte-stride probe across it finds no row pitch
  (mean |b[i] ? b[i+pitch]| sits at 55?60 for every pitch from 1 to 512, where a real row
  stride drops well below), and its ink profile is flat where a glyph atlas would clump.
- The strongest structural hint is that the pitch-**16** neighbour difference is the lowest
  of any pitch tested (26 against 60 at pitch 1 and 84 at pitch 8), i.e. the data has
  16-byte elements ? which is what a DXT5/BC3 block is, and matches the platform
  relationship: for the pad art in `controller_config`, `swap16(360 stream)` equals the PS3
  stream **byte for byte over 1,398,101 bytes**, which is what two builds storing the same
  16-bit endpoint values in opposite endianness look like.
- On `controller_config`'s pad texture a mip-chain test agrees: decoding level 0 as DXT5 at
  1024?1024 and comparing it with the half-scaled decode of level 1 gives a correlation of
  **0.85?0.87**, and the value alternates cleanly with a 16-byte period (0.22 at a 4-byte
  shift, 0.85 at 12, 0.22 at 20, 0.86 at 28) ? 16-byte blocks with a real mip relationship
  across a 1,048,576-byte level.

So DXT5 is the leading hypothesis, not a confirmed format: it is consistent with the block
size, the endianness relationship and the mip chain, but the earlier rejection of "DXT"
rested on a write-back that is now itself in question, and nothing yet decodes the sheet
into recognisable glyphs. The next attempt should confirm it the way this round should
have: write a DXT5 encode of a flat colour into the span and check with a settled-frame
A/B and OCR, not with a whole-frame diff.

### The rest of the scene corpus

`ui/**/*.milo_xbox` scenes own the visible art — `ui/background/gen/background_night.milo_xbox`
holds the splash art (`night_gradient.tex`, `northern_lights.tex`, `rays.tex`, … plus
`img/star.png`), `splash.milo_xbox` holds `list_panel.tex` and `splash_logo.tex`, and
`splash_logo_element.milo_xbox` holds `splash_elements.tex`. Their pixel data follows the
same rule: look for a stored chunk and a 512-multiple region, not for a DXT signature —
the DXT and image-smoothness scans listed below were all red herrings for this title.

Ruled out on the scenes, so a later attempt does not repeat them:

- The RB3 inline bitmap shape (`RndBitmap::LoadHeader`: revision byte, bits per pixel,
  byte order, mip count, dimensions, row bytes, reserved bytes, mip chain) matches
  nothing, nor does any descriptor that merely puts a power-of-two width and height next
  to matching row bytes.
- No region decodes as a **linear** DXT texture; the endpoint signature of a DXT block
  (in real art the two 16-bit colour endpoints of every 8-byte block are close) scores
  4,500 at best in 10.4 MB of `splash.milo_xbox`, where real art scores in the hundreds.
  Byte order does not change that.

Read straight off the bytes, each texture is a record with `width`, `height`, `bpp` and a
length-prefixed builder-side source path — `splash.milo_xbox` names
`img/list_neon_outer.png`, `img/list_panel.png`, `img/start_inner_neon.png` (512×512 and
512×128 at bpp 8), and no such entry exists in either archive.

### Can a UI texture be swapped today?

**No ? and the previous revision of this section said yes.** What that claim rested on is
withdrawn above; what is left is a much smaller, but solid, set of pieces.

**Ready.** Reading the archive and writing a whole replacement entry to it, delivered
through a payload directory (the run log confirms the payload's `main_xbox_0.ark` is the
copy the game uses). Finding candidate art: the `records` map above gives which scene, which
builder-side source path and what dimensions. Reading a scene's chunk stream and rebuilding
one whose chunks are compressed.

**Not ready.** Everything between "this scene, this record" and "these bytes are that
texture". There is no tool that turns a record into a pixel buffer or back, because the
buffer's format and offset inside the scene are not pinned. That is the blocker, and it is
the only one: delivery, sizing and archive surgery are all solved.

The way through it is a measurement that the animated backgrounds cannot fake, and the two
that qualify are:

- **A control whose signal is text.** `scripts/ocr_image.ps1` reads the prompt band and is
  unaffected by the background. It is what showed the old claim was wrong.
- **A settled-frame A/B with a known noise floor.** Capture the screen, wait several
  seconds, capture again, and use the second pair ? the background drifts most just after a
  screen is entered. The floor still has to be measured against a same-build baseline first.

With those in hand, the experiment to run next is small: write one flat DXT5 colour over the
262,167-byte span the two builds differ in, and see whether the prompt band changes at all.
That answers "is this span the shown texture" and "is the format DXT5" in a single run,
which is one run more than the withdrawn claim ever justified.

A practical note that survives: on the five scenes checked (`buttons`, `splash`,
`background_night`, `content_refresh`, `player_state_sounds`) the first chunk is stored
decompressed, so a patch to the start of a scene needs no recompression ? but a scene whose
edit does not fit is better delivered as a whole replacement entry, which the archive format
and the payload route both allow.

## PS3

The PlayStation 3 build is the same game with the same content pipeline, which makes it
both useful and, for the parts that already work, free:

```powershell
.\scripts\extract_assets.ps1 -Platform ps3 `
    -GameRoot "C:\Games\Emulators\RPCS3\dev_hdd0\game\NPUB30749\USRDIR"
```

| | 360 | PS3 |
| --- | --- | --- |
| Base archive | `gen\main_xbox.hdr` + `_0.ark`, 361,769,177 B | `gen\main_ps3.hdr` + `_0.ark`, 353,726,603 B |
| Overlay | `ultimate\gen\patch_xbox.hdr` (62 entries) | `gen\patch_ps3.hdr` (62 entries, 33 added / 29 replaced) |
| Entries | 1,613 | 1,616 |
| Extracted | 1,648 files, 353.8 MiB | 1,651 files, 346.4 MiB |
| Archive format | ARK v6 | **ARK v6** — same reader |
| Texture envelope | 32-byte v2, DXT1/DXT5 | **identical** — same reader |

Every tool in this repository reads the PS3 dump **unchanged**: `hmx_ark.py` lists and
extracts `main_ps3.hdr` as-is, `hmx_tex.py` decodes `esrb_keep.bmp_ps3` / `upsell_04_keep.bmp_ps3`
to the same formats, sizes and mip counts as their 360 twins, and `hmx_milo.py` reads the
PS3 scenes' chunk streams and texture records. The only platform-specific entries are the
shader blobs (`ps3_shaders` / `ps3_preinit_shaders` vs `xbox_shaders` /
`xbox_preinit_shaders`), two `dorkage.fpo` / `.vpo` shaders, and `mc/gamedata/icon0.png`.

The two builds also ship **the same art**: `esrb_keep` is 84.7% byte-identical between
platforms and decodes to the same image (mean channel difference 13/255, i.e. a per-platform
DXT re-encode of one source bitmap). So a PS3 texture is a good reference for what a 360 one
is supposed to look like, and vice versa.

### What this does and does not buy the launcher

Both builds ship **both pad families**: `controller_config` carries `img/xbox_0..3.png` and
`img/ps_0..3.png` (1024×1024 each) on 360 *and* on PS3, so the launcher's Controller page can
show the real pad whichever the player has, out of either dump. The button prompts are the
same story — one 512×512 sheet in `buttons` on both.

What PS3 does **not** do is make the button and controller art any easier to read. Those are
scene-embedded on both platforms (neither dump has a standalone `buttons`/`controller`
texture; the 241 standalone `*_ps3` entries are the same set the 360 has), and the scene
texture layout is the same unsolved detail on both — see "Can a UI texture be swapped
today?" above.

### Editing a scene with PS3 data

The PS3 build is worth keeping for the platform relationship alone ? it is the same game in
the same container, so the two builds are a controlled comparison of exactly the bytes a
reskin has to change.

Measured on `controller_config`, whose pad art is the same picture on both platforms:

- `swap16(360 stream)` equals the PS3 stream **byte for byte across 1,398,101 bytes** at the
  pad record. Both builds store the same values with the 16-bit words in opposite order,
  which is what an endianness difference in 16-bit texture elements looks like.
- On `buttons`, whose art genuinely differs, the same swap reaches **63%** agreement against
  23% left alone. The gap is the size of the difference a platform-specific sheet carries:
  the two sheets are laid out the same but not drawn the same.
- The 360 and PS3 `buttons.milo_xbox` files are the same size (266,490 B), their object
  records are 99.5% identical, and the only large platform-varying span is the 262,167-byte
  sheet region described above.

**What a swap still needs, and what the engine refuses.** Writing a scene of a different
size is not the obstacle it looked like: the payload ark route replaces whole entries and
the archive index carries per-entry offsets and sizes, so a rebuilt scene can be any size.
The obstacle is the texture: until the sheet's layout inside that 262,167-byte span is
pinned by a measurement that survives the animated background, a replacement cannot be
authored. That is the whole of the remaining problem, and it is a parsing problem, not a
delivery one.

Recorded so it is not tried again: a deflate chunk that is recompressed but overruns its
stored size cannot be padded back ? see the chunk-stream note in "The rest of the scene
corpus" ? so an edit that sits in a compressed chunk has to fit or the entry has to be
replaced wholesale, which the payload route makes possible.

### PS3 as a decoder oracle

The PS3 build is worth keeping for one more reason: it is a **second sample of the same
asset**, from an independent build, at the same offset. Measured on `buttons`:

- The scene files are structural twins: byte-identical sizes (266,490 B), the object records
  are **99.5% identical**, the chunk table is identical, and the texture record carries the
  same source path (`../image/icons_buttons_xbox_nomip.bmp`) and the same 512×512 at 8 bpp.
- Only the pixel region differs — and it differs as a **16-bit word swap**: 70% of it matches
  the other build after swapping byte pairs, against 20% left alone. The match is
  alignment-sensitive (it collapses to 5% at the wrong offset), so it is real structure, not
  a coincidence of low-entropy bytes. That constrains the format to **16-bit elements**, which
  is the most useful thing known about it.
- The region is **raw, not compressed**: no zlib / raw-deflate / gzip / lzma framing applies,
  and it is highly compressible (deflate gets it to 20% of its size).
- No glyph-grid periodicity shows up at cell-sized lags (the only autocorrelation peaks are at
  the smallest lags, which just means neighbouring samples are similar), and the region
  decodes as neither DXT1 nor DXT5 in either byte order — on both platforms.

So the next attempt has an oracle the last one did not: find a decoding that reads *both*
copies, or use the 70%-identical majority to separate "this is the image" from "this is a
re-encode". That belongs with the rest of the open problem in
[backlog.md](backlog.md).

## The texture record, pinned

The layout below is read off MiloLib (`MiloLib/Assets/Rnd/RndBitmap.cs` and `RndTex.cs`)
and then **confirmed against this title's bytes** rather than assumed. It is the answer to
"where does the pixel buffer start", which earlier revisions of this document called the
one open question.

```
RndTex:    combinedRevision(u32) [Object fields if revision > 8]
           width(u32) height(u32) bpp(u32) sourcePath(Symbol)
           mipMapK(f32) type(u32) optimizeForPS3(bool) useExternalPath(bool)
RndBitmap: revision(u8) bpp(u8) encoding(u32) mipMaps(u8)
           width(u16) height(u16) bpl(u16) wiiAlphaNum(u16)
           + 13 bytes (revision 2) or 17 bytes, then one block per mip level
level size = (w >> m or 1) * (h >> m or 1) * bpp / 8
```

`encoding` is 8 for DXT1/BC1, 24 for **DXT5/BC3**, 32 for ATI2/BC5, and it agrees with the
separate `bpp` field (dxt5 stores as bpp 8, i.e. 8 bits per pixel of block-compressed data).
The decompressed scene stream is **big-endian**; the values are read as such.

Two independent confirmations on `ui/resource/fonts/gen/buttons.milo_xbox`:

| | |
| --- | --- |
| Fields | revision 193, bpp 8, **encoding 24 (DXT5)**, mipMaps 0, **512 ? 512**, bpl 512 ? every field consistent with the others (`bpl == bpp * width / 8`) |
| Size | pixel data at stream `0x5EC`, 262,144 bytes = 512 ? 512 ? 8 / 8, and it ends at `0x405EC` ? **exactly the end of chunk 0**, which is the stored chunk. The sheet fills its chunk to the byte. |

So the older "file bytes `0xDFC`?`0x40DFC`" note was right about the *location* (`0xDFC` is
stream `0x5EC` plus the 0x810 file header) and wrong about nothing except that it was
arrived at by inference. It was never the format that was missing.

That same inference error is worth naming: **file offsets and stream offsets differ by the
0x810 file header**, and a chunk's stream offset is the sum of the *decompressed* sizes of
the chunks before it, not their stored sizes. Mixing the two silently shifts an edit.

### Which art can be edited in place, and which cannot

A scene's first chunk is stored decompressed and holds the object graph; in several UI
scenes it also holds the sheet. Everything after it is usually compressed:

| Scene | Chunks | Stored | Compressed | Decompressed | Stored |
| --- | --- | --- | --- | --- | --- |
| `buttons.milo_xbox` | 2 | 1 | 1 | 267,147 | 264,426 |
| `blitz_icons.milo_xbox` | 2 | 1 | 1 | 530,914 | 526,447 |
| `header.milo_xbox` | 11 | 1 | 10 | 3,881,542 | 804,073 |
| `background_night.milo_xbox` | 12 | 1 | 11 | 9,398,913 | 2,627,227 |

An edit that lands in a stored chunk can be written in place, because the archive index
records a fixed entry size. An edit that lands in a compressed chunk cannot be padded back
(see the chunk-stream note), so it has to be delivered as a whole replacement scene - which
the archive format allows, since a payload ark carries per-entry sizes and Rock Band Blitz
Ultimate ships scenes whose sizes differ from the originals by megabytes.

## Use the community toolchain, not a hand-rolled parser

The archive and chunk layers here agree with the community's understanding, but the
**asset** layer is solved already and hand-rolling it was a mistake worth recording:

| Tool | What it provides |
| --- | --- |
| [MiloEditor / MiloLib / MiloUtil](https://github.com/ihatecompvir/MiloEditor) | Parses Milo scenes across games. `RndTex` carries an `RndBitmap` with `width`, `height`, `bpp`, `encoding`, `mipMaps` and the per-level pixel bytes, and `RndBitmap.ConvertToImage()` renders any of them to a DDS. Its GUI has a texture viewer/exporter/importer; `MiloUtil` is a CLI (`info`, `extract`, `uncompress`). |
| [Mackiloha / ArkHelper](https://github.com/PikminGuts92/Mackiloha) | Reads and writes the ARK v6 archives ? the same format as `hmx_ark.py`, but complete. |
| [re-notes](https://github.com/PikminGuts92/re-notes) | `swap_rb_art_bytes.py`, the Xbox?PS3 texture conversion. |
| [Rock Band Blitz Deluxe](https://github.com/solamint/rock-band-blitz-deluxe) (MiloHax) | A shipped **Rock Band Blitz** mod whose optional upgrades include **Custom Textures**, built with ArkHelper. Custom art in this game is a solved workflow. |
| [rb3 decomp](https://github.com/DarkRTA/rb3) | The source MiloLib credits; where the asset format is actually documented. |

Two facts read straight out of MiloLib that this document spent a long time guessing at:

- `RndBitmap` is the texture asset ? the width/height/bpp/encoding the record declares map
  onto it directly, so there is no offset to find: parse the object graph, do not scan for a
  record.
- The Xbox 360 **storage scramble** is `[i+1][i][i+3][i+2]` over 4-byte groups
  (`RndBitmap.ConvertToImage`). That is exactly the "16-bit word swap" measured here between
  the platforms, and it is why the same art is byte-different on 360 and PS3.

`tools/milotex` is a small CLI over MiloLib (`list`, `export`, `import`) because MiloLib's
GUI cannot be driven from a script and MiloUtil's `extract` only dumps raw asset bytes. It
builds against a MiloEditor clone; see the project file for the path.

## Where the format knowledge comes from

The archive layout and the header cipher are public community knowledge, not something
this project worked out alone: [OpenNyx](https://github.com/mtolly/onyx)'s
`Onyx.Harmonix.Ark` is the implementation the descriptions here were checked against,
and xorloser's published `dtbcrypt` documents the same 32-bit generator for the `.dtb`
files. [scripts/hmx_ark.py](../scripts/hmx_ark.py) is an independent implementation of
that format in this repository's own tooling style — no code is vendored or translated —
and every claim above was re-verified against the dump rather than assumed.

## Fidelity

The extraction is byte-exact: every entry in both archives has been compared against the
ark slice it claims, and the only differences in the tree are the 29 files the Ultimate
patch is meant to replace (33 of its 62 entries are new). Reading is the only thing this
tool does to the dump — the game folder is opened read-only, and nothing written here is
committed. The PS3 tree is extracted by the same code path against `main_ps3.hdr` and its
`gen\patch_ps3.hdr` overlay, with the same 29-replaced / 33-added split.
