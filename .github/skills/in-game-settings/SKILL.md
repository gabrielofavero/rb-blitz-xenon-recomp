---
name: in-game-settings
description: 'Use when adding, editing, renaming, moving, hiding or reorganising a row or a page of the in-game Help & Options settings surface, when choosing what kind of control a setting gets, or when a settings page or a row label has to be laid out. Covers the row kinds (boolean, horizontal select, submenu, boolean-with-submenu, one-shot choice, action, info, retail route, confirmation), the six-visible-rows page budget and how to split a longer page into submenus, the row-label text budget (a long label is drawn without its value), the label/state/caption rules, provider visibility, hot-vs-restart strategy, and the tests a change owes.'
---

# In-game settings — adding a row, and laying out a page

Every player-visible setting is one row of the merged **HELP & OPTIONS** surface, and the whole surface
is data in one file: `src/ui/settings_screen.cpp` (`AllPanels()` at `:444`), generated into compiled DTA
by the same module. So adding a setting is a **layout decision first** (which control, how the page fits,
how the text fits) and a storage decision second. This skill is the layout half; the store, the contracts
and the guest bridge are `../../../docs/settings/contracts.md` and `../../../docs/settings/architecture.md`.

The two hard numbers below are measured, not conventions: **6 rows visible per page**, and a row line of
**at most 28 characters** before the engine stops drawing the row's value.

## When to use

- Adding, editing, renaming, moving or deleting a row or a page of the merged settings surface.
- Deciding what kind of control a setting should get.
- A page has more rows than fit, or a row's label is too long.
- Grouping rows into categories/submenus, or re-parenting an existing page.

## When not to use

- The guest bridge/transport, the store, the service, migration, hot-apply → `../../../docs/settings/architecture.md`,
  `../../../docs/settings/contracts.md`.
- The retired standalone settings application → `docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md`
  §17-§19 (the retirement record). Every setting is an in-game Help & Options row now.
- Installer pages → `docs/settings/menu.md` part 4.
- The general rules for writing the docs a change owes → the `documentation` skill.

## What owns a row

| File | Role |
| --- | --- |
| `src/ui/settings_screen.h` | Every frozen name (record paths, list name, panel/screen names, row symbols), `kMaxHelpTextChars`, the help table API |
| `src/ui/settings_screen.cpp` | **The table**: `Kind` (`:243`), `Row`/`PanelSpec` (`:263`, `:285`), the row factories (`:342`–`:389`), `AllPanels()` (`:444`), `HelpTextFor` (`:1489`), `Labels()` (`:2105`), and the DTA generator (the main menu's *Play Current Song* row is its own section, `AddPlayCurrentSongRow` at `:2004`) |
| `tests/settings_screen_tests.cpp` | Pins the shape: `TestPlayCurrentSongRow` (`:718`), `TestLabels` (`:1610`), `TestHelpTextsCoverEveryRow` (`:2184`), the root row order and the list ownership |
| `docs/settings/plan.md` §3 | The row-by-row specification to keep in step |
| `docs/settings/menu.md` part 1 | What the surface actually is, measured (`[boot]` evidence and screenshots) |

A row is authored by a **factory**, never hand-built: `BoolRow`, `SelectRow`, `SubmenuRow`,
`BoolSubmenuRow`, `ChooseRow`, `ActionRow`, `InfoRow`, `RouteRow` (`src/ui/settings_screen.cpp:342`–`:389`).
Each factory takes the row's one-or-two-line **help caption** as a required argument.

## 1. Pick the row kind

| Kind | Factory | The player sees | Confirm (A) does | Use it when | Live example |
| --- | --- | --- | --- | --- | --- |
| `kBool` | `BoolRow` | `Label: On` / `Label: Off` | flips it and commits | a genuine yes/no | `Cache DLC Library`, `V-Sync`, `4:3 Letterboxing` |
| `kSelect` | `SelectRow` | `Label: <choice>` | advances to the next choice and **wraps** (`(index + 1) % n`); the arrows step | a short, ordered set of discrete values the player cycles | `Camera Angle`, `Game Speed`, `Gems`, `Resolution`, `Renderer` |
| `kSubmenu` | `SubmenuRow` | `Label` | enters `screen` | to group related rows — **the tool that keeps a page within 6 rows** | `General`, `Interface`, `Tracks & Gems` |
| `kBoolSubmenu` | `BoolSubmenuRow` | `Label: On` / `Label: Off` | enters `screen` (the choice list, `Off`/`None` included) | a feature that is on/off **and** has its own choices — the game's own compound-row model | `Anti-Aliasing`, `Main Menu` |
| `kChoose` | `ChooseRow` | `Label` | sets the setting to **this row's own token**, commits, **and leaves** | one of a few mutually exclusive alternatives that should apply immediately | `Rock Band Blitz`, `Rock Band Blitz Ultimate`, `Rock Band Blitz Demo` |
| `kAction` | `ActionRow` | `Label` | invokes the setting once | a do-it-now command with no stored state | `Unlock Online Only Achievements` |
| `kInfo` | `InfoRow` | `Label` | nothing, deliberately | an entry that must be **present but inert** | `HELP & OPTIONS` in the Main Menu mirror |
| `kRoute` | `RouteRow` | `Label` | points the retail screen's `member` at us, then enters it | to adopt an existing retail page instead of rebuilding it | `Controls`, `Calibration`, `How to Play`, `Credits`, `Audio` |
| `kRestart` / `kClose` / `kCalibration` / `kAchievements` | mutating an `InfoRow` | the confirmation buttons | restart / dismiss / the retail wizard / the achievement grant | the confirmation panels only | `Restart now`, `Close` |

The decision, as questions:

1. On/off with nothing else? → `kBool`.
2. A handful of values stepped through in place? → `kSelect`. **Choices carry a stable token + a display
   label; never an index and never the label as the wire value** (`docs/settings/plan.md` §3.3: "never
   persist list positions").
3. On/off *and* it owns choices? → `kBoolSubmenu` (never a web-style expanding form — show the state,
   enter the choice list, pick `Off`/`None` or an enabled choice there).
4. A group of related rows? → `kSubmenu`.
5. One of a few alternatives that applies and leaves? → `kChoose`.
6. A command? → `kAction`.
7. An existing retail page? → `kRoute`.
8. Present but must not act? → `kInfo`.

## 2. Fit the page — **six visible rows**

The list window draws **six rows**. A seventh row is still reachable by moving down (the list scrolls),
but nothing on screen says so — **no scrollbar, no arrow, no partly cut row** — so a player who does not
already know the page has more will never find them. Six is therefore the page budget, not a hard engine
cap.

Evidence: the 8-row page (`Interface → Hide Items → Main Menu`) draws its first six rows and no scroll
element — `out/_swap/p5b/shots-basescroll/05-main-menu-0.png`; the six-row gameplay page —
`out/_swap/p5b/shots-sixscroll/02-gameplay-0.png`. (`docs/settings/plan.md` §5.2 and §3.4 record that
"5 is a retail list size, not a cap" and that an 8-row list *scrolls* — both true. Scrolling is possible;
it is just not *discoverable*, which is why the budget is 6.)

**When a page would exceed six rows, reorganise — do not let it scroll:**

1. Count the rows the page would show, **including** the provider-only rows that appear only with Ultimate
   installed (base 5 / Ultimate 6 at the root, `docs/settings/plan.md` §3).
2. Group by intent and move the overflow into one `SubmenuRow` (or `BoolSubmenuRow`), then keep the parent
   at six or fewer. Precedent: the Gameplay page's note/gem rows became the `Tracks & Gems` submenu so the
   page stayed a page.
3. Keep the **root** at six or fewer — it is exactly six with Ultimate and five on base.
4. Never leave an empty group behind: a group with no available rows must be omitted, not shown as an
   unfocusable row (`docs/settings/plan.md` §3).

## 3. Fit the line — the text budget

A row draws **one** line: `<label>: <value>` (a boolean's value is `On`/`Off`; a select's is the current
choice). A line that is too long is drawn **without its value** — the label wins the space.

| Budget | Value | Why |
| --- | --- | --- |
| Whole drawn line, label + `: <value>` | **≤ 28 characters** | measured bracket: 27 draws complete, 33 loses the value |
| Label alone | **≤ 22 characters** | leaves room for `: Off` and for the longer select values (`: Default`, `: Borderless`, `: 3840x2160`) |

Measured (`out/_swap/p5b/shots-sixscroll/02-gameplay-0.png`): `Disable Depth of Field: OFF` (27) draws
complete, while `Disable Random Track Wiggles: OFF` (33) is drawn as `Disable Random Track Wiggles:` —
**the `OFF` is not drawn at all**. The capture reads back that way by OCR and by eye.

When a line does not fit:

- **Shorten the label**, keeping the game's own vocabulary: drop filler first (`Disable Random Track
  Wiggles` → `Track Wiggles`); reuse an acronym the title already uses (`HUD`, `FXAA`, `AA`).
- **Move the explanation to the help caption**, which is not laid out in the row and has no width
  problem — see §4.
- Do **not** shorten the value to fit the label; a value the player cannot read is worse than a shorter
  label.

Over-budget labels to fix or verify (they may be drawn without a value): `Disable Random Track Wiggles`
(28), `Unlock Online Only Achievements` (31), `Rock Band Blitz Ultimate` (23).

## 4. Label, state and caption rules

- **Sentence case, like the retail rows beside it.** `General`, `Anti-Aliasing` — not the main menu's
  `GENERAL`. `TestLabels` (`tests/settings_screen_tests.cpp:1610`) fails any label of ours with no
  lowercase letter. Two deliberate exceptions: the **Main Menu mirror** (it lists the menu's own entries,
  so it mirrors their capitals: `PLAY`, `HELP & OPTIONS`, `EXIT GAME`) and the **acronyms** (`HUD`,
  `FXAA`).
- **Boolean state is `: On` / `: Off`**, never `ENABLED` / `DISABLED` (owner decision; the retail
  checkboxes are brought into line by `LocaleOverrides()`).
- **Every row needs a help caption**, 1–2 lines, ≤ `kMaxHelpTextChars` (128) (`src/ui/settings_screen.h:306`),
  enforced by `TestHelpTextsCoverEveryRow` (`tests/settings_screen_tests.cpp:2184`). It is shown while the
  row is highlighted (the overlay wraps at 72% of the display), and it is the right home for anything that
  does not fit the row line. A new factory call cannot omit it — the caption is a required argument.
  Adding a row is inside the standing overlay exception of `.github/copilot-instructions.md` ("UI:
  guest-rendered"): its label/value and caption are drawn the way the rows beside them already are. Any
  **new** host-side overlay beyond that surface still needs the owner's authorization.
- **Labels are unique, and every row symbol has one.** A row symbol with no locale entry draws as the
  bare symbol; `TestLabels` pins both directions.
- A row's `setting` is the stable `<category>.<id>` id (`docs/settings/contracts.md` §5.1), never a label
  and never a position.

The live text bound is `kMaxHelpTextChars`; the comment beside the `Row` description in
`src/ui/settings_screen.cpp` refers to a `kMaxHelpColumns` that no longer exists — fix the comment, not the
test, if you touch it.

## 5. Visibility and application strategy

- **Never ship a dead row.** A row that needs the Ultimate provider is marked `provider_only` and is
  dropped when the gate query answers false, so a base install shows five root rows and an Ultimate install
  six. A row that appears must work, in this boot, for this install.
- **`hot` vs `restart`** (`docs/settings/plan.md` §5.5): a row is `hot` only when its value has a
  **measured** live effect; otherwise it is `restart`. Applying an unproven row live is not allowed.
- **The restart prompt opens only when leaving the merged root** (owner decision, `docs/settings/status.md`),
  not per row — a commit that needs a restart is not allowed to interrupt a mid-page edit.

## 6. Verify

```powershell
cmake --build --preset win-amd64-release --target rb_blitz_settings_screen_tests
ctest --test-dir out\build\win-amd64-release -R settings_screen

# Read a definition back as text (compiled DTA is not greppable):
.\out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe dump-def settings_general
```

Two layout checks the tests cannot make for you, so make them by eye on a capture: the page draws **six
rows or fewer**, and every row shows its **value**.

## Anti-patterns

- **A page that needs scrolling.** Split it with a submenu instead of relying on the invisible scroll.
- **A label long enough to cost the row its value.** Shorten the label or move the words to the caption.
- **A boolean drawn as a submenu, or a compound feature drawn as a plain boolean.** On/off *and* choices is
  `kBoolSubmenu`.
- **Persisting a list position or a label.** Wire the stable token.
- **A row present but dead** (a provider row on a base install), or an empty group left as a row.
- **Forgetting the caption**, or citing a document/section the change did not also update — the docs are
  part of the change (see the `documentation` skill, §4).
