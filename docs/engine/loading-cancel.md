# Cancelling a song load that never finishes (R11)

*Status: **cancelled, and removed from the tree** (2026-10-07). R11's toggle, its timeout cvar,
its launcher rows and `src/hooks/loading_cancel.cpp` are gone. §§1-4 are the history - what the
title does, what was measured, the one question that was never closed, and the shape the removed
module had - and are kept because the measurements are the value here. The two sections that
matter for a future attempt are §5, the verified recipe for putting this build's own text on the
loading screen, and §6, why the "catch the exception during the load and return to the music
library" route this request ended on is not available. §2.1 and §2.2 are the second look at §2:
the first is what a field-by-field read of the prompt label ruled out, the second is the fault
this build was taking the game down with at the time, and what it turned out to be.*

## 1. What the title does

A song start is `song_select_screen` → `ui/loading/gen/loading.dtb`'s **LoadingScreen**, which
shows the loading tips and a progress bar while the song's files are read, and ends on its own
prompt:

| object | what it is |
| --- | --- |
| `loading.lbl` | the prompt label; its token is `loading_press_button`, which is `"PRESS <alt1>A</alt1>TO BEGIN"` in `ui/locale/eng/gen/locale_keep.dtb` |
| `loading_state.ep` | the screen's state: `loading` and `waiting_for_button` |
| `wait_for_button_press` | whether the load ends on the prompt (TRUE for a song start) |
| `load_finished` | the DTA function the load's completion runs: sets `is_load_done`, moves the state to `waiting_for_button` |
| `BUTTON_DOWN_MSG` | `if_else {&& is_load_done {|| == action kAction_Confirm == action kAction_Start}} {do synth play commence.cue; this start_game} unhandled(0)` |

Two things about that shape matter here:

* The prompt label is filled when the screen is built (during `enter`, before any tick), and
  the completion does not set it again (measured, §2.1). Its *visibility* is not set by either:
  it comes from the state move the completion drives.
* `BUTTON_DOWN_MSG` handles confirm only. Everything else - including cancel - falls through to
  `unhandled(0)`.

## 2. What was measured

All of it in this tree, on one song at a time (a DLC package whose `songs/<song>/<song>.milo`
is missing was the reproducer, chosen because the log names the missing file), with the game
driven by `scripts/observe_ui.ps1 -State in-song -Windowed -KeepRunning` and the frames
compared with `scripts/frame_diff.ps1` and read with `scripts/ocr_image.ps1`. §2.1 corrects
what the first pass concluded from this reproducer.

| question | answer |
| --- | --- |
| Does the loading screen keep running while the load is stuck? | Yes: its message handler `sub_822C45A8` is called from `0x822C37E8` about **65 times a second**, for as long as the screen is up |
| Is there a per-frame pulse that is *not* part of the load? | Yes: `sub_827266E0`, the guest's pad read (the wrapper around `__imp__XamInputGetState`), about **1000 times a second** - but on a **different guest thread** (`0xF8000040`) than the UI (`0xF8000028`) |
| `sub_821F9D18` (`RndDrawable::SetShowing`) as a clock? | No. It runs about 300 times a second while the *prompt* is up, and **not at all** while a load is stuck: it is called when something changes a `showing` flag, not every frame |
| Does B cancel? | **Yes, from both states.** B during a stuck load moved the frame by 59 % of its pixels (8.6 % is this screen's own idle animation over the same wall time), and B at the prompt moved it by 72 %; the frame after each is the song selection screen the start came from (`CONTINUE`, the song's name, its base points). Nothing has to be wired to B - the title's own fall-through already goes back |
| Does A cancel or start? | At the prompt of a load that failed, A does nothing (a 15 % frame change, i.e. the idle animation): `is_load_done` is only set by a completion, and this load's completion did not reach the state move |
| Does the prompt label take this build's text? | Yes: `UILabel::SetText` (`sub_8229D7A0`, r3 = label, r4 = the guest string) is called with `"PRESS <alt1>B</alt1>TO CANCEL"` and the log names the label it went to |
| Does setting the text and the `showing` flag make the prompt appear? | **No.** OCR of the frame after it shows the loading tips and no prompt at all: the label is not drawn until the state move runs |

### 2.1 What the second look measured (after the two faults in §3 were traced to a bug in this build)

The label's own bytes, dumped in both states from the same load and the same object
(`+00` to `+70`, `docs`-style word dump, one run each of *the load is running* and *the
title's prompt is up*):

| question | answer |
| --- | --- |
| Is the prompt's visibility a field on the label? | **No.** The 128 bytes are byte-for-byte identical in both states (`+00: 8203AA74 8208BD14 40D62FA4 00000000`, and so on), for the same label at the same address. Whatever hides the prompt is not on the label |
| Is the label shown by *any* write to it? | No: the text arrives and the frame does not change. That is consistent with the byte dump - the gate is a parent, a panel state, or the draw list, all of which the title's own state move is what touches |
| Is the label that the prompt is drawn from the one this module writes to? | Yes. On a failed-DLC load the title's completion runs ~1.2 s in, the prompt becomes visible, and the text it shows is the one this module left on that label (the hand-back in §4 is what puts `PRESS <alt1>A</alt1>TO BEGIN` back, because that completion does not re-set the text itself) |
| Is `PRESS <alt1>A</alt1>TO BEGIN` reached through `UILabel::SetText` when the completion runs? | No. Every `SetText` call of a load (temporary trace of `sub_8229D7A0`: label address + text, ~120 calls) names HUD objects (`Level Cap +%d`, `Checkpoint!`, `%dx`, `Percussion`, `Shockwave`), never the prompt. The prompt's text is the one written when the screen was built, so a load that finishes shows it as it was built, which is why the hand-back has anything to restore |
| Is the visible loading-tips text a label this module could write to instead? | Not found: the tips and the control hints the OCR reads while loading are not set through `UILabel::SetText` at all in the load that shows them, so there is no drawn label on this screen to borrow |
| Does a failed-DLC load actually stall? | Not this one. With a missing `songs/<song>/<song>.milo` the title's own `load_finished` still runs ~1.2 s in and its prompt does appear (OCR: the tips and `PRESS <alt1>A</alt1>TO BEGIN`), A moves the frame by 92 % of its pixels and lands on the song selection screen, and B cancels the same way. So the missing-milo package is a *failed but completed* load, not the endless-load state the request is about - and not a reproducer of it either |

### 2.2 The measurement crash, and what it was

While enabled, this module took the game down on those failed-DLC loads:
`Unhandled guest access violation: read of guest 0x00000014 on thread 0xF8000028`. The cause
was in this module, not in the title: the load-finished hook ran the guest's completion once
from the hook body and once from the hook's own call of the guest function, so the DTA
function `load_finished` was evaluated twice on one load and the second evaluation read a
screen the first had already advanced. With the toggle off (same load, same run) there was no
fault - the control - and with the two halves of the hook in the right order (§4) there is
none either. The rule this leaves behind: a hook calls the guest function it wraps exactly
once, and everything the module needs on the other side of that call goes into the hook body
around it.

## 3. The open question

The one thing that puts the prompt on screen is the move the title's own completion runs - the
DTA function `load_finished` (`sub_822C38F8`, which evaluates it on the LoadingScreen in r4).
The state the feature is for is the state that move has not run in, and the second look (§2.1)
narrows the gap without closing it:

* the prompt label is **not** hidden by any field on the label itself - the same 128 bytes in
  both states - so "write the field that hides it" is not available;
* no other label of this screen is drawn while the load runs with a text this module could
  write into instead;
* firing the move from this build was measured twice and faulted both times: from the load's
  own tick, `sub_822C45A8`, while a load was stuck - `read of guest 0x01D638F0` (and, from the
  draw path in an earlier attempt, `read of guest 0x000F000A`) - and from the pad thread,
  `write of guest 0x00000000`, which is the separate lesson that UI work has to stay on the UI
  thread. Both were measured before §2.2's bug was found, and the first of them was measured in
  the *stalled* state rather than on a load the title failed by itself, so neither is a clean
  reading of "does the move work here".

Three ways on, none taken yet:

1. **Re-run the move now that the hook is clean.** Tried, and it does not work: `sub_822C38F8`
   is called with a prepared DTA value and a result slot that its one caller builds in its own
   frame (at `0x822C47BC`, inside the screen's message handler), and a call made from this
   build's tick without them faults - `Unhandled guest access violation: read of guest
   0x00000000 on thread 0xF8000028`, with the title's own call of the same function a fraction of
   a second earlier running to completion in the same boot. The pump reaches that code path with
   a message object of its own class (`r5` in the handler's arguments, the same object for every
   tick measured), and only the pump's own message gets there: the handler dispatches on the
   message's class symbol against four cached symbols, and the completion is the fourth.
2. **Write the message where the screen *does* draw.** Tried, and it does not work either: the
   loading screen's own text (its rotating tip line, the control hints) is filled through
   `UILabel::SetText` while a load runs, so those labels were written with the message - six of
   them, on the first run - and the frame did not change. A frame captured with the game process
   suspended inside the window (18:57:18.97 written, 18:57:21.25 the screen left) still shows the
   tip the title had put there. So what that screen draws is not what `UILabel::SetText` is told,
   and the object that is drawn is not among the labels the title fills that way.
3. **Say it from the host instead.** The title's own B already leaves the load (measured, from
   both states), so a message drawn by this build needs no guest UI work at all and cannot be
   hidden by a screen state - the ImGui dialog path the probe overlay uses. Measured once, and
   rejected by hand: drawn at 86.3 % of the frame height as planned, its text came out 164 px
   tall against the title's own 64 px (a 3876x2263 capture), so it reads as an overlay and not as
   the title's prompt. Any host-drawn attempt has to match that geometry, and the SDK's font at
   the right scale is the thing to measure first.

## 4. What the removed module did

`src/hooks/loading_cancel.cpp` (removed 2026-10-07), behind `enhancements_loading_cancel`:

* the load's own tick (`sub_822C45A8`, called from `0x822C37E8`) says a load has started, and
  gives the screen object (`r4`) and the vtable it had, so a screen that is gone can be told
  from one that is not;
* the guest's pad read (`sub_827266E0`) is the clock: at
  `enhancements_loading_cancel_seconds` into a load that has not prompted, it raises the flag
  (it is on the wrong thread to do the work itself);
* the loading screen's tick is where the flag is acted on: the prompt label takes
  `"PRESS <alt1>B</alt1>TO CANCEL"` (`UILabel::SetText`, `sub_8229D7A0` - the only write this
  module makes on a guest object, and the one thing measured safe in the state it is for);
* the title's own completion (`sub_822C38F8`) for *this* screen ends the wait, in two halves
  around the one invocation of the guest function the hook makes: the flags go down before it -
  so its own `UILabel::SetText` calls are not rewritten - and, after it, the title's own
  `"PRESS <alt1>A</alt1>TO BEGIN"` goes back on the label if that completion did not set it
  itself. Running the guest function twice is what §2.2 was, so the order there is load-bearing;
* a label given no text at all (`r4 == 0`, which the title does to clear one) is left alone
  rather than read as a string.

Measured end to end on a load that failed by itself (a DLC package with no
`songs/<song>/<song>.milo`), with a 1 s timeout: `watching the load` → the clock at 1.0 s →
the label reads `PRESS <alt1>B</alt1>TO CANCEL` at 1.24 s → the title's own completion at
1.38 s hands back to `PRESS <alt1>A</alt1>TO BEGIN`, no fault, and the prompt on screen is the
title's own. What is still missing is only §3: in the state where the load never prompts, that
text is on a label the player cannot see, which is why the toggle is off and says so.

With the toggle off (the default) the four hooks call the guest's own functions unchanged.

## 5. How to put this build's own text on the loading screen

This is the verified part of R11, and it is a *recipe* rather than a feature because of §5.1.

The screen is `ui/loading/gen/loading.dtb` (scene `loading.milo`), entered as `loading_screen` by
`song_select_screen`'s `on_select_msg` when it takes the offline path. It has two text lines:

| object | what it is | where its text comes from |
| --- | --- | --- |
| `tip.lbl` | the rotating tip | **not** `UILabel::SetText`: the screen's own DTA does `{tip.lbl set_dynamic_controller_token $chosen_tip}` with one of the locale tokens `loading_tip_00` … `loading_tip_70` |
| `loading.lbl` | the prompt | the locale token `loading_press_button` = `PRESS <alt1>A</alt1>TO BEGIN` |

`<alt1>X</alt1>` inside a locale string is what draws the title's own button glyph, so the message
is written the title's way rather than as literal text.

**The recipe.** Patch the localized tip strings in the running game's memory **while the song list
is up**, i.e. before the loading screen is entered, and let the screen be built afterwards:

1. find the tip strings by content - the locale pool holds 81-82 copies of the 71 tips, and each
   copy is found by its own opening text (the scratch tool the measurement used,
   `out/observations/locale_tips.py`, pairs the tokens with their text out of
   `ui/locale/eng/gen/locale_keep.dtb`; `out/` is gitignored, so it is not in the tree);
2. overwrite each copy with the message, on the condition that the slot can hold it (§5.1);
3. start the load. The screen builds its tip line from the patched string, so that line draws the
   message in the title's own font, placement and markup, above the title's own
   `PRESS (A) TO BEGIN`.

Verified end to end: with the tips patched at the song list, OCR of the loaded screen reads
`PRESS / TO CANCEL` on the tip line with the B glyph drawn, and `PRESS (A) TO BEGIN` below it
(`out/drive-ui/vanilla-loaded.png`, 3840x2160; the downscaled view the request was answered with
is `out/drive-ui/preview-cancel-full.jpg`).

### 5.1 The four things that make this brittle

| constraint | measurement |
| --- | --- |
| **The message has to fit the slot.** | The tips are 32-262 characters, so `"PRESS <alt1>B</alt1>TO CANCEL"` (29) fits every tip line. Guard each write against the slot's own terminator anyway - the pool packs strings against each other. |
| **The prompt string is not a target.** | `PRESS <alt1>A</alt1>TO BEGIN` is 27 characters and the pool stores exactly one copy with no slack (it is packed against `Can't tell your track levels from your Blitz meter?`), so a 29-character message would overrun the neighbour. This is why the message goes on the tip line and not on the prompt line. |
| **The address has to be the one the image was read from.** | Scanning one window and computing addresses from another writes ~8 MB past the strings. Measured: the game died with `Unhandled guest access violation: read of guest 0x00000038` on the song list, every run, until the two agreed. |
| **The text is baked when the screen is entered.** | Patching those same 82 strings *while* the screen is up does not move the frame at all - measured with the screen waiting on its prompt, three captures over five seconds (the tips and `PRESS (A) TO BEGIN` unchanged). Only a patch made before the entry is drawn. |

### 5.2 What the tip line actually reads is still unidentified

That last row is the open question §3 never closed, and the second attempt narrowed it without
closing it either. After patching every locale copy, the drawn tip still did not change, and it did
not follow any of these either:

* `UILabel::SetText` on the object the scene names `tip.lbl` - the write lands (the log names the
  object) and the frame does not move;
* a plain-text copy of any tip left anywhere in the guest heap window `0x40000000`-`0x45200000` -
  the scratch scanner `out/observations/inspect_tip.py` finds none after the patching, while the
  line is still drawn.

One measurement that does explain the `SetText` result, and is worth knowing before trying again:
**the scene names more than one object `tip.lbl`.** Two objects carry that name at `+0x20`, and only
one of them is a label - head word `0x8203A95C`, the class the earlier session measured. The other
(head `0x8241A058`) is a property record: its fields point at `set_dynamic_controller_token`,
`your_cred` and `num_powerups`. A "find the first object with the name" search takes the property
record, which is what the removed module did, so its `SetText` never touched the label at all.
Whether the real label takes a `SetText` is therefore still unmeasured.

### 5.3 Driving a load for a capture

Two route facts the harness needs, both measured:

* **With the Ultimate payload installed, a song start does not reach the loading screen.** Its
  `song_select_screen` branches on the DTA flag `unlockall` (set with the payload) and goes to
  `powerup_select_screen` first, whose `PLAY SONG` needs a *successful* `purchase_powerups` before
  it will `goto_screen loading_screen`. Under synthetic input that button did not fire in three
  sweeps of 16 candidates (A, Start, dpad and stick directions, X, Y, shoulders, stick press).
  `--ultimate_mode=0` restores the offline path straight to the loading screen, and that is the
  route every capture in §5 was taken on.
* **The loading screen outlives the load.** For a bundled song the load is ~1.5 s, but the screen
  then waits on its own prompt (`wait_for_button_press TRUE`), so the frame can be captured at
  leisure instead of in a burst.

## 6. The "try-catch the load" route is not available

The request after the message was dropped: wrap the load, catch an exception in it, and return to
the music library. The catching half is real; the returning half is not.

**Catching a guest fault is supported.** Guest faults arrive at the SDK's vectored exception
handler, and a module can put a callback in that chain with `arch::ExceptionHandler::Install`
(`rexglue-sdk/src/core/exception_handler_win.cpp`; 8 slots, run in order). A callback that returns
true has its RIP/EFLAGS and any integer/XMM registers it modified written back into the context and
the fault resumes with `EXCEPTION_CONTINUE_EXECUTION` - that is how `MMIOHandler` recovers faults
in mapped MMIO (`rexglue-sdk/src/system/mmio_handler.cpp`). When no callback claims it, the memory
path logs `Unhandled guest access violation` (`rexglue-sdk/src/system/xmemory.cpp`) and the handler
returns `EXCEPTION_CONTINUE_SEARCH`, which is what ends the process.

**But claiming a fault is not recovering from it.** `EXCEPTION_CONTINUE_EXECUTION` re-runs the
faulting instruction: a load that faulted on a bad pointer out of the package's own data faults
again unless that pointer is repaired, and nothing at the fault site knows what it should be.

**And there is nothing to unwind to.** The only unwind the runtime has is the guest's own
`setjmp`/`longjmp`, bridged to a **host** `jmp_buf` held in a `thread_local` map keyed by the guest
buffer's address (`rexglue-sdk/resources/templates/codegen/pch_h.inja`), where `ppc_longjmp` on a
buffer that was never registered **aborts**. Guest code registers one at 21 call sites, and 0 of
them run on any route this project can drive (measured, [history/bringup-log.md](../history/bringup-log.md)
"Does anything actually longjmp?"). A hook cannot supply the missing frame either: the loading
screen's tick (`sub_822C45A8`) returns every frame, so no frame of this build's stays on the stack
for the length of a load.

**Even a mechanical unwind would not land in the music library.** Which screen is up is the title's
own DTA screen stack, and the load leaves it mid-transition - the machinery that would put the song
list back is the same machinery that is stuck. Resuming in the menu loop with the loading screen
still current is not a return to the library.

**And the return is not the missing piece.** The title's own B already leaves a load and lands on
the song selection screen, from the running state and from the prompt state both (frame diff 59 %
and 72 %, then OCR of the song list - §2). What was missing was the screen saying so, which is the
message - a display problem, not an exception one.

Conclusion: not viable as a recovery mechanism, and unnecessary for the behaviour it was meant to
provide. R11 was removed on 2026-10-07.
