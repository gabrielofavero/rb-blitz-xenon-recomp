// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the button-mapping block (launcher/src/controller_tab.h).

#include "controller_tab.h"

#include "imgui.h"

#include <algorithm>
#include <string>
#include <utility>

#include "row_ui.h"

namespace rb_blitz::launcher {
namespace {

// Not constexpr: ImVec4 has no constexpr constructor.
const ImVec4 kWarning{0.95f, 0.75f, 0.25f, 1.0f};


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
    case SDL_GAMEPAD_BUTTON_GUIDE:
      return remap::Target::kGuide;
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
      // The extra buttons a modern pad has - paddles, the touchpad, misc - have no control of
      // their own to capture into: the SDK folds them onto buttons that already exist, and a
      // binding names a control rather than a physical key.
      return std::nullopt;
  }
}

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

// The value column's text: the sources, or nothing at all for a control the pad still reports
// by itself. Twenty-odd rows of "unchanged" would be twenty-odd rows of noise, and the column
// being empty is a clear enough way to say it - the "?" beside the heading is where the rule is
// written down.
std::string BindingText(const std::vector<remap::Source>* sources) {
  if (sources == nullptr) {
    return {};
  }
  if (sources->empty()) {
    return "Nothing can press this";
  }
  std::string text;
  for (const remap::Source& source : *sources) {
    if (!text.empty()) {
      text += ", ";
    }
    text += remap::FormatSource(source);
  }
  return text;
}

}  // namespace

void ControllerTab::Draw(std::size_t first_row, FocusModel& ring, ProfileSession& session,
                         NavAction action) {
  const double now = ImGui::GetTime();
  if (listening_ && now >= listen_until_) {
    status_ = std::string(remap::TargetLabel(*listening_)) +
              " was not changed: nothing was pressed";
    listening_.reset();
  }

  ImGui::SeparatorText("Button mapping");
  // The rule the rows must not repeat: one sentence and a "?" rather than a line of explanation
  // per control.
  ImGui::TextDisabled(
      "Assign listens for three seconds and adds the first input it sees: a pad button, a key, "
      "or a mouse button.");
  ImGui::SameLine();
  ImGui::SmallButton("?");
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Every control already answers to its own pad button and a keyboard key: the D-pad is\n"
        "WASD, the face buttons are the I/J/K/L diamond, the shoulders are Q/E, and the sticks,\n"
        "Back and Start have their own keys.\n"
        "\n"
        "Assign adds to what a control already has, so one button can answer to several inputs.\n"
        "Press Assign again to add another.\n"
        "\n"
        "Reset removes your own row, which puts the control back to that default.\n"
        "Reset all does that for every control at once.\n"
        "\n"
        "The triggers and Guide are left to the pad: a keyboard cannot stand in for a trigger's\n"
        "travel, and Guide belongs to the platform.\n"
        "\n"
        "Escape cancels a listen. Assignments are written by Save, on the bottom bar.");
  }
  ImGui::Spacing();

  ImGui::Spacing();
  // Above the list rather than below it: the list is taller than most windows, so a line under
  // it is a line nobody reads.
  if (!status_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextUnformatted(status_.c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();
  }

  const remap::Table table = remap::Table::FromRows(session.profile().remap);
  const std::vector<remap::Target>& targets = remap::Targets();
  // Three columns - the control, what presses it, and the row's own two buttons - with an equal share
  // of the width each, so the three sit at even intervals down the whole list and the row reads as
  // one small table. Left to themselves the columns end up at whatever distance their own text
  // happens to make: a one-letter control's binding starts a hair away from it, and a wide one's
  // further along, which is the "everything at a different distance" this replaces. Equal shares
  // also put the buttons about two thirds across, which is where the schema tabs put the widget
  // their own rows drive, and safely clear of the scrollbar this list always has.
  ImGui::BeginTable("##bindings", 3,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings);
  ImGui::TableSetupColumn("Control");
  ImGui::TableSetupColumn("Assigned to");
  ImGui::TableSetupColumn("##actions");
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const remap::Target target = targets[index];
    const bool listening_here = listening_ && *listening_ == target;
    // What presses the control: the user's row, or the binding the launcher ships. Reset only
    // applies to the user's own row - a control on its default has nothing to remove.
    const std::vector<remap::Source>* sources = table.Effective(target);
    const bool user_bound = table.IsBound(target);

    // A table disambiguates its columns but not its rows, so every row's "Assign" would be the
    // same ImGui id and the click would land on the first row that submitted one. The row's own
    // index is what makes each row its own scope.
    ImGui::PushID(static_cast<int>(index));
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(std::string(remap::TargetLabel(target)).c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    // Wrapped at the column's own edge rather than clipped at it: the binding is the one column
    // whose text can outgrow the room it has (a control bound to several inputs), and a binding on
    // two lines is readable where one that runs out of its cell is not.
    ImGui::PushTextWrapPos(0.0f);
    if (listening_here) {
      ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
      ImGui::Text("Listening... %.1fs", listen_until_ - now);
      ImGui::PopStyleColor();
    } else if (sources != nullptr && sources->empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
      ImGui::TextUnformatted(BindingText(sources).c_str());
      ImGui::PopStyleColor();
    } else if (sources != nullptr) {
      ImGui::TextUnformatted(BindingText(sources).c_str());
    }
    ImGui::PopTextWrapPos();

    ImGui::TableSetColumnIndex(2);
    // Only one listen at a time: while one is running, every other row's Assign is disabled
    // rather than quietly stealing the capture.
    if (Item(first_row + index * 2, ring, action, listening_here ? "Cancel" : "Assign",
             !listening_ || listening_here)) {
      if (listening_here) {
        listening_.reset();
        status_ = "Listen cancelled";
      } else {
        BeginListening(target);
        status_.clear();
      }
    }
    ImGui::SameLine();
    if (Item(first_row + index * 2 + 1, ring, action, "Reset", user_bound && !listening_)) {
      remap::Table reset = table;
      reset.Reset(target);
      session.profile().remap = reset.ToRows();
      status_ = std::string(remap::TargetLabel(target)) + " is back to the default";
    }
    ImGui::PopID();
  }
  ImGui::EndTable();

  ImGui::Spacing();
  if (Item(first_row + targets.size() * 2, ring, action, "Reset all bindings",
           table.BoundCount() > 0)) {
    remap::Table reset = table;
    reset.ResetAll();
    session.profile().remap = reset.ToRows();
    status_ = "Every control is back to the keys it ships with";
  }

  // Only while a listen is running: the edges it looks for are measured against the previous
  // frame, and a panel nobody is capturing with has no reason to poll SDL sixty times a second.
  // A listen that begins on this frame already recorded the state it began in (BeginListening),
  // so the click that started it cannot be captured by it.
  if (listening_) {
    if (const std::optional<remap::Source> captured = CapturedInput()) {
      CommitCapture(session, *captured);
    } else {
      SnapshotInput();
    }
  }
}

bool ControllerTab::Item(std::size_t index, FocusModel& ring, NavAction action,
                         const char* label, bool enabled) {
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
  // A5: the outline as well as the colour, so the ring is legible without relying on the shade.
  DrawFocusOutline(in_ring);
  if (!enabled) {
    ImGui::EndDisabled();
  }
  // A pointer is a device like any other (D6): hovering adopts the ring, so the bottom bar
  // describes the button the mouse is over rather than the last one the keyboard left behind.
  AdoptRingOnHover(index, ring);
  if (in_ring && !ImGui::IsItemVisible()) {
    ImGui::SetScrollHereY(0.5f);
  }
  return enabled && (clicked || focused);
}

std::string ControllerTab::HelpText(std::size_t row) {
  const std::vector<remap::Target>& targets = remap::Targets();
  if (row >= targets.size() * 2) {
    return "Removes every assignment at once, so every control goes back to the keys it ships with.";
  }
  const std::string label(remap::TargetLabel(targets[row / 2]));
  if (row % 2 == 0) {
    return label + ": adds the next input it sees - a pad button, a key or a mouse button - to "
                   "what already presses it.";
  }
  // The default a Reset restores is both devices': the control's own pad button and a key, which
  // is what the block's own rule above says.
  return label + ": removes your assignments, putting the control back to its own pad button and "
                 "its key.";
}

void ControllerTab::BeginListening(remap::Target target) {
  listening_ = target;
  listen_until_ = ImGui::GetTime() + kListenSeconds;
  // The mouse button that opened this is down right now, and so is whatever the user happened
  // to be holding. Recording that as "the previous frame" is what leaves all of them without an
  // edge to be captured on.
  SnapshotInput();
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

void ControllerTab::CommitCapture(ProfileSession& session, const remap::Source& source) {
  const remap::Target target = *listening_;
  listening_.reset();

  remap::Table table = remap::Table::FromRows(session.profile().remap);
  std::vector<remap::Source> sources;
  if (const std::vector<remap::Source>* existing = table.Bindings(target)) {
    sources = *existing;
  }
  if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
    sources.push_back(source);
  }
  table.Set(target, std::move(sources));
  session.profile().remap = table.ToRows();

  status_ = std::string(remap::TargetLabel(target)) + " now answers to " +
            remap::FormatSource(source);
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

}  // namespace rb_blitz::launcher
