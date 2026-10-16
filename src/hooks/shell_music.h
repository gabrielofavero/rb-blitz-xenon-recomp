// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R9: the main menu's background music, drawn from the loaded DLC instead of the
// three tracks the title ships. See src/hooks/shell_music.cpp for the flow and
// docs/engine/main-menu.md (part 2, the background music) for the evidence.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// `PPCContext` for StandDownForRefresh's signature: the SDK's own context type, the same one every
// hook in this tree takes (rex/hook.h pulls this header in).
#include <rex/ppc/context.h>

namespace rex {
class Runtime;
}  // namespace rex

namespace rb_blitz::shell_music {

// Hooks the engine's shell-music loader (0x82757F50) so that, with R9 on and a DLC
// package mounted, every track the menu asks for is one of the loaded DLC songs rather
// than the title's own three. The packages are mounted read-only from
// rb_blitz::dlc::LibraryPackagePaths(), so nothing is copied and nothing under game/ is
// written.
//
// Call once from RbBlitzApp::OnPostLoadXexImage(), after dlc::Configure(): it reads the
// library that layer just enumerated, mounts the chosen packages, and leaves the guest
// untouched. With the toggle off - its default - it logs and returns, and the hook is
// inert, so a boot without R9 is the boot this project had before the hook existed.
void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root);

// --- the "Play Current Song" row's view of this layer (owner feature, 2026-10-14) --------------
// The main menu's second entry starts whichever song the menu is playing, so it needs the facts
// below at two moments: the payload builder bakes the pool - and the probes that name it - into the
// menu record at boot, and the probe channel (`AnswerPlayQuery`) answers, once per press, which one
// is playing.
//
// `MenuMusicEnabled` is the setting's own value (the revived `general.dlc_menu_songs` row, read
// from the settings store in Configure()), and `HasPool` is whether that setting produced a pool.
// They differ exactly when the setting is on and nothing could be mounted (no library, or no
// package holds a stream) - the state the settings screen reports as unavailable, and the reason
// the Play Current Song row is absent from a menu whose background music is the title's own.

// Whether the "DLC Menu Songs" setting asked for the DLC pool this boot.
bool MenuMusicEnabled();

// True when this boot really has a pool (the setting on *and* at least one song mounted).
bool HasPool();

// The pool's songs, in pool order, as the song key the title's own song manager knows them by
// (from package metadata, or the stream basename when metadata is absent). Empty with no pool.
std::vector<std::string> PoolSongNames();

// The pool index whose stream is audibly advancing, or -1 during loading and with no pool.
int CurrentPoolIndex();

// The host's answer for one `rbbz-play/<...>` query path - what the menu's Play Current Song row
// asks through `file_exists`:
//   `rbbz-play/begin` - snapshot a selection (audible, recently heard, or pending at startup).
//   `rbbz-play/current/<i>` - true for the selected index, stable throughout one press.
//   `rbbz-play/result/<reason>` - record guest lookup success/failure and end the snapshot.
// Returns false (and leaves `*answer` alone) for a path this layer does not own, so the caller
// keeps forwarding it to the original.
bool AnswerPlayQuery(std::string_view path, bool* answer);

// R7's refresh press takes the menu's current track down *before* it blocks (owner report,
// 2026-10-16). The press waits on the guest thread while the library scan catches up
// (src/hooks/dlc.cpp's bounded wait), and a stream left playing while nothing advances it makes
// the audio device repeat its last buffered slice - the owner heard it as a stuck record. Tearing
// the stream down through the same path a screen change uses (its own destructor, then
// StopStreams) leaves nothing to repeat; the menu's own re-entry reloads the pool when the pass
// returns, and because the interrupted song counts as played the rotation hands over the *next*
// one rather than replaying what was cut.
//
// Runs on the guest thread that answers the press, inside the file hook that recognises the
// refresh sentinel (`src/hooks/dlc_refresh.cpp`); `base` and `ctx` are that thread's. Returns true
// when a stream was really torn down, false when the menu had nothing playing (or nothing has ever
// been polled - the menu is not up).
bool StandDownForRefresh(uint8_t* base, PPCContext& ctx);

}  // namespace rb_blitz::shell_music
