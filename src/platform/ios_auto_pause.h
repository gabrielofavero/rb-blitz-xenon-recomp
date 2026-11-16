// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::platform {
// Protected by ios_runtime's activation mutex. Only the guest script changes
// song ownership; the UIKit thread never calls into the guest.
class IOSAutoPause {
 public:
  void SetActive(bool active) {
    if (active_ && !active && in_song_) pending_ = true;
    active_ = active;
  }
  void SetInSong(bool in_song) {
    in_song_ = in_song;
    pending_ = in_song && !active_;
  }
  bool Consume() {
    if (!active_ || !pending_) return false;
    pending_ = false;
    return true;
  }

 private:
  bool active_ = true;
  bool in_song_ = false;
  bool pending_ = false;
};
}  // namespace rb_blitz::platform
