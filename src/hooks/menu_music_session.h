// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "hooks/menu_music_pool.h"
#include <map>
#include <utility>

namespace rb_blitz::shell_music {

// Stable metadata identities and a single ordered playback history. Mounts are
// deliberately external: evicting a device never invalidates a song or cursor.
class MusicSession {
 public:
  static constexpr size_t kAhead = 3;
  static constexpr size_t kBehind = 2;
  static constexpr size_t kResidentLimit = 8;
  enum class Outcome { kQueued, kHeard, kSkipped, kFailed };
  struct Entry { size_t song; size_t pass; Outcome outcome = Outcome::kQueued; };

  size_t Discover(size_t package, std::vector<PoolEntry> songs) {
    if (!discovered_.insert(package).second) return 0;
    const size_t before = sources_.size();
    for (auto& song : songs) {
      const auto identity = SongIdentity(PoolSongKey(song));
      if (identity.empty() || identities_.contains(identity)) continue;
      const size_t id = sources_.size();
      identities_.emplace(identity, id);
      sources_.push_back(std::move(song));
      packages_.push_back(package);
      strikes_.push_back(0);
      remaining_.push_back(id);
    }
    return sources_.size() - before;
  }

  void DiscoveryComplete() { discovery_complete_ = true; }
  bool DiscoveryFinished() const { return discovery_complete_; }
  void BlockPackage(size_t package) { blocked_.insert(package); }
  bool PackageBlocked(size_t package) const { return blocked_.contains(package); }
  bool Usable(size_t song) const {
    return song < sources_.size() && strikes_[song] < 3 && !blocked_.contains(packages_[song]);
  }
  size_t UpcomingCount() const {
    size_t count = 0;
    for (size_t i = cursor_ == kNoIndex ? 0 : cursor_ + 1; i < playlist_.size(); ++i)
      if (Usable(playlist_[i].song)) ++count;
    return count;
  }
  bool NeedsDiscovery() const {
    return !discovery_complete_ && UpcomingCount() < (cursor_ == kNoIndex ? 1 + kAhead : kAhead);
  }

  // Reserve only the current/initial entry plus a few successors. A merged pack
  // can contribute many cheap catalog records without mounting other packages.
  void PlanAhead(std::mt19937& rng) {
    size_t ahead = UpcomingCount();
    const size_t wanted = cursor_ == kNoIndex ? 1 + kAhead : kAhead;
    while (ahead < wanted) {
      std::erase_if(remaining_, [this](size_t id) { return !Usable(id); });
      if (remaining_.empty()) {
        if (!discovery_complete_) break;  // never repeat ahead of unseen packages
        for (size_t id = 0; id < sources_.size(); ++id)
          if (Usable(id)) remaining_.push_back(id);
        if (remaining_.empty()) break;
        ++pass_;
      }
      std::vector<size_t> choices;
      for (size_t i = 0; i < remaining_.size(); ++i)
        if (playlist_.empty() || remaining_[i] != playlist_.back().song) choices.push_back(i);
      if (choices.empty()) break;  // sole song must not follow itself
      const size_t pick = choices[std::uniform_int_distribution<size_t>(0, choices.size()-1)(rng)];
      playlist_.push_back({remaining_[pick], pass_});
      remaining_.erase(remaining_.begin() + pick);
      ++ahead;
    }
  }

  size_t NextPosition() const {
    for (size_t i = cursor_ == kNoIndex ? 0 : cursor_ + 1; i < playlist_.size(); ++i)
      if (Usable(playlist_[i].song)) return i;
    return kNoIndex;
  }
  bool Select(size_t position) {
    if (position >= playlist_.size() || !Usable(playlist_[position].song)) return false;
    cursor_ = position;
    return true;
  }
  // Future navigator seam: >=5 s restarts; otherwise follow actual history.
  // No commands, input interception or guest navigation are installed here.
  size_t PreviousPosition(uint32_t samples) const {
    if (cursor_ == kNoIndex || samples >= 5 * 44100) return cursor_;
    for (size_t i = cursor_; i > 0; --i)
      if (Usable(playlist_[i-1].song)) return i-1;
    return cursor_;
  }
  void Heard(size_t song) {
    if (song >= sources_.size()) return;
    strikes_[song] = 0;
    heard_.insert(song);
    if (cursor_ != kNoIndex && playlist_[cursor_].song == song)
      playlist_[cursor_].outcome = Outcome::kHeard;
  }
  void Failed(size_t song) {
    if (song >= sources_.size()) return;
    ++strikes_[song];
    if (cursor_ != kNoIndex && playlist_[cursor_].song == song)
      playlist_[cursor_].outcome = Outcome::kFailed;
  }
  void Skipped() {
    if (cursor_ != kNoIndex && playlist_[cursor_].outcome == Outcome::kQueued)
      playlist_[cursor_].outcome = Outcome::kSkipped;
  }

  std::set<size_t> WarmPackages(const std::vector<size_t>& pins = {}) const {
    std::set<size_t> result;
    if (cursor_ != kNoIndex) {
      result.insert(packages_[playlist_[cursor_].song]);
      size_t kept = 0;
      for (size_t i = cursor_; i > 0 && kept < kBehind; --i)
        if (Usable(playlist_[i-1].song)) { result.insert(packages_[playlist_[i-1].song]); ++kept; }
    }
    size_t kept = 0;
    const size_t wanted = cursor_ == kNoIndex ? 1 + kAhead : kAhead;
    for (size_t i = cursor_ == kNoIndex ? 0 : cursor_ + 1;
         i < playlist_.size() && kept < wanted; ++i)
      if (Usable(playlist_[i].song)) { result.insert(packages_[playlist_[i].song]); ++kept; }
    for (size_t id : pins) if (id < packages_.size()) result.insert(packages_[id]);
    return result;
  }
  size_t Cursor() const { return cursor_; }
  size_t SongAt(size_t position) const { return playlist_.at(position).song; }
  size_t Package(size_t song) const { return packages_.at(song); }
  const std::vector<PoolEntry>& Sources() const { return sources_; }
  const std::vector<Entry>& Playlist() const { return playlist_; }
  bool WasHeard(size_t song) const { return heard_.contains(song); }

 private:
  std::vector<PoolEntry> sources_;
  std::vector<size_t> packages_, remaining_;
  std::vector<unsigned> strikes_;
  std::vector<Entry> playlist_;
  std::map<std::string, size_t> identities_;
  std::set<size_t> discovered_, blocked_, heard_;
  size_t cursor_ = kNoIndex, pass_ = 0;
  bool discovery_complete_ = false;
};
}  // namespace rb_blitz::shell_music
