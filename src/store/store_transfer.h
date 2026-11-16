// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P5 of the RhythmVerse store plan (§5/P5, §3.1 step 3): the game's streaming transfer.
//
// The catalogue transport (`store_http.h`) reads a whole body into memory because a listing is
// small. A package is tens of megabytes, so this file streams one to the receiver instead, and it
// keeps the same two rules: **the transport never follows a redirect** (the policy in
// `store_download.cpp` resolves every `Location` itself) and **nothing here touches the game**
// (the download worker calls it on a host thread).
//
// Windows binds this interface to WinHTTP; Linux/macOS use libcurl and iOS uses NSURLSession.

#pragma once

#include "store/store_download.h"

namespace rb_blitz::store {

// The production binding for `TransferFn`: HTTPS only, redirects disabled, and the body handed to
// the receiver in chunks of at most 64 KiB. Content decoding is disabled where supported; iOS
// requests identity and rejects other encodings before body delivery so lengths remain truthful.
TransferFn PlatformTransfer();

}  // namespace rb_blitz::store
