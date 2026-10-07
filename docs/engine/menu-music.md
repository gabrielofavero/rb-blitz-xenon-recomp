# The main menu's background music

**The question** ([plans/customization-plan.md](../plans/customization-plan.md) C1/R9): where do the
main menu's songs come from once DLC is loaded, and can a DLC song be played there?

**The answer.** The menu's background music is `ShellMusicPanel`, Blitz's rename of the RB3 engine's
`MetaMusic`. Its pool is **data** — the `shellmusic` block of the synth config (`config/synth.dtb`
inside the ark) — and it already picks a song at random and chains to the next when one ends. The pool
is three of the title's *own* streamed tracks (`sfx/shell/samples/shellmusic_1..3.mogg`), and every
entry's path is built as `sfx/shell/<name>` (`0x822B7968`). DLC songs are never reachable from that
prefix: the title opens them from a mounted content device
(`cntXXXXXXXX:\songs\<key>\<key>.<ext>`). So R9 is not a data edit — the pool file's size is pinned by
the ark index and its names cannot express a content device — it is one hook on the loader the pool
goes through, fed by packages this project mounts itself.

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
2. picks an element **at random** (`0x822B7550`, a `[0, size)` pick) — so "a random song, then the
   next" is already the title's behaviour, over three entries;
3. formats the entry's name with `"sfx/shell/%s"` (string `0x8203ACE8`) and reads its `gain`
   (string `0x8203ACE0`);
4. makes a `ShellMusic` (FX dir `sfx/shell/shell_music_fx.milo`, string `0x8203ACC0`) and calls
   `ShellMusic::Load(path, gain, loop=1, play_from_buffer=1)`;
5. which streams the path. `Load` is called from exactly one site in the image — this one — so it is
   the single point every menu track goes through.

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
2. takes up to that many package paths from `rb_blitz::dlc::LibraryPackagePaths()` — the structured
   root's packages and the flat library's, spread across the library rather than taken off the front;
3. mounts each read-only as a device of its own (`\Device\BlitzMenuMusic\<n>\`, reachable as
   `menumusic<n>:`) and finds the `.mogg` the package holds;
4. hooks `ShellMusic::Load` (`0x82757F50`). When the title asks for one of its own
   `sfx/shell/...` pool entries, the path is replaced with `menumusic<n>:\songs\<song>\<song>` - a
   different song of exactly the same kind - and the original `Load` runs with it.

Nothing is copied, extracted or written under `game/`, and with R9 off (or no library) nothing is
mounted and the hook calls the guest unchanged.

**Still to measure.** The substitution itself and the mounted-device path are the shape the guest
already uses, but this build has not yet been run with the toggle on: what an in-game run has to show
is the `shell_music:` log line naming a DLC song, and audio proving the stream resolved. Two details
are reasoned and not yet confirmed: that the engine appends `.mogg` to a path it is given (it must, to
turn the config's `samples/shellmusic_1` into `samples/shellmusic_1.mogg`), and that a multi-channel
DLC mixdown streams through the same `Stream` a stereo one does.

## 5. Where it lives

| Thing | Where |
| --- | --- |
| The pool (data) | `config/synth.dtb` in `game/gen/main_xbox_0.ark` |
| The pool reader + random pick | `0x822B7968`, `0x822B7550` |
| The streamed player | `ShellMusic` `0x82758208` (ctor), `0x82757F50` (`Load`) |
| The feature | [src/hooks/shell_music.cpp](../../src/hooks/shell_music.cpp) |
| The toggles | [toggles.md](toggles.md) (R9) |
| The library it draws from | [dlc.md](../dlc.md), `rb_blitz::dlc::LibraryPackagePaths()` |
