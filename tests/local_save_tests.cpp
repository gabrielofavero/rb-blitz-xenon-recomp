// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "fs/local_save.h"

#include <chrono>

using namespace rb_blitz::local_save;
using namespace rb_blitz::test;

int main() {
  BeginCase("native save with scores, mapping and stars survives envelope round trip");
  Bytes payload;
  for (uint32_t value : {11, 2, 1001, 45000, 1002, 120000, 5, 3, 2, 1001, 3, 1002, 5, 8})
    Append32(payload, value);
  CHECK_TRUE(ValidPayload(payload));
  Bytes loaded;
  const auto encoded = Encode(payload);
  CHECK_TRUE(Decode(encoded, loaded));
  CHECK_TRUE(loaded == payload);

  BeginCase("empty progress is valid; bad revisions and counts never reach guest loader");
  Bytes empty;
  for (uint32_t value : {11, 0, 0, 0, 0, 0}) Append32(empty, value);
  CHECK_TRUE(ValidPayload(empty));
  for (size_t size = 0; size < payload.size(); ++size)
    CHECK_FALSE(ValidPayload(std::span(payload).first(size)));
  auto bad = payload;
  bad[3] = 12;
  CHECK_FALSE(ValidPayload(bad));
  bad = payload;
  std::fill(bad.begin() + 4, bad.begin() + 8, 0xff);
  CHECK_FALSE(ValidPayload(bad));
  bad = payload;
  std::fill(bad.begin() + 32, bad.begin() + 36, 0xff);
  CHECK_FALSE(ValidPayload(bad));
  bad = payload;
  bad.push_back(0);
  CHECK_FALSE(ValidPayload(bad));

  BeginCase("a checkpoint with progress is told apart from an empty one");
  const auto progress = Summarize(payload);
  CHECK_TRUE(progress.scores == 2);
  CHECK_TRUE(progress.stars == 2);
  CHECK_FALSE(progress.empty());
  CHECK_TRUE(Summarize(empty).empty());
  CHECK_TRUE(Summarize(Bytes{}).empty());  // nothing read yet is not progress

  BeginCase("every single-byte corruption and truncation is rejected");
  for (size_t i = 0; i < encoded.size(); ++i) {
    auto corrupt = encoded;
    corrupt[i] ^= 1;
    CHECK_FALSE(Decode(corrupt, loaded));
    CHECK_FALSE(Decode(std::span(encoded).first(i), loaded));
  }
  CHECK_TRUE(loaded == payload);  // a failed read cannot replace the caller's good data

  BeginCase("real file replacement, failed write, and restart readback");
  const auto root = std::filesystem::temp_directory_path() /
      ("rb-blitz-local-save-test-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto path = root / "profile" / "local_save.bin";
  CHECK_TRUE(Load(path, loaded) == LoadResult::Missing);
  std::string error;
  CHECK_TRUE(Save(path, empty, error));
  CHECK_TRUE(Save(path, payload, error));
  CHECK_TRUE(Load(path, loaded) == LoadResult::Loaded);
  CHECK_TRUE(loaded == payload);
  auto temporary = path;
  temporary += ".tmp";
  std::filesystem::create_directory(temporary);
  CHECK_FALSE(Save(path, empty, error));
  CHECK_TRUE(Load(path, loaded) == LoadResult::Loaded);
  CHECK_TRUE(loaded == payload);
  std::filesystem::remove(temporary);
  std::ofstream(path, std::ios::binary | std::ios::trunc) << "damaged";
  CHECK_TRUE(Load(path, loaded) == LoadResult::Invalid);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  return Finish();
}
