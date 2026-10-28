// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <string_view>

namespace rb_blitz::ui::music {

// Song DTA display fields can contain Latin-1 bytes, while ImGui expects UTF-8.
// Preserve valid UTF-8 sequences so newer packages and mixed-encoding fields work too.
inline std::string DisplayTextUtf8(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i]);
    size_t length = 0;
    if (lead < 0x80) length = 1;
    else if (lead >= 0xC2 && lead <= 0xDF) length = 2;
    else if (lead >= 0xE0 && lead <= 0xEF) length = 3;
    else if (lead >= 0xF0 && lead <= 0xF4) length = 4;
    if (length > 1 && i + length <= text.size()) {
      const auto second = static_cast<unsigned char>(text[i + 1]);
      if ((second & 0xC0) != 0x80 ||
          (lead == 0xE0 && second < 0xA0) ||
          (lead == 0xED && second >= 0xA0) ||
          (lead == 0xF0 && second < 0x90) ||
          (lead == 0xF4 && second >= 0x90)) length = 0;
      for (size_t j = 2; length && j < length; ++j)
        if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) length = 0;
    } else if (length > 1) {
      length = 0;
    }
    if (length) {
      out.append(text.substr(i, length));
      i += length;
    } else {
      out += static_cast<char>(0xC0 | (lead >> 6));
      out += static_cast<char>(0x80 | (lead & 0x3F));
      ++i;
    }
  }
  return out;
}

}  // namespace rb_blitz::ui::music
