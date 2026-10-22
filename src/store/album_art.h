// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3's album art (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md §5/P3): the names,
// the URL rule and the texture cache that sit between the offer record and the retail art loader.
//
// ## What the retail side does (measured 2026-10-21)
//
// `store_details_panel.refresh(offer)` runs
//
//     {if_else {&& {offer has_data preview_art} {is_allowed_to_preview_offer offer}}
//       {unless {== {property cur_preview_art} {offer get_data [preview_art]}}
//         {set {property cur_preview_art} {offer get_data [preview_art]}}
//         {album.mat set diffuse_tex cover_art_none.tex}
//         {store_panel load_art {property cur_preview_art} this}}
//       ...}
//
// and `art_loaded` sets `album.mat`'s `diffuse_tex` to `{store_panel album_tex}`. The native half:
//
//   * `BandStorePanel::load_art` (`sub_8274E760`) builds `panel request prefix + the value` and
//     calls `sub_824456A8(panel, path, callback)`, which asks the netcache manager
//     (`sub_82469C28`) for that path with the panel as the client and remembers the callback in
//     `panel + 104`.
//   * `sub_82469C28` first looks the path up in the manager's loaded-file container
//     (`sub_82474E08(manager + 76)` - the same container `sub_824685E0` inserts into) and, when it
//     is there, wraps it and calls the client. So a texture is delivered exactly like a metadata
//     record: insert the bytes before the request runs.
//   * `sub_82445CD8` decodes the bytes with `RndBitmap::Load` (`sub_821FE800` -> `sub_821FD400`,
//     which reads the 32-byte envelope in src/assets/texture_codec.h) and copies the bitmap into
//     the panel's texture (`panel + 100`), then messages `art_loaded`.
//
// The guard is a **DTA function**, not a native method: `ui/gen/global.dtb` defines
// `{func is_allowed_to_preview_offer (offer) {if_else {offer has_data rating}
// {offer get_data (rating)} 2}}`. Our records deliberately omit `rating` (writing 0 drew the
// literal token `RATING_0`), so the guard answers 2 and the art branch runs with no payload edit.
//
// ## What this module owns
//
// A key derived from the catalogue's own identity (never the opaque API id as a path), the path the
// offer's `preview_art` carries, the URL rule for the API's `album_art` field, and the in-memory
// textures the serving policy answers with. The image decode and the fetch live in
// `album_art_service.h` (Windows); everything here is SDK-free and covered by
// tests/store_art_tests.cpp.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace rb_blitz::store {

// The texture the album art is encoded at. Retail's own cover art is 512x512 DXT1; 256x256 DXT5
// loses nothing at the size the details panel draws it and quarters both the transfer work and the
// 64 KiB image (plus its mip chain) the guest holds.
inline constexpr int kAlbumArtTextureSize = 256;

// The file name the art request ends with. The panel composes `request prefix + preview_art`, so
// this half of the path is what identifies an art request whatever the prefix turns out to be.
inline constexpr char kAlbumArtFileName[] = "cover_art.png_xbox";

// A stable, path-safe key for one catalogue entry: 16 lowercase hex digits of FNV-1a 64 over the
// API's file id, or over `artist|title` when the record carries no id. The opaque API id is never
// used as a path segment (it can contain dots and is under the server's control), and hashing keeps
// every key inside one alphabet and one length, so `ArtPathForKey` needs no escaping.
std::string ArtKeyForSong(std::string_view file_id, std::string_view artist, std::string_view title);

// `/offers/<key>/cover_art.png_xbox` - the value an offer's `preview_art` carries. The panel
// composes `request prefix + this value`, and the prefix is measured as `dlc_store` **without** a
// trailing slash (`BandStorePanel::load_art` concatenates the two strings verbatim: a value without
// the leading slash produced `dlc_storeoffers/...` in boot `rb_blitz_173.log`), so the composed
// request is `dlc_store/offers/<key>/cover_art.png_xbox`.
std::string ArtPathForKey(std::string_view key);

// The key a composed request path names, or empty when the path is not one of ours. The rule is
// positional from the end: `.../<16 hex>/cover_art.png_xbox`, so nothing here depends on the
// prefix the panel chose.
std::string ArtKeyFromPath(std::string_view path);
bool IsArtRequestPath(std::string_view path);

// The URL rule for the API's `album_art` field is `rhythmverse_client.h`'s `ResolveAlbumArtUrl`
// (HTTPS, exactly `rhythmverse.co`, a path under `/assets/album_art/`, image extensions only) -
// P0 already wrote it and `store_client` pins it. This module never re-implements it: the offer
// builder asks the client and stores the resolved URL on the offer, and the worker fetches exactly
// that value.

// The master gate (plan §1.6): with RhythmVerse off no art is fetched, and with the `store_art_off`
// diagnostic switch on the records carry no `preview_art` at all, which is what makes the retail
// panel draw its own placeholder. Read once at boot by `InstallAlbumArt`.
void SetAlbumArtEnabled(bool enabled);
bool AlbumArtEnabled();

// --- The cache ---------------------------------------------------------------------------------
//
// One process-wide table, written by the art worker and read by the guest's file-cache hook. Both
// sides copy under the same mutex; nothing here holds a lock across a draw or a network call.

// The URL to fetch for a key. Published with the page that carries the offer.
void PublishArtUrl(std::string key, std::string url);
std::string ArtUrlForKey(std::string_view key);

// The encoded `.png_xbox` bytes for a key, empty until the worker has produced them.
void PublishArtTexture(std::string key, std::string bytes);
std::string ArtTextureForKey(std::string_view key);
bool HasArtTexture(std::string_view key);

// Diagnostics: how many textures are held and how many URLs are known.
std::size_t ArtTextureCount();
std::size_t ArtUrlCount();

// Test hook: forget everything (the suites run in one process).
void ResetAlbumArtForTests();

// The worker seam. The image decode and the HTTP fetch are Windows-only and live in
// `src/store/album_art_service.h`, which installs this at boot; the SDK-free modules (the guest's
// file-cache hook in particular) ask for a texture through `RequestArtTexture`, so they never have
// to include the Windows half. With nothing installed a request is a no-op.
using ArtTextureRequest = std::function<void(std::string_view key)>;
void SetArtTextureRequest(ArtTextureRequest request);
void RequestArtTexture(std::string_view key);

}  // namespace rb_blitz::store
