// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The stable row vocabulary of the in-game Controls surface
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md §14, P7).
//
// One row is one token, exactly the way a settings row is `<category>.<id>` on the guest channel.
// The token is what the generated DTA writes, what the bridge parses, what the input store reads
// and what a test can walk - never a label and never an index:
//
//   input.keyboard_support          a switch (the profile's `[settings]` row of the same name)
//   input.mouse_support             a switch (`mouse_ui_nav`)
//   input.general.<target>          one `[remap]` control (`input.general.a`)
//   input.controller.<action>       one `[gameplay_remap_controller]` action
//   input.keyboard_mouse.<action>   one `[gameplay_remap_keyboard_mouse]` action
//   input.reset.general             Reset All of `[remap]`
//   input.reset.controller          Reset All of the Controller gameplay table
//   input.reset.keyboard_mouse      Reset All of the Mouse & Keyboard gameplay table
//
// The two `input.custom_*` switches are gone (P2 of the interface/gameplay mapping restructure
// plan, D3): a gameplay table is custom exactly when it has a binding row, and the surface says
// so by the rows it shows. An old profile's `gameplay_mapping_*` key is left in the file and has
// no token any more.
//
// The three table prefixes deliberately match `gameplay_remap::TableKindSection`'s vocabulary
// (`controller`, `keyboard_mouse`) so a log line and a menu token cannot disagree about which
// table they mean. SDK-free: the generated menu, the bridge sink and the input store all read
// this one table.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "profile/gameplay_remap.h"
#include "profile/remap.h"

namespace rb_blitz::profile {

// What an `input.*` token names. The values are wire-stable (the generated DTA spells the tokens,
// not these numbers), but keeping them explicit makes a table-driven test possible.
enum class InputRowKind : uint8_t {
  kNone = 0,
  kKeyboardSupport,
  kMouseSupport,
  kGeneralBinding,
  kControllerBinding,
  kKeyboardMouseBinding,
  kResetGeneral,
  kResetController,
  kResetKeyboardMouse,
};

struct InputRowToken {
  InputRowKind kind = InputRowKind::kNone;
  // The target or action name for a binding token; empty for a switch or a reset.
  std::string name;

  bool valid() const { return kind != InputRowKind::kNone; }
};

// True for a token in the `input.` namespace. Matching the prefix is the bridge's rule: a name in
// our namespace is ours, and no content file can have it.
bool IsInputToken(std::string_view token);

// The token as a row, or an invalid row when this build does not know it. A binding name is
// parsed against the target/action vocabulary, so `input.general.nope` is invalid rather than a
// row that silently does nothing.
InputRowToken ParseInputRowToken(std::string_view token);

// The spelling of a switch token.
std::string_view InputSwitchToken(InputRowKind kind);

// The spelling of a binding token.
std::string GeneralBindingToken(remap::Target target);
std::string GameplayBindingToken(gameplay_remap::TableKind kind,
                                 gameplay_remap::Action action);

// The spelling of a Reset All token.
std::string ResetAllToken(InputRowKind kind);

// The table a binding token belongs to; `kNone` for a switch or a reset.
std::optional<gameplay_remap::TableKind> BindingTable(InputRowKind kind);

// The locale symbol of a binding row (`settings_bind_general_a`). A row's canonical symbol is the
// key its caption is filed under, and the two derived spellings a press produces are
// `<symbol>_on` / `<symbol>_off` (or the row's own name for an action row).
std::string InputRowSymbol(std::string_view token);

// The inverse: a canonical menu symbol back to its row. The caption event carries the symbol, and
// the store needs the token to name the row it describes. Invalid when `symbol` is not a binding
// row of this build.
InputRowToken ParseInputRowSymbol(std::string_view symbol);

// The label a person reads for a binding row: the target's own label ("A", "Left bumper") or the
// gameplay action's ("First Note").
std::string BindingRowLabel(const InputRowToken& token);

// The name the Interface Mapping page gives a `[remap]` target (P3 of the interface/gameplay
// mapping restructure plan). The page speaks about what the interface does, not about the pad's
// plastic: `A` is `Confirm`, `B` is `Go Back`, Start is `Search`, Y is `Shortcuts`, Back is
// `Sort`, and the four D-pad controls are `Up`/`Down`/`Left`/`Right`. The seven targets the page
// does not show (D2: X, the shoulders, the triggers, the two stick clicks) keep `TargetLabel`.
//
// The binding token is unchanged either way (`input.general.a`), so a profile written before the
// page was rebuilt binds the same control. `BindingRowLabel` answers from here, which is what
// keeps the drawn row, the overlay's replacement line and the help caption spelling the same name.
std::string InterfaceTargetLabel(remap::Target target);

// ------------------------------------------------------------------------------------------------
// The display spelling (P1 of the interface/gameplay mapping restructure plan, 2026-10-19)
// ------------------------------------------------------------------------------------------------
//
// A binding row draws the inputs it currently holds, not a `Default`/`Custom` state: the owner
// asked for `First Note: A` in the row itself. The profile grammar (`pad:a`, `key:Return`) is for
// files; these functions turn a row into the words a person reads, and they are the only place
// that spelling lives, so the overlay, the help caption and a test cannot disagree.
//
// The display names are deliberately *not* `TargetLabel`/`PadDirectionName`: a row has one line,
// so a pad control reads "LB", "L3" or "D-pad Right" there while the settings label can afford
// "Left bumper"/"Left stick". Key names are SDL's own, title-cased for a row ("q" -> "Q", "right"
// -> "Arrow Right"), and a name this build does not know is passed through unchanged rather than
// guessed at.

// One source as a row draws it: "A", "Right Click", "Scroll Up", "Arrow Right", "L-Stick Up".
std::string SourceDisplayName(const remap::Source& source);

// A whole binding list as a row draws it: "Q, Right Click". Empty for an empty list.
std::string BindingListText(const std::vector<remap::Source>& sources);

// The row's own line: "<label>: <list>", or "<label>: (no inputs)" when the row binds nothing.
// `sources` null means there is no effective binding at all (the analog triggers' pad
// pass-through) and reads the same way. Empty when the token is not a binding row.
std::string BindingRowText(const InputRowToken& token,
                           const std::vector<remap::Source>* sources);

}  // namespace rb_blitz::profile
