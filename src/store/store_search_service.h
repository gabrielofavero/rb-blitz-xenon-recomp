// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P4 of the RhythmVerse store plan: the search's transport. `store_search.{h,cpp}` decides *what*
// to ask; this file is the one place that actually asks it, on a worker thread, through
// `store_http` (WinHTTP, redirects never followed) and hands the parsed page back to the session.
//
// Why it is a separate file from the session: `store_search.cpp` is SDK-free and network-free so
// its tests need neither a socket nor a boot. This file is the game's half - it installs the
// submit sink at boot (`InstallSearchTransport`, called beside `InstallPathProbe`), owns the one
// worker thread, and is never linked into a test.
//
// The project rule this obeys: a network call never runs on a guest thread and never touches the
// cvar registry or guest memory. The guest's `write_file` hook only *posts*; every byte arrives on
// the worker.

#pragma once

#include <memory>

namespace rb_blitz::store {

// Starts the worker and routes the session's submits to it. Idempotent. Called once per boot on
// the host thread, before the guest starts; a build with no call to this (the tests) simply has no
// transport, which `ApplyStoreWrite` reports as a refusal rather than a silent nothing.
void InstallSearchTransport();

// The P4 measurement switch: makes the retail store screen open the authored key pages as it is
// entered, so a boot can drive the whole search without P3's front menu having landed yet. It is
// a measurement, not a feature - the plan's shipping entry is the store's own Search row, which
// P3's record writer serves - and it is off by default. Read on the host thread during the
// payload build.
bool SearchDiagnostic();

}  // namespace rb_blitz::store
