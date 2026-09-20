// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Rules of the Rock Band Blitz Ultimate payload overlay (src/fs/payload_overlay.h):
// how the payload directory and the game root are unioned, which side a read or a
// write of a merged record goes to, and which payload records stay hidden.
//
// Kept free of any SDK dependency for the same reason src/hooks/crypto_keytable.h
// is: tests/payload_overlay_tests.cpp has to cover these rules on the host, without
// booting the game (docs/rb3-references.md §7.3).

#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb_blitz::fs {

// ASCII case folding, matching the SDK's rex::string::utf8_lower_ascii() and so the
// case-insensitive name lookups the guest file system does. Bytes outside ASCII are
// left alone and therefore compare byte-wise; the payload has no such names.
inline std::string FoldName(const std::string_view name) {
  std::string folded(name);
  for (char& c : folded) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return folded;
}

// Guest paths arrive both as "gen/patch_xbox.hdr" (from callers and cvars) and as
// "gen\patch_xbox.hdr" (assembled from Entry::path() parts), so paths are compared
// with either separator and without a leading or trailing one.
inline std::string FoldGuestPath(const std::string_view path) {
  std::string folded;
  folded.reserve(path.size());
  for (char c : path) {
    if (c == '/') {
      c = '\\';
    }
    if (c == '\\' && folded.empty()) {
      continue;
    }
    folded.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  }
  if (!folded.empty() && folded.back() == '\\') {
    folded.pop_back();
  }
  return folded;
}

// Same rule as rex::string::utf8_join_guest_paths: an empty left side leaves the right
// side alone, so joining the root's path ("") with a name is the name.
inline std::string JoinGuests(const std::string_view left, const std::string_view right) {
  if (left.empty()) {
    return std::string(right);
  }
  std::string joined(left);
  if (joined.back() != '\\') {
    joined.push_back('\\');
  }
  joined.append(right);
  return joined;
}

// One record of a merged directory listing. The merge fills in both sides' flags, so
// a caller only describes the side its listing came from, through PayloadChild() for
// the payload listing and BaseChild() for the game root one.
struct OverlayChild {
  std::string name;  // the payload's spelling when both sides have the record
  bool in_payload = false;
  bool in_base = false;
  bool payload_is_dir = false;
  bool base_is_dir = false;

  bool in_both() const { return in_payload && in_base; }
  // The payload copy wins when both sides have the name, so the effective kind is the
  // payload's.
  bool is_dir() const { return in_payload ? payload_is_dir : base_is_dir; }
};

inline OverlayChild PayloadChild(std::string name, bool is_dir) {
  OverlayChild child;
  child.name = std::move(name);
  child.in_payload = true;
  child.payload_is_dir = is_dir;
  return child;
}

inline OverlayChild BaseChild(std::string name, bool is_dir) {
  OverlayChild child;
  child.name = std::move(name);
  child.in_base = true;
  child.base_is_dir = is_dir;
  return child;
}

// Union of one payload listing and one game root listing. A name only one side has
// becomes one record for that side, a name both sides have becomes one record carrying
// both (matched case-insensitively, since the two sides are separate directories that
// need not agree on case), and the result is sorted by folded name because the host
// listings arrive in whatever order the host file system reported. The sort is what
// makes a boot walk the merged tree in a fixed order.
inline std::vector<OverlayChild> MergeOverlayChildren(
    const std::vector<OverlayChild>& payload, const std::vector<OverlayChild>& base) {
  std::vector<OverlayChild> merged;
  merged.reserve(payload.size() + base.size());
  auto find = [&merged](const std::string_view name) -> OverlayChild* {
    const std::string folded = FoldName(name);
    for (OverlayChild& child : merged) {
      if (FoldName(child.name) == folded) {
        return &child;
      }
    }
    return nullptr;
  };
  for (const OverlayChild& child : payload) {
    if (OverlayChild* existing = find(child.name); existing != nullptr) {
      existing->in_payload = true;
      existing->payload_is_dir = child.payload_is_dir;
    } else {
      merged.push_back(child);
    }
  }
  for (const OverlayChild& child : base) {
    if (OverlayChild* existing = find(child.name); existing != nullptr) {
      existing->in_base = true;
      existing->base_is_dir = child.base_is_dir;
    } else {
      merged.push_back(child);
    }
  }
  std::sort(merged.begin(), merged.end(),
            [](const OverlayChild& left, const OverlayChild& right) {
              const std::string left_folded = FoldName(left.name);
              const std::string right_folded = FoldName(right.name);
              if (left_folded != right_folded) {
                return left_folded < right_folded;
              }
              return left.name < right.name;
            });
  return merged;
}

// Where a record's data is read from: the payload copy when it exists, which is what
// installing the mod means, with one exception - a directory the game root also has
// keeps the game root's handle. A host directory handle is what the guest's directory
// enumeration and any game root write are bound to, and this title never opens one
// (every NtCreateFile in the boot traces is a non-directory open), so the game root
// stays in charge of directories.
enum class OverlayBacking { kNone, kPayload, kBase };

inline OverlayBacking ReadBackingFor(const OverlayChild& child) {
  if (!child.in_payload) {
    return child.in_base ? OverlayBacking::kBase : OverlayBacking::kNone;
  }
  if (child.in_base && child.payload_is_dir && child.base_is_dir) {
    return OverlayBacking::kBase;
  }
  return OverlayBacking::kPayload;
}

// Writes only ever land in the game root: the payload directory is mounted read-only
// on purpose, so the game cannot damage it and deleting the folder stays a complete
// uninstall. A record the game root does not have is therefore not writable at all.
inline OverlayBacking WriteBackingFor(const OverlayChild& child) {
  return child.in_base ? OverlayBacking::kBase : OverlayBacking::kNone;
}

// Matches an overlay-relative path ("default.xex", "gen\foo.xex") against the hide
// list. Hidden payload records never join the union; a hidden directory takes its whole
// subtree with it, which is what VisiblePayloadChildren() below implements, because the
// merge of a directory is the only place its children are ever named.
inline bool IsHiddenPath(const std::vector<std::string>& hidden,
                         const std::string_view relative_path) {
  const std::string folded = FoldGuestPath(relative_path);
  for (const std::string& entry : hidden) {
    if (FoldGuestPath(entry) == folded) {
      return true;
    }
  }
  return false;
}

// The payload records of the directory at `parent_path` that join the union. Dropping a
// hidden directory here is what removes everything below it: the records of that
// directory are collected one level lower, and that level is never reached.
inline std::vector<OverlayChild> VisiblePayloadChildren(
    const std::vector<OverlayChild>& payload, const std::string_view parent_path,
    const std::vector<std::string>& hidden, size_t* hidden_count = nullptr) {
  std::vector<OverlayChild> visible;
  visible.reserve(payload.size());
  for (const OverlayChild& child : payload) {
    if (IsHiddenPath(hidden, JoinGuests(parent_path, child.name))) {
      if (hidden_count != nullptr) {
        ++*hidden_count;
      }
      continue;
    }
    visible.push_back(child);
  }
  return visible;
}

}  // namespace rb_blitz::fs
