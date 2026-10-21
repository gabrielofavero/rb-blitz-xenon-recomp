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
// So serving a record is: put it in the container *before* the request runs, and let retail do the
// rest. That also means the request path is the only thing we key on - no client list, no notify
// call, no re-entrancy into the panel from here.

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

}  // namespace rb_blitz::store
