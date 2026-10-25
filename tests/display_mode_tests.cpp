// SPDX-License-Identifier: GPL-2.0-only
// Production SDL key handling and desired window state, without opening a
// native window or navigating the game/UI.
#include "check.h"

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
  return rb_blitz::test::Finish();
}
