// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3's album art, the worker half (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md
// §5/P3, plan §3.1's art rule): fetch the API's `album_art` image on a host thread, decode it with
// Windows Imaging Component, encode the `.png_xbox` the retail art loader wants
// (`src/assets/texture_codec.h`) and publish it into the cache `album_art.h` owns.
//
// Why a worker: the guest asks for the texture inside the file-cache hook, on a guest thread, and a
// network round trip there would stall a frame (the project's rule for anything network-facing).
// The hook therefore only ever *reads* the cache; a miss enqueues the key and leaves the retail
// loader with nothing, which is what the panel already handles (it keeps the placeholder art).
//
// Why Windows Imaging Component, and why nothing is vendored: WIC is part of Windows and decodes
// PNG and JPEG to straight RGBA in one call, which is all the codec needs. The button-layout plan
// needs the same two halves in the other direction (decode a DXT sheet, write a PNG), and WIC's
// encoder covers that too, so the shared piece stays `src/assets/texture_codec.*` and the platform
// piece stays here - no new third-party dependency in either direction.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "store/store_metadata.h"

namespace rb_blitz::store {

// Starts the worker and reads `store_art_off` (and the master switch's snapshot). Called once per
// boot on the host thread, beside the other store installations.
void InstallAlbumArt();

// Enqueues every offer of a page that carries an art URL and has no texture yet. Called from two
// places: the catalogue worker when a page lands (so the page's art is usually ready long before a
// player opens it), and the serving policy when a page is written (so a first visit to a page that
// the catalogue only just produced still starts the fetch). Never blocks.
void RequestAlbumArt(const std::vector<Offer>& offers);

// Enqueues one key - the guest asked the art loader for a texture this boot does not hold yet.
// No-op when the key has no URL, already has a texture, or is already queued/in flight.
void RequestAlbumArtTexture(std::string_view key);

// Diagnostics: `off`, `idle`, `queued`, `fetching`, `failed`.
std::string_view AlbumArtStateToken();

// Diagnostics: how many textures this boot produced, and why the last one failed (empty when the
// last fetch succeeded or nothing has run).
std::size_t AlbumArtEncodedCount();
std::string AlbumArtLastError();

}  // namespace rb_blitz::store
