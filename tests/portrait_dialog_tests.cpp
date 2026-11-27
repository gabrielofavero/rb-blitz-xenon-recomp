// SPDX-License-Identifier: GPL-2.0-only
// Render and click the actual overlay without launching or navigating the game.
#include "check.h"
#include "ui/portrait_dialog.h"
#include "ui/portrait_viewport.h"
#include "input/title_input.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <rex/ui/style.h>
#include <rex/ui/ui_system.h>

namespace { bool portrait = true; }
namespace rb_blitz::settings { bool InterfacePortraitEnabled() { return portrait; } }
namespace rb_blitz::input {
bool HostInputFocused() { return true; }
bool SamplePhysicalPad(rex::hid::IInputSystem::PadReading*, bool, ExtraButtons*) { return false; }
}

int main() {
  using namespace rb_blitz::test;
  namespace modal = rb_blitz::ui::portrait_dialog;
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.Fonts->AddFontDefault();
  unsigned char* pixels = nullptr;
  int atlas_width = 0, atlas_height = 0;
  io.Fonts->GetTexDataAsAlpha8(&pixels, &atlas_width, &atlas_height);
  CHECK_TRUE(pixels && atlas_width > 0 && atlas_height > 0);
  rex::ui::ToastStyle theme;
  theme.font = io.Fonts->Fonts.front();
  auto overlay = modal::CreateOverlay(theme);
  const auto frame = [&] {
    io.DeltaTime = 1.f / 60;
    ImGui::NewFrame();
    CHECK_TRUE(overlay->Draw(io));
    ImGui::Render();
  };
  bool answer = false;
  for (const auto size : {ImVec2(320, 693), ImVec2(402, 874), ImVec2(1206, 2622), ImVec2(768, 1024)}) {
    BeginCase("actual portrait modal keeps its body and clickable Continue inside the safe area");
    io.DisplaySize = size;
    const rb_blitz::ui::PortraitViewport viewport{402, 874, 0, 62, 402, 778};
    rb_blitz::ui::PublishPortraitViewport(viewport);
    modal::Observe(1, 2, 100, true);
    modal::PublishLabel(100, true, "Graphics were lowered to help the game start. Saved choices are unchanged. Review Help & Options.");
    modal::PublishLabel(100, false, "<alt1>A</alt1> Continue");
    CHECK_TRUE(modal::AnswerQuery("rbbz-portrait-dialog/open", &answer));
    frame(); frame();
    CHECK_TRUE(overlay->WantsExclusiveInput());
    auto* window = ImGui::FindWindowByName("##portrait_error");
    CHECK_TRUE(window && window->Active);
    CHECK_TRUE(window->Pos.x >= 0 && window->Pos.x + window->Size.x <= size.x);
    CHECK_TRUE(window->Pos.y >= size.y * 62 / 874);
    CHECK_TRUE(window->Pos.y + window->Size.y <= size.y * 840 / 874);
    CHECK_TRUE(ImGui::GetDrawData()->TotalVtxCount > 0);
    // The final button occupies the bottom strip of the real modal window.
    const float scale = window->WindowPadding.x / 22;
    const float button_height = std::max(44.f, 48 * scale);
    const ImVec2 button(window->Pos.x + window->Size.x / 2,
        window->Pos.y + window->Size.y - window->WindowPadding.y - button_height / 2);
    io.AddMousePosEvent(button.x, button.y);
    frame();
    io.AddMouseButtonEvent(0, true); frame();
    io.AddMouseButtonEvent(0, false); frame();
    CHECK_TRUE(modal::AnswerQuery("rbbz-portrait-dialog/confirm", &answer));
    CHECK_TRUE(answer);
    modal::AnswerQuery("rbbz-portrait-dialog/confirm", &answer);
    CHECK_FALSE(answer);
    modal::AnswerQuery("rbbz-portrait-dialog/close", &answer);
    frame();
    CHECK_FALSE(overlay->WantsExclusiveInput());
  }
  BeginCase("long localized messages scroll above the actual Continue button");
  std::string long_body;
  for (int i = 0; i < 100; ++i) long_body += "A longer localized explanation that must remain readable. ";
  modal::PublishLabel(100, true, long_body);
  modal::AnswerQuery("rbbz-portrait-dialog/open", &answer);
  frame(); frame();
  bool scrollable = false;
  for (auto* window : ImGui::GetCurrentContext()->Windows)
    if (window->Active && (window->Flags & ImGuiWindowFlags_ChildWindow) && window->ScrollMax.y > 0)
      scrollable = true;
  CHECK_TRUE(scrollable);
  BeginCase("landscape does not open or capture input with the portrait modal");
  portrait = false;
  modal::Observe(1, 2, 100, true);
  modal::AnswerQuery("rbbz-portrait-dialog/open", &answer);
  frame();
  CHECK_FALSE(overlay->WantsExclusiveInput());
  overlay.reset();
  ImGui::DestroyContext();
  return Finish();
}
