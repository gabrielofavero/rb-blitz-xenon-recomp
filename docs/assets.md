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

The base archive is unpacked into `extracted\` first and the Ultimate patch is then
written over it in place, so the result is the file tree the title reads with Ultimate
installed. The folder is gitignored (`/extracted/`) — like `game\`, it is retail content
and is never committed. Flags: `-Clean` (wipe the tree first), `-SkipUltimate`,
`-GameRoot` / `-OutDir` (point at another dump or output path), `-DryRun`.

The reusable half is [scripts/hmx_ark.py](../scripts/hmx_ark.py), a dependency-free
reader for these archives that takes any `.hdr`:

```powershell
python scripts\hmx_ark.py list    --hdr game\gen\main_xbox.hdr           # index, by type
python scripts\hmx_ark.py list    --hdr game\gen\main_xbox.hdr -v        # every entry
python scripts\hmx_ark.py extract --hdr game\gen\main_xbox.hdr --out extracted `
                                  --manifest extracted\_ark_main_xbox.json
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
  convention other ARK extractors use — so an entry can never escape `extracted\` while
  nothing is dropped and every path stays unique.
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
python scripts\hmx_tex.py info   extracted\ui\image\gen\esrb_keep.bmp_xbox
python scripts\hmx_tex.py export extracted\ui\image\gen\esrb_keep.bmp_xbox art.png
python scripts\hmx_tex.py export extracted\ui\image\gen\esrb_keep.bmp_xbox art.dds
python scripts\hmx_tex.py import art.png --template extracted\ui\...\esrb_keep.bmp_xbox `
                                      --out new.bmp_xbox
```

`.dds` export/import is bit-exact (the raw block data with a DDS header), so it is the
lossless round trip; `.png` is for painting in an image editor and lands around 33–39 dB
PSNR on round-tripped art. `swap` is `import` plus the archive write:

```powershell
# small overlay archive: gen\patch_xbox.hdr + _0.ark, the title's own patch-ark route
python scripts\hmx_tex.py swap art.png --template extracted\...\esrb_keep.bmp_xbox `
                                       --hdr game\gen\main_xbox.hdr --patch-dir out\mod
# full-size copy of the base archive with just this entry replaced
python scripts\hmx_tex.py swap art.png --template extracted\...\esrb_keep.bmp_xbox `
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
python scripts\hmx_milo.py info extracted\ui\splash\gen\splash.milo_xbox --out-dir out\milo
python scripts\hmx_milo.py list extracted\ui\splash\gen\splash.milo_xbox --textures
python scripts\hmx_milo.py dump extracted\ui\splash\gen\splash.milo_xbox --chunk 3 --out chunk3.bin
```

### Where the art actually lives, and what an edit does

The important result: **editing a scene inside the archive changes what the game draws.**
Verified on the button prompts, by four experiments against the same screen:

| What was written | What the main menu showed |
| --- | --- |
| nothing (retail) | the round **A** glyph and the **B** glyph (`A SELECT` / `B BACK`) |
| the region zeroed | the glyphs gone — OCR of the same screen loses `A SELECT` and `B BACK` |
| the region filled with `0xFF` | fully opaque (invisible on the white menu) — the glyphs are still drawn |
| a solid-colour DXT encode | a 4-pixel dither — the bytes are *not* DXT |

So the chain ark → scene → screen works for edited content, and the region involved is
exactly located:

| | |
| --- | --- |
| Entry | `ui/resource/fonts/gen/buttons.milo_xbox` (266,490 B, ark offset 325,896,454) |
| Region | file bytes `0xDFC`…`0x40DFC`, **262,144 bytes** — the whole tail of chunk 0 |
| Chunk 0 | 263,660 bytes and **stored decompressed** (bit 24 set), so a patch needs no recompression |
| Record just before it | a length-prefixed source path `../image/icons_buttons_xbox_nomip.bmp` and the fields 512 / 512 / 512, i.e. **512×512, row bytes 512, one level** — matching the `nomip` in that name, at 8 bits per pixel |
| Meaning | the font's glyph sheet: the scene's own `buttons.tex` and `buttons.font`, whose character set is the Xbox pad (` ABXYgclLrRSsD12345wdxa[]6789^&*(`) |

The byte value behaves as **ink coverage**, not colour: `0x00` is transparent and `0xFF`
is fully opaque, with the material supplying the colour (a `UIColor` set — `normal`,
`focused`, `disabled`, `selected` — is in the same scene). That is also why the sheet
decodes as neither DXT1 nor DXT5 (both produce a dither when written back) and why no
256-entry palette exists anywhere in the scene: the bytes are the ink samples themselves.

What is still open is the *sample-to-screen mapping*. Every probe that wrote a spatially
varying pattern came back as a single one-pixel seam rather than a scaled image, which
points at a cell grid (the font's `Glyph` rectangles — `x`, `y`, `width`, `height` per
character) rather than a plain row-major sheet; the glyph records themselves are the next
thing to read. Two of the four experiments above also show the trap: a change can be
*invisible* (opaque white on a white menu) while still being applied, so a swap has to be
checked by a pixel diff, not by eye.

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

So today: **standalone textures can be swapped** (`hmx_tex.py`, byte-verified offline,
DDS round trip bit-exact), a **scene's own bitmap can be edited and the game will use it**
(the four experiments above — the delivery chain is proven), the scenes that own the
menu, button and layout art can be read and enumerated (`hmx_milo.py`), and what is left
is the sample-to-screen mapping inside a scene's bitmap, which the font's glyph records
should settle.

Two practical notes for the next attempt. A scene patch never needs to recompress: on the
five scenes checked (`buttons`, `splash`, `background_night`, `content_refresh`,
`player_state_sounds`) the first chunk is always stored decompressed, and the bitmaps sit
in it. And a swap must be judged by a pixel diff: writing opaque white over a white menu
is applied but looks like nothing happened, which is exactly the trap this section walked
into twice.

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
committed.
