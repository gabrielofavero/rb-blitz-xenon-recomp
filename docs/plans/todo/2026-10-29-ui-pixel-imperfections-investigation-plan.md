# UI pixel imperfections: source and rendering investigation

**Status: planning, 2026-10-29. No production graphics change has been made.** The owner reported
small breaks or uneven pixels in menu text and star icons at 1920×1080 and 2560×1080, on both
D3D12 and Vulkan. The apparent concentration near the horizontal center is a hypothesis, not a
measured seam.

## 1. Goal and current evidence

| Id | Requirement |
| --- | --- |
| R1 | Reproduce the reported pixels in named UI elements and record their exact output coordinates, resolution, renderer, AA, filtering, and capture stage. |
| R2 | Separate defects in source assets or font rasterization from guest GPU rendering, resolution scaling, and host presentation. |
| R3 | Change code or assets only after a cause is demonstrated by a controlled comparison; verify the fix on both renderers and at 16:9 and ultrawide sizes. |

**[owner report, 2026-10-29]** The examples are the second R in *Play Current Song*, the M in
*RhythmVerse*, the G in *Exit Game*, the slash in *Audio/Video*, and the start of the third gray
star on each *Your Songs* row. Six close-up PNGs (`01.png`–`06.png`) and three 2560×1080 screen
images were supplied in the conversation. Their original full-resolution files are not in this
repository. Renderer and 1920×1080 claims are owner observations, not independently captured in
the investigation below.

**[measured, 2026-10-29]** Disposable D3D12 runs with AA `none` saved the game's guest output
before host presentation. Evidence is gitignored under `out/_swap/graphic-issues/`:

| Capture | Guest/output plan | What it shows |
| --- | --- | --- |
| `wide_menu2/guest-output.png` | 1280×540 × 2 → 2560×1080 | Main menu text, including *Play Current Song* and *RhythmVerse*. |
| `wide_options/guest-output.png` | 1280×540 × 2 → 2560×1080 | Highlighted *Audio/Video*. The supplied `03.png` crop matches a region at output `(1261, 700)` with at most one 8-bit channel level of difference. |
| `wide_songs/guest-output.png` | 1280×540 × 2 → 2560×1080 | Three rows of repeated gray stars. No distinct full-height center seam was established. |
| `720_menu/guest-output.png` | 1280×720 × 1 → 1280×720 | Main-menu text control at scale 1. |
| `1080_none/guest-output.png` | 960×540 × 2 → 1920×1080 | Title screen only; it does **not** validate the reported 16:9 menu examples. |

The close-up match places at least that reported pixel pattern in the guest output. It rules out
the host presenter as the *sole* source of that example, but does not distinguish authored pixels,
glyph sampling, guest rasterization, or a guest render-target/resolve error. The temporary frame
capture and timed input code used for these runs was removed; the final release build passed.

## 2. Investigation decisions

| Id | Decision |
| --- | --- |
| D1 | Keep the original crops and full-size reference images together in an ignored `out/_swap/graphic-issues/reference/` evidence directory when available. Record original pixels; do not diagnose from the chat preview, which may be resized. |
| D2 | Capture the same settled UI state at each stage that is accessible: original/decoded font or icon texture, guest output, and presented window. A guest-output match alone does not prove the upstream source. |
| D3 | Test the position claim directly: compare identical stars across one row and across rows, and repeat after a controlled guest UI position or resolution change. A defect that follows the same asset pixel suggests source/sampling; one fixed to output X suggests a render path. |
| D4 | Keep production fixes narrowly scoped. An SDK change needs a shared Xbox 360 rendering error with a reproducible regression; otherwise use the title's asset or project route as appropriate. Do not add a host overlay to mask the pixels. |

## 3. Work packages and evidence gates

| Package | Work | Done when |
| --- | --- | --- |
| P0. Reproducible corpus | Save the user's original-resolution examples; build a disposable-profile capture route that reaches main menu, Help & Options, and Your Songs without timing-dependent key pulses. Respect the repository's UI-testing preference. Record checksums, config and a settled-frame marker. | The five named examples can be captured twice in one build at 2560×1080; captures and logs live under `out/_swap/graphic-issues/`. |
| P1. Locate pixels | Mark exact anomalous output `(x,y)` values in each capture. Compare the same icon instance at other rows/columns and the same glyph at another position. Repeat the relevant menu states at 1920×1080 and 1280×720. | A table states whether each candidate follows the asset, the guest coordinate, or the output coordinate; ambiguous examples stay labeled ambiguous. |
| P2. Isolate stage | Decode the relevant font atlas/icon source if possible. Compare source texels, guest output and presented-window pixels at the marked sites. Hold scene and settings constant while testing D3D12/Vulkan, AA `none`/`fxaa`/`fxaa_extreme`, and the available filtering modes. Native AA is tested only when offered by the device. | The earliest stage containing each confirmed defect is recorded, with a same-build control capture and no reliance on a moving background pixel diff alone. |
| P3. Fix and regression | If P1–P2 isolate a reproducible error, implement the smallest correction and compare before/after captures at both 16:9 and 21:9, scale 1 and 2 where applicable, on both backends. Check fonts, repeated stars, background and title art for regressions. | The named pixels improve without new seams, a release build passes, and the evidence supports the cause. If the shapes are authored or expected rasterization, close with that finding and no speculative code change. |

## 4. Open questions

- Are the reported text edges defects in the game's font atlas or normal antialiasing of small
  glyphs? The same question applies to the star's left rim.
- Do any anomalies remain at one fixed screen column after changing UI placement, or does the
  apparent center pattern come from centered labels and the middle of a five-star row?
- Does the 1920×1080 menu reproduce the same asset pixel pattern? Only its title screen was
  captured in the first pass.

The evidence directory is local and ignored. Regenerate it as needed; do not add game dumps,
full-resolution owner screenshots, or temporary probe binaries to source control.
