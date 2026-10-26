// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <string>

#include "input/menu_music_nav.h"

namespace rb_blitz::shell_music {

namespace nav = input::menu_music_nav;
enum class MusicSource { kNone, kDlc, kTitle };
enum class TransportState { kUnavailable, kLoading, kPlaying, kPaused, kStopped };
enum class ArtworkState { kMissing, kLoading, kReady, kError };
struct Capabilities {
  bool next = false, previous = false, play_pause = false;
  bool Supports(nav::Command command) const;
};
// All strings are owned; catalog IDs and art keys outlive mounted device aliases.
struct NowPlayingTrack {
  MusicSource source = MusicSource::kNone;
  size_t song = std::numeric_limits<size_t>::max();
  std::string key, title, artist, artwork_key;
};
struct PlaybackUpdate {
  NowPlayingTrack track;
  TransportState state = TransportState::kUnavailable;
  Capabilities capabilities;
};
struct CommandResult {
  nav::Request request;
  nav::CompletionStatus status = nav::CompletionStatus::kCancelled;
  std::string explanation;
};
struct NowPlayingSnapshot {
  uint64_t epoch = 1, revision = 0;
  bool available = false;
  PlaybackUpdate playback;
  ArtworkState artwork = ArtworkState::kMissing;
  std::optional<nav::Request> pending;
  std::deque<nav::Request> queued;
  std::optional<CommandResult> result;
};

// One lock covers playback facts, the active command and its bounded follow-ups.
// Acceptance remains separate from completion; no callback runs under the lock.
class NowPlayingBridge {
 public:
  NowPlayingSnapshot ReadSnapshot() const;
  bool Publish(uint64_t epoch, PlaybackUpdate update, std::optional<bool> available = {});
  bool PublishArtwork(uint64_t epoch, size_t song, const std::string& key, ArtworkState state);
  // Strict callers retain the one-slot busy answer; production navigation opts
  // into four waiting requests so rapid presses survive decoder/package loads.
  static constexpr size_t kQueuedLimit = 4;
  nav::PostResult Post(nav::Command command, nav::Producer producer, uint64_t epoch = 0,
                      bool queue_if_busy = false);
  std::optional<nav::Request> Take();
  bool Complete(const nav::Request& request, nav::CompletionStatus status,
                std::string explanation = {});
  void SetAvailable(bool available);
  // Invalidates every request and worker result from the previous session.
  uint64_t Reset();
 private:
  void CancelLocked();
  mutable std::mutex mutex_;
  NowPlayingSnapshot snapshot_;
  uint64_t next_request_ = 1;
  bool taken_ = false;
};

NowPlayingBridge& NowPlaying();

}  // namespace rb_blitz::shell_music
