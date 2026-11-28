// SPDX-License-Identifier: GPL-2.0-only
#include "ui/menu_music_player.h"

#include <cfloat>
#include <mutex>

#include <imgui.h>
#include <rex/logging/log_category.h>
#include <rex/ui/image_texture.h>
#include <rex/ui/style.h>
#include <rex/ui/ui_system.h>

#include "graphics/preparation_loading.h"
#include "hooks/shell_music.h"
#include "input/input_capture.h"
#include "input/remap.h"
#include "input/title_input.h"
#include "settings/runtime_settings.h"
#include "ui/achievements_overlay.h"
#include "ui/help_overlay.h"
#include "ui/menu_music_artwork.h"
#include "ui/menu_music_text.h"
#include "ui/text_marquee.h"
#include "ui/portrait_viewport.h"

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
std::mutex touch_bounds_mutex;
std::optional<MenuBounds> normalized_touch_bounds;

void PublishTouchBounds(std::optional<MenuBounds> bounds, float width, float height) {
  std::lock_guard lock(touch_bounds_mutex);
  normalized_touch_bounds = bounds;
  if (bounds) {
    normalized_touch_bounds = MenuBounds{bounds->left / width, bounds->top / height,
                                         bounds->right / width, bounds->bottom / height};
  }
}

bool Eligible(const NowPlayingSnapshot& snapshot) {
  return settings::title::enhancements_music_player.Get() && snapshot.available &&
         snapshot.playback.track.source != shell_music::MusicSource::kNone &&
         input::MainMenuMusicInputAvailable() && input::HostInputFocused() &&
         !input::capture::Armed() && !input::SystemUIActive() &&
         !achievements::Active() && !graphics::GraphicsLoadingPending();
}
std::array<bool, 3> Enabled(const NowPlayingSnapshot& snapshot) {
  const bool mouse = input::MouseSupportEnabled();
  return {mouse && snapshot.playback.capabilities.previous,
          mouse && snapshot.playback.capabilities.play_pause,
          mouse && snapshot.playback.capabilities.next};
}
MenuBounds PortraitSafeArea(float width, float height) {
  if (width >= height) return {};
  const auto viewport = CurrentPortraitViewport();
  if (viewport.width <= 0 || viewport.height <= 0 ||
      std::abs(viewport.width / viewport.height - width / height) > .02f) return {};
  const float x = width / viewport.width, y = height / viewport.height;
  return {viewport.safe_x * x, viewport.safe_y * y,
          (viewport.safe_x + viewport.safe_width) * x,
          (viewport.safe_y + viewport.safe_height) * y};
}
ImVec2 TL(MenuBounds b) { return {b.left, b.top}; }
ImVec2 BR(MenuBounds b) { return {b.right, b.bottom}; }
MenuBounds OffsetY(MenuBounds b, float y) { return {b.left, b.top + y, b.right, b.bottom + y}; }

std::string SingleLine(std::string_view text) {
  std::string line = DisplayTextUtf8(text);
  for (char& c : line)
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  return line;
}

void Label(ImDrawList* draw, ImFont* font, float size, ImVec2 at, float width,
           ImU32 color, std::string_view text) {
  std::string fit = SingleLine(text);
  if (font->CalcTextSizeA(size, FLT_MAX, 0, fit.c_str()).x > width) {
    const float ellipsis = font->CalcTextSizeA(size, FLT_MAX, 0, "...").x;
    if (width < ellipsis) return;
    const char* end = nullptr;
    font->CalcTextSizeA(size, width - ellipsis, 0, fit.c_str(), nullptr, &end);
    fit.resize(static_cast<size_t>(end - fit.c_str()));
    fit += "...";
  }
  draw->AddText(font, size, at, color, fit.c_str());
}

void DrawSongTitle(ImDrawList* draw, ImFont* font, float size, ImVec2 at, float width,
                   ImU32 color, std::string_view title, float scale, TextMarquee& marquee) {
  if (width <= 0) return;
  const std::string text = SingleLine(title);
  const float text_width = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x;
  const float shift = marquee.Offset(text_width, width, scale, ImGui::GetTime());
  const ImVec4 clip{at.x, draw->GetClipRectMin().y, at.x + width, draw->GetClipRectMax().y};
  draw->AddText(font, size, {at.x - shift, at.y}, color, text.c_str(), nullptr, 0,
                text_width > width ? &clip : nullptr);
}

ImU32 Fade(ImU32 color, float opacity) {
  const auto alpha = static_cast<unsigned>((color >> IM_COL32_A_SHIFT) * opacity);
  return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

constexpr ImU32 kSheetBase = IM_COL32(0, 0, 0, 255);
constexpr ImU32 kSheetTop = IM_COL32(46, 46, 46, 255);
constexpr ImU32 kWhite = IM_COL32(244, 244, 244, 255);
constexpr ImU32 kMuted = IM_COL32(184, 184, 184, 255);

void DrawCover(ImDrawList* draw, MenuBounds bounds, const rex::ui::ImageTexture* texture,
               float rounding, ImU32 ink, ImU32 background, float opacity) {
  if (texture) {
    draw->AddImageRounded(texture->GetTexture(), TL(bounds), BR(bounds), {0, 0}, {1, 1},
                          Fade(IM_COL32_WHITE, opacity), rounding);
    return;
  }
  draw->AddRectFilled(TL(bounds), BR(bounds), Fade(background, opacity), rounding);
  const ImVec2 center{(bounds.left + bounds.right) * .5f, (bounds.top + bounds.bottom) * .5f};
  const float size = bounds.right - bounds.left;
  draw->AddCircle(center, size * (24.f / 76), Fade(ink, opacity), 32, size / 38);
  draw->AddCircleFilled(center, size * (5.f / 76), Fade(ink, opacity));
}

void TransportIcon(ImDrawList* draw, ImVec2 center, float s, int control, bool paused,
                   ImU32 ink) {
  const auto [cx, cy] = center;
  if (control == 1) {
    if (paused) {
      draw->AddTriangleFilled({cx - 4 * s, cy - 7 * s},
                              {cx - 4 * s, cy + 7 * s}, {cx + 7 * s, cy}, ink);
    } else {
      draw->AddRectFilled({cx - 6 * s, cy - 7 * s}, {cx - 2 * s, cy + 7 * s}, ink);
      draw->AddRectFilled({cx + 2 * s, cy - 7 * s}, {cx + 6 * s, cy + 7 * s}, ink);
    }
    return;
  }
  const float direction = control == 0 ? -1.f : 1.f;
  for (float offset : {-4.f, 4.f})
    draw->AddTriangleFilled(
        {cx + (offset - 3 * direction) * s, cy - 6 * s},
        {cx + (offset - 3 * direction) * s, cy + 6 * s},
        {cx + (offset + 3 * direction) * s, cy}, ink);
}

void DrawPortraitControls(ImDrawList* draw, const std::array<MenuBounds, 3>& bounds,
                          const std::array<bool, 3>& enabled, float offset, float s,
                          bool paused, bool expanded, MenuPoint mouse, float opacity) {
  for (int i = 0; i < 3; ++i) {
    const auto b = OffsetY(bounds[i], offset);
    const bool hover = pointer.active_transport() && enabled[i] && b.Contains(mouse);
    const bool pressed = pointer.armed() == i;
    const bool primary = expanded && i == 1;
    const ImU32 fill = !enabled[i] ? IM_COL32(255, 255, 255, 13)
        : primary ? (pressed ? kMuted : hover ? IM_COL32_WHITE : kWhite)
        : IM_COL32(255, 255, 255, pressed ? 42 : hover ? 36 : 18);
    const ImU32 ink = !enabled[i] ? Fade(kWhite, .42f) : primary ? kSheetBase : kWhite;
    const float rounding = primary ? 25 * s : 11 * s;
    draw->AddRectFilled(TL(b), BR(b), Fade(fill, opacity), rounding);
    TransportIcon(draw, {(b.left + b.right) * .5f, (b.top + b.bottom) * .5f},
                  (expanded ? 1.25f : 1.f) * s, i, paused, Fade(ink, opacity));
  }
}

class Player final : public rex::ui::DialogBase {
 public:
  explicit Player(rex::ui::IUISystem& ui)
      : ui_(ui), artwork_(shell_music::ReadArtwork) {}
  ~Player() override { CancelPlayerPointer(); }

 private:
  void Release() {
    texture_.reset();
    texture_tag_ = {};
  }
  void UpdateArtwork(const ArtTag& tag) {
    if (texture_tag_ != tag) Release();
    const auto cover = artwork_.Request(tag.key.empty() ? std::nullopt : std::optional{tag});
    if (!cover) return;
    if (texture_tag_ != tag) {
      NowPlaying().PublishArtwork(tag.epoch, tag.song, tag.key, cover->state);
      texture_tag_ = tag;
    }
    if (cover->state == shell_music::ArtworkState::kReady && !texture_) {
      if (auto image = rex::ui::ImageTexture::Create(cover->width, cover->height, cover->rgba))
        texture_ = std::move(*image);
    }
  }

  bool Draw(ImGuiIO& io) override {
    const auto snapshot = NowPlaying().ReadSnapshot();
    const auto& track = snapshot.playback.track;
    const ArtTag tag{snapshot.epoch, track.song, track.artwork_key};
    const auto layout = PlayerLayout(io.DisplaySize.x, io.DisplaySize.y,
                                     PortraitSafeArea(io.DisplaySize.x, io.DisplaySize.y));
    const bool visible = Eligible(snapshot) && layout.scale > 0;
    const auto enabled = Enabled(snapshot);
    if (!drawn_visible || layout.compact != drawn_layout.compact || tag != drawn_tag ||
        track.title != last_title_) {
      bar_title_.Reset();
      drawer_title_.Reset();
      last_title_ = track.title;
    }
    drawn_visible = visible;
    drawn_layout = layout;
    drawn_tag = tag;
    logical_w = io.DisplaySize.x;
    logical_h = io.DisplaySize.y;
    pointer.Present(layout, tag.epoch, tag.song,
                    visible && input::MouseSupportEnabled(), enabled);
    pointer.Animate(io.DeltaTime);
    PublishTouchBounds(pointer.HitBounds(), logical_w, logical_h);
    const MenuPoint mouse{io.MousePos.x, io.MousePos.y};
    pointer_over = visible && pointer.Owns(mouse);
    if (!visible) {
      artwork_.Request({});
      Release();
      return true;
    }
    UpdateArtwork(tag);

    ImFont* font = help::CaptionFont();
    if (!font) font = ImGui::GetFont();
    const auto& theme = ui_.GetStyle().keyboard;
    ImFont* title_font = theme.heading_font ? theme.heading_font : font;
    if (theme.font) font = theme.font;
    auto* draw = ImGui::GetForegroundDrawList();
    const std::string_view title = track.title.empty() ? "Menu music" : std::string_view(track.title);
    const std::string_view artist = track.artist.empty() ? "Unknown artist" : std::string_view(track.artist);
    const bool paused = snapshot.playback.state == shell_music::TransportState::kPaused;
    if (layout.compact) {
      DrawPortrait(draw, title_font, font, layout, enabled, paused, mouse,
                   title, artist, snapshot);
    } else {
      const float s = layout.scale;
      draw->AddRectFilled(TL(layout.panel), BR(layout.panel), IM_COL32(0, 0, 0, 190), 7 * s);
      DrawCover(draw, layout.cover, texture_.get(), texture_ ? 0.f : 3 * s,
                ImGui::GetColorU32(theme.muted_text), IM_COL32(20, 20, 20, 210), 1);
      const float left = layout.controls[0].left;
      const float width = layout.panel.right - left - 12 * s;
      DrawSongTitle(draw, title_font, 18 * s, {left, layout.panel.top + 12 * s}, width,
                    ImGui::GetColorU32(theme.text), title, s, bar_title_);
      Label(draw, font, 14 * s, {left, layout.panel.top + 36 * s}, width,
            ImGui::GetColorU32(theme.muted_text), artist);
      for (int i = 0; i < 3; ++i) {
        const auto b = layout.controls[i];
        const bool hover = input::MouseSupportEnabled() && b.Contains(mouse);
        const bool pressed = pointer.armed() == i;
        draw->AddRectFilled(TL(b), BR(b), !enabled[i] ? IM_COL32(24, 24, 24, 180)
            : pressed ? IM_COL32(28, 28, 28, 255) : hover ? IM_COL32(65, 65, 65, 245)
            : IM_COL32(45, 45, 45, 230), 2 * s);
        const ImU32 ink = ImGui::GetColorU32(enabled[i] ? theme.text :
            ImVec4(theme.muted_text.x, theme.muted_text.y, theme.muted_text.z, .4f));
        TransportIcon(draw, {(b.left + b.right) * .5f, (b.top + b.bottom) * .5f}, s,
                      i, paused, ink);
      }
      if (snapshot.pending)
        draw->AddCircleFilled({layout.panel.right - 9 * s, layout.panel.top + 9 * s},
                              3 * s, IM_COL32(200, 200, 200, 255));
    }
    return true;
  }

  void DrawPortrait(ImDrawList* draw, ImFont* heading, ImFont* font, const Layout& layout,
                    const std::array<bool, 3>& enabled, bool paused, MenuPoint mouse,
                    std::string_view title, std::string_view artist,
                    const NowPlayingSnapshot& snapshot) {
    const float s = layout.scale;
    const float expansion = pointer.expansion();
    const auto panel = layout.Panel(expansion);
    const auto handle = layout.Handle(expansion);
    const float center = (handle.left + handle.right) * .5f;
    if (expansion > 0)
      draw->AddRectFilled({0, 0}, {panel.right, panel.top},
                          Fade(IM_COL32(0, 0, 0, 165), expansion));
    draw->AddRectFilled(TL(panel), BR(panel), kSheetBase);
    draw->AddRectFilledMultiColor(TL(panel), {panel.right, std::min(panel.bottom, panel.top + 470 * s)},
                                 kSheetTop, kSheetTop, kSheetBase, kSheetBase);
    draw->AddLine(TL(panel), {panel.right, panel.top}, IM_COL32(255, 255, 255, 58));
    draw->AddRectFilled({center - 18 * s, panel.top + 9 * s},
                        {center + 18 * s, panel.top + 13 * s}, kWhite, 2 * s);
    if (snapshot.pending)
      draw->AddCircleFilled({layout.content.right, panel.top + 11 * s}, 3 * s, kWhite);

    draw->PushClipRect(TL(panel), BR(panel), true);
    const float compact_opacity = std::clamp(1 - 3 * expansion, 0.f, 1.f);
    if (compact_opacity > 0) {
      const float offset = panel.top - layout.panel.top;
      const bool has_cover = layout.cover.right > layout.cover.left;
      if (has_cover)
        DrawCover(draw, OffsetY(layout.cover, offset), texture_.get(), 7 * s,
                  Fade(kMuted, .75f), kSheetBase, compact_opacity);
      const float left = has_cover ? layout.cover.right + 11 * s : layout.cover.left;
      const float width = layout.controls[0].left - left - 9 * s;
      DrawSongTitle(draw, heading, 17 * s, {left, panel.top + 28 * s}, width,
                    Fade(kWhite, compact_opacity), title, s, bar_title_);
      Label(draw, font, 13 * s, {left, panel.top + 52 * s}, width,
            Fade(kMuted, compact_opacity), artist);
      DrawPortraitControls(draw, layout.controls, enabled, offset, s, paused, false,
                           mouse, compact_opacity);
    } else {
      bar_title_.Reset();
    }

    const float expanded_opacity = std::clamp((expansion - .18f) * 2.4f, 0.f, 1.f);
    if (expanded_opacity > 0) {
      const float offset = panel.top - layout.expanded_top;
      const float left = layout.content.left, right = layout.content.right;
      const float width = right - left;
      const float art_size = layout.expanded_cover_size;
      const float art_top = panel.top + 48 * s;
      const MenuBounds cover{center - art_size * .5f, art_top, center + art_size * .5f, art_top + art_size};
      constexpr char kNowPlaying[] = "Now playing";
      const float heading_width = heading->CalcTextSizeA(15 * s, FLT_MAX, 0, kNowPlaying).x;
      Label(draw, heading, 15 * s, {center - heading_width * .5f, panel.top + 23 * s}, width,
            Fade(kWhite, expanded_opacity), kNowPlaying);
      draw->AddRectFilled({cover.left - 3 * s, cover.top + 3 * s},
                          {cover.right + 3 * s, cover.bottom + 7 * s},
                          Fade(IM_COL32(0, 0, 0, 80), expanded_opacity), 13 * s);
      DrawCover(draw, cover, texture_.get(), 10 * s, kMuted, kSheetBase, expanded_opacity);
      DrawSongTitle(draw, heading, 27 * s, {left, cover.bottom + 13 * s}, width,
                    Fade(kWhite, expanded_opacity), title, s, drawer_title_);
      Label(draw, font, 17 * s, {left, cover.bottom + 46 * s}, width,
            Fade(kMuted, expanded_opacity), artist);
      if (!snapshot.playback.track.album.empty())
        Label(draw, font, 14 * s, {left, cover.bottom + 72 * s}, width,
              Fade(kMuted, expanded_opacity), snapshot.playback.track.album);

      DrawPortraitControls(draw, layout.expanded_controls, enabled, offset, s, paused, true,
                           mouse, expanded_opacity);
      const float queue_heading = cover.bottom + 167 * s;
      Label(draw, heading, 17 * s, {left, queue_heading}, width,
            Fade(kWhite, expanded_opacity), "Up next");
      const auto& queue = snapshot.playback.up_next;
      const float queue_top = queue_heading + 27 * s;
      if (queue.empty()) {
        Label(draw, font, 14 * s, {left, queue_top}, width,
              Fade(kMuted, expanded_opacity), "No tracks queued");
      } else {
        draw->AddRectFilled({left, queue_top},
                            {right, queue_top + queue.size() * 39 * s},
                            Fade(IM_COL32(255, 255, 255, 12), expanded_opacity), 10 * s);
        for (size_t i = 0; i < queue.size(); ++i) {
          const float y = queue_top + i * 39 * s;
          if (i)
            draw->AddLine({left + 11 * s, y}, {right - 11 * s, y},
                          Fade(IM_COL32(255, 255, 255, 24), expanded_opacity));
          Label(draw, font, 13 * s, {left + 11 * s, y + 12 * s}, 20 * s,
                Fade(kMuted, expanded_opacity), std::to_string(i + 1));
          const float row_x = left + 36 * s, row_width = right - row_x - 10 * s;
          Label(draw, heading, 14 * s, {row_x, y + 4 * s}, row_width,
                Fade(kWhite, expanded_opacity), queue[i].title);
          Label(draw, font, 12 * s, {row_x, y + 21 * s}, row_width,
                Fade(kMuted, expanded_opacity), queue[i].artist.empty() ? "Unknown artist" : queue[i].artist);
        }
      }
    } else {
      drawer_title_.Reset();
    }
    draw->PopClipRect();
  }

  rex::ui::IUISystem& ui_;
  Artwork artwork_;
  std::unique_ptr<rex::ui::ImageTexture> texture_;
  ArtTag texture_tag_;
  std::string last_title_;
  TextMarquee bar_title_, drawer_title_;
};
}  // namespace

std::unique_ptr<rex::ui::IDialog> CreatePlayer(rex::ui::IUISystem& ui) {
  return std::make_unique<Player>(ui);
}
void CancelPlayerPointer() {
  pointer.Cancel();
  PublishTouchBounds({}, 0, 0);
  pointer_over = false;
  drawn_visible = false;
}
bool PlayerPointerOver() { return pointer_over; }
bool PlayerOwnsTouchStart(float x, float y) {
  std::lock_guard lock(touch_bounds_mutex);
  return normalized_touch_bounds && normalized_touch_bounds->Contains({x, y});
}
bool RoutePlayerPointer(PointerEvent event, float x, float y, int pw, int ph,
                        float to_logical, int button) {
  const auto snapshot = NowPlaying().ReadSnapshot();
  const bool visible = drawn_visible && Eligible(snapshot) &&
                       input::MouseSupportEnabled() &&
                       std::abs(pw * to_logical - logical_w) < 1 &&
                       std::abs(ph * to_logical - logical_h) < 1 &&
                       snapshot.epoch == drawn_tag.epoch;
  auto enabled = Enabled(snapshot);
  if (snapshot.playback.track.song != drawn_tag.song ||
      snapshot.playback.track.artwork_key != drawn_tag.key) enabled = {};
  pointer.Present(drawn_layout, snapshot.epoch, snapshot.playback.track.song, visible, enabled);
  const auto p = LogicalPointer(x, y, float(pw), float(ph), logical_w, logical_h);
  const auto result = pointer.Route(event, p, button);
  pointer_over = visible && pointer.Owns(p);
  if (result.clicked) {
    const auto posted = nav::Post(kCommands[*result.clicked],
                                  nav::Producer::kPlayer, snapshot.epoch);
    REXLOG_INFO("music_player: click command={} request={} status={}",
                *result.clicked, posted.id, static_cast<int>(posted.status));
  }
  return result.consumed;
}
}  // namespace rb_blitz::ui::music
