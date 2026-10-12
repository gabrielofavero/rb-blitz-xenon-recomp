// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
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
    do {
#ifdef _WIN32
      handle_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle_ != INVALID_HANDLE_VALUE) return true;
      const auto code = GetLastError();
      if (code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION) break;
#else
      handle_ = open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
      if (handle_ < 0) break;
      if (flock(handle_, LOCK_EX | LOCK_NB) == 0) return true;
      close(handle_); handle_ = -1;
#endif
      if (!wait) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    if (error) *error = "cannot lock settings file (another writer or access denied)";
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
  if (ec) { if (error) *error = "cannot create settings directory: " + ec.message(); return false; }
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
  if (!stream) { if (error) *error = "cannot open temporary settings file"; return false; }
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
    ok = MoveFileExW(temporary.c_str(), file.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, file, ec);
    ok = !ec;
#endif
  }
  if (!ok) {
    std::filesystem::remove(temporary, ec);
    if (error) *error = "cannot flush or replace settings file; previous file retained";
  }
  return ok;
}
}  // namespace rb_blitz::fs
