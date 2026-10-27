# Native song folders as Blitz DLC — proof and implementation plan

**Status: planning, 2026-10-26.** No native folder loader, build, or guest/UI
test was performed for this plan. The sample inventory below is static inspection
of three owner-supplied folders; whether any of them plays in Blitz is unproven.
The goal is to use those folders directly at runtime, without making CON files or
requiring the player to run a converter.

## 1. Request and success criteria

Accept Clone Hero and Phase Shift style song folders in a configured local library.
Expose each recognized folder to Blitz as one read-only DLC item, translate only
what the guest needs, and use the charts for instruments actually present. The
source folder remains the authority for edits and is never repackaged as STFS.

| Id | Requirement |
| --- | --- |
| R1 | A folder with `song.ini`, `notes.mid` or `notes.chart`, and playable audio can appear in Your Songs through the title's normal DLC enumeration. Do not require a CON, a manual conversion step, or a parallel song-selection UI. |
| R2 | Use only authored compatible guitar, bass, drums, vocals and keys charts. Ignore other formats by default; do not invent notes for missing parts or silently map rhythm/co-op to keys. Report what was used and skipped. |
| R3 | Play through the guest's normal song-loading, timing, scoring, pause and exit paths. Single stereo audio is acceptable, but do not claim instrument-isolated audio from a mix. |
| R4 | Keep existing STFS and extracted RB content, songcache, refresh, menu music and store ownership behavior correct in mixed libraries. Give each folder a stable, collision-safe DLC identity and song ID. |
| R5 | Do not modify or delete source folders through a guest content API. Reject malformed or incomplete folders with a reason and keep the rest of the library usable. |
| R6 | Measure first-use, changed-folder and warm-cache cost separately. No full audio decode, re-encode or chart rebuild on each warm boot; no conversion work on the render/audio thread. |
| R7 | Use guest-authored UI for any eventual player-facing status or setting. No new host-side screen overlay is part of this plan. |

**Exit:** the three samples are independently discovered, the authored supported
parts are available, at least one full song of each viable type completes without
audio/chart drift or a load fault, and the mixed-library performance gates in §6
pass. A file appearing in Your Songs alone is not proof of playability.

## 2. Evidence and unknowns at planning time

### 2.1 Current content path

- **[tree, 2026-10-26]** [ContentPackage](../../../rexglue-sdk/src/system/xam/content_manager.cpp)
  mounts a directory through `HostPathDevice` and a file through
  `StfsContainerDevice`. `ListContent` can enumerate directory items, and
  `ExtraContentItem` can map a stable guest item name to a host path
  ([content_manager.h](../../../rexglue-sdk/include/rex/system/xam/content_manager.h)).
  This proves a useful mount seam, **not** that raw Clone Hero files are playable.
- **[tree]** The current flat-library scanner
  ([dlc_library.h](../../../src/fs/dlc_library.h)) reads STFS identity headers.
  The title's package metadata reader looks for `songs/songs.dta` or equivalent
  ([dlc_song_metadata.cpp](../../../src/fs/dlc_song_metadata.cpp)); menu music
  expects a `.mogg` path ([shell_music.cpp](../../../src/hooks/shell_music.cpp)).
  Raw `song.ini`, `notes.chart` and `song.ogg` do not satisfy those paths.
- **[tree]** The store's Downloaded index reads mounted package song metadata,
  not folder names ([store_library.h](../../../src/store/store_library.h)). The
  menu-music catalogue and guest songcache are separate consumers. A loader that
  only adds an enumeration row would leave these inconsistent.
- **[recorded measurement]** [DLC guide](../../guides/dlc.md) §4.2 measured
  390 s cold discovery and 57.5 s cached boot for 1,402 packages on the owner's
  earlier library. These figures describe existing content, not a native-folder
  implementation or an expected delay for three folders.

### 2.2 Owner-supplied fixtures

**[static inspection, 2026-10-26]** Source evidence is the three original
folders under `<sample-root>`; P0 records a reproducible inventory under
`out/native-song-folders/p0/` before implementation. All audio files identify as
stereo Ogg Vorbis with `ffprobe`. The folder names are descriptive, not parsers.

| Folder | Source chart | Standard parts observed | Relevant limit |
| --- | --- | --- | --- |
| `Bon Jovi - I Want to Be Loved (PhaseShift)` | `notes.mid`, 6 tracks | Guitar, bass, drums, keys | No vocals track. `song.ini` says `diff_guitar = 0` although MIDI guitar notes exist; inspect chart, not just difficulty tags. |
| `Dio - Dont Talk To Strangers (Clone Hero)` | `notes.chart` | `[ExpertSingle]`, `[ExpertDoubleBass]`; `[ExpertDoubleRhythm]` is additional | No drums, vocals or keys; no lower-difficulty sections. Confirm Blitz's required difficulty/part policy before calling it playable. |
| `Unholy Orpheus - Undeveloped Land (RhythmVerse)` | `notes.mid`, 32 tracks | Guitar, bass, drums, vocals, keys | Also carries GHL, pro, dance, harmony and LIPSYNC tracks. The separate `GHWT Template - Final.mid` is not the selected `notes.mid`. |

The third folder follows the same `song.ini`/MIDI/Ogg structure; there is no
evidence of a separate proprietary container in this sample. Missing vocals or
drums cannot be recovered from its stereo mix. The existing optional-LIPSYNC
rejection hook ([midi_tracks.cpp](../../../src/hooks/midi_tracks.cpp)) is not a
general validator for all 32 MIDI tracks.

**Unproven:** the exact Blitz-required DTA fields and assets for a minimal
two-channel song; whether its MOGG reader accepts a seekable wrapper around these
Ogg streams; which MIDI difficulties and missing parts it will load; and whether
the guest's songcache notices a folder edit without explicit refresh. P0–P3 must
answer these with guest traces before the adapter design is fixed.

## 3. Decisions for the proof

| Id | Decision |
| --- | --- |
| D1 | Keep the original files read-only. Present one synthetic marketplace-content identity per recognized folder, with a stable name within `XCONTENT_DATA`'s 42-byte field. Preserve ordinary CON and extracted-package registration. |
| D2 | Prove the SDK's directory mount first with a known-good **extracted RB** fixture, then prove generated guest assets from an owner-supplied folder. This separates a mount failure from a chart/audio failure. The fixture is a proof tool, not a required player-side conversion. |
| D3 | Prefer a read-only virtual file view over copying whole song directories. It must support random access and truthful file lengths for `songs/songs.dta`, the song MIDI and `.mogg`; it may pass unchanged host files through. A private generated-asset cache is acceptable when necessary, but contains no CON and never becomes the content authority. |
| D4 | Use the title's normal MIDI/audio loaders and content enumeration. Do not build a second gameplay or song-selection path. If an Ogg-to-MOGG wrapper cannot meet the guest's seek/channel contract, measure and cache the required audio conversion; never encode the entire song on each boot or selection. |
| D5 | Derive supported parts from parsed chart events. A `diff_*` tag is display metadata, not proof of notes. Use a safe missing-part representation verified against the guest; do not manufacture playable lanes. |
| D6 | Make the new scanner opt-in during proof. Decide the shipping entry point after mixed-root evidence: reuse the existing DLC Location/`--dlc_library` path only if it can distinguish song folders, extracted RB packages and arbitrary directories without changing established results. |
| D7 | A failed adapter item is quarantined with a specific source-path diagnostic; it never becomes an empty DLC that blocks scanning or poisons a successful songcache. Preserve the original and allow retry after a change or explicit refresh. |
| D8 | Treat external native folders as read-only sources in the planned [song-management flow](2026-10-26-in-game-song-management-plan.md). Hide can apply; Delete must not remove the original folder unless the owner later specifies an explicit policy. |

## 4. Proof ladder and implementation packages

Each package has a stop condition. Move this plan to `in-progress/` when P0
starts; append dated evidence and any revised decision before proceeding.

| Package | Deliverable and proof | Stop/advance rule |
| --- | --- | --- |
| P0 — capture the contract | Inventory the three sources (file names, sizes, codecs, sample rates, MIDI track names and note-bearing difficulties) without changing them. Trace a known-good RB3 package's DTA, MIDI, MOGG, optional MILO/art paths, content identity and guest file opens. Save commands, manifests and logs under `out/native-song-folders/p0/`. | Advance only with a minimal guest-file contract and a known-good control whose own play path works. If a needed asset cannot be synthesized or safely reused, record it as a blocker. |
| P1 — prove directory enumeration | Register one known-good extracted RB package as an `ExtraContentItem`, without STFS. Test enumeration, opening, license, file reads, close/reopen, songcache refresh and launch. Record guest open paths and a full-song control. | If the extracted package cannot play through this path, fix the content/device seam before working on Clone Hero data. |
| P2 — prove audio view | Build a seekable two-channel MOGG view over one sample `song.ogg`, or a cached audio asset if direct wrapping fails. Validate header/channel table, random reads, sample rate, duration, start/seek/restart and A/V sync against `ffprobe`. Log guest Vorbis/MOGG failures. | Do not call the audio path proven from a header parse alone: a full play and a seek/restart must be audible and synchronized. If wrapping fails, compare cached alternatives before choosing one. |
| P3 — prove chart/metadata | Parse `song.ini`, standard MIDI and `.chart`; emit a minimal guest-compatible `songs.dta` and MIDI. Preserve tempo, time signatures, offsets, phrases and authored supported difficulties; synthesize only structural tracks the guest requires. Filter extra MIDI tracks with an explicit allowlist. Validate generated output independently of the production parser, then run one source folder through enumeration and song load. | Advance only after the song can load and a timed note/section sample matches its source chart. Record the behavior of absent parts and an Expert-only chart. |
| P4 — implement read-only folder adapter | Discover complete song folders recursively, create stable source IDs/song IDs, publish virtual package files, and cache derived assets by source path, file size/mtime, adapter version and format version. Bound parsing, allocation and file size; reject path traversal, duplicate ambiguous files and malformed events. Support cancellation/refresh and atomic cache publication. | Unit and integration tests cover valid/invalid mixed roots, collisions, edits and failure isolation. No full-media read on a warm cache hit. |
| P5 — integrate other consumers | Feed the same mounted metadata to Your Songs, store Downloaded, menu music, Play Current Song and refresh. Coordinate with guest songcache invalidation and the [song-management plan](2026-10-26-in-game-song-management-plan.md). Keep `CON` and extracted RB behavior unchanged. | A mixed library presents each item once; source changes update every surface after refresh/restart; external folders stay read-only. |
| P6 — acceptance and performance | Build the release preset, run host tests, then test all three folders in the guest and record logs/captures/timings under `out/native-song-folders/p6/`. Compare with the same root without native scanning, for cold, changed and warm boots. Exercise retail and Ultimate, song start, full play, pause, replay, menu return and a corrupt-folder neighbor. | Ship only the chart/audio combinations actually proven. Document unsupported parts or folders rather than listing them as playable. |

Host tests and builds do not navigate the game. Before P1/P2/P3/P6 guest/UI
navigation, follow the repository `AGENTS.md` owner-testing preference: ask
whether the owner wants to perform the run, wait for the answer, and build the
latest change first (`cmake --build --preset win-amd64-release`). The owner can
authorize autonomous UI driving for that scope.

## 5. Tests that decide compatibility

| Case | Required observation |
| --- | --- |
| Bon Jovi | Four authored standard parts discovered; vocals absent; DTA rank/chart availability agrees with parsed MIDI, including its misleading guitar `diff_*` tag. Intro, mid-song and ending note times match the source and audio. |
| Dio | Expert guitar/bass are recognized. Rhythm is logged as skipped unless a separately chosen mapping is implemented. Establish whether Blitz can play a song with only these standard parts; if not, exclude with a clear reason. Do not fabricate drums/vocals/keys. |
| Unholy Orpheus | Five standard parts are present; extra GHL/pro/dance/lipsync tracks neither enter the playable chart nor overflow guest MIDI metadata. Choose `notes.mid`, not the template MIDI, and verify vocal/lyric behavior only to the extent actually authored. |
| Audio | Each source's stereo 44.1/48 kHz Vorbis audio starts, streams, seeks and ends. Check latency and chart drift at several points, not just a successful decoder open. Confirm that missing stems produce no false promise of per-instrument mute. |
| Mixed and damaged roots | Existing CONs and extracted RB packages keep their identity and playback; an unrelated directory is ignored; one malformed MIDI/Ogg/INI is diagnosed without blocking healthy items. Duplicate path/ID and songcache refresh cases remain deterministic. |

Any gameplay pass records build identity, root configuration, source fingerprints,
guest log, song selection, audible/visual result and elapsed times. A failure
report names the stage (scan, mount, DTA, MIDI, audio, song start, or gameplay)
so a non-playable item is not mislabeled a container problem.

## 6. Performance gates

Measure these on the same release build and machine, with the same existing DLC
library. Do not extrapolate §2.1's 1,402-CON cold-boot number to this adapter.

| Measurement | Target to validate before shipping |
| --- | --- |
| Warm folder scan | Stat/fingerprint only. Report added time for three folders and for a synthetic 1,000-folder tree; target under 100 ms for the three samples on the test SSD. No MIDI or full Ogg read on a hit. |
| First use / edit | Record parser, guest-asset generation, optional audio processing, cache write and guest discovery separately. Run them off the render/audio thread; publish only complete items. A progress message, if needed, uses the guest's existing UI route. |
| Song selection | Measure added adapter mount time, first-audio latency and peak memory against a comparable CON. Avoid building the whole song on selection; a cache miss has a bounded, visible failure/retry path. |
| In-song | No per-frame source scan or chart conversion. Stream audio with bounded buffers; compare frame-time distribution and underruns to the same build with native scanning disabled. |
| Existing library | Cold and warm baselines remain within measurement noise when native scanning is disabled; mixed-root enumeration does not trigger duplicate mounts or repeatedly invalidate the guest songcache. |

Report actual distributions and hardware/cache conditions. The 100 ms warm
three-folder figure is a provisional product target, **not a measured result**;
revise it in this plan if a controlled run shows a different defensible limit.

## 7. Open questions

1. Which exact DTA fields, MILO/art assets and audio-channel declarations are
   mandatory for Blitz, and which can use a shared default?
2. Does the guest accept a virtual seekable MOGG over an unchanged 2-channel Ogg,
   including the 48 kHz sample, or does it require a cached transcode?
3. Which difficulty does Blitz consume, and does it accept guitar/bass-only
   content without empty rails or a song-start fault?
4. Can a virtual device be registered without changing unrelated SDK mounts,
   and will the title's content cache notice a native-folder edit on refresh?
5. How should a native song that also exists as an RB package be displayed? Do
   not collapse two charts merely because artist and title match; use exact
   source identity and record any explicit deduplication rule.

The next action is P0. Implementation and guest compatibility remain unproven.
