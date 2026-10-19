// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The caption overlay's drawing and its two publishers. See src/ui/help_overlay.h for the design
// and the threading rules.
//
// Two things here are deliberately *not* the SDK's defaults (owner request, 2026-10-14):
//
//   * **The typeface.** The drawer's own font is ProggyTiny at 10 px (imgui_drawer.cpp), a pixel
//     face chosen for the debug overlays; the caption is player-facing text, so a system sans is
//     loaded instead and the caption falls back to the SDK's face if none can be read. The atlas is
//     baked at `kCaptionBakedSize` because this build's ImGui backend is a **legacy** one (it has
//     no `ImGuiBackendFlags_RendererHasTextures`, so glyphs are preloaded at each font's
//     `LegacySize` and any other requested size is scaled from the nearest bake): baking large and
//     drawing smaller is what keeps the text crisp, so the drawn size is clamped to the baked one.
//   * **Where it sits.** The caption is a subtitle above the A/B button prompts, not at the very
//     bottom of the frame where those prompts are drawn.
//
// Everything is derived from `io.DisplaySize`, which the drawer converts from the window's physical
// size into 96-DPI *logical* units, so a fraction of the display keeps the caption's proportion to
// the game's own UI at 720p, 1080p, ultrawide and at every DPI scale.

#include "ui/help_overlay.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "hooks/settings_bridge.h"
#include "input/input_capture.h"
#include "input/remap.h"
#include "settings/input_bindings.h"
#include "ui/help_topics.h"

REXCVAR_DEFINE_BOOL(settings_help_overlay, true, "Settings",
                    "Show a short description of the highlighted row at the bottom of the screen "
                    "while a settings menu is open (Help & Options and the retail pages it "
                    "opens). The same switch as Interface -> Settings Labels, applied live. "
                    "Faithful: off draws nothing at all.");

namespace rb_blitz::ui::help {
namespace {

// The current caption. Written by whichever source just changed it (guest thread), read and
// copied by the dialog's draw (UI thread). Equal text is a no-op, which is what makes the
// observer's once-per-frame publication free.
std::mutex g_text_mutex;
std::string g_text;

// The face the caption draws with: a system sans when one could be read, null for the SDK's own
// font (the fallback). Written once, inside the drawer's initialization, before any draw.
ImFont* g_caption_font = nullptr;

// --- the caption's type -------------------------------------------------------------------------
//
// The atlas bakes the face at this size, and it is the ceiling for the drawn size (see the file
// comment: this backend cannot add glyphs after the atlas is built, and upscaling a bake is what
// makes text soft).
constexpr float kCaptionBakedSize = 40.0f;
// The drawn size, as a fraction of the display height, and its bounds. 0.028 of 1080 is 30 px:
// three times the 10 px the overlay used to inherit, and about the height of a retail row label at
// that resolution.
constexpr float kCaptionSizeFraction = 0.028f;
constexpr float kCaptionMinSize = 16.0f;
constexpr float kCaptionMaxSize = kCaptionBakedSize;
// The transparent panel, tuned against the settings buttons the caption sits over. It was 0.72 -
// an opaque slab the size of a paragraph.
constexpr float kCaptionBackgroundAlpha = 0.32f;
// How far the caption's bottom edge sits above the frame's bottom. The A/B prompts live in the
// last tenth of the screen, so the caption is placed clear of them; the floor keeps it readable on
// a short window, and the fraction keeps the gap proportional on a tall one.
constexpr float kCaptionBottomFraction = 0.135f;
constexpr float kCaptionBottomMin = 56.0f;
// The wrap width as a fraction of the display width, and its floor.
constexpr float kCaptionWrapFraction = 0.72f;
constexpr float kCaptionWrapMin = 320.0f;
// The panel padding, as a fraction of the drawn text height.
constexpr float kCaptionPadFraction = 0.42f;

// The system faces to try, in order of preference: the Windows UI face first, then two broadly
// present sans faces. Tried by name under the Windows font directory, so the list names faces and
// never a machine's absolute path.
constexpr const char* kCaptionFontCandidates[] = {"segoeui.ttf", "tahoma.ttf", "arial.ttf"};

// The directory the candidates are looked up in, from the environment rather than a literal: an
// unset `WINDIR` means "cannot look", which falls back to the SDK's font instead of guessing a
// path.
std::string SystemFontDirectory() {
#ifdef _WIN32
  const char* windir = std::getenv("WINDIR");
  if (windir == nullptr || windir[0] == '\0') {
    return {};
  }
  return std::string(windir) + "\\Fonts\\";
#else
  return {};
#endif
}

float CaptionFontSize(const ImVec2 display) {
  return std::clamp(display.y * kCaptionSizeFraction, kCaptionMinSize, kCaptionMaxSize);
}

class CaptionSink final : public settings::bridge::HelpSink {
 public:
  void Show(const std::string_view symbol) override { PublishSymbol(symbol); }
};
CaptionSink g_sink;

class HelpOverlayDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit HelpOverlayDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}
  ~HelpOverlayDialog() override {
    if (settings::bridge::GetHelpSink() == &g_sink) {
      settings::bridge::SetHelpSink(nullptr);
    }
  }

  void OnDraw(ImGuiIO& io) override {
    // P8 (plan §14.4/§17.2): the recovery path's observation point. This draw runs every frame,
    // whether or not a device is connected - the guest's state filter does not run while nothing
    // enumerates, which is exactly the state a recovery exists for.
    input::TickInputRecovery();
    std::string text;
    {
      std::lock_guard lock(g_text_mutex);
      text = g_text;
    }
    // P7: the listening dialog's live state is not a row caption - it changes while the player
    // holds the page, and a wrong-kind press must say so without waiting for a selection move. It
    // is drawn even when Settings Labels is off, because a capture that cannot be cancelled is a
    // navigation problem rather than optional help.
    const std::string capture_status = input::capture::StatusText();
    const bool capturing = !capture_status.empty();
    // P8: the same rule for recovery. A profile with both host gates off and no pad leaves no
    // route at all, so the "hold F10" sentence cannot be optional either; it outranks a row
    // caption, and the two live states never apply at once (one needs a menu, the other needs no
    // usable input).
    const std::string recovery_status = input::RecoveryNotice();
    const bool recovering = !recovery_status.empty();
    if (recovering) {
      text = recovery_status;
    } else if (capturing) {
      text = capture_status;
    }
    // Off is invisible, not "drawn and empty", and the cvar is re-read here so the toggle is
    // live - the probe overlay's rule, and what makes Interface -> Settings Labels a `hot` row.
    if (text.empty() || (!capturing && !recovering && !REXCVAR_GET(settings_help_overlay))) {
      return;
    }

    const float size = CaptionFontSize(io.DisplaySize);
    const float pad = std::max(6.0f, size * kCaptionPadFraction);
    const float max_width = std::max(kCaptionWrapMin, io.DisplaySize.x * kCaptionWrapFraction);
    const float bottom = std::max(kCaptionBottomMin, io.DisplaySize.y * kCaptionBottomFraction);

    // The face is pushed for the window's own frame, so every metric below (padding, rounding,
    // wrap) is expressed against the size actually drawn.
    ImGui::PushFont(g_caption_font, size);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - bottom),
                            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(max_width + pad * 2.0f, 1e5f));
    ImGui::SetNextWindowBgAlpha(kCaptionBackgroundAlpha);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, std::max(2.0f, size * 0.22f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad * 0.55f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Begin("##rb_blitz_help", nullptr, flags)) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + max_width);
      ImGui::TextUnformatted(text.c_str());
      ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
  }
};

}  // namespace

void ConfigureCaptionFont(ImFontAtlas* atlas) {
  if (atlas == nullptr) {
    return;
  }
  const std::string directory = SystemFontDirectory();
  for (const char* candidate : kCaptionFontCandidates) {
    if (directory.empty()) {
      break;
    }
    const std::string path = directory + candidate;
    if (ImFont* font = atlas->AddFontFromFileTTF(path.c_str(), kCaptionBakedSize, nullptr,
                                                atlas->GetGlyphRangesDefault())) {
      g_caption_font = font;
      REXLOG_INFO("help_overlay: caption font '{}' baked at {:.0f} px", candidate,
                  static_cast<double>(kCaptionBakedSize));
      return;
    }
  }
  // No face could be read. The caption is still drawn - the SDK's own font, at the same derived
  // size - and the log says so, because a fallback that looks different must not be a surprise.
  g_caption_font = nullptr;
  REXLOG_WARN("help_overlay: no system sans font could be loaded from '{}'; the caption uses the "
              "SDK's own font at the caption size",
              directory.empty() ? "(no WINDIR)" : directory);
}

void PublishText(std::string text) {
  {
    std::lock_guard lock(g_text_mutex);
    if (g_text == text) {
      return;
    }
    g_text = text;
  }
  REXLOG_DEBUG("help_overlay: caption -> '{}'", text.empty() ? "(hidden)" : text);
}

void PublishSymbol(const std::string_view symbol) {
  // P7: a Controls binding row's caption is the row's current inputs, which the static table
  // cannot know. The store answers first; every other symbol keeps the existing resolution.
  if (settings::InputBindings() != nullptr) {
    const std::string described = settings::InputBindings()->Describe(symbol);
    if (!described.empty()) {
      PublishText(described);
      return;
    }
  }
  PublishText(TextForSymbol(symbol));
}

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer) {
  settings::bridge::SetHelpSink(&g_sink);
  return std::make_unique<HelpOverlayDialog>(drawer);
}

}  // namespace rb_blitz::ui::help
