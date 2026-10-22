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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/flags.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "hooks/gameplay_input.h"
#include "hooks/settings_bridge.h"
#include "input/effective_input.h"
#include "input/input_capture.h"
#include "input/native_menu.h"
#include "input/remap.h"
#include "profile/input_rows.h"
#include "settings/input_bindings.h"
#include "store/store_catalogue_status.h"
#include "store/store_download_service.h"
#include "store/store_search.h"
#include "ui/help_topics.h"
#include "ui/settings_screen.h"

// `store::SearchStatusText()` and the store's own P7 sentences are read every frame by the draw
// below; the namespace alias keeps the calls readable beside the other status sources.
namespace store_ns = rb_blitz::store;

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

// --- P1: the binding rows' own line -------------------------------------------------------------
//
// The owner asked for each binding row to draw the inputs it currently holds ("First Note: A"),
// not a state word: the list's row text is a locale entry baked at boot, so the host draws the
// line over the guest's own label instead. The geometry comes from the guest's draw
// (src/input/native_menu.cpp publishes every visible row's text box), the page's row identities
// come from the generated surface (ui::screen::ScreenBindingRows), and the values come from the
// published effective snapshot - which is why a capture shows up in the row the moment the store
// publishes, with no re-entry and no second copy of the binding tables.
//
// The guest's label is covered by the overlay's own background: two texts in one box would read
// as one. A line that does not fit the row's width is drawn as `Multiple` (the owner's
// allowance); a row where even that does not fit is left alone, so the guest's own label stays
// legible rather than being covered by nothing.

const std::vector<profile::remap::Source>* SourcesFor(
    const profile::InputRowToken& row, const input::EffectiveInput& snapshot) {
  switch (row.kind) {
    case profile::InputRowKind::kGeneralBinding:
      if (const auto target = profile::remap::ParseTarget(row.name)) {
        return snapshot.general.Effective(*target);
      }
      return nullptr;
    case profile::InputRowKind::kControllerBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(row.name)) {
        return snapshot.controller.Effective(*action);
      }
      return nullptr;
    case profile::InputRowKind::kKeyboardMouseBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(row.name)) {
        return snapshot.keyboard_mouse.Effective(*action);
      }
      return nullptr;
    default:
      return nullptr;
  }
}

// The guest's own line is measured by the draw, not re-derived: the label rectangle the guest's
// label widget projected is exactly the text it rendered (measured 2026-10-19: "A: Custom" box
// 162 px against 167 px of glyphs). It is the cover; the row's cell is the width budget.
void DrawBindingRows(ImGuiIO& io) {
  // The last set of lines drawn, so the log names each page's rows once (and says when the page
  // was left). Only the UI thread touches it, like every other value in this draw.
  static std::string last_lines;
  const auto finish = [](const char* why) {
    if (!last_lines.empty()) {
      REXLOG_INFO("help_overlay: binding rows {}", why);
      last_lines.clear();
    }
  };

  std::string screen;
  std::vector<input::MenuRowGeometry> rows;
  if (!input::VisibleMenuRows(&screen, &rows)) {
    finish("(no menu list)");
    return;
  }
  // P2's measurement, kept as a permanent evidence line: while one of our pages is open, does the
  // live song controller still carry the selected preset? A value here means the surface can name
  // the selection immediately, even before the next song; `none` means it can only show the last
  // preset a song observed (the restructure plan's open question 1).
  if (screen.starts_with("settings_")) {
    static std::string last_preset_line;
    const int live = gameplay_input::LiveControllerPreset();
    std::string line = screen + " live-preset=";
    line += live < 0 ? std::string("none (no song controller)")
                     : profile::gameplay_remap::PresetLabel(live);
    if (line != last_preset_line) {
      REXLOG_INFO("help_overlay: settings page {}", line);
      last_preset_line = line;
    }
  }
  const std::vector<std::string> tokens = ui::screen::ScreenBindingRows(screen);
  if (tokens.empty()) {
    finish("(page left)");
    return;
  }
  const std::shared_ptr<const input::EffectiveInput> snapshot = input::CurrentInput();
  if (!snapshot) {
    finish("(no input snapshot)");
    return;
  }

  const float width = io.DisplaySize.x, height = io.DisplaySize.y;
  if (!(width > 0.0f) || !(height > 0.0f)) {
    return;
  }
  const int frame_w = static_cast<int>(std::lround(width));
  const int frame_h = static_cast<int>(std::lround(height));
  const int aspect_w = REXCVAR_GET(video_mode_width);
  const int aspect_h = REXCVAR_GET(video_mode_height);
  const bool letterbox = REXCVAR_GET(present_letterbox);
  const bool overscan = REXCVAR_GET(present_allow_overscan_cutoff);
  const int safe_x = REXCVAR_GET(present_safe_area_x);
  const int safe_y = REXCVAR_GET(present_safe_area_y);
  const auto to_window = [&](const float x, const float y) {
    return input::MenuPointToWindow({x, y}, frame_w, frame_h, aspect_w, aspect_h, letterbox,
                                    overscan, safe_x, safe_y);
  };

  ImDrawList* draw = ImGui::GetForegroundDrawList();
  std::string lines;
  std::string fit;
  for (const input::MenuRowGeometry& entry : rows) {
    if (entry.index < 0 || static_cast<std::size_t>(entry.index) >= tokens.size()) {
      continue;
    }
    const std::string& token = tokens[static_cast<std::size_t>(entry.index)];
    if (token.empty()) {
      continue;
    }
    const profile::InputRowToken row = profile::ParseInputRowToken(token);
    if (!row.valid()) {
      continue;
    }
    const std::string line = profile::BindingRowText(row, SourcesFor(row, *snapshot));
    if (line.empty()) {
      continue;
    }
    const input::MenuPoint label_top = to_window(entry.label.left, entry.label.top);
    const input::MenuPoint label_end = to_window(entry.label.right, entry.label.bottom);
    const input::MenuPoint cell_top = to_window(entry.cell.left, entry.cell.top);
    const input::MenuPoint cell_end = to_window(entry.cell.right, entry.cell.bottom);
    const float label_width = label_end.x - label_top.x;
    const float label_height = label_end.y - label_top.y;
    const float row_width = cell_end.x - cell_top.x;
    if (!std::isfinite(label_width) || !std::isfinite(label_height) ||
        !std::isfinite(row_width) || label_width < 4.0f || label_height < 6.0f ||
        row_width < 16.0f) {
      continue;
    }
    // The caption face draws below the size it is asked for (the drawer's legacy path scales the
    // bake), so the ask is calibrated from the face's own answer: push the label's height, read
    // what came out, and scale the ask so the final line is about the guest's own height. Measured
    // 2026-10-19: without this the overlay drew half-height text inside a full-height box.
    const float probe = std::max(label_height, 11.0f);
    ImGui::PushFont(g_caption_font, probe);
    const float rendered = ImGui::GetFontSize();
    ImGui::PopFont();
    if (!(rendered > 0.0f)) {
      continue;
    }
    const float size =
        std::clamp(probe * (label_height * 0.75f) / rendered, 11.0f, kCaptionMaxSize);
    ImGui::PushFont(g_caption_font, size);
    const float pad = std::max(3.0f, ImGui::GetFontSize() * 0.22f);
    const float room = row_width - 6.0f;
    std::string shown = line;
    float text_width = ImGui::CalcTextSize(shown.c_str()).x;
    if (text_width + pad * 2.0f > room) {
      shown = "Multiple";
      text_width = ImGui::CalcTextSize(shown.c_str()).x;
    }
    if (fit.empty()) {
      char numbers[192];
      std::snprintf(numbers, sizeof(numbers),
                    "display=%.0fx%.0f label=%.1fx%.1f cell=%.1fx%.1f size=%.1f text=%.1f "
                    "room=%.1f",
                    static_cast<double>(width), static_cast<double>(height),
                    static_cast<double>(label_width), static_cast<double>(label_height),
                    static_cast<double>(row_width), static_cast<double>(cell_end.y - cell_top.y),
                    static_cast<double>(size), static_cast<double>(text_width),
                    static_cast<double>(room));
      fit = numbers;
    }
    if (text_width + pad * 2.0f <= room) {
      // Cover the guest's whole line - its label box plus a margin - and widen it only when the
      // replacement itself is wider. Both are measured in the same units the draw uses, so the
      // two texts can never show through each other.
      const float center_x = (label_top.x + label_end.x) * 0.5f;
      const float center_y = (label_top.y + label_end.y) * 0.5f;
      const float cover_width = std::max(label_width, text_width) + pad * 2.0f;
      const float text_height = ImGui::GetTextLineHeight();
      draw->AddRectFilled({center_x - cover_width * 0.5f, label_top.y - 1.0f},
                          {center_x + cover_width * 0.5f, label_end.y + 1.0f},
                          IM_COL32(5, 9, 18, 255), std::max(2.0f, size * 0.16f));
      draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    {center_x - text_width * 0.5f, center_y - text_height * 0.5f},
                    IM_COL32(255, 255, 255, 255), shown.c_str());
      if (!lines.empty()) {
        lines += " | ";
      }
      lines += token;
      lines += " = ";
      lines += shown;
    }
    ImGui::PopFont();
  }
  if (!lines.empty()) {
    lines = screen + ": " + lines;
  }
  if (lines != last_lines) {
    if (!fit.empty()) {
      REXLOG_INFO("help_overlay: row text fit {}", fit);
    }
    REXLOG_INFO("help_overlay: binding rows {}",
                lines.empty() ? std::string("(nothing to draw)") : lines);
    last_lines = lines;
  }
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
    // P1: a binding row draws its inputs over the guest's label. Drawn whether or not the caption
    // switch is on - it is the row's value, not help text - and before the caption, whose own
    // early-out must not take it with it.
    DrawBindingRows(io);
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
    // RhythmVerse plan P4: the search's field and its progress are input feedback too - the key
    // pages draw static labels, so the only place the typed query can appear is here. It outranks
    // every caption while the search surface is open (or a request is in flight), and it is drawn
    // with Settings Labels off for the same reason a capture is: a field a player cannot read is
    // an input problem, not optional help.
    const std::string search_status = store_ns::SearchStatusText();
    const bool searching = !search_status.empty();
    // P5: a store download's progress and its outcome are the same kind of input feedback - the
    // details page draws static rows, so the strip is where "42%" and "Installed, restart the
    // game" appear. It yields to the search field (an input surface takes precedence over a
    // progress line) and outranks a row caption.
    const std::string download_status = store_ns::DownloadStatusText();
    const bool downloading = !download_status.empty();
    // P7: the store's own words - the credit line when it opens, the loading sentence while the
    // first page is in flight, and the empty/offline sentence after it. It yields to every input
    // surface (the search field, a download's progress, the recovery notice, a capture) and
    // outranks a row caption: it is what explains a page that has nothing on it. The module itself
    // says nothing outside a short window after the store served a record, so the line cannot
    // follow a player out of the store.
    const std::string store_status = store_ns::CatalogueStatusText();
    const bool store_speaking = !store_status.empty();
    if (searching) {
      text = search_status;
    } else if (downloading) {
      text = download_status;
    } else if (recovering) {
      text = recovery_status;
    } else if (capturing) {
      text = capture_status;
    } else if (store_speaking) {
      text = store_status;
    }
    // Off is invisible, not "drawn and empty", and the cvar is re-read here so the toggle is
    // live - the probe overlay's rule, and what makes Interface -> Settings Labels a `hot` row.
    // The store's own sentences are drawn regardless: the credit line is the attribution the
    // store has to carry, and an empty or unreachable catalogue is a state a player cannot explain
    // from the screen, the same rule the search field follows.
    if (text.empty() ||
        (!capturing && !recovering && !searching && !downloading && !store_speaking &&
         !REXCVAR_GET(settings_help_overlay))) {
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
