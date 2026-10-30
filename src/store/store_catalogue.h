// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3's catalogue for the store's front menu (docs/plans/finished/2026-10-13-rhythmverse-store-plan.md
// §5/P3): the offers the "Recently Added" row is answered with.
//
// The retail browser has no paging of its own - its PageUp/PageDown are screen transitions, not a
// next-page control (measured 2026-10-21 in `store.dtb`'s `store_browser_panel`) - so the "next
// page" the store never showed is the catalogue's: one visit accumulates up to
// `store_catalogue_pages` API pages (25 records each) into the single list the browser scrolls,
// publishing each page as it lands so a player who gets there early already has the first one.
//
// P7's rules (§5/P7), and where they live:
//
//   * **chunking** - the page cursor is the server's own (`ListingPage::page`), kept separately from
//     the offers a caller has kept (`rhythmverse_client.h`'s `NextPageAfter`), so a page the
//     instrument/eligibility filter emptied is not mistaken for the last one. The budget is
//     `store_catalogue_pages`; an underfilled page is a valid page; `total_filtered` is never read
//     as a count of downloadable offers.
//   * **caching** - every page goes through the shared in-memory page cache
//     (`store_page_cache.h`): a fresh page is reused for `store_page_cache_ttl_seconds` (~5 min), an
//     identical fetch already in flight is not started twice, and the cache's key is the request
//     body, so a filter commit cannot read a page fetched for the old filter.
//   * **politeness** - a failed page backs off for a wait derived from the answer
//     (`FailureBackoffSeconds`: `Retry-After` honoured, 5xx a minute, transport 10 s), a page that
//     landed resets it, and the pause between two pages is **cancellable**: the master switch going
//     off or a filter commit stops the walk before the next request instead of after it.
//   * **empty / offline text** - every state change is published to `store_catalogue_status.h`,
//     which is what the caption strip draws (the credit line when the store opens, the loading
//     sentence while the first page is in flight, and the empty/offline sentences after it).
//
// Why this is a worker and not a call: the offers page is asked for on the **guest** thread (inside
// the file-cache hook, `store_serve.cpp` answers the request), and a network round trip there would
// stall a frame. So the guest only ever *reads* a snapshot; the fetch runs here, on its own thread,
// through `store_http` (WinHTTP, redirects never followed) and each page is published when it lands.
// Measured 2026-10-20: the store's front menu is served first and a row is pressed ~14 s later, so
// the fetch started when the store opens is usually ready by the time a page is asked for. When it
// is not, the page is empty - a record the retail DTA already handles (zero offers refreshes the
// menu instead of opening the browser), never a blocked frame.
//
// The module obeys the project's worker rules: no cvar registry access, no guest memory, no shared
// mutable state beyond the snapshot behind a mutex, and the settings it uses are the immutable
// `StoreSettings` snapshot (`store_settings.h`) captured when the fetch starts - a commit during the
// fetch invalidates the answer instead of racing it.
//
// The rules for what an offer *is* (eligibility, direct link, archive names, the instrument filter)
// are `rhythmverse_client.h`'s and `store_search.h`'s `ShapeOffers`; this file only owns "when" and
// "how often" and how many pages one visit may cost.

#pragma once

#include <string>
#include <string_view>

namespace rb_blitz::store {

// Starts the worker and installs the offer source the serving policy reads
// (`store_serve.h`'s `SetOfferSource`). Called once per boot on the host thread, beside the other
// store installations; idempotent. Also sets the shared page cache's TTL from
// `store_page_cache_ttl_seconds` and publishes the catalogue's initial state to the caption strip
// (`store_catalogue_status.h`).
void InstallStoreCatalogue();

// Called from the guest thread when the store asks for something that needs the catalogue: the
// front menu itself (a good moment to start fetching) and any menu page. Never blocks and never
// throws; a fetch already in flight makes this a no-op (single flight), a fresh page makes it a
// no-op until the TTL expires, and a failed one backs off before trying again.
void RequestCatalogueRefresh(std::string_view why);

// P9-10's **user-initiated** walk: the same worker and the same `store_catalogue_pages` budget,
// but it does not wait out the backoff a previous failure named - the player pressed the row and
// asked, so the press is not held off by a politeness delay meant for automatic retries. Single
// flight still folds a burst of presses into the running walk, and a failed attempt re-arms the
// wait for the automatic path.
void RequestCatalogueRefreshNow(std::string_view why);

// P9-10's read/write pair on the guest bridge (`src/hooks/settings_bridge_hooks.cpp`), beside the
// settings snapshot's own queries:
//
//   rbbz-store/catalogue/failed   answered true while no offers are held and the last walk failed
//   rbbz-store/catalogue/retry    starts `RequestCatalogueRefreshNow("a press asked")`
//
// `AnswerCatalogueQuery` returns false for any other path so the caller can forward the call;
// `HandleCatalogueWrite` does the same for a write it does not own.
bool AnswerCatalogueQuery(std::string_view path, bool* answer);
bool HandleCatalogueWrite(std::string_view path);

// The log token for the catalogue's current state: `fresh`, `fetching`, `stale`, `failed`,
// `off`. Diagnostics only.
std::string_view CatalogueStateToken();

}  // namespace rb_blitz::store
