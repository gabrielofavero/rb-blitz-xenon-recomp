// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P5 of the RhythmVerse store plan (§5/P5): the download job service behind the guest bridge.
//
// Three properties shape it:
//
//   * **The guest sends a token, nothing else.** The catalogue registers an eligible offer
//     (`RegisterOffer`), the host keeps its validated URL and size, and the DTA can only ask for
//     a job token that was handed to it. No URL, no destination and no file name ever travels
//     from the guest.
//   * **One install at a time, and the last word stays with the settings snapshot.** Jobs are
//     queued; master Off cancels the running one and refuses new ones (the worker reads the same
//     immutable snapshot every chunk, so a live commit takes effect mid-transfer).
//   * **A receipt is history, never ownership.** A successful install appends one line to
//     `downloads.tsv` and starts the host refresh; the "Downloaded" state itself stays the loaded
//     library's (`store_download` §3.2, P3's index), so deleting a package cannot leave the store
//     claiming it is installed.
//
// SDK-free: the transfer is the injected `TransferFn` (WinHTTP in the game, a fake in the tests)
// and the refresh is the injected callback (`rb_blitz::dlc::RefreshAfterStoreInstall` in the
// game), so `tests/store_download_tests.cpp` runs the whole service against a temporary folder.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "store/store_download.h"
#include "store/store_install.h"

namespace rb_blitz::store {

struct DownloadConfig {
  InstallLayout layout;
  // `<user_data_root>/store/downloads.tsv`; empty disables receipts.
  std::filesystem::path receipt_path;
  TransferFn transfer;
  // Runs on the worker after a successful install (the host library refresh). May be empty.
  std::function<void()> refresh;
  RetryPolicy retry;
  std::size_t max_offers = 512;
  // The boot's log sink; the service itself stays free of the SDK so the tests can link it.
  std::function<void(std::string_view line, bool warning)> log;
};

// Called once at boot (src/hooks/dlc.cpp Configure) on a host thread.
void ConfigureDownloads(DownloadConfig config);
bool DownloadsConfigured();

// The catalogue's half of the contract (P3): an eligible offer becomes a token the store's list
// record can carry. Re-registering the same file id replaces the record (a URL revision) and
// returns the same token; the oldest offer is evicted once `max_offers` is reached.
std::optional<std::string> RegisterOffer(const OfferRecord& offer);

struct JobStatus {
  JobState state = JobState::kUnknown;
  int bucket = 0;
  DownloadError error = DownloadError::kNone;
  std::uint64_t received = 0;
  std::uint64_t total = 0;
};

std::optional<JobStatus> QueryJob(std::string_view token);
// The installed host path once a job has committed (empty otherwise).
std::filesystem::path InstalledPath(std::string_view token);

// What the caption strip says right now, or empty when there is nothing to say. The line follows
// the newest job: `Downloading 'Artist - Title': 42%` while it runs, the install result for a while
// after it commits (the title cannot play a package its boot never discovered - plan §3.2/P5), and
// the failure's own words (never a generic "download failed"). Host-thread safe; the overlay draws
// it every frame.
std::string DownloadStatusText();
// The job the line is about, for tests and diagnostics (empty when the line is empty).
std::string DownloadStatusJob();

// The guest's two channels. Both return false for a path outside `rbbz-store/`, so the hook can
// forward it; a path inside the namespace is always consumed.
bool AnswerDownloadQuery(std::string_view path, bool* answer);
bool HandleDownloadWrite(std::string_view path);

// Stops and joins the worker, cancelling queued work. Safe to call more than once.
void StopDownloads();
// Waits until no job is queued or running (tests, restart). Never call from a guest thread.
void WaitForDownloadsIdle();
// Drops every offer, job and the configuration (tests).
void ResetDownloadsForTesting();

// --- Receipts -------------------------------------------------------------------------------------

// One transfer record. `pending` means the verified package is on disk but the running title has
// not been told about it yet (restart required until guest discovery is proven).
struct Receipt {
  std::string file_id;
  std::string job;
  std::string relative_path;  // relative to the resolved DLC root, '/'-separated
  std::uint64_t bytes = 0;
  bool pending = true;
  std::int64_t stamp = 0;  // host clock ticks; ordering only
};

bool AppendReceipt(const std::filesystem::path& path, const Receipt& receipt, std::string* error);
std::vector<Receipt> ReadReceipts(const std::filesystem::path& path);

}  // namespace rb_blitz::store
