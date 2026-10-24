// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3's data lane (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md §5/P3): what a store
// netcache request is answered with.
//
// The seam is the file cache itself. `FileCache::Request` (`sub_8235A240`) asks the manager's
// loaded-file container whether the path is already loaded (`sub_82468518` -> `sub_82474E68`): when
// it is, the retail code registers the calling client and notifies it with the data
// (`sub_8235A0F8`); when it is not, it builds a request entry and submits it to the netcache, which
// performs no I/O offline. So a record is delivered by putting it in that container *before* the
// request runs - `sub_824685E0(manager, path, data, size)` is the retail insert (its signature is
// read off the download-completion call site, which passes the path, the downloaded bytes and
// their length) - and everything after that is retail's own path. No notify call of our own, no
// client list to maintain.
//
// This file is the policy: which path is ours, and which bytes answer it. The guest-side half
// (allocating a guest buffer, calling the insert) is src/store/store_fetch.cpp.
//
// SDK-free, so tests/store_serve_tests.cpp covers the path rules and the record choice without a
// boot.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "store/store_metadata.h"

namespace rb_blitz::store {

// Every record the retail store fetches lives under this prefix (plan §0.1: the front menu, the
// per-offer directories and the related-offer records). A request outside it is the retail
// server's business and is never touched.
inline constexpr char kStoreRequestPrefix[] = "dlc_store/";

// True for a request path the store's own catalogue owns.
bool IsStoreRequestPath(std::string_view path);

// True for the front menu's own path (`dlc_store/<region>/dlc_top_<platform>_<locale>.dta`).
bool IsFrontMenuPath(std::string_view path);

// True for `dlc_store/<region>/<locale>/<name>/` - one front-menu row's offers page (measured
// 2026-10-20: pressing the served front menu's first row asks for `dlc_store/europe/eng/recent/`).
// Offer and related-record paths are deeper and are not menus.
bool IsMenuRecordPath(std::string_view path);

// True for the search row's own page (`dlc_store/<region>/<locale>/search/`). It is a menu record
// like Recently Added's, but its offers are the search session's answer instead of the
// catalogue's list - which is how the plan's "results replace the browser list" is true: the page
// goes through the same parser, the same `process_offers`, and the same browser.
bool IsSearchMenuPath(std::string_view path);

// Where that page's offers come from. Installed once per boot by the search's transport module
// (`src/store/store_search_service.cpp`), which composes "the session's accepted offers, as the
// record model" and marks the answer presented; a suite installs its own source, and a boot that
// never installs one answers the search page with an empty list - a valid page.
using SearchPageSource = std::function<std::vector<Offer>()>;
void SetSearchPageSource(SearchPageSource source);
// The installed source's page (empty without one).
std::vector<Offer> SearchPageOffers();

// The offers a menu page is answered with. Installed once per boot by the catalogue worker
// (`src/store/store_catalogue.cpp`); a test that never installs one gets an empty page, which is a
// valid record and keeps this module free of the network.
using OfferSource = std::function<std::vector<Offer>()>;
void SetOfferSource(OfferSource source);

// A copy of the offers the installed source answers with, or none when no source is installed.
std::vector<Offer> CatalogueOffers();

// The Downloaded matcher (plan §3.2). The serving policy asks it once per offer when a menu page is
// written, so the record carries `is_purchased` for the songs the loaded DLC library already holds.
// The index itself lives in `src/store/store_library.cpp` (it needs the package reader) and is
// installed once per boot; this module stays SDK-free and a test can install its own predicate.
using DownloadedPredicate = std::function<bool(const Offer&)>;
void SetDownloadedPredicate(DownloadedPredicate predicate);

// True when the installed predicate says the song is in the loaded library. False with no
// predicate installed, which is the honest answer for a boot that never built the index.
bool IsDownloadedOffer(const Offer& offer);

// The catalogue offers with the Downloaded flag resolved. `BuiltinRecord` writes this shape.
std::vector<Offer> OffersForPage();

// The download registration seam (P5). Each row of a served page carries a job token
// (`Offer::job`) the details page's Download row can send; the worker behind that token lives in
// `src/store/store_download_service.h` and is installed once per boot by the download service's
// wiring. Keeping it a seam has the usual two reasons: this header stays SDK-free and free of the
// network, and a suite can install a fake registrar to check that every offer on a page carries a
// distinct token - which is the one thing the guest side relies on.
//
// The registrar receives what the worker needs and returns the token, or "" when it will not take
// the offer (switched off, refused). `url` is what `DecideOffer` resolved, and `api_size` is the
// catalogue's byte count (empty when unknown).
using OfferRegistrar = std::function<std::string(std::string_view file_id, std::string_view url,
                                                std::optional<std::int64_t> api_size,
                                                std::string_view title)>;
void SetOfferRegistrar(OfferRegistrar registrar);

// Fills `offer->job` from the installed registrar, or leaves it empty when no registrar is
// installed or the offer carries no download identity. Called by the serving path for every offer
// of every page, so a row a player can see is always a row the worker can be asked about. The
// title it passes is what the caption strip shows ("Artist - Title") and what the installed
// package is named after (sanitized by `store_install.cpp`, which owns that rule).
void AttachDownloadJob(Offer* offer);

// The album-art prefetch seam. `src/store/album_art_service.h` owns the worker that fetches and
// encodes a cover; this module only reports which offers a page carries, so the fetch starts on the
// first visit to a page the catalogue produced moments ago. Installed once per boot; a suite that
// never installs one prefetches nothing, and this header stays SDK-free and free of the network.
using ArtPrefetch = std::function<void(const std::vector<Offer>&)>;
void SetArtPrefetch(ArtPrefetch prefetch);

// The answer to one request. `bytes` empty means "we have nothing for this path": the retail
// fetch then runs exactly as it does today (and does nothing offline). `source` is the log token
// that says where the bytes came from, so a boot's evidence names the branch.
//
// The bytes of a `.dta` record are **text**: the insert parses that type with the engine's own text
// reader (`sub_82418878`, whose tokenizer `sub_82417BF8` compares tokens against `#define`,
// `#merge`, `#ifdef` and `#autorun`). Measured 2026-10-20 evening: a compiled DTB served at the
// same path parsed to one stray symbol node, while the text record parsed to the `submenus` map.
struct ServedRecord {
  std::string bytes;
  std::string source;
};

// The bytes of the host file named exactly `<dir>/<path>` (forward slashes in `path`). Empty when
// `dir` is empty, the file does not exist or cannot be read, or `path` is not a plain relative path
// - `..` segments, a leading `/`, a drive letter and a UNC prefix are all refused, because the
// path is composed by the guest.
std::string ReadServedRecord(std::string_view dir, std::string_view path, std::string* why);

// The record this build generates for a path, or empty when it has none. Today that is the front
// menu at `dlc_store/<region>/dlc_top_<platform>_<locale>.dta`; a menu path answers an empty page
// once its request shape is measured (the panel's own request names it, and the probe logs it).
std::string BuiltinRecord(std::string_view path, std::string* why);

// The record to serve: the host directory first, so a boot can iterate the schema with no rebuild,
// then the built-in one.
ServedRecord RecordForRequest(std::string_view path, std::string_view serve_dir);

}  // namespace rb_blitz::store
