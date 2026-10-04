# Observing a UI state

The harness behind [plans/customization-plan.md](../plans/customization-plan.md) P3 and contract 3, and
the thing E1's baseline set is made with. It answers a narrow question — *drive the game to this named
screen, save it, read it, and tell me what changed* — and it refuses to answer it in the ways that
have already lied to this project.

```powershell
.\scripts\observe_ui.ps1 -ListStates
.\scripts\observe_ui.ps1 -State "main menu" -Ocr
.\scripts\observe_ui.ps1 -State title -BaselineDir out\observations\baseline
.\scripts\observe_ui.ps1 -State title -Compare out\observations\baseline
```

The execution policy on this machine is Restricted, so a bare `.\scripts\...ps1` fails: run it as
`powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\observe_ui.ps1 ...`, or from a shell
that already has the policy set.

## What it is made of

It joins scripts that already existed rather than re-implementing any of them
([src/diag/probe.md](probe.md) is the *other* half of the instrumentation, for the guest's internals):

| Script | What the harness uses it for |
| --- | --- |
| [drive_ui.ps1](../../scripts/drive_ui.ps1) | sends the keys — one tap is one menu row, through the MnK pad |
| [capture_window.ps1](../../scripts/capture_window.ps1) | saves the window, and **refuses to save the wrong one** if another window owns the foreground |
| [ocr_image.ps1](../../scripts/ocr_image.ps1) | reads the static text back (Windows OCR) |
| [frame_diff.ps1](../../scripts/frame_diff.ps1) | measures the difference against a baseline |
| [ui_states.ps1](../../scripts/ui_states.ps1) | **the one table** of state names, routes and needles |

## The states

`-ListStates` prints the table as JSON. The names are dashes; `-State "main menu"` and
`-State main-menu` are the same state.

| State | Needle (the text that proves it) | Route |
| --- | --- | --- |
| `boot` | — | the window, before the splash finishes |
| `title` | `TO START` | wait for the title screen |
| `main-menu` | `DOWNLOAD CONTENT` | title → sign-in → offline prompt → menu |
| `song-list` | `YOUR SONGS` | menu, A on row 0 (PLAY) |
| `in-song` | — | …and A on the list's first row (Random Song) |
| `pause` | — | …and START |
| `help-options` | `HOW TO PLAY` | menu row 3 |
| `controls` | `CONTROLLER` | HELP & OPTIONS, one row down |
| `calibration` | `CALIBRATION WIZARD` | HELP & OPTIONS, two rows down |
| `audio-video` | `OVERSCAN` | HELP & OPTIONS, three rows down |
| `credits` | `PROGRAMMER` | HELP & OPTIONS, four rows down |
| `leaderboards` | `CAREER LEADERBOARD` | menu row 1 |
| `download` | `ESTORE` | menu row 4 |
| `exit-confirm` | `WANT TO EXIT` | menu row 5 |

The needles are the strings those screens actually draw, as read by OCR on this build; the routes are
the ones [acceptance_screens.ps1](../../scripts/acceptance_screens.ps1) already walks. Two measured
properties of the guest's menus make a route safe to write this way: **a list clamps at both ends**
(so eight taps up is the first row, whatever the highlight was) and **a list resets its highlight to
the first row whenever it is entered**.

A state may also carry a **`Crop`**, because some screens only give up their text in a band. Measured:
the How to Play title is stylised, and a full-frame OCR of a 3840×2160 capture loses it entirely —
the frame is already past the size Windows OCR will upscale, so `-GrayscaleScale 3` is refused
outright and scale 2 reads the rows but not the heading. `help-options` therefore OCRs
`150,100,2700,500` instead. **A crop is in the captured frame's pixels, so it follows the capture
size**: the report carries `capture_size` and `ocr.crop` so a crop that no longer lines up is visible
rather than silently wrong.

`in-song` and `pause` have no needle yet — nobody has pinned down what they draw — so the harness
captures them and reports the expectation as unknown rather than inventing a string that could pass
for the wrong reason. **`in-song` starts a song**, which the title may write to the save when the
song ends; the harness closes the window mid-song, but a state that plays to the end is a different
thing and is not this.

## The report

One JSON object on stdout; the exit code is 0 when the state was reached and its expectation held.

| Field | Meaning |
| --- | --- |
| `state`, `note` | what was asked for, and what it is |
| `binary_sha256` | the build the observation was taken with — the identity a baseline has to match |
| `ultimate_mode` | `0` (vanilla) unless `-UltimateMode 1`; an installed payload must not change a screen |
| `launched` | whether the harness started the game or attached with `-ReuseRunning` |
| `route` | the exact action list it drove, so a run can be repeated by hand |
| `capture` | the PNG it saved |
| `settle_seconds` | how long it waited after the route before capturing |
| `ocr.ran`, `ocr.needle`, `ocr.crop`, `ocr.matched`, `ocr.text` | what the screen said, and whether it said what it should. `ocr.text` is the *crop's* text when the state carries one, not the whole frame |
| `diff` | the comparison (below) |
| `passed` | `ocr.matched` **and** the diff, when there is one |

`diff.status` is one of:

- **`not-requested`** — no `-Compare`.
- **`no-baseline`** — `-Compare` named a directory with no `<state>.png` in it. **This is not a
  failure and does not fail the run**: there is nothing to compare against, and a measurement with
  nothing to measure against is not a pass.
- **`compared`** — with `percent`, `max_percent`, `same_build`, `pass` and the raw `frame_diff` line.

## The two rules, and why

Both come from things this project has already been wrong about ([backlog.md](../backlog.md),
"Measure the noise floor before believing any screenshot diff").

**A pixel diff is only evidence against a same-build baseline.** Two runs of the *same* build differ
by **3.5–5.9 %** of the frame just after a screen is entered and **~0.14 %** once it has settled. A
diff of 3 % is therefore a normal unsettled screen, not a changed one — which is exactly how a round
of "proven" texture swaps turned out to be nothing. So the harness needs `-BaselineDir` to have
something to compare to, reports `same_build` when the baseline's metadata says which build made it,
and compares against the **settled** frame (hence `settle_seconds`) rather than the frame the last
key produced.

**And on an animated screen there is no settled frame at all**, which the harness measured on its
first real pair: the title screen's own two same-build runs differ by **0.046 %** in one pair and
**1.978 %** in another, because the aurora behind it is a slow animation and what differs is the
distance between two runs' phases. No settle time removes that. That is why the ceiling is **per
state** (`MaxDiff` in [ui_states.ps1](../../scripts/ui_states.ps1), 2.5 % for the two screens with the
animated background) and why **OCR is the control and the diff is corroboration** wherever a needle
exists.

**The default threshold sits between those two floors.** `-MaxDiffPercent` defaults to `1.0`: an
order of magnitude above the settled floor, several times below the just-entered one, for a state
that does not override it. Raise it deliberately, not to make a test pass.

## Adding a state

1. Add an entry to [scripts/ui_states.ps1](../../scripts/ui_states.ps1) with `Route` (an action list
   in [drive_ui.ps1](../../scripts/drive_ui.ps1)'s grammar), `Needle`, `Settle`, `Note` and — only if
   the text needs it — `Crop`. Add the name to `$script:UiStateOrder` too.
2. Run it once with `-Ocr` and no baseline: the report's `ocr.text` is what the screen says. Put the
   shortest unambiguous fragment of that text in `Needle` — not a whole sentence, which OCR noise
   will break (measured: the title screen reads `BLITZ PRESS@TO START`, so the needle is `TO START`).
   If the needle does not appear, the text is not there to be read: try a `Crop` of the band it is
   drawn in before weakening the needle to something the full frame does read.
3. If the state animates, raise `Settle` and then **measure two same-build pairs**; if they disagree,
   the state has no settled frame and its `MaxDiff` has to come from the range, not from a wait.
4. Record the baseline with `-BaselineDir` only once the state reads the same way twice.

## Open, and left open on purpose

- **The How to Play screen's first row is not what its description implies.** Entering HELP & OPTIONS
  and accepting nothing captures a screen whose title reads `How to PLAY` and whose visible rows read
  `BASIC TUTORIAL` and `ADVANCED TUTORIAL`; one row down from there is `CONTROLLER`, which is how
  [acceptance_screens.ps1](../../scripts/acceptance_screens.ps1)'s table reaches Controls. So row 0 is
  a tutorial entry, and that document's "a submenu titled How to Play over four pages" describes rows
  1–4 and does not account for row 0. Not resolved here — it belongs to S2's flow map, and the harness
  only reports what it saw.
- **`in-song` and `pause` have no needle.** Nothing has pinned down what they draw, so they capture
  and report an unknown expectation rather than a string that could pass for the wrong reason.
- **The cost is in `frame_diff`.** A capture run measured **46 s** end to end; a compare run **154 s**,
  of which most is that script's per-pixel PowerShell loop over a 3840×2160 frame. The harness is a
  research tool, not a `ctest` target, and this is why.
