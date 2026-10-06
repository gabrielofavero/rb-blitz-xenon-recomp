// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the editable settings row (launcher/src/settings_edit.h, B2).

#include "settings_edit.h"

#include "imgui.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "row_ui.h"
#include "schema_view.h"

namespace rb_blitz::launcher {
namespace {

int ParseIntOrZero(std::string_view text) {
  int value = 0;
  const char* first = text.data();
  const char* last = text.data() + text.size();
  if (std::from_chars(first, last, value).ec != std::errc{}) {
    return 0;
  }
  return value;
}

float ParseFloatOrZero(std::string_view text) {
  const std::string owned(text);
  char* end = nullptr;
  const float value = std::strtof(owned.c_str(), &end);
  return end == owned.c_str() ? 0.0f : value;
}

// ImGui keeps an InputInt/InputFloat's text state itself, but InputText edits the caller's
// buffer, so a text row needs somewhere to keep it between frames - and needs to notice when
// the profile's value changed underneath (a reset, an import).
struct TextState {
  std::string value;  // the profile value the buffer was last in step with
  std::string text;
};

std::map<std::string, TextState>& TextBuffers() {
  static std::map<std::string, TextState> buffers;
  return buffers;
}

int ResizeCallback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto* buffer = static_cast<std::string*>(data->UserData);
    buffer->resize(static_cast<std::size_t>(data->BufTextLen));
    data->Buf = buffer->data();
  }
  return 0;
}

// The visible focus state every row shares: the frame takes the same colour the selected tab
// does, and A5 draws the outline around it as well (row_ui's DrawFocusOutline) so the ring is a
// shape and not only a shade. Both call sites go through here, which is why the outline is added
// once rather than at every widget.
bool BeginFocus(std::size_t index, FocusModel& ring) {
  const bool focused = !ring.Empty() && ring.Index() == index;
  if (focused) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  }
  return focused;
}

void EndFocus(std::size_t index, FocusModel& ring, bool focused) {
  DrawFocusOutline(focused);
  if (focused) {
    ImGui::PopStyleColor(3);
  }
  AdoptRingOnHover(index, ring);
  // In the ring and out of sight is not usable, so the row is brought back into view exactly
  // as a read-only row was (A1/A5).
  if (focused && !ImGui::IsItemVisible()) {
    ImGui::SetScrollHereY(0.5f);
  }
}

void Write(ProfileSession& session, const settings::Setting& setting, std::string value) {
  session.SetSetting(setting.key, std::move(value), setting.default_text,
                     StyleForKind(setting.kind));
}

bool DrawBool(const settings::Setting& setting, std::size_t index, FocusModel& ring,
              NavAction action, ProfileSession& session, std::string_view current) {
  bool value = current == "true";
  bool changed = false;
  // Enter or Space on the focused row toggles it, the same as clicking the box: one press,
  // one change.
  if (action == NavAction::kActivate && !ring.Empty() && ring.Index() == index) {
    value = !value;
    changed = true;
  }
  const bool focused = BeginFocus(index, ring);
  if (ImGui::Checkbox("##value", &value)) {
    changed = true;
  }
  EndFocus(index, ring, focused);
  if (changed) {
    Write(session, setting, value ? "true" : "false");
  }
  return changed;
}

bool DrawInt(const settings::Setting& setting, float value_width, std::size_t index,
             FocusModel& ring, NavAction action, ProfileSession& session,
             std::string_view current) {
  const int low = setting.has_range ? ParseIntOrZero(setting.min_text) : 0;
  const int high = setting.has_range ? ParseIntOrZero(setting.max_text) : 0;
  int value = ParseIntOrZero(current);
  bool changed = false;
  // With the focus on the row, Enter or Space steps it; a slider is not a keyboard-navigable
  // widget while ImGui's own navigation is off (A1).
  if (action == NavAction::kActivate && !ring.Empty() && ring.Index() == index) {
    value = setting.has_range ? std::min(value + 1, high) : value + 1;
    changed = true;
  }
  const bool focused = BeginFocus(index, ring);
  ImGui::SetNextItemWidth(value_width);
  if (setting.has_range) {
    if (ImGui::SliderInt("##value", &value, low, high)) {
      changed = true;
    }
  } else if (ImGui::InputInt("##value", &value)) {
    changed = true;
  }
  EndFocus(index, ring, focused);
  if (changed) {
    Write(session, setting, std::to_string(value));
  }
  return changed;
}

bool DrawFloat(const settings::Setting& setting, float value_width, std::size_t index,
               FocusModel& ring, NavAction action, ProfileSession& session,
               std::string_view current) {
  const float low = setting.has_range ? ParseFloatOrZero(setting.min_text) : 0.0f;
  const float high = setting.has_range ? ParseFloatOrZero(setting.max_text) : 0.0f;
  float value = ParseFloatOrZero(current);
  bool changed = false;
  if (action == NavAction::kActivate && !ring.Empty() && ring.Index() == index) {
    value = setting.has_range ? std::min(value + 1.0f, high) : value + 1.0f;
    changed = true;
  }
  const bool focused = BeginFocus(index, ring);
  ImGui::SetNextItemWidth(value_width);
  if (setting.has_range) {
    if (ImGui::SliderFloat("##value", &value, low, high)) {
      changed = true;
    }
  } else if (ImGui::InputFloat("##value", &value)) {
    changed = true;
  }
  EndFocus(index, ring, focused);
  if (changed) {
    Write(session, setting, std::to_string(value));
  }
  return changed;
}

// An enum is a row of radios, one ring entry each, so the pad and the keyboard can reach every
// choice without ImGui's own navigation - which is deliberately off (A1).
bool DrawEnum(const settings::Setting& setting, std::size_t index, FocusModel& ring,
              NavAction action, ProfileSession& session, std::string_view current) {
  const std::vector<std::string_view> choices = SettingChoices(setting);
  bool changed = false;
  for (std::size_t i = 0; i < choices.size(); ++i) {
    const std::string label(choices[i]);
    if (i != 0) {
      ImGui::SameLine();
    }
    if (DrawFocusableRadio(label.c_str(), index + i, ring, action, choices[i] == current)) {
      Write(session, setting, std::string(choices[i]));
      changed = true;
    }
  }
  return changed;
}

bool DrawText(const settings::Setting& setting, float value_width, std::size_t index,
              FocusModel& ring, NavAction action, ProfileSession& session,
              std::string_view current) {
  TextState& state = TextBuffers()[std::string(setting.key)];
  if (state.value != current) {
    state.value = std::string(current);
    state.text = std::string(current);
  }
  // Enter on the focused row puts the caret in the field: a read-only-looking row is not what
  // the ring landed on, and ImGui's own navigation is off (A1), so nothing else would.
  if (action == NavAction::kActivate && !ring.Empty() && ring.Index() == index) {
    ImGui::SetKeyboardFocusHere();
  }
  const bool focused = BeginFocus(index, ring);
  ImGui::SetNextItemWidth(value_width);
  const bool entered =
      ImGui::InputText("##value", state.text.data(), state.text.size() + 1,
                       ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
                       ResizeCallback, &state.text);
  EndFocus(index, ring, focused);
  if (entered || ImGui::IsItemDeactivatedAfterEdit()) {
    state.value = state.text;
    Write(session, setting, state.text);
    return true;
  }
  return false;
}

}  // namespace

bool DrawEditableSetting(const settings::Setting& setting, float value_width, std::size_t index,
                         FocusModel& ring, NavAction action, ProfileSession& session) {
  const std::string_view current = LauncherValueText(session, setting);
  switch (setting.kind) {
    case settings::Kind::kBool:
      return DrawBool(setting, index, ring, action, session, current);
    case settings::Kind::kInt:
      return DrawInt(setting, value_width, index, ring, action, session, current);
    case settings::Kind::kFloat:
      return DrawFloat(setting, value_width, index, ring, action, session, current);
    case settings::Kind::kEnum:
      return DrawEnum(setting, index, ring, action, session, current);
    case settings::Kind::kString:
      return DrawText(setting, value_width, index, ring, action, session, current);
    case settings::Kind::kPathDir:
    case settings::Kind::kPathFile:
      // A path row is the General tab's own editor (B1); elsewhere there is nothing honest to
      // change, so it stays read-only rather than silently ignoring a click (B2).
      ImGui::BeginDisabled();
      DrawReadOnlyValue(setting, value_width, current);
      ImGui::EndDisabled();
      return false;
  }
  return false;
}

}  // namespace rb_blitz::launcher
