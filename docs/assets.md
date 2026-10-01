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
`1 payload copies preferred`, and a trace shows the title reading the merged path). What
was *not* observed is a swapped texture on screen — because, as the next section shows,
nothing the reachable screens draw comes from these entries.

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

The rest of what is on screen comes out of `ui/**/*.milo_xbox` scenes — and a `.milo_xbox`
*is* a `ChunkStream`: magic `0xCDBEDEAF`, then chunks that are either LZX (XMemCompress)
or marked "already stored decompressed" with bit 24 of the chunk-size word (the RB3
decompilation at [freeqaz/rb3-xenon](https://github.com/freeqaz/rb3-xenon) is the
reference for that and for the inline `RndBitmap` layout, `ChunkStream::DecompressChunk`
and `RndBitmap::LoadHeader`; nothing here is guessed). The splash screen's art is a good
example: `splash.milo_xbox` is 2.3 MB, its string table is plaintext (`splash_city.tex`,
`splash_logo.tex`, `list_panel.tex`, … 13 textures and their materials), and the pixels
behind those names are inside compressed chunks.

So today: **standalone textures can be swapped** (`hmx_tex.py`, byte-verified offline),
and the on-screen menus, buttons and layout art need one more tool — a `.milo_xbox`
reader that decompresses its ChunkStream. Its two halves both exist in this repository
already: `rexglue-sdk\thirdparty\libmspack` decodes the LZX chunks, and the chunk stream
can be re-emitted uncompressed (bit 24) so no LZX *compressor* is needed to write one
back. That is backlogged rather than half-built; see [backlog.md](backlog.md).

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
