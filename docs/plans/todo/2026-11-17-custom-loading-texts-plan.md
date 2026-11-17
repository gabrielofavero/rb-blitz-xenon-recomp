# Custom loading texts — implementation plan

**Status:** Planning only, 2026-11-17. Static code inspection and read-only
decoding of local retail/Ultimate records completed. No feature implementation,
build, game navigation or visual acceptance in this pass. The inventory below is
a verified starting point; P0 must close the remaining screen/scene coverage
before implementation claims a complete map.

## 1. Requirements

| Id | Requirement |
| --- | --- |
| R1 | Map exactly which messages can appear on every loading screen: screen, phase, label, token, original localized text, selection conditions and effective retail/Ultimate source. Distinguish present locale entries from reachable messages. |
| R2 | Provide a structured replacement file, preferably JSON, that replaces selected messages while all unspecified entries retain their effective original behavior. |
| R3 | Allow inline input images in custom text. Resolve semantic actions through current bindings and controller artwork; support several prompts in one sentence. |
| R4 | Allow separate controller, keyboard, mouse and touch wording for the same message, with a shared default and documented fallback. |
| R5 | Change wording and input images live when the active input changes, including while the loading screen remains visible. No re-entry, restart or new random tip selection. |

The deliverable is a replacement system and an exact inventory. A message editor,
new settings pages, new tip pools, altered randomization and file watching are
outside the initial scope. Hot input switching is required; hot JSON editing is
not required. Do not add a feature toggle solely for this feature.

## 2. Evidence and current behavior

Inspected 2026-11-17. Local evidence is under
`out/observations/custom-loading-texts-2026-11-17/`: decoded records,
`manifest.json` with source hashes, and `ultimate-manifest.json`. These contain
retail data and remain gitignored. Decode with the production
[loading record inspector](../../../tools/loading_record_inspect.cpp), launched
through the Windows test runner. Existing diagnostic binaries were used only
for read-only decoding; they do not establish current-build runtime behavior.

| Source | Finding / implication |
| --- | --- |
| Decoded `ui/loading/gen/loading.dtb`, retail and local Ultimate override | `loading_screen` enters `loading_panel`, calls `display_loading_tip`, and passes the chosen locale token to `tip.lbl set_dynamic_controller_token`. Both decoded scripts are identical in this snapshot. |
| Decoded English `ui/locale/eng/gen/locale_keep.dtb` | Contains `loading_tip_00` through `loading_tip_70`, `loading_wait` and `loading_press_button`. Tip 15 is absent from the selectable array. |
| [menu_payload.cpp](../../../src/settings/menu_payload.cpp), locale transforms and `AddGraphicsLoadingGate` | Already rewrites tip 03's Power-up glyph, supplies preparation status strings, and authors graphics status/hints on the guest connect screen. Extend the composed records rather than replacing the whole payload. |
| [input_font_scene.cpp](../../../src/ui/input_font_scene.cpp), `PrepareLoading` | Loading tip and begin prompt already receive the private inline-help image font. Other loading labels need an explicit font inventory. |
| [action_hint_probe.cpp](../../../src/hooks/action_hint_probe.cpp), `sub_822C45A8` | Loading message-handler hook publishes binding/family/layout-dependent hint images. It currently refreshes images, not arbitrary device-specific tip wording. |
| [last_device.h](../../../src/input/last_device.h), [last_device.cpp](../../../src/input/last_device.cpp) | Tracks controller/keyboard/mouse plus `using_touch`; has 100 ms family debounce and physical activity filtering. Touch currently shares controller artwork. |
| [input_icons.cpp](../../../src/input/input_icons.cpp), `CaptureActive`, `Ordered`, `First`, `Hint` | Resolves effective interface/gameplay actions and physical source images. Reuse its binding and layout semantics. |
| [native_menu.cpp](../../../src/input/native_menu.cpp), [start_prompt.h](../../../src/input/start_prompt.h) | Existing draw-time touch wording handles `loading_press_button` and restores the original when physical input resumes. Compose with this owner rather than letting two writers alternate on one label. |
| [loading-cancel.md](../../engine/loading-cancel.md) §5 | Earlier live locale-memory overwrites did not refresh an entered tip; duplicate objects named `tip.lbl` include a non-label. An address found by name alone is insufficient. |

### 2.1 Screen and phase map established so far

| Context | Text source / known messages | Coverage still required in P0 |
| --- | --- | --- |
| Song load: `loading_screen` / `loading_panel` | `tip.lbl`: selected pool below. `loading.lbl`: locale includes `loading_wait` = “Loading” and `loading_press_button` = the begin prompt. Screen changes `loading_state.ep` between loading and waiting-for-button. | Decode scene state/label assignments to prove both status-token routes, timing, wrapping and any other labels. |
| Tutorial song load, same screen | Normal chooser overrides its result with `tutorial_tip`, initially `loading_tip_00`, when song metadata reports `is_fake_song`. | Find all writers of `tutorial_tip` and whether any entry route bypasses this chooser. |
| Post-start content/save loading: `server_connect_screen` / `server_connect_panel` | `status.lbl` receives state-dependent tokens listed in §2.3; footer hints are separate widgets. | Identify states actually reachable after current offline, discovery, recovery and Ultimate transforms. |
| Graphics preparation, same connect screen | `rbbz_graphics_checking`, `rbbz_graphics_preparing`, `rbbz_graphics_failed`, `rbbz_graphics_failed_data`, `rbbz_graphics_failed_cache`; guest hints include retry/ignore/skip. | Map phase predicates, formatted progress arguments and portrait variant labels. |
| Content refresh: `content_refresh_panel` | DTB only names `content_refresh.milo`; no text assignment in the decoded record. | Decode MILO and route callers. Do not conclude there is no text from the DTB alone. |
| Autosave warning / startup icon / loading spinners | DTBs name `autosave_warning.milo` and `spinner_autosave.milo`. The early host boot icon implementation draws an icon. | Inventory scene-authored wording and distinguish spinner-only surfaces from loading-message surfaces. |
| Other transient loads, including library/store transitions | Not exhaustively inventoried in this pass. | Enumerate guest loading/pending panels and host-owned statuses; explicitly record included surfaces and unrelated progress dialogs. No silent omissions. |

Status messages and tips have separate roles. Replacements for progress/errors
must retain their meaning, required counters and actual available actions. Never
show a random tip in place of a load failure or promise an unavailable cancel.

### 2.2 Exact authored song-tip pool

Each suffix below means `loading_tip_<suffix>`. Retail and local Ultimate use
the same 70 entries. Each row groups entries with identical authored threshold
and weight; it does not claim a measured probability distribution.

| Minimum unlocked Power-ups | Weight | Tip suffixes |
| --- | --- | --- |
| 0 | 1.0 | 00 |
| 1 | 0.3 | 16, 17, 18, 19 |
| 1 | 0.5 | 62 |
| 1 | 1.0 | 01, 02, 03, 05, 08, 09, 10, 11, 13, 21, 22, 24, 26, 27, 32, 34, 35, 37, 38, 39, 43, 44, 45, 53, 56, 57, 58, 64, 68, 69, 70 |
| 2 | 1.0 | 36 |
| 3 | 1.0 | 07, 23, 25, 29, 40, 42, 46, 50, 55, 59, 67 |
| 4 | 1.0 | 04, 06, 30, 31 |
| 5 | 1.0 | 47, 66 |
| 6 | 1.0 | 14, 28, 48 |
| 7 | 1.0 | 12 |
| 8 | 0.3 | 63 |
| 8 | 0.5 | 60, 61 |
| 8 | 1.0 | 20, 65 |
| 10 | 1.0 | 41, 54 |
| 14 | 1.0 | 49 |
| 16 | 1.0 | 33, 51, 52 |

The normal chooser counts configured non-upgrade Power-ups unlocked by the
participating user's cred, sums eligible weights, and selects through its DTA
loop. Preserve that loop; reproduce its exact boundary behavior in P0 before
claiming the threshold table equals the set actually selected at runtime.
`$unlockall && !is_online` uses `random_elem` over the entire array, bypassing
normal gates and the tutorial override. A non-shipping handler can cycle the
array through `loading_tip_num`; it is not an end-user tip-rotation feature.
The ordinary screen entry chooses a tip; no timed rotation appears in this DTB.

The inventory must retain the full original text locally alongside each token,
including multiline strings and platform preprocessor branches. For example,
tip 03 describes deploying an Overdrive Power-up and contains an inline button;
tip 58 describes the historical Typewriter preset and contains two buttons.
Replacing a locale-only tip such as 15 must warn that it has no proven reachable
route, rather than silently adding it to the pool.

### 2.3 Authored connect-screen status map

These mappings are from the untransformed retail `update_state` method. They
are candidate routes, not claims that online/login states run in this port.

| State name suffix (`kServerConnectPanel_...`) | Token |
| --- | --- |
| Inactive | Empty token |
| LoadingSongCache / CreateSongCache / MissingSongCache / CorruptSongCache | `song_info_cache_loading` / `song_info_cache_create` / `song_info_cache_missing` / `song_info_cache_corrupt` |
| LoadingGlobalOptions / CreateGlobalOptions / MissingGlobalOptions / CorruptGlobalOptions | `global_options_loading` / `global_options_create` / `global_options_missing` / `global_options_corrupt` |
| EnterName / EnterPassword | `server_connect_enter_name` / `server_connect_enter_password` |
| NoValidLoginCandidate / IncorrectPassword | `server_connect_no_valid_login_candidate` / `server_connect_incorrect_password` |
| FailedNoAccount / CreatingAccount / Failed | `server_connect_create_account` / `server_connect_creating_account` / `server_connect_failed` |
| EnumeratingContent / Connected / OfflineMode | `server_connect_enumerating_content` / `server_connect_connected` / `server_connect_offline_mode` |
| RegisterTrophies / NoSpaceGlobalOptions | `server_connect_registering_trophies` / `global_options_no_space` |
| Other switch cases/default | `server_connect_connecting`; enumerate effective cases in P0 |

The PS3-only trophy-error branch has a separate formatted token; record it as
excluded platform content for this Xbox 360 guest. Save/cache warnings and
errors require their own state rows even when their screen looks like ordinary
loading. No online service restoration is part of this feature.

## 3. Product and integration decisions

| Id | Decision |
| --- | --- |
| D1 | Render through the guest's existing UILabels, authored DTA and served scene records. No new host overlay is proposed. Spinner-only screens do not acquire a text panel as an incidental implementation choice. |
| D2 | Identify replacements by stable context + slot + original token, with phase/state restrictions where needed. Never match displayed English, array position, a raw memory address, or the first object with a matching name. |
| D3 | Keep guest tip selection, weights, tutorial choice and transition state machines intact. Resolve replacement content after the guest selects its token. Retain that source identity while switching device variants. |
| D4 | Use versioned UTF-8 JSON. Ship an authored example; load an optional `loading_texts.json` from the resolved persistent user-data root, separate from saves, DLC and settings profiles. Missing file means original content. Never edit installed game archives. |
| D5 | Each replacement has locale-specific `default`, `controller`, `keyboard`, `mouse`, `touch` variants. Missing variants use the fallback in §4. Explicit empty/invalid messages do not erase a status label. |
| D6 | Input images reference semantic interface or gameplay actions, not literal A/X/Enter. Resolve against the effective input snapshot, current guest preset and selected controller scheme. Literal historical controls require an explicit physical-source form, separate from semantic action prompts. |
| D7 | Detect text input mode separately from the artwork family. Touch has precedence over controller when `using_touch` is true. Keyboard and mouse remain distinct even where their icon fallback uses the same binding table. |
| D8 | Update the visible message and image publication coherently on the guest thread at a safe poll/draw boundary. No file I/O, full parsing or repeated guest symbol allocation in the frame path. |
| D9 | Initial file loading occurs before affected records/labels are built. File edits apply on next launch; automatic input changes apply during the current screen. Invalid files fall back safely with actionable diagnostics. |
| D10 | Preserve localized begin/touch behavior and original text restoration. One composed resolver owns each affected label; coordinate with `TouchStartPrompt`, dynamic-controller tokens and graphics progress updates. |

## 4. Proposed JSON and rendering contract

Illustrative schema, to be frozen in P1 after P0 confirms context identifiers.
Arrays contain literal text fragments or typed input placeholders; users never
need to author private glyph codepoints or engine font markup.

```json
{
  "schema_version": 1,
  "replacements": [
    {
      "context": "song_loading",
      "slot": "tip",
      "token": "loading_tip_03",
      "locales": {
        "eng": {
          "default": ["Activate your Power-up when you have enough Energy."],
          "controller": ["Press ", {"action": "gameplay.powerup"}, " to activate your Power-up."],
          "keyboard": ["Use ", {"action": "gameplay.powerup"}, " to activate your Power-up."],
          "mouse": ["Activate your Power-up with ", {"action": "gameplay.powerup"}, "."],
          "touch": ["Use the on-screen Power-up control when you have enough Energy."]
        }
      }
    }
  ]
}
```

Use installed locale IDs (`eng`, `deu`, `esl`, `fre`, `ita`, `ptb`). Proposed
fallback order: active locale + active device, active locale + default, English +
active device, English + default, original effective guest message. Document
the English fallback clearly; missing translations never render token names.
The example's touch claim must be checked against actual touch controls before
shipping it. Do not manufacture a touch button through the text system.

Action names form a closed registry, including interface Confirm/Cancel/Start
and gameplay Power-up, lane movement and note actions. Each entry declares its
binding domain. Render the first effective binding in the active source family,
following existing hint order. When there is no binding for that family, use a
truthful cross-family fallback with its real image/legend, or an explicit
unbound variant; never draw an unrelated default controller button. Specify
and test that policy in P1. Touch controls may map to virtual guest buttons,
but touch wording must describe the actual gesture or control.

Reserve and account for glyph slots alongside existing inline-help and mapping
fonts. Repeated actions share slots; several distinct actions in one message
remain visible simultaneously. Use existing keycap/mouse/controller artwork
and readable text legends for unavailable art. An exhausted atlas must yield
readable text, never corrupt another label's tile. Do not put arbitrary strings
into a glyph API that accepts only one character.

Progress/status templates need typed, allowlisted values such as current/total;
retain required values and state semantics. Do not treat JSON as printf, DTA or
commands. Escape literal text for the guest renderer and validate UTF-8, fragment
types, action names, locale IDs, duplicate selectors, allowed slots, maximum
file/message/fragment counts and length. Reject an ambiguous/invalid candidate
snapshot as a unit and keep original content (or the previous valid snapshot
if an explicit reload is added later). Report file, selector and field.

## 5. Live input switching

Reuse `last_device` observations; do not infer the input from a connected pad,
synthetic guest button, focus gain or the configured controller layout. Existing
mouse motion deliberately leaves ordinary keyboard/controller hints unchanged;
it only exits touch after deliberate movement. For this feature, record last
meaningful physical source independently if mouse movement must select mouse
copy in ordinary mode. Preserve the existing hint-family policy elsewhere.
Treat meaningful pointer motion as mouse activity for loading copy, with a
tested threshold/debounce; jitter and touch-generated mouse events cannot win.

Keep a bounded screen-lifetime state: context, slot, original selected token,
native phase/format arguments, actual label identity and rendered revision.
Capture input mode, binding revision, controller scheme/preset, locale and
content revision coherently. Re-resolve only when one changes. Switch within the
next safe guest update after the input tracker commits its debounce; no extra
delay or timer is added. Keep tip identity and load progress unchanged.

Refreshing just the atlas is insufficient when wording changes. Prototype the
actual live UILabel update using its owning panel/loaded directory and confirmed
label class. Preserve its original dynamic-token identity and status formatting,
and restore the current original content if replacement no longer applies.
Discard label references on unload, scene regeneration or recovery. Hooks call
the native body exactly once and retain loading/start/cancel behavior.

## 6. Implementation sequence

All steps below remain unchecked. Move this plan to `in-progress/` when feature
work begins; static planning evidence alone does not complete P0.

| Step | Work and deliverable | Completion evidence |
| --- | --- | --- |
| P0 | Complete the exact screen/message inventory. Resolve archive/provider precedence, current payload transforms, scene state text and call sites across retail/Ultimate, full/trial, startup/discovery/refresh, tutorial, normal song and ready-to-begin. Produce a generated local JSON/CSV containing every original localized string, token, slot, predicates, threshold/weight, format args, source hash, input markup and reachability. Record screens with no text. Add a checked-in semantic/token inventory without redistributing wholesale retail locales. | Reproducible decoder/export command; no unexplained candidate loading surface, missing token, duplicate platform branch or locale-only entry; explicit static vs observed reachability. Account for all 71 tip locale tokens and all 70 pool entries. |
| P1 | Freeze schema, paths, fallback, source/activity policy, semantic action registry and limits. Implement pure parsing/selection/fragment resolution and an authored example. | Headless cases for partial replacements, malformed/oversized files, duplicate selectors, all four input modes, locale fallback, literal escaping, unbound/mixed bindings and multiple images. |
| P2 | Apply replacements through served guest records/native label routes. Preserve selection/state logic, format arguments and localization. Extend private image-font coverage only where inventory proves it is needed. | Generated retail/Ultimate record checks; no-configuration behavior equivalent to current payload; targeted progress/error and glyph-capacity checks. |
| P3 | Add live refresh with one label owner and safe lifetime handling. Integrate existing loading glyph publication, touch prompt rewriting and device activity tracking. | Headless transition sequences controller → keyboard → mouse → touch → controller; same tip stays selected; binding/layout/locale revisions refresh; focus/jitter/idle pad and touch-generated mouse events do not steal mode; unload/re-entry/recovery invalidate references. |
| P4 | Build and perform behavior/visual acceptance on the actual screens. Cover short/long text, multiple icons, all supported label scales, landscape/portrait and the existing render backends. | Current build first; owner chooses manual testing or explicitly authorizes agent UI driving. Capture input changes while a load is active and while waiting to begin, without using a begin action to leave the screen prematurely. Verify real mapped actions match the displayed prompts. |
| P5 | Document the JSON contract, worked replacement examples, inventory and diagnostics; reconcile loading/input studies and packaging. | Documentation checker passes, authored example validates, no retail dumps ship, and acceptance results/remaining limits are recorded before moving to `finished/`. |

Build before requesting behavior tests:

```powershell
cmake --build --preset win-amd64-release
```

Run payload test executables through
`out/build/win-amd64-release/rb_blitz_windows_test_runner.exe`, or their existing
CTest registrations. Headless tests and builds do not navigate the game. Before
any game/UI driving, ask whether the owner wants to test themselves and wait,
unless they have authorized autonomous UI testing for the work's scope.

## 7. Open investigation gates

- Exact effective scene routing and complete localized inventory for loading,
  refresh, autosave and other transient surfaces remain P0 work.
- The live setter for the real `tip.lbl`, its dynamic-controller-token variants
  and device-driven reflow need proof. Old memory-patching measurements are not
  evidence that a correct native label route cannot refresh.
- Tip chooser boundary semantics and every tutorial-tip override need tracing;
  do not silently repair or replace the guest randomizer in this feature.
- Mouse activity semantics require a separate loading-copy source if changing
  the shared family tracker would alter existing UI hint behavior.
- Measure font/atlas capacity, line wrapping and readable text fallback with
  actual labels. Settings caption limits are not a loading-label size budget.
- Decide P0 inclusion for unrelated store/download progress dialogs explicitly;
  none may be mislabeled as fully inventoried by this initial plan.

## 8. Done criteria

R1–R5 are complete when the inventory accounts for every agreed loading surface,
an authored JSON replaces only selected entries, several semantic input images
can coexist, and all four device variants switch live on the same visible
message with correct effective bindings. Missing/invalid configuration preserves
the game, tutorial and progress/error behavior remain correct, and owner visual
acceptance plus relevant headless tests are recorded.
