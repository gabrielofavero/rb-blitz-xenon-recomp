# The main menu's background music

**The question** ([plans/customization-plan.md](../plans/customization-plan.md) C1/R9): where do the
main menu's songs come from once DLC is loaded, and can a DLC song be played there?

**The answer.** The menu's background music is `ShellMusicPanel`, Blitz's rename of the RB3 engine's
`MetaMusic`. Its pool is **data** — the `shellmusic` block of the synth config (`config/synth.dtb`
inside the ark) — three of the title's *own* streamed tracks
(`sfx/shell/samples/shellmusic_1..3.mogg`); every entry's path is built as `sfx/shell/<name>`
(`0x822B7968`). The panel picks one of the three **at random when it is entered** and starts it with
`loop=1`, so the title plays ONE track per menu visit, looping it; the panel's per-frame poll
(`0x827586D8`) drives that stream and tears it down when it ends, but nothing asks for a next track on
its own. DLC songs are never reachable from that prefix: the title opens them from a mounted content
device (`cntXXXXXXXX:\songs\<key>\<key>.<ext>`). So R9 is not a data edit — the pool file's size is
pinned by the ark index and its names cannot express a content device — it is hooks on the loader the
pool goes through and on the poll that drives it, fed by packages this project mounts itself.

**The feature.** `enhancements_menu_dlc_songs` + `enhancements_menu_dlc_song_count`
([toggles.md](toggles.md), R9), implemented in
[src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp). Off by default, and inert without a DLC
library.

**Evidence convention** (as in the plan): **[tree]** is this working tree with `file:line`;
**[cited]** is a build product — a log line or a capture from a run on this machine; **[assumed]** is
reasoned and not yet measured.

## 1. Where the pool is

`config/synth.dtb` — 635 bytes in the retail ark (`game/gen/main_xbox_0.ark`) **[tree]** — carries one
`shellmusic` block, decoded here through the same DTB reader R5 uses:

```
(shellmusic
   (volume -10.0)
   (fade_time 1.0)
   (pan -1.0 1.0)
   (music
      (samples/shellmusic_1 (gain 0.0))
      (samples/shellmusic_2 (gain 0.0))
      (samples/shellmusic_3 (gain 0.0))
   )
)
```

**[cited]** the same file is in the community script dump (`rbblitz/360/config/synth.dta`), which is
where the block's shape is legible. The three entries resolve to ark entries
`sfx/shell/samples/shellmusic_1..3.mogg` (4.5–5.0 MB each) **[tree]**, i.e. thematically the title's
own menu tracks, not its full soundtrack.

## 2. Who reads it

`ShellMusicPanel` and `ShellMusic` / `ShellMusicLoader` are recovered from the image's RTTI (the
codegen trace's `VTableScanner`, run over the decrypted `default.xex`):

| Class | COL | Vtable | Members of interest |
| --- | --- | --- | --- |
| `ShellMusicPanel` | `0x821385F8`, `0x8213864C` | `0x8203ADA4` (22 slots), `0x8203AE04` (16) | ctor `0x822B7B98`; the pool loader `0x822B7968` |
| `ShellMusic` (RB3's `MetaMusic`) | `0x82147B08` | `0x82105484` (22 slots) | ctor `0x82758208`; `Load` `0x82757F50` |
| `ShellMusicLoader` (RB3's `MetaMusicLoader`) | `0x82147ABC` | `0x82105384` (5 slots) | |

`0x822B7968` is the whole pool path, and it does five things per track:

1. interns the symbols `synth`, `shellmusic` and `music` (strings at `0x82000AB8`, `0x8203ACF8`,
   `0x8203AD04`) and asks the config for their array;
2. picks an element **at random** (`0x822B7550`, a `[0, size)` pick) — one entry per call, and the
   call is the panel's entry point, so the title picks one track per menu visit, not per track;
3. formats the entry's name with `"sfx/shell/%s"` (string `0x8203ACE8`) and reads its `gain`
   (string `0x8203ACE0`);
4. makes a `ShellMusic` (FX dir `sfx/shell/shell_music_fx.milo`, string `0x8203ACC0`) and calls
   `ShellMusic::Load(path, gain, loop=1, play_from_buffer=1)`;
5. which streams the path. `Load` is called from exactly one site in the image — this one — so it is
   the single point every menu track goes through.

`0x827586D8` is `ShellMusic`'s per-frame poll: the panel calls it every frame (from `0x822B7290` and
`0x822B71F0`) and it applies the track's volume. It has an end-of-track path (`0x82758904`: clear
`ShellMusic+44`, stop the channels), but the menu never reaches it, because the stream it plays loops
- the `loop` argument the panel passes only scales a fade at the seam. What does move is the stream's
own play position (`Stream+0xA0`, in samples, advancing with the audio, verified live), which restarts
near zero at the seam. A position that went backwards is therefore the observable end of a track, and
the point R9 hooks.

`ShellMusic` is RB3's `MetaMusic` under another name: a streamed player with a random start offset and
a loop, not a fixed sample bank. A pool entry is therefore a **stream path**, and anything the engine
can stream is something the menu can play.

## 3. Why a DLC song is not a pool entry

A DLC song's files are not under `sfx/shell/` and are not in the ark: the guest opens them from a
mounted content device. A run on this machine shows the shape **[cited]**:

```
Registered symbolic link: cnt000001d3: => \Device\Content\3\
StfsContainerDevice::ResolvePath(\songs\44155)
NtCreateFile FAILED: path='cnt000001d3:\songs\44155\44155.milo' -> 0xc000000f
```

The device name (`cnt%08x`, string `0x8205D650`) is chosen by the *guest* and registered with
`ObCreateSymbolicLink`; the path inside is `\songs\<key>\<key>.<ext>`. The pool's entries are prefixes
of `sfx/shell/`, so no entry the title's own config can hold names one — and `config/synth.dtb` sits
inside the ark, whose index pins its size, so the array cannot grow past three entries either.

## 4. What R9 does

`rb_blitz::shell_music::Configure()` runs from `OnPostLoadXexImage`, after `dlc::Configure()`:

1. reads the R9 toggle and `enhancements_menu_dlc_song_count`;
2. draws that many package paths **at random** from `rb_blitz::dlc::LibraryPackagePaths()` — the
   structured root's packages and the flat library's — so the pool is different on every boot;
3. mounts each read-only as a device of its own (`\Device\BlitzMenuMusic\<n>\`, reachable as
   `menumusic<n>:`) and finds the `.mogg` the package holds;
4. hooks `ShellMusic::Load` (`0x82757F50`). When the title asks for one of its own
   `sfx/shell/...` pool entries, the path is replaced with `menumusic<n>:\songs\<song>\<song>` - a
   different song of exactly the same kind - and the original `Load` runs with it, unchanged
   otherwise (the same gain, loop and FX the panel chose);
5. hooks the poll (`0x827586D8`). While a song R9 started plays it watches that stream's own position
   (`Stream+0xA0`); at the seam, where the song restarts, the position jumps backwards - the one thing
   the guest offers as "this track ended" - and R9 loads the next song of the deck onto the same
   `ShellMusic`, so the menu walks its pool instead of repeating one track. The pool is a shuffled
   deck: every mounted song is heard before any repeats, the deck reshuffles when it runs out, and the
   first song of a new deck is never the one that just played.

Nothing is copied, extracted or written under `game/`, and with R9 off (or no library) nothing is
mounted and the hook calls the guest unchanged.

**Measured.** An in-game run does show the substitution - `shell_music: the main menu asked for
"sfx/shell/samples/shellmusic_1"; playing "menumusic9:\songs\themonster\themonster.mogg" instead (R9)`
- so the engine appends `.mogg` to a path it is given, and a DLC mixdown streams through the same
`Stream` a stereo one does. It also confirmed the two things this feature used to get wrong: the pool
was the same packages in the same order every boot (the old index stride), and the menu only asked for
a new track when it was re-entered - with the stream looping, one song played for as long as the menu
stayed open. Both are what the random draw and the poll hook above address. The stream's position was
read live to confirm the signal the hook rotates on: `Stream+0xA0` advances at about 44,100 counts a
second and restarted near zero at the seam. The same run then showed the rotation itself - three
different songs in a row, each handed over about a song-length apart
(`14:34:14 rihannashutupanddrive` -> `14:37:55 ringoffire3` -> `14:41:34 funhouse`), with no menu
re-entry between them.

## 5. Where it lives

| Thing | Where |
| --- | --- |
| The pool (data) | `config/synth.dtb` in `game/gen/main_xbox_0.ark` |
| The pool reader + random pick | `0x822B7968`, `0x822B7550` (called on panel entry) |
| The streamed player | `ShellMusic` `0x82758208` (ctor), `0x82757F50` (`Load`) |
| The per-frame poll, and a track's end | `0x827586D8`; the stream's position, `Stream+0xA0` |
| The feature | [src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp) |
| The toggles | [toggles.md](toggles.md) (R9) |
| The library it draws from | [dlc.md](../dlc.md), `rb_blitz::dlc::LibraryPackagePaths()` |
