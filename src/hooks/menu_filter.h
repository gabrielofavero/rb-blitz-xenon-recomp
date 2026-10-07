// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R5: hiding the main menu's offline-dead rows, R10: naming the Ultimate mod's own
// settings row, and R3: answering the offline-mode prompts (docs/engine/
// main-menu-flow.md, docs/engine/toggles.md).
//
// The main menu's option list lives in `ui/splash/gen/splash.dtb`, the panel a
// start goes through in `ui/net/gen/server_connect.dtb`, and the two labels R7
// rewrites in `ui/locale/eng/gen/locale_keep.dtb` - all read out of an ark in 64 KiB
// blocks and then compiled by the title into its own DTA objects. A block is therefore
// where an edit is made without touching the player's copy of the game - and the edit
// has to keep the file's length, because the ark index describes offsets and sizes
// (src/ui/menu_options.h). The locale is larger than one block, so a candidate that a
// read cuts short is remembered and patched on the read that continues it: the ark's
// blocks land straight after one another in guest memory, measured in the boot trace.
//
// Two things are checked on every read:
//
//   * whether the block carries a file these edits are for, by the file's own
//     header first and then by what it defines (src/ui/menu_options.cpp), and
//   * whether that file is one the title checks against its content database.
//
// The second decides what a patch owes: the title's database holds a {name,
// sha1} row per content file and calls `PlatformMgr::SetDiskError` when a file
// does not match, which black-screens the boot. The payload's splash.dtb is not
// in it (the mod's files are read through the update/content device, which the
// database does not cover), while the retail game's is. So a patched file whose
// *original* digest is found in the database has that row retargeted to the
// digest of the patched bytes, which keeps "the file on disk is the file the
// title knows" true instead of switching the check off. A file whose digest is
// not in the database is patched as it is.
//
// Faithful behaviour is a toggle being off: nothing is read, nothing is rewritten,
// and each toggle logs its own state at boot. Two default on - R10 and R3, whose
// subjects are the mod's own name and the two questions a start otherwise asks - and
// their defaults are stated where they are defined (src/enhancements.cpp).

#pragma once

namespace rb_blitz::menu_filter {

// Logs what the filter will do, once, after the toggles are final: which edits
// are on and which rows the menu one was told to hide. The row list is the one
// thing a user can get wrong, so it is echoed in the boot log. Also the point
// where the cvars are read: the read hook itself must not touch the registry per
// read.
void Configure();

}  // namespace rb_blitz::menu_filter
