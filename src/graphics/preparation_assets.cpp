// SPDX-License-Identifier: GPL-2.0-only
#include "graphics/preparation_assets.h"
#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <map>
#include <stdexcept>
#include <tuple>
#include <nlohmann/json.hpp>
#include <xxhash.h>
#include "fs/atomic_file.h"

namespace rb_blitz::graphics {
namespace {
using Json = nlohmann::json;
void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
uint32_t BE(std::span<const uint8_t> b, size_t p, size_t n = 4) {
  Require(p <= b.size() && n <= b.size() - p, "shader source outside installed data");
  uint32_t v = 0;
  for (size_t i = 0; i < n; ++i) v = v << 8 | b[p + i];
  return v;
}
uint64_t LE(std::span<const uint8_t> b, size_t p, size_t n) {
  Require(p <= b.size() && n <= b.size() - p, "truncated shader cache record");
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i) v |= uint64_t(b[p + i]) << (8 * i);
  return v;
}
void Put(Bytes& b, uint64_t v, size_t n) {
  for (size_t i = 0; i < n; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
uint64_t Hex(const std::string& s) { return std::stoull(s, nullptr, 16); }
Bytes Unhex(const std::string& s) {
  Require(s.size() % 2 == 0, "invalid recipe hex");
  Bytes b;
  for (size_t i = 0; i < s.size(); i += 2) b.push_back(uint8_t(Hex(s.substr(i, 2))));
  return b;
}
uint32_t Mask(unsigned s, unsigned e) {
  return s <= e ? (UINT32_MAX >> s) & (UINT32_MAX << (31 - e))
                : (UINT32_MAX >> s) | (UINT32_MAX << (31 - e));
}
uint32_t RM(uint32_t v, int r, unsigned s, unsigned e) { return std::rotl(v, r) & Mask(s, e); }
uint32_t RI(uint32_t d, uint32_t v, int r, unsigned s, unsigned e) {
  const auto m = Mask(s, e);
  return (d & ~m) | (std::rotl(v, r) & m);
}
using Words = std::array<uint32_t, 3>;
using Element = std::array<uint32_t, 7>;
// Same declaration linker as scripts/shaders/shader_vertex_link.py / sub_82521908.
Words LinkFetch(Words w, const Element* e, const std::array<uint32_t, 16>& strides,
                const std::array<uint32_t, 8>& masks) {
  auto [a, b, c] = w;
  uint32_t swizzle;
  if (!e) {
    a = RI(a, 95, 20, 2, 11);
    b = (b & 0xbfc6cfff) | 0x60000;
    c = (c & 0x80000000) | strides[0];
    swizzle = 37456;
  } else {
    const auto stream = (*e)[0], offset = (*e)[1], fmt = (*e)[2];
    swizzle = RM(fmt, 16, 29, 31) | RM(fmt, 22, 26, 28);
    swizzle = RM(swizzle, 3, 0, 28) | RM(fmt, 13, 29, 31);
    swizzle = RM(swizzle, 1, 0, 30) | RM(fmt, 0, 16, 21);
    swizzle = RM(swizzle, 3, 0, 28) & 65535;
    if (RM(fmt, 0, 24, 25) == 64) {
      for (unsigned shift : {13u, 10u, 7u, 4u})
        if ((swizzle & (7u << shift)) <= (3u << shift)) swizzle ^= 1u << shift;
    }
    uint32_t encoded = (95 - stream) * 0x1560056;
    encoded = RI(encoded, encoded, 11, 8, 12);
    a = RM(RI(a, encoded, 1, 5, 11), 0, 5, 1);
    const auto flags = RM(fmt, 12, 14, 19) | RM(fmt, 0, 22, 23);
    b = RM(flags, 4, 0, 27) | (b & 0xbfc0cfff);
    c = (c & 0x80000000) | RM(offset, 6, 1, 23) | strides[stream];
  }
  swizzle |= 14;
  auto v = masks[RM(b, 1, 28, 30) / 2] & swizzle;
  auto low = v & 65535;
  auto x = RM(v, 29, 19, 31) | low;
  for (int i = 0; i < 3; ++i) x = RM(x, 29, 3, 31) | low;
  b = RI(b, x, 31, 29, 31);
  for (unsigned component = 0; component < 3; ++component) {
    v = masks[RM(b, 30 - component * 3, 28, 30) / 2] & swizzle;
    low = v & 65535;
    auto y = RM(v, 29, 19, 31) | low;
    if (component == 0) {
      x = RM(v, 2, 14, 29);
      for (int i = 0; i < 2; ++i) y = RM(y, 29, 3, 31) | low;
      x |= RM(y, 31, 1, 31);
    } else if (component == 1) {
      x = RM(v, 3, 13, 28) | low;
      y = RM(y, 29, 3, 31) | low;
      x = RM(x, 2, 0, 29) | RM(y, 31, 1, 31);
    } else {
      x = RM(v, 3, 13, 28) | low;
      y = RM(v, 29, 19, 31) | low;
      x = RM(RM(x, 3, 0, 28) | low, 2, 0, 29) | RM(y, 31, 1, 31);
    }
    b = RI(x, b, 0, 29 - component * 3, 25 - component * 3);
  }
  return {a, b, c};
}
Bytes Link(Bytes code, const Json& source, const Json& manifest, std::span<const uint8_t> image) {
  const auto& config = manifest.at("layouts");
  const auto name = source.at("vertex_layout");
  const auto& layouts = config.at("layouts");
  auto layout = std::find_if(layouts.begin(), layouts.end(), [&](const Json& l) { return l.at("name") == name; });
  Require(layout != layouts.end(), "unknown vertex declaration");
  std::array<uint32_t, 8> masks;
  for (size_t i = 0; i < masks.size(); ++i) masks[i] = BE(image, config.at("swizzle_masks_offset").get<size_t>() + i * 2, 2);
  std::array<uint32_t, 16> strides{};
  auto s = layout->at("strides").get<std::vector<uint32_t>>();
  Require(s.size() <= strides.size(), "too many vertex streams");
  std::copy(s.begin(), s.end(), strides.begin());
  std::vector<Element> elements;
  if (layout->contains("fvf")) {
    Require(layout->at("fvf") == 450, "unknown FVF");
    elements = {{0,0,0x2a23b9,0,0,0,0}, {0,12,0x182886,0,10,0,0},
                {0,16,0x182886,0,10,1,0}, {0,20,0x2c23a5,0,5,0,0}};
  } else {
    size_t p = layout->at("declaration_offset");
    for (size_t i = 0; i <= 32; ++i, p += 12) {
      const auto stream = BE(image, p, 2);
      if (stream == 255) break;
      Require(i < 32 && stream < 16, "invalid vertex declaration");
      elements.push_back({stream, BE(image,p+2,2), BE(image,p+4), BE(image,p+8,1),
                         BE(image,p+9,1), BE(image,p+10,1), BE(image,p+11,1)});
    }
  }
  const auto fetches = source.at("fetches").get<std::vector<uint32_t>>();
  Require(fetches.size() <= 32, "too many vertex fetches");
  std::vector<Words> words;
  for (const auto f : fetches) {
    const size_t p = (f & 4095) * 12;
    Words w{BE(code,p), BE(code,p+4), BE(code,p+8)};
    Require((w[0] & 31) == 0, "non-fetch vertex source");
    auto e = std::find_if(elements.begin(), elements.end(), [&](const Element& e) {
      return e[4] == ((f >> 12) & 15) && e[5] == ((f >> 16) & 15);
    });
    words.push_back(LinkFetch(w, e == elements.end() ? nullptr : &*e, strides, masks));
  }
  auto key = [](Words w) { return RM(w[0],14,25,29) | RM(w[0],7,30,31); };
  auto offset = [](Words w) { return int32_t(RM(w[2],1,0,30)) >> 9; };
  auto width = [&](Words w) { return BE(image, config.at("format_sizes_offset").get<size_t>() + ((w[1] >> 16) & 63), 1); };
  size_t cursor = 0;
  while (cursor < words.size()) {
    while (cursor < words.size() && !(fetches[cursor] & 0x300000)) ++cursor;
    size_t end = cursor + 1;
    while (end < words.size() && !(fetches[end] & 0x300000)) ++end;
    if (end >= words.size()) break;
    const auto start = cursor;
    cursor = end + 1;
    const bool special = (fetches[end] & 0x300000) == 0x300000;
    const auto anchor = special ? key(words[end]) : UINT32_MAX;
    std::stable_sort(words.begin()+start, words.begin()+end+(special?0:1), [&](Words a, Words b) {
      return std::tuple(key(a)==anchor, -int64_t(key(a)), offset(a), width(a)) <
             std::tuple(key(b)==anchor, -int64_t(key(b)), offset(b), width(b));
    });
    size_t previous = start;
    for (size_t current = start + 1; current <= end; ++current) {
      auto& a = words[previous]; auto& b = words[current];
      const auto ak = RM(RI(a[0],a[0],25,12,13),0,7,13);
      const auto bk = RM(RI(b[0],b[0],25,12,13),0,7,13);
      const int span = offset(b) - offset(a) + int(width(b));
      if (ak == bk && offset(a) <= offset(b) && span <= 8) {
        a[0] = RI(a[0], uint32_t(std::max(int(RM(a[0],5,29,31)),span-1)),27,2,4);
        b[1] |= 0x40000000;
      } else previous = current;
    }
  }
  for (size_t i = 0; i < fetches.size(); ++i)
    for (size_t w = 0; w < 3; ++w)
      for (size_t b = 0; b < 4; ++b) code[(fetches[i]&4095)*12+w*4+b] = uint8_t(words[i][w] >> (24-b*8));
  return code;
}
Bytes ReadCache(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) return {};
  const auto size = std::filesystem::file_size(path);
  Require(size <= 32 * 1024 * 1024, "cache exceeds preparation bound");
  Bytes b(size);
  std::ifstream f(path, std::ios::binary);
  Require(bool(f.read(reinterpret_cast<char*>(b.data()), b.size())), "cannot read preparation cache");
  return b;
}
void Save(const std::filesystem::path& path, const Bytes& bytes) {
  if (ReadCache(path) == bytes) return;
  std::string error;
  Require(fs::WriteAtomic(path, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &error),
          "cannot persist local preparation inputs");
}
// A store is usable only when it parses to its last record. Anything else is a
// stale, truncated or damaged layer, which the caller rebuilds rather than
// refuses to boot on (P7: bad cache state must never block a launch, and must
// never be reused as if it were complete).
bool ParseShaders(const Bytes& data, const Bytes& header,
                  std::map<std::pair<uint64_t,bool>,Bytes>* out) {
  out->clear();
  if (data.size() < header.size() || !std::equal(header.begin(),header.end(),data.begin()))
    return false;
  for (size_t p = header.size(); p < data.size();) {
    if (p+12 > data.size()) return false;
    const auto hash = LE(data,p,8), packed = LE(data,p+8,4);
    const size_t size = size_t(packed & 0x7fffffff) * 4;
    if (!size || size > 65536 || p+12+size > data.size()) return false;
    Bytes code(data.begin()+p+12,data.begin()+p+12+size);
    if (XXH3_64bits(code.data(),code.size()) != hash) return false;
    (*out)[{hash,bool(packed>>31)}] = std::move(code);
    p += 12+size;
  }
  return true;
}
// The SDK records byte_swap(version) in the file, so it lands big-endian.
std::array<uint8_t,4> VersionBytes(uint32_t version) {
  return {uint8_t(version>>24),uint8_t(version>>16),uint8_t(version>>8),uint8_t(version)};
}
Bytes RecipeHeader(bool dx, const std::string& kind) {
  Bytes header{'X','E','P','S'};
  Put(header,dx?(kind=="rov"?0x4f525844:0x54525844):(kind=="fsi"?1:0),4);
  const auto version = VersionBytes(dx?kD3D12RecipeStorageVersion:kVulkanRecipeStorageVersion);
  header.insert(header.end(),version.begin(),version.end());
  return header;
}
bool ParseRecipes(const Bytes& data, const Bytes& header, size_t stride,
                  std::map<uint64_t,Bytes>* out) {
  out->clear();
  if (data.size() < 12 || !std::equal(header.begin(),header.end(),data.begin()) ||
      (data.size()-12) % stride != 0) return false;
  for (size_t p = 12; p < data.size(); p += stride) {
    const auto hash = LE(data,p,8);
    Bytes raw(data.begin()+p+8,data.begin()+p+stride);
    if (XXH3_64bits(raw.data(),raw.size()) != hash) return false;
    (*out)[hash] = std::move(raw);
  }
  return true;
}
Bytes RecipeStore(const Bytes& header, const std::map<uint64_t,Bytes>& records) {
  Bytes bytes = header;
  for (const auto& [hash,raw] : records) { Put(bytes,hash,8); bytes.insert(bytes.end(),raw.begin(),raw.end()); }
  return bytes;
}
// Writes `bytes` to `file` only if they differ, then reads the file back and
// requires it to parse with the expected record count. A layer is never
// announced as reusable without this second reading.
void PublishShaders(const std::filesystem::path& file, const Bytes& bytes, const Bytes& header,
                    size_t expected) {
  Save(file,bytes);
  std::map<std::pair<uint64_t,bool>,Bytes> parsed;
  Require(ParseShaders(ReadCache(file),header,&parsed) && parsed.size() == expected,
          "a rebuilt shader layer did not verify");
}
void PublishRecipes(const std::filesystem::path& file, const Bytes& bytes, const Bytes& header,
                    size_t stride, size_t expected) {
  Save(file,bytes);
  std::map<uint64_t,Bytes> parsed;
  Require(ParseRecipes(ReadCache(file),header,stride,&parsed) && parsed.size() == expected,
          "a rebuilt recipe layer did not verify");
}
}

SeedReport SeedPreparation(const std::string& text, const std::filesystem::path& cache,
                           const AssetReader& read, std::span<const uint8_t> image,
                           const std::function<bool()>& cancelled,
                           const PreparationInputs& inputs, uint64_t content_epoch,
                           std::string* persistence_error) {
  const auto manifest = Json::parse(text);
  Require(manifest.at("schema") == 1 && manifest.at("title_id") == "5841122D", "unsupported preparation manifest");
  const auto root = cache / "shaders" / "shareable";
  std::filesystem::create_directories(root);
  const Bytes shader_header = {'X','E','S','H',0x20,0x20,0x12,0x19};
  PreparationLedger ledger;
  std::string ledger_reason;
  ledger.Load(cache,&ledger_reason);
  ledger.content_epoch = std::max(ledger.content_epoch,content_epoch);
  SeedReport report;

  // --- Layer 1: local shader extraction, from installed content.
  const auto xsh_file = root / "5841122D.xsh";
  std::map<std::pair<uint64_t,bool>,Bytes> shaders;
  const bool extraction_recorded = ledger.extraction.complete &&
      ledger.extraction.ExtractionValid(inputs);
  const Bytes previous_shaders = ReadCache(xsh_file);
  if (!previous_shaders.empty()) {
    const bool parsed = ParseShaders(previous_shaders,shader_header,&shaders);
    const bool counted = parsed && shaders.size() == ledger.extraction.entries;
    if (extraction_recorded && counted) report.extraction_reused = true;
    else { if (!parsed) shaders.clear(); ++report.repaired_layers; }
  } else if (extraction_recorded) {
    // The ledger said the layer was complete but its file is gone.
    ++report.repaired_layers;
  }
  if (!report.extraction_reused) {
    std::map<std::string,Bytes> assets;
    for (const auto& source : manifest.at("sources")) {
      if (cancelled()) throw std::runtime_error("preparation cancelled");
      const uint64_t hash = Hex(source.at("hash"));
      const bool pixel = source.at("stage") == "pixel";
      if (shaders.contains({hash,pixel})) continue;
      const std::string asset = source.at("asset");
      std::span<const uint8_t> data = image;
      if (asset != "default.xex") {
        if (!assets.contains(asset)) assets[asset] = read(asset);
        data = assets.at(asset);
        Require(data.size() == manifest.at("assets").at(asset).at("bytes"), "installed shader asset changed");
      }
      const size_t p = source.at("offset"), size = source.at("bytes");
      Require(size && size <= 65536 && p <= data.size() && size <= data.size()-p, "invalid shader locator");
      Bytes code(data.begin()+p,data.begin()+p+size);
      if (source.contains("vertex_layout")) code = Link(std::move(code),source,manifest,image);
      Require(XXH3_64bits(code.data(),code.size()) == hash, "installed shader identity mismatch");
      shaders[{hash,pixel}] = std::move(code);
    }
  }

  // --- Layer 2: portable recipes, one store per declared rendering path.
  struct RecipePlan {
    size_t index = 0;
    bool dx = false;
    size_t stride = 0;
    Bytes header;
    std::filesystem::path file;
    std::map<uint64_t,Bytes> records;
    bool reuse = false;
  };
  std::vector<RecipePlan> plans;
  for (const auto& [path, recipes] : manifest.at("paths").items()) {
    const size_t index = PreparationPathIndex(path);
    Require(index < kPreparationPathCount, "undeclared rendering path in the preparation manifest");
    RecipePlan plan;
    plan.index = index;
    plan.dx = path.starts_with("d3d12/");
    plan.stride = plan.dx ? 72 : 68;
    const std::string kind = path.substr(path.find('/')+1);
    plan.header = RecipeHeader(plan.dx,kind);
    plan.file = root / ("5841122D."+kind+(plan.dx?".d3d12.xpso":".vk.xpso"));
    const bool recorded = ledger.recipes[index].complete &&
        ledger.recipes[index].RecipesValid(inputs,plan.dx);
    const Bytes previous = ReadCache(plan.file);
    if (!previous.empty()) {
      const bool parsed = ParseRecipes(previous,plan.header,plan.stride,&plan.records);
      const bool counted = parsed && plan.records.size() == ledger.recipes[index].entries;
      if (recorded && counted) plan.reuse = true;
      else { if (!parsed) plan.records.clear(); ++report.repaired_layers; }
    } else if (recorded) ++report.repaired_layers;
    if (!plan.reuse) for (const auto& recipe : recipes) {
      auto raw = Unhex(recipe.at("raw"));
      const auto hash = Hex(recipe.at("hash"));
      Require(raw.size()==plan.stride-8 && XXH3_64bits(raw.data(),raw.size())==hash,"invalid portable recipe");
      Require(!plan.records.contains(hash) || plan.records.at(hash)==raw,"recipe hash collision");
      plan.records[hash] = std::move(raw);
    }
    plans.push_back(std::move(plan));
  }

  // Every layer that is about to be rewritten is marked incomplete *before* its
  // file is touched, so a crash or a cancelled write can never leave a valid
  // claim behind for a half-written store.
  const bool rewriting = !report.extraction_reused ||
      std::any_of(plans.begin(),plans.end(),[](const RecipePlan& p) { return !p.reuse; });
  if (rewriting) {
    ledger.extraction.complete = report.extraction_reused;
    for (const auto& plan : plans) ledger.recipes[plan.index].complete = plan.reuse;
    std::string ignored;
    ledger.Save(cache,&ignored);
  }

  if (!report.extraction_reused) {
    Bytes xsh = shader_header;
    for (const auto& [key, code] : shaders) {
      Put(xsh,key.first,8); Put(xsh,(key.second?0x80000000u:0u)|uint32_t(code.size()/4),4);
      xsh.insert(xsh.end(),code.begin(),code.end());
    }
    if (cancelled()) throw std::runtime_error("preparation cancelled");
    PublishShaders(xsh_file,xsh,shader_header,shaders.size());
    ledger.extraction.complete = true;
    ledger.extraction.catalogue = inputs.catalogue;
    ledger.extraction.content = inputs.content;
    ledger.extraction.executable = inputs.executable;
    ledger.extraction.entries = shaders.size();
  }
  report.shaders = shaders.size();

  for (auto& plan : plans) {
    if (plan.reuse) {
      report.recipes_reused[plan.index] = true;
    } else {
      if (cancelled()) throw std::runtime_error("preparation cancelled");
      PublishRecipes(plan.file,RecipeStore(plan.header,plan.records),plan.header,plan.stride,
                     plan.records.size());
      ledger.recipes[plan.index].complete = true;
      ledger.recipes[plan.index].catalogue = inputs.catalogue;
      ledger.recipes[plan.index].content = inputs.content;
      ledger.recipes[plan.index].executable = inputs.executable;
      ledger.recipes[plan.index].d3d12_version = inputs.d3d12_version;
      ledger.recipes[plan.index].vulkan_version = inputs.vulkan_version;
      ledger.recipes[plan.index].entries = plan.records.size();
    }
    report.records[plan.index] = ledger.recipes[plan.index].entries;
  }

  // The ledger is the reusable result and is announced only after every layer
  // above verified. A failed write is a persistence fact, not a preparation
  // failure: the live pipelines exist either way.
  std::string persistence;
  report.persisted = ledger.Save(cache,&persistence);
  if (!report.persisted && persistence_error) *persistence_error = persistence;
  return report;
}
}
