// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace rb_blitz::shell_music {

// The observation of a one-shot guest stream. A filename choice alone is not playback.
// EOF is authoritative; rewinds are a defensive escape if a source unexpectedly loops.
class Playback {
 public:
  enum class Event { kNone, kAudible, kEnded, kFailed };
  static constexpr int64_t kProgressDeadlineMs = 3000;

  void Begin(int64_t now) {
    active_ = true;
    audible_ = false;
    stream_ = position_ = 0;
    progress_at_ = now;
  }
  void Stop() { active_ = audible_ = false; }

  Event Observe(int64_t now, uint32_t stream, bool playing, bool finished, uint32_t position) {
    if (!active_) return Event::kNone;
    if (stream && finished) return Finish(Event::kEnded);
    // A source replaced outside our handoff must not inherit the old song's identity.
    if (stream_ && stream && stream != stream_) return Finish(Event::kFailed);
    if (stream) stream_ = stream;
    if (stream && playing && position > 0 && position != position_) {
      if (position < position_) return Finish(Event::kEnded);
      position_ = position;
      progress_at_ = now;
      if (!audible_) {
        audible_ = true;
        return Event::kAudible;
      }
    }
    // Includes a missing stream, a load that never becomes ready, and a stalled decoder.
    if (now - progress_at_ >= kProgressDeadlineMs) return Finish(Event::kFailed);
    return Event::kNone;
  }
  uint32_t position() const { return position_; }

 private:
  Event Finish(Event event) {
    Stop();
    return event;
  }
  bool active_ = false;
  bool audible_ = false;
  uint32_t stream_ = 0;
  uint32_t position_ = 0;
  int64_t progress_at_ = 0;
};

}  // namespace rb_blitz::shell_music
