// SPDX-License-Identifier: GPL-2.0-only
// Bounded binary stdin protocol for the offline resolver. Hash guest-endian bytes
// with the SDK's actual XXH3 implementation, exactly as both pipeline caches do.
#include <array>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <vector>
#include <xxhash.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
#endif
  constexpr uint32_t kMaxBytes = 65536;
  constexpr uint32_t kMaxRecords = 32768;
  for (uint32_t record = 0; record <= kMaxRecords; ++record) {
    std::array<unsigned char, 4> header{};
    std::cin.read(reinterpret_cast<char*>(header.data()), header.size());
    if (std::cin.gcount() == 0 && std::cin.eof()) return 0;
    if (std::cin.gcount() != 4 || record == kMaxRecords) return 1;
    uint32_t size = uint32_t(header[0]) | uint32_t(header[1]) << 8 |
                    uint32_t(header[2]) << 16 | uint32_t(header[3]) << 24;
    if (!size || size > kMaxBytes || size % 4) return 1;
    std::vector<char> bytes(size);
    std::cin.read(bytes.data(), size);
    if (std::cin.gcount() != size) return 1;
    std::cout << std::hex << std::setfill('0') << std::setw(16)
              << XXH3_64bits(bytes.data(), bytes.size()) << '\n';
  }
  return 1;
}
