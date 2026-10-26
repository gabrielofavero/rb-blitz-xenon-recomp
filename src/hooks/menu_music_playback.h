// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace rb_blitz::shell_music {

// The decoder cursor includes the native random start offset and decoded-ahead
// reserve. Navigation uses progress since the first playing observation instead
// of mistaking that initial cursor for time the listener has heard.
class PlaybackAge {
 public:
  void Reset() { stream_ = last_ = samples_ = 0; }
  void Observe(uint32_t stream, bool playing, uint32_t position) {
    if (!stream || !playing || !position) return;
    if (stream != stream_) {
      stream_ = stream;
      last_ = position;
      samples_ = 0;
      return;
    }
    if (position >= last_) {
      const uint32_t delta = position - last_;
      samples_ = UINT32_MAX - samples_ < delta ? UINT32_MAX : samples_ + delta;
    }
    last_ = position;
  }
  uint32_t samples() const { return samples_; }
 private:
  uint32_t stream_ = 0, last_ = 0, samples_ = 0;
};

// The observation of a one-shot guest stream. A filename choice alone is not playback.
// EOF is authoritative; rewinds are a defensive escape if a source unexpectedly loops.
class Playback {
 public:
  enum class Event { kNone, kAudible, kEnded, kFailed };
  static constexpr int64_t kProgressDeadlineMs = 3000;
  // Allow cold package opening and the larger menu-only decoded reserve.
  static constexpr int64_t kStartupDeadlineMs = 15000;

  void Begin(int64_t now) {
    active_ = true;
    audible_ = false;
    stream_ = position_ = 0;
    progress_at_ = started_at_ = now;
  }
  void Stop() { active_ = audible_ = false; }
  bool active() const { return active_; }

  // Native content/graphics loading can starve decoding and ShellMusic polls.
  // Keep the same stream/position/identity, but give it a fresh recovery window
  // after loading. EOF and stream replacement remain authoritative in Observe.
  void DeferWatchdog(int64_t now) { progress_at_ = started_at_ = now; }

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
    // Opening/decrypting a cold package can take longer than recovering a running decoder.
    // Keep both waits bounded without quarantining a healthy song after only three seconds.
    if (!audible_ && now - started_at_ >= kStartupDeadlineMs) return Finish(Event::kFailed);
    if (audible_ && now - progress_at_ >= kProgressDeadlineMs) return Finish(Event::kFailed);
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
  int64_t started_at_ = 0;
};

// A press captures one song for its entire probe sequence. During the short silent handoff,
// prefer the song the player just heard; at first startup allow the selected pending song.
// Title fallback and screen exit clear all candidates, so neither can select stale gameplay.
class PlaySelection {
 public:
  static constexpr int64_t kHandoffGraceMs = 2000;
  void Loading(int index, int64_t now) {
    if (current_ >= 0) {
      recent_ = current_;
      transition_at_ = now;
    }
    current_ = -1;
    pending_ = index;
  }
  // Manual navigation must not let the automatic handoff's old-song grace win.
  // An already captured press still names its original song until Complete().
  void Skipped(int index, int64_t now) {
    current_ = recent_ = -1;
    pending_ = index;
    transition_at_ = now;
  }
  void Failed(int index) {
    if (current_ == index) current_ = -1;
    if (pending_ == index) pending_ = -1;
    if (recent_ == index) recent_ = -1;
  }
  void Audible(int index) { current_ = pending_ = recent_ = index; }
  void Stop() { current_ = pending_ = recent_ = captured_ = -1; capturing_ = false; }
  int Choose(int64_t now) const {
    if (current_ >= 0) return current_;
    if (recent_ >= 0 && now - transition_at_ <= kHandoffGraceMs) return recent_;
    return pending_;
  }
  int Capture(int64_t now) {
    capturing_ = true;
    return captured_ = Choose(now);
  }
  int Query(int64_t now) const { return capturing_ ? captured_ : Choose(now); }
  void Complete() { capturing_ = false; captured_ = -1; }

 private:
  int current_ = -1, pending_ = -1, recent_ = -1, captured_ = -1;
  int64_t transition_at_ = 0;
  bool capturing_ = false;
};

}  // namespace rb_blitz::shell_music
