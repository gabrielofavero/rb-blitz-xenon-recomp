# Guest action-hint routes (Controller Layout P10)

Status: main-menu footer icons, Y rebound and live refresh reported working, 2026-11-02; other P10 routes open.
The owner accepted P9's mapping-row paging visual check. Other P10 routes
remain open.

This inventory distinguishes the *action* a hint promises from the button character
currently drawn. The effective Interface Mapping owns menu, pause and dialog
actions; Gameplay Mapping owns song actions. `input::icons::First` already selects
the first effective source in the active device family. The shared `InlineHelp`
footer now consumes it. The mapping rows use `input_icon_rows::Build` and are P9, not
evidence that ordinary hints are correct.

| Surface and action | Guest route measured in the installed Xbox content | Current P10 state |
| --- | --- | --- |
| Menu and Help & Options footer actions | The authored settings panels in `src/ui/settings_screen.cpp` use the native guest list and `kAction_*` handlers. The owner trace measured visible Confirm/Cancel `InlineHelp` labels and action IDs. | The owner confirmed default main-menu footer icons and Y after rebinding Confirm, then reported the remaining main-menu live checks working as well. The shared footer route uses a private early-glyph font scene and numbered alternate style. Other panels still need explicit checks. |
| Song Select footer | `song_select_panel` calls `InlineHelp.ihp set_action_token` for at least `kAction_ShellOption`; its other cases and visibility vary with state. | Shared renderer now handles known action IDs; visible actions and owner acceptance need a panel audit. |
| Inbox footer | `ui/inbox/gen/inbox.dtb` has an `update_inline_help` method that clears Confirm, Cancel, Start, Option and ShellOption, then conditionally sets them. | Shared remap-aware renderer is implemented; Inbox-specific image and refresh behavior remain unverified. |
| Recommendations and Challenge footers | Their installed DTBs assign Confirm to `inline_help_continue`, `store_offer` or `challenge_play_song`, and Option to `rec_option_prev_audio`; the Challenge panel uses `challenge_help.ihp`. | Same action-token seam, with distinct `InlineHelp` object names and state-dependent captions. |
| Leaderboards, store and upsell footers | Installed DTBs assign actions to `InlineHelp_leaderboards.ihp`, `cancel.ihp`, `store.ihp` and `InlineHelp.ihp`. The leaderboard ShellOption caption can be a variable token. | Keep the action identity from the call, including variable captions. Check visibility and disabled states. |
| Pause and results footers | The pause DTB handles `kAction_Cancel`, `kAction_ViewModify` and `kAction_Start`. Results DTBs include `set_action_token` calls through an expression that finds `InlineHelp.ihp` in `results_shared_panel`; a simple named-object search misses these. | Trace the scene/shared-panel draw route and exact visible actions. |
| Loading screen, begin song (Confirm or Start) | `loading.lbl` uses locale key `loading_press_button`, whose English text is `PRESS <alt1>A</alt1>TO BEGIN`. The loading screen accepts Confirm or Start after load completion; the label is filled on enter. | Advertises Interface Confirm, the action represented by the retail A; Start remains an accepted alternative. The prompt's alternate font and six locale strings now use Confirm's private glyph tile. The loading UI handler publishes binding and family changes. Generated-record checks pass; the owner reported the visual result good on 2026-11-02. |
| Tutorial and loading tips for note play, power-up deploy, continue | English locale contains `loading_tip_*` strings, including `Play left note: {string:lane0_swing_token}`, `Play right note: {string:lane1_swing_token}`, power-up text with `<alt>X</alt>`, and `Press <alt1>A</alt1> to Continue`. | Inspect each draw context and assign its Gameplay or Interface action. A locale-wide X/A substitution would be ambiguous. |
| Calibration instructions | `cal_audio_desc_pad` and `cal_video_desc_pad` say to press `<alt1>A</alt1>`; the calibration scene has its own button images. | Actionable calibration hints need an explicit measured route and outcome even though calibration-specific scene art is outside the button-sheet replacement scope. |
| Standalone Start prompt | English locale key `press_start_to_continue` spells `Press START to continue`. | Inspect caller and action before replacement; this is text, not an `InlineHelp` token. |
| Other inline glyph strings | The English locale has 68 lines with `<alt...>` markup or `Press` text, including account-link and power-up instructions. | Enumerate callers and locale variants. Static text matching alone does not prove a line is actionable in the current context. |

## Evidence and next measurements

The static scan parsed all 65 installed `assets/game/360/ui/**/*.dtb` records
with the production `rb_blitz_loading_record_inspect` decoder. Direct
`set_action_token` calls occurred in Challenge, Inbox, Recommendations,
Song Leaderboards, Song Select, Store and Upsell. Results Hub also calls
`set_action_token` through a `results_shared_panel` object lookup. This scan
does not cover native C++ calls or scene object properties, so absence of a
call in a DTB is not evidence that its footer is absent.

- `out/_swap/inbox_retail.tree.txt`: `update_inline_help` and its
  `InlineHelp.ihp` action-token calls. The decoded tree is local, ignored
  evidence from the installed record; the tracked explanation is also in
  [messages.md](messages.md) §4.
- `out/_swap/song_select.tree.txt`: action-token calls in the song-select panel.
- `out/_swap/ui_objects.tree.txt`: the fixed `InlineHelp` joypad
  `action_chars` table. This is the likely shared footer character seam, not
  yet a demonstrated safe live-write target.
- `ui/resource/inline_help/gen/default.milo_xbox`: its installed scene's
  string table references `text.lbl`, Pentatonic and `buttons.milo`. The
  footer therefore has a distinct scene/font route from P9's mapping-list
  `input_icons.milo`; sharing P9 image tiles would require an explicit font
  and publication design, not just a change to `action_chars`.
- `out/_swap/locale.tree.txt`: `inline_help_fmt`, `inline_help_*`,
  `loading_press_button`, calibration descriptions and gameplay tips.
  [loading-cancel.md](loading-cancel.md) §1 identifies the loading label and
  its Confirm-or-Start handler.
- `src/input/input_icons.{h,cpp}`: `CaptureActive` and `First` provide the
  effective source and art; `src/ui/input_icon_rows.cpp` demonstrates the guest
  font atlas route for mapping rows only.

The static `InlineHelp::DrawShowing` body (`0x824D85E8` in the pinned
recompiled image) walks two parallel arrays: one 28-byte entry per displayed
action and one pointer per `UILabel`. It invokes each label's draw method; it
does not itself resolve a binding or compose a character. The action entries
and label update method were pinned by the trace and the update path below.
The owner's `RB_BLITZ_P10_TRACE=1` run in
`out/build/win-amd64-release/logs/rb_blitz_470.log` recorded visible footer
entries with action IDs 1 (Confirm) and 2 (Cancel), each paired with its own
label pointer. The recompiled `InlineHelp::UpdateLabels` at `0x824D9330`
iterates the same 28-byte entries and calls the formatter at `0x824D8FF0`
with the label, the action character and the caption. The locale format is
`<alt>%s</alt> %s`; the installed inline-help scene's second font is
`buttons.milo`. This confirms the action-aware label seam.

`src/hooks/action_hint_probe.cpp` now maps the measured action IDs through
the `ui_objects.dtb` action-character table, resolves each target with
`input::icons::First`, and refreshes already-visible footer labels when the
effective-input or last-device/layout revision changes. The generated payload
points the inline-help scene's button style at `inline_help_icons.milo`; the
texture provider reserves 15 private image slots for those actions while P9
rows retain their accepted glyph codes. Unknown actions keep
their original guest formatting. The bounded `p10_trace` output remains
available for the owner-run visual check.

The owner checked the first build in both retail and Xbox Series layouts,
including a Y rebound: captions remained but icons were blank. A second build
with high private glyph codes also remained blank. Logs `rb_blitz_475.log` and
`rb_blitz_476.log` prove that the footer formatter received F2/F3 and the GPU
composed all 180 atlas slots, so the failure lies after those steps. The
installed locale's `inline_help_fmt` uses `<alt>` whereas P9's proven private
font uses `<alt1>`. All six installed locales carry the same format. The new
generated records change that format to `<alt1>%s</alt1> %s`, point the footer
scene at a separate `inline_help_icons.milo`, and alias early private glyph
codes to the reserved footer atlas slots. This keeps P9 row glyphs stable and
avoids relying on unproven high codepoint rendering in this label style.
`rb_blitz_menu_payload_tests check-p10-records` passed 309 checks against the
installed Xbox assets, including the generated locale and scene references.
In `rb_blitz_477.log`, the latest owner run formatted Confirm/Cancel with early
glyphs 0x81/0x82 and composed 180 atlas slots, including footer tiles 102/103.
The owner confirmed that default main-menu footer icons now appear and Confirm
shows Y after rebinding. In a follow-up to the same-screen refresh and active-family
checks, the owner reported that these work as well; the trace records repeated
active-family and binding-revision changes. This is main-menu evidence only. Loading, tips,
calibration and other inline/localized-text routes in the table remain
unimplemented and unproven. Do not treat the shared footer route as coverage
for those surfaces.

On 2026-11-02, a screenshot showed the footer keycaps still soft after changing
the resampler. The shared 1024-pixel font atlas had reduced every icon to a
32-pixel tile. The footer's 15 reserved tiles now use 48-pixel images in 50-pixel
font cells; the 165 mapping-row tiles use 32-pixel images in 34-pixel cells below
them. The footer scene has its own 50-pixel cell dimensions and UVs, while the
mapping font keeps its separate 34-pixel dimensions. The generated-record check
verifies both scenes' glyph geometry, and the atlas test checks every tile's
gutter and preserves the retail texture blocks. A release build and host tests
pass. The owner's next screenshot showed soft `Enter` and `Esc` keycaps in both
the mapping rows and footer. A later attempt to replace these with generated
keycaps changed the appearance without authorization and was reverted. The
original package images remain in use; the rendering blur still needs a fix.

The first 2048-square attempt kept the retail texture names and bitmap source
path. The owner's next screenshot showed `8` and `9` in the footer and font
fragments in mapping rows. `rb_blitz_484.log` had no private-font texture match:
the guest continued sampling the retail Pentatonic texture at the new UVs.
The copied scene now gives its Tex, Mat and bitmap source records separate
equal-length names, while retaining the font links. Its 2048 texture has
64-pixel mapping icons and 96-pixel footer icons. The coordinates are exactly
twice the proven 1024 atlas layout; if the guest still aliases the copied
texture, the runtime also composes the original 32/48-pixel icons into the
retail font's unused band at the same normalized UVs. Tests verify the new
scene's dimensions, both font geometries, and that the retail fallback keeps
the original text blocks. On 2026-11-02 the owner confirmed the latest footer
and mapping-row appearance looks good. The private-texture upload path is still
inferred from host checks; the visual result is accepted.

The next P10 route is `loading.lbl`. The installed loading scene carries five
`buttons.milo` font references: three earlier labels, the begin prompt, and
the rotating tip. Its `loading_press_button` token precedes only the prompt's
reference. The generated scene changes that one reference to
`inline_help_icons.milo`, the same private action-hint font the footer uses,
and each installed locale replaces
the retail A inside `<alt1>` with the private Confirm glyph. The guest still
owns the prompt's text, placement, visibility and Confirm-or-Start handler.
When Controller Layout is disabled, the generated records keep the retail A
and the original loading scene.
The loading-screen UI message hook publishes Confirm's first effective source
when bindings, family or layout change. The release build and generated-record
check pass across all six installed locales. On 2026-11-02 the owner reported
the loading prompt looks good, and identified a separate Space/Search conflict:
the SDK's legacy MnK default also bound Space to A. With a present profile,
the input filter now feeds General Mapping the physical pad and raw key events,
so Space follows Interface Mapping's Start/Search binding alone. The owner
confirmed the Search behavior on 2026-11-02. The same screenshot exposed a
retail-layout Search hint using the Xbox Series Start image. Retail Start/Back
now resolve to the installed Xbox 360 sheet's arrow glyphs; the owner accepted
that image on 2026-11-02. Release builds and the focused input and icon tests pass.

`rb_blitz_488.log`, `rb_blitz_490.log`, and `rb_blitz_492.log` ended at the
same guest allocator null read shortly after a song exit, with Xbox Series
active. The `492` bitmap trace places the failing 1 MiB request in the retail
`rays.tex` texture, after repeated screen loads. Both private font scenes had
loaded 2048-square guest bitmaps even though this run rendered icons through
the retail 1024-square fallback. The scenes now retain 1024-square guest
bitmaps with distinct resource names; their UVs still select the same fallback
pixels. The host's 2048-square composition is downsampled to 1024 for both
private textures and the retail font. This removes 8.4 MiB of resident guest
bitmap payload from the two scenes. The bitmap trace also records guest buffer
addresses and free-list totals on `rays.tex` loads to distinguish cumulative
leaks from fragmentation in the next live run. The loading scene uses the
footer's font directly instead of mounting a third scene.

**2026-11-02 pixel-scope revert.** `rb_blitz_494.log` ended in a guest access
violation (`read of guest 0x00000004`, `fault stage=outside ShellMusic`) after a
song exit on a build whose guest bitmap was already 1024. The crash cause is not
proven. The owner assessed that the larger atlas values were not the cause of
the soft keycaps and asked to return to the original pixel scope, deferring the
image-quality work to a later session. The 2048-square host composition and the
64-pixel mapping / 96-pixel footer tiles, as well as the earlier 48/50-pixel
footer tiles, are superseded by the original
1024-square geometry: 28 columns by seven rows of 36-pixel cells holding
32-pixel images from `kIconTop` 768, with the 15 reserved footer tiles in
physical slots 101..115. The host composes 1024 directly, the private guest
bitmap stays 1024, and `ComposeRetail` copies the icon band from the matching
mip level. Texture identity, separate glyph tables, the loading prompt and the
six-locale routes are unchanged. Release build and the focused `button_scheme`,
`button_scheme_fingerprints`, `menu_payload`, `input_bindings` and
`settings_screen` host suites pass; `check-p10-records` passes 397 checks. The
soft-image issue and live crash verification remain open.

**2026-11-02 heap-crash root cause.** `rb_blitz_495.log` names the failing step
rather than the atlas layout: the loading screen's 1,048,580-byte `rays.tex`
request finds 11,688,352 bytes free across 1,540 blocks but a largest block of
only 678,076, and the allocator's failure path then reads guest address 0x4.
Resolving the fault RIP against `rb_blitz.map` puts it at `sub_8246FA20 + 0x5FC`
(the guest heap allocator), called from `RndBitmap` (`sub_821FC8A8`) and
`RndTex::Load` (`sub_8220CCA8`). An earlier reading placed the fault in the
deleting-destructor adjustor `sub_824929C0`; that identification was wrong and is
superseded by the allocator frame. The same signature predates the dedicated
texture (`rb_blitz_467.log`) and occurred on 2026-10-26 (`rb_blitz_372.log`), so
the fragmentation is cumulative and not caused by the icon font. Two changes
remove the avoidable pressure and give the heap headroom:

- The mod's private texture identity is gone: the icon and hint scenes keep the
  retail Pentatonic texture bytes and names, so both share the retail upload and
  the override replaces only its unused bottom band. No extra guest bitmap
  exists for the icons (about 2.6 MiB saved).
- The generated `config/gen/mem.dtb` raises the Xbox main heap from its retail
  137,000,000 bytes to 160,000,000, the size the PS3 branch of the same file
  uses, so the title creates a larger heap at boot. `RaiseXboxMainHeap` rewrites
  only the `HX_XBOX` / `main` / `size` node; `check-p10-records` parses the
  generated record back and asserts 160,000,000.
- `input_font_alloc_trace.cpp` logs the heap's free total on the first guest
  allocation, which is the live evidence for which definition was served.

Release build and the same focused suites pass; `check-p10-records` passes 395
checks. The shared-retail visual result and the live crash behavior are the
owner's next check.

**2026-11-02 icon appearance evidence.** The soft-keycap question is tracked in
[the icon rendering quality plan](../plans/in-progress/2026-11-02-icon-rendering-quality-investigation-plan.md).
Measured: the bounding-box BC3 encoder costs 1.9-3.1 dB against a least-squares
refined fit on representative keycaps; the straight-alpha mip filter darkens icon
edges by 24-33 levels of 255; and the guest renders a 540p-class frame that the
host presents at 2x, which caps the sharpness any atlas can add. The plan orders
the encoder, mip-filter, private-1024-atlas and 1080p-guest options by risk.

**2026-11-02 encoder and mip fix (appearance).** The plan's first step landed. `EncodeDxt1` and
`EncodeDxt5` now write the best of the bounding-box, principal-axis and least-squares-refined
colour fits (`FitColoursBest`, `src/assets/texture_codec.cpp`), judged by the exact integer error
of the palette they decode to, so the fit cannot be worse than the bounding box it replaced.
`ResizeRgba` filters premultiplied colour and unpremultiplies afterwards, and
`BleedTransparentColours` fills fully transparent texels with the nearest visible colour before
every level is encoded - in the icon atlas and in `BuildPlatformTexture`. Alpha is never modified,
so the atlas geometry, the transparent gutters and the guest heap are untouched: no guest bitmap
changed size or mip count. Host evidence: `store_art` carries the refined-fit, premultiplied-mip
and alpha-bleed fixtures, and `button_scheme`, `button_scheme_fingerprints`, `menu_payload`,
`settings_screen` and the rest of the suite pass on the release build. The on-screen result - the
footer Enter/Esc and one mapping row at 2560x1080 - is still the owner's capture, so the visual
gain is **unproven in the guest**.
