# Portrait UI alignment and calibration

Status: delivered, 2026-11-19. Owner visual acceptance remains unverified.

## Requirements and decisions

- D1: Footer buttons share the menu row palette and hover behavior, with 80% of the menu opacity.
- D2: Footer captions and screen headings center in their reserved guest UI areas.
- D3: Calibration artwork centers on the resting speaker body; touch instructions describe screen taps.
- D4: Calibration results center vertically and use a larger, wrapped mobile presentation.
- D5: Preset navigation highlights the guest background yellow and its arrow black on hover.

The implementation authors guest Milo scenes and uses native input/draw hooks.
No additional host overlay is required. Geometry changes apply to portrait
presentation on every platform; touch calibration wording applies in both
orientations. Windows release builds and offline asset checks are available
locally. Other native platform builds and visual behavior are unverified.

## Execution

| Step | Deliverable | Status and evidence |
| --- | --- | --- |
| 1 | Shared colors, text bounds and heading/footer alignment | Delivered. Shared RGB palette; normal/border/hover alpha is 53.6%/15.2%/80%. Native text rectangles center on the live button or marquee midpoint at 164.6512 guest units. |
| 2 | Centered calibration art/results and reversible touch wording | Delivered. Resting speaker alpha bounds map to local (223.3656,39.78075). All twelve audio/video prompts in six installed locales remove the button phrase on touch and restore it for physical input. Success alone centers at (0,0), fits the viewport and uses two lines on touch. |
| 3 | Preset hover, regression checks and release build | Delivered. Hidden native draw groups prevent duplicate arrow rendering; the preset label draw scopes yellow backgrounds and black symbols. Release build, five focused CTest suites and 965 installed portrait/locale checks pass. |

## Validation, 2026-11-19

- `cmake --build --preset win-amd64-release`: pass. The final test-only update
  also passes `cmake --build --preset win-amd64-release --target rb_blitz_menu_payload_tests`.
- `ctest --test-dir out/build/win-amd64-release --output-on-failure -R '^(menu_payload|native_mouse|last_device|button_scheme|settings_screen)$'`:
  5/5 pass.
- `rb_blitz_windows_test_runner.exe rb_blitz_menu_payload_tests.exe check-portrait-records assets/game/360 out/portrait-ui-alignment`:
  965 checks pass, including source scene refusal, phone/tablet aspects, exact
  footer/menu palette correspondence, speaker centering, private arrow draw
  membership and all six installed locales.
- Documentation checker: final result recorded in `out/portrait-ui-alignment-docs.log`.
- Evidence: `out/portrait-ui-alignment/`, `out/portrait-ui-alignment-build.log`,
  `out/portrait-ui-alignment-test-build.log`, `out/portrait-ui-alignment-tests.log`
  and `out/portrait-ui-alignment-records.log`.

Initial validation found two encoding regressions in the existing credits
expectations and a misspelled record constant in the new asset check; both were
corrected before the passing runs. A separate, broader `check-p10-records`
exploration crashes with Windows exception `0xC0000005`; it is not an acceptance
gate for this change. The focused portrait check independently validates the
calibration locales. Its crash/stack evidence is retained in
`out/portrait-ui-alignment-locales.log` and
`out/portrait-ui-alignment-legacy-debug.log`; that broader test remains unresolved.

## Owner visual checks

The owner performs the game/UI check. Skip/Back colors and hover, Help & Options
Back alignment, marquee headings, calibration artwork/prompts/results, and both
preset arrow hovers remain visually unverified. Check touch, controller and
keyboard/mouse transitions at narrow phone and wider portrait aspects. Native
Linux, macOS and iOS builds have not been run for this change.
