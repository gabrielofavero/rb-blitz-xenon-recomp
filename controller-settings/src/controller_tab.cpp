// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the Controller tab's block (controller-settings/src/controller_tab.h).

#include "controller_tab.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "binding_text.h"
#include "binding_table.h"

namespace rb_blitz::profile {
namespace {

// Not constexpr: ImVec4 has no constexpr constructor.
const ImVec4 kWarning{0.95f, 0.75f, 0.25f, 1.0f};

// The two `?` pages. The `###` keeps the display title clean and gives each popup an id of its own.
constexpr const char* kGeneralHelpPopup = "General Mapping###general-mapping-help";
constexpr const char* kGameplayHelpPopup = "Gameplay Mapping###gameplay-mapping-help";

// The trigger reading the SDK treats as pressed: it keeps a trigger as the byte `value >> 7` and
// compares that against HID_SDL_TRIGG_THRES, so the same arithmetic here captures a press at the
// moment the game would see it.
bool TriggerAxisPressed(int axis) { return (axis >> 7) > remap::kTriggerThreshold; }

// Which control an SDL gamepad button is. The order is SDL's, and the SDK's own table
// (rexglue-sdk/src/input/sdl/sdl_input_driver.cpp, xbutton_lookup) walks the same one - the
// pad's buttons and the guest's bits are one for one, which is what lets a binding name either
// side with the same words.
std::optional<remap::Target> TargetForPadButton(SDL_GamepadButton button) {
  switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
      return remap::Target::kA;
    case SDL_GAMEPAD_BUTTON_EAST:
      return remap::Target::kB;
    case SDL_GAMEPAD_BUTTON_WEST:
      return remap::Target::kX;
    case SDL_GAMEPAD_BUTTON_NORTH:
      return remap::Target::kY;
    case SDL_GAMEPAD_BUTTON_BACK:
      return remap::Target::kBack;
    case SDL_GAMEPAD_BUTTON_START:
      return remap::Target::kStart;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
      return remap::Target::kLeftThumb;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
      return remap::Target::kRightThumb;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
      return remap::Target::kLeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
      return remap::Target::kRightShoulder;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
      return remap::Target::kDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
      return remap::Target::kDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
      return remap::Target::kDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
      return remap::Target::kDpadRight;
    default:
      // Everything else has no control of its own to capture into, and that deliberately includes
      // Guide: it is a platform button, no target names it any more, and the runtime keeps it out
      // of the guest's pad state (docs/settings/contracts.md §2.3).
      return std::nullopt;
  }
}

// The vocabulary is the panel's list, so the two must agree about how many controls there are:
// this is the panel's own count of the rows it draws.
static_assert(kControlRows == remap::kTargetCount,
              "the panel's control count must be the remap vocabulary's target count");

// The mouse buttons the grammar names, with SDL's mask for each.
struct MouseCapture {
  uint32_t mask;
  const char* name;
};

constexpr MouseCapture kMouseCaptures[] = {
    {SDL_BUTTON_MASK(SDL_BUTTON_LEFT), "left"},
    {SDL_BUTTON_MASK(SDL_BUTTON_RIGHT), "right"},
    {SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE), "middle"},
    {SDL_BUTTON_MASK(SDL_BUTTON_X1), "x1"},
    {SDL_BUTTON_MASK(SDL_BUTTON_X2), "x2"},
};

// Whether a source is available for play: a pad always is (there is no pad switch), a key follows
// Keyboard Support and a mouse button follows Mouse Support. A source that is not available is
// still listed - presence and availability are different facts (plan R7).
bool SourceEnabled(const remap::Source& source, bool keyboard_on, bool mouse_on) {
  switch (source.kind) {
    case remap::SourceKind::kPad:
      return true;
    case remap::SourceKind::kKey:
      return keyboard_on;
    case remap::SourceKind::kMouse:
      return mouse_on;
  }
  return true;
}

std::string ListeningText(double until, double now) {
  char text[64] = {};
  std::snprintf(text, sizeof(text), "Listening... %.1fs", std::max(0.0, until - now));
  return text;
}

std::string DisabledSourceHint(bool keyboard_on, bool mouse_on) {
  std::string hint;
  if (!keyboard_on) {
    hint = "Keyboard support is off, so the game ignores these keys.";
  }
  if (!mouse_on) {
    if (!hint.empty()) {
      hint += " ";
    }
    hint += "Mouse support is off, so the game ignores these mouse buttons.";
  }
  return hint;
}

}  // namespace

void ControllerTab::Draw(std::size_t first_row, FocusModel& ring, ProfileSession& session,
                         NavAction action) {
  const double now = ImGui::GetTime();
  if (listening_ && now >= listen_until_) {
    status_ = ListeningLabel(*listening_) + " was not changed: nothing was pressed";
    listening_.reset();
    listen_hint_.clear();
  }

  // The block the draw and the ring have to agree about. The shell sized the ring from this same
  // function before the frame; a switch flipped below changes it and reports the change.
  const ControllerBlock block = BlockFor(session.profile());

  // The block's own messages sit above it rather than under it: the tables are taller than most
  // windows, so a line beneath them is a line nobody reads.
  if (!status_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextWrapped("%s", status_.c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();
  }

  // A `[remap]` row this build refuses - Guide, and only Guide - is preserved by a save and never
  // applied (docs/settings/contracts.md §2.3). Saying so is the difference between
  // "the button I bound does nothing" being a decision and being a bug report.
  const std::vector<std::string> refused = remap::RefusedRowNames(session.profile().remap);
  if (!refused.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextWrapped(
        "Guide is not remappable: the '%s' row in your settings file is kept but ignored, and "
        "Guide is never passed to the game.",
        refused.front().c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();
  }

  DrawGeneralMapping(block, first_row, ring, session, action, now);
  DrawGameplayMapping(block, first_row, ring, session, action, now);

  // Only while a listen is running: the edges it looks for are measured against the previous
  // frame, and a panel nobody is capturing with has no reason to poll SDL sixty times a second.
  // A listen that begins on this frame already recorded the state it began in (BeginListening),
  // so the click that started it cannot be captured by it.
  if (listening_) {
    if (const std::optional<remap::Source> captured = CapturedInput()) {
      if (!CommitCapture(session, *captured)) {
        // A source the table refuses must not re-fire every frame while it is held: this frame
        // becomes the "previous" the next edge is measured against.
        SnapshotInput();
      }
    } else {
      SnapshotInput();
    }
  }
}

void ControllerTab::DrawGeneralMapping(const ControllerBlock& block, std::size_t first_row,
                                       FocusModel& ring, ProfileSession& session, NavAction action,
                                       double now) {
  ImGui::SeparatorText("General Mapping");
  // The rule the rows must not repeat: one sentence and a `?` rather than a line of explanation
  // per control.
  ImGui::TextDisabled(
      "Map inputs to controller buttons. While a gameplay mapping is on, these bindings control "
      "menus only.");
  ImGui::SameLine();
  DrawHelpRow(first_row + BlockRowStart(block, block.general_help), ring, action,
              HelpPage::kGeneral,
              "Opens the General Mapping explanation: what these bindings do in menus and in "
              "play, and how Assign and Reset work.");
  ImGui::Spacing();

  const remap::Table table = remap::Table::FromRows(session.profile().remap);
  const std::vector<remap::Target>& targets = remap::Targets();
  std::vector<BindingRowView> views;
  views.reserve(targets.size());
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const remap::Target target = targets[index];
    BindingRowView view;
    view.id = index;
    view.label = remap::TargetLabel(target);
    // What presses the control: the user's row, or the binding Controller Settings ships. Reset
    // only applies to the user's own row - a control on its default has nothing to remove.
    if (const std::vector<remap::Source>* sources = table.Effective(target)) {
      view.nothing_bound = sources->empty();
      for (const remap::Source& source : *sources) {
        view.spans.push_back(binding_text::Span{remap::FormatSource(source), true});
      }
    }
    view.listening = Listening();
    view.listening_here =
        listening_ && listening_->table == TableId::kGeneral && listening_->target == target;
    if (view.listening_here) {
      view.listening_text = ListeningText(listen_until_, now);
    }
    // Only one listen at a time: while one is running, every other row's Assign is disabled
    // rather than quietly stealing the capture.
    view.assign_enabled = !Listening() || view.listening_here;
    view.reset_enabled = table.IsBound(target);
    views.push_back(std::move(view));
  }

  const BindingTableResult result =
      DrawBindingTable("##general_bindings", views,
                       first_row + BlockRowStart(block, block.general_table), ring, action);
  if (result.assign.has_value() && *result.assign < targets.size()) {
    const remap::Target target = targets[*result.assign];
    const bool listening_here =
        listening_ && listening_->table == TableId::kGeneral && listening_->target == target;
    if (listening_here) {
      listening_.reset();
      listen_hint_.clear();
      status_ = "Listen cancelled";
    } else {
      ListeningRow row;
      row.table = TableId::kGeneral;
      row.target = target;
      BeginListening(row);
    }
  }
  if (result.reset.has_value() && *result.reset < targets.size()) {
    remap::Table reset = table;
    reset.Reset(targets[*result.reset]);
    session.profile().remap = reset.ToRows();
    status_ = std::string(remap::TargetLabel(targets[*result.reset])) + " is back to the default";
  }

  ImGui::Spacing();
  if (DrawBlockButton(first_row + BlockRowStart(block, block.general_reset_all), ring, action,
                      "Reset all bindings", table.BoundCount() > 0)) {
    remap::Table reset = table;
    reset.ResetAll();
    session.profile().remap = reset.ToRows();
    status_ = "Every control is back to the keys it ships with";
  }
}

void ControllerTab::DrawGameplayMapping(const ControllerBlock& block, std::size_t first_row,
                                        FocusModel& ring, ProfileSession& session, NavAction action,
                                        double now) {
  (void)now;
  ImGui::Spacing();
  ImGui::SeparatorText("Gameplay Mapping");
  ImGui::TextDisabled(
      "Bind inputs directly to gameplay actions. Each table accepts only its own input type. "
      "Changes apply the next time the game starts.");
  ImGui::SameLine();
  DrawHelpRow(first_row + BlockRowStart(block, block.gameplay_help), ring, action,
              HelpPage::kGameplay,
              "Opens the Gameplay Mapping explanation: the two switches, what each table "
              "accepts, and when changes apply.");
  ImGui::Spacing();

  DrawGameplaySwitch(block, first_row + BlockRowStart(block, block.controller_switch), ring, action,
                     session, /*controller=*/true, block.switches.controller);
  if (block.has_controller_table()) {
    ImGui::Indent();
    // The Controller table has no device gate: a pad has no switch of its own (plan R1).
    DrawGameplayTable(TableId::kController,
                      first_row + BlockRowStart(block, block.controller_table),
                      first_row + BlockRowStart(block, block.controller_reset_all), ring, session,
                      action, /*keyboard_on=*/true, /*mouse_on=*/true);
    ImGui::Unindent();
  }

  ImGui::Spacing();
  DrawGameplaySwitch(block, first_row + BlockRowStart(block, block.keyboard_mouse_switch), ring,
                     action, session, /*controller=*/false, block.switches.keyboard_mouse);
  if (block.has_keyboard_mouse_table()) {
    const bool keyboard_on =
        SettingBool(session.profile(), kKeyboardSupportKey, kKeyboardSupportDefault);
    const bool mouse_on = SettingBool(session.profile(), kMouseSupportKey, kMouseSupportDefault);
    ImGui::Indent();
    DrawGameplayTable(TableId::kKeyboardMouse,
                      first_row + BlockRowStart(block, block.keyboard_mouse_table),
                      first_row + BlockRowStart(block, block.keyboard_mouse_reset_all), ring,
                      session, action, keyboard_on, mouse_on);
    ImGui::Unindent();
  }
}

void ControllerTab::DrawGameplaySwitch(const ControllerBlock& block, std::size_t row,
                                       FocusModel& ring, NavAction action, ProfileSession& session,
                                       bool controller, bool on) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(controller ? "Controller" : "Keyboard + Mouse");
  ImGui::SameLine();
  if (!DrawBlockButton(row, ring, action, on ? "On" : "Off", true)) {
    return;
  }

  const bool next = !on;
  session.SetSetting(controller ? kGameplayControllerKey : kGameplayKeyboardMouseKey,
                     next ? "true" : "false",
                     (controller ? kGameplayControllerDefault : kGameplayKeyboardMouseDefault)
                         ? "true"
                         : "false",
                     ValueStyle::kBare);

  BlockSwitches switches = block.switches;
  if (controller) {
    switches.controller = next;
  } else {
    switches.keyboard_mouse = next;
  }
  const ControllerBlock after = BuildControllerBlock(switches);
  pending_focus_ = controller ? after.controller_switch : after.keyboard_mouse_switch;
  layout_changed_ = true;

  // A listen that belonged to a table the user just hid has nothing to assign any more.
  const TableId listening_table =
      controller ? TableId::kController : TableId::kKeyboardMouse;
  if (!next && listening_ && listening_->table == listening_table) {
    listen_hint_.clear();
    listening_.reset();
    status_ = "Listen cancelled: the table is hidden";
    return;
  }
  if (controller) {
    status_ = next ? "Controller gameplay mapping is on: its table plays, the saved preset does "
                     "not, and the change applies the next time the game starts"
                   : "Controller gameplay mapping is off: the saved controller preset plays again";
  } else {
    status_ = next
                  ? "Keyboard + Mouse gameplay mapping is on: that table plays beside the saved "
                    "controller preset, and the change applies the next time the game starts"
                  : "Keyboard + Mouse gameplay mapping is off: keys and mouse go through General "
                    "Mapping, and the change applies the next time the game starts";
  }
}

void ControllerTab::DrawGameplayTable(TableId table_id, std::size_t first_row,
                                      std::size_t reset_all_row, FocusModel& ring,
                                      ProfileSession& session, NavAction action, bool keyboard_on,
                                      bool mouse_on) {
  const gameplay_remap::TableKind kind =
      table_id == TableId::kController ? gameplay_remap::TableKind::kController
                                       : gameplay_remap::TableKind::kKeyboardMouse;
  std::vector<ProfileSetting>& rows =
      table_id == TableId::kController ? session.profile().gameplay_remap_controller
                                       : session.profile().gameplay_remap_keyboard_mouse;
  gameplay_remap::Table table = gameplay_remap::Table::FromRows(kind, rows);

  // A row this build keeps but does not apply - a source of the other input type, or a value it
  // cannot read - is named rather than left looking like a binding that silently does nothing.
  for (const gameplay_remap::Table::RejectedRow& rejected : table.Rejected()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextWrapped("%s in this table is kept but not applied: %s", rejected.row.key.c_str(),
                       rejected.reason.c_str());
    ImGui::PopStyleColor();
  }

  std::vector<BindingRowView> views;
  views.reserve(gameplay_remap::kActionCount);
  for (std::size_t index = 0; index < gameplay_remap::kActionCount; ++index) {
    const gameplay_remap::Action action_id = gameplay_remap::Actions()[index];
    BindingRowView view;
    view.id = index;
    view.label = gameplay_remap::ActionLabel(action_id);
    bool any_disabled = false;
    if (const std::vector<remap::Source>* sources = table.Effective(action_id)) {
      view.nothing_bound = sources->empty();
      for (const remap::Source& source : *sources) {
        const bool enabled = SourceEnabled(source, keyboard_on, mouse_on);
        any_disabled = any_disabled || !enabled;
        view.spans.push_back(binding_text::Span{remap::FormatSource(source), enabled});
      }
    }
    if (any_disabled) {
      view.disabled_hint = DisabledSourceHint(keyboard_on, mouse_on);
    }
    view.listening = Listening();
    view.listening_here =
        listening_ && listening_->table == table_id && listening_->action == action_id;
    if (view.listening_here) {
      view.listening_text = ListeningText(listen_until_, ImGui::GetTime());
    }
    view.assign_enabled = !Listening() || view.listening_here;
    view.reset_enabled = table.IsBound(action_id);
    views.push_back(std::move(view));
  }

  const BindingTableResult result = DrawBindingTable(
      table_id == TableId::kController ? "##controller_gameplay" : "##keyboard_mouse_gameplay",
      views, first_row, ring, action);
  if (listening_ && listening_->table == table_id && !listen_hint_.empty()) {
    // The same sentence the top of the block carries, repeated where the user is looking: a
    // listen refused for the wrong input type must not look like a stalled capture.
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextWrapped("%s", listen_hint_.c_str());
    ImGui::PopStyleColor();
  }
  if (result.assign.has_value() && *result.assign < gameplay_remap::kActionCount) {
    const gameplay_remap::Action action_id = gameplay_remap::Actions()[*result.assign];
    const bool listening_here =
        listening_ && listening_->table == table_id && listening_->action == action_id;
    if (listening_here) {
      listen_hint_.clear();
      listening_.reset();
      status_ = "Listen cancelled";
    } else {
      ListeningRow row;
      row.table = table_id;
      row.action = action_id;
      BeginListening(row);
    }
  }
  if (result.reset.has_value() && *result.reset < gameplay_remap::kActionCount) {
    gameplay_remap::Table reset = table;
    reset.Reset(gameplay_remap::Actions()[*result.reset]);
    rows = reset.ToRows();
    status_ = std::string(gameplay_remap::ActionLabel(gameplay_remap::Actions()[*result.reset])) +
              " is back to the default in this table";
  }

  ImGui::Spacing();
  if (DrawBlockButton(reset_all_row, ring, action, "Reset all bindings", table.BoundCount() > 0)) {
    gameplay_remap::Table reset = table;
    reset.ResetAll();
    rows = reset.ToRows();
    status_ = table_id == TableId::kController
                  ? "Every Controller gameplay binding is back to the pad preset's controls"
                  : "Every Keyboard + Mouse gameplay binding is back to the title's keyboard "
                    "preset";
  }
}

void ControllerTab::DrawHelpRow(std::size_t row, FocusModel& ring, NavAction action, HelpPage page,
                                std::string_view preview) {
  if (DrawBlockButton(row, ring, action, "?", true)) {
    OpenHelp(page);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%.*s", static_cast<int>(preview.size()), preview.data());
  }
}

void ControllerTab::OpenHelp(HelpPage page) {
  // The page takes over the block: a listen that was counting down would otherwise assign the
  // input the user presses to close the page.
  if (listening_) {
    listening_.reset();
    listen_hint_.clear();
    status_ = "Listen cancelled";
  }
  help_page_ = page;
  help_request_ = true;
}

void ControllerTab::DrawHelpModal(NavAction action) {
  if (help_page_ == HelpPage::kNone) {
    return;
  }
  const bool general = help_page_ == HelpPage::kGeneral;
  const char* id = general ? kGeneralHelpPopup : kGameplayHelpPopup;
  const bool requested = help_request_;
  if (requested) {
    help_request_ = false;
    ImGui::OpenPopup(id);
  }
  ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f), ImVec2(720.0f, FLT_MAX));
  if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", general ? GeneralMappingHelp().data() : GameplayMappingHelp().data());
    ImGui::Spacing();
    if (ImGui::Button("Close") || action == NavAction::kCancel) {
      ImGui::CloseCurrentPopup();
      help_page_ = HelpPage::kNone;
    }
    ImGui::EndPopup();
  } else if (!requested) {
    // Closed by something that is not our button (the window went away, a viewport changed).
    // Keeping "open" with no popup on screen would swallow Escape for the rest of the session.
    help_page_ = HelpPage::kNone;
  }
}

bool ControllerTab::TakeLayoutChange() {
  const bool changed = layout_changed_;
  layout_changed_ = false;
  return changed;
}

ControllerBlock ControllerTab::BlockFor(const Profile& profile) {
  return BuildControllerBlock(ReadBlockSwitches(profile));
}

std::string ControllerTab::HelpText(const Profile& profile, std::size_t entry) {
  const ControllerBlock block = BlockFor(profile);
  const std::size_t row = BlockRowForEntry(block, entry);
  return row == kNoBlockRow ? std::string() : BlockHelpText(block, row);
}

std::string ControllerTab::TraceName(const Profile& profile, std::size_t entry) {
  const ControllerBlock block = BlockFor(profile);
  const std::size_t row = BlockRowForEntry(block, entry);
  return row == kNoBlockRow ? std::string("block:unknown") : BlockTraceName(block, row);
}

std::string ControllerTab::ListeningLabel(const ListeningRow& row) const {
  if (row.table == TableId::kGeneral) {
    return std::string(remap::TargetLabel(row.target));
  }
  return std::string(gameplay_remap::ActionLabel(row.action));
}

void ControllerTab::BeginListening(ListeningRow row) {
  listening_ = row;
  listen_until_ = ImGui::GetTime() + kListenSeconds;
  status_.clear();
  listen_hint_.clear();
  // The mouse button that opened this is down right now, and so is whatever the user happened
  // to be holding. Recording that as "the previous frame" is what leaves all of them without an
  // edge to be captured on.
  SnapshotInput();
}

bool ControllerTab::CommitCapture(ProfileSession& session, const remap::Source& source) {
  if (!listening_) {
    return false;
  }
  const ListeningRow row = *listening_;
  if (row.table == TableId::kGeneral) {
    remap::Table table = remap::Table::FromRows(session.profile().remap);
    std::vector<remap::Source> sources;
    // The *effective* list, not the user's row: a control on its default shows that default, and
    // the first assignment adds to what the screen says rather than silently dropping it (§4).
    if (const std::vector<remap::Source>* existing = table.Effective(row.target)) {
      sources = *existing;
    }
    if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
      sources.push_back(source);
    }
    table.Set(row.target, std::move(sources));
    session.profile().remap = table.ToRows();
    status_ = std::string(remap::TargetLabel(row.target)) + " now answers to " +
              remap::FormatSource(source);
    listening_.reset();
    listen_hint_.clear();
    return true;
  }

  const gameplay_remap::TableKind kind = row.table == TableId::kController
                                             ? gameplay_remap::TableKind::kController
                                             : gameplay_remap::TableKind::kKeyboardMouse;
  // Each table accepts only its own input type; a source of the other kind is refused with the
  // reason rather than stored where the game would never read it (plan R11).
  if (!gameplay_remap::AcceptsSource(kind, source)) {
    listen_hint_ = "This table takes " +
                   std::string(kind == gameplay_remap::TableKind::kController
                                   ? "pad inputs"
                                   : "keys and mouse buttons") +
                   " only: " + remap::FormatSource(source) + " was not added. Still listening.";
    return false;
  }
  std::vector<ProfileSetting>& profile_rows =
      kind == gameplay_remap::TableKind::kController ? session.profile().gameplay_remap_controller
                                                    : session.profile().gameplay_remap_keyboard_mouse;
  gameplay_remap::Table table = gameplay_remap::Table::FromRows(kind, profile_rows);
  std::vector<remap::Source> sources;
  if (const std::vector<remap::Source>* existing = table.Effective(row.action)) {
    sources = *existing;
  }
  if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
    sources.push_back(source);
  }
  table.Set(row.action, std::move(sources));
  profile_rows = table.ToRows();
  status_ = std::string(gameplay_remap::ActionLabel(row.action)) + " now answers to " +
            remap::FormatSource(source) + " in this table";
  listening_.reset();
  listen_hint_.clear();
  return true;
}

std::optional<remap::Source> ControllerTab::CapturedInput() const {
  // Pads first: a listen is usually about the pad, so a tie should go to the device the user is
  // holding. The handles come from the registry, because a pad has to be *opened* before SDL will
  // read it at all (pad_source.h) - asking here without opening it would look exactly like a pad
  // nobody is touching.
  std::optional<remap::Source> captured;
  for (SDL_Gamepad* pad : pads_.pads()) {
    if (captured) {
      break;
    }
    const PadSnapshot* previous = nullptr;
    for (const PadSnapshot& snapshot : pads_snapshot_) {
      if (snapshot.id == SDL_GetGamepadID(pad)) {
        previous = &snapshot;
        break;
      }
    }
    if (previous == nullptr) {
      // A pad that appeared while the listen was running. Its first frame is the state its
      // edges will be measured against, so it cannot capture on the frame it arrives.
      continue;
    }
    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT && !captured; ++button) {
      const std::size_t index = static_cast<std::size_t>(button);
      if (!SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(button))) {
        continue;
      }
      if (index < previous->buttons.size() && previous->buttons[index]) {
        continue;  // held before the listen started
      }
      if (const std::optional<remap::Target> target =
              TargetForPadButton(static_cast<SDL_GamepadButton>(button))) {
        captured = remap::Source{remap::SourceKind::kPad, std::string(remap::TargetName(*target))};
      }
    }
    if (!captured &&
        TriggerAxisPressed(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) &&
        !previous->left_trigger) {
      captured = remap::Source{remap::SourceKind::kPad, "left_trigger"};
    }
    if (!captured &&
        TriggerAxisPressed(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) &&
        !previous->right_trigger) {
      captured = remap::Source{remap::SourceKind::kPad, "right_trigger"};
    }
  }
  if (captured) {
    return captured;
  }

  int key_count = 0;
  const bool* keys = SDL_GetKeyboardState(&key_count);
  if (keys != nullptr) {
    const std::size_t count = std::min(static_cast<std::size_t>(key_count), key_down_.size());
    for (std::size_t scancode = 0; scancode < count; ++scancode) {
      if (!keys[scancode] || key_down_[scancode]) {
        continue;
      }
      const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
      if (name != nullptr && *name != '\0') {
        return remap::Source{remap::SourceKind::kKey, name};
      }
    }
  }

  const uint32_t mouse = SDL_GetMouseState(nullptr, nullptr);
  for (const MouseCapture& capture : kMouseCaptures) {
    if ((mouse & capture.mask) != 0 && (mouse_down_ & capture.mask) == 0) {
      return remap::Source{remap::SourceKind::kMouse, capture.name};
    }
  }
  return std::nullopt;
}

void ControllerTab::SnapshotInput() {
  int key_count = 0;
  const bool* keys = SDL_GetKeyboardState(&key_count);
  const std::size_t count = keys == nullptr ? 0 : static_cast<std::size_t>(key_count);
  key_down_.assign(count, false);
  for (std::size_t scancode = 0; scancode < count; ++scancode) {
    key_down_[scancode] = keys[scancode];
  }

  // The registry's pads, and only those: a pad nobody opened cannot be read, so snapshotting one
  // would record a button that is always up and a capture that would never fire. The list is
  // rebuilt rather than merged, so a pad that has gone cannot leave a stale snapshot behind to be
  // the "previous frame" a reconnected one is measured against.
  std::vector<PadSnapshot> snapshots;
  snapshots.reserve(pads_.count());
  for (SDL_Gamepad* pad : pads_.pads()) {
    PadSnapshot snapshot;
    snapshot.id = SDL_GetGamepadID(pad);
    snapshot.buttons.assign(static_cast<std::size_t>(SDL_GAMEPAD_BUTTON_COUNT), false);
    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
      snapshot.buttons[static_cast<std::size_t>(button)] =
          SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(button));
    }
    snapshot.left_trigger =
        TriggerAxisPressed(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    snapshot.right_trigger =
        TriggerAxisPressed(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    snapshots.push_back(std::move(snapshot));
  }
  pads_snapshot_ = std::move(snapshots);

  mouse_down_ = SDL_GetMouseState(nullptr, nullptr);
}

}  // namespace rb_blitz::profile
