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
// Same declaration linker as scripts/shader_vertex_link.py / sub_82521908.
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
}

void SeedPreparation(const std::string& text, const std::filesystem::path& cache,
                     const AssetReader& read, std::span<const uint8_t> image,
                     const std::function<bool()>& cancelled) {
  const auto manifest = Json::parse(text);
  Require(manifest.at("schema") == 1 && manifest.at("title_id") == "5841122D", "unsupported preparation manifest");
  const auto root = cache / "shaders" / "shareable";
  std::filesystem::create_directories(root);
  std::map<std::pair<uint64_t,bool>,Bytes> shaders;
  const Bytes old = ReadCache(root / "5841122D.xsh");
  const Bytes header = {'X','E','S','H',0x20,0x20,0x12,0x19};
  if (!old.empty()) {
    Require(old.size() >= 8 && std::equal(header.begin(),header.end(),old.begin()), "unsupported local shader storage");
    for (size_t p = 8; p < old.size();) {
      const auto hash = LE(old,p,8), packed = LE(old,p+8,4);
      const size_t size = (packed & 0x7fffffff) * 4;
      Require(size && size <= 65536 && size <= old.size()-std::min(old.size(),p+12), "invalid local shader extent");
      Bytes code(old.begin()+p+12,old.begin()+p+12+size);
      Require(XXH3_64bits(code.data(),code.size()) == hash, "local shader hash mismatch");
      shaders[{hash,bool(packed>>31)}] = std::move(code);
      p += 12+size;
    }
  }
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
  Bytes xsh = header;
  for (const auto& [key, code] : shaders) {
    Put(xsh,key.first,8); Put(xsh,(key.second?0x80000000u:0u)|uint32_t(code.size()/4),4);
    xsh.insert(xsh.end(),code.begin(),code.end());
  }
  if (cancelled()) throw std::runtime_error("preparation cancelled");
  Save(root / "5841122D.xsh",xsh);
  for (const auto& [path, recipes] : manifest.at("paths").items()) {
    const bool dx = path.starts_with("d3d12/");
    const std::string kind = path.substr(path.find('/')+1);
    const size_t stride = dx ? 72 : 68;
    Bytes xpso{'X','E','P','S'};
    Put(xpso,dx?(kind=="rov"?0x4f525844:0x54525844):(kind=="fsi"?1:0),4);
    xpso.insert(xpso.end(),{0x20,0x26,0x02,uint8_t(dx?0x26:0x28)});
    const auto file = root / ("5841122D."+kind+(dx?".d3d12.xpso":".vk.xpso"));
    const auto previous = ReadCache(file);
    std::map<uint64_t,Bytes> records;
    if (!previous.empty()) {
      Require(previous.size() >= 12 && std::equal(xpso.begin(),xpso.end(),previous.begin()) && (previous.size()-12)%stride==0,
              "unsupported local recipe storage");
      for (size_t p = 12; p < previous.size(); p += stride) {
        const auto hash = LE(previous,p,8);
        Bytes raw(previous.begin()+p+8,previous.begin()+p+stride);
        Require(XXH3_64bits(raw.data(),raw.size())==hash,"local recipe hash mismatch");
        records[hash] = std::move(raw);
      }
    }
    for (const auto& recipe : recipes) {
      auto raw = Unhex(recipe.at("raw"));
      const auto hash = Hex(recipe.at("hash"));
      Require(raw.size()==stride-8 && XXH3_64bits(raw.data(),raw.size())==hash,"invalid portable recipe");
      Require(!records.contains(hash) || records.at(hash)==raw,"recipe hash collision");
      records[hash] = std::move(raw);
    }
    for (const auto& [hash,raw] : records) { Put(xpso,hash,8); xpso.insert(xpso.end(),raw.begin(),raw.end()); }
    if (cancelled()) throw std::runtime_error("preparation cancelled");
    Save(file,xpso);
  }
}
}
