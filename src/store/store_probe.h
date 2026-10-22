// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P2 of docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md: the store's data-layer
// probe. Two things, both switched by a cvar because they are measurements rather than features:
//
//   * `store_trace` logs every *game-root* path the guest resolves or opens that names the
//     store — the offer/metadata names the retail parser asks for, and whether the read
//     succeeded. The observation point is `fs::PayloadOverlayDevice`, the device every `game:` /
//     `d:` path resolves through, so it costs one function-pointer check per open when off.
//   * `store_local_server_root` rewrites `config/gen/store.dtb`'s `netcache_init` so the store's
//     default server is a **local** (host-file) one rooted there (src/store/store_payload.h).
//     Empty, the record is left exactly as shipped.
//
// Neither is a shipping behaviour: the plan's A/B decision is made from what they measure, and P3
// replaces the rewrite with the real catalogue side.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace rb_blitz::store {

// Installs the game-root path observer when `store_trace` is on. Called once per boot on the host
// thread, before the guest starts (the observer itself runs on guest threads and must not read
// the cvar registry).
void InstallPathProbe();

// The same switch, for the hooks that cannot read the registry themselves.
bool TraceEnabled();

// The store panel the boot's last request came from: `sub_8274FB60`'s `this`, captured by the
// request hook before the file cache is called. The panel's front-menu builder needs the same
// object (`sub_827537C0(panel, array, source)` pushes the menu into `panel + 52`), and this is the
// one place a boot hands it to us with no pointer arithmetic in between. 0 until the first request.
void RememberPanel(uint32_t panel);
uint32_t Panel();

// A short hex listing of the words at a guest address, for diagnostics that have to show a layout
// the code has not been proven to use yet. Unmapped words read as 0.
std::string GuestWords(uint8_t* base, uint32_t addr, int count);

// A big-endian write into guest memory that also lifts the page protection: the image's data
// section is mapped read-only, so a plain store to a global there is silently dropped. Returns
// false when the address is not in a known heap. The caller restores the word itself.
bool WriteGuestU32Protected(uint8_t* base, uint32_t addr, uint32_t value);

// The value of `store_local_server_root` as a P2 measurement switch: empty disables the rewrite.
// Read on the host thread during the payload build.
std::string LocalServerRoot();

}  // namespace rb_blitz::store
