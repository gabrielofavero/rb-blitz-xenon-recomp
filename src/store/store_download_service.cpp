// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_download_service.h.

#include "store/store_download_service.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <mutex>
#include <cstdio>
#include <thread>
#include <utility>

#include "store/store_settings.h"

namespace rb_blitz::store {
namespace {

constexpr std::string_view kStorePrefix = "rbbz-store/";
constexpr std::string_view kReceiptHeader = "# rbbz-store downloads v1";
constexpr std::size_t kMaxQueuedJobs = 8;

struct State {
  std::mutex mutex;
  DownloadConfig config;
  bool configured = false;
  std::map<std::string, OfferRecord> offers;
  std::map<std::string, JobStatus> jobs;
  std::map<std::string, std::filesystem::path> installed;
  std::deque<std::string> queue;
  std::set<std::string> deletions;
  std::set<std::pair<std::string, std::string>> removed_songs;
  bool warning_dismissed = false;
  std::string active;
  bool active_deleting = false;
  std::atomic<bool> cancel{false};
  bool worker_running = false;
  bool stopping = false;
  std::thread worker;

  // A joinable std::thread would terminate the process at static destruction. A process that
  // exits mid-transfer waits at most the transport's own receive timeout for the worker; the
  // in-game restart path calls StopDownloads() explicitly (dlc::DrainRefreshForRestart).
  ~State() {
    {
      cancel.store(true);
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
    }
    if (worker.joinable()) {
      worker.join();
    }
  }
};

State& S() {
  static State state;
  return state;
}

void LogLine(const std::string_view line, const bool warning = false) {
  std::function<void(std::string_view, bool)> sink;
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    sink = S().config.log;
  }
  if (sink) {
    sink(line, warning);
  }
}

void SetFailed(const std::string& token, const DownloadError error, const std::string& detail) {
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    JobStatus& status = S().jobs[token];
    status.state = JobState::kFailed;
    status.error = error;
    status.bucket = 0;
  }
  LogLine("store_download: job " + token + " failed (" + std::string(DownloadErrorToken(error)) +
              "): " + detail,
          true);
}

void SetCancelled(const std::string& token, const DownloadError error, const std::string& detail) {
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    JobStatus& status = S().jobs[token];
    status.state = JobState::kCancelled;
    status.error = error;
    status.bucket = 0;
  }
  LogLine("store_download: job " + token + " cancelled (" +
              std::string(DownloadErrorToken(error)) + "): " + detail,
          false);
}

bool StopRequested() {
  std::lock_guard<std::mutex> lock(S().mutex);
  return S().stopping;
}

void UpdateProgress(const std::string& token, const std::uint64_t received,
                    const std::uint64_t total) {
  std::lock_guard<std::mutex> lock(S().mutex);
  JobStatus& status = S().jobs[token];
  status.received = received;
  status.total = total;
  status.bucket = total == 0 ? 0 : ProgressBucket(received, total);
}

void RunJob(const std::string& token) {
  OfferRecord offer;
  DownloadConfig config;
  bool found_offer = false;
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    const auto found = S().offers.find(token);
    if (found != S().offers.end()) {
      offer = found->second;
      found_offer = true;
    }
    config = S().config;
  }
  if (!found_offer) {
    SetFailed(token, DownloadError::kUnknownOffer, "the offer is no longer registered");
    return;
  }

  std::string plan_error;
  const std::optional<StagedInstall> install =
      PlanInstall(config.layout, offer.file_id, offer.title, &plan_error);
  if (!install) {
    SetFailed(token, DownloadError::kStorage, plan_error);
    return;
  }
  if (!EnsureStagingDirectory(*install, &plan_error)) {
    SetFailed(token, DownloadError::kStorage, plan_error);
    return;
  }

  const auto enabled = [] { return Settings()->enabled; };
  for (int attempt = 0;; ++attempt) {
    if (StopRequested() || S().cancel.load()) {
      DiscardStagedFile(*install);
      SetCancelled(token, DownloadError::kCancelled, "the download was stopped");
      return;
    }
    DiscardStagedFile(*install);
    const TransferOutcome outcome = TransferPackage(
        offer, install->part_path, config.transfer, &S().cancel, enabled,
        [&token](const std::uint64_t received, const std::uint64_t total) {
          UpdateProgress(token, received, total);
        });

    if (outcome.error == DownloadError::kNone) {
      std::filesystem::path final_path;
      std::string commit_error;
      if (!CommitStagedInstall(*install, &final_path, &commit_error)) {
        DiscardStagedFile(*install);
        SetFailed(token, DownloadError::kStorage, commit_error);
        return;
      }
      {
        const std::lock_guard<std::mutex> lock(S().mutex);
        JobStatus& status = S().jobs[token];
        status.state = JobState::kInstalled;
        status.bucket = 100;
        status.received = outcome.bytes;
        if (status.total == 0) status.total = outcome.bytes;
        S().installed[token] = final_path;
        S().removed_songs.erase({FoldForMatch(offer.artist), FoldForMatch(offer.song)});
      }
      // The quoted name is the job's own title ("Artist - Title", the same string the details
      // page's popup draws). It is what makes a boot log say *which* song was installed, and it is
      // what scripts/acceptance/acceptance_store.ps1 reads to know the title the next boot's song list must
      // carry - the receipt stays a transfer history with no name in it, by design.
      LogLine("store_download: job " + token + " installed '" + offer.title + "' " +
                  final_path.string() + " (" + std::to_string(outcome.bytes) + " bytes, title " +
                  std::to_string(outcome.title_id) + ")",
              false);
      if (!config.receipt_path.empty()) {
        Receipt receipt;
        receipt.file_id = offer.file_id;
        receipt.job = token;
        const std::filesystem::path relative =
            final_path.lexically_relative(config.layout.destination_root);
        receipt.relative_path = relative.generic_string();
        receipt.bytes = outcome.bytes;
        receipt.pending = true;
        receipt.stamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::string receipt_error;
        if (!AppendReceipt(config.receipt_path, receipt, &receipt_error)) {
          LogLine("store_download: the install succeeded but its receipt could not be written: " +
                      receipt_error,
                  true);
        }
      }
      if (config.refresh) {
        // The host half of OQ6: the new container is scanned and handed to the content manager.
        // The title's own discovery is not started here; until that is proven, the store says a
        // restart is required (plan §3.2/P5).
        config.refresh();
      }
      return;
    }

    DiscardStagedFile(*install);
    if (outcome.error == DownloadError::kCancelled) {
      SetCancelled(token, DownloadError::kCancelled, outcome.detail);
      return;
    }
    if (outcome.error == DownloadError::kDisabled) {
      SetCancelled(token, DownloadError::kDisabled, "RhythmVerse was turned off");
      return;
    }
    if (IsTransient(outcome.error, outcome.http_status) &&
        attempt < config.retry.max_retries) {
      const int delay = RetryDelayMs(config.retry, attempt, outcome.retry_after_seconds);
      LogLine("store_download: job " + token + " attempt " + std::to_string(attempt + 1) +
                  " failed (" + std::string(DownloadErrorToken(outcome.error)) + "), retrying in " +
                  std::to_string(delay) + " ms",
              false);
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
      while (std::chrono::steady_clock::now() < until) {
        if (StopRequested() || S().cancel.load()) {
          SetCancelled(token, DownloadError::kCancelled, "the download was stopped");
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
      continue;
    }
    SetFailed(token, outcome.error, DescribeTransferFailure(outcome));
    return;
  }
}

void RunDelete(const std::string& token) {
  DownloadConfig config;
  OfferRecord offer;
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    config = S().config;
    offer = S().offers.at(token);
  }
  auto paths = config.packages ? config.packages(offer.artist, offer.song)
                              : std::vector<std::filesystem::path>{};
  const auto installed = InstalledPath(token);
  if (!installed.empty()) paths.push_back(installed);
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  if (paths.empty()) {
    SetFailed(token, DownloadError::kStorage, "No matching installed package was found");
    return;
  }
  for (const auto& path : paths) {
    // Only indexed package roots reach here. Never follow a link or recursively remove a
    // library root: extracted packages must have their own songs metadata below the root.
    std::error_code ec;
    const auto resolved = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
      SetFailed(token, DownloadError::kStorage, "The package path could not be resolved");
      return;
    }
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || !path.is_absolute() || resolved == resolved.root_path() ||
        std::filesystem::is_symlink(status)) {
      SetFailed(token, DownloadError::kStorage, "The package path could not be verified");
      return;
    }
    if (std::filesystem::is_directory(status)) {
      const auto songs = resolved / "songs";
      if (!std::filesystem::is_directory(songs, ec) || ec ||
          std::filesystem::is_symlink(std::filesystem::symlink_status(songs, ec))) {
        SetFailed(token, DownloadError::kStorage, "The extracted package has no songs directory");
        return;
      }
      std::filesystem::remove_all(resolved, ec);
      if (ec) {
        // A recursive removal may have removed some files before encountering a locked one.
        const std::lock_guard<std::mutex> lock(S().mutex);
        S().removed_songs.emplace(FoldForMatch(offer.artist), FoldForMatch(offer.song));
        S().warning_dismissed = false;
      }
    } else {
      std::filesystem::remove(resolved, ec);
    }
    if (ec) {
      SetFailed(token, DownloadError::kStorage, ec.message());
      return;
    }
    if (config.forget_package) config.forget_package(path);
    {
      const std::lock_guard<std::mutex> lock(S().mutex);
      S().removed_songs.emplace(FoldForMatch(offer.artist), FoldForMatch(offer.song));
      S().warning_dismissed = false;
      // Alternate charts may have separate tokens pointing at the same package.
      for (auto it = S().installed.begin(); it != S().installed.end();) {
        if (it->second == path) {
          S().jobs.erase(it->first);
          it = S().installed.erase(it);
        } else ++it;
      }
    }
    LogLine("store_download: deleted '" + offer.title + "' package " + path.string());
  }
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    S().jobs[token] = JobStatus{JobState::kDeleted};
  }
}

void WorkerLoop() {
  for (;;) {
    std::string token;
    bool deleting;
    {
      const std::lock_guard<std::mutex> lock(S().mutex);
      if (S().stopping || S().queue.empty()) {
        S().worker_running = false;
        return;
      }
      token = S().queue.front();
      S().queue.pop_front();
      S().active = token;
      deleting = S().deletions.erase(token) != 0;
      S().active_deleting = deleting;
      S().cancel.store(false);
      JobStatus& status = S().jobs[token];
      status.state = JobState::kDownloading;
      status.error = DownloadError::kNone;
      status.bucket = 0;
      status.received = 0;
      status.total = 0;
    }
    if (deleting) RunDelete(token);
    else RunJob(token);
    {
      const std::lock_guard<std::mutex> lock(S().mutex);
      if (S().active == token) {
        S().active.clear();
      }
    }
  }
}

// Starts the worker when there is none. Called with no lock held.
void EnsureWorker() {
  bool spawn = false;
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    if (!S().configured || S().worker_running) {
      return;
    }
    S().worker_running = true;
    spawn = true;
  }
  if (spawn) {
    if (S().worker.joinable()) {
      S().worker.join();
    }
    S().worker = std::thread(WorkerLoop);
  }
}

bool Enqueue(const std::string& token, const bool download_again) {
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    if (!S().configured || S().offers.find(token) == S().offers.end()) {
      return false;
    }
    JobStatus& status = S().jobs[token];
    if (!Settings()->enabled) {
      // Master Off refuses the start itself (plan §1.6: no new catalogue/art/download request),
      // and records why so the list can show the state instead of a silent no-op.
      status.state = JobState::kCancelled;
      status.error = DownloadError::kDisabled;
      return false;
    }
    if (S().active == token ||
        std::find(S().queue.begin(), S().queue.end(), token) != S().queue.end()) {
      return true;  // start is idempotent
    }
    if (status.state == JobState::kInstalled && !download_again) {
      return true;  // already installed; only "Download Again" re-installs
    }
    if (S().queue.size() + (S().active.empty() ? 0 : 1) >= kMaxQueuedJobs) {
      status.state = JobState::kFailed;
      status.error = DownloadError::kBusy;
      return false;
    }
    status = JobStatus{};
    status.state = JobState::kQueued;
    S().queue.push_back(token);
  }
  EnsureWorker();
  return true;
}

// --- Receipts -------------------------------------------------------------------------------------

std::string SanitizeField(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    const unsigned char code = static_cast<unsigned char>(c);
    if (code < 0x20 || code == 0x7F) {
      c = '?';
    }
  }
  return out;
}

std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  for (;;) {
    const std::size_t tab = line.find('\t', start);
    fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
    if (tab == std::string::npos) {
      break;
    }
    start = tab + 1;
  }
  return fields;
}

}  // namespace

// --- Configuration --------------------------------------------------------------------------------

void ConfigureDownloads(DownloadConfig config) {
  DownloadConfig copy = config;
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    S().config = std::move(config);
    S().configured = true;
  }
  LogLine("store_download: configured to install under '" +
              copy.layout.destination_root.string() + "' (" +
              (copy.layout.structured ? "structured" : "flat") +
              (copy.transfer ? ", transport ready" : ", no transport") + ")",
          false);
}

bool DownloadsConfigured() {
  const std::lock_guard<std::mutex> lock(S().mutex);
  return S().configured;
}

std::optional<std::string> RegisterOffer(const OfferRecord& offer) {
  if (offer.file_id.empty() || offer.url.empty()) {
    return std::nullopt;
  }
  const std::string token = JobTokenForFileId(offer.file_id);
  const std::lock_guard<std::mutex> lock(S().mutex);
  S().offers[token] = offer;
  const std::size_t limit = S().config.max_offers == 0 ? 512 : S().config.max_offers;
  while (S().offers.size() > limit) {
    // Evict the oldest-looking name that no job is using. A job's offer is kept while it is
    // active or queued, so a running download cannot lose its URL to catalogue scrolling.
    auto victim = S().offers.begin();
    while (victim != S().offers.end() &&
           (victim->first == token || victim->first == S().active ||
            std::find(S().queue.begin(), S().queue.end(), victim->first) != S().queue.end())) {
      ++victim;
    }
    if (victim == S().offers.end()) {
      break;
    }
    S().offers.erase(victim);
  }
  return token;
}

std::optional<JobStatus> QueryJob(const std::string_view token) {
  const std::lock_guard<std::mutex> lock(S().mutex);
  const auto found = S().jobs.find(std::string(token));
  if (found == S().jobs.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::filesystem::path InstalledPath(const std::string_view token) {
  const std::lock_guard<std::mutex> lock(S().mutex);
  const auto found = S().installed.find(std::string(token));
  return found == S().installed.end() ? std::filesystem::path() : found->second;
}

// --- The guest channels ---------------------------------------------------------------------------

bool AnswerDownloadQuery(const std::string_view path, bool* answer) {
  if (answer == nullptr || !path.starts_with(kStorePrefix)) {
    return false;
  }
  if (path == "rbbz-store/library/refresh-needed") {
    const std::lock_guard<std::mutex> lock(S().mutex);
    *answer = !S().removed_songs.empty() && !S().warning_dismissed;
    return true;
  }
  if (path == "rbbz-store/library/ready") {
    std::function<bool()> ready;
    { const std::lock_guard<std::mutex> lock(S().mutex); ready = S().config.library_ready; }
    *answer = !ready || ready();
    return true;
  }
  constexpr std::string_view owned = "rbbz-store/owned/";
  if (path.starts_with(owned)) {
    DownloadConfig config;
    OfferRecord offer;
    std::filesystem::path installed;
    {
      const std::lock_guard<std::mutex> lock(S().mutex);
      const auto token = std::string(path.substr(owned.size()));
      const auto found = S().offers.find(token);
      if (found == S().offers.end()) { *answer = false; return true; }
      offer = found->second;
      config = S().config;
      if (S().installed.contains(token)) installed = S().installed.at(token);
    }
    std::error_code ec;
    *answer = (!installed.empty() && std::filesystem::exists(installed, ec)) ||
        (config.packages && !config.packages(offer.artist, offer.song).empty());
    return true;
  }
  const std::optional<JobQuery> query = ParseJobQuery(path.substr(kStorePrefix.size()));
  if (!query) {
    return false;
  }
  const std::lock_guard<std::mutex> lock(S().mutex);
  const auto found = S().jobs.find(query->token);
  bool value = false;
  if (found != S().jobs.end()) {
    const JobStatus& status = found->second;
    switch (query->kind) {
      case JobQueryKind::kState:
        value = status.state == query->state;
        break;
      case JobQueryKind::kProgressAtLeast:
        value = status.state == JobState::kDownloading && status.bucket >= query->bucket;
        break;
      case JobQueryKind::kError:
        // A refused start is `cancelled` with kDisabled, and a user cancel is `cancelled` with
        // kCancelled; both are terminal states the list must be able to name, so the error
        // predicate reads the terminal pair rather than `failed` alone.
        value = (status.state == JobState::kFailed || status.state == JobState::kCancelled) &&
                status.error == query->error;
        break;
    }
  }
  *answer = value;
  return true;
}

bool HandleDownloadWrite(const std::string_view path) {
  if (!path.starts_with(kStorePrefix)) {
    return false;
  }
  if (path == "rbbz-store/library/dismiss-warning") {
    const std::lock_guard<std::mutex> lock(S().mutex);
    S().warning_dismissed = true;
    return true;
  }
  if (path == "rbbz-store/library/refresh") {
    std::function<void()> refresh;
    { const std::lock_guard<std::mutex> lock(S().mutex); refresh = S().config.refresh; }
    if (refresh) {
      refresh();  // scan the host library on the refresh worker
      ClearStoreRefreshWarning();
    }
    return true;
  }
  const std::optional<JobCommand> command = ParseJobCommand(path.substr(kStorePrefix.size()));
  if (!command) {
    LogLine("store_download: refusing a malformed write: " + std::string(path.substr(0, 96)),
            true);
    return true;
  }
  switch (command->kind) {
    case JobCommandKind::kDelete: {
      {
        const std::lock_guard<std::mutex> lock(S().mutex);
        if (!S().configured || !Settings()->enabled || !S().offers.contains(command->token)) {
          S().jobs[command->token] = JobStatus{JobState::kFailed, 0,
              Settings()->enabled ? DownloadError::kUnknownOffer : DownloadError::kDisabled};
          return true;
        }
        if (S().worker_running || !S().queue.empty()) {
          if (std::find(S().queue.begin(), S().queue.end(), command->token) != S().queue.end()) {
            S().deletions.insert(command->token);  // replace a queued transfer with the deletion
            return true;
          }
          if (S().active == command->token && !S().active_deleting &&
              !S().deletions.contains(command->token)) {
            // An install has published its result but is still finishing its receipt/rescan.
            // Keep this delete behind it rather than leaving the popup waiting on Installed.
            S().deletions.insert(command->token);
            S().queue.push_back(command->token);
          }
          if (S().active != command->token &&
              std::find(S().queue.begin(), S().queue.end(), command->token) == S().queue.end())
            S().jobs[command->token] = JobStatus{JobState::kFailed, 0, DownloadError::kBusy};
          return true;
        }
        S().jobs[command->token] = JobStatus{JobState::kQueued};
        S().deletions.insert(command->token);
        S().queue.push_back(command->token);
      }
      EnsureWorker();
      break;
    }
    case JobCommandKind::kStart:
      if (!Enqueue(command->token, false)) {
        LogLine("store_download: start for " + command->token + " was refused", true);
      } else {
        // Accepted writes are logged too: the guest's half of the pipeline is a button in a retail
        // record, and a boot's evidence has to be able to say the press arrived at all (the states,
        // the progress and the install are what the following lines report).
        LogLine("store_download: start accepted for " + command->token, false);
      }
      break;
    case JobCommandKind::kDownloadAgain:
      if (!Enqueue(command->token, true)) {
        LogLine("store_download: download-again for " + command->token + " was refused", true);
      } else {
        LogLine("store_download: download-again accepted for " + command->token, false);
      }
      break;
    case JobCommandKind::kCancel: {
      bool active = false;
      bool removed_queued = false;
      {
        const std::lock_guard<std::mutex> lock(S().mutex);
        if (S().active == command->token) {
          active = true;
        } else {
          const auto queued =
              std::find(S().queue.begin(), S().queue.end(), command->token);
          if (queued != S().queue.end()) {
            S().queue.erase(queued);
            S().deletions.erase(command->token);
            JobStatus& status = S().jobs[command->token];
            status.state = JobState::kCancelled;
            status.error = DownloadError::kCancelled;
            removed_queued = true;
          }
        }
      }
      // Logged outside the lock: LogLine takes it too.
      if (removed_queued) {
        LogLine("store_download: queued job " + command->token + " cancelled", false);
      }
      if (active) {
        S().cancel.store(true);
      }
      break;
    }
  }
  return true;
}

// --- Lifecycle ------------------------------------------------------------------------------------

void StopDownloads() {
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    S().stopping = true;
    S().cancel.store(true);
    S().queue.clear();
    S().deletions.clear();
  }
  if (S().worker.joinable()) {
    S().worker.join();
  }
  {
    const std::lock_guard<std::mutex> lock(S().mutex);
    S().stopping = false;
    S().worker_running = false;
    S().active.clear();
  }
}

void WaitForDownloadsIdle() {
  for (;;) {
    bool busy = false;
    {
      const std::lock_guard<std::mutex> lock(S().mutex);
      busy = S().worker_running || !S().queue.empty() || !S().active.empty();
    }
    if (!busy) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (S().worker.joinable()) {
    S().worker.join();
  }
}

void ResetDownloadsForTesting() {
  StopDownloads();
  const std::lock_guard<std::mutex> lock(S().mutex);
  S().offers.clear();
  S().jobs.clear();
  S().installed.clear();
  S().deletions.clear();
  S().removed_songs.clear();
  S().warning_dismissed = false;
  S().configured = false;
  S().config = DownloadConfig{};
}

void ClearStoreRefreshWarning() {
  const std::lock_guard<std::mutex> lock(S().mutex);
  S().removed_songs.clear();
  S().warning_dismissed = false;
}

// --- Receipts -------------------------------------------------------------------------------------

bool AppendReceipt(const std::filesystem::path& path, const Receipt& receipt,
                   std::string* error) {
  if (path.empty()) {
    if (error != nullptr) {
      *error = "no receipt path";
    }
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    if (error != nullptr) {
      *error = DescribeFileError(ClassifyFileError(ec), ec.message());
    }
    return false;
  }
  const bool fresh = !std::filesystem::exists(path, ec);
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (!file) {
    if (error != nullptr) {
      *error = "the receipt file cannot be opened";
    }
    return false;
  }
  if (fresh) {
    file << kReceiptHeader << "\n";
  }
  file << SanitizeField(receipt.file_id) << '\t' << SanitizeField(receipt.job) << '\t'
       << SanitizeField(receipt.relative_path) << '\t' << receipt.bytes << '\t'
       << (receipt.pending ? "pending" : "installed") << '\t' << receipt.stamp << "\n";
  file.flush();
  if (!file.good()) {
    if (error != nullptr) {
      *error = "the receipt file could not be written";
    }
    return false;
  }
  return true;
}

std::vector<Receipt> ReadReceipts(const std::filesystem::path& path) {
  std::vector<Receipt> receipts;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return receipts;
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    const std::vector<std::string> fields = SplitTabs(line);
    if (fields.size() != 6) {
      continue;
    }
    Receipt receipt;
    receipt.file_id = fields[0];
    receipt.job = fields[1];
    receipt.relative_path = fields[2];
    try {
      receipt.bytes = static_cast<std::uint64_t>(std::stoull(fields[3]));
      receipt.pending = fields[4] == "pending";
      receipt.stamp = std::stoll(fields[5]);
    } catch (...) {
      continue;
    }
    receipts.push_back(std::move(receipt));
  }
  return receipts;
}

}  // namespace rb_blitz::store
