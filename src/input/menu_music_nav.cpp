// SPDX-License-Identifier: GPL-2.0-only
#include "input/menu_music_nav.h"

#include <limits>
#include "hooks/menu_music_now_playing.h"

namespace rb_blitz::input::menu_music_nav {

std::optional<Press> Latch::Update(Input input, bool enabled) {
  const Input before = previous_;
  previous_ = input;
  if (!enabled) return {};
  const uint16_t pressed = input.buttons & ~before.buttons;
  if (pressed & kNextButton) return Press{Command::kNext, Producer::kPad};
  if (input.media_next && !before.media_next) return Press{Command::kNext, Producer::kMediaKey};
  if (pressed & kPreviousButton) return Press{Command::kPrevious, Producer::kPad};
  if (input.media_previous && !before.media_previous)
    return Press{Command::kPrevious, Producer::kMediaKey};
  if (pressed & kPlayPauseButton) return Press{Command::kPlayPause, Producer::kPad};
  if (input.media_play_pause && !before.media_play_pause)
    return Press{Command::kPlayPause, Producer::kMediaKey};
  return {};
}

Previous PreviousAction(uint32_t position_samples, size_t cursor) {
  return cursor == 0 || cursor == std::numeric_limits<size_t>::max() ||
                 position_samples >= kRestartSamples
             ? Previous::kRestart : Previous::kStepBack;
}

PostResult Post(Command command, Producer producer, uint64_t epoch) {
  return shell_music::NowPlaying().Post(command, producer, epoch);
}
std::optional<Request> Take() { return shell_music::NowPlaying().Take(); }
void SetAvailable(bool available) { shell_music::NowPlaying().SetAvailable(available); }
bool Available() { return shell_music::NowPlaying().ReadSnapshot().available; }

}  // namespace rb_blitz::input::menu_music_nav
