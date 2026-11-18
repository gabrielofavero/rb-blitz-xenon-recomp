// SPDX-License-Identifier: GPL-2.0-only
#include "ui/menu_music_player.h"

#include <cfloat>

#include <imgui.h>
#include "settings/runtime_settings.h"
#include <rex/logging/log_category.h>
#include <rex/ui/ui_system.h>
#include <rex/ui/image_texture.h>
#include <rex/ui/style.h>

#include "graphics/preparation_loading.h"
#include "hooks/shell_music.h"
#include "input/input_capture.h"
#include "input/remap.h"
#include "input/title_input.h"
#include "ui/help_overlay.h"
#include "ui/achievements_overlay.h"
#include "ui/menu_music_artwork.h"
#include "ui/menu_music_text.h"


namespace rb_blitz::ui::music {
namespace {
namespace nav = input::menu_music_nav;
using shell_music::NowPlaying;
using shell_music::NowPlayingSnapshot;
constexpr nav::Command kCommands[] = {
    nav::Command::kPrevious, nav::Command::kPlayPause, nav::Command::kNext};
PointerRouter pointer;
Layout drawn_layout;
ArtTag drawn_tag;
float logical_w = 0, logical_h = 0;
bool pointer_over = false, drawn_visible = false;
bool Eligible(const NowPlayingSnapshot& s) {
  return rb_blitz::settings::title::enhancements_music_player.Get() && s.available &&
         s.playback.track.source != shell_music::MusicSource::kNone &&
         input::MainMenuMusicInputAvailable() && input::HostInputFocused() &&
         !input::capture::Armed() && !input::SystemUIActive() &&
         !achievements::Active() &&
         !graphics::GraphicsLoadingPending();
}
std::array<bool, 3> Enabled(const NowPlayingSnapshot& s) {
  const bool mouse = input::MouseSupportEnabled();
  return {mouse && s.playback.capabilities.previous,
          mouse && s.playback.capabilities.play_pause,
          mouse && s.playback.capabilities.next};
}
ImVec2 TL(MenuBounds b) { return {b.left, b.top}; }
ImVec2 BR(MenuBounds b) { return {b.right, b.bottom}; }
void Label(ImDrawList* draw, ImFont* font, float size, ImVec2 at, float width,
           ImU32 color, const std::string& text) {
  // UTF-8 boundary truncation; never split a multibyte character.
  std::string fit = DisplayTextUtf8(text);
  if (font->CalcTextSizeA(size, FLT_MAX, 0, fit.c_str()).x > width) {
    do {
      size_t end = fit.size() - 1;
      while (end && (static_cast<unsigned char>(fit[end]) & 0xC0) == 0x80)
        --end;
      fit.resize(end);
    } while (!fit.empty() &&
             font->CalcTextSizeA(size, FLT_MAX, 0, (fit + "...").c_str()).x >
                 width);
    fit += "...";
  }
  draw->AddText(font, size, at, color, fit.c_str());
}
class Player final : public rex::ui::DialogBase {
 public:
  explicit Player(rex::ui::IUISystem& ui)
      : ui_(ui), artwork_(shell_music::ReadArtwork) {}
  ~Player() override {
    CancelPlayerPointer();
    Release();
  }

 private:
  void Release() {
    texture_.reset();
    texture_tag_ = {};
  }
  bool Draw(ImGuiIO& io) override {
    const auto snapshot = NowPlaying().ReadSnapshot();
    const auto& track = snapshot.playback.track;
    const ArtTag tag{snapshot.epoch, track.song, track.artwork_key};
    const auto layout = PlayerLayout(io.DisplaySize.x, io.DisplaySize.y);
    const bool visible = Eligible(snapshot) && layout.scale > 0;
    drawn_visible = visible;
    drawn_layout = layout;
    drawn_tag = tag;
    logical_w = io.DisplaySize.x;
    logical_h = io.DisplaySize.y;
    pointer.Present(layout, tag.epoch, tag.song,
                    visible && input::MouseSupportEnabled(), Enabled(snapshot));
    if (!visible) {
      pointer_over = false;
      artwork_.Request({});
      Release();
      return true;
    }
    if (texture_tag_ != tag)
      Release();
    std::shared_ptr<const Cover> cover;
    if (!tag.key.empty())
      cover = artwork_.Request(tag);
    else
      artwork_.Request({});
    if (cover) {
      NowPlaying().PublishArtwork(tag.epoch, tag.song, tag.key, cover->state);
      if (cover->state == shell_music::ArtworkState::kReady && !texture_) {
        if (auto image = rex::ui::ImageTexture::Create(cover->width, cover->height, cover->rgba))
          texture_ = std::move(*image);
        texture_tag_ = tag;
      }
    }
    ImFont* font = help::CaptionFont();
    if (!font) font = ImGui::GetFont();
    const auto& theme = ui_.GetStyle().keyboard;
    ImFont* title_font = theme.heading_font ? theme.heading_font : font;
    if (theme.font) font = theme.font;
    auto* draw = ImGui::GetForegroundDrawList();
    const float s = layout.scale;
    draw->AddRectFilled(TL(layout.panel), BR(layout.panel),
                        layout.compact ? IM_COL32(16, 12, 30, 240) : IM_COL32(0, 0, 0, 190),
                        layout.compact ? 0 : 7 * s);
    if (!layout.compact && texture_)
      draw->AddImage(texture_->GetTexture(), TL(layout.cover), BR(layout.cover));
    else if (!layout.compact) {
      draw->AddRectFilled(TL(layout.cover), BR(layout.cover),
                          IM_COL32(20, 20, 20, 210), 3 * s);
      const float cx = (layout.cover.left + layout.cover.right) * .5f;
      const float cy = (layout.cover.top + layout.cover.bottom) * .5f;
      draw->AddCircle({cx, cy}, 24 * s, ImGui::GetColorU32(theme.muted_text), 32,
                      2 * s);
      draw->AddCircleFilled({cx, cy}, 5 * s, ImGui::GetColorU32(theme.muted_text));
    }
    const float left = layout.compact ? layout.panel.left + 12 * s : layout.controls[0].left;
    const float width = (layout.compact ? layout.controls[0].left : layout.panel.right) - left - 12 * s;
    Label(draw, title_font, (layout.compact ? 16 : 18) * s,
          {left, layout.panel.top + (layout.compact ? 16 : 12) * s}, width,
          ImGui::GetColorU32(theme.text),
          track.title.empty() ? "Menu music" : track.title);
    Label(draw, font, (layout.compact ? 13 : 14) * s,
          {left, layout.panel.top + (layout.compact ? 40 : 36) * s}, width,
          ImGui::GetColorU32(theme.muted_text),
          track.artist.empty() ? "Unknown artist" : track.artist);
    const auto enabled = Enabled(snapshot);
    const bool paused =
        snapshot.playback.state == shell_music::TransportState::kPaused;
    const MenuPoint mouse{io.MousePos.x, io.MousePos.y};
    for (int i = 0; i < 3; ++i) {
      const auto b = layout.controls[i];
      const bool hover = input::MouseSupportEnabled() && b.Contains(mouse);
      const bool pressed = pointer.armed() == i;
      draw->AddRectFilled(TL(b), BR(b), !enabled[i] ? IM_COL32(24, 24, 24, 180) :
          pressed ? IM_COL32(28, 28, 28, 255) : hover ? IM_COL32(65, 65, 65, 245) :
          layout.compact ? IM_COL32(16, 13, 28, 170) : IM_COL32(45, 45, 45, 230),
          layout.compact ? 0 : 2 * s);
      if (layout.compact) draw->AddRect(TL(b), BR(b), IM_COL32(255, 255, 255, 48));
      const ImU32 ink = ImGui::GetColorU32(enabled[i] ? theme.text :
          ImVec4(theme.muted_text.x, theme.muted_text.y, theme.muted_text.z, .4f));
      const float cx = (b.left + b.right) * .5f, cy = (b.top + b.bottom) * .5f;
      if (i == 1) {
        if (paused)
          draw->AddTriangleFilled({cx - 4 * s, cy - 7 * s},
                                  {cx - 4 * s, cy + 7 * s}, {cx + 7 * s, cy},
                                  ink);
        else {
          draw->AddRectFilled({cx - 6 * s, cy - 7 * s},
                              {cx - 2 * s, cy + 7 * s}, ink);
          draw->AddRectFilled({cx + 2 * s, cy - 7 * s},
                              {cx + 6 * s, cy + 7 * s}, ink);
        }
      } else {
        const float direction = i == 0 ? -1.f : 1.f;
        for (float offset : {-4.f, 4.f})
          draw->AddTriangleFilled(
              {cx + (offset - 3 * direction) * s, cy - 6 * s},
              {cx + (offset - 3 * direction) * s, cy + 6 * s},
              {cx + (offset + 3 * direction) * s, cy}, ink);
      }
    }
    // No optimistic transport changes: only the owner's snapshot controls
    // icons.
    if (snapshot.pending)
      draw->AddCircleFilled(
          {layout.panel.right - 9 * s, layout.panel.top + 9 * s}, 3 * s,
          IM_COL32(200, 200, 200, 255));
    return true;
}
  rex::ui::IUISystem& ui_;
  Artwork artwork_;
  std::unique_ptr<rex::ui::ImageTexture> texture_;
  ArtTag texture_tag_;
};
}  // namespace
std::unique_ptr<rex::ui::IDialog> CreatePlayer(rex::ui::IUISystem& ui) {
  return std::make_unique<Player>(ui);
}
void CancelPlayerPointer() {
  pointer.Cancel();
  pointer_over = false;
  drawn_visible = false;
}
bool PlayerPointerOver() { return pointer_over; }
bool RoutePlayerPointer(PointerEvent event, float x, float y, int pw, int ph,
                        float to_logical, int button) {
  const auto snapshot = NowPlaying().ReadSnapshot();
  const bool visible = drawn_visible && Eligible(snapshot) &&
                       input::MouseSupportEnabled() &&
                       std::abs(pw * to_logical - logical_w) < 1 &&
                       std::abs(ph * to_logical - logical_h) < 1 &&
                       snapshot.epoch == drawn_tag.epoch &&
                       snapshot.playback.track.song == drawn_tag.song;
  pointer.Present(drawn_layout, drawn_tag.epoch, drawn_tag.song, visible,
                  Enabled(snapshot));
  const auto p =
      LogicalPointer(x, y, float(pw), float(ph), logical_w, logical_h);
  pointer_over = visible && drawn_layout.panel.Contains(p);
  const auto result = pointer.Route(event, p, button);
  if (result.clicked) {
    const auto posted = nav::Post(kCommands[*result.clicked],
                                  nav::Producer::kPlayer, snapshot.epoch);
    REXLOG_INFO("music_player: click command={} request={} status={}",
                *result.clicked, posted.id, static_cast<int>(posted.status));
  }
  return result.consumed;
}
}  // namespace rb_blitz::ui::music
