// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "hooks/menu_music_session.h"

namespace rb_blitz::shell_music {
// The VFS owns devices. This index controls residency; the owner stops new opens
// to cold sources before retirement, and existing Files hold the device in place.
template <typename Device>
class MusicMountCache {
 public:
  bool Contains(size_t package) const { return devices_.contains(package); }
  size_t Size() const { return devices_.size(); }
  bool Full() const { return Size() >= MusicSession::kResidentLimit; }
  bool Insert(size_t package, Device* device) {
    return device && !Full() && devices_.emplace(package, device).second;
  }
  template <typename Retire>
  size_t RetireCold(const std::set<size_t>& warm, Retire retire) {
    size_t count = 0;
    for (auto it = devices_.begin(); it != devices_.end();) {
      if (!warm.contains(it->first) && it->second->open_file_count() == 0 && retire(it->first)) {
        it = devices_.erase(it);
        ++count;
      } else ++it;
    }
    return count;
  }
 private:
  std::map<size_t, Device*> devices_;
};
}  // namespace rb_blitz::shell_music
