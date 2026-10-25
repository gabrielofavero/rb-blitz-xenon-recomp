// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "graphics/preparation_assets.h"
#include "graphics/preparation_archive.h"
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <xxhash.h>

using namespace rb_blitz::test;
using namespace rb_blitz::graphics;
using Json = nlohmann::json;
Bytes Read(const std::filesystem::path& path) {
  std::ifstream f(path,std::ios::binary);
  if (!f) throw std::runtime_error("missing test input");
  return Bytes(std::istreambuf_iterator<char>(f),{});
}
Bytes ReadRange(const std::filesystem::path& path, uint64_t offset, size_t size) {
  std::ifstream f(path,std::ios::binary);
  if (!f || size > 32*1024*1024) throw std::runtime_error("invalid archive test input");
  f.seekg(offset);
  Bytes bytes(size);
  if (!f.read(reinterpret_cast<char*>(bytes.data()),size)) throw std::runtime_error("short archive test read");
  return bytes;
}
std::string Hex(uint64_t v) {
  char out[17]; std::snprintf(out,sizeof(out),"%016llx",static_cast<unsigned long long>(v)); return out;
}
bool Refuses(const Json& j, const std::filesystem::path& root, bool cancel = false) {
  try { SeedPreparation(j.dump(),root,[](const std::string&) { return Bytes{1,2,3,4}; },{},[&] { return cancel; }); }
  catch (const std::exception&) { return true; }
  return false;
}
int main(int argc, char** argv) {
  if (argc == 6 && std::string_view(argv[1]) == "archives") {
    try {
      using namespace rb_blitz::settings::ultimate_adapter;
      const auto text = Read(argv[2]), image = Read(argv[4]);
      const std::filesystem::path game = argv[3];
      std::vector<ArkIndex> indexes;
      for (const auto* name : {"gen/patch_xbox.hdr","gen/main_xbox.hdr"}) {
        if (!std::filesystem::exists(game/name)) continue;
        auto bytes = Read(game/name);
        ArkIndex index;
        std::string why;
        if (!ReadArkIndex(bytes.data(),bytes.size(),&index,&why)) throw std::runtime_error(why);
        indexes.push_back(std::move(index));
      }
      SeedPreparation(std::string(text.begin(),text.end()),argv[5],[&](const std::string& name) -> Bytes {
        for (const auto& index : indexes) {
          const auto* entry = FindPreparationAsset(index,name);
          if (!entry) continue;
          if (entry->compressed()) throw std::runtime_error("compressed test asset");
          uint64_t offset = entry->offset;
          for (size_t i = 0; i < index.parts.size(); ++i) {
            if (offset >= index.part_sizes[i]) { offset -= index.part_sizes[i]; continue; }
            if (entry->size > index.part_sizes[i]-offset) break;
            std::printf("Resolved %s as %s\n",name.c_str(),entry->path.c_str());
            return ReadRange(game/index.parts[i],offset,entry->size);
          }
          throw std::runtime_error("test asset crosses part boundary");
        }
        throw std::runtime_error("test shader asset missing: "+name);
      },image,[] {return false;});
      std::printf("Installed ARK preparation inputs verified.\n");
      return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
  }
  // Optional installed-data leg is headless: no guest CPU or UI is started.
  if (argc == 5 && std::string_view(argv[1]) == "installed") {
    try {
      const auto text = Read(argv[2]), image = Read(std::filesystem::path(argv[3])/"image");
      SeedPreparation(std::string(text.begin(),text.end()),argv[4],
          [&](const std::string& name) { return Read(std::filesystem::path(argv[3])/name); },image,[] { return false; });
      std::printf("Installed preparation catalogue extracted and merged.\n");
      return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
  }
  BeginCase("root shader assets resolve both authored and ARK dot-prefixed paths");
  {
    rb_blitz::settings::ultimate_adapter::ArkIndex index;
    index.entries = {{"./xbox_shaders",123,456,0},{".\\xbox_preinit_shaders",10,20,0}};
    const auto* shader = FindPreparationAsset(index,"xbox_shaders");
    CHECK_TRUE(shader != nullptr);
    if (shader) CHECK_EQ(shader->offset,123);
    CHECK_TRUE(FindPreparationAsset(index,"xbox_preinit_shaders") != nullptr);
    CHECK_TRUE(FindPreparationAsset(index,"../xbox_shaders") == nullptr);
    index.entries[0].path = "xbox_shaders";
    CHECK_TRUE(FindPreparationAsset(index,"xbox_shaders") != nullptr);
  }
  const auto root = std::filesystem::current_path()/"preparation-assets-test";
  // This test owns exactly this directory; each run removes only its known files.
  std::filesystem::create_directories(root/"shaders/shareable");
  const auto xsh = root/"shaders/shareable/5841122D.xsh";
  std::filesystem::remove(xsh);
  const Bytes code{1,2,3,4};
  Json j{{"schema",1},{"title_id","5841122D"},{"assets",{{"xbox_shaders",{{"bytes",4}}}}},
      {"sources",Json::array({{{"stage","pixel"},{"hash",Hex(XXH3_64bits(code.data(),code.size()))},
          {"asset","xbox_shaders"},{"offset",0},{"bytes",4}}})},{"paths",Json::object()}};
  BeginCase("unresolved, wrong identity and cancelled inputs never publish a shader cache");
  auto bad = j; bad["sources"][0]["bytes"] = 5;
  CHECK_TRUE(Refuses(bad,root)); CHECK_FALSE(std::filesystem::exists(xsh));
  bad = j; bad["sources"][0]["hash"] = "1111111111111111";
  CHECK_TRUE(Refuses(bad,root)); CHECK_FALSE(std::filesystem::exists(xsh));
  CHECK_TRUE(Refuses(j,root,true)); CHECK_FALSE(std::filesystem::exists(xsh));
  BeginCase("warm extraction validates local bytes and needs no installed asset read");
  CHECK_FALSE(Refuses(j,root));
  auto before = Read(xsh);
  int reads = 0;
  SeedPreparation(j.dump(),root,[&](const std::string&) { ++reads; return Bytes{}; },{},[] {return false;});
  CHECK_EQ(reads,0); CHECK_TRUE(before == Read(xsh));
  BeginCase("a corrupt shader cache cannot satisfy readiness");
  { std::ofstream f(xsh,std::ios::binary|std::ios::trunc); before.back() ^= 1;
    f.write(reinterpret_cast<const char*>(before.data()),before.size()); }
  CHECK_TRUE(Refuses(j,root));
  std::filesystem::remove(xsh);
  return Finish();
}
