// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "fs/screenshots.h"

#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

using namespace rb_blitz::screenshots;
using namespace rb_blitz::test;

namespace {
std::vector<uint8_t> Read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
uint32_t LE32(const std::vector<uint8_t>& bytes, size_t at) {
  return uint32_t(bytes.at(at)) | uint32_t(bytes.at(at + 1)) << 8 |
      uint32_t(bytes.at(at + 2)) << 16 | uint32_t(bytes.at(at + 3)) << 24;
}
}  // namespace

int main() {
  BeginCase("pictures follow the title root and legacy Saved Games layout");
  CHECK_TRUE(PicturesDirectory("Blitz") == std::filesystem::path("Blitz/Pictures"));
  CHECK_TRUE(PicturesDirectory("Blitz/") == std::filesystem::path("Blitz/Pictures"));
  CHECK_TRUE(PicturesDirectory("Blitz/Saved Games") == std::filesystem::path("Blitz/Pictures"));
  CHECK_TRUE(PicturesDirectory("Blitz/saved games/") == std::filesystem::path("Blitz/Pictures"));
  CHECK_TRUE(PicturesDirectory("custom-data") == std::filesystem::path("custom-data/Pictures"));
  CHECK_TRUE(PicturesDirectory({}).empty());

  const auto root = std::filesystem::temp_directory_path() /
      ("rb-blitz-screenshots-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto directory = root / std::filesystem::path(u8"Blitz-\u00e7") / "Pictures";
  // Top row red, green, blue; bottom row white, black, yellow. Source padding
  // has sentinel bytes and the final row omits padding, per RawImage's contract.
  const std::array<uint8_t, 28> rgba = {
      255,0,0,7, 0,255,0,8, 0,0,255,9, 99,99,99,99,
      255,255,255,10, 0,0,0,11, 255,255,0,12};
  std::filesystem::path saved;
  std::string error;
  BeginCase("BMP header, RGB channels, bottom-up orientation and row padding");
  CHECK_TRUE(SaveBitmap(directory, "screenshots/song", 3, 2, 16, rgba, saved, error));
  CHECK_TRUE(saved == directory / "song_000001.bmp");
  const auto bytes = Read(saved);
  CHECK_TRUE(bytes.size() == 78);
  CHECK_TRUE(bytes.at(0) == 'B' && bytes.at(1) == 'M');
  CHECK_TRUE(LE32(bytes, 2) == bytes.size());
  CHECK_TRUE(LE32(bytes, 10) == 54 && LE32(bytes, 14) == 40);
  CHECK_TRUE(LE32(bytes, 18) == 3 && LE32(bytes, 22) == 2);
  CHECK_TRUE(bytes.at(26) == 1 && bytes.at(28) == 24);
  CHECK_TRUE(LE32(bytes, 30) == 0 && LE32(bytes, 34) == 24);
  const std::vector<uint8_t> expected = {
      255,255,255, 0,0,0, 0,255,255, 0,0,0,
      0,0,255, 0,255,0, 255,0,0, 0,0,0};
  CHECK_TRUE(std::vector<uint8_t>(bytes.begin() + 54, bytes.end()) == expected);

  BeginCase("numbered captures never replace an existing file, including concurrent saves");
  CHECK_TRUE(SaveBitmap(directory, "GAME:\\screenshots\\song", 3, 2, 16, rgba, saved, error));
  CHECK_TRUE(saved.filename() == "song_000002.bmp");
  std::array<std::filesystem::path, 4> concurrent_paths;
  std::array<bool, 4> successes{};
  std::vector<std::thread> writers;
  for (size_t i = 0; i < concurrent_paths.size(); ++i) {
    writers.emplace_back([&, i] {
      std::string failure;
      successes[i] = SaveBitmap(directory, "song", 3, 2, 16, rgba, concurrent_paths[i], failure);
    });
  }
  for (auto& writer : writers) writer.join();
  for (size_t i = 0; i < concurrent_paths.size(); ++i) {
    CHECK_TRUE(successes[i]);
    CHECK_TRUE(Read(concurrent_paths[i]) == bytes);
    for (size_t j = 0; j < i; ++j) CHECK_TRUE(concurrent_paths[i] != concurrent_paths[j]);
  }
  CHECK_TRUE(Read(directory / "song_000001.bmp") == bytes);

  BeginCase("guest prefixes cannot escape Pictures or create invalid filenames");
  CHECK_TRUE(SaveBitmap(directory, "../../bad:name?", 3, 2, 16, rgba, saved, error));
  CHECK_TRUE(saved == directory / "bad_name__000001.bmp");
  CHECK_TRUE(SaveBitmap(directory, "", 3, 2, 16, rgba, saved, error));
  CHECK_TRUE(saved.filename() == "screenshot_000001.bmp");

  BeginCase("invalid images and file errors report failure without a saved path");
  CHECK_FALSE(SaveBitmap(directory, "invalid", 0, 2, 16, rgba, saved, error));
  CHECK_TRUE(saved.empty() && !error.empty());
  CHECK_FALSE(SaveBitmap(directory, "invalid", 3, 0, 16, rgba, saved, error));
  CHECK_FALSE(SaveBitmap(directory, "invalid", 3, 2, 11, rgba, saved, error));
  CHECK_FALSE(SaveBitmap(directory, "invalid", 3, 3, 16, rgba, saved, error));
  CHECK_FALSE(SaveBitmap(directory, "invalid", UINT32_MAX, UINT32_MAX, 0, rgba, saved, error));
  CHECK_FALSE(SaveBitmap(directory, "invalid", 3, 2, SIZE_MAX, rgba, saved, error));
  CHECK_FALSE(SaveBitmap({}, "invalid", 3, 2, 16, rgba, saved, error));
  std::ofstream(root / "blocked") << "keep";
  CHECK_FALSE(SaveBitmap(root / "blocked/Pictures", "song", 3, 2, 16, rgba, saved, error));
  CHECK_TRUE(saved.empty() && !error.empty());
  CHECK_TRUE(Read(root / "blocked") == std::vector<uint8_t>({'k','e','e','p'}));

  // Cleanup is confined to the unique directory created by this test.
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  return Finish();
}
