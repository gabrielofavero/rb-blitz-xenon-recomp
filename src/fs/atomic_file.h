// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace rb_blitz::fs {

// The OS's own verdict on a failed file operation, as a stable phrase plus its numeric code.
//
// P3's rule (docs/plans/in-progress/2026-10-14-installed-game-fix-prompts.md, D5) is that a failed
// save names the operation, the path and the error - never a generic "saving" state. The old text
// here was "cannot lock settings file (another writer or access denied)", which cannot tell a
// transient competing writer (retry) from a directory the player may not write (stop and report).
inline std::string OsErrorText(const unsigned long code) {
#ifdef _WIN32
  std::string text;
  switch (code) {
    case ERROR_ACCESS_DENIED: text = "access denied"; break;
    case ERROR_SHARING_VIOLATION: text = "sharing violation: another writer holds the file"; break;
    case ERROR_LOCK_VIOLATION: text = "lock violation: another writer holds the lock"; break;
    case ERROR_FILE_NOT_FOUND: text = "the file does not exist"; break;
    case ERROR_PATH_NOT_FOUND: text = "a directory in the path does not exist"; break;
    case ERROR_DISK_FULL: text = "the disk is full"; break;
    case ERROR_WRITE_PROTECT: text = "the volume is write-protected"; break;
    case ERROR_LOCK_FAILED: text = "the lock could not be taken"; break;
    default: text = "unclassified"; break;
  }
  return text + " (win32 error " + std::to_string(code) + ")";
#else
  (void)code;
  return std::string(std::strerror(errno)) + " (errno " + std::to_string(errno) + ")";
#endif
}

// A persistent sidecar locks the whole read/merge/replace transaction. The OS releases
// the lock on process death; stale lock files never block a later run.
class FileLock {
 public:
  FileLock() = default;
  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;
  ~FileLock() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
    if (handle_ >= 0) close(handle_);
#endif
  }
  bool Acquire(std::filesystem::path file, std::string* error, bool wait = true) {
    file += ".lock";
    std::error_code ec;
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), ec);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(wait ? 2 : 0);
    unsigned long last_error = 0;
    for (;;) {
#ifdef _WIN32
      handle_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle_ != INVALID_HANDLE_VALUE) return true;
      last_error = GetLastError();
      if (last_error != ERROR_SHARING_VIOLATION && last_error != ERROR_LOCK_VIOLATION) break;
#else
      handle_ = open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
      if (handle_ < 0) { last_error = errno; break; }
      if (flock(handle_, LOCK_EX | LOCK_NB) == 0) return true;
      last_error = errno;
      close(handle_); handle_ = -1;
#endif
      // A non-blocking acquisition is one attempt; a blocking one retries until its own deadline.
      if (!wait || std::chrono::steady_clock::now() >= deadline) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (error) {
      *error = "cannot lock " + file.string() + ": " + OsErrorText(last_error) +
               (wait ? " (waited 2s)" : "");
    }
    return false;
  }
 private:
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int handle_ = -1;
#endif
};

inline bool WriteAtomic(const std::filesystem::path& file, const std::string& text,
                        std::string* error) {
  std::error_code ec;
  if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), ec);
  if (ec) {
    if (error) *error = "cannot create " + file.parent_path().string() + ": " + ec.message();
    return false;
  }
  static std::atomic<unsigned long long> sequence{0};
  auto temporary = file;
#ifdef _WIN32
  const auto process = GetCurrentProcessId();
#else
  const auto process = getpid();
#endif
  temporary += ".tmp." + std::to_string(process) + "." + std::to_string(++sequence);
#ifdef _WIN32
  std::FILE* stream = _wfopen(temporary.c_str(), L"wb");
#else
  std::FILE* stream = std::fopen(temporary.c_str(), "wb");
#endif
  if (!stream) {
    if (error) {
#ifdef _WIN32
      *error = "cannot open " + temporary.string() + " for writing: " + OsErrorText(GetLastError());
#else
      *error = "cannot open " + temporary.string() + " for writing: " + OsErrorText(errno);
#endif
    }
    return false;
  }
  bool ok = text.empty() || std::fwrite(text.data(), 1, text.size(), stream) == text.size();
  ok = std::fflush(stream) == 0 && ok;
#ifdef _WIN32
  ok = _commit(_fileno(stream)) == 0 && ok;
#else
  ok = fsync(fileno(stream)) == 0 && ok;
#endif
  ok = std::fclose(stream) == 0 && ok;
  if (ok) {
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), file.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
      return true;
    }
    const unsigned long replace_error = GetLastError();
#else
    std::filesystem::rename(temporary, file, ec);
    if (!ec) return true;
    const unsigned long replace_error = static_cast<unsigned long>(ec.value());
#endif
    std::filesystem::remove(temporary, ec);
    if (error) {
      *error = "cannot replace " + file.string() + ": " + OsErrorText(replace_error) +
               "; the previous file is unchanged";
    }
    return false;
  }
  std::filesystem::remove(temporary, ec);
  if (error) *error = "cannot write " + temporary.string() + "; the previous file is unchanged";
  return false;
}
}  // namespace rb_blitz::fs
