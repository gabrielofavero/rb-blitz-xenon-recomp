// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The pure half of R9 (src/hooks/shell_music.cpp): given the songs a boot found in the
// DLC it mounted, which ones the menu plays and in what order.
//
// It is deliberately free of the SDK, of guest memory and of any global state, so the
// two answers that matter - "is the opening song different every boot" and "does a song
// ever follow itself" - can be reasoned about and tested (tests/menu_music_pool_tests.cpp)
// without booting the game. shell_music.cpp owns the devices, the hooks and the guest
// strings; everything here is bookkeeping.

#pragma once

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::shell_music {

// One playable song: the guest device it streams from (an alias like `menumusic3`) and the
// path into that device, without the extension `ShellMusic::Load` appends.
struct PoolEntry {
  std::string alias;
  std::string stream;
};

inline constexpr size_t kNoIndex = static_cast<size_t>(-1);

// A song's identity: the last component of its stream path. A package names the folder and
// the `.mogg` after the song key, so the same song carried by two packages - the walk can
// see both a loose dump and a merged pack - has one name.
inline std::string_view SongName(const std::string& stream) {
  const size_t slash = stream.find_last_of('\\');
  return slash == std::string::npos ? std::string_view(stream)
                                    : std::string_view(stream).substr(slash + 1);
}

// The menu's pool: a random sample of the songs the walk found, up to `cap`, with a song
// seen twice collapsed to one entry. The candidates arrive in walk order, and a merged pack
// hands over every song it holds at once, so taking them in turn would open on the same
// track every boot; the sample is shuffled before it is cut. `cap` is small (at most
// enhancements_menu_dlc_song_count), so this is a sample of the library, not the library.
inline std::vector<PoolEntry> BuildPool(std::vector<PoolEntry> candidates, size_t cap,
                                        std::mt19937& rng) {
  std::vector<PoolEntry> pool;
  pool.reserve(std::min(cap, candidates.size()));
  std::vector<std::string> seen;
  for (PoolEntry& candidate : candidates) {
    std::string name(SongName(candidate.stream));
    if (std::find(seen.begin(), seen.end(), name) != seen.end()) {
      continue;
    }
    seen.push_back(std::move(name));
    pool.push_back(std::move(candidate));
  }
  std::shuffle(pool.begin(), pool.end(), rng);
  if (pool.size() > cap) {
    pool.resize(cap);
  }
  return pool;
}

// ------------------------------------------------------------------------------------------------
// The "Play Current Song" probe channel (owner feature, 2026-10-14)
// ------------------------------------------------------------------------------------------------
// The main menu's Play Current Song row has to learn *which* pooled song the menu is playing, and
// the only value a DTA script can read back from the host is a boolean (`file_exists`, the builtin
// the settings channel already rides on - see src/hooks/settings_bridge_hooks.cpp). So the row asks
// one question per pool index and the host answers true for exactly one of them. The paths are
// parsed here, without the SDK, so the protocol is a test rather than a claim.
// The namespace the channel lives in, and the probe prefix inside it. The whole namespace is ours:
// a name in it is a name no content file can have, so the builtin hook answers every one of them -
// including a malformed probe, which is answered "false" rather than forwarded, because a
// forwarded bare name is a name the title's own file layer may answer out of data it already has
// (the lesson R7's refresh sentinel taught, docs/engine/main-menu.md part 1 §7.1).
inline constexpr std::string_view kPlayNamespace = "rbbz-play/";
inline constexpr std::string_view kPlayProbePrefix = "rbbz-play/current/";

// The pool index a probe path names, or -1 when the path is not one of ours. A probe is
// `<prefix><decimal index>`; anything else in the namespace is not a question we can answer.
inline int PlayProbeIndex(std::string_view path) {
  if (!path.starts_with(kPlayProbePrefix)) {
    return -1;
  }
  const std::string_view digits = path.substr(kPlayProbePrefix.size());
  if (digits.empty() || digits.size() > 6) {
    return -1;
  }
  int value = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') {
      return -1;
    }
    value = value * 10 + (c - '0');
  }
  return value;
}

// The play order of the pool: a shuffled deck, dealt to the end and then reshuffled. A
// per-load pick can hand the same song back twice in a row; a deck cannot, and the first
// card of a new deck is never the card that just played, so no song ever follows itself.
class Deck {
 public:
  explicit Deck(std::mt19937& rng) : rng_(rng) {}

  // The next index of the pool, or kNoIndex when there is no pool. `pool_size` is the size
  // of the pool being walked; a different size - a new library, or R9 switched off and on
  // again - starts a fresh deck.
  size_t Next(size_t pool_size) {
    if (pool_size == 0) {
      Reset();
      return kNoIndex;
    }
    if (pool_size != size_) {
      Reset();
      size_ = pool_size;
    }
    if (next_ >= order_.size()) {
      order_.resize(pool_size);
      std::iota(order_.begin(), order_.end(), size_t{0});
      std::shuffle(order_.begin(), order_.end(), rng_);
      if (order_.size() > 1 && order_.front() == last_) {
        std::swap(order_.front(), order_[1]);
      }
      next_ = 0;
    }
    last_ = order_[next_++];
    return last_;
  }

  void Reset() {
    order_.clear();
    next_ = 0;
    last_ = kNoIndex;
    size_ = 0;
  }

 private:
  std::mt19937& rng_;
  std::vector<size_t> order_;
  size_t next_ = 0;
  size_t last_ = kNoIndex;
  size_t size_ = 0;
};

}  // namespace rb_blitz::shell_music
