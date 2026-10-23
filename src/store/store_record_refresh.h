// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace rb_blitz::store {

// LoadedFile's text payload is parsed once by retail. Replacing its source bytes
// alone does not replace the DataArray delivered to the store. Keep the file's
// identity and reference count, reparse fresh owned bytes, then release only the
// file's reference to the old array. Other consumers retain their own references.
// ABI: sub_82468138 (parse), sub_82468320 (destroy), sub_82474ED8 (cache byte count).
template <class Read, class Write, class Parse, class Release>
bool RefreshCachedTextRecord(std::uint32_t manager, std::uint32_t file,
                             std::uint32_t buffer, std::uint32_t size,
                             Read read, Write write, Parse parse, Release release) {
  if (!file || read(file + 52) != 3) return false;
  const auto previous = read(file + 68);
  const auto previous_size = read(file + 60);
  write(file + 56, buffer);
  write(file + 60, size);
  parse(file);  // consumes the buffer and replaces file + 68
  const auto parsed = read(file + 68);
  if (!parsed) {
    write(file + 68, previous);
    write(file + 60, previous_size);
    return false;
  }
  write(manager + 92, read(manager + 92) - previous_size + size);
  if (previous && previous != parsed) release(previous);
  return true;
}

}  // namespace rb_blitz::store
