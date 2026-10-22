// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3's catalogue for the store's front menu (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md
// §5/P3): the offers the "Recent" row is answered with.
//
// The retail browser has no paging of its own - its PageUp/PageDown are screen transitions, not a
// next-page control (measured 2026-10-21 in `store.dtb`'s `store_browser_panel`) - so the "next
// page" the store never showed is the catalogue's: one visit accumulates up to
// `store_catalogue_pages` API pages (25 records each) into the single list the browser scrolls,
// publishing each page as it lands so a player who gets there early already has the first one.
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
// "how often". P7's disk cache replaces the body of `RunOne`, not this seam.

#pragma once

#include <string>
#include <string_view>

namespace rb_blitz::store {

// Starts the worker and installs the offer source the serving policy reads
// (`store_serve.h`'s `SetOfferSource`). Called once per boot on the host thread, beside the other
// store installations; idempotent.
void InstallStoreCatalogue();

// Called from the guest thread when the store asks for something that needs the catalogue: the
// front menu itself (a good moment to start fetching) and any menu page. Never blocks and never
// throws; a fetch already in flight makes this a no-op (single flight), a fresh page makes it a
// no-op until the TTL expires, and a failed one backs off before trying again.
void RequestCatalogueRefresh(std::string_view why);

// The log token for the catalogue's current state: `fresh`, `fetching`, `stale`, `failed`,
// `off`. Diagnostics only.
std::string_view CatalogueStateToken();

}  // namespace rb_blitz::store
