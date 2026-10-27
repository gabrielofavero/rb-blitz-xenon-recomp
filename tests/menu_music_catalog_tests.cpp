// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_music_catalog.h"
#include <chrono>

using namespace rb_blitz::shell_music;

int main(int argc, char** argv) {
  if (argc == 4 && std::string_view(argv[1]) == "--benchmark") {
    // Read-only validation against real containers, with synthetic metadata.
    // The disposable output is separate from the runtime's real catalog.
    std::vector<catalog::Package> packages;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(argv[2]))
      if (entry.is_regular_file()) paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    const auto identities = catalog::Identities(paths);
    for (size_t i = 0; i < paths.size(); ++i) {
      const auto key = "Benchmark" + std::to_string(i);
      packages.push_back({paths[i], identities[i],
          {{"", "songs/" + key + "/" + key, key, paths[i].filename().string(), "Benchmark Artist"}}});
    }
    std::string why;
    CHECK_TRUE(catalog::Save(argv[3], packages, &why));
    for (int trial = 0; trial < 3; ++trial) {
      const auto start = std::chrono::steady_clock::now();
      const auto loaded = catalog::Load(argv[3], paths);
      const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
      size_t reused = 0;
      for (const auto& package : loaded) reused += package.identity.has_value();
      CHECK_EQ(reused, paths.size());
      std::printf("catalog stat-validation benchmark: %zu packages, %llu bytes, %.3f ms (trial %d, synthetic metadata)\n",
          reused, static_cast<unsigned long long>(std::filesystem::file_size(argv[3])), elapsed, trial + 1);
    }
    return rb_blitz::test::Finish();
  }
  using rb_blitz::test::BeginCase;
  const auto root = std::filesystem::temp_directory_path() /
      ("rbbz-music-catalog-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "extracted" / "songs");
  const auto first = root / "first.con", second = root / "second.con";
  { std::ofstream(first) << "one"; std::ofstream(second) << "two"; }
  { std::ofstream(root / "extracted" / "songs" / "songs.dta") << "metadata"; }
  const auto cache = root / "catalog.cache";
  const PoolEntry song{"old_alias", "songs/Track/Track", "CaseSensitiveKey", "A \"title\"\nwith a line", "Artist",
                       "songs/Track/gen/Track_keep.png_xbox"};
  std::vector<catalog::Package> packages{{first, catalog::Identity(first), {song}},
      {second, catalog::Identity(second), {song}},
      {root / "extracted", catalog::Identity(root / "extracted"), {song}}};

  BeginCase("bounded catalog codec preserves metadata and rejects partial/corrupt files");
  const auto encoded = catalog::Encode(packages);
  std::vector<catalog::Package> decoded;
  CHECK_TRUE(catalog::Decode(encoded, &decoded));
  CHECK_EQ(decoded.size(), 3u);
  CHECK_TRUE(decoded[0].path == first);
  CHECK_TRUE(decoded[0].identity == packages[0].identity);
  CHECK_TRUE(decoded[0].songs[0].title == song.title);
  CHECK_TRUE(decoded[0].songs[0].song_key == song.song_key);
  CHECK_TRUE(decoded[0].songs[0].artwork_locator == song.artwork_locator);
  CHECK_TRUE(decoded[0].songs[0].alias.empty()); // rebuilt from this boot's package index
  CHECK_FALSE(catalog::Decode(encoded.substr(0, encoded.size()/2), &decoded));
  CHECK_FALSE(catalog::Decode(encoded + "junk", &decoded));
  CHECK_FALSE(catalog::Decode("RBBMUSIC0\n0\n", &decoded));
  auto invalid = packages;
  invalid[0].songs[0].stream = "../outside";
  CHECK_FALSE(catalog::Decode(catalog::Encode(invalid), &decoded));
  invalid[0].songs[0].stream = "otherdevice:audio";
  CHECK_FALSE(catalog::Decode(catalog::Encode(invalid), &decoded));

  BeginCase("cached metadata reuses file identities, follows paths across reordering, and invalidates changes");
  std::string why;
  CHECK_TRUE(catalog::Save(cache, packages, &why));
  auto loaded = catalog::Load(cache, {second, first, root / "extracted"});
  CHECK_TRUE(loaded[0].identity == packages[1].identity);
  CHECK_TRUE(loaded[1].identity == packages[0].identity);
  CHECK_TRUE(loaded[2].identity.has_value());
  { std::ofstream(first, std::ios::app) << "changed"; }
  { std::ofstream(root / "extracted" / "songs" / "new.mogg") << "new stream"; }
  loaded = catalog::Load(cache, {first, second, root / "extracted", root / "missing.con"});
  CHECK_FALSE(loaded[0].identity.has_value());
  CHECK_TRUE(loaded[0].songs.empty());
  CHECK_TRUE(loaded[1].identity.has_value());
  CHECK_FALSE(loaded[2].identity.has_value());
  CHECK_FALSE(loaded[3].identity.has_value());
  std::filesystem::remove(first);
  std::filesystem::remove(second);
  std::filesystem::remove(root / "extracted" / "songs" / "songs.dta");
  std::filesystem::remove(root / "extracted" / "songs" / "new.mogg");
  std::filesystem::remove(root / "extracted" / "songs");
  std::filesystem::remove(root / "extracted");
  std::filesystem::remove(cache);
  std::filesystem::remove(root);
  return rb_blitz::test::Finish();
}
