// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// HTTPS download for the payload and for the Ultimate archive, over WinHTTP.
//
// WinHTTP rather than WinINet or a bundled HTTP library: it is part of Windows,
// it honours the machine's proxy configuration (WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
// covers a PAC script as well as a static proxy), and it needs no extra DLL in
// the setup executable.
//
// The body is streamed to disk and hashed as it arrives, so a 400 MB payload
// never lands in memory and a truncated or tampered download is rejected before
// anything is extracted.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace rb_blitz::installer {

struct DownloadRequest {
  std::string url;
  std::filesystem::path destination;
  // Both optional: a mismatch after a complete download is an error, not a
  // retry, so these only catch a corrupted transfer.
  std::uint64_t expected_size = 0;
  std::string expected_sha256;
  // Percent lines for the wizard's progress bar; empty means no reporting.
  std::filesystem::path progress_file;
  std::string user_agent = "rb_blitz_setup_helper/1.0";
};

struct DownloadResult {
  std::uint64_t size = 0;
  std::string sha256;
  int status_code = 0;
  std::string final_url;
};

// Overwrites `destination`; on failure the partial file is removed, so a failed
// download never leaves something that looks complete.
bool DownloadToFile(const DownloadRequest& request, DownloadResult* out, std::string* error);

}  // namespace rb_blitz::installer
