// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the writable-root policy in src/rb_blitz_app.h::OnConfigurePaths,
// which src/fs/path_policy.h holds the decidable part of. No SDK, no game image, no
// boot: the question is pure host path arithmetic.
//
// What is pinned here is that only a directory *inside* the read-only game root is
// refused. The distinction matters because the two plausible ways to answer it
// disagree: the std::filesystem::relative()-based answer says "yes, inside" for any
// directory that is not a descendant of the game root - a sibling of it, another
// drive - and every writable root named that way (--user_data_root, --cache_root,
// REX_USER_DATA_ROOT) was then discarded and replaced by the default under
// Documents (docs/bringup-log.md, 2026-09-21).

#include "check.h"

#include "fs/path_policy.h"

#include <cwctype>
#include <filesystem>
#include <cstdio>
#include <string>

namespace {

using namespace rb_blitz::fs;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

// A scratch tree with the relationships that matter:
//
//   <temp>/rb_blitz-path-policy/root           the stand-in for game_data_root
//   <temp>/rb_blitz-path-policy/root/user/save inside it (a descendant)
//   <temp>/rb_blitz-path-policy/sibling        next to it (the case that broke)
//   <temp>/rb_blitz-path-policy                its parent
//
// The directories are created for real, so weakly_canonical() resolves them the way
// it resolves a launcher's actual path rather than lexically by accident.
struct Scratch {
  fs::path base;
  fs::path root;
  fs::path nested;
  fs::path sibling;

  Scratch() {
    std::error_code ec;
    base = fs::temp_directory_path(ec) / "rb_blitz-path-policy";
    fs::remove_all(base, ec);
    root = base / "root";
    nested = root / "user" / "save";
    sibling = base / "sibling";
    fs::create_directories(nested, ec);
    fs::create_directories(sibling, ec);
  }

  ~Scratch() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }
};

#ifdef _WIN32
fs::path UpperCased(const fs::path& p) {
  std::wstring text = p.native();
  for (wchar_t& c : text) {
    c = static_cast<wchar_t>(std::towupper(c));
  }
  return fs::path(text);
}
#endif

}  // namespace

int main() {
  BeginCase("the game root itself is inside");
  {
    Scratch s;
    CHECK_TRUE(IsSameOrInside(s.root, s.root));
  }

  BeginCase("descendants are inside");
  {
    Scratch s;
    CHECK_TRUE(IsSameOrInside(s.nested, s.root));
    CHECK_TRUE(IsSameOrInside(s.root / "user", s.root));
    CHECK_TRUE(IsSameOrInside(s.root / "not" / "created" / "yet", s.root));
  }

  BeginCase("spelling does not change the answer");
  {
    Scratch s;
    CHECK_TRUE(IsSameOrInside(s.root / "user/./save/", s.root));
    CHECK_TRUE(IsSameOrInside(s.root.generic_string() + "\\user\\save", s.root));
    CHECK_TRUE(IsSameOrInside(s.nested / ".." / "save", s.root));
#ifdef _WIN32
    CHECK_TRUE(IsSameOrInside(UpperCased(s.root), s.root));
    CHECK_TRUE(IsSameOrInside(UpperCased(s.nested), s.root));
#endif
  }

  BeginCase("directories outside the game root are not inside");
  {
    Scratch s;
    // The case that was broken: a directory next to the game root, on the same
    // drive, which std::filesystem::relative() cannot express as a relative path.
    CHECK_FALSE(IsSameOrInside(s.sibling, s.root));
    CHECK_FALSE(IsSameOrInside(s.base / "root.save", s.root));
    CHECK_FALSE(IsSameOrInside(s.root / ".." / "sibling", s.root));
    // The game root's own ancestors, including the drive root.
    CHECK_FALSE(IsSameOrInside(s.base, s.root));
    CHECK_FALSE(IsSameOrInside(s.base.parent_path(), s.root));
    CHECK_FALSE(IsSameOrInside(s.root.root_path(), s.root));
  }

  BeginCase("another drive is not inside");
  {
    Scratch s;
    std::error_code ec;
    const fs::path other_drive =
        s.root.root_name().native() == L"C:" ? fs::path("D:\\") : fs::path("C:\\");
    if (fs::exists(other_drive, ec) && !ec) {
      CHECK_FALSE(IsSameOrInside(other_drive, s.root));
      CHECK_FALSE(IsSameOrInside(other_drive / "rb_blitz", s.root));
    } else {
      std::printf("[ SKIP ] this host has no second drive\n");
    }
  }

  BeginCase("nothing to compare answers not inside");
  {
    Scratch s;
    CHECK_FALSE(IsSameOrInside(fs::path(), s.root));
    CHECK_FALSE(IsSameOrInside(s.root, fs::path()));
    CHECK_FALSE(IsSameOrInside(fs::path(), fs::path()));
  }

  return Finish();
}
