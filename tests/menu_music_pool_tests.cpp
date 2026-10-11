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
//   * the deck deals every song once per cycle and never hands the same song back twice in
//     a row, including across a reshuffle - which is exactly what a per-load random pick
//     gets wrong.
//
// No SDK, no game image, no boot: the fixtures are song names.

#include "check.h"

#include "hooks/menu_music_pool.h"

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

void TestDeckEmpty() {
  BeginCase("deck has no card to deal from an empty pool");
  std::mt19937 rng(1);
  Deck deck(rng);
  CHECK_EQ(deck.Next(0), kNoIndex);
}

void TestDeckDealsEverySongOnce() {
  BeginCase("deck deals every song exactly once per cycle");
  for (size_t size = 1; size <= 8; ++size) {
    std::mt19937 rng(100u + static_cast<unsigned>(size));
    Deck deck(rng);
    std::vector<int> dealt(size, 0);
    for (size_t i = 0; i < size; ++i) {
      const size_t index = deck.Next(size);
      CHECK_TRUE(index < size);
      ++dealt[index];
    }
    for (size_t i = 0; i < size; ++i) {
      CHECK_EQ(dealt[i], 1);
    }
  }
}

void TestDeckNeverDealsTheSameSongTwiceInARow() {
  BeginCase("deck never deals the same song twice in a row");
  for (size_t size = 2; size <= 8; ++size) {
    std::mt19937 rng(200u + static_cast<unsigned>(size));
    Deck deck(rng);
    size_t previous = kNoIndex;
    for (int deal = 0; deal < 500; ++deal) {
      const size_t index = deck.Next(size);
      CHECK_EQ(index == previous, 0);
      previous = index;
    }
  }
}

void TestDeckReshuffles() {
  BeginCase("deck starts from a different card under different seeds");
  std::set<size_t> first_cards;
  for (unsigned seed = 0; seed < 32; ++seed) {
    std::mt19937 rng(seed);
    Deck deck(rng);
    first_cards.insert(deck.Next(8));
  }
  CHECK_TRUE(first_cards.size() > 1);
}

}  // namespace

int main() {
  TestSongName();
  TestPoolDedupesTheSameSong();
  TestPoolCapsAndStaysASubset();
  TestPoolKeepsWhatItHas();
  TestPoolIsShuffled();
  TestDeckEmpty();
  TestDeckDealsEverySongOnce();
  TestDeckNeverDealsTheSameSongTwiceInARow();
  TestDeckReshuffles();
  return Finish();
}
