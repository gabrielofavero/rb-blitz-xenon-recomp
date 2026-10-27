// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R9: the main menu's background music, drawn from bundled songs and loaded DLC
// instead of the title's three shell tracks. See src/hooks/shell_music.cpp for the flow and
// docs/engine/main-menu.md (part 2, the background music) for the evidence.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include "ui/menu_music_artwork.h"

namespace rex {
class Runtime;
}  // namespace rex

namespace rb_blitz::shell_music {

// Hooks the engine's shell-music loader (0x82757F50) so that, with R9 on, every track
// the menu asks for is a bundled or loaded DLC song rather than the title's own
// three shell tracks. Bundled songs stream natively from the game archive.
// DLC packages are mounted read-only from
// rb_blitz::dlc::LibraryPackagePaths(), so nothing is copied and nothing under game/ is
// written.
//
// Call once from RbBlitzApp::OnPostLoadXexImage(), after dlc::Configure(): it reads the
// bundled catalog and the library that layer just enumerated. One shuffled DLC package
// prepares asynchronously; native Load selects from the songs ready at that point.
// A small rolling cache is prepared during native cache loading and menu playback. With the toggle off
// it leaves the title's tracks in control; the independent navigator can still move them.
void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root);

// Startup starts host-only preparation; native cache loading also ensures it is running. Guest polls
// publish at most one ready device without waiting for I/O. Close never joins.
void StartQueuedPool();
void PublishQueuedPool(bool during_loading = false);
void CloseQueuedPool();

// Worker-only bounded read. Opening under the session lock leases the existing
// cache device until copied bytes are owned; art never mounts another package.
ui::music::ArtRead ReadArtwork(const ui::music::ArtTag& tag, std::vector<uint8_t>* bytes);

// Metadata key of the selection captured by rbbz-play/begin, including songs
// appended after boot. The read_file hook returns it as a native Symbol.
std::string SelectedSongKey();

// --- the "Play Current Song" row's view of this layer (owner feature, 2026-10-14) --------------
// The main menu's second entry starts whichever song the menu is playing, so it needs the facts
// below at two moments: the payload builder checks that the startup pool exists,
// and the live channel captures a selection and returns its metadata key on press.
//
// `MenuMusicEnabled` is the setting's own value (the revived `general.dlc_menu_songs` row, read
// from the settings store in Configure()), and `HasPool` is whether that setting found
// bundled songs or DLC candidates for asynchronous preparation.
// They differ when the setting is on but the archive has no bundled songs and there are
// no DLC candidates - the unavailable state, and the reason
// the Play Current Song row is absent from a menu whose background music is the title's own.

// Whether Background Music asked for the library pool this boot.
bool MenuMusicEnabled();

// True with the setting on and bundled songs or DLC candidates. An asynchronous
// candidate may still fail; the press-time selection reports availability then.
bool HasPool();

// Discovered catalog songs in stable ID order, independent of mount residency.
// Keys come from metadata, or the stream basename when metadata is absent.
std::vector<std::string> PoolSongNames();

// The stable catalog ID whose stream is audibly advancing, or -1 while loading.
int CurrentPoolIndex();

// The host's answer for one `rbbz-play/<...>` query path - what the menu's Play Current Song row
// asks through `file_exists`:
//   `rbbz-play/next|previous|pause` - post a navigator request (false if unavailable/busy).
//   `rbbz-play/begin` - snapshot a selection (audible, recently heard, or pending at startup).
//   `rbbz-play/current/<i>` - true for the selected index, stable throughout one press.
//   `rbbz-play/result/<reason>` - record guest lookup success/failure and end the snapshot.
// The authored row reads the key through read_file("rbbz-play/song"); indexed
// probes remain available for diagnostics.
// Returns false (and leaves `*answer` alone) for a path this layer does not own, so the caller
// keeps forwarding it to the original.
bool AnswerPlayQuery(std::string_view path, bool* answer);

}  // namespace rb_blitz::shell_music
