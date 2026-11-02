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
| Loading screen, begin song (Confirm or Start) | `loading.lbl` uses locale key `loading_press_button`, whose English text is `PRESS <alt1>A</alt1>TO BEGIN`. The loading screen accepts Confirm or Start after load completion; the label is filled on enter. | The displayed A is not sufficient to identify which of the two accepted actions should be advertised. Choose and document the hint's action, then update its guest label when binding or family changes. |
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
the original text blocks. An owner screenshot is still needed to establish
whether the separate private texture is uploaded and how sharp it appears.
