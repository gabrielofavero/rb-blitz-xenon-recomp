// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "input/native_menu.h"
#include "input/touch_scroll.h"
#include "input/reading_viewport.h"
#include "input/inline_help.h"
#include "input/dialog_prompts.h"
#include "input/pad_packet.h"
#include <unordered_map>
using namespace rb_blitz::input;
using namespace rb_blitz::test;

int main() {
  BeginCase("transition scene lookups use ObjectDir rather than its drawable or virtual object base");
  {
    constexpr uint32_t directory = 0x1000, drawable = directory + 156, object = 0x1300;
    std::unordered_map<uint32_t, uint32_t> words{
        {directory + 4, 0x2000}, {drawable + 4, 0x2100},
        {0x2004, object - directory - 4}, {0x2104, object - drawable - 4}};
    const auto read = [&](uint32_t address) { return words.at(address); };
    const auto lookup_directory = PanelDirectoryFromDrawable(drawable, read);
    CHECK_EQ(lookup_directory, directory);
    CHECK_TRUE(lookup_directory != drawable && lookup_directory != object);
    // ObjectDir's hash table is at +8; the virtual object has unrelated bytes
    // there. Sending it to FindObject reproduces the invalid hash-table read.
    words[directory + 8] = 0x3000;
    words[object + 8] = 0xFFFFFFFF;
    CHECK_EQ(read(lookup_directory + 8), 0x3000u);
    words[0x2004] += 4;
    CHECK_EQ(PanelDirectoryFromDrawable(drawable, read), 0u);
    words[directory + 4] = 0;
    CHECK_EQ(PanelDirectoryFromDrawable(drawable, read), 0u);
    CHECK_EQ(PanelDirectoryFromDrawable(0, read), 0u);
  }
  const auto scroll_point = [](float y, float x = 100.f, float pitch = 40.f) {
    return TouchScrollPoint{x, y, y / pitch};
  };
  BeginCase("touch tap slop does not move the viewport or turn a horizontal gesture into a tap");
  TouchListScroll kinetic;
  kinetic.Begin(scroll_point(200), 10, 5000, 1);
  kinetic.Move(scroll_point(205), 1.02);
  CHECK_TRUE(std::abs(kinetic.Position() - 10) < .001f);
  CHECK_TRUE(kinetic.End(scroll_point(205), 1.03));
  CHECK_FALSE(kinetic.End(scroll_point(205), 1.04));
  kinetic.Begin(scroll_point(200), 10, 5000, 2);
  kinetic.Move(scroll_point(202, 130), 2.02);
  CHECK_FALSE(kinetic.Dragging());
  CHECK_FALSE(kinetic.End(scroll_point(202, 130), 2.03));

  BeginCase("dragging follows the finger in fractional rows and never activates on release");
  kinetic.Reset();
  kinetic.Begin(scroll_point(200), 10, 5000, 3);
  kinetic.Move(scroll_point(150), 3.04);
  CHECK_TRUE(kinetic.Dragging());
  CHECK_TRUE(std::abs(kinetic.Position() - 11.25f) < .001f);
  CHECK_FALSE(kinetic.End(scroll_point(150), 3.05));
  CHECK_TRUE(kinetic.Velocity() > 0);
  kinetic.Advance(3.10);
  CHECK_TRUE(kinetic.Position() > 11.25f);

  BeginCase("catching a moving list stops it without opening the song under the finger");
  const float moving_position = kinetic.Position();
  kinetic.Begin(scroll_point(200), 11, 5000, 3.10);
  CHECK_TRUE(std::abs(kinetic.Position() - moving_position) < .001f);
  CHECK_TRUE(kinetic.Velocity() == 0);
  CHECK_FALSE(kinetic.End(scroll_point(200), 3.12));
  kinetic.Begin(scroll_point(200), 11, 5000, 3.20);
  CHECK_TRUE(kinetic.End(scroll_point(200), 3.22));

  BeginCase("momentum decays independently of the guest frame rate");
  kinetic.Reset();
  kinetic.Begin(scroll_point(300), 20, 5000, 4);
  kinetic.Move(scroll_point(260), 4.02);
  kinetic.Move(scroll_point(200), 4.04);
  CHECK_FALSE(kinetic.End(scroll_point(200), 4.05));
  auto slow_frames = kinetic, fast_frames = kinetic;
  for (int i = 1; i <= 30; ++i) slow_frames.Advance(4.05 + i / 30.0);
  for (int i = 1; i <= 120; ++i) fast_frames.Advance(4.05 + i / 120.0);
  CHECK_TRUE(std::abs(slow_frames.Position() - fast_frames.Position()) < .001f);
  CHECK_TRUE(std::abs(slow_frames.Velocity() - fast_frames.Velocity()) < .001f);
  for (int i = 1; i <= 60; ++i) fast_frames.Advance(5.05 + i / 30.0);
  CHECK_TRUE(fast_frames.Velocity() == 0);

  BeginCase("a held finger and a suspended guest do not launch stale momentum");
  kinetic.Begin(scroll_point(200), 20, 5000, 6);
  kinetic.Move(scroll_point(100), 6.03);
  CHECK_FALSE(kinetic.End(scroll_point(100), 6.30));
  CHECK_TRUE(kinetic.Velocity() == 0);
  kinetic.Begin(scroll_point(200), 20, 5000, 7);
  kinetic.Move(scroll_point(100), 7.03);
  kinetic.End(scroll_point(100), 7.04);
  const float before_suspend = kinetic.Position();
  kinetic.Advance(9);
  CHECK_TRUE(kinetic.Position() == before_suspend);
  CHECK_TRUE(kinetic.Velocity() == 0);

  BeginCase("both list boundaries clamp drag and momentum, including shortened filter results");
  kinetic.Reset();
  kinetic.Begin(scroll_point(200), 1, 5, 10);
  kinetic.Move(scroll_point(400), 10.02);
  CHECK_TRUE(kinetic.Position() == 0);
  CHECK_TRUE(kinetic.Velocity() == 0);
  kinetic.Move(scroll_point(360), 10.04);
  CHECK_TRUE(kinetic.Position() > 0); // reversing immediately leaves the boundary
  kinetic.Move(scroll_point(-1000), 10.06);
  CHECK_TRUE(kinetic.Position() == 5);
  CHECK_TRUE(kinetic.Velocity() == 0);
  CHECK_FALSE(kinetic.End(scroll_point(-1000), 10.07));
  kinetic.Limit(2);
  CHECK_TRUE(kinetic.Position() == 2);

  BeginCase("touch movement uses the actual row pitch while tap slop stays in window points");
  kinetic.Reset();
  kinetic.Begin(scroll_point(200, 100, 20), 5, 50, 11);
  kinetic.Move(scroll_point(175, 100, 20), 11.02);
  CHECK_TRUE(std::abs(kinetic.Position() - 6.25f) < .001f);
  kinetic.Reset(); // window, dialog and physical input boundaries abandon the gesture
  CHECK_FALSE(kinetic.OwnsView());
  CHECK_FALSE(kinetic.Pressed());
  CHECK_FALSE(kinetic.End(scroll_point(175), 11.03));

  BeginCase("touch menus hide idle selection and activate once after feedback and release");
  MenuTouchHighlight touch;
  CHECK_FALSE(touch.Draw(true, 0x10, 0, false)); // default selected row remains unlit
  touch.Press(0x10, 3);
  CHECK_FALSE(touch.TakeAccept(true, 0x10, 3, false)); // a tap between frames waits for feedback
  CHECK_FALSE(touch.Draw(true, 0x20, 0, false)); // other visible lists stay unlit
  CHECK_TRUE(touch.Draw(true, 0x10, 3, false));
  CHECK_FALSE(touch.Draw(true, 0x10, 3, false)); // release cannot leave it yellow
  CHECK_TRUE(touch.TakeAccept(true, 0x10, 3, false));
  CHECK_FALSE(touch.TakeAccept(true, 0x10, 3, false)); // activation is delivered once
  CHECK_FALSE(touch.Draw(true, 0x10, 3, false));
  touch.Press(0x10, 3);
  CHECK_TRUE(touch.Draw(true, 0x10, 3, true));
  CHECK_TRUE(touch.Draw(true, 0x10, 3, true));
  CHECK_FALSE(touch.TakeAccept(true, 0x10, 3, true)); // hold shows feedback without activating
  CHECK_TRUE(touch.TakeAccept(true, 0x10, 3, false));
  touch.Reset(); // navigating to another page cannot transfer the press
  CHECK_FALSE(touch.Draw(true, 0x20, 0, true));
  CHECK_FALSE(touch.Draw(true, 0x20, 0, false));

  BeginCase("touch cancellation and physical navigation preserve the native selection anchor");
  touch.Press(0x10, 3);
  CHECK_TRUE(touch.Draw(true, 0x10, 3, true));
  touch.Move(0x10, -1); // moving off the pressed row cancels it
  touch.Move(0x10, 3); // moving back does not create another press
  CHECK_FALSE(touch.Draw(true, 0x10, 3, true));
  CHECK_FALSE(touch.TakeAccept(true, 0x10, 3, false));
  touch.Press(0x10, 3);
  CHECK_FALSE(touch.Draw(true, 0x10, 4, true)); // changing selection cancels feedback
  CHECK_FALSE(touch.TakeAccept(true, 0x10, 4, false));
  touch.Press(0x10, 3);
  CHECK_TRUE(touch.Draw(false, 0x10, 3, false)); // keyboard/controller restores the same row
  CHECK_FALSE(touch.TakeAccept(false, 0x10, 3, false));
  CHECK_FALSE(touch.Draw(true, 0x10, 3, false)); // switching back to touch clears selection art
  touch.Press(0x10, 3);
  touch.Reset(); // disabled input or focus loss discards pending activation
  CHECK_FALSE(touch.TakeAccept(true, 0x10, 3, false));

  BeginCase("footer feedback follows mouse hover and active touch presses across the full button");
  const MenuBounds footer_bounds{.2f, .85f, .4f, .95f};
  CHECK_TRUE(MenuFooterHovered(MenuPoint{.21f, .9f}, footer_bounds, true, false));
  CHECK_TRUE(MenuFooterHovered(MenuPoint{.39f, .9f}, footer_bounds, true, false));
  CHECK_FALSE(MenuFooterHovered(MenuPoint{.41f, .9f}, footer_bounds, true, false));
  CHECK_FALSE(MenuFooterHovered(MenuPoint{.3f, .9f}, footer_bounds, false, false));
  CHECK_FALSE(MenuFooterHovered(MenuPoint{.3f, .9f}, footer_bounds, true, true));
  CHECK_TRUE(MenuFooterHovered(MenuPoint{.21f, .9f}, footer_bounds, true, true, true));
  CHECK_TRUE(MenuFooterHovered(MenuPoint{.39f, .9f}, footer_bounds, true, true, true));
  CHECK_FALSE(MenuFooterHovered(MenuPoint{.41f, .9f}, footer_bounds, true, true, true));
  CHECK_FALSE(MenuFooterHovered(MenuPoint{.3f, .9f}, footer_bounds, false, true, true));
  CHECK_FALSE(MenuFooterHovered({}, footer_bounds, true, false));
  CHECK_FALSE(MenuFooterHovered(MenuPoint{NAN, NAN}, footer_bounds, true, false));

  BeginCase("touch footer actions wait for a highlighted draw and release, including taps between frames");
  MenuFooterTouchPress press;
  press.Begin({.3f, .9f}, 0x2000);
  CHECK_EQ(press.Poll(true, false), 0); // a quick tap still needs feedback
  CHECK_FALSE(press.Draw({.5f, .85f, .7f, .95f}));
  CHECK_EQ(press.Poll(true, false), 0);
  CHECK_TRUE(press.Draw(footer_bounds));
  CHECK_EQ(press.Poll(true, true), 0); // hold keeps the button on screen
  CHECK_EQ(press.Poll(true, false), 0x2000);
  CHECK_FALSE(press.Draw(footer_bounds));
  CHECK_EQ(press.Poll(true, false), 0); // deliver once
  press.Begin({.3f, .9f}, 0x2000);
  CHECK_TRUE(press.Draw(footer_bounds));
  CHECK_EQ(press.Poll(false, false), 0); // lost focus discards the action
  CHECK_FALSE(press.Draw(footer_bounds));
  press.Begin({.3f, .9f}, 0x2000);
  CHECK_TRUE(press.Draw(footer_bounds));
  press.Reset(); // a different screen/panel must not inherit the action
  CHECK_EQ(press.Poll(true, false), 0);

  BeginCase("footer captions center on live text and icon quads instead of layout spacing or stale vertices");
  {
    constexpr uint32_t label = 0x40100000, batches = 0x40101000;
    constexpr uint32_t text_mesh = 0x40102000, icon_mesh = 0x40103000;
    constexpr uint32_t text_vertices = 0x40104000, icon_vertices = 0x40105000;
    std::unordered_map<uint32_t, uint32_t> words{
        {label + 132, batches}, {label + 136, batches + 24},
        {batches + 4, text_mesh}, {batches + 8, 1},
        {batches + 16, icon_mesh}, {batches + 20, 1},
        {text_mesh + 324, text_mesh}, {text_mesh + 256, text_vertices}, {text_mesh + 260, 8},
        {icon_mesh + 324, icon_mesh}, {icon_mesh + 256, icon_vertices}, {icon_mesh + 260, 4}};
    std::unordered_map<uint32_t, float> floats;
    for (uint32_t i = 0; i < 4; ++i) {
      floats[text_vertices + i * 96] = i & 1 ? 30.f : 10.f;
      floats[text_vertices + i * 96 + 8] = i & 2 ? 8.f : -4.f;
      floats[icon_vertices + i * 96] = i & 1 ? 5.f : -9.f;
      floats[icon_vertices + i * 96 + 8] = i & 2 ? 10.f : -8.f;
      floats[text_vertices + (i + 4) * 96] = 200.f; // unused mesh capacity
      floats[text_vertices + (i + 4) * 96 + 8] = 200.f;
    }
    const auto read_word = [&](uint32_t at) { return words.at(at); };
    const auto read_float = [&](uint32_t at) { return floats.at(at); };
    auto measured = InlineHelpGlyphBounds(label, read_word, read_float);
    CHECK_TRUE(measured.has_value());
    CHECK_TRUE((*measured == std::array<float, 4>{-9, -8, 30, 10}));
    words[batches + 20] = 0; // touch hides the icon without discarding its allocated mesh
    measured = InlineHelpGlyphBounds(label, read_word, read_float);
    CHECK_TRUE(measured.has_value());
    CHECK_TRUE((*measured == std::array<float, 4>{10, -4, 30, 8}));
    // The reading viewport clips both fonts independently and ignores unused
    // mesh capacity. Scrolling the strip exposes the previously hidden glyph.
    words[batches + 20] = 1;
    auto clipped = ReadingGlyphsOutside(label, -5, 9, read_word, read_float);
    CHECK_TRUE(clipped && clipped->size() == 1);
    if (clipped && clipped->size() == 1) {
      CHECK_EQ(clipped->front().mesh, icon_mesh);
      CHECK_EQ(clipped->front().vertices, icon_vertices);
      CHECK_TRUE((clipped->front().x == std::array<float, 4>{-9, 5, -9, 5}));
    }
    clipped = ReadingGlyphsOutside(label, -9, 11, read_word, read_float);
    CHECK_TRUE(clipped && clipped->empty());
    clipped = ReadingGlyphsOutside(label, -9, 0, read_word, read_float);
    CHECK_TRUE(clipped && clipped->size() == 2);
    CHECK_FALSE(ReadingGlyphsOutside(label, 10, 0, read_word, read_float));
    CHECK_FALSE(ReadingGlyphsOutside(label, NAN, 0, read_word, read_float));
    words[batches + 20] = 0;
    words[batches + 8] = 2;
    for (uint32_t i = 4; i < 8; ++i) {
      floats[text_vertices + i * 96] = 0;
      floats[text_vertices + i * 96 + 8] = 0;
    }
    measured = InlineHelpGlyphBounds(label, read_word, read_float);
    CHECK_TRUE(measured.has_value());
    CHECK_TRUE((*measured == std::array<float, 4>{10, -4, 30, 8}));
    floats[text_vertices] = NAN;
    CHECK_FALSE(InlineHelpGlyphBounds(label, read_word, read_float));
    floats[text_vertices] = 10;
    words[batches + 8] = 3; // invalid live count exceeds the allocation
    CHECK_FALSE(InlineHelpGlyphBounds(label, read_word, read_float));
    CHECK_FALSE(ReadingGlyphsOutside(label, -9, 11, read_word, read_float));
    CHECK_FALSE(InlineHelpGlyphBounds(0, read_word, read_float));
  }
  BeginCase("pause and native song confirmations retain the gameplay resolution");
  for (const auto screen : {"game_screen", "pause_confirm_quit_song_screen",
                            "pause_confirm_restart_song_screen"})
    CHECK_TRUE(GameplayResolutionScreen(screen));
  for (const auto screen : {"", "splash_screen", "loading_screen", "song_select_screen",
                            "settings_screen", "settings_pause_screen",
                            "settings_av_pause_screen", "settings_display_pause_screen",
                            "settings_resolution_pause_screen", "settings_scale_pause_screen",
                            "settings_display_confirm_pause_screen", "settings_restart_pause_screen",
                            "av_options_pause_screen", "controller_config_pause_screen",
                            "tutorials_pause_screen", "pause_credits_screen"})
    CHECK_FALSE(GameplayResolutionScreen(screen));
  BeginCase("pause switches resolution for inline Help and Options and returns to gameplay");
  CHECK_TRUE(GameplayResolutionScreen("game_screen", "pause_panel", "pause.lst"));
  CHECK_FALSE(GameplayResolutionScreen("game_screen", "pause_panel", "options.lst"));
  CHECK_FALSE(GameplayResolutionScreen("settings_general_pause_screen", "settings_general_pause_panel",
                                      "options.lst"));
  CHECK_FALSE(GameplayResolutionScreen("game_screen", "pause_panel", "options.lst"));
  CHECK_TRUE(GameplayResolutionScreen("game_screen", "pause_panel", "pause.lst"));
  CHECK_TRUE(GameplayResolutionScreen("game_screen", "game_panel"));

  BeginCase("dialogs own their prompts and background footer targets cannot receive presses");
  CHECK_TRUE(DialogScopeFor("song_select_panel", "shortcuts.lst") == MenuDialogScope::kShortcuts);
  CHECK_TRUE(DialogScopeFor("song_select_panel", "songs.lst") == MenuDialogScope::kNone);
  for (const auto panel : {"rbbz_download_panel", "rbbz_search_loading_panel",
                           "rbbz_library_panel", "error_popup_panel"})
    CHECK_TRUE(DialogScopeFor(panel, "") == MenuDialogScope::kPanel);

  BeginCase("song searches, RhythmVerse and Messages share vertical touch scrolling");
  for (const auto screen : {"song_select_screen", "store_menu_screen", "store_browser_screen", "inbox_screen"}) {
    CHECK_TRUE(MenuTouchScrollEligible(screen, MenuDialogScope::kNone, false));
    CHECK_FALSE(MenuTouchScrollEligible(screen, MenuDialogScope::kNone, true));
    CHECK_TRUE(MenuTouchScrollEligible(screen, MenuDialogScope::kShortcuts, false));
    for (const auto panel : {"rbbz_download_panel", "rbbz_search_loading_panel",
                             "rbbz_library_panel", "error_popup_panel"})
      CHECK_FALSE(MenuTouchScrollEligible(screen, DialogScopeFor(panel, ""), false));
  }
  for (const auto screen : {"", "store_info_screen", "store_details_screen", "splash_screen",
                            "game_screen", "settings_screen"})
    CHECK_FALSE(MenuTouchScrollEligible(screen, MenuDialogScope::kNone, false));

  BeginCase("dialog scope excludes background footer targets");
  const std::vector<MenuHintTarget> scoped{
      {1, 2, 10, {.2f, .8f, .4f, .9f}, 0x1000, 0},
      {1, 2, 10, {.2f, .6f, .4f, .7f}, 0x2000, 42}};
  CHECK_EQ(HitMenuHint({.3f, .65f}, scoped, 1, 2, 11, 42), 0x2000);
  CHECK_EQ(HitMenuHint({.3f, .85f}, scoped, 1, 2, 11, 42), 0);
  CHECK_EQ(HitMenuHint({.3f, .65f}, scoped, 1, 2, 11), 0);
  CHECK_EQ(HitMenuHint({.3f, .85f}, scoped, 1, 2, 11), 0x1000);
  MenuMouseQueue popup_queue;
  popup_queue.Context(1, 0, true, 20);
  popup_queue.Push(MouseAction::kAccept);
  popup_queue.PulseButton(0x1000);
  popup_queue.Context(1, 0, true, 21); // focus-less popup replaces another panel
  CHECK_EQ(popup_queue.SamplePad(), 0);
  CHECK_FALSE(popup_queue.NextFrame());

  BeginCase("dialog action spans use native measured advances and leave the gap inert");
  const auto measure_fixture = [](std::u16string_view text) {
    std::vector<float> widths{0};
    float width = 0;
    bool tag = false;
    for (const auto ch : text) {
      if (ch == u'<') tag = true;
      if (!tag) width += ch == u' ' ? 2.f : ch == u'A' || ch == u'B' ? 12.f : 7.f;
      widths.push_back(width);
      if (ch == u'>') tag = false;
    }
    return widths;
  };
  const std::u16string cancel = u"Press <alt1>B</alt1> to Cancel";
  const auto cancel_spans = DialogPromptSpans(cancel, measure_fixture(cancel));
  CHECK_EQ(cancel_spans.size(), 1);
  CHECK_EQ(cancel_spans.at(0).action, 2);
  CHECK_TRUE(cancel_spans.at(0).left == 0 && cancel_spans.at(0).right == 1);
  const std::u16string dual = u"<alt1>A</alt1> Refresh   <alt1>B</alt1> Close";
  const auto spans = DialogPromptSpans(dual, measure_fixture(dual));
  CHECK_EQ(spans.size(), 2);
  CHECK_EQ(spans.at(0).action, 1);
  CHECK_EQ(spans.at(1).action, 2);
  CHECK_TRUE(spans.at(0).right > .5f && spans.at(0).right < spans.at(1).left);
  const std::vector<MenuHintTarget> dual_targets{
      {1, 0, 10, {spans.at(0).left, .6f, spans.at(0).right, .7f}, 0x1000, 42},
      {1, 0, 10, {spans.at(1).left, .6f, spans.at(1).right, .7f}, 0x2000, 42}};
  CHECK_EQ(HitMenuHint({.25f, .65f}, dual_targets, 1, 0, 11, 42), 0x1000);
  CHECK_EQ(HitMenuHint({.85f, .65f}, dual_targets, 1, 0, 11, 42), 0x2000);
  CHECK_EQ(HitMenuHint({(spans[0].right + spans[1].left) / 2, .65f},
                       dual_targets, 1, 0, 11, 42), 0);
  for (const std::u16string text : {u"Please wait", u"<alt1>Z</alt1> Unknown", u"<alt1>B Broken"})
    CHECK_TRUE(DialogPromptSpans(text, measure_fixture(text)).empty());
  CHECK_TRUE(DialogPromptSpans(cancel, {}).empty());
  auto bad_widths = measure_fixture(cancel); bad_widths.back() = NAN;
  CHECK_TRUE(DialogPromptSpans(cancel, bad_widths).empty());
  const std::u16string private_cancel = u"<alt>\x82</alt> Close";
  CHECK_EQ(DialogPromptSpans(private_cancel, measure_fixture(private_cancel)).at(0).action, 2);
  const std::u16string uppercase = u"Press <ALT1>B</ALT1> to Cancel";
  CHECK_EQ(DialogPromptSpans(uppercase, measure_fixture(uppercase)).at(0).action, 2);

  BeginCase("footer hit capture preserves SetWorldXfm placement instead of recomputing the local origin");
  constexpr uint32_t transformable = 0x40100500;
  unsigned evaluations = 0;
  uint8_t dirty = 0;
  const auto read_dirty = [&](uint32_t address) {
    CHECK_EQ(address, transformable + 189);
    return dirty;
  };
  const auto evaluate = [&](uint32_t object) {
    CHECK_EQ(object, transformable);
    ++evaluations;
    return uint32_t{0x40100800}; // Evaluator would replace the footer's temporary matrix.
  };
  CHECK_EQ(WorldTransformForDraw(transformable, read_dirty, evaluate), transformable + 72);
  CHECK_EQ(evaluations, 0); // A clean footer must keep its rendered bottom-of-screen position.
  dirty = 1;
  CHECK_EQ(WorldTransformForDraw(transformable, read_dirty, evaluate), 0x40100800);
  CHECK_EQ(evaluations, 1); // Dirty labels still resolve their current parent/local placement.

  BeginCase("guest footer records preserve full labels and their native actions");
  {
    const InlineHelpEntry localized{0x40100100, 1, 0x40101000};
    std::unordered_map<uint32_t, uint32_t> caption_words{
        {localized.record + 4, 0x40102000}, {localized.record + 16, 0x40103000}};
    const auto read_caption = [&] {
      return InlineHelpCaptionToken(localized,
          [&](uint32_t address) { return caption_words.at(address); },
          [](uint32_t address) -> std::string_view {
            return address == 0x40102000 ? "select" : "Choisir";
          });
    };
    CHECK_TRUE(read_caption() == "select");
    CHECK_TRUE(CalibrationFooterCaption(true, read_caption()) == "rbbz_calibration_next");
    const std::vector<InlineHelpEntry> calibration{
        {0x40100100, 1, 1}, {0x40100200, 1, 2}, {0x40100300, 2, 3}};
    const auto caption = [](const auto& entry) -> std::string_view {
      return entry.record == 1 ? "select" : entry.record == 2 ? "retry" : "inline_help_back";
    };
    const auto visible = CalibrationFooterEntries(calibration, caption);
    CHECK_EQ(visible.size(), 3);
    CHECK_EQ(visible[0].label, calibration[2].label); // Back stays left.
    CHECK_EQ(visible[1].label, calibration[0].label); // Next remains available.
    CHECK_EQ(visible[2].label, calibration[1].label); // Retry has its own upper row.
    CHECK_EQ(MenuHintButton(visible[0].action), 0x2000);
    CHECK_EQ(MenuHintButton(visible[1].action), 0x1000);
    CHECK_EQ(MenuHintButton(visible[2].action), 0x1000);
    for (const auto token : {"retry", "rbbz_loading_retry"}) {
      // The failure panel owns only Retry; the covered calibration panel owns
      // Back/Next. The one-entry widget must still use the upper three-slot pose.
      const auto placement = CalibrationFooterPlacement(true, token, 0, 1);
      CHECK_EQ(placement.index, 2);
      CHECK_EQ(placement.count, 3);
      const auto ordinary = CalibrationFooterPlacement(false, token, 0, 1);
      CHECK_EQ(ordinary.index, 0);
      CHECK_EQ(ordinary.count, 1);
    }
    const auto next = CalibrationFooterPlacement(true, "select", 1, 2);
    CHECK_EQ(next.index, 1);
    CHECK_EQ(next.count, 2);
    const auto back_only = CalibrationFooterEntries(
        std::vector<InlineHelpEntry>{calibration[0], calibration[2]}, caption);
    CHECK_EQ(back_only.size(), 2);
    CHECK_EQ(back_only[0].label, calibration[2].label);
    CHECK_EQ(back_only[1].label, calibration[0].label);
    CHECK_EQ(CalibrationFooterEntries(std::vector<InlineHelpEntry>{calibration[0]}, caption).size(), 1);
    CHECK_TRUE(CalibrationFooterCaption(true, "select") == "rbbz_calibration_next");
    CHECK_TRUE(CalibrationFooterCaption(true, "continue") == "rbbz_calibration_next");
    CHECK_TRUE(CalibrationFooterCaption(false, "select") == "select");
    CHECK_TRUE(CalibrationFooterCaption(true, "retry") == "retry");
    CHECK_TRUE(CalibrationFooterCaption(true, "inline_help_back") == "inline_help_back");
    CHECK_EQ(calibration.size(), 3); // Guest navigation retains its complete native action list.
  }
  BeginCase("recommendation footer shows only Back then Select with their original guest records");
  {
    const std::vector<InlineHelpEntry> actions{{10, 4, 100}, {20, 2, 200}, {30, 5, 300}, {40, 1, 400}, {50, 3, 500}};
    for (unsigned mask = 1; mask < 32; ++mask) {
      std::vector<InlineHelpEntry> visible;
      for (size_t i = 0; i < actions.size(); ++i) if (mask & (1u << i)) visible.push_back(actions[i]);
      const auto ordered = RecommendationFooterEntries(visible);
      CHECK_EQ(ordered.size(), size_t(bool(mask & 2)) + size_t(bool(mask & 8)));
      for (const auto& entry : visible)
        CHECK_EQ(std::count_if(ordered.begin(), ordered.end(), [&](const auto& n) {
          return n.label == entry.label && n.action == entry.action && n.record == entry.record;
        }), entry.action == 1 || entry.action == 2 ? 1 : 0);
      if (mask & 2) CHECK_EQ(ordered[0].action, 2u);
      if (mask & 8) CHECK_EQ(ordered[(mask & 2) ? 1 : 0].action, 1u);
    }
    CHECK_EQ(actions[0].label, 10u);
  }

  constexpr uint32_t footer = 0x40001000, actions = 0x40002000, labels = 0x40003000;
  std::unordered_map<uint32_t, uint32_t> words{
      {footer + 80, actions}, {footer + 84, actions + 4 * 28},
      {footer + 92, labels}, {footer + 96, labels + 4 * 4},
      {actions, 1}, {actions + 28, 2}, {actions + 56, 4}, {actions + 84, 3},
      {labels, 0x40100100}, {labels + 4, 0x40100200},
      {labels + 8, 0x40100300}, {labels + 12, 0x40100400}};
  const auto read = [&words](uint32_t address) { return words.at(address); };
  const auto entries = ReadInlineHelpEntries(footer, read);
  CHECK_EQ(entries.size(), 4);
  for (size_t i = 0; i < entries.size(); ++i) {
    CHECK_EQ(entries[i].label, 0x40100100 + i * 0x100);
    CHECK_EQ(entries[i].action, read(actions + uint32_t(i) * 28));
    CHECK_EQ(entries[i].record, actions + uint32_t(i) * 28);
  }
  CHECK_EQ(MenuHintButton(entries.at(1).action), 0x2000); // Back uses its own record
  CHECK_EQ(MenuHintButton(entries.at(2).action), 0x10);   // Search is Start
  BeginCase("separate footer draws preserve dynamic labels and restore both native vectors");
  const auto original_words = words;
  unsigned drawn = 0;
  CHECK_TRUE(DrawInlineHelpLabelsSeparately(footer, read,
      [&words](uint32_t address, uint32_t value) { words.at(address) = value; },
      [&](size_t index) {
        const auto visible = ReadInlineHelpEntries(footer, read);
        CHECK_EQ(visible.size(), 1);
        CHECK_EQ(visible.front().label, entries[index].label);
        CHECK_EQ(visible.front().action, entries[index].action);
        ++drawn;
      }));
  CHECK_EQ(drawn, 4);
  CHECK_TRUE(words == original_words);
  words[actions] = 14; // A later calibration state replaces Confirm with another native action.
  words[labels] = 0x40100500;
  CHECK_TRUE(DrawInlineHelpLabelsSeparately(footer, read,
      [&words](uint32_t address, uint32_t value) { words.at(address) = value; },
      [&](size_t index) {
        if (index == 0) {
          const auto visible = ReadInlineHelpEntries(footer, read);
          CHECK_EQ(visible.front().action, 14);
          CHECK_EQ(visible.front().label, 0x40100500);
        }
      }));
  CHECK_EQ(words.at(footer + 84), actions + 4 * 28);
  CHECK_EQ(words.at(footer + 96), labels + 4 * 4);
  words = original_words;
  words[footer + 96] += 4;
  CHECK_TRUE(ReadInlineHelpEntries(footer, read).empty()); // inconsistent parallel arrays
  words[footer + 96] -= 4;
  words[labels + 4] = 0;
  CHECK_EQ(ReadInlineHelpEntries(footer, read).size(), 3); // never dereference an invalid label
  CHECK_EQ(ReadInlineHelpEntries(footer, read).at(1).record, actions + 2 * 28);
  words[footer + 84] = actions - 28;
  CHECK_TRUE(ReadInlineHelpEntries(footer, read).empty());
  CHECK_TRUE(ReadInlineHelpEntries(0, read).empty()); // reject before touching guest memory

  BeginCase("footer clicks and touch presses hit only visible legend blocks in the current context");
  const std::vector<MenuHintTarget> hints{
      {1, 2, 10, {.1f, .85f, .3f, .95f}, MenuHintButton(1)},
      {1, 2, 10, {.4f, .85f, .6f, .95f}, MenuHintButton(2)},
      {1, 2, 10, {.7f, .85f, .9f, .95f}, MenuHintButton(4)}};
  CHECK_EQ(HitMenuHint({.11f, .9f}, hints, 1, 2, 11), 0x1000); // icon
  CHECK_EQ(HitMenuHint({.29f, .9f}, hints, 1, 2, 11), 0x1000); // caption
  CHECK_EQ(HitMenuHint({.5f, .9f}, hints, 1, 2, 11), 0x2000);
  CHECK_EQ(HitMenuHint({.8f, .9f}, hints, 1, 2, 11), 0x10); // Search
  CHECK_EQ(HitMenuHint({.35f, .9f}, hints, 1, 2, 11), 0); // gap
  CHECK_EQ(HitMenuHint({.5f, .8f}, hints, 1, 2, 11), 0);
  CHECK_EQ(HitMenuHint({NAN, .9f}, hints, 1, 2, 11), 0);
  CHECK_EQ(HitMenuHint({.5f, .9f}, hints, 3, 2, 11), 0); // retired screen
  CHECK_EQ(HitMenuHint({.5f, .9f}, hints, 1, 3, 11), 0); // retired focus
  CHECK_EQ(HitMenuHint({.5f, .9f}, hints, 1, 2, 12), 0); // no longer drawn
  CHECK_EQ(HitMenuHint({.5f, .9f}, hints, 1, 2, 10), 0);
  CHECK_EQ(MenuHintButton(0), 0);
  CHECK_EQ(MenuHintButton(99), 0);

  BeginCase("all footer actions survive repeated pad polls and deliver a release");
  for (const auto [action, button] : std::vector<std::pair<uint32_t, uint16_t>>{
           {1, 0x1000}, {2, 0x2000}, {3, 0x8000}, {4, 0x10},
           {5, 0x20}, {11, 0x200}, {14, 0x4000}}) {
    MenuMouseQueue clicks;
    clicks.Context(1, 2, true);
    clicks.PulseButton(MenuHintButton(action));
    CHECK_EQ(clicks.SamplePad(), button);
    CHECK_EQ(clicks.SamplePad(), button);
    CHECK_FALSE(clicks.NextFrame());
    CHECK_FALSE(clicks.NextFrame());
    CHECK_EQ(clicks.SamplePad(), 0);
    clicks.Push(MouseAction::kBack);
    CHECK_TRUE(clicks.NextFrame().has_value());
    clicks.PulseButton(button);
    clicks.Context(3, 2, true);
    CHECK_EQ(clicks.SamplePad(), 0); // context change cancels a pending press
  }

  BeginCase("library wheel scales with content and never exceeds a visible page");
  CHECK_EQ(LibraryWheelRows(51, 10), 1);
  CHECK_EQ(LibraryWheelRows(100, 10), 1);
  CHECK_EQ(LibraryWheelRows(101, 10), 2);
  CHECK_EQ(LibraryWheelRows(501, 10), 6);
  CHECK_EQ(LibraryWheelRows(5000, 10), 10);
  CHECK_EQ(LibraryWheelRows(5000, 4), 4);
  CHECK_EQ(LibraryWheelRows(0, 0), 1);
  CHECK_EQ(LibraryWheelRows(-1, 10), 1);

  BeginCase("only library screens and their authored navigation labels are header targets");
  CHECK_EQ(LibraryHeaderForScreen("song_select_screen"), 0);
  CHECK_EQ(LibraryHeaderForScreen("store_browser_screen"), 1);
  CHECK_EQ(LibraryHeaderForScreen("inbox_screen"), 2);
  CHECK_EQ(LibraryHeaderForScreen("recommendation_screen"), 3);
  CHECK_EQ(LibraryHeaderForScreen("store_info_screen"), -1);
  CHECK_EQ(LibraryHeaderForLabel("main_my_songs.lbl"), 0);
  CHECK_EQ(LibraryHeaderForLabel("main_store.lbl"), 1);
  CHECK_EQ(LibraryHeaderForLabel("main_inbox.lbl"), 2);
  CHECK_EQ(LibraryHeaderForLabel("main_Home.lbl"), 3);
  CHECK_EQ(LibraryHeaderForLabel("main_new_msg.lbl"), -1);

  BeginCase("portrait row surfaces follow provider widgets rather than unnamed label clones");
  CHECK_TRUE(PortraitListRowField("store_menu_screen", "filter"));
  CHECK_FALSE(PortraitListRowField("store_menu_screen", "count"));
  for (const auto field : {"song", "album", "group", "group_center"})
    CHECK_TRUE(PortraitListRowField("store_browser_screen", field));
  CHECK_FALSE(PortraitListRowField("store_browser_screen", "purchased"));
  CHECK_TRUE(PortraitListRowField("store_info_screen", "list"));
  CHECK_TRUE(PortraitListRowField("inbox_screen", "subject_line"));
  CHECK_TRUE(PortraitListRowField("inbox_screen", "header"));
  CHECK_FALSE(PortraitListRowField("inbox_screen", "new"));
  CHECK_FALSE(PortraitListRowField("store_menu_screen", ""));
  CHECK_FALSE(PortraitListRowField("powerup_select_screen", "name"));

  BeginCase("preset arrows dispatch their matching D-pad directions only on their art");
  const MenuBounds left_arrow{.22f, .25f, .24f, .31f};
  const MenuBounds right_arrow{.74f, .25f, .76f, .31f};
  CHECK_TRUE(HitMenuArrow({.23f, .28f}, left_arrow, right_arrow) == MouseAction::kLeft);
  CHECK_TRUE(HitMenuArrow({.75f, .28f}, left_arrow, right_arrow) == MouseAction::kRight);
  CHECK_FALSE(HitMenuArrow({.5f, .28f}, left_arrow, right_arrow));
  CHECK_FALSE(HitMenuArrow({.23f, .35f}, left_arrow, right_arrow));
  CHECK_FALSE(HitMenuArrow({NAN, .28f}, left_arrow, right_arrow));
  CHECK_FALSE(HitMenuArrow({.23f, .28f}, std::nullopt, std::nullopt));
  MenuMouseQueue preset_clicks;
  preset_clicks.Context(1, 2, true);
  preset_clicks.Pulse(*HitMenuArrow({.23f, .28f}, left_arrow, right_arrow));
  CHECK_EQ(preset_clicks.SamplePad(), 4);
  preset_clicks.Reset();
  preset_clicks.Pulse(*HitMenuArrow({.75f, .28f}, left_arrow, right_arrow));
  CHECK_EQ(preset_clicks.SamplePad(), 8);

  BeginCase("powerup paging targets only arrows drawn for the current page");
  // The first page has no back arrow; the last page has no next arrow.
  CHECK_FALSE(HitMenuArrow({.23f, .28f}, std::nullopt, right_arrow));
  CHECK_TRUE(HitMenuArrow({.75f, .28f}, std::nullopt, right_arrow) == MouseAction::kRight);
  CHECK_TRUE(HitMenuArrow({.23f, .28f}, left_arrow, std::nullopt) == MouseAction::kLeft);
  CHECK_FALSE(HitMenuArrow({.75f, .28f}, left_arrow, std::nullopt));
  // A small margin makes the thin arrow art practical to tap.
  CHECK_TRUE(HitMenuArrow({.77f, .28f}, std::nullopt, right_arrow) == MouseAction::kRight);
  CHECK_FALSE(HitMenuArrow({.79f, .28f}, std::nullopt, right_arrow));
  // Five visible icons from seven entries: either arrow reveals the other end.
  CHECK_EQ(MenuArrowPage(0, 5, 2, MouseAction::kRight), 2);
  CHECK_EQ(MenuArrowPage(2, 5, 2, MouseAction::kLeft), 0);
  CHECK_EQ(MenuArrowPage(0, 5, 2, MouseAction::kLeft), 0);
  CHECK_EQ(MenuArrowPage(2, 5, 2, MouseAction::kRight), 2);
  CHECK_EQ(MenuArrowPage(5, 5, 15, MouseAction::kRight), 10);
  CHECK_EQ(MenuArrowPage(10, 5, 15, MouseAction::kLeft), 5);
  CHECK_EQ(MenuArrowPage(0, 5, 0, MouseAction::kRight), 0);
  // Portrait reveals three large icons at a time, including a partial last page.
  CHECK_EQ(MenuArrowPage(0, 3, 4, MouseAction::kRight), 3);
  CHECK_EQ(MenuArrowPage(3, 3, 4, MouseAction::kRight), 4);
  CHECK_EQ(MenuArrowPage(4, 3, 4, MouseAction::kLeft), 1);
  CHECK_EQ(MenuArrowPage(1, 3, 4, MouseAction::kLeft), 0);

  BeginCase("pointer mapping respects letterboxing, stretch and overscan");
  auto p = MenuPointer(1280, 360, 2560, 720, 16, 9, true, false, 90, 90);
  CHECK_TRUE(p.x == .5f && p.y == .5f);
  p = MenuPointer(640, 360, 2560, 720, 16, 9, true, false, 90, 90);
  CHECK_TRUE(p.x == 0 && p.y == .5f);
  p = MenuPointer(640, 360, 2560, 720, 16, 9, false, false, 90, 90);
  CHECK_TRUE(p.x == .25f && p.y == .5f);
  p = MenuPointer(400, 0, 800, 800, 16, 9, true, false, 90, 90);
  CHECK_TRUE(p.x == .5f && p.y < 0);
  p = MenuPointer(1280, 0, 2560, 720, 16, 9, true, true, 90, 90);
  CHECK_TRUE(std::abs(p.y - .05f) < .001f);
  CHECK_FALSE(std::isfinite(MenuPointer(0, 0, 0, 0, 16, 9, true, false, 90, 90).x));

  // The overlay's forward mapping is the pointer's inverse for the same presenter rectangle: it
  // is how a row the guest drew lands in the window coordinates the host draws in.
  {
    const auto round_trip = [](float x, float y, int w, int h, int aw, int ah, bool letterbox,
                               bool overscan) {
      const MenuPoint guest = MenuPointer(x, y, w, h, aw, ah, letterbox, overscan, 90, 90);
      return MenuPointToWindow(guest, w, h, aw, ah, letterbox, overscan, 90, 90);
    };
    auto back = round_trip(1280, 360, 2560, 720, 16, 9, true, false);
    CHECK_TRUE(std::abs(back.x - 1280) <= 1.0f && std::abs(back.y - 360) <= 1.0f);
    back = round_trip(200, 120, 2560, 1080, 21, 9, true, false);
    CHECK_TRUE(std::abs(back.x - 200) <= 1.0f && std::abs(back.y - 120) <= 1.0f);
    // A stretched (non-letterboxed) frame still round-trips: the rectangle is wider than the
    // guest's aspect, and both directions use that same rectangle.
    back = round_trip(640, 360, 2560, 720, 16, 9, false, false);
    CHECK_TRUE(std::abs(back.x - 640) <= 1.0f && std::abs(back.y - 360) <= 1.0f);
    // The frame's own (0,0) is the presented rectangle's top-left, not the window's: a 16:9 frame
    // in a 2560x720 window is pillarboxed, so the guest's corner sits at x = 640.
    const MenuPoint corner = MenuPointToWindow({0, 0}, 2560, 720, 16, 9, true, false, 90, 90);
    CHECK_TRUE(corner.x == 640.0f && corner.y == 0.0f);
    CHECK_FALSE(std::isfinite(MenuPointToWindow({.5f, .5f}, 0, 0, 16, 9, true, false, 90, 90).x));
  }
  BeginCase("native geometry selects distant rows directly and rejects disabled/outside rows");
  MenuBounds bounds{.2f, .1f, .8f, .9f};
  std::vector<MenuRow> rows{{{.5f,.2f}, 0, true}, {{.5f,.5f}, 7, false}, {{.5f,.8f}, 25, true}};
  CHECK_EQ(HitMenuRow({.5f,.8f}, bounds, rows), 25);
  CHECK_EQ(HitMenuRow({.5f,.5f}, bounds, rows), -1);
  CHECK_EQ(HitMenuRow({.9f,.8f}, bounds, rows), -1);
  CHECK_EQ(HitMenuRow({NAN,.8f}, bounds, rows), -1);

  BeginCase("icon-only powerup cells support direct selection without text");
  std::vector<MenuRow> icons{
      {{.15f,.30f}, 0, true, MenuBounds{.08f,.17f,.23f,.43f}},
      {{.30f,.30f}, 1, true, MenuBounds{.23f,.17f,.38f,.43f}},
      {{.45f,.30f}, 2, true, MenuBounds{.37f,.17f,.53f,.43f}},
      {{.60f,.30f}, 3, true, MenuBounds{.52f,.17f,.67f,.43f}}};
  for (int i = 0; i < 4; ++i)
    CHECK_EQ(HitMenuRow(icons[i].center, {.08f,.17f,.67f,.43f}, icons), i);
  CHECK_EQ(HitMenuRow({.60f,.6f}, {0,0,1,1}, icons), -1);

  BeginCase("library rows include their full width but artist headings remain inert");
  std::vector<MenuRow> library{
      {{.34f,.31f}, 4, true, MenuBounds{.09f,.287f,.59f,.339f}},
      {{.34f,.376f}, 5, false, MenuBounds{.09f,.35f,.59f,.402f}},
      {{.34f,.439f}, 6, true, MenuBounds{.09f,.413f,.59f,.465f}}};
  CHECK_EQ(HitMenuRow({.55f,.439f}, {0,0,1,1}, library), 6);
  CHECK_EQ(HitMenuRow({.55f,.376f}, {0,0,1,1}, library), -1);
  CHECK_EQ(HitMenuRow({.55f,.407f}, {0,0,1,1}, library), -1);

  BeginCase("the mouse's unhover lifts only the highlight it owns (owner rule)");
  MouseHighlight h;
  h.Hover(0x10, 3);
  CHECK_FALSE(h.Lifted(0x10, 3));  // a hover draws the highlight
  CHECK_TRUE(h.PointerLeft(0x10, 3) == MouseHighlight::Leave::kLifted);
  CHECK_TRUE(h.Lifted(0x10, 3));   // ... and the list stays unlit
  CHECK_TRUE(h.Lifted(0x10, -1));  // scrolled out of the band: same selection, still unlit
  CHECK_TRUE(h.Lifted(0x10, 3));
  CHECK_FALSE(h.Lifted(0x10, 4));  // a controller moved it: the highlight returns
  CHECK_FALSE(h.Lifted(0x10, 4));
  CHECK_FALSE(h.Lifted(0x20, 3));  // another list is never affected
  h.Hover(0x10, 5);
  CHECK_TRUE(h.PointerLeft(0x10, 6) == MouseHighlight::Leave::kKept);  // moved before the leave
  CHECK_FALSE(h.Lifted(0x10, 6));
  h.Hover(0x10, 2);
  CHECK_TRUE(h.PointerLeft(0x10, 2) == MouseHighlight::Leave::kLifted);
  CHECK_TRUE(h.Lifted(0x10, 2));
  h.Hover(0x10, 2);                // landing on the row again owns it and lifts nothing
  CHECK_FALSE(h.Lifted(0x10, 2));
  CHECK_TRUE(h.PointerLeft(0x99, 2) == MouseHighlight::Leave::kIgnored);
  h.Hover(0x10, 2);
  h.Reset();
  CHECK_TRUE(h.PointerLeft(0x10, 2) == MouseHighlight::Leave::kIgnored);
  CHECK_FALSE(h.Lifted(0x10, 2));

  BeginCase("a move outside the game rectangle leaves every row");
  MenuMouseQueue outside;
  outside.Context(1, 2, true);
  outside.Push(MouseAction::kMove, {.5f, .5f});
  outside.Push(MouseAction::kMove, {NAN, NAN});  // what the window thread sends for outside
  auto miss = outside.NextFrame();
  CHECK_TRUE(miss && miss->action == MouseAction::kMove);
  CHECK_EQ(HitMenuRow(miss->point, {0,0,1,1}, library), -1);

  BeginCase("a scroll the mouse applied re-catches the hover under the pointer");
  MouseScrollFollow follow;
  CHECK_FALSE(follow.Take().has_value());  // no scroll waiting: nothing lands
  follow.Arm({.4f, .6f});
  auto land = follow.Take();
  CHECK_TRUE(land && land->x == .4f && land->y == .6f);
  CHECK_FALSE(follow.Take().has_value());  // ... exactly once per applied scroll
  follow.Arm({.4f, .6f});
  follow.Pointer({.5f, .7f});              // a later pointer event re-targets the catch
  land = follow.Take();
  CHECK_TRUE(land && land->x == .5f && land->y == .7f);
  follow.Pointer({.5f, .7f});              // an event without a scroll arms nothing
  CHECK_FALSE(follow.Take().has_value());
  follow.Arm({.1f, .2f});
  follow.Reset();                          // a screen or focus change discards it
  CHECK_FALSE(follow.Take().has_value());

  BeginCase("wheel detents carry the pointer and the burst drains before the catch");
  MenuMouseQueue wheel;
  wheel.Context(1, 2, true);
  wheel.Wheel(120, {.5f, .6f});
  auto detent = wheel.NextFrame();
  CHECK_TRUE(detent && detent->action == MouseAction::kUp && detent->point.x == .5f && detent->point.y == .6f);
  CHECK_FALSE(wheel.Pending());
  wheel.Wheel(-240, {.2f, .3f});
  CHECK_TRUE(wheel.Pending());             // both detents are still queued
  detent = wheel.NextFrame();
  CHECK_TRUE(detent && detent->action == MouseAction::kDown && detent->point.y == .3f);
  CHECK_TRUE(wheel.Pending());
  CHECK_TRUE(wheel.NextFrame()->action == MouseAction::kDown);
  CHECK_FALSE(wheel.Pending());
  wheel.Wheel(360, {.5f, .5f});
  wheel.Reset();
  CHECK_FALSE(wheel.Pending());            // discarded input is not followed either

  BeginCase("move coalescing preserves click ordering and never synthesizes directions");
  MenuMouseQueue q;
  q.Context(1, 2, true);
  q.Push(MouseAction::kMove, {.1f,.1f});
  q.Push(MouseAction::kMove, {.5f,.8f});
  q.Push(MouseAction::kAccept, {.5f,.8f});
  q.Push(MouseAction::kMove, {.5f,.2f});
  auto e = q.NextFrame();
  CHECK_TRUE(e && e->point.y == .8f);
  q.Pulse(e->action);
  CHECK_EQ(q.pulse(), 0);
  e = q.NextFrame();
  CHECK_TRUE(e && e->action == MouseAction::kAccept && e->point.y == .8f);
  q.Pulse(e->action);
  for (int i = 0; i < 100; ++i) CHECK_EQ(q.pulse(), 0x1000);
  CHECK_EQ(q.SamplePad(), 0x1000);
  CHECK_FALSE(q.NextFrame());
  CHECK_EQ(q.pulse(), 0x1000);
  CHECK_FALSE(q.NextFrame());
  CHECK_EQ(q.pulse(), 0);
  CHECK_EQ(q.SamplePad(), 0);
  CHECK_TRUE(q.NextFrame()->action == MouseAction::kMove);

  BeginCase("fractional wheel, fast detents, and reversal");
  q.Wheel(60, {.5f, .5f}); CHECK_FALSE(q.NextFrame());
  q.Wheel(60, {.5f, .5f}); CHECK_TRUE(q.NextFrame()->action == MouseAction::kUp);
  q.Wheel(-360, {.5f, .5f});
  for (int i = 0; i < 3; ++i) CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);
  CHECK_FALSE(q.NextFrame());
  q.Wheel(60, {.5f, .5f}); q.Wheel(-120, {.5f, .5f});
  CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);

  BeginCase("scroll animations retain wheel detents without retaining button pulses");
  q.Wheel(-240, {.5f, .5f});
  CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);
  q.Pulse(MouseAction::kDown);
  CHECK_EQ(q.SamplePad(), 2);
  CHECK_FALSE(q.NextFrame(false));
  CHECK_FALSE(q.NextFrame(false)); CHECK_EQ(q.pulse(), 0);
  CHECK_EQ(q.SamplePad(), 0);
  CHECK_FALSE(q.NextFrame(false));
  CHECK_TRUE(q.NextFrame(true)->action == MouseAction::kDown);
  q.Push(MouseAction::kMove, {.5f,.5f});
  CHECK_TRUE(q.NextFrame(false)->action == MouseAction::kMove);

  BeginCase("horizontal powerup wheel pulses release before the next direction");
  q.Pulse(MouseAction::kRight); CHECK_EQ(q.pulse(), 8);
  CHECK_EQ(q.SamplePad(), 8);
  CHECK_FALSE(q.NextFrame()); CHECK_FALSE(q.NextFrame()); CHECK_EQ(q.pulse(), 0);
  q.Pulse(MouseAction::kLeft); CHECK_EQ(q.pulse(), 4);
  q.Reset();

  BeginCase("a delayed input thread cannot lose a click or its release");
  MenuMouseQueue delayed;
  delayed.Context(1, 2, true);
  delayed.Push(MouseAction::kAccept);
  delayed.Pulse(delayed.NextFrame()->action);
  delayed.Push(MouseAction::kAccept);
  // UI frames and connectivity reads do not acknowledge delivery to player one.
  for (int i = 0; i < 60; ++i) {
    CHECK_FALSE(delayed.NextFrame());
    CHECK_EQ(delayed.pulse(), 0x1000);
  }
  CHECK_EQ(delayed.SamplePad(), 0x1000);
  CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.pulse(), 0);
  // A second click must wait until the input thread also sees the release.
  for (int i = 0; i < 60; ++i) CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.SamplePad(), 0);
  auto second = delayed.NextFrame();
  CHECK_TRUE(second && second->action == MouseAction::kAccept);

  BeginCase("many pad polls cannot shorten a press or skip the released frame");
  delayed.Pulse(MouseAction::kAccept);
  for (int i = 0; i < 20; ++i) CHECK_EQ(delayed.SamplePad(), 0x1000);
  CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.SamplePad(), 0x1000);
  delayed.Push(MouseAction::kBack);
  CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.SamplePad(), 0);
  CHECK_TRUE(delayed.NextFrame()->action == MouseAction::kBack);

  BeginCase("new submenu clicks wait for the previous category click to release");
  delayed.Pulse(MouseAction::kAccept);
  CHECK_EQ(delayed.SamplePad(), 0x1000);
  delayed.Context(1, 3, true);
  delayed.Push(MouseAction::kAccept);
  CHECK_EQ(delayed.pulse(), 0);
  CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.SamplePad(), 0);
  CHECK_TRUE(delayed.NextFrame()->action == MouseAction::kAccept);

  BeginCase("gameplay, focus loss and transitions discard pulses and pending input");
  for (int boundary = 0; boundary < 3; ++boundary) {
    q.Context(1, 2, true);
    q.Pulse(MouseAction::kBack); q.Wheel(480, {.5f, .5f}); q.Push(MouseAction::kAccept);
    q.Context(boundary == 0 ? 9 : 1, boundary == 1 ? 9 : 2, boundary != 2);
    CHECK_EQ(q.pulse(), 0); CHECK_FALSE(q.NextFrame());
  }
  q.Push(MouseAction::kAccept); q.Wheel(-120, {.5f, .5f});
  q.Context(1, 2, true);
  CHECK_FALSE(q.NextFrame());

  BeginCase("scrollbar drags coalesce and never lose a press or release");
  MenuMouseQueue rail;
  rail.Context(1, 2, true);
  rail.Scrollbar(MouseAction::kScrollbarDown, {.6f, .3f});
  rail.Scrollbar(MouseAction::kScrollbarDrag, {.6f, .31f});
  rail.Scrollbar(MouseAction::kScrollbarDrag, {.6f, .35f});
  rail.Scrollbar(MouseAction::kScrollbarUp, {.6f, .35f});
  auto drag = rail.NextFrame();
  CHECK_TRUE(drag && drag->action == MouseAction::kScrollbarDown && drag->point.y == .3f);
  drag = rail.NextFrame();
  CHECK_TRUE(drag && drag->action == MouseAction::kScrollbarDrag && drag->point.y == .35f);
  drag = rail.NextFrame();
  CHECK_TRUE(drag && drag->action == MouseAction::kScrollbarUp);
  CHECK_FALSE(rail.NextFrame());

  BeginCase("scrollbar traffic flows while a button pulse waits for its release");
  rail.Push(MouseAction::kAccept, {.5f, .5f});
  rail.Pulse(rail.NextFrame()->action);
  rail.Scrollbar(MouseAction::kScrollbarDrag, {.6f, .2f});
  CHECK_TRUE(rail.NextFrame()->action == MouseAction::kScrollbarDrag);
  CHECK_EQ(rail.pulse(), 0x1000);
  rail.SamplePad();
  CHECK_FALSE(rail.NextFrame());
  rail.Reset();
  CHECK_FALSE(rail.NextFrame());

  BeginCase("rail thumb math walks firstShowing across the measured rail");
  std::vector<MenuRow> rail_rows{
      {{.3f, .2f}, 0, true, MenuBounds{.2f, .15f, .4f, .25f}},
      {{.3f, .3f}, 1, false, MenuBounds{.2f, .25f, .4f, .35f}},
      {{.3f, .4f}, 2, true, MenuBounds{.2f, .35f, .4f, .45f}},
      {{.3f, .5f}, 3, true, MenuBounds{.2f, .45f, .4f, .55f}}};
  const float pitch = RailPitch(rail_rows);
  CHECK_TRUE(std::abs(pitch - .1f) < .001f);
  CHECK_TRUE(RailPitch({{{.5f, .5f}, 0, true}}) == 0);
  // The library's rail: top .2175, bottom .8348, one-row-pitch thumb, 41 scrollable rows.
  const float per = RailPerFirst(.8348f - .2175f, .0624f, 41);
  CHECK_TRUE(std::abs(per - (.8348f - .2175f - .0624f) / 41) < 1e-7f);
  CHECK_EQ(RailPerFirst(.6f, .06f, 0), 0);
  CHECK_EQ(RailPerFirst(.05f, .06f, 41), 0);
  CHECK_EQ(RailFirstFromY(.2175f, .2175f, per, 41), 0);
  CHECK_EQ(RailFirstFromY(.8348f, .2175f, per, 41), 41);
  CHECK_EQ(RailFirstFromY(.95f, .2175f, per, 41), 41);
  CHECK_EQ(RailFirstFromY(.05f, .2175f, per, 41), 0);
  CHECK_EQ(RailFirstFromY(.5f, .2175f, per, 41), int(std::lround((.5f - .2175f) / per)));
  CHECK_EQ(RailFirstFromY(.5f, .2175f, 0, 41), 0);
  CHECK_TRUE(RailYOnThumb(.2175f + per * 10 + .01f, .2175f, per, .0624f, 10));
  CHECK_FALSE(RailYOnThumb(.2175f + per * 10 + .09f, .2175f, per, .0624f, 10));
  CHECK_TRUE(RailYOnThumb(.5f, .2175f, per, .4f, 0));

  BeginCase("thumb grabs preserve their offset and track presses jump immediately");
  const MenuRail measured{.6f, .2f, .62f, .8f, true, .1f};
  MenuRailDrag grab;
  // At first=20/100 the thumb covers .30..40. Grab near its bottom.
  CHECK_EQ(grab.Begin(0x1234, .39f, measured, 20, 100), 20);
  CHECK_TRUE(grab.active && grab.list == 0x1234);
  CHECK_EQ(grab.Target(.39f, measured, 100), 20);
  CHECK_EQ(grab.Target(.49f, measured, 100), 40);
  CHECK_EQ(grab.Target(-.5f, measured, 100), 0);
  CHECK_EQ(grab.Target(1.5f, measured, 100), 100);
  CHECK_EQ(grab.Target(.39f, measured, 100), 20); // dragging back from a clamped endpoint
  // A track press takes effect on Begin, without needing motion or release.
  CHECK_EQ(grab.Begin(0x1234, .65f, measured, 20, 100), 80);
  CHECK_EQ(grab.Target(.70f, measured, 100), 90);
  CHECK_EQ(grab.Begin(0x1234, .2f, measured, 20, 100), 0);
  CHECK_EQ(grab.Begin(0x1234, .8f, measured, 20, 100), 100);
  // Different scene position, thumb size, and row count use the same interaction.
  const MenuRail store_rail{.4f, .1f, .42f, .9f, true, .2f};
  CHECK_EQ(grab.Begin(7, .8f, store_rail, 0, 300), 300);
  CHECK_EQ(grab.Begin(7, .5f, store_rail, 0, 300), 150);
  CHECK_EQ(grab.Target(.2f, store_rail, 300), 0);
  grab.Reset(); CHECK_FALSE(grab.active); CHECK_EQ(grab.list, 0);
  CHECK_EQ(grab.Begin(7, .5f, store_rail, 0, 0), 0); CHECK_FALSE(grab.active);
  CHECK_EQ(grab.Begin(7, NAN, store_rail, 0, 300), 0); CHECK_FALSE(grab.active);
  CHECK_EQ(grab.Begin(7, .5f, {}, 0, 300), 0); CHECK_FALSE(grab.active);
  CHECK_EQ(RailFirstFromY(NAN, .2f, .01f, 100), 0);
  CHECK_EQ(RailFirstFromY(1e30f, .2f, .01f, 100), 100);

  BeginCase("final pad packets expose mouse edges beside an idle controller");
  PadPacket packets;
  PadSample pad;
  // Upstream merging can keep returning the same large physical-device packet
  // while the mouse's smaller packet advances. Only final state matters here.
  auto packet = packets.Observe(pad);
  CHECK_EQ(packets.Observe(pad), packet);
  pad.buttons = 0x1000;
  CHECK_EQ(packets.Observe(pad), ++packet);
  for (int i = 0; i < 20; ++i) CHECK_EQ(packets.Observe(pad), packet);
  pad.buttons = 0;
  CHECK_EQ(packets.Observe(pad), ++packet);
  pad.buttons = 0x1000;
  CHECK_EQ(packets.Observe(pad), ++packet);
  pad.buttons = 0x2000;
  CHECK_EQ(packets.Observe(pad), ++packet);

  BeginCase("packets cover every analog axis and trigger as well as buttons");
  pad.left_trigger = 1; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.right_trigger = 1; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.thumb_lx = -32767; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.thumb_ly = 32767; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.thumb_rx = -123; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.thumb_ry = 456; CHECK_EQ(packets.Observe(pad), ++packet);
  CHECK_EQ(packets.Observe(pad), packet);
  PadPacket player_two;
  CHECK_EQ(player_two.Observe({}), 1);
  CHECK_EQ(packets.Observe(pad), packet);
  BeginCase("settings wheels use six bounded pointer positions");
  const MenuBounds slider_bounds{.284f, .86f, .716f, .97f};
  CHECK_EQ(SliderStepAt({.284f, .91f}, slider_bounds), 0);
  CHECK_EQ(SliderStepAt({.716f, .91f}, slider_bounds), 5);
  CHECK_EQ(SliderStepAt({.5f, .91f}, slider_bounds), 3);
  CHECK_EQ(SliderStepAt({.5f, .5f}, slider_bounds), -1);
  CHECK_EQ(SliderStepAt({NAN, .91f}, slider_bounds), -1);
  BeginCase("small wheels accept touch padding and retain a drag beyond their visual bounds");
  const MenuBounds thin{.3f, .49f, .7f, .51f};
  const auto padded = SliderTouchBounds(thin, 800);
  CHECK_TRUE(std::abs((padded.bottom - padded.top) * 800 - 44) < .001f);
  CHECK_TRUE(padded.left == thin.left);
  CHECK_TRUE(padded.right == thin.right);
  CHECK_EQ(SliderStepAt({.3f, .475f}, padded), 0);
  CHECK_EQ(SliderStepAt({.7f, .525f}, padded), 5);
  CHECK_EQ(SliderDragStepAt({.1f, .2f}, padded), 0);
  CHECK_EQ(SliderDragStepAt({.9f, .8f}, padded), 5);
  CHECK_EQ(SliderDragStepAt({.5f, .8f}, padded), 3);
  CHECK_EQ(SliderDragStepAt({NAN, .8f}, padded), -1);
  CHECK_TRUE(SliderTouchBounds(thin, 0).top == thin.top);
  CHECK_TRUE(SliderTouchBounds(slider_bounds, 800).top == slider_bounds.top);
  CHECK_TRUE(IsSettingsWheel("sound_effects_slider.sld"));
  CHECK_TRUE(IsSettingsWheel("track_sound_slider.sld"));
  CHECK_FALSE(IsSettingsWheel("options.lst"));
  BeginCase("profile aspect changes letterbox within the same ultrawide window");
  const auto native_frame = MenuPresentedRect(2560, 1080, 2560, 1080, true, false, 90, 90);
  const auto hd_frame = MenuPresentedRect(2560, 1080, 1280, 720, true, false, 90, 90);
  CHECK_EQ(native_frame.width, 2560);
  CHECK_EQ(native_frame.height, 1080);
  CHECK_EQ(hd_frame.width, 1920);
  CHECK_EQ(hd_frame.height, 1080);
  // The 320-pixel side bars are outside the guest's pointer space.
  CHECK_TRUE(MenuPointer(319, 540, 2560, 1080, 1280, 720, true, false, 90, 90).x < 0);
  CHECK_TRUE(MenuPointer(2241, 540, 2560, 1080, 1280, 720, true, false, 90, 90).x > 1);
  CHECK_EQ(MenuPointer(1280, 540, 2560, 1080, 1280, 720, true, false, 90, 90).x, .5f);
  return Finish();
}
