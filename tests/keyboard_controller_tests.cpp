// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"

#include <imgui.h>
#include <rex/ui/keyboard_input_controller.h>

namespace {
using Pad = rex::ui::KeyboardControllerSample;
using Controller = rex::ui::KeyboardInputController;

// Real ImGui input fields/buttons in a CPU-only context, without a native window.
class DialogFrame {
 public:
  DialogFrame() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
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
    const auto action = controller.Update({true, true, buttons});
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowSize(ImVec2(480, 240));
    ImGui::Begin("Headless keyboard controls");
    if (first_frame || action.show_keyboard) ImGui::SetKeyboardFocusHere();
    first_frame = false;
    const bool entered = ImGui::InputText("Text", text, sizeof(text),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    active = ImGui::IsItemActive();
    if (pending_keyboard && !action.cancel) ++keyboard_requests;
    pending_keyboard = action.show_keyboard;
    submitted = entered || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    submitted |= ImGui::Button("Done", ImVec2(100, 32)) && mouse_released;
    done_pos = Center();
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

  char text[128] = {};
  bool active = false;
  bool submitted = false;
  bool cancelled = false;
  int keyboard_requests = 0;
  ImVec2 done_pos{}, cancel_pos{};

 private:
  static ImVec2 Center() {
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    return ImVec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
  }
  bool first_frame = true;
  bool pending_keyboard = false;
  Controller controller;
};
}  // namespace

int main() {
  using rb_blitz::test::BeginCase;
  BeginCase("Held B on open/focus regain must release before it cancels");
  Controller opening;
  CHECK_FALSE(opening.Update({true, true, Pad::Cancel}).cancel);
  CHECK_FALSE(opening.Update({true, true, Pad::Cancel}).cancel);
  CHECK_FALSE(opening.Update({true, true, 0}).cancel);
  CHECK_TRUE(opening.Update({true, true, Pad::Cancel}).cancel);
  for (const bool lose_focus : {true, false}) {
    Controller controller;
    CHECK_FALSE(controller.Update({true, true, 0}).cancel);
    CHECK_FALSE(controller.Update({lose_focus, !lose_focus, Pad::Cancel}).cancel);
    CHECK_FALSE(controller.Update({true, true, Pad::Cancel}).cancel);
    CHECK_FALSE(controller.Update({true, true, 0}).cancel);
    CHECK_TRUE(controller.Update({true, true, Pad::Cancel}).cancel);
  }

  BeginCase("Other held controls cannot prevent a fresh B from cancelling");
  Controller controller;
  constexpr uint16_t other_controls = 0xFFF0;
  CHECK_FALSE(controller.Update({true, true, other_controls}).cancel);
  CHECK_TRUE(controller.Update({true, true, other_controls | Pad::Cancel}).cancel);

  BeginCase("Enter and keypad Enter submit while typing");
  for (const auto key : {ImGuiKey_Enter, ImGuiKey_KeypadEnter}) {
    DialogFrame dialog;
    ImGui::GetIO().AddInputCharactersUTF8("test query");
    dialog.Frame();
    ImGui::GetIO().AddKeyEvent(key, true);
    dialog.Frame();
    CHECK_TRUE(dialog.submitted);
    CHECK_FALSE(dialog.cancelled);
    CHECK_TRUE(std::string(dialog.text) == "test query");
  }

  BeginCase("B cancels active typing even while unrelated controls are held");
  {
    DialogFrame dialog;
    ImGui::GetIO().AddInputCharactersUTF8("unfinished query");
    dialog.Frame(other_controls);
    CHECK_TRUE(dialog.active);
    CHECK_FALSE(dialog.submitted || dialog.cancelled);
    dialog.Frame(other_controls | Pad::Cancel);
    CHECK_TRUE(dialog.cancelled);
    CHECK_FALSE(dialog.submitted);
  }

  BeginCase("A requests a keyboard after text focus, never submits, and B still cancels");
  {
    DialogFrame dialog;
    dialog.Frame(Pad::ShowKeyboard);
    CHECK_FALSE(dialog.submitted || dialog.cancelled);
    CHECK_EQ(dialog.keyboard_requests, 0);
    dialog.Frame(Pad::ShowKeyboard);
    CHECK_EQ(dialog.keyboard_requests, 1);
    dialog.Frame(Pad::ShowKeyboard);
    CHECK_EQ(dialog.keyboard_requests, 1);
    CHECK_TRUE(dialog.active);
    dialog.Frame(Pad::ShowKeyboard | Pad::Cancel);
    CHECK_TRUE(dialog.cancelled);
    CHECK_FALSE(dialog.submitted);
  }

  BeginCase("Simultaneous A/B cancels without requesting a keyboard");
  {
    Controller controller;
    controller.Update({true, true, 0});
    const auto action = controller.Update({true, true, Pad::ShowKeyboard | Pad::Cancel});
    CHECK_TRUE(action.cancel);
    CHECK_FALSE(action.show_keyboard);
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
