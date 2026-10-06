// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the launcher-keys block (launcher/src/nav_ui.h, A5).

#include "nav_ui.h"

#include "imgui.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "nav_keys.h"
#include "row_ui.h"

namespace rb_blitz::launcher {
namespace {

const ImVec4 kWarning{0.95f, 0.75f, 0.25f, 1.0f};

// How long a capture waits for a key before giving up. The same three seconds C5's button capture
// uses, for the same reason: a capture that could not be answered would be the launcher refusing
// to read the keyboard, which is the trap this block exists to open.
constexpr double kCaptureSeconds = 3.0;

// The action a row of the block belongs to, and whether it is the row's Assign or its Reset.
struct RowRef {
  NavAction action = NavAction::kNone;
  bool assign = false;
};

RowRef RowFor(std::size_t row) {
  const std::size_t index = row / 2;
  RowRef ref;
  ref.assign = row % 2 == 0;
  if (index < nav_bindings::BindableActions().size()) {
    ref.action = nav_bindings::BindableActions()[index];
  }
  return ref;
}

}  // namespace

std::string NavKeysPanel::HelpText(std::size_t row) {
  const std::size_t last = nav_bindings::kActionCount * 2;
  if (row == last) {
    return "Puts every launcher key back to the key it ships with. Nothing reaches the file until "
           "Save, so a set of keys that turned out to be a mistake can be abandoned by leaving "
           "without saving.";
  }
  const RowRef ref = RowFor(row);
  if (ref.action == NavAction::kNone) {
    return "(nothing to say about this one)";
  }
  const std::string label(nav_bindings::ActionLabel(ref.action));
  if (!ref.assign) {
    return label + " goes back to the key it ships with: " +
           nav_bindings::DefaultBindingText(ref.action) + ".";
  }
  return "Binds a key to " + label + ": press it now, within three seconds. Escape cancels. A key "
         "can answer more than one action - the order in this list decides which one wins - and "
         "Escape is the key to leave alone. " +
         std::string(nav_bindings::ActionHelp(ref.action));
}

std::string NavKeysPanel::RowName(std::size_t row) {
  const std::size_t last = nav_bindings::kActionCount * 2;
  if (row == last) {
    return "launcher-key:reset-all";
  }
  const RowRef ref = RowFor(row);
  if (ref.action == NavAction::kNone) {
    return {};
  }
  return "launcher-key:" + std::string(nav_bindings::ActionName(ref.action)) +
         (ref.assign ? ":assign" : ":reset");
}

void NavKeysPanel::BeginCapture(NavAction action) {
  capturing_ = action;
  capture_until_ = ImGui::GetTime() + kCaptureSeconds;
  // The frame the capture began on is remembered, and the read below skips it: ImGui reports the
  // key that *started* the capture as pressed this frame too, so a capture whose first act was to
  // bind the key that asked for it would bind Enter. C5's listen records the input state it began
  // in for the same reason; this is the same idea where the state is a frame.
  capture_frame_ = ImGui::GetFrameCount();
  status_.clear();
}

bool NavKeysPanel::Item(std::size_t index, FocusModel& ring, NavAction action, const char* label,
                        bool enabled) {
  const bool focused = action == NavAction::kActivate && !ring.Empty() && ring.Index() == index;
  const bool in_ring = !ring.Empty() && ring.Index() == index;
  if (!enabled) {
    ImGui::BeginDisabled();
  }
  if (in_ring) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  }
  const bool clicked = ImGui::Button(label);
  if (in_ring) {
    ImGui::PopStyleColor();
  }
  // A5: the outline, so the ring is a shape and not only a colour (focus_ring.h).
  DrawFocusOutline(in_ring);
  if (!enabled) {
    ImGui::EndDisabled();
  }
  // A pointer is a device like any other (D6): hovering adopts the ring, the same way hovering a
  // row label does.
  AdoptRingOnHover(index, ring);
  if (in_ring && !ImGui::IsItemVisible()) {
    ImGui::SetScrollHereY(0.5f);
  }
  return clicked || (focused && enabled);
}

void NavKeysPanel::Draw(std::size_t first_row, ProfileSession& session, FocusModel& ring,
                        NavAction action, bool safe_mode) {
  const double now = ImGui::GetTime();
  if (capturing_ && now >= capture_until_) {
    status_ = std::string(nav_bindings::ActionLabel(*capturing_)) +
              " was not changed: nothing was pressed";
    capturing_.reset();
  }
  // Escape belongs to the capture while it is running: it is the one key the block reads as a
  // command, and the shell leaves the action to it rather than treating it as "leave the launcher".
  if (capturing_ && action == NavAction::kCancel) {
    status_ = "Capture cancelled";
    capturing_.reset();
  }

  ImGui::SeparatorText("Launcher keys");
  ImGui::TextDisabled("The keys this window answers to. The game's own controls are on the "
                      "Controller tab.");
  ImGui::SameLine();
  ImGui::SmallButton("?");
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Assign binds a key to one action; Reset puts one action back to the keys it ships\n"
        "with, and Reset every key does that for all of them at once.\n"
        "\n"
        "A key on its own and the same key with Ctrl or Shift are different bindings, so Ctrl\n"
        "and Shift combinations can be used alongside the plain keys.\n"
        "\n"
        "Nothing here reaches the file until Save, and --safe-mode starts the launcher on these\n"
        "defaults whatever the file says, which is how a launcher whose keys have stopped\n"
        "working is opened again (launcher/README.md).");
  }

  if (safe_mode) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextWrapped(
        "Safe mode is on, so these defaults are the keys in force whatever the file says. A key "
        "assigned here is written by Save and takes effect the next time the launcher starts.");
    ImGui::PopStyleColor();
  }
  if (!status_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextUnformatted(status_.c_str());
    ImGui::PopStyleColor();
  }
  if (!session.CanSave()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextUnformatted(
        "This profile cannot be written, so a key assigned here would be lost. The reason is on "
        "the bottom bar.");
    ImGui::PopStyleColor();
  }

  nav_bindings::Table table = nav_bindings::Table::FromRows(session.profile().nav);
  const bool usable = session.CanSave();

  // The value column is sized rather than stretched, so the two buttons stay beside the keys they
  // belong to instead of marching off to the window's far edge (the same rule C5's table follows).
  const float available = ImGui::GetContentRegionAvail().x;
  const float label_width =
      ImGui::CalcTextSize("Copy the game's value").x + ImGui::GetStyle().CellPadding.x * 4.0f;
  const float actions_width = ImGui::CalcTextSize("Assign").x + ImGui::CalcTextSize("Cancel").x +
                              ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().ItemSpacing.x * 2.0f +
                              ImGui::GetStyle().CellPadding.x * 6.0f;
  ImGui::BeginTable("##launcher-keys", 3,
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings);
  ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, label_width);
  ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed,
                          std::clamp(available * 0.45f, 260.0f, 900.0f));
  ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, actions_width);

  const std::vector<NavAction>& actions = nav_bindings::BindableActions();
  for (std::size_t index = 0; index < actions.size(); ++index) {
    const NavAction current = actions[index];
    const bool capturing_here = capturing_ && *capturing_ == current;
    const std::string binding = table.BindingText(current);
    // A table disambiguates its columns but not its rows, so each row needs its own scope.
    ImGui::PushID(static_cast<int>(index));
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(std::string(nav_bindings::ActionLabel(current)).c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    if (capturing_here) {
      ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
      ImGui::Text("Press a key... %.1fs", capture_until_ - now);
      ImGui::PopStyleColor();
    } else if (binding.empty()) {
      // Nothing bound is a real setting, and the one worth showing in warning colour: this action
      // has no key at all, so nothing the user can press does it.
      ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
      ImGui::TextUnformatted("(no key)");
      ImGui::PopStyleColor();
    } else {
      ImGui::TextUnformatted(binding.c_str());
    }

    ImGui::TableSetColumnIndex(2);
    // One capture at a time: while one is running every other row's Assign is disabled rather than
    // quietly stealing the key that was about to be captured.
    if (Item(first_row + index * 2, ring, action, capturing_here ? "Cancel" : "Assign",
             usable && (!capturing_ || capturing_here))) {
      if (capturing_here) {
        status_ = "Capture cancelled";
        capturing_.reset();
      } else {
        BeginCapture(current);
      }
    }
    ImGui::SameLine();
    if (Item(first_row + index * 2 + 1, ring, action, "Reset",
             usable && !table.IsDefault(current) && !capturing_)) {
      table.Reset(current);
      session.profile().nav = table.ToRows();
      status_ = std::string(nav_bindings::ActionLabel(current)) + " is back to " +
                nav_bindings::DefaultBindingText(current);
    }
    ImGui::PopID();
  }
  ImGui::EndTable();

  ImGui::Spacing();
  if (Item(first_row + nav_bindings::kActionCount * 2, ring, action,
           "Reset every launcher key", usable && table.ChangedCount() > 0 && !capturing_)) {
    table.ResetAll();
    session.profile().nav = table.ToRows();
    status_ = "Every launcher key is back to the key it ships with (not saved yet)";
  }

  // The capture reads the keyboard itself, once per frame while it is running: the key being
  // pressed is the input being captured, so the ring must not act on it. A capture that begins on
  // this frame cannot capture the key that started it (`capture_frame_`), and the frames after it
  // see every key the way ImGui does.
  if (capturing_ && ImGui::GetFrameCount() != capture_frame_) {
    if (const std::optional<nav_bindings::Trigger> captured = nav_keys::CapturedTrigger()) {
      const NavAction bound = *capturing_;
      capturing_.reset();
      // Add rather than replace: an action with two keys keeps them, which is how Tab and Down both
      // move the ring out of the box. Reset is how one goes away.
      std::vector<nav_bindings::Trigger> triggers = table.Triggers(bound);
      const auto existing = std::find(triggers.begin(), triggers.end(), *captured);
      if (existing == triggers.end()) {
        triggers.push_back(*captured);
      }
      table.Set(bound, std::move(triggers));
      session.profile().nav = table.ToRows();
      status_ = std::string(nav_bindings::ActionLabel(bound)) + " now answers to " +
                nav_bindings::TriggerLabel(*captured) + " (not saved yet)";
      // A key that already answers another action is worth saying out loud: the order of this list
      // decides which of the two wins, and the user cannot see that from the row they just edited.
      for (const NavAction other : actions) {
        if (other == bound) {
          continue;
        }
        const std::vector<nav_bindings::Trigger>& theirs = table.Triggers(other);
        if (std::find(theirs.begin(), theirs.end(), *captured) != theirs.end()) {
          status_ += std::string(", which ") + std::string(nav_bindings::ActionLabel(other)) +
                     " also answers to: whichever is higher in this list wins";
          break;
        }
      }
    }
  }
}

}  // namespace rb_blitz::launcher
