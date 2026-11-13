# Extra song formats as Blitz DLC — YARG spike and expansion plan

**Status: research spike completed 2026-11-13; implementation remains planned.**
This substantially replaces the 2026-10-26 PC-folder-only plan. The spike inspected
YARG/YARG.Core source and this repository; it did not implement a reader, import
content, build binaries, or navigate the game. No new format is proven playable
in Blitz. The earlier three-folder inventory is retained as recorded evidence,
not a new inspection or acceptance result.

## 1. Recommendation and scope

**Build the adapters into the application, behind one default-Off Extra Song
Formats setting.** Confirm an Off-to-On change through the guest's existing
confirmation UI, explaining that boot and DLC loading may take longer. Apply
the saved choice on restart. Keep current Xbox STFS/extracted-package behavior
independent of this setting. This is a recommendation pending implementation and
performance gates, not a runtime setting added by this spike.

The shipping ladder is PC MIDI/Ogg folders, PC `.chart` and additional audio,
extracted/decrypted PS3 songs, then `.sng`/`.yargsong` containers. Share a
guest-compatible asset pipeline between readers. Raw PS3 `.pkg` installers
require a separate deferred investigation; they are not inherited from YARG.

The critical distinction is **source reader versus playable Blitz content**.
YARG parses sources into its own chart, audio mixer and renderer. Blitz runs an
Xbox guest expecting Rock Band DTA, MIDI, MOGG and platform assets. Recognizing
a folder or mounting an archive does not establish playback.

| Id | Success requirement |
| --- | --- |
| R1 | Sources appear in Your Songs through normal DLC enumeration. No CON creation, player-run converter or separate browser. Internal derived files are allowed. |
| R2 | Preserve timing and authored compatible guitar, bass, drums, vocals and keys. Derive availability from events, not only difficulty metadata. Do not invent missing notes or silently map rhythm/co-op/GHL/pro parts to standard rails. |
| R3 | Keep normal guest loading, scoring, powerups, pause, replay and results. Stereo mixes are acceptable with an explicit loss of stem isolation. |
| R4 | Keep stable package/song identities, mixed-library enumeration, guest songcache, Refresh Songs, Downloaded and menu music consistent. |
| R5 | External sources are read-only, including low-level guest file opens. A failed item must not block healthy neighbors. |
| R6 | No whole-library media decode, encode or chart generation on each boot. Bound workers, memory and I/O; keep preparation off render/audio threads. |
| R7 | Use existing guest settings, confirmation and loading routes. No new host overlay. |
| R8 | Publish a tested format/codec/part matrix; do not promise all YARG content or all PS3 DLC. |

## 2. Reproducible upstream evidence

Inspected on 2026-11-13 using shallow source checkouts outside the repository.
No upstream code/assets were added. Pin the parent and its Core submodule rather
than comparing unrelated branch heads:

| Reference | Inspected commit |
| --- | --- |
| YARG `master` | [`f5bfbe6996b4af35722d4204a93c6cc5c8f52a45`](https://github.com/YARC-Official/YARG/tree/f5bfbe6996b4af35722d4204a93c6cc5c8f52a45) |
| Its Core submodule, primary evidence | [`3beb94e526558134145bcd3e409428f759001a40`](https://github.com/YARC-Official/YARG.Core/tree/3beb94e526558134145bcd3e409428f759001a40) |
| YARG `dev`, cross-check | [`3e229311482b0ec8031eff1df875b7ad38e87159`](https://github.com/YARC-Official/YARG/tree/3e229311482b0ec8031eff1df875b7ad38e87159) |
| Its Core submodule | [`2f3900cf91aa141149d60cb122e88876e1709abb`](https://github.com/YARC-Official/YARG.Core/tree/2f3900cf91aa141149d60cb122e88876e1709abb) |

### 2.1 What YARG actually reads

| Source | Observed reader behavior | Consequence for Blitz |
| --- | --- | --- |
| PC folders | [IniBase](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/Song/Entries/Ini/SongEntry.IniBase.cs) lists `notes.mid`, `notes.midi`, `notes.chart`, and UltraStar `notes.txt`. Standard stem extensions are Opus, Ogg, MP3, WAV and AIFF. | Start with INI + standard MIDI + Ogg. Neither a MIDI extension nor YARG codec support proves guest compatibility. UltraStar is deferred. |
| Discovery | [CacheHandler.ScanDirectory/ScanFile](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/Song/Cache/CacheHandler.cs#L636) classifies folders, extracted `songs/songs.dta`, updates/upgrades, SNG/YARGSONG and otherwise tries CON. Recognized song folders stop further descent. | Classify sources deterministically instead of treating every stem/art file as a DLC candidate. Preserve our existing STFS reader. |
| Xbox packages | [CONFile](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/IO/ConHandler/CONFile.cs) recognizes CON/LIVE/PIRS and reads STFS listings; extracted packages have separate entry groups. | Already covered by our SDK; use it as the control. |
| Extracted PS3 | [UnpackedConsolePackageEntryGroup](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/Song/Cache/CacheGroups/UnpackedConsolePackageEntryGroup.cs) chooses PS3 from `.mid.edat` beneath an extracted songs tree. [UnpackedRBPKGEntry](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/Song/Entries/RBCON/SongEntry.UnpackedRBPKG.cs) rejects NPD-marked encrypted MIDI, parses decrypted MIDI bytes, and requires MOGG. | Its class name does not imply an encrypted PKG reader or EDAT decrypter. Support extracted/decrypted sources first. |
| MOGG | [RBCONEntry.ValidateMoggHeader/LoadAudio](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/Song/Entries/RBCON/SongEntry.RBCON.cs#L166) admits versions `0x0A` and YARG-specific `0xF0`; other versions fail its support test. | Do not infer retail encrypted PS3 audio support. Prove guest header, channel and seek compatibility separately. |
| SNG | [SngFile](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/IO/SngHandler/SngFile.cs) reads SNGPKG, version, metadata, offset/length listings and a mask. [SngFileStream](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/IO/SngHandler/SngFileStream.cs) exposes seekable subfiles. | Add an indexed provider feeding the PC adapter. Do not assume ZIP or unpack all audio into memory. |
| YARGSONG | [YARGSongFileStream](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/IO/YARGSongFileStream.cs) handles a YARGSONG wrapper before the SNG reader. | Needs a separate seek-correct stream layer and fixtures. Renaming to SNG is insufficient. |
| PS3 art | [YARGImage.LoadPS3DXT](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/IO/Images/YARGImage.cs#L55) swaps pairs in the payload after a 32-byte header before interpreting DXT. | Useful converter evidence, not proof that a renamed PS3 texture works in the Xbox guest. |

The dev cross-check retains encrypted-EDAT rejection and the MOGG version gate.
Its file dispatcher still selects SNG/YARGSONG or CON. **No raw PS3 `.pkg`
reader was found in the inspected branches.** Dev expands adjacent features
including UltraStar; those do not expand this plan's initial scope. These are
conclusions about the pinned source, not every launcher tool or future release.

### 2.2 Architecture worth reproducing, and reuse limits

YARG separates quick cache restore from full validation/discovery, and records
bad-song reasons. Our cache must additionally coordinate with the guest and
detect relevant edits. Do not import YARG's serialized cache or assume its
lightly validated quick restore meets this plan's requirements. See
[CacheHandler](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/Song/Cache/CacheHandler.cs).

[UnpackedIniEntry.LoadAudio](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/Song/Entries/Ini/SongEntry.UnpackedIni.cs)
loads separate stems into a host mixer. The Unity
[BassAudioManager](https://github.com/YARC-Official/YARG/blob/f5bfbe6996b4af35722d4204a93c6cc5c8f52a45/Assets/Script/Audio/Bass/BassAudioManager.cs)
uses BASS and an Opus plugin. That does not replace Blitz's guest MOGG decoder.
Prefer guest-compatible assets over another playback clock/mixer whose pause,
seek, muting and powerups would need reimplementation.

Core is C# targeting netstandard2.1
([project](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/YARG.Core/YARG.Core.csproj));
this project is C++. Upstream
[Core's license](https://github.com/YARC-Official/YARG.Core/blob/3beb94e526558134145bcd3e409428f759001a40/LICENSE)
is LGPLv3, while local source headers specify GPL-2.0-only. Resolve license and
dependency compatibility before copying, translating or linking upstream code;
none is imported here. Prefer native readers based on documented formats and
explicit fixtures, with provenance recorded. A translated parser still needs
that review. Do not add Unity/.NET/BASS solely to obtain the readers.

## 3. Current Blitz seams and changes needed

Evidence here is the working tree inspected on 2026-11-13. Historical documents
do not override current call sites.

| Seam | Current implementation | Expansion work |
| --- | --- | --- |
| Discovery | [dlc_library.h](../../../src/fs/dlc_library.h) scans STFS identities; [dlc_layout.h](../../../src/fs/dlc_layout.h) admits directory packages in structured roots. | Add directory classification and source manifests; retain grouping/staging exclusions. Sources without Xbox headers need synthetic identities. |
| Mounting | SDK [ContentPackage](../../../rexglue-sdk/src/system/xam/content_manager.cpp) chooses HostPathDevice for directories and STFS for files. [ExtraContentItem](../../../rexglue-sdk/include/rex/system/xam/content_manager.h) carries host path, guest name, title and content type. | Raw folders lack guest assets; SNG files currently go to STFS and fail. Add an explicit source/device factory or prepared-directory mount. Scanner-only changes are insufficient. |
| Source protection | Content-manager mutation APIs avoid external paths. The metadata helper mounts directories read-only, but ContentPackage constructs its directory HostPathDevice with read_only=false. | Protect adapter file devices themselves against write/rename/delete, without changing legitimate save mounts. Content-API protection alone is insufficient. |
| Metadata/validation | [MountPackage, IsSafeSongContent, ReadDeviceSongMetadata](../../../src/fs/dlc_song_metadata.cpp) assume STFS/host directories and static bounded DTA/DTB. | Route all consumers through the same adapter factory/view; preserve static metadata validation and reject executable DTA expressions/unsafe paths. |
| Refresh | [dlc.cpp](../../../src/hooks/dlc.cpp) runs worker scans and publishes on the guest thread; [dlc_refresh.cpp](../../../src/hooks/dlc_refresh.cpp) waits before enumeration resumes. | Capture settings/roots, prepare immutable complete generations and publish once before snapshots. Cancel superseded work; retain mounted generations. |
| Caches | Boot currently uses ScanDlcLibrariesCached and dlc-library-identities.cache; refresh uses ScanDlcLibraries. Guest and menu music have separate caches. | Preserve these distinct layers; generated chart/audio validity needs its own versioned dependency manifest. |
| Other consumers | [shell_music.cpp](../../../src/hooks/shell_music.cpp) finds MOGG and mounts through the shared helper; [store_library.cpp](../../../src/store/store_library.cpp) reads package metadata. | Supply identical DTA, stream paths and identities to Your Songs, Downloaded, menu music and Play Current Song. Invalidate path-keyed catalogs by source generation. |
| Audio | [crypto.cpp](../../../src/hooks/crypto.cpp) repairs Xbox guest MOGG crypto imports; guest MOGG/Vorbis still consumes the bytes. | Xbox support does not prove PS3 EDAT or every PS3 MOGG variant. Trace unencrypted guest acceptance before choosing output headers. |
| SDK delivery | SDK changes are carried in [patches](../../../patches/README.md). | A new factory/device/protection change must reproduce from a clean pinned SDK plus patches, not remain a checkout-only edit. |

### 3.1 Proposed data flow

```text
Configured roots + enabled formats
    -> bounded classifier / source provider (folder, SNG, YARGSONG)
    -> source manifest (identity, metadata, chart, stems, art, dependencies)
    -> compatibility validation + normalized chart/timing model
    -> guest DTA / RB MIDI / audio / Xbox art as required
    -> immutable read-only package view (original streams + derived cache)
    -> ExtraContentItem + shared mount factory
    -> guest discovery/songcache/playback + store/menu-music consumers
```

These are proposed types. Providers need bounded listing/stat/open/read-at
access. Views need truthful lengths, arbitrary reads, EOF, seek/reopen and
concurrent handles. Keep provenance separate from generated paths. A private
prepared-directory prototype can prove the guest contract before virtualizing;
the shipping path should avoid copying unchanged large assets. Do not hardlink
a source into a writable cache.

## 4. Compatibility work by format

### 4.1 PC charts and metadata

Recognize song.ini plus selected notes.mid/notes.midi/notes.chart and usable
audio. Prefer those chart names in that order and diagnose alternatives; never
select an unrelated template MIDI. Normalize case across Windows/Linux/macOS;
reject ambiguous case-duplicate files instead of depending on directory order.

Write guest-compatible songs/songs.dta and MIDI; do not forward INI or .chart
to the guest. Establish the following rules from guest traces and fixtures:

| Area | Required implementation/proof |
| --- | --- |
| Timing | Preserve PPQ, tempo/time-signature changes and tick rounding. Define INI delay and chart offset units/signs in one timeline and apply once. Test positive/negative offsets and non-zero first notes. |
| Parts | Allowlist standard tracks/note ranges, including drum kicks/cymbal flags, vocals/phrases and keys. Diagnose dropped GHL/pro/dance/LIPSYNC and unsupported tracks. |
| GH/CH semantics | Decide open notes, taps/forced HOPOs, extended sustains, six-lane drums, solos/overdrive and section events. Prove mappings; reject features whose removal would materially change the chart. |
| Guest structure | Determine required BEAT/EVENTS/end markers, difficulties, rank fields, duration/preview, art/MILO/venue paths and safe missing-part representation. Generate structural events only when required; never invent playable notes. |
| Identity | Persist source IDs and source-relative song keys; collision-check guest package names within 42 bytes and persistent song IDs. Preserve IDs across edits; artist/title alone are not identity. Keep existing score/content behavior. |

### 4.2 PC audio: likely the largest preparation cost

| Input | Preferred route | Risk/exit condition |
| --- | --- | --- |
| Compatible Ogg Vorbis | Prove a guest MOGG header/seek-table view over unchanged bytes; cache small indexes as needed. | Wrapping may avoid encoding, but guest acceptance, sample rate, channel order and random seeks are unproven. Inspect actual codec; an Ogg extension alone is insufficient. |
| Separate stems | Generate one correctly mapped multichannel MOGG, preserving instrument/crowd channels, pans, gains, alignment and lengths. | Independent compressed streams cannot simply be concatenated into multichannel Vorbis. Expect one-time decode/resample/encode; prove guest channel limits and powerup/mute behavior. |
| Opus/MP3/WAV/AIFF | Select supported decoder/encoder dependencies and prepare guest-compatible audio once. | YARG's codecs are not the guest's. Handle priming/padding, resampling and duration. Do not assume SDK FFmpeg includes required encoders. |
| Single stereo mix | Use a verified backing-track mapping at its intended level. | No instrument isolation. Do not duplicate the mix onto every instrument channel and sum it repeatedly; test powerups with that mapping. |

Use bounded streaming preparation, explicit encoder settings, cancellation and
atomic output. Never decode a whole song into memory. Preserve genuine stems
when supported; do not silently downmix them to avoid mapping work. Test unequal
stem lengths, silence, mono/stereo mixtures and 44.1/48 kHz. Avoid encoding
already compatible audio. Generated lossy audio has a quality tradeoff; cache
keys must include its encoder/format/settings version.

### 4.3 Extracted PS3 Rock Band songs

Start with extracted songs/songs.dta and decrypted MIDI bytes under .mid.edat,
or validated plain .mid, plus eligible MOGG. Validate file magic and MIDI
structure; an extension-only rename is not enough. Reject NPD encrypted data
with a source-specific reason and preserve originals.

Normalize DTA platform paths, expose the verified MIDI under a guest .mid path,
validate/prepare MOGG and convert artwork. YARG's byte-pair swap is a fixture
hypothesis, not a complete Xbox texture encoder: verify dimensions, DXT, byte
order and guest layout. A .milo_ps3 file is not .milo_xbox. Trace whether Blitz
requires it, can omit it or can use a valid shared/default guest asset. No
rename-based MILO claim; mandatory incompatible assets block that source tier.

Preserve pack grouping: one DTA can declare many songs. Validate records
independently and publish a complete set containing only accepted records.
Test a bad song beside a good song inside one pack. Do not hand invalid
mandatory references to guest discovery or allow duplicate source keys.

### 4.4 Archives and deferred formats

Add SNG/YARGSONG after the PC pipeline works. Validate signatures, supported
versions, metadata lengths, listing count/names, arithmetic overflow and
offset/length bounds. Test seek transforms at odd offsets, EOF/short reads and
concurrent stem reads through both wrapper and mask layers. A single locked
archive handle can serialize reads: compare read-at/independent handles and
bounded buffers. Reuse the same chart/audio adapter without permanent full
extraction.

YARG MOGG version 0xF0 requires its own stream adapter if admitted; do not pass
it unchanged to the Xbox guest. Defer until a fixture and measured view or
conversion strategy exist. UltraStar, venue/video/YARG backgrounds, six-fret/pro
parts and arbitrary GH/Wii/other archives are outside initial scope. Adjacent
YARG capabilities do not establish a meaningful mapping to Blitz.

**Raw PS3 .pkg: separate deferred spike.** Accepting unopened installers needs
an independently sourced container specification/reader, supported variants
and crypto/key requirements, bounded index and random-access strategy, then
the same EDAT/MOGG/platform compatibility work. Compare a virtual view against
one-time private staging, including disk duplication. The inspected YARG code
does not supply that layer. Do not advertise raw PKG or encrypted EDAT support.

## 5. Discovery, caches and lifecycle

Use one deterministic root walk where practical. Classify recognized folders
or extracted packs before descending into sidecars, while preserving separate
child songs in ordinary directories. Retain .rbbz-store-staging exclusions and
skip generated caches to avoid self-import. Off adds no adapter traversal,
chart parsing, media probes or preparation; existing STFS checks still run.

When enabled, use configured DLC roots plus an optional explicit extra-source
root list for large external libraries. These are proposed configuration
semantics, not registered cvars. Preserve structured-root registration while
also classifying eligible sources in its selected tree; HasStructuredLayout
must not silently suppress extra formats. Deduplicate adapter sources across
overlapping roots/aliases with cycle/depth limits; avoid changing established
STFS root multiplicity as a side effect.

Keep three separate validity concepts:

1. **Source identity:** persisted ID, root-relative location, provider/format and
   chosen files. Preserve IDs on edits; relocation needs an explicit reconnect
   or migration policy rather than accidentally creating new score identities.
2. **Derived assets:** dependencies include INI/DTA, chart, every used stem, art,
   updates/upgrades if supported, encoder settings and adapter version. Cheap
   size/mtime checks reuse stored preparation hashes; explicit deep refresh
   handles edits preserving timestamps. Parent-directory mtime is insufficient.
3. **Guest/consumer generation:** enabling/disabling, removal and relevant edits
   invalidate affected songcache/menu-music/store views. Unchanged generations
   must not trigger whole-library guest cache invalidation on every boot.

Warm hits check manifests and bounded metadata, not whole-audio hashes or MIDI
parses. Refresh discovers additions/removals and prepares only affected outputs;
deep validation is explicit. Recheck dependencies before publication to catch
source edits during generation. Write complete generations atomically: crash,
full disk and corrupt cache must not expose partial songs. Cache negative
results with dependency/reason information, retrying after edits, adapter
upgrades or explicit retry.

Set a generated-asset disk budget and clean only unpinned generations. Preserve
open song/menu-music handles through refresh. If cache assets disappear, rebuild
or omit with a clear reason rather than serve missing paths. During preparation
retain the last usable library and use existing guest discovery/loading status.
Publish newly playable records at a coherent discovery boundary; never encode
from a guest file-read callback or stall selection on unbounded conversion.

Detect RB update/upgrade directories but defer merges until base support works.
Later support requires explicit precedence, dependency and identity rules;
YARG pro-upgrade semantics cannot be copied blindly. Coordinate read-only
Hide/Unhide with the [song-management plan](2026-10-26-in-game-song-management-plan.md).
Delete must not remove originals or shared archive contents.

## 6. Loading and performance assessment

These are engineering expectations, not measured new-format performance.

| Stage | Possible downside | Mitigation/evidence |
| --- | --- | --- |
| Discovery | PC sidecars multiply directory/stat/antivirus work versus one CON per song, especially on HDD/network/removable roots. | Classify once, stop at source boundaries, bound concurrency; report files/directories/bytes, not just songs. |
| First scan | Chart parsing/normalization, archive indexes, art and missing-duration probes cost CPU/I/O. | Cache dependencies/outputs; prefer bounded header/index probes. Time stages separately. |
| Audio preparation | Stems/non-Vorbis conversion can dominate first use, saturate cores/disk and consume generated storage. | Bounded streaming workers and disk budget; reuse outputs. Do not invent seconds-per-song forecasts before pilots. |
| Warm boot | Dependency checks scale with sources, and additional playable songs still increase guest discovery. | Preserve generations and measure host work versus guest enumeration/mount/DTA work independently. Conversion caching cannot eliminate the latter. |
| Song start/menu music | Layered archive seeks, missing indexes or deferred media work delay first sound. | Prepare mandatory assets before publication; reuse indexes. Art can be lazy only with a proven safe guest default. |
| In-song | Archive transformations, cold seeks or background preparation contend with streaming. | Bounded buffers, read-at access and pause/throttle preparation during songs; compare underruns and frame-time tails. No per-frame scans/conversion. |
| Refresh | Incorrect invalidation rebuilds everything, duplicates songs or forces a fresh guest scan every boot. | Stable IDs, local dependency rebuilds and atomic generation publication; test unchanged refresh followed by boot. |

### 6.1 Existing measurements are not adapter predictions

The [2026-10-27 comparison](../../performance/dlc-cache-comparison-2026-10-27.md)
measured 1,777 Xbox packages. Warm cache-off host scans were about 94 ms. Matched
game runs reduced the host step from 110.9 ms to 4.8 ms with a cache hit while
guest dlc_load stayed around 24.5 s. One first-access host scan took 20.75 s
without controlled OS-cache flushing. These numbers separate host, OS-cache and
guest costs; they do not establish native-format overhead or general boot gains.

That report records cache retirement on 2026-10-28. **Current code has an
identity cache again**, as described in §3. Keep the report historical; do not
call the current implementation uncached or reuse older boot claims as adapter
benchmarks. The earlier 1,402-package/390-second discovery in the DLC guide also
cannot size this feature.

### 6.2 Benchmark matrix and gates

Use matched release builds, settings, machine, profile and library; collect at
least three warm repetitions. Label application/guest/OS/shader-cache states.
A missing app cache is not cold disk. Record source count, duration, codec,
channels, bytes, storage/hardware, median/range and frame-time percentiles.

| Case | Required measurement / provisional gate |
| --- | --- |
| Off baseline | Existing-only and mixed roots with Off: no adapter parsing/media I/O; host/guest cost within normal measured baseline variation. |
| Empty On | Classification overhead with no supported extra sources; no empty packages or guest-cache invalidation. |
| Three original PC samples | First preparation, unchanged boot, chart edit, audio edit, deletion and refresh. Target under 100 ms added host warm-scan work on test SSD, provisional and unmeasured. |
| Scaled libraries | 100/1,000 songs; MIDI/Ogg, chart, stems and PS3 separately and mixed with STFS. Synthetic trees prove discovery only; real representative media is needed for conversion/playback claims. Include slow storage when available. |
| Archives/packs | Equivalent folder versus SNG/YARGSONG, single and multi-song PS3 packs: first sound, seek reads, handles, peak RSS and bytes. |
| Cache/failure | One edit rebuilds only dependent outputs; truncation, deletion, full disk and interruption preserve healthy sources. No full-library conversion. |
| Guest cold/warm | Continuous boot-to-usable-menu timing plus separate scan/preparation/guest discovery stages; record songcache changes and actual songs versus enumeration records. |
| Playback | First sound/ready-to-play versus matched guest-compatible control; full-song sync, seek/restart, pause/replay/results and menu music. No attributable underruns or repeatable frame-time regression beyond baseline variation. |

Set concrete preparation/disk/start-latency budgets from pilot evidence before
scaling. Do not excuse a slower default-Off path or a failed performance gate
by displaying a warning. One proven format tier can ship while others remain
experimental or excluded.

## 7. Setting and confirmation proposal

Choose built-in capability with opt-in scanning, rather than always-on
auto-detection or separately installed readers. This provides support without
imposing new recursive validation/preparation on existing DLC users.

Add a restart-scoped row under **Library & Store > Advanced**
([settings_screen.cpp](../../../src/ui/settings_screen.cpp)). Preserve six rows
per page; use an existing-style child page if needed.

| Element | Proposed exact text / behavior |
| --- | --- |
| Off label | `Extra Song Formats: Off` |
| On label | `Extra Song Formats: On` |
| Caption | `Read PC and extracted PS3 songs; loading may be slower. Restart to apply.` |
| Confirmation body | `Extra formats may slow boot and DLC loading, especially on first scan. Enable?` |
| Actions | `Cancel` (initial focus), `Enable` |
| Activation | Off-to-On previews confirmation; only Enable persists On. Cancel/Back leaves Off and starts no adapter work. Reuse the native confirmation scene. |
| Restart | Confirm saves a pending restart-scoped choice. Use the existing prompt when leaving settings root; no immediate restart on the row. |
| Later boots | Saved On does not prompt every boot. Explicit config/CLI On is opt-in; log the consequence instead of blocking automation with a dialog. |
| Disable | Save Off without an enable warning. Next restart removes extra records and invalidates affected caches; preserve originals and ID mappings. |

The caption is one ASCII line, at most 96 characters including restart suffix;
labels including values are at most 28. During implementation measure actual
wrapping at Normal, Big and Biggest with
[help_layout.h](../../../src/ui/help_layout.h), within its strip above footer
icons. Use the guest's own confirmation widgets; no host overlay is required.
Describe only tiers actually shipping; name archives when that tier passes.
Explain possible extra cost without claiming every warm boot must be slower.

## 8. Retained fixtures and missing evidence

This is the 2026-10-26 recorded static inspection, not revalidated by the spike.
P0 must recover owner-approved locations and fingerprints; do not infer a host
path from the old placeholder sample root.

| Original folder | Recorded chart/parts | Acceptance |
| --- | --- | --- |
| Bon Jovi - I Want to Be Loved (PhaseShift) | notes.mid, six tracks; guitar/bass/drums/keys, no vocals. Guitar notes exist despite diff_guitar=0. | Event-derived four-part availability; aligned source chart/audio. |
| Dio - Dont Talk To Strangers (Clone Hero) | ExpertSingle/ExpertDoubleBass in chart, additional ExpertDoubleRhythm; no other standard parts/lower difficulties. | Prove Expert-only sparse-part behavior, skip rhythm with reason; reject if required parts are unavailable rather than fabricate notes. |
| Unholy Orpheus - Undeveloped Land (RhythmVerse) | Selected notes.mid, 32 tracks, five standard parts plus GHL/pro/dance/harmony/LIPSYNC; separate template MIDI is not selected. | Safe allowlist, no metadata overflow; standard timing and authored vocal/lyric behavior. |

Recorded audio was stereo Ogg Vorbis, with 44.1/48 kHz mentioned in the earlier
plan; recapture exact per-file values. Existing optional-LIPSYNC handling in
[midi_tracks.cpp](../../../src/hooks/midi_tracks.cpp) is not a general validator.

Need additional fixtures: known-good extracted Xbox control; PS3 single/pack
trees with decrypted MIDI, eligible MOGG and PS3 art; encrypted-EDAT rejection;
equivalent folder/SNG/YARGSONG; stems/additional codecs; malformed/version cases.
Use owner-provided or redistributable material. No PS3 fixture was supplied or
played in this spike; guest compatibility remains open.

## 9. Revised packages and stop conditions

**S0 is complete: upstream/code research and plan revision.** Keep the plan in
todo until implementation P0 begins, then move it to in-progress and update
indexes. Append dated build IDs, commands, manifests, logs and measurements
under out/native-song-folders/<package>/ as packages execute.

| Package | Dependency / deliverable | Advance rule |
| --- | --- | --- |
| P0 — guest contract | Recover/fingerprint fixtures; gather PS3/archives. Trace known-good DTA/MIDI/MOGG, art/MILO, identity/license, parts/difficulties and header requirements. Record provenance/dependency choices. | Full control playback and minimal guest contract recorded; resolve source-reuse decisions before borrowing code. Missing PS3 fixtures block that tier, not independent PC proofs. |
| P1 — package/device seam | Extracted Xbox via ExtraContentItem; prepared view/directory with explicit adapter kind and file-device protection; shared metadata/store/music factory; SDK patches. | Enumeration/open/reopen/license/control playback pass; direct writes/deletes fail for adapter sources; clean patched SDK reproduces results. |
| P2 — MIDI/Ogg slice | One PC MIDI/stereo-Ogg folder, generated DTA/RB MIDI and proven MOGG view or cached compatible audio; required defaults and stable IDs. | Full play, seek/restart/pause/replay and timed checkpoints; sparse-part/difficulty verdict; no repeated mix summing. Choose measured audio fallback before generalizing. |
| P3 — PC expansion | Chart offsets/tempo/phrases/edge semantics, stems, decoder/encoder and bounded preparation; independent output validation. | Honest playable/unsupported verdict for all original samples; admitted stems/codecs pass sync/channel behavior. Set pilot preparation/disk budgets. |
| P4 — extracted PS3 | Normalize decrypted MIDI/DTA, validate eligible MOGG, convert art and resolve mandatory MILO; pack isolation. | Single/pack playback passes; encrypted EDAT and incompatible assets fail clearly. No raw PKG claim. |
| P5 — source lifecycle | Bounded discovery, persistent IDs, dependency cache, atomic/cancellable generations, budgets, refresh/consumers, overlapping roots and edits/removal while mounted. | Host bounds/cache/failure tests and guest refresh/boot checks show coherent views without broad repeat rebuilds. |
| P6 — archives | Optional later tier: SNG/YARGSONG signatures/version/listing/seek providers feeding PC pipeline. | Equivalent folder/archive outputs and playback agree; malformed inputs isolated, streaming memory bounded and measured. Do not delay a proven folder release solely for archives. |
| P7 — setting/release | Default-Off gate, native confirmation, restart/profile persistence; benchmarks and retail/Ultimate integration. | Off/Cancel/Back/Enable/restart/disable/CLI pass; fonts/six-row limit pass; ship only proven tiers and report first-use/changed/warm costs separately. |

Host tests/builds do not navigate UI. Before any game/UI navigation in P0–P7,
follow AGENTS.md: ask whether the owner wants to perform the test, wait, and
build latest changes first with `cmake --build --preset win-amd64-release`.
Autonomous UI driving requires owner authorization for that scope. On Windows
run payload test executables through
`out/build/win-amd64-release/rb_blitz_windows_test_runner.exe`; the specified
existing CTest payload registrations already use it.

## 10. Remaining decisions

1. Which unencrypted MOGG header/seek/channel/sample-rate combinations does the
   guest accept? Existing Xbox encrypted-audio support does not settle this.
2. What exact difficulty, missing-part, structural-event and platform-asset
   requirements apply, including Ultimate?
3. Which native parser/decoder/encoder choices meet build, provenance and
   distribution requirements on Windows/Linux/macOS?
4. Can PS3 MILO be omitted/replaced via a valid guest default, and does normalized
   PS3 art satisfy the Xbox loader?
5. What relocation/reconnect policy preserves IDs/scores, and what guest ID/cache
   limits constrain the implementation?
6. What preparation/disk/latency budgets do pilots justify? The 100 ms small-
   library warm target is provisional; no new-format timings were measured.
7. Is raw PKG valuable enough for a separate container/crypto investigation?

**Next implementation action: P0.** Research and the recommendation are ready
for review; readers and guest playability remain unproven.
