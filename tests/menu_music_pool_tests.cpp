// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/hooks/menu_music_pool.h, the pure half of R9 - the main menu's DLC
// background music. Two properties are the whole reason that header exists, and both are
// cheap to state without a game:
//
//   * the pool is a sample of the songs the walk found: a song that two packages both carry
//     is counted once, the sample is never larger than the requested size, and it is
//     shuffled rather than taken in walk order (a merged pack hands over all its songs at
//     once, so walk order would always open on the same track);
//   * the rotation plays every distinct song of the pool before it repeats one, never hands
//     the song that just played back straight away - including across a reshuffle, a menu
//     re-entry or a rebuilt pool, because it remembers song *identities* rather than indices -
//     and, when the pool can only offer the song that just played, says so instead of looping
//     that one song forever;
//   * a song whose load failed sits out the pass it failed in, is tried again in the next, and
//     is retired from the boot's rotation only after repeated failures.
//
// No SDK, no game image, no boot: the fixtures are song names.

#include "check.h"

#include "hooks/menu_music_pool.h"
#include "hooks/menu_music_playback.h"

#include <cstddef>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::shell_music;
using namespace rb_blitz::test;

// Distinct songs under one package alias, the shape a merged pack produces.
std::vector<PoolEntry> DistinctSongs(size_t count, const char* alias = "menumusic0") {
  std::vector<PoolEntry> songs;
  for (size_t i = 0; i < count; ++i) {
    const std::string key = "song" + std::to_string(i);
    songs.push_back({alias, "\\songs\\" + key + "\\" + key});
  }
  return songs;
}

std::set<std::string> Names(const std::vector<PoolEntry>& pool) {
  std::set<std::string> names;
  for (const PoolEntry& entry : pool) {
    names.emplace(SongName(entry.stream));
  }
  return names;
}

void TestSongName() {
  BeginCase("song_name reads the song key off the stream path");
  CHECK_TRUE(SongName("\\songs\\16candles\\16candles") == "16candles");
  CHECK_TRUE(SongName("songs\\a\\b") == "b");
  CHECK_TRUE(SongName("lonely") == "lonely");
  CHECK_TRUE(SongName("") == "");
}

void TestPlayProbePaths() {
  BeginCase("the Play Current Song probes name one pool index, and only their namespace is ours");
  // The record the payload bakes asks one question per pool entry; the host answers true for the
  // track that is playing. The parser is what keeps "a path in our namespace" from meaning "a path
  // that happens to start with something".
  CHECK_EQ(PlayProbeIndex("rbbz-play/current/0"), 0);
  CHECK_EQ(PlayProbeIndex("rbbz-play/current/7"), 7);
  CHECK_EQ(PlayProbeIndex("rbbz-play/current/49"), 49);
  // Not a probe: another namespace, a bare prefix, a trailing separator, a sign, a non-digit, and
  // an index long enough to be a mistake rather than a pool entry.
  CHECK_EQ(PlayProbeIndex("rbbz-settings/current/0"), -1);
  CHECK_EQ(PlayProbeIndex("rbbz-play/current/"), -1);
  CHECK_EQ(PlayProbeIndex("rbbz-play/current/0/"), -1);
  CHECK_EQ(PlayProbeIndex("rbbz-play/current/-1"), -1);
  CHECK_EQ(PlayProbeIndex("rbbz-play/current/x"), -1);
  CHECK_EQ(PlayProbeIndex("rbbz-play/current/1234567"), -1);
  CHECK_EQ(PlayProbeIndex(""), -1);
  CHECK_EQ(PlayProbeIndex("rbbz-play"), -1);
  // The whole namespace is ours (the builtin hook answers it rather than forwarding), and the probe
  // prefix really is inside it.
  CHECK_TRUE(kPlayNamespace.starts_with("rbbz-play"));
  CHECK_TRUE(kPlayProbePrefix.starts_with(kPlayNamespace));
}

void TestPoolDedupesTheSameSong() {
  BeginCase("pool counts a song two packages carry once");
  std::vector<PoolEntry> input = {
      {"menumusic0", "\\songs\\16candles\\16candles"},
      {"menumusic1", "\\songs\\16candles\\16candles"},
      {"menumusic1", "\\songs\\always\\always"},
  };
  std::mt19937 rng(3);
  const std::vector<PoolEntry> pool = BuildPool(input, 8, rng);
  CHECK_EQ(pool.size(), 2);
  CHECK_EQ(Names(pool).count("16candles"), 1);
  CHECK_EQ(Names(pool).count("always"), 1);
}

void TestPoolCapsAndStaysASubset() {
  BeginCase("pool is no larger than the cap and holds only walked songs");
  const std::vector<PoolEntry> input = DistinctSongs(10);
  std::mt19937 rng(7);
  const std::vector<PoolEntry> pool = BuildPool(input, 3, rng);
  CHECK_EQ(pool.size(), 3);
  CHECK_EQ(Names(pool).size(), 3);
  for (const PoolEntry& entry : pool) {
    CHECK_TRUE(std::string(SongName(entry.stream)).size() > 0);
    bool found = false;
    for (const PoolEntry& candidate : input) {
      found = found || candidate.stream == entry.stream;
    }
    CHECK_TRUE(found);
  }
}

void TestPoolKeepsWhatItHas() {
  BeginCase("pool keeps every song when there are fewer than the cap");
  const std::vector<PoolEntry> input = DistinctSongs(2);
  std::mt19937 rng(11);
  const std::vector<PoolEntry> pool = BuildPool(input, 64, rng);
  CHECK_EQ(pool.size(), 2);

  std::mt19937 empty_rng(12);
  CHECK_EQ(BuildPool({}, 8, empty_rng).size(), 0);
}

void TestPoolIsShuffled() {
  BeginCase("pool is shuffled, not taken in walk order");
  const std::vector<PoolEntry> input = DistinctSongs(8);
  std::vector<std::string> walk_order;
  for (const PoolEntry& entry : input) {
    walk_order.push_back(entry.stream);
  }
  bool saw_a_different_order = false;
  for (unsigned seed = 0; seed < 32; ++seed) {
    std::mt19937 rng(seed);
    const std::vector<PoolEntry> pool = BuildPool(input, 8, rng);
    CHECK_EQ(pool.size(), 8);
    std::vector<std::string> order;
    for (const PoolEntry& entry : pool) {
      order.push_back(entry.stream);
    }
    saw_a_different_order = saw_a_different_order || order != walk_order;
  }
  CHECK_TRUE(saw_a_different_order);
}

// The song keys of a pool of `count` distinct songs, in pool order.
std::vector<std::string> PoolKeys(size_t count) {
  std::vector<std::string> keys;
  for (size_t i = 0; i < count; ++i) {
    keys.push_back("song" + std::to_string(i));
  }
  return keys;
}

void TestRotationEmpty() {
  BeginCase("the rotation has nothing to hand out from an empty pool");
  std::mt19937 rng(1);
  Rotation rotation(rng);
  CHECK_EQ(rotation.Next(), kNoIndex);
  CHECK_EQ(rotation.rotations(), 0);
  // A pool that appears, then goes away (R9 switched off and on), is not a rotation to keep.
  rotation.SetPool({"only"});
  CHECK_TRUE(rotation.Next() != kNoIndex);
  rotation.SetPool({});
  CHECK_EQ(rotation.Next(), kNoIndex);
}

void TestRotationHearsEverySongBeforeRepeatingOne() {
  BeginCase("a pass plays every distinct song before any of them is played again");
  for (size_t size = 2; size <= 8; ++size) {
    std::mt19937 rng(300u + static_cast<unsigned>(size));
    Rotation rotation(rng);
    rotation.SetPool(PoolKeys(size));
    std::set<std::string> heard;
    for (size_t i = 0; i < size; ++i) {
      const size_t index = rotation.Next();
      CHECK_TRUE(index < size);
      const std::string& key = rotation.names()[index];
      CHECK_EQ(heard.count(key), 0);
      heard.insert(key);
    }
    CHECK_EQ(heard.size(), size);
  }
}

void TestRotationNeverRepeatsTheSongThatJustPlayed() {
  BeginCase("no song ever follows itself, over as many passes as it is dealt");
  for (size_t size = 2; size <= 8; ++size) {
    std::mt19937 rng(400u + static_cast<unsigned>(size));
    Rotation rotation(rng);
    rotation.SetPool(PoolKeys(size));
    std::string previous;
    for (int deal = 0; deal < 300; ++deal) {
      const size_t index = rotation.Next();
      CHECK_TRUE(index != kNoIndex);
      const std::string key = rotation.names()[index];
      CHECK_TRUE(key != previous);
      previous = key;
    }
  }
}

void TestRotationOneSongDoesNotRepeatItself() {
  BeginCase("a one-song pool plays it once and then has nothing distinct left");
  std::mt19937 rng(5);
  Rotation rotation(rng);
  rotation.SetPool({"only"});
  CHECK_EQ(rotation.Next(), 0);
  // The rule the owner's report turned into: not the same song again, for as long as it is the
  // only distinct thing the pool can offer. The caller hands the menu the title's own tracks here.
  CHECK_EQ(rotation.Next(), kNoIndex);
  CHECK_EQ(rotation.Next(), kNoIndex);
}

void TestRotationKeepsHistoryAcrossARebuiltPool() {
  BeginCase("a rebuilt pool cannot hand back a song this pass has already played");
  std::mt19937 rng(6);
  Rotation rotation(rng);
  rotation.SetPool({"a", "b", "c"});
  const size_t first = rotation.Next();
  const std::string played = rotation.names()[first];
  // The same songs come back reordered and one is added: identity, not position, decides. A song
  // that has been heard is not new again because the list around it changed shape.
  rotation.SetPool({"c", "a", "d", "b"});
  std::set<std::string> this_pass{played};
  for (int i = 0; i < 3; ++i) {
    const size_t index = rotation.Next();
    CHECK_TRUE(index != kNoIndex);
    const std::string& key = rotation.names()[index];
    CHECK_EQ(this_pass.count(key), 0);
    this_pass.insert(key);
  }
}

void TestRotationSkipsAFailedSongAndRetriesItNextPass() {
  BeginCase("a song that failed steps out of the pass, is retried in the next, and is retired "
            "after repeated failures");
  std::mt19937 rng(7);
  Rotation rotation(rng);
  rotation.SetPool({"a", "b", "c"});
  const size_t index = rotation.Next();
  const std::string failed = rotation.names()[index];
  rotation.MarkFailed(failed);
  CHECK_TRUE(rotation.failed(failed));
  // The rest of this pass never hands it back.
  for (int i = 0; i < 2; ++i) {
    const size_t next = rotation.Next();
    CHECK_TRUE(next != kNoIndex);
    CHECK_TRUE(rotation.names()[next] != failed);
  }
  // The next pass does try it again (one failure is a pass-level quarantine, not a boot-level).
  bool seen_again = false;
  for (int i = 0; i < 12 && !seen_again; ++i) {
    const size_t next = rotation.Next();
    CHECK_TRUE(next != kNoIndex);
    seen_again = rotation.names()[next] == failed;
  }
  CHECK_TRUE(seen_again);
  // After enough strikes it sits out the rest of the boot.
  rotation.MarkFailed(failed);
  rotation.MarkFailed(failed);
  rotation.MarkFailed(failed);
  CHECK_TRUE(rotation.quarantined(failed));
  for (int i = 0; i < 60; ++i) {
    const size_t next = rotation.Next();
    CHECK_TRUE(next == kNoIndex || rotation.names()[next] != failed);
  }
}

void TestRotationAnnouncesThePassBoundary() {
  BeginCase("the rotation says when it starts a new pass over the pool");
  std::mt19937 rng(8);
  Rotation rotation(rng);
  rotation.SetPool(PoolKeys(3));
  CHECK_EQ(rotation.reshuffled(), 0);
  for (size_t i = 0; i < 3; ++i) {
    rotation.Next();
    CHECK_EQ(rotation.reshuffled(), 0);
  }
  rotation.Next();  // the fourth pick is the first of the next pass
  CHECK_EQ(rotation.reshuffled(), 1);
  CHECK_EQ(rotation.rotations(), 1);
}

}  // namespace

void TestPlaybackLifecycle() {
  using Playback = rb_blitz::shell_music::Playback;
  using Event = Playback::Event;
  BeginCase("one-shot playback publishes actual samples, finishes once, and cancels on exit");
  Playback playback;
  playback.Begin(0);
  CHECK_TRUE(playback.Observe(10, 10, false, false, 500) == Event::kNone);
  CHECK_TRUE(playback.Observe(20, 10, true, false, 500) == Event::kAudible);
  CHECK_TRUE(playback.Observe(30, 10, true, false, 1000) == Event::kNone);
  CHECK_TRUE(playback.Observe(40, 10, false, true, 1000) == Event::kEnded);
  CHECK_TRUE(playback.Observe(50, 10, false, true, 1000) == Event::kNone);
  // Allocators may reuse an address: Begin must discard the previous source's position.
  playback.Begin(60);
  CHECK_TRUE(playback.Observe(70, 10, true, false, 100) == Event::kAudible);
  playback.Stop();
  CHECK_TRUE(playback.Observe(80, 0, false, false, 0) == Event::kNone);
  CHECK_TRUE(playback.Observe(90, 10, false, true, 100) == Event::kNone);

  BeginCase("missing, failed, replaced and stalled streams cannot freeze the rotation");
  playback.Begin(0);
  CHECK_TRUE(playback.Observe(3000, 0, false, false, 0) == Event::kFailed);
  playback.Begin(0);
  CHECK_TRUE(playback.Observe(3000, 10, false, false, 0) == Event::kFailed);
  playback.Begin(0);
  CHECK_TRUE(playback.Observe(1, 10, true, false, 100) == Event::kAudible);
  CHECK_TRUE(playback.Observe(2, 20, true, false, 200) == Event::kFailed);
  playback.Begin(0);
  CHECK_TRUE(playback.Observe(1, 10, true, false, 100) == Event::kAudible);
  CHECK_TRUE(playback.Observe(3001, 10, true, false, 100) == Event::kFailed);
  // Even a subsecond source must advance instead of replaying its prefix.
  playback.Begin(0);
  playback.Observe(1, 10, true, false, 2000);
  CHECK_TRUE(playback.Observe(2, 10, true, false, 100) == Event::kEnded);
}

void TestCanonicalSongHistory() {
  BeginCase("aliases, case and separators preserve identity, including an empty refresh");
  std::mt19937 rng(2);
  const auto pool = BuildPool({{"a", "\\songs\\Song\\Song"},
                               {"b", "songs/song/SONG"}}, 50, rng);
  CHECK_EQ(pool.size(), 1u);
  CHECK_TRUE(pool[0].stream == "\\songs\\Song\\Song");
  Rotation rotation(rng);
  rotation.SetPool({"Song"});
  CHECK_EQ(rotation.Next(), 0u);
  rotation.SetPool({});
  CHECK_EQ(rotation.Next(), kNoIndex);
  rotation.SetPool({"SONG"});
  CHECK_EQ(rotation.Next(), kNoIndex);
}

int main() {
  TestPlaybackLifecycle();
  TestCanonicalSongHistory();
  TestSongName();
  TestPlayProbePaths();
  TestPoolDedupesTheSameSong();
  TestPoolCapsAndStaysASubset();
  TestPoolKeepsWhatItHas();
  TestPoolIsShuffled();
  TestRotationEmpty();
  TestRotationHearsEverySongBeforeRepeatingOne();
  TestRotationNeverRepeatsTheSongThatJustPlayed();
  TestRotationOneSongDoesNotRepeatItself();
  TestRotationKeepsHistoryAcrossARebuiltPool();
  TestRotationSkipsAFailedSongAndRetriesItNextPass();
  TestRotationAnnouncesThePassBoundary();
  return Finish();
}
