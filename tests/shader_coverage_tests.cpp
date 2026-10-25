#include "check.h"
#include "graphics/shader_coverage.h"
#include "graphics/preparation_assets.h"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <xxhash.h>

using namespace rb_blitz::test;
using namespace rb_blitz::graphics;
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
struct Fixture {
  fs::path root = fs::temp_directory_path()/(
      "rb-shader-coverage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::path cache = root/"cache", journal = root/"evidence"/"discoveries.jsonl";
  Fixture() { fs::create_directories(cache/"shaders"/"shareable"); }
  ~Fixture() { fs::remove_all(root); }
};
void Put(Bytes& b, uint64_t value, size_t n) {
  for (size_t i=0;i<n;++i) b.push_back(uint8_t(value>>(8*i)));
}
Bytes Description(bool dx, uint64_t vertex, uint64_t pixel) {
  Bytes b; Put(b,vertex,8); Put(b,1,8); Put(b,pixel,8); Put(b,1,8);
  b.resize(dx?64:60); return b;
}
std::string Hex(std::span<const uint8_t> bytes) {
  const char* digits="0123456789abcdef";
  std::string s; for (auto b:bytes) { s+=digits[b>>4]; s+=digits[b&15]; } return s;
}
std::string Hash(const Bytes& b) {
  char text[17]; std::snprintf(text,sizeof(text),"%016llx",
      static_cast<unsigned long long>(XXH3_64bits(b.data(),b.size()))); return text;
}
Json Manifest() {
  return Json{{"schema",1},{"title_id","5841122D"},
      {"paths",{{"d3d12/rtv",Json::array()},{"d3d12/rov",Json::array()},
                {"vulkan/fbo",Json::array()},{"vulkan/fsi",Json::array()}}},
      {"sources",Json::array({{{"stage","vertex"},{"hash","0000000000001111"}},
                              {{"stage","pixel"},{"hash","0000000000002222"}}})}};
}
Bytes Store(uint32_t api, uint32_t version, const std::vector<Bytes>& descriptions) {
  Bytes b; Put(b,0x53504558,4); Put(b,api,4);
  for (int i=3;i>=0;--i) b.push_back(uint8_t(version>>(8*i)));
  for (const auto& raw:descriptions) { Put(b,XXH3_64bits(raw.data(),raw.size()),8); b.insert(b.end(),raw.begin(),raw.end()); }
  return b;
}
void Write(const fs::path& file, const Bytes& bytes) {
  std::ofstream f(file,std::ios::binary); f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
std::vector<Json> Lines(const fs::path& file) {
  std::ifstream f(file); std::vector<Json> out; std::string line;
  while (std::getline(f,line)) { auto j=Json::parse(line,nullptr,false); if (!j.is_discarded()) out.push_back(j); }
  return out;
}
void TestDevelopmentScope() {
  BeginCase("only the configured build directory enables collection");
  Fixture f; fs::create_directories(f.root/"build"); fs::create_directories(f.root/"installed");
  CHECK_TRUE(IsDevelopmentExecutable(f.root/"build"/"rb_blitz.exe",f.root/"build"));
  CHECK_FALSE(IsDevelopmentExecutable(f.root/"installed"/"rb_blitz.exe",f.root/"build"));
  CHECK_FALSE(IsDevelopmentExecutable(f.root/"missing"/"rb_blitz.exe",f.root/"build"));
  CHECK_EQ(DevelopmentDirectoryIdentity(f.root/"build").size(),size_t(64));
  CHECK_TRUE(DevelopmentDirectoryIdentity(f.root/"build") ==
             DevelopmentDirectoryIdentity(f.root/"build"/"."));
  CHECK_TRUE(DevelopmentDirectoryIdentity(f.root/"installed") !=
             DevelopmentDirectoryIdentity(f.root/"build"));
  CHECK_TRUE(DevelopmentDirectoryIdentity(f.root/"missing").empty());
}
void TestFourPathsAndRestartDeduplication() {
  BeginCase("four paths record only catalogue misses and deduplicate across launches");
  Fixture f; auto manifest=Manifest();
  auto dx=Description(true,0x1111,0x2222), vk=Description(false,0x1111,0x2222);
  manifest["paths"]["d3d12/rtv"].push_back({{"hash",Hash(dx)},{"raw",Hex(dx)}});
  const auto stores=f.cache/"shaders"/"shareable";
  Write(stores/"5841122D.rtv.d3d12.xpso",Store(0x54525844,kD3D12RecipeStorageVersion,{dx}));
  Write(stores/"5841122D.rov.d3d12.xpso",Store(0x4f525844,kD3D12RecipeStorageVersion,{dx,dx}));
  Write(stores/"5841122D.fbo.vk.xpso",Store(0,kVulkanRecipeStorageVersion,{vk}));
  Write(stores/"5841122D.fsi.vk.xpso",Store(1,kVulkanRecipeStorageVersion,{vk}));
  { ShaderCoverageJournal j(manifest.dump(),f.cache,f.journal); CHECK_EQ(j.Scan(),3); CHECK_EQ(j.Scan(),0); }
  { ShaderCoverageJournal j(manifest.dump(),f.cache,f.journal); CHECK_EQ(j.Scan(),0); }
  auto entries=Lines(f.journal); CHECK_EQ(entries.size(),3);
  for (const auto& entry:entries) {
    CHECK_TRUE(entry.at("unresolved_sources").empty()); CHECK_TRUE(!entry.contains("microcode"));
  }
}
void TestPartialAndCorruptStores() {
  BeginCase("partial tails are retried and stale/corrupt stores cannot publish recipes");
  Fixture f; auto manifest=Manifest();
  auto a=Description(false,0x1111,0x2222), b=Description(false,0x3333,0);
  auto file=f.cache/"shaders"/"shareable"/"5841122D.fbo.vk.xpso";
  auto full=Store(0,kVulkanRecipeStorageVersion,{a,b}), partial=full;
  partial.resize(partial.size()-3); Write(file,partial);
  ShaderCoverageJournal j(manifest.dump(),f.cache,f.journal);
  CHECK_EQ(j.Scan(),1); CHECK_EQ(j.Scan(),0);
  Write(file,full); CHECK_EQ(j.Scan(),1);
  auto entries=Lines(f.journal); CHECK_EQ(entries.size(),2);
  CHECK_EQ(entries.back().at("unresolved_sources").size(),1);
  CHECK_TRUE(entries.back().at("unresolved_sources")[0]=="vertex:0000000000003333");
  // A stale version is ignored, even with a valid description hash.
  Write(file,Store(0,0,{Description(false,0x4444,0)})); CHECK_EQ(j.Scan(),0);
  auto broken=Store(0,kVulkanRecipeStorageVersion,{Description(false,0x5555,0)});
  broken.back()^=1; Write(file,broken); CHECK_EQ(j.Scan(),0);
  Write(file,Store(0,kVulkanRecipeStorageVersion,{Description(false,0x4444,0)})); CHECK_EQ(j.Scan(),1);
}
void TestTruncatedJournalRecoveryAndNewCatalogue() {
  BeginCase("a truncated journal tail stays intact and new entries remain readable");
  Fixture f; auto manifest=Manifest();
  fs::create_directories(f.journal.parent_path());
  { std::ofstream out(f.journal); out << "{unfinished"; }
  auto raw=Description(false,0x1111,0x2222);
  Write(f.cache/"shaders"/"shareable"/"5841122D.fbo.vk.xpso",Store(0,kVulkanRecipeStorageVersion,{raw}));
  { ShaderCoverageJournal j(manifest.dump(),f.cache,f.journal); CHECK_EQ(j.Scan(),1); }
  { ShaderCoverageJournal j(manifest.dump(),f.cache,f.journal); CHECK_EQ(j.Scan(),0); }
  CHECK_EQ(Lines(f.journal).size(),1);
  manifest["paths"]["vulkan/fbo"].push_back({{"hash",Hash(raw)},{"raw",Hex(raw)}});
  { ShaderCoverageJournal j(manifest.dump(),f.cache,f.journal); CHECK_EQ(j.Scan(),0); }
  CHECK_EQ(Lines(f.journal).size(),1); // Promotion never deletes historical evidence.
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 5 && std::string_view(argv[1]) == "--scan") {
    try {
      std::ifstream file(argv[2],std::ios::binary);
      if (!file) throw std::runtime_error("cannot open fixture catalogue");
      std::string catalogue(std::istreambuf_iterator<char>(file),{});
      ShaderCoverageJournal journal(catalogue,argv[3],argv[4]);
      std::printf("%zu discoveries\n",journal.Scan());
      return 0;
    } catch (const std::exception& error) {
      std::fprintf(stderr,"coverage scan: %s\n",error.what());
      return 1;
    }
  }
  TestDevelopmentScope(); TestFourPathsAndRestartDeduplication();
  TestPartialAndCorruptStores(); TestTruncatedJournalRecoveryAndNewCatalogue();
  return Finish();
}
