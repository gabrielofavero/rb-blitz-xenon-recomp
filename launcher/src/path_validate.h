// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The schema's `validate` field, enforced (docs/plans/launcher-plan.md Contract 1, B1).
//
// A path row carries the rules its value must satisfy, so the tab does not have to know what
// "a DLC folder" or "not inside the game root" mean: this module does, and it is the same
// rule set the runtime uses - src/fs/dlc_layout.h for the layout and src/fs/path_policy.h for
// the game-root question (D4's "same rule, same wording, one implementation").
//
// Dependency-free on purpose - no ImGui, no SDL, no SDK - so the refusals are unit-tested
// against directory fixtures rather than clicked through.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rb_blitz::launcher {

struct PathVerdict {
  bool ok = true;
  std::string reason;  // why not, when !ok - shown under the row
  std::string note;    // honest extra detail when ok (e.g. "no packages found yet")
};

// Applies the '|'-separated rules of a row's `validate` to `value`. The rules the table uses
// today, and everything else is reported rather than ignored:
//   exists                  the path has to be there
//   dlc_layout              either root src/hooks/dlc.cpp mounts: the structured
//                           <title_id>/<content_type>/<package> tree (refused with
//                           src/fs/dlc_layout.h's own wording), or a flat library of loose
//                           containers under it, which the runtime reads where it lies
//                           (src/fs/dlc_library.h), so a dumped song folder is accepted
//   inside_game_root:forbid refused when it sits inside the game root, with the reason
//                           src/fs/path_policy.h exists for (the runtime would redirect it
//                           to the platform user folder, so the row would silently do
//                           something else - D4)
// An empty value is always accepted: it means "the game's own default", which is what the
// path rows document.
PathVerdict ValidatePathValue(std::string_view rules, const std::filesystem::path& value,
                              const std::filesystem::path& game_root);

}  // namespace rb_blitz::launcher
