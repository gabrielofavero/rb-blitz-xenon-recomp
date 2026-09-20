// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the drag-and-drop install's union rules (src/fs/overlay_merge.h). No
// SDK, no game image, no boot: the rules are pure host code, so the whole overlay can be
// proven correct without mounting a device.
//
// What is pinned here is the set of decisions that make "vanilla or Ultimate, never a
// mixed install" hold at the file level: which side of the union answers a read, which
// side accepts a write, how a name that both sides carry (in different spellings) is
// de-duplicated, and how the hidden payload default.xex prunes a subtree.

#include "check.h"

#include "fs/overlay_merge.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace rb_blitz;
using namespace rb_blitz::fs;
using namespace rb_blitz::test;

void CheckText(const char* file, int line, const std::string& actual, const std::string& expected,
               const char* actual_expr, const char* expected_expr) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, std::string(actual_expr) + " == " + expected_expr + " failed:\n         actual   \"" +
                     actual + "\"\n         expected \"" + expected + "\"");
}

// check.h compares integers and memory only, and half of what the union decides is text.
#define CHECK_TEXT(actual, expected) CheckText(__FILE__, __LINE__, (actual), (expected), #actual, #expected)

std::vector<std::string> Names(const std::vector<OverlayChild>& children) {
  std::vector<std::string> names;
  names.reserve(children.size());
  for (const OverlayChild& child : children) {
    names.push_back(child.name);
  }
  return names;
}

const OverlayChild* Find(const std::vector<OverlayChild>& children, std::string_view name) {
  const std::string folded = FoldName(name);
  for (const OverlayChild& child : children) {
    if (FoldName(child.name) == folded) {
      return &child;
    }
  }
  return nullptr;
}

void CheckBacking(const OverlayChild* child, const OverlayBacking read, const OverlayBacking write) {
  if (child == nullptr) {
    CHECK_TRUE(child != nullptr);
    return;
  }
  CHECK_EQ(static_cast<uint64_t>(ReadBackingFor(*child)), static_cast<uint64_t>(read));
  CHECK_EQ(static_cast<uint64_t>(WriteBackingFor(*child)), static_cast<uint64_t>(write));
}

void FoldNameFoldsAsciiAndLeavesOtherBytes() {
  BeginCase("FoldNameFoldsAsciiAndLeavesOtherBytes");
  CHECK_TEXT(FoldName("Default.XEX"), "default.xex");
  CHECK_TEXT(FoldName("patch_xbox_0.ark"), "patch_xbox_0.ark");
  CHECK_TEXT(FoldName(""), "");
  // A byte above ASCII is not a letter to fold; the install must not corrupt a name it
  // cannot classify.
  CHECK_TEXT(FoldName("\xC3\x89"), "\xC3\x89");
}

void FoldGuestPathDropsCaseAndSeparatorSpelling() {
  BeginCase("FoldGuestPathDropsCaseAndSeparatorSpelling");
  CHECK_TEXT(FoldGuestPath("Gen/Patch_xbox.hdr"), "gen\\patch_xbox.hdr");
  CHECK_TEXT(FoldGuestPath("\\gen\\data\\"), "gen\\data");
  CHECK_TEXT(FoldGuestPath("gen/"), "gen");
  CHECK_TEXT(FoldGuestPath("GAME:"), "game:");
  CHECK_TEXT(FoldGuestPath(""), "");
}

void JoinGuestsBuildsOverlayPaths() {
  BeginCase("JoinGuestsBuildsOverlayPaths");
  CHECK_TEXT(JoinGuests("", "default.xex"), "default.xex");
  CHECK_TEXT(JoinGuests("gen", "x.bin"), "gen\\x.bin");
  CHECK_TEXT(JoinGuests("gen\\", "x.bin"), "gen\\x.bin");
}

void PayloadOnlyRecordReadsThePayloadAndRefusesWrites() {
  BeginCase("PayloadOnlyRecordReadsThePayloadAndRefusesWrites");
  const std::vector<OverlayChild> merged = MergeOverlayChildren({PayloadChild("patch_xbox.hdr", false)}, {});
  CHECK_EQ(merged.size(), 1);
  const OverlayChild* child = Find(merged, "patch_xbox.hdr");
  CHECK_TRUE(child != nullptr);
  if (child == nullptr) {
    return;
  }
  CHECK_TRUE(child->in_payload);
  CHECK_FALSE(child->in_base);
  CHECK_FALSE(child->is_dir());
  CheckBacking(child, OverlayBacking::kPayload, OverlayBacking::kNone);
}

void BaseOnlyRecordReadsAndWritesTheGameRoot() {
  BeginCase("BaseOnlyRecordReadsAndWritesTheGameRoot");
  const std::vector<OverlayChild> merged = MergeOverlayChildren({}, {BaseChild("main_xbox.hdr", false)});
  CHECK_EQ(merged.size(), 1);
  const OverlayChild* child = Find(merged, "main_xbox.hdr");
  CHECK_TRUE(child != nullptr);
  if (child == nullptr) {
    return;
  }
  CHECK_FALSE(child->in_payload);
  CHECK_TRUE(child->in_base);
  CheckBacking(child, OverlayBacking::kBase, OverlayBacking::kBase);
}

void ShadowedFileReadsThePayloadCopy() {
  BeginCase("ShadowedFileReadsThePayloadCopy");
  const std::vector<OverlayChild> merged =
      MergeOverlayChildren({PayloadChild("main_xbox.hdr", false)}, {BaseChild("main_xbox.hdr", false)});
  CHECK_EQ(merged.size(), 1);
  const OverlayChild* child = Find(merged, "main_xbox.hdr");
  CHECK_TRUE(child != nullptr);
  if (child == nullptr) {
    return;
  }
  CHECK_TRUE(child->in_payload);
  CHECK_TRUE(child->in_base);
  CheckBacking(child, OverlayBacking::kPayload, OverlayBacking::kBase);
}

void MergedDirectoryKeepsTheGameRootHandle() {
  BeginCase("MergedDirectoryKeepsTheGameRootHandle");
  const std::vector<OverlayChild> merged =
      MergeOverlayChildren({PayloadChild("gen", true)}, {BaseChild("gen", true)});
  CHECK_EQ(merged.size(), 1);
  const OverlayChild* child = Find(merged, "gen");
  CHECK_TRUE(child != nullptr);
  if (child == nullptr) {
    return;
  }
  CHECK_TRUE(child->is_dir());
  // A payload directory is a wrapper, never a handle: reads and writes both start from
  // the game root, so an install cannot be half-moved under a caller's feet.
  CheckBacking(child, OverlayBacking::kBase, OverlayBacking::kBase);
}

void KindFromThePayloadWinsWhenTheTwoSidesDisagree() {
  BeginCase("KindFromThePayloadWinsWhenTheTwoSidesDisagree");
  // Only reachable if an install put a file where the game root has a directory (or the
  // reverse); the union resolves by the payload's kind and still writes on the game
  // root's record of that name.
  const std::vector<OverlayChild> file_over_dir =
      MergeOverlayChildren({PayloadChild("gen", false)}, {BaseChild("gen", true)});
  CHECK_EQ(file_over_dir.size(), 1);
  CHECK_FALSE(file_over_dir[0].is_dir());
  CheckBacking(&file_over_dir[0], OverlayBacking::kPayload, OverlayBacking::kBase);

  const std::vector<OverlayChild> dir_over_file =
      MergeOverlayChildren({PayloadChild("gen", true)}, {BaseChild("gen", false)});
  CHECK_EQ(dir_over_file.size(), 1);
  CHECK_TRUE(dir_over_file[0].is_dir());
  CheckBacking(&dir_over_file[0], OverlayBacking::kPayload, OverlayBacking::kBase);
}

void MergeDeduplicatesCaseInsensitivelyKeepingThePayloadSpelling() {
  BeginCase("MergeDeduplicatesCaseInsensitivelyKeepingThePayloadSpelling");
  const std::vector<OverlayChild> merged = MergeOverlayChildren({PayloadChild("Patch_Xbox.HDR", false)},
                                                               {BaseChild("patch_xbox.hdr", false)});
  CHECK_EQ(merged.size(), 1);
  CHECK_TEXT(merged[0].name, "Patch_Xbox.HDR");
  CHECK_TRUE(merged[0].in_payload);
  CHECK_TRUE(merged[0].in_base);
}

void MergeDoesNotDependOnListingOrder() {
  BeginCase("MergeDoesNotDependOnListingOrder");
  // Host directory listings arrive in whatever order the file system reports, so the
  // union of the same two sets of records must not depend on that order.
  const std::vector<OverlayChild> payload = {PayloadChild("Gen", true), PayloadChild("Patch.hdr", false)};
  const std::vector<OverlayChild> base = {BaseChild("gen", true), BaseChild("patch.hdr", false),
                                          BaseChild("main_xbox.hdr", false)};
  const std::vector<OverlayChild> shuffled_payload = {payload[1], payload[0]};
  const std::vector<OverlayChild> shuffled_base = {base[2], base[0], base[1]};
  const std::vector<OverlayChild> reference = MergeOverlayChildren(payload, base);
  const std::vector<OverlayChild> reordered = MergeOverlayChildren(shuffled_payload, shuffled_base);
  CHECK_EQ(reference.size(), reordered.size());
  for (size_t i = 0; i < reference.size() && i < reordered.size(); ++i) {
    CHECK_TEXT(reference[i].name, reordered[i].name);
    CHECK_EQ(reference[i].in_payload, reordered[i].in_payload);
    CHECK_EQ(reference[i].in_base, reordered[i].in_base);
    CHECK_EQ(reference[i].is_dir(), reordered[i].is_dir());
  }
}

void MergeSortsByNameSoResolutionAndDumpsAreStable() {
  BeginCase("MergeSortsByNameSoResolutionAndDumpsAreStable");
  const std::vector<OverlayChild> merged =
      MergeOverlayChildren({PayloadChild("Zed.bin", false), PayloadChild("gen", true)},
                           {BaseChild("abc.bin", false), BaseChild("Gen", true)});
  const std::vector<std::string> expected = {"abc.bin", "gen", "Zed.bin"};
  CHECK_EQ(merged.size(), expected.size());
  const std::vector<std::string> actual = Names(merged);
  CHECK_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < actual.size() && i < expected.size(); ++i) {
    CHECK_TEXT(actual[i], expected[i]);
  }
}

void MergeOfNothingIsNothing() {
  BeginCase("MergeOfNothingIsNothing");
  CHECK_EQ(MergeOverlayChildren({}, {}).size(), 0);
}

void HiddenPathsMatchCaseAndSeparatorSpelling() {
  BeginCase("HiddenPathsMatchCaseAndSeparatorSpelling");
  const std::vector<std::string> hidden = {"default.xex"};
  CHECK_TRUE(IsHiddenPath(hidden, "Default.XEX"));
  CHECK_TRUE(IsHiddenPath(hidden, "\\default.xex"));
  CHECK_TRUE(IsHiddenPath(hidden, "default.xex/"));
  CHECK_FALSE(IsHiddenPath(hidden, "gen/default.xex"));
  CHECK_FALSE(IsHiddenPath(hidden, "default.xex.bak"));
  CHECK_FALSE(IsHiddenPath({}, "default.xex"));
}

void NestedHiddenPathsAndTheirSubtreesStayOut() {
  BeginCase("NestedHiddenPathsAndTheirSubtreesStayOut");
  size_t hidden_count = 0;
  const std::vector<OverlayChild> top =
      VisiblePayloadChildren({PayloadChild("gen", true), PayloadChild("readme.txt", false)}, "",
                             {"gen"}, &hidden_count);
  CHECK_EQ(hidden_count, 1);
  CHECK_EQ(top.size(), 1);
  if (top.size() != 1) {
    return;
  }
  CHECK_TEXT(top[0].name, "readme.txt");

  // Nothing ever asks for the children of gen: the merge recurses only through the
  // records it kept, so the whole subtree is gone with the one match.
  size_t nested_hidden = 0;
  const std::vector<OverlayChild> nested = VisiblePayloadChildren(
      {PayloadChild("inner.bin", false), PayloadChild("other.bin", false)}, "gen", {"gen/inner.bin"},
      &nested_hidden);
  CHECK_EQ(nested_hidden, 1);
  CHECK_EQ(nested.size(), 1);
  if (nested.size() == 1) {
    CHECK_TEXT(nested[0].name, "other.bin");
  }

  size_t untouched = 0;
  const std::vector<OverlayChild> kept =
      VisiblePayloadChildren({PayloadChild("default.xex", false)}, "", {}, &untouched);
  CHECK_EQ(untouched, 0);
  CHECK_EQ(kept.size(), 1);
}

void BackingDecisionsCardinality() {
  BeginCase("BackingDecisionsCardinality");
  // A record that reached neither side cannot have come from a merge, but the rule must
  // still be total.
  OverlayChild neither = PayloadChild("x", false);
  neither.in_payload = false;
  CHECK_EQ(static_cast<uint64_t>(ReadBackingFor(neither)), static_cast<uint64_t>(OverlayBacking::kNone));
  CHECK_EQ(static_cast<uint64_t>(WriteBackingFor(neither)), static_cast<uint64_t>(OverlayBacking::kNone));
}

}  // namespace

int main() {
  FoldNameFoldsAsciiAndLeavesOtherBytes();
  FoldGuestPathDropsCaseAndSeparatorSpelling();
  JoinGuestsBuildsOverlayPaths();
  PayloadOnlyRecordReadsThePayloadAndRefusesWrites();
  BaseOnlyRecordReadsAndWritesTheGameRoot();
  ShadowedFileReadsThePayloadCopy();
  MergedDirectoryKeepsTheGameRootHandle();
  KindFromThePayloadWinsWhenTheTwoSidesDisagree();
  MergeDeduplicatesCaseInsensitivelyKeepingThePayloadSpelling();
  MergeDoesNotDependOnListingOrder();
  MergeSortsByNameSoResolutionAndDumpsAreStable();
  MergeOfNothingIsNothing();
  HiddenPathsMatchCaseAndSeparatorSpelling();
  NestedHiddenPathsAndTheirSubtreesStayOut();
  BackingDecisionsCardinality();
  return rb_blitz::test::Finish();
}
