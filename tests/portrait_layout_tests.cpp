// SPDX-License-Identifier: GPL-2.0-only
// Exercise the production panel draw hook with isolated guest memory, no UI.
#include "check.h"
#include "hooks/portrait_layout.h"
#include "ui/portrait_menu.h"
#include "ui/portrait_dialog.h"
#include "input/native_menu.h"
#include "input/portrait_panel.h"
#include "settings/resolution_profiles.h"
#include "settings/window_policy.h"

#include <array>
#include <limits>
#include <future>
#include <rex/ppc/hook.h>
#include <rex/memory/endian.h>

REX_EXTERN(sub_824C8E88);
namespace {
constexpr uint32_t kPanel = 0x1000, kLocal = kPanel + 88;
bool eligible = true, nested = false;
bool suppress_dialog = false;
unsigned dirty_calls = 0, draw_calls = 0;
float aspect = 9.f / 16;
std::array<float, 16> expected{};
void Close(float value, float wanted) { CHECK_TRUE(std::abs(value - wanted) < .001f); }
}

bool rb_blitz::ui::portrait_dialog::SuppressDirectory(uint32_t directory) {
  return suppress_dialog && directory == kPanel - 156;
}

std::optional<float> rb_blitz::input::PortraitPanelDrawAspect(uint8_t*, uint32_t drawable) {
  return eligible && drawable == kPanel ? std::optional<float>{aspect} : std::nullopt;
}
void rb_blitz::input::DrawPortraitPanel(PPCContext& ctx, uint8_t* base, uint32_t,
                                       void (*draw)(PPCContext&, uint8_t*)) {
  draw(ctx, base);
}
rb_blitz::input::MenuPoint rb_blitz::input::PortraitPanelTranslation() { return {}; }
extern "C" REX_FUNC(sub_821F0300) {
  CHECK_EQ(ctx.r3.u32, kPanel + 80);
  ++dirty_calls;
}
extern "C" REX_FUNC(__imp__sub_824C8E88) {
  ++draw_calls;
  for (size_t i = 0; i < expected.size(); ++i)
    Close(rex::memory::load_and_swap<float>(base + kLocal + i * 4), expected[i]);
  if (eligible) {
    const auto fit = rb_blitz::hooks::PortraitDrawFit();
    const auto point = rb_blitz::input::PortraitDrawPoint(50, -197);
    Close(point.x, fit.X(50)); Close(point.y, fit.Z(-197));
    if (!nested) {
      nested = true;
      ctx.r3.u32 = kPanel;
      sub_824C8E88(ctx, base);
      nested = false;
    }
  }
  ctx.r3.u32 = 0x12345678;
}

int main() {
  using namespace rb_blitz::test;
  using namespace rb_blitz::ui;
  using rb_blitz::settings::DisplayMode;
  BeginCase("guest draw threads consume the panel published by UI polling and observe retirement");
  rb_blitz::input::PortraitPanelPublisher panels;
  std::async(std::launch::async, [&] { panels.Publish(0x1234, true, "splash_screen"); }).get();
  auto snapshot = std::async(std::launch::async, [&] { return panels.Snapshot(); }).get();
  CHECK_EQ(snapshot.directory, 0x1234u);
  CHECK_TRUE(snapshot.active && snapshot.screen == "splash_screen");
  panels.Publish(0, false, "game_screen");
  snapshot = std::async(std::launch::async, [&] { return panels.Snapshot(); }).get();
  CHECK_EQ(snapshot.directory, 0u);
  CHECK_FALSE(snapshot.active);
  CHECK_TRUE(snapshot.screen == "game_screen");
  BeginCase("portrait pause fits all eight native actions inside its panel");
  CHECK_TRUE(PortraitSplashScreen("game_screen", "pause_panel"));
  CHECK_FALSE(PortraitSplashScreen("game_screen", "game_panel"));
  for (const float aspect : {.3f, 201.f / 437, 9.f / 16, .75f, .9f}) {
    const auto pause = PortraitPause(aspect);
    CHECK_TRUE(pause.width < PortraitSplash(aspect).width);
    CHECK_TRUE(pause.height < 480);
    CHECK_TRUE(367.f * pause.list_scale < pause.width);
    CHECK_TRUE(pause.list_z + 27.625f * pause.list_scale < pause.title_z - pause.title_size);
    CHECK_TRUE(pause.list_z - ((pause.rows - 1) * kPortraitMenuRowStep + 27.625f) *
               pause.list_scale > -pause.height / 2 + 10);
    CHECK_TRUE(kPortraitMenuRowStep > 55.25f);
  }
  BeginCase("library transitions retain portrait for both native panel layouts");
  for (const auto source : kPortraitLibraryScreens)
    for (const auto destination : kPortraitLibraryScreens) {
      const auto transition = std::string("trans_") + std::string(source) + "_" + std::string(destination);
      CHECK_TRUE(PortraitLibraryTransition(transition));
      CHECK_TRUE(PortraitSplashScreen(transition, ""));
    }
  for (const auto screen : {"song_select_screen", "trans_splash_screen_song_select_screen",
                            "trans_song_select_screen_powerup_select_screen",
                            "trans_song_select_screen_inbox_screen_extra"})
    CHECK_FALSE(PortraitLibraryTransition(screen));
  CHECK_TRUE(PortraitLibraryPanelScreen("songselect") == "song_select_screen");
  CHECK_TRUE(PortraitLibraryPanelScreen("songselect", true) == "store_browser_screen");
  CHECK_TRUE(PortraitLibraryPanelScreen("InboxPanel") == "inbox_screen");
  CHECK_TRUE(PortraitLibraryPanelScreen("RecommendationsPanel") == "recommendation_screen");
  CHECK_TRUE(PortraitLibraryPanelScreen("PanelDir").empty());
  BeginCase("library tab titles share the settled marquee center at every portrait width");
  for (const float aspect : {.3f, 201.f / 437, 9.f / 16, .75f, .9f}) {
    constexpr float rail_center = (204.37974548339844f + 169.7950897216797f) / 2;
    Close(PortraitSongs(aspect).tabs_z, rail_center);
    Close(PortraitMessages(aspect).tabs_z, rail_center);
    Close(PortraitRecommendations(aspect).tabs_z, rail_center);
  }
  BeginCase("portrait powerups separate the complete song card, three choices and footer");
  CHECK_TRUE(PortraitSplashScreen("powerup_select_screen", "powerup_select_panel"));
  for (float aspect : {.3f, 201.f / 437, 9.f / 16, .75f, .9f}) {
    const auto layout = PortraitPowerups(aspect);
    CHECK_EQ(layout.choices, 3);
    CHECK_TRUE(layout.card_z - layout.card_height / 2 > layout.categories_z + 45 * layout.category_scale);
    CHECK_TRUE(layout.categories_z - 45 * layout.category_scale > layout.choices_z + 25 * layout.choices_scale);
    CHECK_TRUE(layout.CategoryCaptionZ() - 8 > layout.choices_z + 25 * layout.choices_scale);
    CHECK_TRUE(layout.InfoZ() + 12 < layout.choices_z - 25 * layout.choices_scale);
    CHECK_TRUE(layout.InfoZ() - 12 > PortraitFooterAction(aspect, 0, 2).z + 30);
    CHECK_TRUE((64 + 25) * layout.choices_scale < layout.width / 2);
    CHECK_TRUE((64 + 46 + 16) * layout.choices_scale < layout.width / 2);
    CHECK_TRUE(layout.description_z > PortraitFooterAction(aspect, 0, 2).z + 30);
  }
  BeginCase("portrait songs reserve separate rows, details and footer at phone and tablet aspects");
  for (float aspect : {.3f, 201.f / 437, 9.f / 16, .75f, .9f}) {
    const auto layout = PortraitSongs(aspect);
    CHECK_TRUE(layout.rows >= 4 && layout.rows <= 7);
    CHECK_TRUE(layout.list_z - (layout.rows - 1) * kPortraitSongRowStep * layout.list_scale -
               kPortraitSongRowHeight / 2 * layout.list_scale > layout.card_z + layout.card_height / 2);
    for (size_t i = 0; i < 3; ++i) {
      const auto slot = PortraitSongsFooter(aspect, i, 3);
      CHECK_TRUE(std::abs(slot.x) + slot.width / 2 <= layout.width / 2 + .001f);
      CHECK_TRUE(slot.z + slot.height / 2 < layout.card_z - layout.card_height / 2);
    }
  }
  BeginCase("phone and tablet canvases stay inside asymmetric safe areas after resizing");
  for (const float authored : {.35f, 201.f / 437, 9.f / 16, .75f, .9f}) {
    for (const PortraitViewport v : {PortraitViewport{402, 874, 0, 62, 402, 778},
                                    {540, 960, 0, 0, 540, 960},
                                    {768, 1024, 20, 24, 748, 980},
                                    {320, 1000, 14, 40, 300, 930}}) {
      const auto fit = FitPortraitViewport(authored, v);
      CHECK_TRUE(fit.scale > 0 && fit.scale <= 1);
      for (float x : {-240 * authored, 240 * authored})
        for (float z : {-240.f, 240.f}) {
          const float px = v.width / 2 + fit.X(x) * v.height / 480;
          const float py = v.height / 2 - fit.Z(z) * v.height / 480;
          CHECK_TRUE(px > v.safe_x && px < v.safe_x + v.safe_width);
          CHECK_TRUE(py > v.safe_y && py < v.safe_y + v.safe_height);
        }
    }
  }
  const auto phone = FitPortraitViewport(9.f / 16, {402, 874, 0, 62, 402, 778});
  CHECK_TRUE(phone.scale < .8f);
  CHECK_TRUE(phone.z < 0); // more space reserved above than below
  Close(FitPortraitViewport(.75f, {402, 874}).scale,
        FitPortraitViewport(.75f, {402, 874, 0, 0, 402, 874}).scale);
  Close(FitPortraitViewport(.75f, {}).scale, 1);
  Close(FitPortraitViewport(std::numeric_limits<float>::quiet_NaN(), {402, 874}).scale, 1);

  BeginCase("standard phone resolutions and intermediate resize widths contain the entire authored canvas");
  // Physical pixels and logical points must produce the same layout. Include
  // compact 16:9 phones, notched iPhones, and 18:9 through 22:9 Android displays.
  for (const auto size : {DisplayMode{640,1136}, {750,1334}, {1080,1920},
                          {1125,2436}, {828,1792}, {1080,2340}, {1170,2532},
                          {1179,2556}, {1206,2622}, {1290,2796}, {1320,2868},
                          {1080,2160}, {1080,2400}, {1440,3120}, {1080,2520}}) {
    for (float density : {1.f, 2.f, 3.f}) {
      const float width = size.width / density, height = size.height / density;
      for (float safe_top : {0.f, height * .075f}) {
        PortraitViewport viewport{width, height, width * .01f, safe_top,
                                  width * .98f, height - safe_top - height * .04f};
        for (float authored : {width / height, 9.f / 16, .75f}) {
          const auto fit = FitPortraitViewport(authored, viewport);
          const float left = width / 2 + fit.X(-240 * authored) * height / 480;
          const float right = width / 2 + fit.X(240 * authored) * height / 480;
          const float top = height / 2 - fit.Z(240) * height / 480;
          const float bottom = height / 2 - fit.Z(-240) * height / 480;
          CHECK_TRUE(left > viewport.safe_x && right < viewport.safe_x + viewport.safe_width);
          CHECK_TRUE(top > safe_top && bottom < safe_top + viewport.safe_height);
          for (int resized = 240; resized <= 600; ++resized) {
            const auto narrow = FitPortraitViewport(authored, {float(resized), height});
            CHECK_TRUE(narrow.scale * authored * height < resized);
          }
        }
      }
    }
  }

  BeginCase("mobile preview follows 201:437 independently of desktop proportions and render scale");
  using namespace rb_blitz::settings;
  for (const DisplayMode native : {DisplayMode{1920,1080}, {2560,1080}, {3440,1440},
                                   {5120,1440}, {1080,1920}, {0,0}}) {
    const auto basis = MobilePreviewDisplayBasis(native);
    CHECK_TRUE(SameAspect(basis, {437, 201}, 3));
    CHECK_TRUE(SameAspect(PortraitCropSize(basis), {201, 437}, 3));
    if (native.width > 0 && native.height > 0) {
      CHECK_TRUE(basis.width <= native.width && basis.height <= native.height);
    }
  }

  BeginCase("production guest panel draw fits once and restores every authored matrix byte");
  std::array<uint8_t, 0x10000> memory{};
  std::array<float, 16> authored{1,0,0,99, 0,1,0,98, 0,0,1,97, 5,12,-10,96};
  PPCContext ctx{};
  ctx.r1.u32 = 0xE000;
  const auto write = [&] {
    for (size_t i = 0; i < authored.size(); ++i)
      rex::memory::store_and_swap<float>(memory.data() + kLocal + i * 4, authored[i]);
  };
  write();
  const auto original = memory;
  for (const PortraitViewport v : {PortraitViewport{402,874,0,62,402,778},
                                   {540,960,0,0,540,960}, {768,1024,0,24,768,980}}) {
    PublishPortraitViewport(v);
    expected = FitPortraitTransform(authored, FitPortraitViewport(aspect, v));
    for (int frame = 0; frame < 20; ++frame) {
      ctx.r3.u32 = kPanel;
      sub_824C8E88(ctx, memory.data());
      CHECK_EQ(ctx.r3.u32, 0x12345678);
      CHECK_TRUE(std::memcmp(memory.data() + kLocal, original.data() + kLocal, 64) == 0);
      Close(rb_blitz::hooks::PortraitDrawFit().scale, 1);
      Close(rb_blitz::hooks::PortraitDrawFit().x, 0);
      Close(rb_blitz::hooks::PortraitDrawFit().z, 0);
    }
  }
  CHECK_EQ(draw_calls, 120);
  CHECK_EQ(dirty_calls, 120); // dirty on entry and restoration, never for nested draws
  eligible = false;
  expected = authored;
  ctx.r3.u32 = kPanel;
  sub_824C8E88(ctx, memory.data());
  CHECK_EQ(draw_calls, 121);
  CHECK_EQ(dirty_calls, 120);
  BeginCase("host dialog suppression leaves all guest transforms and render state untouched");
  const uint32_t directory = kPanel - 156, object = 0x1600;
  rex::memory::store_and_swap<uint32_t>(memory.data() + directory + 4, 0x800);
  rex::memory::store_and_swap<uint32_t>(memory.data() + kPanel + 4, 0x900);
  rex::memory::store_and_swap<uint32_t>(memory.data() + 0x804, object - directory - 4);
  rex::memory::store_and_swap<uint32_t>(memory.data() + 0x904, object - kPanel - 4);
  suppress_dialog = true;
  const auto before = memory;
  ctx.r3.u32 = kPanel;
  sub_824C8E88(ctx, memory.data());
  CHECK_TRUE(before == memory);
  CHECK_EQ(ctx.r3.u32, kPanel);
  CHECK_EQ(draw_calls, 121);
  CHECK_EQ(dirty_calls, 120);
  return Finish();
}
