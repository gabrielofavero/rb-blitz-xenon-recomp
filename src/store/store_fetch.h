// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3's data lane, the guest half (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md
// §5/P3): answer a store netcache request from src/store/store_serve.h.
//
// The measured chain (2026-10-20, `out/_swap/blitz_image.bin` + the recompiled sources):
//
//   * `sub_8274FB60` builds `dlc_store/<region>/<name>` and calls `sub_8235A240(cache, &path,
//     client)` - the file cache's request entry.
//   * `sub_8235A240` asks `sub_82468518(manager, path)` whether the manager's loaded-file container
//     (`manager + 76`) already holds the path (`sub_82474E68` -> `sub_82474E08`, an exact string
//     match against the entry's own path at `+44`).
//       - loaded  -> register the client and notify it with the bytes (`sub_8235A0F8`, which builds
//                    the file wrapper `sub_82359438` and calls each client's vtable slot 5);
//       - missing -> build a request entry and submit it to the netcache, which is the step that
//                    performs no I/O offline (the P2 measurement).
//   * `sub_824685E0(manager, path, data, size)` is the retail insert into that container. Its
//     signature is read off its other caller, the download-completion path: it passes the path, the
//     downloaded buffer and its length, and the function classifies the record by the path's
//     extension (`.dta` is the text parser's type) before storing it.
//
// So serving a record is: put it in the container *before* the request runs, and then - once the
// request has registered its client - run the retail notification, which is what turns the loaded
// file into the panel's menu. Measured 2026-10-20 evening (`rb_blitz_088.log`): the panel then
// draws the served front menu's rows and pressing one asks for `dlc_store/europe/eng/recent/`,
// exactly the `MenuRecordPath` template.
//
// The notification is `sub_8235A0F8(cache, &request_path, 1)`, the same call the cache's own
// "already loaded" branch makes. It builds the `file_req_complete` message from the loaded file
// (`sub_82468560` -> `sub_82359438`) and dispatches it to every registered client, so the message
// layout stays retail's business. The one obstacle is the manager's readiness word: the file lookup
// answers only while `manager + 44 == 1`, which is 0 on a boot that never reached a server, so it
// is set for the call and restored afterwards.

#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace rb_blitz::store {

// Reads the serving switches once, on the host thread, before the guest starts. The guest-side
// hook itself must not read the cvar registry (project rule).
void InstallStoreFetch();

// Called from the file-cache hook (`sub_8235A240`) before the original runs. `path_string` is the
// guest `String` holding the request path. Returns true when a record was put into the manager's
// loaded-file container.
bool ServeStoreRequest(PPCContext& ctx, uint8_t* base, uint32_t path_string);

// Called from the same hook *after* the original ran, and only for a request that was served. It
// does not send anything: the record is delivered by `sub_8235A240`'s **own** body, because
// `ServeStoreRequest` leaves the cache's readiness word set and that body takes its "already loaded
// -> notify" branch (`sub_8235A0F8`) by itself. This is the observation point - it logs the record
// it delivered and the panel's menu list while `store_trace` is on. `cache` is the cache object
// `sub_8235A240` was called on (its `this`). Does nothing unless `store_fetch_deliver` is on.
void DeliverStoreRecord(PPCContext& ctx, uint8_t* base, uint32_t cache, uint32_t path_string);

// The album-art half, from the netcache entry `BandStorePanel::load_art` reaches
// (`sub_8274E760` -> `sub_824456A8` -> `sub_82469C28(manager, path, client, 1)`). The path there is a
// **C string** (every call site passes a `String`'s chars word), not a `String` object. That entry
// performs the loaded-file lookup itself, with no readiness guard, so serving is the same insert
// and nothing else: the original then takes its "already loaded" branch and calls the client.
// `ctx.r3` is the manager, `ctx.r4` the path; returns true when a texture was inserted.
// A path whose texture has not been encoded yet enqueues the key and answers false, which leaves
// the retail loader exactly as it is offline (the panel keeps its placeholder art).
bool ServeArtRequest(PPCContext& ctx, uint8_t* base, uint32_t path_string);

}  // namespace rb_blitz::store
