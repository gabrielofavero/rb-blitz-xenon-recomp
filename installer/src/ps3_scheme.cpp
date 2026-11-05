// SPDX-License-Identifier: GPL-2.0-only
#include "ps3_scheme.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "assets/texture_codec.h"
#include "controller_schemes/schemes.h"
#include "inflate.h"
#include "util.h"

namespace rb_blitz::installer {
namespace {
namespace fs = std::filesystem;
using Data = std::vector<std::uint8_t>;

std::uint16_t Be16(const std::uint8_t* p) { return (std::uint16_t(p[0]) << 8) | p[1]; }
std::uint32_t Le32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
         (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
void Put32(Data* out, std::uint32_t n) {
  for (int i = 0; i < 4; ++i) out->push_back(std::uint8_t(n >> (24 - i * 8)));
}
bool Bad(std::string* error, std::string_view message) {
  SetError(error, std::string(message));
  return false;
}

bool ReadMiloBytes(const Data& file, Data* stream, std::string* error) {
  if (file.size() < 0x810 || (Le32(file.data()) != 0xCDBEDEAF &&
       Le32(file.data()) != 0xCABEDEAF) || Le32(file.data() + 4) != 0x810)
    return Bad(error, "invalid Buttons/controller Milo chunk header");
  const auto count = Le32(file.data() + 8);
  if (!count || count > 512) return Bad(error, "invalid Milo chunk count");
  stream->clear();
  std::size_t pos = 0x810;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto word = Le32(file.data() + 0x10 + i * 4);
    const auto stored_size = word & 0x00ffffff;
    if (!stored_size || stored_size > file.size() - pos)
      return Bad(error, "truncated Milo chunk");
    if ((word & 0x01000000) || Le32(file.data()) == 0xCABEDEAF) {
      stream->insert(stream->end(), file.begin() + pos, file.begin() + pos + stored_size);
    } else {
      if (stored_size < 5) return Bad(error, "short compressed Milo chunk");
      const auto expanded = Le32(file.data() + pos);
      if (expanded > 32 * 1024 * 1024) return Bad(error, "oversized Milo chunk");
      MemorySource source(file.data() + pos + 4, stored_size - 4);
      Data decoded;
      std::uint32_t crc = 0;
      if (!InflateRawToVector(source, 0, stored_size - 4, &decoded, &crc, error) ||
          decoded.size() != expanded) return Bad(error, "Milo DEFLATE failed");
      stream->insert(stream->end(), decoded.begin(), decoded.end());
    }
    if (stream->size() > 128 * 1024 * 1024)
      return Bad(error, "oversized Milo stream");
    pos += stored_size;
  }
  return true;
}

bool ReadMilo(const fs::path& path, Data* stream, std::string* error) {
  Data file;
  return ReadFileBytes(path, &file, error) && ReadMiloBytes(file, stream, error);
}

struct GlyphTable { std::array<std::uint8_t, 34 * 18> bytes{}; };
bool Glyphs(const Data& stream, std::array<GlyphTable, 2>* tables, std::string* error) {
  constexpr std::uint8_t marker[] = {0, 0, 0, 0x22, 0, 0x20};
  const std::uint8_t base[] = {0x42, 0xaa, 0, 0};  // 85.0f
  std::size_t found = 0;
  for (std::size_t at = 0; at + 4 + 34 * 18 <= stream.size(); ++at) {
    if (std::memcmp(stream.data() + at, marker, sizeof(marker))) continue;
    const auto begin = at > 256 ? at - 256 : 0;
    const auto prior = std::search(stream.begin() + begin, stream.begin() + at,
                                   std::begin(base), std::end(base));
    if (prior == stream.begin() + at) continue;
    const auto* first = stream.data() + at + 4;
    if (Be16(first) != 0x20 || Be16(first + 18) != 0x26 ||
        Be16(first + 33 * 18) != 0xa0) continue;
    if (found >= 2) return Bad(error, "too many Buttons Glyph tables");
    std::memcpy((*tables)[found++].bytes.data(), first, 34 * 18);
  }
  if (found != 2) return Bad(error, "expected two Buttons Glyph tables");
  return true;
}

bool Texture(const Data& stream, std::string_view asset, std::uint32_t width,
             std::uint32_t height, bool xbox, Data* rgba, std::string* error) {
  const auto it = std::search(stream.begin(), stream.end(), asset.begin(), asset.end());
  if (it == stream.end()) return Bad(error, "scene texture path missing");
  const auto next = std::search(it + asset.size(), stream.end(), asset.begin(), asset.end());
  if (next != stream.end()) return Bad(error, "scene texture path is ambiguous");
  const auto offset = std::size_t(it - stream.begin()) + asset.size();
  std::size_t record_at = stream.size();
  for (std::size_t at = offset; at + 31 <= stream.size() && at < offset + 80; ++at) {
    const auto* p = stream.data() + at;
    if (p[4] == 8 && p[5] == 0 && p[6] == 0 && p[7] == 0 && p[8] == 0x18 &&
        Be16(p + 10) == width && Be16(p + 12) == height && Be16(p + 14) == width &&
        std::all_of(p + 16, p + 31, [](std::uint8_t b) { return b == 0; })) {
      record_at = at; break;
    }
  }
  const auto size = std::size_t(width) * height;
  if (record_at == stream.size() || size > stream.size() - record_at - 31)
    return Bad(error, "measured BC3 texture record missing or truncated");
  Data blocks(stream.begin() + record_at + 31, stream.begin() + record_at + 31 + size);
  if (xbox) assets::SwapSixteenBitWords(blocks.data(), blocks.size());
  rgba->resize(size * 4);
  assets::DecodeDxt5(blocks.data(), width, height, rgba->data());
  for (std::size_t i = 0; i < rgba->size(); i += 4)
    if ((*rgba)[i + 3] == 0) std::fill_n(rgba->begin() + i, 3, 0);
  return true;
}

void Chunk(Data* png, std::string_view name, const Data& payload) {
  Put32(png, std::uint32_t(payload.size()));
  const auto start = png->size();
  png->insert(png->end(), name.begin(), name.end());
  png->insert(png->end(), payload.begin(), payload.end());
  Put32(png, Crc32(png->data() + start, png->size() - start));
}
bool Png(const fs::path& target, const Data& rgba, unsigned width, unsigned height,
         std::string* error) {
  if (rgba.size() != std::size_t(width) * height * 4 || !width || !height)
    return Bad(error, "invalid PNG pixels");
  Data png{137, 80, 78, 71, 13, 10, 26, 10};
  Data ihdr;
  Put32(&ihdr, width); Put32(&ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  Chunk(&png, "IHDR", ihdr);
  Data raw;
  raw.reserve(std::size_t(height) * (1 + width * 4));
  for (unsigned y = 0; y < height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba.begin() + std::size_t(y) * width * 4,
               rgba.begin() + std::size_t(y + 1) * width * 4);
  }
  Data zlib{0x78, 0x01};
  for (std::size_t pos = 0; pos < raw.size();) {
    const auto size = std::min<std::size_t>(65535, raw.size() - pos);
    zlib.push_back(pos + size == raw.size() ? 1 : 0);
    zlib.push_back(std::uint8_t(size)); zlib.push_back(std::uint8_t(size >> 8));
    zlib.push_back(std::uint8_t(~size)); zlib.push_back(std::uint8_t(~size >> 8));
    zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + size);
    pos += size;
  }
  std::uint32_t a = 1, b = 0;
  for (auto byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
  Put32(&zlib, (b << 16) | a);
  Chunk(&png, "IDAT", zlib);
  Chunk(&png, "IEND", {});
  return WriteFileBytes(target, png.data(), png.size(), false, error);
}

struct Slot { const char* id; int x, y, w, h; bool selected; };
constexpr Slot kSlots[] = {
  {"a_button",0,2,85,83,true}, {"b_button",85,0,85,85,true},
  {"x_button",170,0,85,85,true}, {"y_button",255,2,84,82,true},
  {"glyph_0067",0,0,0,0,false}, {"glyph_0063",0,0,0,0,false},
  {"left_trigger",2,96,83,65,true}, {"left_bumper",87,96,83,65,true},
  {"right_trigger",170,95,85,65,true}, {"right_bumper",255,96,85,65,true},
  {"start_button",343,102,81,53,false}, {"back_button",428,110,81,37,false},
  {"dpad",2,172,83,83,true}, {"instrument_drums",87,173,83,81,false},
  {"instrument_bass",170,173,85,81,false}, {"instrument_guitar",255,173,85,81,false},
  {"instrument_vocals",340,173,85,81,false}, {"instrument_keys",427,172,83,82,false},
  {"dpad_up",2,255,83,85,true}, {"dpad_right",87,256,83,84,true},
  {"dpad_down",172,257,83,83,true}, {"dpad_left",256,257,84,83,true},
  {"left_stick",340,257,85,83,true}, {"right_stick",427,256,83,84,true},
  {"left_stick_up",2,342,83,83,false}, {"left_stick_right",87,340,83,85,false},
  {"left_stick_down",172,340,83,85,false}, {"left_stick_left",255,342,85,83,false},
  {"right_stick_up",342,342,83,83,false}, {"right_stick_right",427,340,83,85,false},
  {"right_stick_left",0,427,85,83,false}, {"right_stick_down",87,425,83,85,false},
};

bool WriteButtons(const Data& sheet, const fs::path& target, std::string* error) {
  if (sheet.size() != 512 * 512 * 4) return Bad(error, "wrong PS3 button sheet size");
  for (std::size_t i = 0; i < std::size(kSlots); ++i) {
    const auto& slot = kSlots[i];
    const int cx = int(i % 6) * 85, cy = int(i / 6) * 85;
    int minx = cx + 85, miny = cy + 85, maxx = 0, maxy = 0;
    for (int y = cy; y < std::min(cy + 85, 512); ++y)
      for (int x = cx; x < std::min(cx + 85, 512); ++x)
        if (sheet[(std::size_t(y) * 512 + x) * 4 + 3]) {
          minx = std::min(minx, x); miny = std::min(miny, y);
          maxx = std::max(maxx, x + 1); maxy = std::max(maxy, y + 1);
        }
    if (!maxx) { minx = 0; miny = 0; }
    const int w = maxx ? maxx - minx : 0, h = maxy ? maxy - miny : 0;
    if (minx != slot.x || miny != slot.y || w != slot.w || h != slot.h)
      return Bad(error, S("PS3 source art bounds changed for ", slot.id, ": got ",
                          minx, ",", miny, ",", w, ",", h, "; expected ",
                          slot.x, ",", slot.y, ",", slot.w, ",", slot.h));
    if (!slot.selected) continue;
    Data crop(std::size_t(w) * h * 4);
    for (int row = 0; row < h; ++row)
      std::copy_n(sheet.begin() + (std::size_t(miny + row) * 512 + minx) * 4,
                  std::size_t(w) * 4, crop.begin() + std::size_t(row) * w * 4);
    if (!Png(target / "buttons" / (std::string(slot.id) + ".png"), crop, w, h, error))
      return false;
  }
  return true;
}
}  // namespace

bool ProbePs3Folder(const fs::path& folder, fs::path* scene, std::string* error) {
  for (const auto& root : {folder, folder / "USRDIR", folder / "PS3_GAME/USRDIR"}) {
    const auto candidate = root / "ui/resource/fonts/gen/buttons.milo_ps3";
    if (fs::is_regular_file(candidate)) {
      Data stream;
      if (!ReadMilo(candidate, &stream, error)) return false;
      std::array<GlyphTable, 2> tables;
      if (!Glyphs(stream, &tables, error)) return false;
      *scene = candidate;
      return true;
    }
  }
  return Bad(error, "no ui/resource/fonts/gen/buttons.milo_ps3 in extracted PS3 folder");
}

bool InstallPs3FolderScheme(const fs::path& folder, const fs::path& install_root,
                            std::string* error) {
  fs::path ps3_scene;
  if (!ProbePs3Folder(folder, &ps3_scene, error)) return false;
  Data file;
  if (!ReadFileBytes(ps3_scene, &file, error)) return false;
  return InstallPs3SceneScheme(file, install_root, error);
}

bool ProbePs3SceneBytes(const Data& bytes, std::string* error) {
  Data stream;
  std::array<GlyphTable, 2> tables;
  return ReadMiloBytes(bytes, &stream, error) && Glyphs(stream, &tables, error);
}

bool InstallPs3SceneScheme(const Data& bytes, const fs::path& install_root,
                           std::string* error) {
  const auto xbox_game = install_root / "game";
  const auto xbox_buttons = xbox_game / "ui/resource/fonts/gen/buttons.milo_xbox";
  const auto xbox_pads = xbox_game / "ui/controller_config/gen/controller_config.milo_xbox";
  Data ps3, xbox, pads;
  if (!ReadMiloBytes(bytes, &ps3, error) || !ReadMilo(xbox_buttons, &xbox, error) ||
      !ReadMilo(xbox_pads, &pads, error)) return false;
  std::array<GlyphTable, 2> ps3_tables, xbox_tables;
  if (!Glyphs(ps3, &ps3_tables, error) || !Glyphs(xbox, &xbox_tables, error)) return false;
  for (int i = 0; i < 2; ++i)
    if (ps3_tables[i].bytes != xbox_tables[i].bytes)
      return Bad(error, "PS3 and Xbox 360 Buttons Glyph tables differ");
  Data sheet;
  if (!Texture(ps3, "../image/icons_buttons_xbox_nomip.bmp", 512, 512,
               false, &sheet, error)) return false;
  const auto schemes = install_root / "assets/controller/schemes";
  const auto temp_root = schemes / ".ps3-import-staging";
  const auto staged = temp_root / "ps3";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  if (ec || !EnsureDirectory(staged / "buttons", error) ||
      !EnsureDirectory(staged / "pads", error)) return false;
  auto cleanup = [&] { std::error_code ignored; fs::remove_all(temp_root, ignored); };
  if (!WriteButtons(sheet, staged, error)) { cleanup(); return false; }
  std::string manifest = "schema_version = 1\nid = \"ps3\"\nname = \"PlayStation 3\"\n"
                         "kind = \"controller\"\n\n[buttons]\n";
  for (const auto& slot : kSlots)
    if (slot.selected) manifest += S(slot.id, " = \"buttons/", slot.id, ".png\"\n");
  manifest += "\n[controller.slots]\n";
  for (int index = 0; index < 4; ++index) {
    const auto id = S("ps_", index);
    Data rgba;
    if (!Texture(pads, S("img/", id, ".png"), 1024, 1024, true, &rgba, error) ||
        !Png(staged / "pads" / (id + ".png"), rgba, 1024, 1024, error)) {
      cleanup(); return false;
    }
    manifest += S(id, " = \"pads/", id, ".png\"\n");
  }
  if (!WriteFileText(staged / "scheme.toml", manifest, false, error)) {
    cleanup(); return false;
  }
  controller_schemes::Scheme validated;
  if (!controller_schemes::LoadScheme(staged, true, controller_schemes::DecodePngWic,
                                       &validated, error)) { cleanup(); return false; }
  const auto final = schemes / "ps3", backup = schemes / ".ps3-import-backup";
  fs::remove_all(backup, ec);
  if (ec) { cleanup(); return Bad(error, "cannot clear PS3 import backup"); }
  if (fs::exists(final)) {
    fs::rename(final, backup, ec);
    if (ec) { cleanup(); return Bad(error, "cannot replace existing PS3 scheme"); }
  }
  fs::rename(staged, final, ec);
  if (ec) {
    if (fs::exists(backup)) fs::rename(backup, final, ec);
    cleanup();
    return Bad(error, "cannot publish PS3 scheme");
  }
  cleanup();
  fs::remove_all(backup, ec);
  return true;
}

}  // namespace rb_blitz::installer
