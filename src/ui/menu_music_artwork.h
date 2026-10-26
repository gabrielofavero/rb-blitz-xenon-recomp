// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include "hooks/menu_music_now_playing.h"

namespace rb_blitz::ui::music {
struct ArtTag {
  uint64_t epoch = 0;
  size_t song = 0;
  std::string key;
  bool operator==(const ArtTag&) const = default;
};
struct Cover {
  ArtTag tag;
  shell_music::ArtworkState state = shell_music::ArtworkState::kError;
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;
};
// Resolve against exact package entries, never another song's basename.
std::string ResolveArtwork(std::string_view stream,
                           const std::vector<std::string>& assets);
std::shared_ptr<Cover> DecodeCover(ArtTag tag,
                                   const std::vector<uint8_t>& bytes);
enum class ArtRead { kReady, kDeferred, kMissing };
class Artwork {
 public:
  using Reader = std::function<ArtRead(const ArtTag&, std::vector<uint8_t>*)>;
  explicit Artwork(Reader reader);
  ~Artwork();
  // One running + one latest waiting request, eight 256-square RGBA cache
  // entries (2 MiB maximum). Null clears work and pixels on visibility/session
  // boundaries.
  std::shared_ptr<const Cover> Request(std::optional<ArtTag> tag);

 private:
  void Run();
  Reader reader_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stop_ = false;
  uint64_t generation_ = 0;
  std::optional<ArtTag> wanted_, queued_, running_;
  std::list<std::shared_ptr<Cover>> cache_;
  std::thread worker_;
};
}  // namespace rb_blitz::ui::music
