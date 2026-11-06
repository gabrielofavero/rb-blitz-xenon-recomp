// SPDX-License-Identifier: GPL-2.0-only
#include "messages/messages_cache.h"

#include <fstream>

#include "fs/atomic_file.h"

namespace rb_blitz::messages {
bool Load(const std::filesystem::path& path, MessageFeed* feed, std::string* error) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size > 256 * 1024) {
    if (error) *error = ec ? "cannot read " + path.string() + ": " + ec.message()
                           : "feed exceeds 256 KiB";
    return false;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) { if (error) *error = "cannot open " + path.string(); return false; }
  std::string body(size, '\0');
  if (!stream.read(body.data(), static_cast<std::streamsize>(size))) {
    if (error) *error = "cannot read " + path.string();
    return false;
  }
  return ParseFeed(body, feed, error);
}

bool Save(const std::filesystem::path& path, const std::string& body, std::string* error) {
  MessageFeed parsed;
  if (!ParseFeed(body, &parsed, error)) return false;
  return fs::WriteAtomic(path, body, error);
}
}  // namespace rb_blitz::messages
