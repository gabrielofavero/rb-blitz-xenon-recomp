// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "messages/messages_api.h"

namespace rb_blitz::messages {

// The provider owns each 16-byte entry, but not the InboxMessage it points to. Keeping the
// object and both source buffers for the boot also makes repeated build_list calls safe.
struct GuestObject {
  std::uint32_t message = 0;
  std::uint32_t subject_string = 0;
  std::uint32_t body_chars = 0;
};

class GuestObjectPool {
 public:
  const GuestObject* Find(const Message& message) const {
    auto it = objects_.find(Key(message));
    return it == objects_.end() ? nullptr : &it->second;
  }
  void Remember(const Message& message, GuestObject object) {
    objects_.emplace(Key(message), object);
  }
  std::size_t size() const { return objects_.size(); }

 private:
  static std::string Key(const Message& message) {
    // Explicit separators prevent different field tuples from sharing one guest object.
    return message.id + '\0' + message.published + '\0' + message.title + '\0' + message.body;
  }
  std::map<std::string, GuestObject> objects_;
};

inline std::string SubjectLine(const Message& message) {
  return message.published.empty() ? message.title : message.published + "  " + message.title;
}

// A provider entry is four big-endian words in guest memory: symbol, message pointer, type 1,
// false flag (the last word's first byte). The same layout is emitted by retail build_list.
inline std::array<std::uint8_t, 16> EncodeEntry(std::uint32_t symbol,
                                                std::uint32_t message) {
  std::array<std::uint8_t, 16> bytes{};
  for (int word = 0; word < 3; ++word) {
    const auto value = word == 0 ? symbol : (word == 1 ? message : 1u);
    for (int byte = 0; byte < 4; ++byte)
      bytes[word * 4 + byte] = static_cast<std::uint8_t>(value >> (24 - byte * 8));
  }
  return bytes;
}

}  // namespace rb_blitz::messages
