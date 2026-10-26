// SPDX-License-Identifier: GPL-2.0-only
#include "ui/menu_music_artwork.h"

#include <algorithm>
#include <chrono>


#include "assets/texture_codec.h"

namespace rb_blitz::ui::music {
namespace {
std::string AssetIdentity(std::string path) {
  for (char& c : path) {
    if (c == '\\') c = '/';
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  }
  while (path.starts_with('/')) path.erase(0, 1);
  return path;
}
}  // namespace
std::string ResolveArtwork(std::string_view stream,
                           const std::vector<std::string>& assets) {
  const std::string identity = AssetIdentity(std::string(stream));
  const auto slash = identity.find_last_of('/');
  if (slash == std::string::npos) return {};
  const std::string exact = identity.substr(0, slash + 1) + "gen/" +
                            identity.substr(slash + 1) + "_keep.png_xbox";
  for (const auto& asset : assets)
    if (AssetIdentity(asset) == exact) return asset;
  return {};
}
std::shared_ptr<Cover> DecodeCover(ArtTag tag,
                                   const std::vector<uint8_t>& bytes) {
  auto result = std::make_shared<Cover>();
  result->tag = std::move(tag);
  assets::TextureEnvelope envelope;
  std::string why;
  // Check dimensions before the decoder allocates. Bound encoded and decoded
  // work.
  if (bytes.size() > 2 * 1024 * 1024 ||
      !assets::ParseTextureEnvelope(bytes.data(), bytes.size(), &envelope,
                                    &why) ||
      envelope.width > 1024 || envelope.height > 1024)
    return result;
  std::vector<uint8_t> pixels;
  if (!assets::DecodePlatformTexture(bytes.data(), bytes.size(), true, &pixels,
                                     &envelope, &why))
    return result;
  result->width = std::min(256u, envelope.width);
  result->height = std::min(256u, envelope.height);
  result->rgba.resize(result->width * result->height * 4);
  for (uint32_t y = 0; y < result->height; ++y)
    for (uint32_t x = 0; x < result->width; ++x) {
      const size_t from =
          ((y * envelope.height / result->height) * envelope.width +
           x * envelope.width / result->width) *
          4;
      std::copy_n(pixels.data() + from, 4,
                  result->rgba.data() + (y * result->width + x) * 4);
    }
  result->state = shell_music::ArtworkState::kReady;
  return result;
}
Artwork::Artwork(Reader reader)
    : reader_(std::move(reader)), worker_([this] { Run(); }) {}
Artwork::~Artwork() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
    ++generation_;
    queued_.reset();
  }
  wake_.notify_one();
  worker_.join();
}
std::shared_ptr<const Cover> Artwork::Request(std::optional<ArtTag> tag) {
  std::lock_guard lock(mutex_);
  if (!tag || (wanted_ && tag->epoch != wanted_->epoch)) {
    ++generation_;
    cache_.clear();
    queued_.reset();
    wanted_.reset();
  }
  wanted_ = tag;
  if (!tag) return {};
  for (auto it = cache_.begin(); it != cache_.end(); ++it)
    if ((*it)->tag == *tag) {
      auto cover = *it;
      cache_.splice(cache_.begin(), cache_, it);
      return cover;
    }
  if (tag != running_ && tag != queued_) {
    queued_ = tag;
    wake_.notify_one();
  }
  return {};
}
void Artwork::Run() {
  for (;;) {
    ArtTag tag;
    uint64_t generation;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stop_ || queued_.has_value(); });
      if (stop_) return;
      tag = *queued_;
      queued_.reset();
      running_ = tag;
      generation = generation_;
    }
    std::vector<uint8_t> bytes;
    ArtRead read = ArtRead::kMissing;
    std::shared_ptr<Cover> cover;
    try {
      read = reader_(tag, &bytes);
      if (read == ArtRead::kReady) cover = DecodeCover(tag, bytes);
    } catch (...) {
      read = ArtRead::kMissing;
    }
    if (!cover && read != ArtRead::kDeferred) {
      cover = std::make_shared<Cover>();
      cover->tag = tag;
      cover->state = shell_music::ArtworkState::kError;
    }
    {
      std::unique_lock lock(mutex_);
      running_.reset();
      if (stop_) return;
      if (generation != generation_ || wanted_ != tag) continue;
      if (read == ArtRead::kDeferred) {
        wake_.wait_for(
            lock, std::chrono::milliseconds(250), [this, generation, &tag] {
              return stop_ || generation != generation_ || wanted_ != tag;
            });
        if (!stop_ && generation == generation_ && wanted_ == tag)
          queued_ = tag;
        continue;
      }
      cache_.push_front(std::move(cover));
      while (cache_.size() > 8) cache_.pop_back();
    }
  }
}
}  // namespace rb_blitz::ui::music
