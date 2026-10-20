// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P2 of docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md: the data-layer
// decision gate. This is the *measurement* half — it points the retail store's netcache
// at a local (host-file) server so the paths the store asks for become file opens this
// project can see, instead of a request to a server that no longer exists.
//
// The record is `config/gen/store.dtb`, whose single definition is `netcache_init` (decoded
// verbatim in out/_swap/config_store.tree.txt): a `servers` table of named servers (each with
// `server`/`port`/`root`, and one `local` entry carrying `{local TRUE} {root ...}`) and a
// `default_server` symbol that picks one. The rewrite keeps every other record in the file —
// the store's `groupings`, `dummy_upsell_offer` and the PS3 terms — because those are still
// read by the same parser.
//
// The transform touches **every** `servers`/`default_server` the definition carries, not just
// the Xbox branch: the shipped record keeps its platform branches inside `#ifdef` nodes
// (`HX_XBOX`, `HX_PS3`, `_SHIP`, `_DEBUG`) whose evaluation is the guest's business, so
// rewriting only the branch this build "should" take would make the measurement depend on a
// guess about that evaluation. Rewriting them all means whichever branch the guest takes, its
// default server is the local one.
//
// SDK-free on purpose: no rex headers, so tests/store_payload_tests.cpp covers it without a boot.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ui/dtb_author.h"

namespace rb_blitz::store {

// The `netcache_init` definition's own name in `config/gen/store.dtb`.
inline constexpr char kNetcacheInitName[] = "netcache_init";

// Rewrites `roots` (the parsed `config/gen/store.dtb` record) in place so that:
//
//   * every `servers` table carries a `local` entry rooted at `server_root`
//     (an existing `local` entry is rewritten rather than duplicated), and
//   * every `default_server` picks `local`.
//
// `server_root` is the guest-relative directory the netcache resolves against, spelled the way
// the shipped `local` entry spells its own (`ui/store/remote_metadata/`, trailing slash
// included). An empty root is refused: a local server with no root would measure nothing.
//
// Returns false and fills `why` when the record does not carry `netcache_init`, when it carries
// no `default_server` at all, or when `server_root` is empty — never a partial rewrite that
// looks successful.
bool PointNetcacheAtLocalServer(std::vector<ui::dtb::Node>* roots, std::string_view server_root,
                                std::string* why);

}  // namespace rb_blitz::store
