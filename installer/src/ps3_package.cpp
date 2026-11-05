// SPDX-License-Identifier: GPL-2.0-only
#include "ps3_package.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "inflate.h"
#include "ps3_scheme.h"
#include "util.h"

namespace rb_blitz::installer {
namespace {
using Data = std::vector<std::uint8_t>;

bool Bad(std::string* error, std::string text) {
  SetError(error, std::move(text));
  return false;
}
std::uint32_t Be32(const std::uint8_t* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
         (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t Be64(const std::uint8_t* p) {
  return (std::uint64_t(Be32(p)) << 32) | Be32(p + 4);
}
std::uint32_t Le32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

struct Package {
  FileSource file;
  std::uint64_t data_offset = 0, data_size = 0;
  std::uint32_t item_count = 0;
  std::array<std::uint8_t, 16> digest{};
  std::string content_id;
};

bool OpenPackage(const std::filesystem::path& path, Package* pkg, std::string* error) {
  if (!pkg->file.Open(path, error)) return false;
  std::array<std::uint8_t, 0x80> header{};
  if (pkg->file.Read(0, header.data(), header.size()) != header.size() ||
      Be32(header.data()) != 0x7f504b47)
    return Bad(error, "not a PS3 PKG header");
  const auto header_size = Be32(header.data() + 0x10);
  if (header_size != 0x80 && header_size != 0xc0)
    return Bad(error, "unsupported PS3 PKG header variant");
  pkg->item_count = Be32(header.data() + 0x14);
  pkg->data_offset = Be64(header.data() + 0x20);
  pkg->data_size = Be64(header.data() + 0x28);
  if (!pkg->item_count || pkg->item_count > 65536 ||
      pkg->data_offset >= pkg->file.size() ||
      pkg->data_size > pkg->file.size() - pkg->data_offset)
    return Bad(error, "PS3 PKG index or data bounds are invalid");
  const auto* cid = header.data() + 0x30;
  const auto end = std::find(cid, cid + 0x30, std::uint8_t(0));
  pkg->content_id.assign(cid, end);
  // The measured Blitz package is NPUB30749. A DLC PKG must not be accepted
  // just because it shares the title ID or contains a similarly named file.
  if (!pkg->content_id.starts_with("UP8802-NPUB30749_00-RBBLITZ"))
    return Bad(error, "not a Rock Band Blitz PS3 package (content ID mismatch)");
  // Finalized retail packages encrypt the data section with a separate AES key.
  // The QA-digest stream below applies only to non-finalized packages. Never
  // mistake a retail package for a corrupt file table or a missing licence.
  if ((header[4] & 0x80) != 0)
    return Bad(error, "finalized retail PS3 PKG requires a package decryption key; use an extracted PS3 game folder");
  std::copy_n(header.data() + 0x60, 16, pkg->digest.data());
  return true;
}

struct Sha1 {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  ~Sha1() { if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
  bool Open(std::string* error) {
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0)
      return Bad(error, "Windows SHA-1 provider unavailable");
    return true;
  }
  bool Hash(const std::array<std::uint8_t, 64>& input,
            std::array<std::uint8_t, 20>* digest, std::string* error) const {
    if (BCryptHash(algorithm, nullptr, 0,
                   const_cast<PUCHAR>(input.data()), ULONG(input.size()),
                   digest->data(), ULONG(digest->size())) < 0)
      return Bad(error, "Windows SHA-1 failed");
    return true;
  }
};

bool CryptSlice(Package* pkg, Sha1& sha, std::uint64_t start, std::uint64_t length,
                Data* output, std::string* error) {
  if (length > 32 * 1024 * 1024 || start > pkg->data_size ||
      length > pkg->data_size - start)
    return Bad(error, "PS3 PKG item lies outside the data section or is too large");
  output->resize(std::size_t(length));
  if (pkg->file.Read(pkg->data_offset + start, output->data(), output->size()) !=
      output->size()) return Bad(error, "short PS3 PKG item read");
  std::array<std::uint8_t, 64> context{};
  std::copy_n(pkg->digest.data(), 8, context.data());
  std::copy_n(pkg->digest.data(), 8, context.data() + 8);
  std::copy_n(pkg->digest.data() + 8, 8, context.data() + 16);
  std::copy_n(pkg->digest.data() + 8, 8, context.data() + 24);
  for (std::uint64_t block = start / 16; block <= (start + length - (length != 0)) / 16 && length;
       ++block) {
    for (int i = 0; i < 8; ++i) context[56 + i] = std::uint8_t(block >> (56 - i * 8));
    std::array<std::uint8_t, 20> digest;
    if (!sha.Hash(context, &digest, error)) return false;
    const auto first = std::max(start, block * 16);
    const auto last = std::min(start + length, block * 16 + 16);
    for (auto pos = first; pos < last; ++pos)
      (*output)[std::size_t(pos - start)] ^= digest[std::size_t(pos - block * 16)];
  }
  return true;
}

struct Item { std::string name; std::uint64_t offset = 0, size = 0; };
bool Items(Package* pkg, Sha1& sha, std::vector<Item>* items, std::string* error) {
  Data table;
  if (!CryptSlice(pkg, sha, 0, std::uint64_t(pkg->item_count) * 0x20, &table, error))
    return false;
  items->clear();
  for (std::uint32_t i = 0; i < pkg->item_count; ++i) {
    const auto* p = table.data() + i * 0x20;
    const auto name_offset = Be32(p), name_size = Be32(p + 4);
    const auto offset = Be64(p + 8), size = Be64(p + 16);
    if (!name_size || name_size > 1024 || name_offset > pkg->data_size ||
        name_size > pkg->data_size - name_offset || offset > pkg->data_size ||
        size > pkg->data_size - offset)
      return Bad(error, "PS3 PKG file table is invalid or uses unsupported encryption");
    Data name;
    if (!CryptSlice(pkg, sha, name_offset, name_size, &name, error)) return false;
    const auto end = std::find(name.begin(), name.end(), std::uint8_t(0));
    items->push_back({std::string(name.begin(), end), offset, size});
  }
  return true;
}

struct ArkReader {
  const Data& bytes;
  std::size_t pos = 0;
  bool Take(std::size_t count, const std::uint8_t** out) {
    if (count > bytes.size() - pos) return false;
    *out = bytes.data() + pos; pos += count; return true;
  }
  bool U32(std::uint32_t* value) {
    const std::uint8_t* p;
    if (!Take(4, &p)) return false;
    *value = Le32(p); return true;
  }
  bool U64(std::uint64_t* value) {
    const std::uint8_t* p;
    if (!Take(8, &p)) return false;
    *value = std::uint64_t(Le32(p)) | (std::uint64_t(Le32(p + 4)) << 32);
    return true;
  }
};

std::int32_t Round(std::int32_t key) {
  const auto q = key / 127773;
  const auto raw = std::int64_t(key - q * 127773) * 16807 - std::int64_t(q) * 2836;
  const auto value = std::int32_t(std::uint32_t(raw));
  return value <= 0 ? value + 2147483647 : value;
}
bool DecryptArk(Data* bytes, std::string* error) {
  if (bytes->size() < 4) return Bad(error, "short PS3 ARK index");
  if (Le32(bytes->data()) <= 10) return true;
  const auto first = Le32(bytes->data());
  auto key = Round(std::int32_t(first));
  Data body(bytes->begin() + 4, bytes->end()), stream(body.size());
  for (std::size_t i = 0; i < body.size(); ++i) {
    stream[i] = std::uint8_t(key);
    body[i] ^= stream[i];
    key = Round(key);
  }
  if (std::int32_t(Le32(body.data())) < 0)
    for (std::size_t i = 0; i < body.size(); ++i) body[i] = ((*bytes)[i + 4] ^ stream[i] ^ 0xff);
  *bytes = std::move(body);
  if (Le32(bytes->data()) != 6) return Bad(error, "unsupported PS3 ARK index version");
  return true;
}

bool FindButtonsInArk(Data header, std::uint64_t* offset, std::uint32_t* size,
                      std::uint32_t* inflated, std::uint32_t* part,
                      std::string* error) {
  if (!DecryptArk(&header, error)) return false;
  ArkReader r{header};
  std::uint32_t version, count, repeated;
  const std::uint8_t* p;
  if (!r.U32(&version) || version != 6 || !r.Take(20, &p) ||
      !r.U32(&count) || count == 0 || count > 16 ||
      !r.U32(&repeated) || repeated != count)
    return Bad(error, "invalid PS3 ARK header");
  std::vector<std::uint32_t> part_sizes(count);
  for (auto& n : part_sizes) if (!r.U32(&n)) return Bad(error, "short PS3 ARK size table");
  if (!r.U32(&repeated) || repeated != count) return Bad(error, "bad PS3 ARK path count");
  for (std::uint32_t i = 0; i < count; ++i) {
    std::uint32_t n;
    if (!r.U32(&n) || n > 1024 || !r.Take(n, &p))
      return Bad(error, "bad PS3 ARK path");
  }
  if (!r.U32(&repeated) || repeated != count || !r.Take(4 * count, &p))
    return Bad(error, "bad PS3 ARK checksum table");
  std::uint32_t strings_size, offsets_count;
  if (!r.U32(&strings_size) || strings_size > 8 * 1024 * 1024 ||
      !r.Take(strings_size, &p)) return Bad(error, "bad PS3 ARK string table");
  const std::string_view strings(reinterpret_cast<const char*>(p), strings_size);
  if (!r.U32(&offsets_count) || offsets_count > 100000)
    return Bad(error, "bad PS3 ARK string offsets");
  std::vector<std::uint32_t> offsets(offsets_count);
  for (auto& n : offsets) if (!r.U32(&n)) return Bad(error, "short PS3 ARK string offsets");
  const auto string_at = [&](std::uint32_t index) -> std::string_view {
    if (index >= offsets.size() || offsets[index] >= strings.size()) return {};
    const auto start = offsets[index];
    const auto end = strings.find('\0', start);
    return end == std::string_view::npos ? std::string_view{} : strings.substr(start, end - start);
  };
  std::uint32_t files;
  if (!r.U32(&files) || files > 100000) return Bad(error, "bad PS3 ARK file count");
  for (std::uint32_t i = 0; i < files; ++i) {
    std::uint64_t at;
    std::uint32_t name, folder, length, expand;
    if (!r.U64(&at) || !r.U32(&name) || !r.U32(&folder) ||
        !r.U32(&length) || !r.U32(&expand))
      return Bad(error, "short PS3 ARK file table");
    if (string_at(name) != "buttons.milo_ps3") continue;
    const auto dir = string_at(folder);
    if (!dir.ends_with("ui/resource/fonts/gen") || !length || length > 16 * 1024 * 1024)
      continue;
    for (std::uint32_t part_index = 0; part_index < count; ++part_index) {
      if (at < part_sizes[part_index]) {
        if (length > part_sizes[part_index] - at)
          return Bad(error, "Buttons scene crosses PS3 ARK part boundary");
        *offset = at; *size = length; *inflated = expand; *part = part_index;
        return true;
      }
      at -= part_sizes[part_index];
    }
    return Bad(error, "Buttons scene offset exceeds PS3 ARK parts");
  }
  return Bad(error, "PS3 ARK index has no Buttons scene");
}
}  // namespace

bool ReadPs3PackageScene(const std::filesystem::path& package, Data* scene,
                         std::string* content_id, std::string* error) {
  Package pkg;
  if (!OpenPackage(package, &pkg, error)) return false;
  Sha1 sha;
  if (!sha.Open(error)) return false;
  std::vector<Item> items;
  if (!Items(&pkg, sha, &items, error)) return false;
  const auto find = [&](std::string_view suffix) -> const Item* {
    for (const auto& item : items)
      if (Lower(item.name).ends_with(suffix)) return &item;
    return nullptr;
  };
  const auto* hdr = find("gen/main_ps3.hdr");
  if (!hdr) return Bad(error, "PS3 package has no gen/main_ps3.hdr");
  Data ark_header;
  if (!CryptSlice(&pkg, sha, hdr->offset, hdr->size, &ark_header, error)) return false;
  std::uint64_t offset;
  std::uint32_t size, inflated, part;
  if (!FindButtonsInArk(std::move(ark_header), &offset, &size, &inflated, &part, error))
    return false;
  const auto* ark = find(S("gen/main_ps3_", part, ".ark"));
  if (!ark || offset > ark->size || size > ark->size - offset)
    return Bad(error, "PS3 package lacks the ARK part containing Buttons");
  if (inflated && inflated != size)
    return Bad(error, "compressed Buttons ARK entry is unsupported");
  if (!CryptSlice(&pkg, sha, ark->offset + offset, size, scene, error)) return false;
  if (!ProbePs3SceneBytes(*scene, error)) return false;
  *content_id = pkg.content_id;
  return true;
}

}  // namespace rb_blitz::installer
