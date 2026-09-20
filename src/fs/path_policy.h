// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// One question about the writable roots src/rb_blitz_app.h configures: does a
// directory the launcher asked for sit inside the read-only game root? Kept free of
// any SDK dependency for the same reason src/fs/overlay_merge.h is - it is pure host
// path arithmetic, and tests/path_policy_tests.cpp has to cover it without booting
// the game (docs/rb3-references.md §7.3).

#pragma once

#include <cwctype>
#include <filesystem>
#include <string>

namespace rb_blitz::fs {

// Path elements are compared the way the file system compares them: case-insensitive
// on Windows ("D:\Game" and "d:\game" are the same directory), byte-wise elsewhere.
inline bool SamePathElement(const std::filesystem::path& lhs,
                            const std::filesystem::path& rhs) {
#ifdef _WIN32
  const std::wstring a = lhs.native();
  const std::wstring b = rhs.native();
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::towlower(a[i]) != std::towlower(b[i])) {
      return false;
    }
  }
  return true;
#else
  return lhs == rhs;
#endif
}

// True when `candidate` is `root` or a strict descendant of it. Both sides are
// resolved with weakly_canonical() first, so "." / ".." segments, a trailing
// separator and a differently spelled existing path cannot hide a match.
//
// std::filesystem::relative() looks like the obvious way to ask this and is the
// wrong one: when the two paths are unrelated - a sibling directory, another drive -
// the LLVM-clang-on-Windows build of it used here returns an *empty* path with no
// error set, so a caller reasoning "no leading .. segment, therefore inside" answers
// yes for every directory outside the game root, and silently discards the override
// it was asked to check (docs/bringup-log.md, 2026-09-21). Walking the elements has
// no such case.
//
// An empty candidate or root, or a path that cannot be made absolute, answers false:
// the caller keeps the launcher's override instead of relocating writable state
// behind its back.
inline bool IsSameOrInside(const std::filesystem::path& candidate,
                           const std::filesystem::path& root) {
  if (candidate.empty() || root.empty()) {
    return false;
  }

  std::error_code ec;
  const std::filesystem::path abs_root = std::filesystem::absolute(root, ec);
  if (ec || abs_root.empty()) {
    return false;
  }
  const std::filesystem::path abs_candidate = std::filesystem::absolute(candidate, ec);
  if (ec || abs_candidate.empty()) {
    return false;
  }
  const std::filesystem::path canon_root = std::filesystem::weakly_canonical(abs_root, ec);
  if (ec || canon_root.empty()) {
    return false;
  }
  const std::filesystem::path canon_candidate = std::filesystem::weakly_canonical(abs_candidate, ec);
  if (ec || canon_candidate.empty()) {
    return false;
  }

  auto c = canon_candidate.begin();
  for (auto r = canon_root.begin(); r != canon_root.end(); ++r) {
    if (c == canon_candidate.end() || !SamePathElement(*c, *r)) {
      return false;
    }
    ++c;
  }
  return true;
}

}  // namespace rb_blitz::fs
