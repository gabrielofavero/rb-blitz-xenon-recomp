// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace rb_blitz::input::menu_music_nav {

enum class Command { kNext, kPrevious, kPlayPause };
enum class Producer { kPad, kMediaKey, kControlsPage, kPlayer };
enum class PostStatus { kAccepted, kBusy, kUnavailable, kStaleSession };
enum class CompletionStatus { kApplied, kFailed, kCancelled };
struct Request {
  uint64_t id = 0;
  uint64_t epoch = 0;
  Command command = Command::kNext;
  Producer producer = Producer::kPad;
  // A Previous pressed during another navigation belongs to the new selection's
  // loading interval, even if execution waits behind slow package/decoder work.
  bool previous_step_back = false;
};
struct PostResult {
  PostStatus status = PostStatus::kUnavailable;
  uint64_t id = 0;  // nonzero only for an accepted request
};

inline constexpr uint16_t kPreviousButton = 0x0100;
inline constexpr uint16_t kNextButton = 0x0200;
inline constexpr uint16_t kPlayPauseButton = 0x0010;
inline constexpr uint32_t kRestartSamples = 5 * 44100;
inline constexpr const char* kNextKey = "MediaTrackNext";
inline constexpr const char* kPreviousKey = "MediaTrackPrevious";
inline constexpr const char* kPlayPauseKey = "MediaPlayPause";

struct Input {
  uint16_t buttons = 0;
  bool media_next = false;
  bool media_previous = false;
  bool media_play_pause = false;
};
struct Press {
  Command command;
  Producer producer;
};

// Sample even when disabled: entering a menu with a held control must not post.
// Simultaneous edges have a fixed priority: Next, Previous, Play/Pause; pad before key.
class Latch {
 public:
  std::optional<Press> Update(Input input, bool enabled);
  void Reset(Input held = {}) { previous_ = held; }
 private:
  Input previous_{};
};

enum class Previous { kRestart, kStepBack };
Previous PreviousAction(uint32_t position_samples, size_t cursor);

// SDK-free shared bridge, initially unavailable. P2 supplies production gates and
// executes Take() on the guest owner; posting never calls guest code.
PostResult Post(Command command, Producer producer = Producer::kPad, uint64_t epoch = 0);
std::optional<Request> Take();
void SetAvailable(bool available);
bool Available();

}  // namespace rb_blitz::input::menu_music_nav
