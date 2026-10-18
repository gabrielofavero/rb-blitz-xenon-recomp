# 2026-10-15 — the native discovery bar (the P6 overlay retired)

**Owner request (2026-10-15).** The generated overlay P6 added did not read as game-native: the
in-game bar and its info (*discovering*, *load song information*, …) had to be the title's own.

**What changed.** The host-side ImGui progress overlay P6 added (2026-10-14) is retired. The
discovery screen is the title's own drawing again: the connect panel's `progress_bar.grp` /
`content_percent.lbl` (the Flow that reads the panel's `current_content` / `total_content`) for
the bar, and the title's own locale entries — *Discovering Downloadable Content*, or *Loading
Cached Content List* on a real host-cache hit (P6's label edit) — for the words. The host
supplies numbers only.

## What the code does now

| Piece | Behaviour |
| --- | --- |
| `src/hooks/content_progress.cpp` | Keeps P6's model (one count per real aggregate enumerator; terminators, pending submissions and failures are not items) and adds `Model::ItemsCounted()` / `Model::ItemsExpected()` — the two numbers **summed over every batch of the attempt**, so the two interleaved enumerator walks read as one monotonic count. |
| the push | The constructor hook (`sub_82759308`) remembers the engine's progress object; when a whole percent moves, the host writes the object's `+8`/`+12` and calls the engine's own push `sub_82759190` on a copy of the context. ~100 pushes over the screen, not one per item. |
| `SetTotal` (`sub_827593A8`) | Forwarded; only a **non-positive** announcement is rewritten to `-1` (the Flow's own "not enumerating" value). The call zeroes the current count, so the engine resets the bar for the phase it starts next — the host never re-pushes after it. |
| `Increment` (`sub_827593B8`) | Hooked and counted (`native_increments`), forwarded. |
| `src/ui/help_overlay.cpp` | `DrawContentProgress` deleted, with the `Display`/`Snapshot` seam in `src/hooks/content_progress.h`; the caption overlay (Settings Labels) is untouched. |

Pacing is unchanged from P6: the frame rate is freed for the discovery and restored when
`UIManager::Poll` observes the usable menu.

## Evidence (measured 2026-10-15, `out/_swap/dlc-native-bar/bar-2`)

Boot: release tree, owner's structured library (**1,773 packages**, grown from the 1,402 of the
P6 record), `enhancements_dlc_cache=true` (cold walk — no hit this leg), `uncap_enumeration=true`,
`gpu_backend=vulkan`. The bar was captured every ~1.5 s with no input during the discovery.

```
15:06:04.925  content_pipeline: attempt 1 started; native_mouse: screen=server_connect_screen panel=server_connect_panel
15:06:05.128  the title's own bar is at 1 of 1773 item(s) (0%)
15:06:08.892  the title's own bar is at 887 of 3546 item(s) (25%)
15:06:12.441  the title's own bar is at 1773 of 3546 item(s) (50%)
15:06:15.975  the title's own bar is at 2660 of 3546 item(s) (75%)
15:06:19.499  the title's own bar is at 3546 of 3546 item(s) (100%)
15:06:19.507  the title announced a total of 3546 item(s) to enumerate (the host counted 3546 of 3546)
```

* **The title's own bar fills 0 → 100 % in 14.4 s** (3,546 items, ~4.1 ms/item with the frame
  rate free), one push per whole percent, and none after `15:06:19.5`.
* **The drawn fill was not taken on faith**: `out/_swap/p2-bar.ps1` counts the cyan pixels of the
  bar's own row — shots `02` → `07` grow 196 → 424 → 624 → 828 → 1,072 → **1,284** cyan pixels
  (spans 31 → 208). The shots carry the title's own text (*Discovering Downloadable Content*) and
  **no host overlay box anywhere**.
* **The engine owns the panel from its own end call on.** Shot `08` (right after
  `15:06:19.5`) shows the bar **empty** — the engine's `SetTotal` zeroed the current count and
  pushed an empty bar for the phase it starts next. From there the bar **refills with the
  engine's own `Increment` calls** (whose body is `++current` + push) through the mounting phase:
  shots `12` → `114` grew 10 → 413 cyan pixels over ~4.5 minutes, with **zero** host pushes in
  that window (the host only pushes from XAM enumerate messages, and none arrive after the
  enumeration).
* **No faults**: no `[FATAL]` and no `access violation` line in the leg.

**Re-run on the final rebuilt binary (`bar-3`, 84 s wall, 16:48–16:49).** The same sequence
reproduced: pushes at 0/25/50/75/100 % over 14.3 s (`16:48:46.944` → `16:49:01.256`), the drawn
fill 228 → 1,306 cyan pixels across shots 02–07, and the bar empty at shot 08 right after the
engine's `SetTotal(3546)` (`16:49:01.263`). This leg stops right after the enumeration, so it does
not show the engine's refill — `bar-2` does.

**Control leg (`bar-1`).** A harness quoting bug (below) ran the same boot with a **broken
`--dlc_root`** (0 packages): the engine announced `SetTotal(0)` → the non-positive branch wrote
`-1` → the bar stayed hidden, `Complete` followed 0.2 s later and the usable menu was reached at
`15:04:34.224` (attempt total 210 ms, `native_increments=0`). That is the clamp path exercised,
and it is why the 2026-10-10 "the engine announces 0" measurement and today's "3546" are both on
record: the engine's own argument follows what it has to process (0 with nothing to mount,
2 × 1,773 items with a full library), and only the non-positive case is rewritten.

## Superseded readings

* P6's overlay (host-drawn "Reading content: N / T packages" and unnumbered
  *Loading song information…* phases) is retired; its counting model, cache work, pacing boundary
  and installed measurements stand. See the P6 record in
  [`../in-progress/2026-10-14-installed-game-fix-prompts.md`](2026-10-14-installed-game-fix-prompts.md).
* §12 of the song-cache plan pushed `counted` / `2 × ContentItemCount()`. Today's numbers are the
  enumerators' own sizes summed per attempt — the same 3,546 for this library, now measured
  rather than inferred.
* The 2026-10-10 "`Increment` is never called" reading is superseded for the **mounting** phase:
  it drives the refill after the engine's `SetTotal` (shot 08 → 114); during the enumeration
  itself the host's pushes remain the only writer.

## Limits

* The big-library leg was killed while the mounting phase was still running (P6 measured that
  phase at ~94 s warm, longer cold); the usable menu was not part of this leg's evidence.
* One boot per condition. The harness (`out/_swap/dlc-native-bar/run.ps1`) stops right after the
  engine's `Complete` — the enumeration window is what it exists to capture.
* The engine's `SetTotal` argument (0 vs 3,546) is recorded, not reverse-engineered: whoever
  revisits the mounting bar should watch both values and the `Increment` counts together.
* **Harness trap that cost the first leg**: `Start-Process -ArgumentList` splits a path that
  contains spaces unless that element is quoted — `--dlc_root="F:\Games\...\Singles"`. The
  unquoted form boots with an unresolved root and enumerates nothing (that leg is kept as the
  control).

## Tests

`content_progress` (ctest #13) grew the two aggregate-accessor cases (sums across interleaved
batches, a batch with no announced size adding no denominator, and a new attempt zeroing both).
The Vulkan-off tree is **38/38** on this change; `content_progress` passes standalone.
