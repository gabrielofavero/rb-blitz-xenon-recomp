# Local shader resolution (P2, 2026-10-24)

P2's local shader-resolution criterion is now met for the current candidate:
**186/186 identities resolve from the installed content**, including all 120 pixel
and 66 vertex identities in ten distinct preserved P0 stores. The former vertex
blocker is resolved by reproducing declaration linking and extracting two bootstrap
programs from the supported executable. Resolution requires no previous gameplay or
developer cache on the user's machine. P3's catalogue now consumes this resolver for
every recipe-referenced identity ([shader-cache-p3.md](shader-cache-p3.md)); P4 must
integrate preparation. This alone does not establish live GPU readiness or
remove in-song compilation.

Implementation: [shader_asset_resolver.py](../../scripts/shader_asset_resolver.py),
[SDK hash helper](../../tools/shader_microcode_hash.cpp),
[vertex linker](../../scripts/shader_vertex_link.py),
[bounded XEX source reader](../../scripts/shader_xex_source.py),
[layout recipes](../../config/shader_vertex_layouts.json),
[hash-only candidate](../../config/shader_resolution_candidate.json),
[hermetic tests](../../tests/shader_asset_resolver_tests.py).
This is offline tooling; the game does not invoke it yet. Integration with the
preparation operation belongs to P4. No SDK changes or game/UI runs were needed.

## Reproducing the inspection

Build first:

```powershell
cmake --build --preset win-amd64-release
python scripts/shader_asset_resolver.py --game-root game --output out/shader-cache-p2/resolved-retail-index.json
python scripts/shader_asset_resolver.py --game-root game --effective-overlay-root game/ultimate --output out/shader-cache-p2/resolved-ultimate-index.json
ctest --test-dir out/build/win-amd64-release -R '^shader_asset_resolver' --output-on-failure
```

Use a **new** output path for each invocation. The tool refuses to overwrite an
existing file. Exit 0 means every requested candidate identity resolved; exit 2
means unresolved coverage or unavailable vertex sources; exit 1 means invalid
input or an I/O/tool failure. The offline tool uses Python 3.11+, PyCryptodome (also
used by the existing XEX tools), and the SDK's existing key source. It never logs
keys or writes a decoded executable to disk.

`--effective-overlay-root` names the provider actually mounted by the runtime's
`ultimate::Configure`, rather than a requested Ultimate mode. Omit it for an
unoverlaid retail root or a root with a merged patch archive. The reader follows
overlay-before-base host-file precedence, then patch-before-main archive-record
precedence, including part files selected through the same host union. It reads the
two shader assets directly from their installed ARKs; it does not depend on the
developer's extracted `assets/game/360` tree. It records the selected header hashes,
part paths/offsets, asset and executable SHA-256 values, a combined content
fingerprint and a separate linkage-recipe fingerprint.
Identical effective shader content has the same fingerprint even if its provider
or installation path differs. Future runtime callers should supply the actual
mounted sources, including any additional content provider, rather than infer them
from a target label. External title updates, DLC and custom shader providers are
not certified by these two base assets.

By default it verifies the committed 186-identity candidate. `--catalogue path.json`
can select another shader-identity request list with this shape:

```json
{"schema":1,"title_id":"5841122D","shaders":[{"stage":"pixel","hash":"f920af5cd865655e"}]}
```

This is a shader-identity request list, not P3's pipeline-recipe catalogue. The tool
reports each request as resolved or unresolved. An empty list is not complete.
`--runtime path.xsh` (repeatable) validates local captured evidence, including the
SDK format version and every microcode hash. It compares resolved bytes with the
capture byte for byte. Runtime stores are **never** a resolution source. Neither
command emits `.xsh` data, imports records into the player's cache, or bundles retail
shader bytes. The JSON contains hashes and source locators, not code.

`Resolver.resolve(stage, hash)` returns verified bytes from the current parsed
sources and linkage recipes or `None`; it never substitutes an unlinked template.
Persisted index locators are
evidence only: no API trusts or executes them. Rebuilding the index reparses and
fingerprints the current content. `live_ready` is always false; GPU preparation and
process residency are separate work.

## Established asset layout

The framing blocker in the earlier investigation came from inspecting the **base
class** parser, `sub_8224DC70`. The actual D3D shader-cache vtable points to
`sub_823EC540` at offset +92 (the table near guest `0x820783D8`). Its read order is:

```text
XOBX                           4 bytes
version = 1                    little-endian u32
named_entry_count              little-endian u32
for each named entry:
    name_length, name          little-endian u32, then ASCII bytes
    permutation_count          little-endian u32
    pixel_object_bank_size     little-endian u32
    pixel_code_bank_size       little-endian u32
    pixel_object_bank
    pixel_code_bank
    vertex_object_bank_size    little-endian u32
    vertex_code_bank_size      little-endian u32
    vertex_object_bank
    vertex_code_bank
    for each permutation:
        key                    little-endian u64
        pixel_object_offset    little-endian u32
        pixel_code_offset      little-endian u32
        vertex_object_offset   little-endian u32
        vertex_code_offset     little-endian u32
```

Offsets are bank-relative, aligned to 16 bytes; shared bank offsets are allowed.
The earlier supposed summed vertex/pixel strides are the first pair of **bank
lengths**. The name scan also missed `multimesh` and `standard`, so its 21-name
estimate was incomplete. The parser now consumes both files to EOF: 23 named entries
and 2,098 permutations in `xbox_shaders`; 22 and 89 in `xbox_preinit_shaders`.

The object banks contain guest big-endian D3D metadata. A pixel object's program
header is at +40, a vertex object's at +872. Their version words are respectively
`0x102A1100` and `0x102A1101`. Header +4 gives metadata length; +8 gives data-buffer
length; +24 points to a descriptor within that metadata. The descriptor's first
two big-endian dwords give the microcode offset and byte length within the selected
code-buffer slice. This indirection matters: bloom's pixel buffer has a 64-byte
constants prefix; hashing the whole buffer or header +8 bytes gives the wrong
identity. The helper hashes only the indicated guest-endian microcode with the same
SDK `XXH3_64bits` used by both pipeline caches.

The parser bounds the asset, entries, names, total permutations, four banks, every
table offset, object header, descriptor, data buffer and microcode slice before
reading or hashing them. It rejects duplicate names/keys, unknown versions,
trailing data, truncated records and hash collisions. ARK reads and decompression,
runtime `.xsh` records and the helper's binary input are bounded too. Its 32 MiB
asset and 64 KiB microcode limits deliberately reject unsupported larger content
rather than assuming coverage.

## Measurements from local assets and preserved captures

Both the retail and effective separate-Ultimate installation selected these assets:

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `xbox_preinit_shaders` | 160,557 | `959b6c14f8f7cd29fc68dc736743df10fffe05f26a0d6520d6374e30c076c608` |
| `xbox_shaders` | 5,271,431 | `70616f5aca230ae9a9be776a0a5fd32f30dc33d50c63e20c509357483b57e2e2` |

Earlier asset-only fingerprint (before vertex source integration):
`321d016c1ff7ff77acac323ee1a2df05405490dba43480d5ddb015a7f5a09a6e`.
The current fingerprint including the supported executable is
`875dac1915e208c64f5c673f0568b57d13c730b5df18ca230da7766061adc95b`.
This certifies equal shader inputs on this installation, not equal pipeline coverage
or compatibility for arbitrary Ultimate versions.

| Input | Pixel identities resolved exactly | Vertex identities resolved exactly |
| --- | ---: | ---: |
| Original P2 comparison: `d3d12-ultimate-empty-r3/inputs/.../5841122D.xsh` | 117 / 117 | 64 / 64 |
| Union of seven preserved P0 `after-cache` stores | 75 / 75 | 41 / 41 |
| Union of all ten distinct preserved P0 `.xsh` file contents | 120 / 120 | 66 / 66 |

The current index contains 565 unique pixel and 763 reconstructed vertex identities
(1,328 total), plus 178 distinct source vertex templates and provenance for all
2,187 permutations. It applies five bounded layout recipes to the source templates;
it does not infer readiness from template hashes. The broadest union
includes developer input-cache records, so it is experimental shader-identity
evidence, not a promise of exercised gameplay coverage.

Local evidence (ignored, not distributed):

- `out/shader-cache-p2/retail-index.json`: original 181-record comparison.
- `out/shader-cache-p2/ultimate-validation.json`: individual seven after-cache
  comparisons, plus their 116-identity candidate union.
- `out/shader-cache-p2/candidate-shaders.json`: that union as hash/stage-only requests.
- `out/shader-cache-p2/ultimate-index.json`: CLI resolution of those requests through
  the effective Ultimate installation, without a runtime store as input.
- `out/shader-cache-p2/all-preserved-stores.json`: each distinct store's digest,
  earlier unresolved identity result and the broadest 186-identity union.
- `out/shader-cache-p2/resolved-retail-index.json` and
  `resolved-ultimate-index.json`: successful fresh local resolution of the committed
  candidate with no `.xsh` input.
- `out/shader-cache-p2/resolved-all-preserved-stores.json`: final **byte-for-byte**
  comparison of every shader in all ten distinct stores, using the same resolver
  whose source inputs are exclusively the installation and declarative layout recipes.

## Resolved vertex declaration linking and bootstrap sources

`sub_823EC540` initializes pixel objects through `sub_82514420` and vertex objects
through `sub_82516010`, then attaches them with `sub_823EB860`. The latter does not
perform the final vertex transformation. `sub_82516010` preserves the source fetch
instructions in per-shader storage. The later D3D submission paths
`sub_82522410` / `sub_82522208` invoke **`sub_82521908`** with the live vertex
declaration and stream mapping. It matches usage/index against declaration elements
(12-byte records starting at declaration +52), rewrites fetch format, stream and
offset fields, sorts/coalesces fetches and writes the resulting instructions into
the uploaded program. This explains why matching stored tails did not recover the
runtime hashes; merely extracting the correct vertex bank is insufficient.

`shader_vertex_link.py` now reproduces the fetch rewriting, declaration component
mapping, ordering, prefetch counts and mini-fetch coalescing. It uses the source
asset's bounded fetch-slot table. Only those instructions are rewritten; the asset
supplies the rest of the shader. Layout recipes contain declaration locations and
stream strides, not replacement shader instruction words. The observed layouts are:

| Layout | Installed declaration / initialization | Stream strides in dwords |
| --- | --- | --- |
| Packed mesh | Image offset `0x80C910` | 9 |
| Unpacked mesh | Image offset `0x80C970` | 24 |
| Packed multimesh | Image offset `0x80CBF0` | 9, 1 |
| Particles | Image offset `0x80CD18` | 9 |
| Fullscreen | FVF 450 via `sub_82512730` | 7 |

The fullscreen case came from `sub_823EAB58`: it calls `sub_825161F8` with 450, then
draws with a 28-byte stride. The constructor produces XYZ, diffuse and specular
colors, and one float2 UV at byte offset 20. This accounts for the two postprocess
identities absent from the four static declaration cases. The other two missing
identities were **not** asset templates: they occur byte for byte in the executable
at image offsets `0xAA180` (108 bytes) and `0xA9D10` (96 bytes).

The XEX reader verifies `default.xex` against the project's entrypoint fingerprint
before using any address-based recipe. The key stays in its existing SDK owner.
Basic-compression block lengths, image allocation and each source read are bounded.
It parses declarations with a 32-element cap and reads the supported executable's
swizzle-mask and format-size tables. Bootstrap lengths are bounded and their exact
SDK hashes must match. Linked data is published only after source and hash checks
succeed, with a 32,768-program / 64 MiB generation cap. A second source-generation
call requires a new resolver, so an earlier content generation cannot leak through.

Candidate/layout recipes were established against preserved developer captures,
as pipeline catalogues can be. On a new installation those captures are unnecessary:
all shader bytes come from that user's assets or executable, and the linker applies
the semantic recipes. The shipped JSON contains identities, offsets and declaration
parameters, not retail shader bytes. Unknown executable revisions, malformed sources
or identities requiring another layout remain explicitly unsupported. Missing vertex
requests report `vertex_layout_not_covered`; missing pixels report
`absent_from_installed_assets`. This resolves P2 for the current candidate; arbitrary
DLC/custom declarations and complete P3 pipeline coverage remain unproven.

## Validation

Release build succeeded with `cmake --build --preset win-amd64-release`, including
the SDK hash helper, `rb_blitz.exe` and both renderer DLLs. The build reports the
existing non-frozen local toolchain (Clang 19.1.7 and CMake 4.4.0).

The `shader_asset_resolver` ctest passes nineteen hermetic tests: every byte truncation
of a synthetic asset; malformed counts, versions, stages, descriptors and offsets;
SDK hash/endianness and protocol checks; collision refusal; constant prefixes;
duplicate/shared records; content fingerprint changes; stage-aware unresolved
coverage; XSH corruption; and real synthetic retail/overlay/merged ARKs including
truncation, duplicate entries and bounded decompression; CLI failure/blocker exit
codes and refusal to overwrite evidence; synthetic declaration linking, sorting,
mini-fetch coalescing, unpublished failures, FVF construction, and bounded plaintext/
encrypted basic-XEX decoding. No retail shader blobs are in the fixtures. Separate
`shader_asset_resolver_retail` and `shader_asset_resolver_ultimate` tests verify the
committed candidate directly from installed content when those installations exist.

The full release ctest run passed **54/54** with `TEMP` and `TMP` set to the writable
workspace directory `out/_swap/p2/test-temp`. With the default temporary location,
the unchanged `path_policy` test fails at `tests/path_policy_tests.cpp:88` for a
not-yet-created descendant path; an isolated rerun reproduced it there and passed
with the workspace temporary directory. No path-policy code was changed.
