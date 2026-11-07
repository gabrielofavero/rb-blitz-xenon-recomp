// SPDX-License-Identifier: GPL-2.0-only
// Production SDL key handling and desired window state, without opening a
// native window or navigating the game/UI.
#include "check.h"
#include "settings/display_runtime.h"

#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <thread>
#include <rex/ui/window_sdl.h>

namespace {
class Context final : public rex::ui::WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};

void Key(rex::ui::WindowSDL& window, SDL_EventType type, SDL_Scancode key,
         SDL_Keymod modifiers = SDL_KMOD_NONE, bool repeat = false) {
  SDL_Event event{};
  event.type = type;
  event.key.scancode = key;
  event.key.mod = modifiers;
  event.key.repeat = repeat;
  window.HandleKeyEvent(event);
}
}  // namespace

int main() {
  Context context;
  rex::ui::WindowSDL window(context, "Display mode test", 1280, 720);
  CHECK_TRUE(!window.IsFullscreen() && !window.IsBorderless());
  Key(window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RETURN);
  CHECK_TRUE(!window.IsBorderless());

  Key(window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RETURN, SDL_KMOD_ALT);
  CHECK_TRUE(window.IsBorderless() && !window.IsFullscreen());
  Key(window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RETURN, SDL_KMOD_ALT, true);
  CHECK_TRUE(window.IsBorderless());
  // Alt may have been released before Enter. The release still ends the chord.
  Key(window, SDL_EVENT_KEY_UP, SDL_SCANCODE_RETURN);
  Key(window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RETURN, SDL_KMOD_ALT);
  CHECK_TRUE(!window.IsBorderless() && !window.IsFullscreen());
  Key(window, SDL_EVENT_KEY_UP, SDL_SCANCODE_RETURN);

  window.SetFullscreen(true);
  window.SetBorderless(true);
  CHECK_TRUE(window.IsBorderless() && !window.IsFullscreen());
  window.SetFullscreen(true);
  CHECK_TRUE(window.IsFullscreen() && !window.IsBorderless());
  Key(window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_KP_ENTER, SDL_KMOD_ALT);
  CHECK_TRUE(!window.IsFullscreen() && !window.IsBorderless());
  Key(window, SDL_EVENT_KEY_UP, SDL_SCANCODE_KP_ENTER);
  Key(window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_KP_ENTER, SDL_KMOD_ALT);
  CHECK_TRUE(window.IsBorderless() && !window.IsFullscreen());

  // Losing focus mid-chord must not swallow the next deliberate press.
  SDL_Event lost{};
  lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  window.HandleWindowEvent(lost);
  Key(window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_KP_ENTER, SDL_KMOD_ALT);
  CHECK_TRUE(!window.IsBorderless());
  CHECK_TRUE(window.GetDesiredLogicalWidth() == 1280);
  CHECK_TRUE(window.GetDesiredLogicalHeight() == 720);

  // The compound API stores exactly one desired mode before a native window
  // exists. The runtime caller dispatches it on the UI thread after Open().
  using Mode = rex::ui::Window::DisplayMode;
  for (Mode from : {Mode::kWindowed, Mode::kBorderless, Mode::kFullscreen}) {
    CHECK_TRUE(window.SetDisplayMode(from));
    for (Mode to : {Mode::kWindowed, Mode::kBorderless, Mode::kFullscreen}) {
      CHECK_TRUE(window.SetDisplayMode(to));
      CHECK_TRUE(window.GetDisplayMode() == to);
      CHECK_TRUE(window.IsFullscreen() == (to == Mode::kFullscreen));
      CHECK_TRUE(window.IsBorderless() == (to == Mode::kBorderless));
    }
  }

  const auto live_directory = std::filesystem::temp_directory_path() /
      ("rb-blitz-display-keep-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto live_file = live_directory / "settings.toml";
  std::string live_error;
  window.SetDisplayMode(Mode::kWindowed);
  {
    rb_blitz::settings::DisplayPreviewTransaction preview(context, window, live_file);
    CHECK_TRUE(preview.Begin("windowed", "borderless", &live_error));
    CHECK_TRUE(preview.active());
    int saves = 0;
    CHECK_TRUE(!preview.Keep(false, [&](std::string*) { ++saves; return true; }, &live_error));
    CHECK_TRUE(saves == 0 && preview.active());
    CHECK_TRUE(preview.Revert(&live_error));
    CHECK_TRUE(!preview.active() && window.GetDisplayMode() == Mode::kWindowed);
    CHECK_TRUE(preview.Begin("windowed", "borderless", &live_error));
    auto marker = live_file;
    marker += ".display-preview.toml";
    std::error_code marker_error;
    std::filesystem::remove(marker, marker_error);
    CHECK_TRUE(!marker_error);
    std::filesystem::create_directory(marker, marker_error);
    CHECK_TRUE(!marker_error);
    std::filesystem::create_directory(marker / "keep-marker-nonempty", marker_error);
    CHECK_TRUE(!marker_error);
    CHECK_TRUE(!preview.Revert(&live_error));
    CHECK_TRUE(preview.active());
    CHECK_TRUE(!preview.Revert(&live_error));
    CHECK_TRUE(preview.active());
    std::filesystem::remove_all(marker, marker_error);
    CHECK_TRUE(!marker_error);
    CHECK_TRUE(preview.Revert(&live_error));
    CHECK_TRUE(!preview.active());
    CHECK_TRUE(preview.Begin("windowed", "borderless", &live_error));
    CHECK_TRUE(!preview.Keep(true, [&](std::string* error) {
      ++saves;
      *error = "simulated save failure";
      return false;
    }, &live_error));
    CHECK_TRUE(saves == 1 && !preview.active());
    CHECK_TRUE(window.GetDisplayMode() == Mode::kWindowed);
    std::string failed_previous;
    bool failed_pending = true;
    CHECK_TRUE(rb_blitz::settings::ReadUnconfirmedDisplayPreview(
        live_file, &failed_previous, &failed_pending, &live_error));
    CHECK_TRUE(!failed_pending);
    CHECK_TRUE(preview.Begin("windowed", "fullscreen", &live_error));
    CHECK_TRUE(preview.Keep(true, [&](std::string*) { ++saves; return true; }, &live_error));
    CHECK_TRUE(saves == 2 && !preview.active());
  }
  std::string live_previous;
  bool live_pending = true;
  CHECK_TRUE(rb_blitz::settings::ReadUnconfirmedDisplayPreview(
      live_file, &live_previous, &live_pending, &live_error));
  CHECK_TRUE(!live_pending);
  std::error_code live_cleanup_error;
  std::filesystem::remove(live_directory, live_cleanup_error);

  // The timeout worker must roll back even without another guest frame. Pump
  // the fake UI queue as SDL would; no native window or game UI is opened.
  const auto timeout_directory = std::filesystem::temp_directory_path() /
      ("rb-blitz-display-timeout-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto timeout_file = timeout_directory / "settings.toml";
  window.SetDisplayMode(Mode::kWindowed);
  {
    rb_blitz::settings::DisplayPreviewTransaction preview(
        context, window, timeout_file, std::chrono::milliseconds(25));
    CHECK_TRUE(preview.Begin("windowed", "borderless", &live_error));
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (preview.active() && std::chrono::steady_clock::now() < limit) {
      context.ExecutePendingFunctionsFromUIThread();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK_TRUE(!preview.active() && window.GetDisplayMode() == Mode::kWindowed);
  }
  std::string timeout_previous;
  bool timeout_pending = true;
  CHECK_TRUE(rb_blitz::settings::ReadUnconfirmedDisplayPreview(
      timeout_file, &timeout_previous, &timeout_pending, &live_error));
  CHECK_TRUE(!timeout_pending);
  std::error_code timeout_cleanup_error;
  std::filesystem::remove(timeout_directory, timeout_cleanup_error);

  // A transition may fail after its recovery marker is durable. Even if a
  // native rollback also fails, the next boot must still see the old mode.
  Context stopped_context;
  rex::ui::WindowSDL stopped_window(stopped_context, "Stopped display", 1280, 720);
  stopped_context.QuitFromUIThread();
  const auto directory = std::filesystem::temp_directory_path() /
      ("rb-blitz-display-preview-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto settings_file = directory / "settings.toml";
  std::string error;
  {
    rb_blitz::settings::DisplayPreviewTransaction preview(
        stopped_context, stopped_window, settings_file);
    bool began = true;
    std::thread worker([&] { began = preview.Begin("windowed", "borderless", &error); });
    worker.join();
    CHECK_TRUE(!began);
    CHECK_TRUE(!preview.active());
  }
  std::string previous;
  bool pending = false;
  CHECK_TRUE(rb_blitz::settings::ReadUnconfirmedDisplayPreview(
      settings_file, &previous, &pending, &error));
  CHECK_TRUE(pending && previous == "windowed");
  CHECK_TRUE(rb_blitz::settings::ClearUnconfirmedDisplayPreview(settings_file, &error));
  CHECK_TRUE(rb_blitz::settings::ReadUnconfirmedDisplayPreview(
      settings_file, &previous, &pending, &error));
  CHECK_TRUE(!pending);
  std::error_code cleanup_error;
  std::filesystem::remove(directory, cleanup_error);
  return rb_blitz::test::Finish();
}
