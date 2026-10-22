// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "settings/ultimate_adapter.h"
#include "ui/dtb_author.h"

namespace rb_blitz::settings {
using RecordReader = std::function<bool(const std::string&, std::vector<uint8_t>*, std::string*)>;
using MenuRecords = std::map<std::string, std::vector<uint8_t>>;

// Read through the mounted archive view, so provider edits survive the extension.
//
// `notes`, when given, receives one line per *optional* edit that was requested and refused - a
// main-menu row, the store's server rewrite, the credit line - with the authoring side's own
// reason. An optional refusal is not a boot failure, but it is a difference from what the boot
// configured, and the log is the only place that can say so: the caller's boot line knows the
// request, not the outcome (measured 2026-10-20: P1's row was reported "built" while the menu did
// not draw it).
bool BuildMenuRecords(const RecordReader& read, const std::vector<std::string>& locales,
                      MenuRecords* records, std::string* error, bool merge_provider = false,
                      bool cached_dlc_list = false, bool refresh_row_active = false,
                      std::vector<std::string>* notes = nullptr,
                      const std::vector<std::string>& hidden_menu_rows = {});
// Applied to the complete generated locale record, before it is archived. This
// path has no split-read length constraint and serves both installed providers.
bool SetDiscoveryCacheLabel(std::vector<ui::dtb::Node>* roots, bool cache_hit);
// Applied the same way, for the main menu's downloadable-content row: with R7 on the row *is*
// the library refresh (`TapRefreshCache`), so the label it draws has to say so. Built into the
// locale record here rather than left to the in-flight menu_filter relabel, which cannot reach a
// record spanning more than two archive reads (measured 2026-10-15: with a provider installed the
// 87 KB English locale sits across three read blocks and the relabel was silently refused).
bool SetRefreshRowLabel(std::vector<ui::dtb::Node>* roots, bool refresh_row_active);// The recompilation project's credit line, inserted into the credits DTA the title loads. The
// DTA is the list the credits screen renders (`credits_file` in `config/gen/sync_keep.dtb`
// points at it): the retail file carries the official sections starting with "Core Development
// Team", and the Ultimate payload's own copy starts with its "Rock Band Blitz Ultimate 2.0
// Development Team" heading followed by one "Only Dev" row.
//
// Default (retail file): a "Recompilation Project" heading and a "Developer" / "Gabriel Fávero"
// row are inserted before the first official section, then a "Thanks To" section naming what the
// build stands on (ReXGlue, XenonRecomp, Xenia, band3_recomp, RhythmVerse, MiloLib - one table
// in the implementation, so the two credit files cannot drift), separated from it by blank rows.
// Ultimate (the payload's file): the Ultimate heading is renamed to "Recompilation Project &
// Ultimate 2.0 Mod" and its one row becomes two - "Recompilation Developer" / "Gabriel Fávero"
// and "Ultimate 2.0 Developer" / "Synchrony" - followed by the same "Thanks To" section.
//
// A file whose first non-empty section is not one of those two headings is left alone (false):
// a mod release may change what its credits say, and that must not turn into a boot failure.
bool AddRecompilationCredits(std::vector<ui::dtb::Node>* roots, bool ultimate, std::string* why);// The same row's *action*, built into the payload's own copy of the splash record
// (`ui/splash/gen/splash.dtb`; the in-flight twin is menu_filter's `TapRefreshCache`). Two sibling
// statements - the title's connect/discovery screen, then the `file_exists` the host answers by
// re-scanning - with no byte budget: the payload's record may grow, so both always fit and nothing
// is padded. The overlay prefers this copy, and the in-flight edit cannot be relied on for it
// (measured 2026-10-15: with a provider installed the press still ran the title's own store body
// while every read that carried the row was rewritten - out/_swap/menu-run/refresh14). False when
// the flag is off, when the roots hold no such case, or when it already carries the sentinel.
bool SetRefreshRowAction(std::vector<ui::dtb::Node>* roots, bool refresh_row_active);
// Extend an existing patch index with our records. Existing records retain their offsets,
// compression and checksums, and the ark keeps the part count it had: when the mounted index has a
// single part its bytes are carried into the new one (`mounted_part`) and only the last records
// move, which is what keeps the archive shaped like every archive this title reads. With no
// mounted part (the base game), our own part is appended instead. `part_path` receives the record
// path to write `part` to. No installed archive is modified on disk.
std::vector<uint8_t> ExtendMenuArchive(ultimate_adapter::ArkIndex index, const MenuRecords& records,
                                       const std::vector<uint8_t>& mounted_part,
                                       std::string* part_path, std::vector<uint8_t>* part);
}
