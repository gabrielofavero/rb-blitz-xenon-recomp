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
| `capture`, `capture_size` | the PNG it saved, and its pixel size — a capture at one size cannot be diffed against another |
| `settle_seconds` | how long it waited after the route before capturing |
| `ocr.ran`, `ocr.needle`, `ocr.crop`, `ocr.matched`, `ocr.text` | what the screen said, and whether it said what it should. `ocr.text` is the *crop's* text when the state carries one, not the whole frame |
| `diff` | the comparison (below); `diff.scale` is the factor both frames were downscaled by |
| `passed` | `ocr.matched`, **and** the diff where the diff is allowed to decide (see `diff.gates_pass`) |

`diff.status` is one of:

- **`not-requested`** — no `-Compare`.
- **`no-baseline`** — `-Compare` named a directory with no `<state>.png` in it. **This is not a
  failure and does not fail the run**: there is nothing to compare against, and a measurement with
  nothing to measure against is not a pass.
- **`compared`** — with `percent`, `scale`, `baseline_scale`, `same_scale`, `max_percent`,
  `threshold_from`, `same_build`, `settled`, `gates_pass`, `pass` and the raw `frame_diff` line.
  `gates_pass` is the field that decides the run: it is false on a state with no settled frame and on
  a baseline whose floor was measured at another scale, and `percent` is then corroboration only.

### The floor is scale-dependent

`-CompareScale N` downscales both frames by `N` before diffing, and the factor is recorded in the
report (`diff.scale`) and in the baseline's metadata — **a floor is never compared across scales.**
Two reasons, both practical:

- the project's older floor numbers (**3.5–5.9 %** just-entered, **~0.14 %** settled) were measured on
  ~1 Mpx captures, while a capture here is **3840×2160 = 8.3 Mpx**, so the same absolute movement of a
  few pixels is a different percentage;
- [frame_diff.ps1](../../scripts/frame_diff.ps1) walks every pixel in PowerShell, so the cost of a
  comparison is the frame's area: at `-CompareScale 4` a compare takes tens of seconds instead of
  minutes.

[observe_baseline.ps1](../../scripts/observe_baseline.ps1) is the driver that produces a whole
baseline set with one scale: two passes per state (capture, then compare), the second pass's
`percent` **being** that state's same-build floor. It writes `out/observations/baseline/<build>/`
(`<build>` = the executable's SHA-256, first 12 digits) with one PNG + JSON per state and a
`summary.json`. It is resumable: `-Resume` reuses a state's existing capture instead of booting again,
because the run is a long sequence of boots and does get interrupted. It records the scale in each
baseline's metadata and warns when a state's baseline was captured at a scale other than the one the
comparison used, because a floor does not transfer between scales.

### What the floor actually is, measured

`observe_baseline.ps1 -Resume -CompareScale 4` on build `caa1e18c3c6c` (2026-10-04) measured one
same-build pair per state. The `noise` column is the whole-frame percentage; the `screen` column is
what OCR read in that run's capture, and it is the column that says whether the number means anything:

| State | noise | OCR of the capture | Verdict |
| --- | --- | --- | --- |
| `help-options` | **0 %** | `HOW TO PLAY` | reproduced exactly |
| `exit-confirm` | **0 %** | `WANT TO EXIT` | reproduced exactly |
| `song-list` | 0.473 % | the entry tutorial, not the list | reproduced — but not the state it names |
| `controls` | 5 % | `CONTROLLER` | same screen; its top three tenths differ, the rest is pixel-identical |
| `calibration` | 5 % | `CALIBRATION WIZARD` | same screen, same shape |
| `leaderboards` | 10.535 % | `CAREER LEADERBOARD` | same screen, its aurora band |
| `download` | 14.62 % | the eStore notice | same screen, its aurora band |
| `title` | 18.28 % | `TO START` | same screen, aurora phase; four other same-build pairs read 0.046 %, 1.978 %, 10.74 % and 1.894 % |
| `main-menu` | 17.71 % | the menu's own rows | same screen, aurora phase |
| `pause` | 56.031 % | `GAME PAUSED` | same menu over a live scene |
| `audio-video` | 13.1 % | the help index | **route miss** — a row short |
| `credits` | 75.384 % | the Audio/Video page | **route miss** — a row short, and the page scrolls |
| `in-song` | 72.777 % | unreadable | a different song: the route accepts *Random Song* |
| `boot` | 84.972 % | — | **not a screen** — one capture was black (mean luminance 4.4 against 75.2) |

Two of the fourteen states reproduce exactly and three more sit under the 1 % default. The rest do
not, each for a different reason — an animated background, a live scene, a route that landed a screen
short, a random song, a race with the splash. `audio-video` and `credits` say the important thing:
**a same-build `percent` cannot tell a change of screen from a change of pixels**, so a run that gates
on it reports a route miss as a visual regression and a visual regression as a route miss.

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

**And on an animated screen there is no settled frame at all**, which the harness has now measured
five times on the title screen: **0.046 %**, **1.978 %**, **18.28 %**, **10.74 %** and **1.894 %**,
the last three with their captures OCR'd as `TO START` — the same screen. The first two suggested a
2.5 % ceiling; the third and fourth are what that ceiling cost, a run of the same build reported as a
failure. The aurora behind the title is an animation, what differs is the distance between two runs'
phases, and no settle time removes it. So the state is not given a ceiling at all: it carries
**`Settled = $false`** in [ui_states.ps1](../../scripts/ui_states.ps1), its diff is reported, and
**OCR is the control while the diff is corroboration**.

**A run gates on the diff only where the diff can be believed** — a state that has a settled frame,
compared against a baseline measured at the same `-CompareScale`. Anything else is marked
`Settled = $false` and gates on its needle, and the report says so in `diff.settled` and
`diff.gates_pass`. `-MaxDiffPercent` (default **1.0**) and a per-state `MaxDiff` therefore apply to
the settled states only. Raise either deliberately, not to make a test pass.

## Adding a state

1. Add an entry to [scripts/ui_states.ps1](../../scripts/ui_states.ps1) with `Route` (an action list
   in [drive_ui.ps1](../../scripts/drive_ui.ps1)'s grammar), `Needle`, `Settle`, `Note` and — only if
   the text needs it — `Crop`. Add the name to `$script:UiStateOrder` too.
2. Run it once with `-Ocr` and no baseline: the report's `ocr.text` is what the screen says. Put the
   shortest unambiguous fragment of that text in `Needle` — not a whole sentence, which OCR noise
   will break (measured: the title screen reads `BLITZ PRESS@TO START`, so the needle is `TO START`).
   If the needle does not appear, the text is not there to be read: try a `Crop` of the band it is
   drawn in before weakening the needle to something the full frame does read.
3. If the state animates, raise `Settle` and then **measure two same-build pairs**. If they disagree —
   and on this build they do, by 10–18 % on the aurora screens — the state has no settled frame: mark
   it `Settled = $false` and drop any `MaxDiff`. Do not widen `MaxDiff` to cover the spread; at 15 %
   it is no longer a threshold.
4. Record the baseline with `-BaselineDir` only once the state reads the same way twice, and check
   that both readings are of the right screen: a `percent` is not evidence that the route arrived
   where the state's name says it did.

## Open, and left open on purpose

- **The How to Play screen's first row is not what its description implies.** Entering HELP & OPTIONS
  and accepting nothing captures a screen whose title reads `How to PLAY` and whose visible rows read
  `BASIC TUTORIAL` and `ADVANCED TUTORIAL`; one row down from there is `CONTROLLER`, which is how
  [acceptance_screens.ps1](../../scripts/acceptance_screens.ps1)'s table reaches Controls. So row 0 is
  a tutorial entry, and that document's "a submenu titled How to Play over four pages" describes rows
  1–4 and does not account for row 0. Not resolved here — it belongs to S2's flow map, and the harness
  only reports what it saw.
- **`in-song` and `pause` have no needle.** Nothing has pinned down what they draw, so they capture
  and report an unknown expectation rather than a string that could pass for the wrong reason. `pause`
  did read `GAME PAUSED` in the baseline run, which is a candidate; `in-song` cannot have a stable one
  until the route stops accepting *Random Song*.
- **The menu chain's routes are not deterministic, and nothing waits for a screen to appear.** The
  route language is taps and `wait:N` ([drive_ui.ps1](../../scripts/drive_ui.ps1)); a dialog that
  takes longer than its `wait` leaves the next tap on the dialog, and the run captures whatever that
  produced. Measured on the baseline run: `main-menu` captured the "Proceed in Offline Mode?" dialog,
  `audio-video` the help index, and `credits` the Audio/Video page. Two of those three have a needle
  that did not read — the harness noticing — but `song-list` is the counter-example: both of its
  readings were the one-time entry tutorial, so it agreed with itself about the wrong screen. Until a
  route can wait on a needle, a menu-chain `percent` is not a measurement of the screen it names.
- **The harness can change what the next run sees.** `in-song` starts a song and the guest writes its
  save; the entry tutorial that `song-list` captured is the kind of thing a save decides. A baseline
  set is only comparable to another taken from the same save state, and nothing records that yet.
- **`-Resume` compares across sessions.** It reuses a capture because the run is a long sequence of
  boots; the run behind the table above reused a morning capture and compared it against an evening
  one 11 h later. The build matched, so the comparison is legitimate — but `same_build` is the only
  thing it checks, and nothing about the desktop, the save or the machine's load is checked at all.
- **The cost is in `frame_diff`.** A capture run measured **46 s** end to end; a compare run **154 s**,
  of which most is that script's per-pixel PowerShell loop over a 3840×2160 frame. The harness is a
  research tool, not a `ctest` target, and this is why.
