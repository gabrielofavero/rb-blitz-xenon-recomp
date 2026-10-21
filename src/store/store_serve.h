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

#include <string>
#include <string_view>

namespace rb_blitz::store {

// Every record the retail store fetches lives under this prefix (plan §0.1: the front menu, the
// per-offer directories and the related-offer records). A request outside it is the retail
// server's business and is never touched.
inline constexpr char kStoreRequestPrefix[] = "dlc_store/";

// True for a request path the store's own catalogue owns.
bool IsStoreRequestPath(std::string_view path);

// The answer to one request. `text` empty means "we have nothing for this path": the retail fetch
// then runs exactly as it does today (and does nothing offline). `source` is the log token that
// says where the bytes came from, so a boot's evidence names the branch.
struct ServedRecord {
  std::string text;
  std::string source;
};

// The text of the host file named exactly `<dir>/<path>` (forward slashes in `path`). Empty when
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
