// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <rex/memory/utils.h>

namespace rb_blitz::platform {

inline bool GuestRangeReadable(const void* pointer, size_t bytes) {
  size_t available = 0;
  rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
  return pointer && rex::memory::QueryProtect(const_cast<void*>(pointer), available, access) &&
         available >= bytes &&
         (static_cast<unsigned>(access) & static_cast<unsigned>(rex::memory::PageAccess::kReadOnly));
}

// POSIX mprotect requires page alignment; the SDK's Protect implementation does
// not currently apply the alignment promised by its public header.
inline bool WriteProtectedGuestBe32(uint8_t* pointer, uint32_t value) {
  if (!GuestRangeReadable(pointer, sizeof(value))) return false;
  const size_t page_size = rex::memory::page_size();
  const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
  const uintptr_t start = address - address % page_size;
  const uintptr_t end = ((address + sizeof(value) + page_size - 1) / page_size) * page_size;
  rex::memory::PageAccess previous = rex::memory::PageAccess::kNoAccess;
  if (!rex::memory::Protect(reinterpret_cast<void*>(start), end - start,
                            rex::memory::PageAccess::kReadWrite, &previous)) return false;
  pointer[0] = static_cast<uint8_t>(value >> 24);
  pointer[1] = static_cast<uint8_t>(value >> 16);
  pointer[2] = static_cast<uint8_t>(value >> 8);
  pointer[3] = static_cast<uint8_t>(value);
  return rex::memory::Protect(reinterpret_cast<void*>(start), end - start, previous);
}

}  // namespace rb_blitz::platform
