// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The pure half of R9 (src/hooks/shell_music.cpp): given the songs a boot found in the
// DLC it mounted, which ones the menu plays and in what order.
//
// It is deliberately free of the SDK, of guest memory and of any global state, so the
// two answers that matter - "is the opening song different every boot" and "does a song
// ever follow itself, or come back before the pool has been heard out" - can be reasoned
// about and tested (tests/menu_music_pool_tests.cpp) without booting the game.
// shell_music.cpp owns the devices, the hooks and the guest strings; everything here is
// bookkeeping.

#pragma once

#include <algorithm>
#include <cstddef>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::shell_music {

// One playable song: the guest device it streams from (an alias like `menumusic3`) and the
// path into that device, without the extension `ShellMusic::Load` appends.
struct PoolEntry {
  std::string alias;
  std::string stream;
  std::string song_key{};  // songs.dta/dtb key, which need not equal the filename
};

inline constexpr size_t kNoIndex = static_cast<size_t>(-1);

// A song's identity: the last component of its stream path. A package names the folder and
// the `.mogg` after the song key, so the same song carried by two packages - the walk can
// see both a loose dump and a merged pack - has one name.
inline std::string_view SongName(const std::string& stream) {
  const size_t slash = stream.find_last_of("\\/");
  return slash == std::string::npos ? std::string_view(stream)
                                    : std::string_view(stream).substr(slash + 1);
}

// Mount aliases, path separators and filename case do not create another song.
// Keep the original spelling in PoolEntry for the guest's case-sensitive Symbol lookup.
inline std::string SongIdentity(const std::string& stream) {
  std::string key(SongName(stream));
  for (char& c : key) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return key;
}

inline std::string PoolSongKey(const PoolEntry& song) {
  return song.song_key.empty() ? std::string(SongName(song.stream)) : song.song_key;
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
    std::string name = SongIdentity(PoolSongKey(candidate));
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

// The rotation over the pool: every song is heard once before any of them is heard again, no song
// follows itself - including across a reshuffle, a menu re-entry or a rebuilt pool - and a song
// whose load failed steps aside for the rest of the rotation instead of being handed out again.
//
// Two things are deliberate here and worth stating, because they are what the owner's report
// ("long gaps and repeat") turned into rules:
//
//   * **history is by song identity, not by index.** A pool is rebuilt whenever the library it was
//     drawn from changes (a refresh, or the toggle going off and on again), and indices mean
//     something different afterwards. The rotation therefore remembers the song *keys* it has
//     played, so a rebuilt pool cannot hand back a song that has just been heard just because its
//     index moved.
//   * **one distinct playable song is not a rotation.** A pool that can only offer the song that
//     just played reports "nothing distinct left" (`kNoIndex`) rather than repeating it; the caller
//     decides what the player hears instead (src/hooks/shell_music.cpp hands the menu back to the
//     title's own three tracks, which is music rather than a loop of one song).
class Rotation {
 public:
  explicit Rotation(std::mt19937& rng) : rng_(rng) {}

  // The pool to rotate over, in draw order. Calling this again with a rebuilt pool keeps the
  // history: a song that has already been played in this rotation is not new again because the
  // list it sits in changed shape.
  void SetPool(std::vector<std::string> names) {
    for (std::string& name : names) name = SongIdentity(name);
    pool_ = std::move(names);
  }

  // The next song to play, or kNoIndex when nothing distinct is left (an empty pool, or a pool
  // whose only unplayed song is the one that just played).
  size_t Next() {
    if (pool_.empty()) {
      return kNoIndex;
    }
    std::vector<size_t> fresh = Candidates();
    // A pass that has played everything it can is dealt again: this pass's failures get another
    // chance (a song that failed once is not out for the boot, only for the pass it failed in),
    // and the song that just played is not the one that opens the new pass.
    if (fresh.empty()) {
      failed_.clear();
      played_.clear();
      fresh = Candidates();
      if (!fresh.empty()) {
        ++rotations_;
      }
      last_reshuffled_ = true;
    } else {
      last_reshuffled_ = false;
    }
    if (fresh.empty()) {
      return kNoIndex;  // one distinct playable song, and it has just played
    }
    std::uniform_int_distribution<size_t> pick(0, fresh.size() - 1);
    const size_t index = fresh[pick(rng_)];
    played_.insert(pool_[index]);
    last_ = pool_[index];
    return index;
  }

  // A song the menu could not actually start (the stream never appeared, or never advanced): it
  // steps out of this rotation so the next pick cannot hand it straight back.
  void MarkFailed(const std::string& song) {
    const std::string name = SongIdentity(song);
    if (name.empty()) {
      return;
    }
    const int strikes = ++strikes_[name];
    failed_.insert(name);
    if (strikes >= kMaxStrikes) {
      quarantined_.insert(name);
    }
  }

  // A successful start breaks a run of failures. Three unrelated cold-load delays over
  // hours of successful playback must not permanently remove an otherwise healthy song.
  void MarkStarted(const std::string& song) {
    const auto name = SongIdentity(song);
    strikes_.erase(name);
    failed_.erase(name);
  }

  // Did the last Next() start a new pass over the pool? (For the log line, and for a test that
  // wants to see the boundary rather than infer it.)
  bool reshuffled() const { return last_reshuffled_; }
  // How many times a full pass has been dealt, since the first one. Zero before the first pick.
  size_t rotations() const { return rotations_; }
  size_t size() const { return pool_.size(); }
  const std::vector<std::string>& names() const { return pool_; }
  const std::string& last() const { return last_; }
  bool failed(const std::string& name) const { return failed_.count(SongIdentity(name)) != 0; }
  // A song that has failed enough times to sit out the rest of the boot.
  bool quarantined(const std::string& name) const {
    return quarantined_.count(SongIdentity(name)) != 0;
  }

 private:
  static constexpr int kMaxStrikes = 3;

  // Every index this pass could still hand out: not quarantined, not already played in this pass,
  // and - when the pass opens - not the song that just played.
  std::vector<size_t> Candidates() {
    std::vector<size_t> fresh;
    for (size_t i = 0; i < pool_.size(); ++i) {
      const std::string& name = pool_[i];
      if (Quarantined(name) || played_.count(name) != 0) {
        continue;
      }
      if (played_.empty() && name == last_) {
        continue;  // the song that just played never opens the pass that follows it
      }
      fresh.push_back(i);
    }
    return fresh;
  }

  bool Quarantined(const std::string& name) const {
    return quarantined_.count(name) != 0 || failed_.count(name) != 0;
  }

  std::mt19937& rng_;
  std::vector<std::string> pool_;
  std::set<std::string> played_;        // song keys heard in the current pass
  std::set<std::string> failed_;        // keys that failed in the current pass
  std::set<std::string> quarantined_;   // keys that failed too often to try again this boot
  std::map<std::string, int> strikes_;
  std::string last_;
  bool last_reshuffled_ = false;
  size_t rotations_ = 0;
};

}  // namespace rb_blitz::shell_music
