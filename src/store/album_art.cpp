// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/album_art.h"

#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace rb_blitz::store {
namespace {

std::mutex g_mutex;
std::map<std::string, std::string> g_urls;
std::map<std::string, std::string> g_textures;
ArtTextureRequest g_texture_request;
bool g_enabled = true;

// FNV-1a 64. Not a checksum anyone verifies - a short stable name - so the constants are the
// reference ones and nothing here needs to be collision-proof for a page of 25 offers.
std::uint64_t Fnv1a64(std::string_view text) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const char c : text) {
    hash ^= static_cast<std::uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string Hex16(std::uint64_t value) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[std::size_t(i)] = kDigits[value & 0xF];
    value >>= 4;
  }
  return out;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsHex16(std::string_view text) {
  if (text.size() != 16) return false;
  for (const char c : text) {
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    if (!digit && !lower) return false;
  }
  return true;
}

}  // namespace

std::string ArtKeyForSong(std::string_view file_id, std::string_view artist,
                          std::string_view title) {
  if (!file_id.empty()) {
    return Hex16(Fnv1a64(file_id));
  }
  // A record without an id can still be displayed; the pair is the same identity plan §3.2 uses
  // for the Downloaded mark, so two spellings of one song share a cover only when they are the
  // same song by that rule.
  if (artist.empty() && title.empty()) {
    return {};
  }
  return Hex16(Fnv1a64(std::string(artist) + "|" + std::string(title)));
}

std::string ArtPathForKey(std::string_view key) {
  if (!IsHex16(key)) {
    return {};
  }
  // The leading slash is the separator onto the panel's prefix (`dlc_store`), which has none.
  return "/offers/" + std::string(key) + "/" + kAlbumArtFileName;
}

std::string ArtKeyFromPath(std::string_view path) {
  const std::size_t name = path.rfind(kAlbumArtFileName);
  if (name == std::string_view::npos) {
    return {};
  }
  // The exact tail must be `/cover_art.png_xbox`.
  if (name == 0 || path[std::size_t(name) - 1] != '/') {
    return {};
  }
  const std::string_view before = path.substr(0, std::size_t(name) - 1);
  const std::size_t slash = before.rfind('/');
  const std::string_view key =
      slash == std::string_view::npos ? before : before.substr(slash + 1);
  if (!IsHex16(key)) {
    return {};
  }
  // Only the `offers/<key>/` spelling is ours: a retail path that happens to end in the same file
  // name (none does) would otherwise be answered from our cache. The parent segment is the folder
  // the composed path put the key in, whatever prefix came before it.
  const std::string_view parent =
      slash == std::string_view::npos ? std::string_view() : before.substr(0, slash);
  if (parent != "offers" && !EndsWith(parent, "/offers")) {
    return {};
  }
  return std::string(key);
}

bool IsArtRequestPath(std::string_view path) { return !ArtKeyFromPath(path).empty(); }

void SetAlbumArtEnabled(bool enabled) { g_enabled = enabled; }

bool AlbumArtEnabled() { return g_enabled; }

void PublishArtUrl(std::string key, std::string url) {
  if (key.empty() || url.empty()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_urls[std::move(key)] = std::move(url);
}

std::string ArtUrlForKey(std::string_view key) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto it = g_urls.find(std::string(key));
  return it == g_urls.end() ? std::string() : it->second;
}

void PublishArtTexture(std::string key, std::string bytes) {
  if (key.empty() || bytes.empty()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_textures[std::move(key)] = std::move(bytes);
}

std::string ArtTextureForKey(std::string_view key) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto it = g_textures.find(std::string(key));
  return it == g_textures.end() ? std::string() : it->second;
}

bool HasArtTexture(std::string_view key) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_textures.find(std::string(key)) != g_textures.end();
}

std::size_t ArtTextureCount() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_textures.size();
}

std::size_t ArtUrlCount() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_urls.size();
}

void ResetAlbumArtForTests() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_urls.clear();
  g_textures.clear();
  g_texture_request = nullptr;
  g_enabled = true;
}

void SetArtTextureRequest(ArtTextureRequest request) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_texture_request = std::move(request);
}

void RequestArtTexture(std::string_view key) {
  ArtTextureRequest request;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    request = g_texture_request;
  }
  if (request && !key.empty()) {
    request(key);
  }
}

}  // namespace rb_blitz::store
