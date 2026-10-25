// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"

#include <imgui.h>
#include <rex/ui/keyboard_input_controller.h>

namespace {
using Pad = rex::ui::KeyboardControllerSample;
using Controller = rex::ui::KeyboardInputController;
using Action = rex::ui::KeyboardControllerAction;

Action Tick(Controller& controller, uint16_t buttons = 0, bool focused = true,
            bool connected = true, float delta = 1.0f / 60.0f) {
  return controller.Update({connected, focused, buttons}, delta);
}

void NoAction(const Action& action) {
  CHECK_FALSE(action.focus_text || action.focus_changed || action.cancel);
}

void Move(Controller& controller, uint16_t direction) {
  CHECK_TRUE(Tick(controller, direction).focus_changed);
  NoAction(Tick(controller));
}

// CPU-only ImGui integration: real input fields/buttons, no native window or game.
class DialogFrame {
 public:
  DialogFrame() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    Frame();
    Frame();
    CHECK_TRUE(active);
  }
  ~DialogFrame() { ImGui::DestroyContext(); }

  void Frame(uint16_t buttons = 0) {
    const auto action = Tick(controller, buttons);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowSize(ImVec2(480, 240));
    ImGui::Begin("Headless keyboard controls");
    const auto focus = controller.focus();
    if (first_frame || action.focus_text ||
        (action.focus_changed && focus == Controller::Focus::Text)) {
      ImGui::SetKeyboardFocusHere();
    }
    first_frame = false;
    const bool entered = ImGui::InputText("Text", text, sizeof(text),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    active = ImGui::IsItemActive();
    submitted = entered || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (action.focus_changed && focus == Controller::Focus::Done)
      ImGui::SetKeyboardFocusHere();
    submitted |= ImGui::Button("Done", ImVec2(100, 32)) && mouse_released;
    done_pos = Center();
    if (action.focus_changed && focus == Controller::Focus::Cancel)
      ImGui::SetKeyboardFocusHere();
    cancelled = (ImGui::Button("Cancel", ImVec2(100, 32)) && mouse_released) ||
                action.cancel || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    cancel_pos = Center();
    ImGui::End();
    ImGui::EndFrame();
  }

  void Click(ImVec2 pos) {
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(pos.x, pos.y);
    Frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    Frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    Frame();
  }

  Controller controller;
  char text[128] = {};
  bool active = false;
  bool submitted = false;
  bool cancelled = false;
  ImVec2 done_pos{}, cancel_pos{};

 private:
  static ImVec2 Center() {
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    return ImVec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
  }
  bool first_frame = true;
};
}  // namespace

int main() {
  using rb_blitz::test::BeginCase;
  BeginCase("Opening press, focus loss and disconnect require a release");
  Controller opening;
  NoAction(Tick(opening, Pad::Cancel));
  NoAction(Tick(opening, Pad::Cancel));
  NoAction(Tick(opening));
  CHECK_TRUE(Tick(opening, Pad::Cancel).cancel);
  for (const bool lose_focus : {true, false}) {
    Controller controller;
    NoAction(Tick(controller));
    NoAction(Tick(controller, Pad::Cancel, !lose_focus, lose_focus));
    NoAction(Tick(controller, Pad::Cancel));
    NoAction(Tick(controller));
    CHECK_TRUE(Tick(controller, Pad::Cancel).cancel);
  }

  BeginCase("Controller navigation stays available; A only returns focus to text");
  Controller navigation;
  NoAction(Tick(navigation));
  Move(navigation, Pad::Down);
  CHECK_EQ(navigation.focus(), Controller::Focus::Done);
  Move(navigation, Pad::Right);
  CHECK_EQ(navigation.focus(), Controller::Focus::Cancel);
  Move(navigation, Pad::Left);
  CHECK_EQ(navigation.focus(), Controller::Focus::Done);
  CHECK_TRUE(Tick(navigation, Pad::Select).focus_text);
  CHECK_EQ(navigation.focus(), Controller::Focus::Text);
  NoAction(Tick(navigation, Pad::Select));
  NoAction(Tick(navigation));
  CHECK_TRUE(Tick(navigation, Pad::Cancel).cancel);

  BeginCase("Direction holds repeat after a delay");
  Controller repeat;
  NoAction(Tick(repeat));
  CHECK_TRUE(Tick(repeat, Pad::Down).focus_changed);
  for (int i = 0; i < 3; ++i) NoAction(Tick(repeat, Pad::Down, true, true, 0.1f));
  CHECK_EQ(repeat.focus(), Controller::Focus::Done);
  CHECK_TRUE(Tick(repeat, Pad::Down, true, true, 0.1f).focus_changed);
  CHECK_EQ(repeat.focus(), Controller::Focus::Cancel);

  BeginCase("Enter and keypad Enter submit while typing or highlighting either button");
  for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter}) {
    for (int target = 0; target < 3; ++target) {
      DialogFrame dialog;
      ImGui::GetIO().AddInputCharactersUTF8("test query");
      dialog.Frame();
      for (int i = 0; i < target; ++i) {
        dialog.Frame(Pad::Down);
        dialog.Frame();
      }
      ImGui::GetIO().AddKeyEvent(key, true);
      dialog.Frame();
      CHECK_TRUE(dialog.submitted);
      CHECK_FALSE(dialog.cancelled);
      CHECK_TRUE(std::string(dialog.text) == "test query");
    }
  }

  BeginCase("B cancels during active text entry without submitting");
  {
    DialogFrame dialog;
    ImGui::GetIO().AddInputCharactersUTF8("unfinished query");
    dialog.Frame();
    CHECK_TRUE(dialog.active);
    dialog.Frame(Pad::Cancel);
    CHECK_TRUE(dialog.cancelled);
    CHECK_FALSE(dialog.submitted);
  }

  BeginCase("A never submits on Done; navigation can leave and re-enter typing directly");
  {
    DialogFrame dialog;
    dialog.Frame(Pad::Down);
    dialog.Frame();
    CHECK_FALSE(dialog.active);
    CHECK_EQ(dialog.controller.focus(), Controller::Focus::Done);
    dialog.Frame(Pad::Select);
    CHECK_FALSE(dialog.submitted || dialog.cancelled);
    dialog.Frame();
    CHECK_TRUE(dialog.active);
    CHECK_FALSE(dialog.submitted || dialog.cancelled);
  }

  BeginCase("Mouse clicks still submit Done and cancel Cancel during typing");
  {
    DialogFrame dialog;
    dialog.Click(dialog.done_pos);
    CHECK_TRUE(dialog.submitted);
    CHECK_FALSE(dialog.cancelled);
  }
  {
    DialogFrame dialog;
    dialog.Click(dialog.cancel_pos);
    CHECK_TRUE(dialog.cancelled);
    CHECK_FALSE(dialog.submitted);
  }
  return rb_blitz::test::Finish();
}
