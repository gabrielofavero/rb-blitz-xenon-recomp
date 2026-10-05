// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The General tab's *Install Ultimate* action (docs/plans/launcher-plan.md D5, B8).
//
// The launcher never bundles the mod and never downloads it itself: it drives the same
// helper the installer put in the install folder (`rb_blitz_setup_helper.exe`) with
// `install-ultimate --dest <game root> --from-pinned`, which is the installer's own
// download-and-verify route. When that is not possible - the helper is missing, the build
// has no pinned URL, or the download fails - the user is handed the pinned release URL and
// the folder to unpack it into, which is the manual route the plan names.
//
// The child process runs without a window and is watched through the helper's own progress
// file (two lines: percent, then the current step), so the launcher keeps rendering while a
// download is in flight. Nothing here blocks the frame loop.

#pragma once

#include <filesystem>
#include <string>

namespace rb_blitz::launcher {

class UltimateInstaller {
 public:
  enum class Status {
    kIdle,       // nothing started yet
    kRunning,    // the helper is working
    kSucceeded,  // the helper reported ok=1
    kFailed,     // the helper reported ok=0, or could not be run at all
  };

  ~UltimateInstaller();

  bool Busy() const { return status_ == Status::kRunning; }
  Status status() const { return status_; }

  // The line the General tab shows: the helper's own `error=` on failure, a short
  // confirmation on success, and empty while idle or running.
  const std::string& message() const { return message_; }

  // Where the user is told to unpack the release by hand when the automatic route fails,
  // and the pinned release URL the helper reports. Both may be empty - a build with no
  // pin says so rather than pointing at a URL it cannot vouch for.
  const std::filesystem::path& manual_destination() const { return manual_destination_; }
  const std::string& manual_url() const { return manual_url_; }

  // 0-100 from the helper's progress file, or -1 when the helper has not written one yet.
  int progress_percent() const { return progress_percent_; }
  const std::string& progress_detail() const { return progress_detail_; }

  // Starts the helper against `game_root`. Looking for the helper begins in `search_dir`
  // (the launcher's own folder, where the installer puts it). Returns false and fills
  // `error` when the helper cannot be found or started; the status is then kFailed and the
  // manual route is still offered.
  bool Start(const std::filesystem::path& game_root, const std::filesystem::path& search_dir,
             std::string* error);

  // Moves the state machine along: reaps the child when it exits and reads the summary file.
  // Call once per frame; cheap when idle.
  void Poll();

  // Stops a running helper. The mod is staged under the game root and only moved into place
  // once it verifies, so cancelling leaves the game's own files alone; the staging folder is
  // removed here so a cancelled install is not a half-written one.
  void Cancel();

  // Drops a finished result so the tab can dismiss its notice.
  void Reset();

  // The helper that was found, or empty. Exposed so the tab can say where it looked.
  const std::filesystem::path& helper_path() const { return helper_path_; }

  // A stable folder for the helper's summary, progress and log files: the launcher reads
  // them while the helper writes them.
  static std::filesystem::path WorkDir();

 private:
  void Fail(const std::string& reason);

  Status status_ = Status::kIdle;
  std::string message_;
  std::string progress_detail_;
  int progress_percent_ = -1;
  std::filesystem::path manual_destination_;
  std::string manual_url_;
  std::filesystem::path summary_path_;
  std::filesystem::path progress_path_;
  std::filesystem::path helper_path_;
  std::filesystem::path log_path_;
  void* process_ = nullptr;  // HANDLE, opaque so the header stays Win32-free
};

// Opens a URL or a folder through the OS shell (the default browser / the file manager).
// Best-effort: the URL and the path are on screen either way, so a failure is not reported.
void OpenInShell(const std::string& url);
void OpenFolder(const std::filesystem::path& folder);

}  // namespace rb_blitz::launcher
