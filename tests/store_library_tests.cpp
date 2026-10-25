// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the store's Downloaded matcher (src/store/store_library.h): the SDK-free half.
//
// What these pin down (plan §3.2):
//   * the field normalization - trim, collapse interior whitespace, ASCII case folding - applied
//     identically to both sides, so a trailing space in songs.dta cannot hide a match;
//   * both fields are required: an offer or a library entry with an empty artist or title never
//     matches, because an empty field would pair with every other empty one;
//   * no fuzzy matching - punctuation, version suffixes and featured-artist text are significant;
//   * the index is a set: the same pair added twice is one song (a merged pack and a loose package
//     may both declare it).

#include "check.h"

#include "store/store_library.h"

#include <string>

namespace {

using namespace rb_blitz::test;
using rb_blitz::store::LoadedSongIndex;
using rb_blitz::store::NormalizeSongField;

void TestNormalization() {
  BeginCase("normalization trims, collapses and folds case");
  CHECK_TRUE(NormalizeSongField("  Bush ") == "bush");
  CHECK_TRUE(NormalizeSongField("Kate\t Bush") == "kate bush");
  CHECK_TRUE(NormalizeSongField("THE  1975") == "the 1975");
  CHECK_TRUE(NormalizeSongField("Four Year Strong") == "four year strong");
  // Interior punctuation and apostrophes are significant - the plan forbids stripping them.
  CHECK_TRUE(NormalizeSongField("Don't Stop Me Now") == "don't stop me now");
  CHECK_TRUE(NormalizeSongField("AC/DC") == "ac/dc");
  CHECK_TRUE(NormalizeSongField("") == "");
  CHECK_TRUE(NormalizeSongField("   ") == "");
}

void TestIndex() {
  BeginCase("the index matches exactly after normalization and needs both fields");
  LoadedSongIndex index;
  index.Add("Bush", "The People That We Love");
  index.Add("Kate Bush", "Army Dreamers");
  CHECK_TRUE(index.size() == 2);

  // The same spelling with different case and spacing is the same song.
  CHECK_TRUE(index.Contains("BUSH", "the people that we love"));
  CHECK_TRUE(index.Contains("  Kate  Bush ", "Army Dreamers  "));
  CHECK_FALSE(index.Contains("Bush", "Army Dreamers"));
  CHECK_FALSE(index.Contains("Kate Bush", "The People That We Love"));

  // Both fields required, on both sides of the comparison.
  CHECK_FALSE(index.Contains("", "Army Dreamers"));
  CHECK_FALSE(index.Contains("Kate Bush", ""));
  LoadedSongIndex incomplete;
  incomplete.Add("", "Army Dreamers");
  incomplete.Add("Kate Bush", "   ");
  CHECK_TRUE(incomplete.size() == 0);
  CHECK_FALSE(incomplete.Contains("", ""));

  // No fuzzy matching: a version suffix, featured-artist text or punctuation difference misses.
  CHECK_FALSE(index.Contains("Bush", "The People That We Love (Live)"));
  CHECK_FALSE(index.Contains("Bush", "The People That We Loved"));
  CHECK_FALSE(index.Contains("Bush feat. Someone", "The People That We Love"));

  // A pair added twice is one song (a merged pack and a loose package can both declare it).
  index.Add("BUSH ", "the people that we love");
  CHECK_TRUE(index.size() == 2);

  // An alternate chart of the same artist + title is the same pair by this rule (the owner's
  // deliberate approximation: all of them show Downloaded, and Download Again stays explicit).
  index.Add("Bush", "The People That We Love");
  CHECK_TRUE(index.size() == 2);
  CHECK_TRUE(index.Contains("Bush", "The People That We Love"));
}

void TestClear() {
  BeginCase("clearing the index is what an invalidated library looks like");
  LoadedSongIndex index;
  index.Add("Bush", "The People That We Love");
  CHECK_TRUE(index.Contains("Bush", "The People That We Love"));
  index.Clear();
  CHECK_TRUE(index.size() == 0);
  CHECK_FALSE(index.Contains("Bush", "The People That We Love"));
}

void TestPackages() {
  BeginCase("ownership tracks Unicode names and every package of a matching song");
  LoadedSongIndex index;
  index.Add("BJ\xC3\x96RK", "J\xC3\x93GA", "first.con");
  index.Add("bj\xC3\xB6rk", "j\xC3\xB3ga", "second.con");
  index.Add("Other Artist", "Other Song", "first.con");
  CHECK_EQ(index.size(), 2u);
  CHECK_TRUE(index.Contains("bj\xC3\xB6rk", "j\xC3\xB3ga"));
  CHECK_TRUE(index.Contains("BJ\xD6RK", "J\xD3GA"));  // Latin-1 songs.dta, UTF-8 API
  CHECK_EQ(index.Packages("bj\xC3\xB6rk", "j\xC3\xB3ga").size(), 2u);
  index.RemovePackage("first.con");
  CHECK_FALSE(index.Contains("Other Artist", "Other Song"));
  CHECK_TRUE(index.Contains("bj\xC3\xB6rk", "j\xC3\xB3ga"));
  index.RemovePackage("second.con");
  CHECK_EQ(index.size(), 0u);
}

}  // namespace

int main() {
  TestNormalization();
  TestIndex();
  TestClear();
  TestPackages();
  return rb_blitz::test::Finish();
}
