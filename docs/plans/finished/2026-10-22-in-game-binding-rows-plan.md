# In-game binding rows — replacing the Interface Mapping overlay

**Status: implemented (2026-10-25); gameplay verification deferred at the owner's request.** This plan replaces the host-drawn binding
rows (`src/ui/help_overlay.cpp`, `DrawBindingRows` at `:168`) with rows the **guest draws itself**,
filled from host text at page entry, so a capture is visible in-game and without a restart.

It is also the first consumer of the owner's UI directive (2026-10-22): a host may not draw a
panel, box or button over the game's UI; the only tolerated exception is a text-only label
replacement, and this change removes the one that exists.

## Implementation (2026-10-25)

- Main-menu and pause binding lists (Interface Mapping, Navigation, Music Library Options and
  all six gameplay groups) use the live text channel. Assign/Reset child pages keep their guest
  actions and state labels; returning to a listing refreshes its text.
- `read_file` is `sub_8241DD48` (registration at `0x82421A60`). Paths outside our namespace
  forward unchanged, and the existing enabled/forward-only gates apply.
- Correction to the proposed ABI: a DataArray is a **16-byte header**, not the node buffer.
  Allocate it with `sub_82457F50(16, 16)` and call `sub_823FB260(header, count)`; it initializes
  the node buffer and refcount 1. Intern each line with `sub_8245C878` into a type-5 Symbol node.
  Transfer that reference in the type-16 result cell; native variable/list ownership releases it.
  This follows the title's constructor/destructor pair, avoiding a fabricated header or leaked
  page buffers. Symbols use the same list-provider path as the pre-existing authored pages.
- `PanelBindingText` is SDK-free and takes one effective snapshot. Non-binding rows remain locale
  symbols for translation. All text goes directly into Symbol nodes, so quotes/backslashes never
  pass through a DTA parser; the proposed bounded-alphabet restriction is unnecessary.
- Selection, movement and scroll-help cases use integer indices on those pages. Host tests pin
  text counts/order, both contexts, refresh/reset/empty bindings, gameplay tables, and literal
  quote/backslash preservation. No diagnostic-only page is shipped: acceptance exercises the
  actual guest pages.
- `DrawBindingRows`, its opaque covers, its `Multiple` fallback and the cross-thread geometry
  publisher are removed. The existing caption strip remains.
- Release build and six related ctest suites pass. Runtime capture/reset, highlighting and long
  line layout still require gameplay verification with the updated acceptance scripts.

### Runtime attempt and remaining acceptance

The owner deferred further gameplay testing on 2026-10-25. No binding page was reached and no
`rbbz-settings/text/` request was observed; **guest rendering and interaction are not claimed**.
D3D12 attempts `guest-rows-20261025-c/d` and Vulkan attempt `guest-rows-20261025-e` remained
before the menu after content enumeration and shader preparation. The D3D12 thread capture
(`out/_swap/binding-stacks.log`) placed the main guest thread in `sub_8251E390`, waiting on the
GPU command ring. A forward-only comparison (`guest-rows-20261025-f`, channel log confirms
forward-only on) also remained before the menu when it was stopped at the owner's request.
This does not establish the cause of the startup stall.

Evidence is under `out/interface-mapping/p1-rows/`; all runs used isolated userdata. The game and
harness have been stopped. Remaining checks: actual guest text/selection/highlighting, capture
return, Reset All without leaving the listing, long-line clipping and both pause-context routes.
No diagnostic logging or host binding overlay is left in the implementation.

The documentation checker still reports unrelated missing RhythmVerse-plan references; stale
binding-plan references were corrected as part of this change.

The original design and proposed spike below are retained as the implementation rationale.

## 1. The request, as requirements

The owner's report: the Interface Mapping page is a **host overlay** — a host-drawn slab with its
own heading and boxes sitting on top of the guest's page. It reads as a cheap shortcut and must not
be the way a feature is built.

| Id | Requirement |
| --- | --- |
| R1 | Every pixel of the binding rows is drawn by the guest. No new host rectangle, panel or button. |
| R2 | **Hot**: the text reflects the effective bindings whenever the listing page is entered, with no restart and no second copy of the binding tables. |
| R3 | **Parity**: the drawn line is the one `profile::BindingRowText` produces today (`Confirm: A, Enter, A`). |
| R4 | The pages keep their rows and their dispatch (Navigation, Confirm, Go Back, Music Library Options, Reset All; each row's Assign/Reset child). |
| R5 | With the surface absent (the guest never asks), the host draws nothing at all. |
| D1 | **Directive**: a host-side overlay is not an option, even temporarily, once this plan starts. |

## 2. What exists today

- The rows are list symbols; **the row's text is the locale entry keyed by the symbol**
  (`Labels()`, `src/ui/settings_screen.cpp:3229`; `PopulateMenu` at `:1806` fills the list with
  symbols). An unknown symbol draws as its own text — that is the fallback the spike in §4 uses.
- A binding row is `Kind::kStateSubmenu` (`src/ui/settings_screen.cpp:243`): it draws
  `<label>: Custom` / `<label>: Default`, chosen by a `file_exists snapshot/...` gate. It cannot
  draw an arbitrary string.
- Because a symbol's text is **baked into the payload's locale at boot**, P1 of the restructure plan
  drew the values over the guest's label instead (`DrawBindingRows`), with a per-frame geometry
  snapshot from `src/input/native_menu.cpp` and an opaque cover. That is the shortcut this plan
  retires.
- The settings channel is **one bit**: `file_exists` answers a bool and `write_file` accepts a
  request (`src/hooks/settings_bridge_hooks.cpp:820`, `:828`). Its result cell is
  `{u32 value, u32 type}` with type 0 = int/bool (`:198`).
- Measured earlier (RhythmVerse store work): a DataNode type is **5 = symbol, 16 = array,
  18 = string**, and a DataArray is a node array (8-byte `{value, type}` elements).

## 3. The route: rows whose text the host supplies, drawn by the guest

The guest's own list machinery already supports everything except delivery of the text:

| Fact | Where it was measured |
| --- | --- |
| `UIList` messages: `set_data` 0x8209F52C, `set_provider` 0x8209F538, `selected_data` 0x8209F564, **`selected_pos` 0x8209F574**, `selected_sym` 0x8209F584, `num_data` 0x8209EFC4 | `[static]` name table in the pinned image (`out/_swap/blitz_image.bin`, base 0x82000000) |
| A list label's own text setters exist as DTA messages — `set_display_text_string` 0x820423DC, `set_token_with_suffix` 0x82042140, `set_token_fmt` 0x8209C190 — and `set_display_text_string`'s handler converts the argument node to a string and calls the receiver's virtual text setter (`vt+0x30`) | `[static]` dispatcher at 0x822D39D8 (case at 0x822D3A30) |
| The engine interns a C string for us: `sub_8245C878(out_slot, const char*)` — empty/null → the global empty string, otherwise a global map lookup (`sub_8245C408`) then create (`sub_8246A9F8`) + insert (`sub_8245C6D8`) | `[static]` `generated/default/rb_blitz_recomp.76.cpp:16715` |
| The host can allocate guest memory by calling the guest allocator `sub_823E0C00` | `[measured]` `src/store/store_fetch.cpp:120` (`Allocate`) |
| A DTA builtin can answer a typed value: `file_exists` writes `{bool, type 0}`, so the same result cell carries the other node types | `[measured]` `src/hooks/settings_bridge_hooks.cpp:198` |

**The design.** A page whose rows carry live values is filled from the host:

1. The authored `populate_menu` asks the host for the page's rows, e.g.
   `{var rows {read_file "rbbz-settings/text/<panel>"}}`, then `{set_data {var rows}}` +
   `{set_selected 0}` — the same fill pair the generator already emits.
2. The host answers with a DataArray of interned guest strings, one per row, in
   `PanelRowSymbols(panel)` order. Each string is the line the overlay draws today
   (`profile::BindingRowText`), so R3 is satisfied by construction.
3. Rows no longer need a locale entry: the guest draws the string it was given (an unknown string
   is its own text). Dispatch moves from `selected_sym` to **`selected_pos`**, because the row's
   symbol is now runtime text. The index → action table is generated from the same `PanelRowSymbols`
   vector the host composes, and a test pins the two lists' lengths and order.
4. **Hot** means page-entry hot: a capture happens on the row's Assign page and returns to the page
   that lists it, whose `{enter}` re-runs `populate_menu`, so the new inputs appear on the way back.
   A press that changes values without leaving (Reset All) ends with a repopulate of the same page.
5. `DrawBindingRows` is deleted in the step that flips the pages. The help caption strip
   (`Settings Labels`) is host-drawn text and stays: it is the tolerated label-only exception.

### Why not the alternatives

| Option | Why it is not the route |
| --- | --- |
| Enumerate candidate sources as boolean-gated rows | Open key vocabulary (`key:<name>`), one symbol + locale entry per candidate, a 6-row page budget blown by a real binding, and the *listing* page would still show only `Custom`/`Default`. |
| A DTA-authored list provider | Unproven that a script object can become a `UIListProvider` (`set_provider` expects a native object), and it needs the same text delivery anyway. |
| Rewrite the payload locale (or guest locale memory) at runtime | No reload path is measured; the row text is resolved when the list is populated, and a half-rewritten locale is a fault risk in the engine's string table. |
| Bake the lines into the locale at boot | Not hot: a capture would need a restart, which R2 forbids. |
| Keep the overlay, restyle it | Forbidden by D1. |

## 4. Steps

The spike first: the two engine assumptions (`set_data` draws string rows; `selected_pos` answers
the row's index) are cheap to test **with DTA only**, before any bridge work.

| Id | Goal | Deliverable | Evidence |
| --- | --- | --- | --- |
| B1 | Prove a list draws host-shaped text rows and that `selected_pos` identifies the press | A diagnostic page in the generated surface (behind a diagnostic cvar) whose fill is `{array ("Confirm: A, Enter" "Go Back: B, Escape")}` and whose `select_row` switches on `{<list>.lst selected_pos}` | One boot: OCR reads both lines, a press dispatches by index, zero fault lines |
| B2 | Deliver host text to the guest | `read_file` overridden with the same strong-`REX_FUNC` + forward pattern as `file_exists`; the op `rbbz-settings/text/<panel>` answers a DataArray of `sub_8245C878`-interned strings built in an `Allocate`d guest buffer; an SDK-free text composer (page → row lines) with unit tests | ctest for the composer; one boot where a literal-free page draws the composed lines |
| B3 | Author the live pages | Interface Mapping tree (top page, Navigation, Music Library Options, each row's Assign/Reset page) fills from the op and dispatches by index; the index table is pinned against `PanelRowSymbols` | Live acceptance (`scripts/acceptance_controls.ps1`, `scripts/acceptance_binding_rows.ps1`): values follow a capture and a Reset All without re-entering the surface |
| B4 | Retire the overlay | Delete `DrawBindingRows` and its per-frame geometry use; keep the caption strip; update `docs/settings/plan.md` §3 (the Interface Mapping row's "draws the inputs it currently holds"), `docs/reference/known-issues.md` if the limitation is recorded, `docs/settings/status.md`, and this plan's status | `python .github/skills/documentation/scripts/check_docs.py`; a capture of the page with no host pixels |

## 5. Open questions

| Id | Question | Answered by |
| --- | --- | --- |
| Q1 | Does `set_data` accept **String** nodes and draw them, or must each row be a Symbol interned from the text? | B1 (try both node kinds) |
| Q2 | Is `selected_pos` the data index, or an on-screen position? | B1 |
| Q3 | Where does a row line clip? The overlay's `Multiple` fallback existed because the host covered a fixed label box; a guest row is laid out by the engine, so the fallback should be dropped, not ported. | B1 capture |
| Q4 | Can a source name ever contain a DTA-hostile character (quote, backslash, comma)? `SourceDisplayName` is a bounded vocabulary; a test should pin the alphabet rather than escape defensively. | B2 |
| Q5 | Does the row's highlight/cell width still come from the list's own geometry with text rows? | B1 capture |
| Q6 | The store's search field has the same missing capability (its typed query is drawn on the caption strip today). Moving it to the guest is a follow-up consumer of the same op, not part of this plan. | follow-up |

## 6. How to reproduce

- Image scans: `out/_swap/find_str_refs.py <addr>…` and `out/_swap/strings_in_range.py <from> <to>`
  decode big-endian `lis`/`addi` pairs, and are how the name-table addresses in §3 were found
  (2026-10-22).
- The surface as text: `out\build\win-amd64-release\rb_blitz_settings_screen_tests.exe dump-def
  settings_interface_mapping` (and `dump-def settings_interface_navigation`).
- The current overlay's behaviour: `scripts\acceptance_binding_rows.ps1` (24 steps) — the run whose
  captures show the host rectangles.
- Docs check: `python .github/skills/documentation/scripts/check_docs.py`.
