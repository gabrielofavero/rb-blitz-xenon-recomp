// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P5 of the RhythmVerse store plan (§5/P5, §3.1 step 3): the game's streaming transfer.
//
// The catalogue transport (`store_http.h`) reads a whole body into memory because a listing is
// small. A package is tens of megabytes, so this file streams one to the receiver instead, and it
// keeps the same two rules: **WinHTTP never follows a redirect** (the policy in
// `store_download.cpp` resolves every `Location` itself) and **nothing here touches the game**
// (the download worker calls it on a host thread).
//
// Windows-only: the file includes <windows.h>/<winhttp.h> and is compiled only by the Windows
// build (CMakeLists.txt appends it under `if(WIN32)`), exactly like `store_http.cpp`.

#pragma once

#include "store/store_download.h"

namespace rb_blitz::store {

// The production binding for `TransferFn`: HTTPS only, redirects disabled, transparent content
// decoding disabled (so the length the policy compares is the length that arrived), and the body
// handed to the receiver in 64 KiB chunks.
TransferFn WinHttpTransfer();

}  // namespace rb_blitz::store
