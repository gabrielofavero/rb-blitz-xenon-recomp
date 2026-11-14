// SPDX-License-Identifier: GPL-2.0-only
#include "fs/screenshots.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <system_error>
#include <vector>

namespace rb_blitz::screenshots {
namespace {

std::string FileStem(std::string_view prefix) {
  const auto separator = prefix.find_last_of("/\\");
  if (separator != std::string_view::npos) prefix.remove_prefix(separator + 1);
  std::string result;
  for (const unsigned char c : prefix.substr(0, 100)) {
    result += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-') ? char(c) : '_';
  }
  return result.empty() ? "screenshot" : result;
}

void Put32(std::array<uint8_t, 54>& header, size_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) header[offset + i] = uint8_t(value >> (i * 8));
}

}  // namespace

std::filesystem::path PicturesDirectory(const std::filesystem::path& user_data_root) {
  if (user_data_root.empty()) return {};
  auto root = user_data_root.lexically_normal();
  if (root.filename().empty()) root = root.parent_path();
  auto leaf = root.filename().u8string();
  for (auto& c : leaf) if (c >= u8'A' && c <= u8'Z') c += u8'a' - u8'A';
  if (leaf == u8"saved games") root = root.parent_path();
  return root / "Pictures";
}

bool SaveBitmap(const std::filesystem::path& directory, std::string_view guest_prefix,
                uint32_t width, uint32_t height, size_t stride,
                std::span<const uint8_t> pixels, std::filesystem::path& saved_path,
                std::string& error) {
  saved_path.clear();
  error.clear();
  const uint64_t row_bytes = uint64_t(width) * 4;
  const uint64_t bmp_stride = (uint64_t(width) * 3 + 3) & ~uint64_t(3);
  const uint64_t bmp_size = bmp_stride * height;
  if (directory.empty() || !width || !height || width > INT32_MAX || height > INT32_MAX ||
      stride < row_bytes || bmp_size > UINT32_MAX - 54 ||
      row_bytes > pixels.size() ||
      uint64_t(height - 1) > (pixels.size() - row_bytes) / stride) {
    error = "invalid screenshot dimensions, pixels or directory";
    return false;
  }

  std::array<uint8_t, 54> header{};
  header[0] = 'B'; header[1] = 'M';
  Put32(header, 2, uint32_t(54 + bmp_size));
  Put32(header, 10, 54);
  Put32(header, 14, 40);
  Put32(header, 18, width);
  Put32(header, 22, height);
  header[26] = 1;  // planes
  header[28] = 24;
  Put32(header, 34, uint32_t(bmp_size));
  std::vector<uint8_t> row(static_cast<size_t>(bmp_stride), 0);

  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec) { error = ec.message(); return false; }
  const auto stem = FileStem(guest_prefix);
  for (uint32_t number = 1; number <= 999999; ++number) {
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_%06u.bmp", number);
    const auto path = directory / (stem + suffix);
    // C11 exclusive mode is supported by UCRT and the native desktop C libraries.
#ifdef _WIN32
    FILE* file = nullptr;
    const auto open_error = _wfopen_s(&file, path.c_str(), L"wbx");
#else
    FILE* file = std::fopen(path.c_str(), "wbx");
    const auto open_error = errno;
#endif
    if (!file) {
      if (open_error == EEXIST) continue;
      error = std::generic_category().message(open_error);
      return false;
    }
    bool ok = std::fwrite(header.data(), 1, header.size(), file) == header.size();
    for (uint32_t y = height; ok && y > 0; --y) {
      const uint8_t* source = pixels.data() + size_t(y - 1) * stride;
      for (uint32_t x = 0; x < width; ++x) {
        row[size_t(x) * 3] = source[size_t(x) * 4 + 2];
        row[size_t(x) * 3 + 1] = source[size_t(x) * 4 + 1];
        row[size_t(x) * 3 + 2] = source[size_t(x) * 4];
      }
      ok = std::fwrite(row.data(), 1, row.size(), file) == row.size();
    }
    if (std::fclose(file) != 0) ok = false;
    if (!ok) {
      error = "could not finish writing screenshot";
      std::filesystem::remove(path, ec);
      return false;
    }
    saved_path = path;
    return true;
  }
  error = "all screenshot numbers are already in use";
  return false;
}

}  // namespace rb_blitz::screenshots
