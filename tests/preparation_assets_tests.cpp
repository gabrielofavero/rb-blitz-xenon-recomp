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
  BeginCase("a corrupt shader cache is repaired from installed bytes, never trusted");
  { std::ofstream f(xsh,std::ios::binary|std::ios::trunc); before.back() ^= 1;
    f.write(reinterpret_cast<const char*>(before.data()),before.size()); }
  // P7: this used to refuse the launch. A damaged layer is rebuilt from the
  // installed content instead, and the ledger is the only thing that may report
  // the result as reusable.
  CHECK_FALSE(Refuses(j,root));
  {
    const auto repaired = Read(xsh);
    CHECK_EQ(uint64_t(repaired.size()),uint64_t(8+12+code.size()));
    CHECK_TRUE(repaired.size() > 20 && repaired[0]=='X' && repaired[3]=='H');
    CHECK_EQ(uint64_t(XXH3_64bits(repaired.data()+20,code.size())),
             uint64_t(XXH3_64bits(code.data(),code.size())));
  }

  BeginCase("a stale shader header is replaced instead of failing the launch");
  {
    Bytes stale{'X','E','S','H',9,9,9,9};
    stale.insert(stale.end(),{1,2,3,4});
    std::ofstream f(xsh,std::ios::binary|std::ios::trunc);
    f.write(reinterpret_cast<const char*>(stale.data()),stale.size());
  }
  CHECK_FALSE(Refuses(j,root));
  CHECK_EQ(uint64_t(Read(xsh).size()),uint64_t(8+12+code.size()));

  BeginCase("a layer is reused only for the inputs it was built from");
  {
    PreparationInputs first;
    first.catalogue = 1; first.content = 2; first.executable = 3;
    SeedPreparation(j.dump(),root,[](const std::string&) { return Bytes{1,2,3,4}; },{},
                    [] {return false;},first,0);
    int reads = 0;
    const auto reused = SeedPreparation(j.dump(),root,
        [&](const std::string&) { ++reads; return Bytes{1,2,3,4}; },{},
        [] {return false;},first,0);
    CHECK_TRUE(reused.extraction_reused);
    CHECK_EQ(uint64_t(reads),0);
    // A changed content identity must rebuild the shader layer, and the repair
    // is counted so the log can say which layer was affected.
    PreparationInputs moved = first; moved.content = 99;
    reads = 0;
    const auto rebuilt = SeedPreparation(j.dump(),root,
        [&](const std::string&) { ++reads; return Bytes{1,2,3,4}; },{},
        [] {return false;},moved,0);
    CHECK_FALSE(rebuilt.extraction_reused);
    // The record itself is hash-verified, so a byte-identical shader needs no
    // new asset read; what changed is that the layer is no longer reusable for
    // the old content and is recorded again under the new identity.
    CHECK_TRUE(rebuilt.repaired_layers > 0);
    CHECK_EQ(rebuilt.shaders,uint64_t(1));
  }

  BeginCase("a recipe store of another storage version is replaced with the current one");
  {
    const Bytes raw(64,7);
    const uint64_t hash = XXH3_64bits(raw.data(),raw.size());
    static const char* digits = "0123456789abcdef";
    std::string hex;
    for (const auto byte : raw) { hex.push_back(digits[byte>>4]); hex.push_back(digits[byte&15]); }
    Json with_path = j;
    with_path["paths"] = Json{{"d3d12/rtv",
        Json::array({{{"raw",hex},{"hash",Hex(hash)}}})}};
    const auto store = root/"shaders/shareable/5841122D.rtv.d3d12.xpso";
    {
      // A store written by an older translator: same layout, other version word.
      Bytes stale{'X','E','P','S',0x44,0x58,0x52,0x54,0x20,0x26,0x02,0x99};
      std::ofstream f(store,std::ios::binary|std::ios::trunc);
      f.write(reinterpret_cast<const char*>(stale.data()),stale.size());
    }
    PreparationInputs inputs;
    inputs.catalogue = 5; inputs.content = 6; inputs.executable = 7;
    inputs.d3d12_version = kD3D12RecipeStorageVersion;
    inputs.vulkan_version = kVulkanRecipeStorageVersion;
    const auto report = SeedPreparation(with_path.dump(),root,
        [](const std::string&) { return Bytes{1,2,3,4}; },{},[] {return false;},inputs,0);
    CHECK_FALSE(report.recipes_reused[0]);
    CHECK_TRUE(report.repaired_layers > 0);
    const auto written = Read(store);
    CHECK_EQ(uint64_t(written.size()),uint64_t(12+72));
    CHECK_EQ(uint64_t(written[11]),uint64_t(kD3D12RecipeStorageVersion & 0xFF));
    CHECK_EQ(uint64_t(written[8]),uint64_t((kD3D12RecipeStorageVersion>>24) & 0xFF));
    CHECK_EQ(report.records[0],uint64_t(1));
    // A second pass with the same inputs reuses it and touches no asset.
    int reads = 0;
    const auto again = SeedPreparation(with_path.dump(),root,
        [&](const std::string&) { ++reads; return Bytes{}; },{},[] {return false;},inputs,0);
    CHECK_TRUE(again.recipes_reused[0]);
    CHECK_EQ(uint64_t(reads),0);

    BeginCase("a catalogue update merges with compatible local discoveries");
    // Simulate a locally learned recipe, then install a catalogue that doesn't
    // know it. A changed catalogue identity must merge rather than overwrite.
    const Bytes discovered(64,8);
    std::string discovered_hex;
    for (const auto byte : discovered) {
      discovered_hex.push_back(digits[byte>>4]); discovered_hex.push_back(digits[byte&15]);
    }
    auto learned = with_path;
    learned["paths"]["d3d12/rtv"].push_back({{"raw",discovered_hex},
        {"hash",Hex(XXH3_64bits(discovered.data(),discovered.size()))}});
    auto changed = inputs; changed.catalogue = 101;
    const auto extra = SeedPreparation(learned.dump(),root,
        [](const std::string&) {return Bytes{1,2,3,4};},{},[] {return false;},changed,0);
    CHECK_EQ(extra.records[0],uint64_t(2));
    const auto learned_bytes = Read(store);
    changed.catalogue = 102;
    const auto updated = SeedPreparation(with_path.dump(),root,
        [](const std::string&) {return Bytes{1,2,3,4};},{},[] {return false;},changed,0);
    CHECK_EQ(updated.records[0],uint64_t(2));
    CHECK_TRUE(Read(store) == learned_bytes);
    std::filesystem::remove(store);
  }

  std::filesystem::remove(xsh);
  return Finish();
}
