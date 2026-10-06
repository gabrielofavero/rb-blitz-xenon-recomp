// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The main menu's option list, edited as data (docs/engine/main-menu-flow.md).
//
// The list is a compiled DTA file - `ui/splash/gen/splash.dtb` - and the title
// compiles it again at load, so the *shape* of an edit is what the title accepts
// or refuses. The shape this module writes is the one the title is known to
// accept, and it is byte-neutral on purpose: the ark index describes every
// entry's offset and size, so a lost or gained byte invalidates the whole
// archive a patched file is read out of. See docs/assets.md "dtb".
//
// What the file looks like (the whole grammar this module needs):
//
//   file := [u32 seed][body]                       body is xor'd with the seed's
//                                                  Park-Miller keystream
//   body := 0x01 [u16 root_arity] [u16 line] [u16 deprecated] node*
//   node := [u32 tag] payload
//     tag 0x00 int, 0x01 float, 0x06 unhandled, 0x08 else, 0x09 endif,
//         0x24 autorun                                  -> [u32 value]
//     tag 0x02 var, 0x03 func, 0x04 object, 0x05 symbol,
//         0x07 ifdef, 0x12 string, 0x20 define, 0x21 include,
//         0x22 merge, 0x23 ifndef, 0x25 undef              -> [u32 len][bytes]
//     tag 0x10 array, 0x11 command, 0x13 property          -> [u16 arity][u16 line]
//                                                             [u16 deprecated] node*
//
// `#ifdef NAME ... #endif` are ordinary nodes in that stream, so an array may
// carry a conditional element list. The title resolves them while it loads the
// file: an `#ifdef` whose NAME is not a defined macro makes the loader *skip*
// every following element up to the `#endif` (rb3 `DataArray::Load`,
// `DataArray::Conditional`). That is the lever this module uses twice: a row
// that disappears, and bytes that come back.
//
// The three menu rows that only ever worked online - the leaderboard, the
// achievement list and the downloadable-content browser - are hidden by
// dropping them from the option array. The bytes they take with them are put
// back as extra characters in the name of the array's own `#ifdef`, whose macro
// is undefined once it grows, which makes that conditional drop its (empty)
// block and costs the menu nothing else.
//
// The same idea, read the other way, is how a *label* is relabelled: the Ultimate
// mod's settings row is drawn from a locale value ("Mod Settings"), and a longer
// label needs bytes this file can only give up in one place - the name of an
// `#ifdef` whose macro the build does not define (the file's strings for the
// other platform). A name that matches no macro keeps matching none when it is
// shortened, so the branch the guest takes is the same one it took before.
//
// SDK-free on purpose: tests/menu_options_tests.cpp exercises it against a
// synthetic fixture, with no boot and no retail content (D14).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::menu_options {

// The rows the game cannot serve offline: the leaderboard, the achievement
// list, and the downloadable-content browser. This is the compiled default of
// `enhancements_hidden_menu_options`, so an unconfigured run hides these.
inline constexpr char kDefaultRows[] = "splash_leaderboard,splash_achievements,splash_dlc";

// The Ultimate mod's own settings row: the key its locale entry is filed under,
// the label it ships with, and the one this build draws instead
// (`enhancements_rename_mod_settings`). The row is the mod's, so a run without the
// payload has neither the file nor the label, and the edit never happens.
inline constexpr char kModSettingsKey[] = "mod_settings";
inline constexpr char kModSettingsLabel[] = "Mod Settings";
inline constexpr char kUltimateSettingsLabel[] = "Ultimate Settings";

// What one call did, in the terms a log line and the tests both need.
struct Outcome {
  bool applied = false;      // the file was rewritten
  size_t file_size = 0;      // the .dtb's own length, 4-byte seed included
  size_t freed = 0;          // bytes the removed rows took
  std::vector<std::string> removed;  // the rows that were found and removed
  // SHA-1 of the file as stored, before and after the edit. The caller needs
  // both to correct the title's content database, which holds a digest per file
  // and refuses a file that does not match it. Empty when nothing was written.
  uint8_t digest_before[20] = {};
  uint8_t digest_after[20] = {};
  std::string reason;        // why nothing was written, when applied is false
};

// True when `file` is the start of a .dtb this module can rewrite: the title's
// own file header after the seed, bought cheaply because the caller checks a
// 64 KiB ark read for it.
bool LooksLikeDtb(const uint8_t* file, size_t available);

// Splits a comma-separated row list ("a, b ,c") into its names, dropping empty
// entries and trimming ASCII space around each. Order is kept.
std::vector<std::string> ParseRowNames(std::string_view text);

// Removes `names` from the main menu's option array, in place, keeping the
// file's length. `available` is how many bytes of the file the caller has, seed
// included; the file's real length comes from its own header, so a .dtb that
// sits inside a larger read works too.
//
// Nothing is written unless the whole edit validates: the file must parse and
// reserialize to the bytes it came in as, the option array must be the menu's,
// the array must carry the `#ifdef` element the freed bytes are paid back with,
// and the result must be exactly as long as the input.
Outcome HideRows(uint8_t* file, size_t available, const std::vector<std::string>& names);

// What one RenameLabel call did, in the same terms.
struct Renamed {
  bool applied = false;   // the file was rewritten
  size_t file_size = 0;   // the .dtb's own length, 4-byte seed included
  // Bytes the unused macro name gave up, i.e. how much longer the new label is.
  // Zero when the two labels are the same length, which needs no donor.
  int32_t borrowed = 0;
  std::string from;  // the label that was there
  std::string to;    // the label that is there now
  uint8_t digest_before[20] = {};
  uint8_t digest_after[20] = {};
  std::string reason;  // why nothing was written, when applied is false
};

// Rewrites the locale label `from` of the entry filed under `key` to `to`, in
// place, keeping the file's length. A longer label takes the bytes it needs from
// the name of an `#ifdef` that guards this build's other-platform strings: that
// macro matches nothing here, so the conditional is skipped before and after the
// name changes, and no branch decision moves.
//
// Refuses the file unless the entry is there, the name is long enough to pay with
// (or the label is not longer at all), and the rewrite balances.
Renamed RenameLabel(uint8_t* file, size_t available, std::string_view key,
                    std::string_view from, std::string_view to);

// SHA-1 of `size` bytes, as the title's own content checksum uses it (the value
// a patched file's database row has to be given so the title still recognises
// it). Written to `out`, 20 bytes.
void Sha1Digest(const uint8_t* data, size_t size, uint8_t out[20]);

// The codec's other direction, for a caller that builds a .dtb rather than
// edits one (the tests do): `out` receives the 4-byte seed followed by `body`
// xored with that seed's keystream, which is exactly how the title stores the
// file.
void EncodeFile(uint32_t seed, const uint8_t* body, size_t size, uint8_t* out);

}  // namespace rb_blitz::menu_options
