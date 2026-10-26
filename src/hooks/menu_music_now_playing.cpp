// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/menu_music_now_playing.h"

#include <utility>

namespace rb_blitz::shell_music {

bool Capabilities::Supports(nav::Command command) const {
  switch (command) {
    case nav::Command::kNext: return next;
    case nav::Command::kPrevious: return previous;
    case nav::Command::kPlayPause: return play_pause;
  }
  return false;
}

NowPlayingSnapshot NowPlayingBridge::ReadSnapshot() const {
  std::lock_guard lock(mutex_);
  return snapshot_;
}

bool NowPlayingBridge::Publish(uint64_t epoch, PlaybackUpdate update, std::optional<bool> available) {
  std::lock_guard lock(mutex_);
  if (epoch != snapshot_.epoch) return false;
  const auto& old = snapshot_.playback.track;
  const auto& track = update.track;
  if (old.source != track.source || old.song != track.song || old.key != track.key ||
      old.artwork_key != track.artwork_key)
    snapshot_.artwork = track.artwork_key.empty() ? ArtworkState::kMissing : ArtworkState::kLoading;
  snapshot_.playback = std::move(update);
  if (available) {
    snapshot_.available = *available;
    if (!*available) CancelLocked();
  }
  ++snapshot_.revision;
  return true;
}

bool NowPlayingBridge::PublishArtwork(uint64_t epoch, size_t song, const std::string& key,
                                     ArtworkState state) {
  std::lock_guard lock(mutex_);
  if (epoch != snapshot_.epoch || song != snapshot_.playback.track.song || key.empty() ||
      key != snapshot_.playback.track.artwork_key) return false;
  snapshot_.artwork = state;
  ++snapshot_.revision;
  return true;
}

nav::PostResult NowPlayingBridge::Post(nav::Command command, nav::Producer producer, uint64_t epoch,
                                      bool queue_if_busy) {
  std::lock_guard lock(mutex_);
  if (epoch && epoch != snapshot_.epoch) return {nav::PostStatus::kStaleSession};
  if (!snapshot_.available || !snapshot_.playback.capabilities.Supports(command))
    return {nav::PostStatus::kUnavailable};
  if (snapshot_.pending) {
    if (!queue_if_busy || snapshot_.queued.size() >= kQueuedLimit)
      return {nav::PostStatus::kBusy};
    const nav::Request request{next_request_++, snapshot_.epoch, command, producer,
                               command == nav::Command::kPrevious};
    snapshot_.queued.push_back(request);
    ++snapshot_.revision;
    return {nav::PostStatus::kAccepted, request.id};
  }
  snapshot_.pending = nav::Request{next_request_++, snapshot_.epoch, command, producer,
      command == nav::Command::kPrevious && snapshot_.playback.state == TransportState::kLoading};
  taken_ = false;
  ++snapshot_.revision;
  return {nav::PostStatus::kAccepted, snapshot_.pending->id};
}

std::optional<nav::Request> NowPlayingBridge::Take() {
  std::lock_guard lock(mutex_);
  if (!snapshot_.pending || taken_) return {};
  if (!snapshot_.available ||
      !snapshot_.playback.capabilities.Supports(snapshot_.pending->command)) {
    CancelLocked();
    ++snapshot_.revision;
    return {};
  }
  taken_ = true;
  return snapshot_.pending;
}

bool NowPlayingBridge::Complete(const nav::Request& request, nav::CompletionStatus status,
                                std::string explanation) {
  std::lock_guard lock(mutex_);
  if (!taken_ || !snapshot_.pending || request.epoch != snapshot_.epoch ||
      request.id != snapshot_.pending->id) return false;
  snapshot_.result = CommandResult{*snapshot_.pending, status, std::move(explanation)};
  snapshot_.pending.reset();
  if (status != nav::CompletionStatus::kApplied) snapshot_.queued.clear();
  if (!snapshot_.queued.empty()) {
    snapshot_.pending = snapshot_.queued.front();
    snapshot_.queued.pop_front();
  }
  taken_ = false;
  ++snapshot_.revision;
  return true;
}

void NowPlayingBridge::CancelLocked() {
  if (snapshot_.pending)
    snapshot_.result = CommandResult{*snapshot_.pending, nav::CompletionStatus::kCancelled, {}};
  if (!snapshot_.queued.empty())
    snapshot_.result = CommandResult{snapshot_.queued.back(), nav::CompletionStatus::kCancelled, {}};
  snapshot_.pending.reset();
  snapshot_.queued.clear();
  taken_ = false;
}

void NowPlayingBridge::SetAvailable(bool available) {
  std::lock_guard lock(mutex_);
  if (available == snapshot_.available) return;
  snapshot_.available = available;
  if (!available) CancelLocked();
  ++snapshot_.revision;
}

uint64_t NowPlayingBridge::Reset() {
  std::lock_guard lock(mutex_);
  CancelLocked();
  const auto result = snapshot_.result;
  const auto epoch = snapshot_.epoch + 1;
  const auto revision = snapshot_.revision + 1;
  snapshot_ = {};
  snapshot_.epoch = epoch;
  snapshot_.revision = revision;
  snapshot_.result = result;
  return epoch;
}

NowPlayingBridge& NowPlaying() {
  static NowPlayingBridge bridge;
  return bridge;
}

}  // namespace rb_blitz::shell_music
