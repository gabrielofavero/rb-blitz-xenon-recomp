# Cancelling a song load that never finishes (R11)

*Status: implemented and measured up to one open question, off by default
(`enhancements_loading_cancel`), and the toggle's own description says so. What is verified,
what was measured, and what is still open are each below. §2.1 and §2.2 are the second look:
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

1. **Re-run the move now that the hook is clean.** "Run the title's own completion once, on the
   UI thread, and let it show its own prompt" is the whole feature if it holds up, and the
   earlier faults are now suspect: the §2.2 double evaluation is exactly the shape of "the DTA
   reads a screen the previous evaluation advanced". This is the cheapest of the three: one
   build, one stalled load, one log.
2. **Find what actually draws the label.** Not a field on the label (§2.1), so it is an
   ancestor or the draw list: the milo (`ui/loading/gen/loading.milo`) is where the object
   graph and the state move that owns the prompt are, and reading it is the way to name the
   object instead of guessing at one.
3. **Say it from the host instead.** The title's own B already leaves the load (measured, from
   both states), so a message drawn by this build - the SDK's ImGui dialog path, the way
   `src/diag/probe_overlay.cpp` is drawn - needs no guest UI work at all and cannot be hidden
   by a screen state. It is a different look from the title's own prompt, which is why it is
   not the default of the two options in the request.

## 4. What the code does today

`src/hooks/loading_cancel.cpp`, behind `enhancements_loading_cancel`:

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
