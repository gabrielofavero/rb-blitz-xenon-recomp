// SPDX-License-Identifier: GPL-2.0-only
#include "platform/guest_memory.h"

#include <cstdio>
#include <sys/mman.h>

int main() {
  using namespace rb_blitz::platform;
  const size_t page = rex::memory::GetPageSize();
  auto* bytes = static_cast<uint8_t*>(mmap(nullptr, page * 2, PROT_READ,
                                         MAP_PRIVATE | MAP_ANON, -1, 0));
  if (bytes == MAP_FAILED) return 1;
  int failures = 0;
  auto check = [&](bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); ++failures; }
  };
  check(WriteProtectedGuestBe32(bytes + 12, 0x12345678), "unaligned literal write failed");
  check(bytes[12] == 0x12 && bytes[13] == 0x34 && bytes[14] == 0x56 && bytes[15] == 0x78,
        "literal is not big endian");
  size_t available = 0;
  rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
  check(rex::memory::QueryProtect(bytes + 12, available, access) &&
        access == rex::memory::PageAccess::kReadOnly, "original read-only protection was not restored");
  check(!GuestRangeReadable(nullptr, 4), "null range was readable");
  check(mprotect(bytes + page, page, PROT_NONE) == 0, "guard page setup failed");
  check(!GuestRangeReadable(bytes + page, 4), "no-access range was readable");
  check(!GuestRangeReadable(bytes + page - 2, 4), "range crossed a protection boundary");
  check(!WriteProtectedGuestBe32(bytes + page, 0x12345678), "write accepted a no-access page");
  munmap(bytes, page * 2);
  return failures ? 1 : 0;
}
