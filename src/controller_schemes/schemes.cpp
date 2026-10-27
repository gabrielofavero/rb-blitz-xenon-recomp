// SPDX-License-Identifier: GPL-2.0-only
#include "controller_schemes/schemes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "assets/texture_codec.h"

namespace rb_blitz::controller_schemes {
namespace {
namespace fs = std::filesystem;
constexpr uintmax_t kMaxPngBytes = 8 * 1024 * 1024;

std::string Trim(std::string s) {
  const auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  const auto b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
bool ParseString(const std::string& raw, std::string* value) {
  if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return false;
  value->clear();
  for (size_t i = 1; i + 1 < raw.size(); ++i) {
    if (raw[i] == '"' || static_cast<unsigned char>(raw[i]) < 0x20) return false;
    if (raw[i] == '\\') {
      if (++i + 1 >= raw.size()) return false;
      switch (raw[i]) {
        case 'n': value->push_back('\n'); break;
        case 't': value->push_back('\t'); break;
        case '"': value->push_back('"'); break;
        case '\\': value->push_back('\\'); break;
        default: return false;
      }
    } else value->push_back(raw[i]);
  }
  return true;
}
bool Reserved(const std::string& id) {
  static const std::set<std::string> ids = {"retail", "keyboard", "ps3", "xbox-series",
      "playstation-series", "nintendo-switch", "steam-controller", "steam-deck"};
  return ids.contains(id);
}
bool ValidName(const std::string& name) {
  size_t count = 0;
  for (size_t i = 0; i < name.size();) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    if (c < 0x20 || c == 0x7f) return false;
    size_t bytes = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
                   c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!bytes || i + bytes > name.size()) return false;
    for (size_t j = 1; j < bytes; ++j)
      if ((static_cast<unsigned char>(name[i + j]) & 0xc0) != 0x80) return false;
    i += bytes;
    if (++count > 20) return false;
  }
  return count != 0;
}
bool ValidImage(const Image& image) {
  return image.width && image.height && image.width <= 2048 && image.height <= 2048 &&
      image.rgba.size() == size_t(image.width) * image.height * 4;
}
bool SafeImagePath(const fs::path& folder, const std::string& name, fs::path* path,
                   std::string* why) {
  const fs::path relative = fs::path(std::u8string(name.begin(), name.end()));
  if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
      relative.extension() != ".png") { *why = "image path must be a relative .png"; return false; }
  for (const auto& part : relative) {
    if (part == "." || part == ".." || part.empty()) {
      *why = "image path escapes package"; return false;
    }
  }
  fs::path current = folder;
  std::error_code ec;
  if (fs::is_symlink(current, ec)) { *why = "package is a symlink"; return false; }
  for (const auto& part : relative) {
    current /= part;
    if (fs::is_symlink(current, ec)) { *why = "image path contains a symlink"; return false; }
  }
  if (!fs::is_regular_file(current, ec) || ec) { *why = "missing image: " + name; return false; }
  if (fs::file_size(current, ec) > kMaxPngBytes || ec) {
    *why = "image exceeds 8 MB: " + name; return false;
  }
  *path = current;
  return true;
}
bool LoadImage(const fs::path& folder, const std::string& name, DecodePng decoder,
               Image* image, std::string* why) {
  fs::path path;
  if (!SafeImagePath(folder, name, &path, why)) return false;
  if (!decoder(path, image, why)) return false;
  if (!ValidImage(*image)) { *why = "invalid RGBA8 dimensions: " + name; return false; }
  return true;
}
const Slot* FindButton(const std::string& id) {
  const auto& slots = ButtonSlots();
  auto it = std::find_if(slots.begin(), slots.end(), [&](const Slot& s) { return s.id == id; });
  return it == slots.end() ? nullptr : &*it;
}
bool IsPad(const std::string& id) {
  const auto& slots = PadSlots();
  return std::find(slots.begin(), slots.end(), id) != slots.end();
}
void BlitFit(const Image& source, std::vector<uint8_t>* canvas, uint32_t canvas_width,
             uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  Image fitted = Fit(source, width, height);
  for (uint32_t row = 0; row < height; ++row) {
    std::copy_n(fitted.rgba.data() + size_t(row) * width * 4, size_t(width) * 4,
                canvas->data() + (size_t(y + row) * canvas_width + x) * 4);
  }
}
}  // namespace

const std::vector<Slot>& ButtonSlots() {
  static const std::vector<Slot> slots = {
      {"confirm_a", {2, 0, 81, 81}}, {"back_b", {87, 0, 80, 80}},
      {"x", {172, 0, 80, 80}}, {"y", {257, 0, 80, 80}},
      {"glyph_0067", {340, 0, 21, 21}}, {"glyph_0063", {425, 0, 21, 21}},
      {"left_trigger", {11, 85, 63, 63}}, {"left_bumper", {86, 85, 82, 82}},
      {"right_trigger", {180, 85, 64, 64}}, {"right_bumper", {256, 85, 82, 82}},
      {"next_arrow", {341, 85, 83, 83}}, {"previous_arrow", {426, 85, 83, 83}},
      {"dpad", {2, 170, 81, 81}}, {"glyph_0031", {87, 170, 81, 81}},
      {"glyph_0032", {172, 170, 81, 81}}, {"glyph_0033", {257, 170, 81, 81}},
      {"glyph_0034", {342, 170, 81, 81}}, {"glyph_0035", {427, 170, 81, 81}},
      {"dpad_up", {2, 255, 81, 81}}, {"dpad_right", {87, 255, 81, 81}},
      {"dpad_down", {172, 255, 81, 81}}, {"dpad_left", {257, 255, 81, 81}},
      {"left_stick", {342, 255, 81, 81}}, {"right_stick", {427, 255, 81, 81}},
      {"glyph_0036", {2, 340, 81, 81}}, {"glyph_0037", {87, 340, 81, 81}},
      {"glyph_0038", {172, 340, 81, 81}}, {"glyph_0039", {257, 340, 81, 81}},
      {"glyph_005e", {342, 340, 81, 81}}, {"glyph_0026", {427, 340, 81, 81}},
      {"glyph_0028", {2, 425, 81, 81}}, {"glyph_002a", {87, 425, 81, 81}},
  };
  return slots;
}
const std::vector<std::string>& PadSlots() {
  static const std::vector<std::string> slots = {"xbox_0", "xbox_1", "xbox_2", "xbox_3",
                                                 "ps_0", "ps_1", "ps_2", "ps_3"};
  return slots;
}
const Image* Scheme::Pad(const std::string& slot) const {
  auto it = pads.find(slot);
  if (it != pads.end()) return &it->second;
  return has_shared_pad ? &shared_pad : nullptr;
}

bool LoadScheme(const fs::path& folder, bool trusted_reserved, DecodePng decoder,
                Scheme* result, std::string* why) {
  std::error_code ec;
  if (fs::is_symlink(folder, ec) || !fs::is_directory(folder, ec)) {
    *why = "package folder is missing or a symlink"; return false;
  }
  const fs::path manifest = folder / "scheme.toml";
  if (fs::is_symlink(manifest, ec) || !fs::is_regular_file(manifest, ec) ||
      fs::file_size(manifest, ec) > 65536) {
    *why = "missing, linked, or oversized scheme.toml"; return false;
  }
  std::ifstream file(manifest, std::ios::binary);
  if (!file) { *why = "cannot read scheme.toml"; return false; }
  std::map<std::string, std::string> values;
  std::string section, line;
  unsigned line_no = 0;
  while (std::getline(file, line)) {
    ++line_no;
    // TOML comments outside quoted strings only.
    bool quoted = false, escape = false;
    for (size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '"' && !escape) quoted = !quoted;
      if (line[i] == '#' && !quoted) { line.resize(i); break; }
      if (line[i] == '\\' && !escape) escape = true; else escape = false;
    }
    line = Trim(line);
    if (line.empty()) continue;
    if (line.front() == '[' && line.back() == ']') {
      section = line.substr(1, line.size() - 2);
      if (section != "buttons" && section != "controller" &&
          section != "controller.slots" && section != "icons") {
        *why = "unknown section at line " + std::to_string(line_no); return false;
      }
      continue;
    }
    auto equal = line.find('=');
    if (equal == std::string::npos) { *why = "invalid line " + std::to_string(line_no); return false; }
    std::string key = Trim(line.substr(0, equal));
    std::string raw = Trim(line.substr(equal + 1));
    if (key.empty()) { *why = "empty key"; return false; }
    std::string value;
    if (section.empty() && key == "schema_version") {
      if (raw != "1") { *why = "schema_version must be integer 1"; return false; }
      value = "1";
    } else if (!ParseString(raw, &value)) {
      *why = "invalid value at line " + std::to_string(line_no); return false;
    }
    const std::string full = section.empty() ? key : section + "." + key;
    if (!values.emplace(full, value).second) { *why = "duplicate key: " + full; return false; }
  }
  auto required = [&](const char* key) -> std::string {
    auto it = values.find(key); return it == values.end() ? "" : it->second;
  };
  if (required("schema_version") != "1") { *why = "schema_version must be 1"; return false; }
  Scheme scheme;
  scheme.id = required("id"); scheme.name = required("name"); scheme.folder = folder;
  if (!std::regex_match(scheme.id, std::regex("[a-z0-9][a-z0-9-]{0,31}")) ||
      scheme.id != folder.filename().string()) { *why = "invalid id or folder name"; return false; }
  if (scheme.id == "retail" || scheme.id == "keyboard" ||
      (Reserved(scheme.id) && !trusted_reserved)) { *why = "reserved id"; return false; }
  if (!ValidName(scheme.name)) { *why = "name must be 1-20 UTF-8 characters"; return false; }
  if (required("kind") != "controller") { *why = "kind must be controller"; return false; }
  std::set<std::string> paths;
  unsigned image_count = 0;
  for (const auto& [key, value] : values) {
    if (key == "schema_version" || key == "id" || key == "name" || key == "kind") continue;
    Image* image = nullptr;
    if (key.starts_with("buttons.") && FindButton(key.substr(8))) image = &scheme.buttons[key.substr(8)];
    else if (key.starts_with("controller.slots.") && IsPad(key.substr(17)))
      image = &scheme.pads[key.substr(17)];
    else if (key == "controller.file") { image = &scheme.shared_pad; scheme.has_shared_pad = true; }
    else if (key == "icons.file") { image = &scheme.icons; scheme.has_icons = true; }
    else { *why = "unknown key: " + key; return false; }
    const fs::path relative = fs::path(std::u8string(value.begin(), value.end()));
    if (!paths.insert(relative.generic_string()).second) {
      *why = "duplicate image path: " + value; return false;
    }
    if (++image_count > 64) { *why = "more than 64 images"; return false; }
    if (!LoadImage(folder, value, decoder, image, why)) return false;
    if ((key == "controller.file" || key.starts_with("controller.slots.")) &&
        (image->width > 2048 || image->height > 2048)) { *why = "invalid pad"; return false; }
    if (key == "icons.file" && (image->width != 1024 || image->height != 512)) {
      *why = "icons.png must be 1024x512"; return false;
    }
  }
  if (scheme.buttons.empty() && scheme.pads.empty() && !scheme.has_shared_pad) {
    *why = "package has no button or pad replacement"; return false;
  }
  unsigned files = 0;
  for (fs::recursive_directory_iterator it(folder, fs::directory_options::none, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (it->is_symlink(ec)) { *why = "package contains a symlink"; return false; }
    if (!it->is_regular_file(ec)) continue;
    if (++files > 64) { *why = "package contains more than 64 files"; return false; }
    const auto relative = fs::relative(it->path(), folder, ec);
    if (relative != fs::path("scheme.toml") &&
        !paths.contains(relative.generic_string())) scheme.extra_files.push_back(relative);
  }
  if (ec) { *why = "cannot enumerate package"; return false; }
  *result = std::move(scheme);
  return true;
}

const Scheme* Registry::Find(const std::string& id) const {
  auto it = schemes.find(id); return it == schemes.end() ? nullptr : &it->second;
}
std::string Registry::Summary() const {
  std::ostringstream out;
  out << "Xbox 360 (retail)";
  for (const auto& [id, scheme] : schemes) out << ", " << scheme.name << " (" << id << ")";
  out << "; rejected=" << rejected.size() << "; shadowed=" << shadowed.size();
  return out.str();
}
Registry Discover(const fs::path& install_root, const fs::path& user_root, DecodePng decoder) {
  Registry registry;
  auto scan = [&](const fs::path& root, bool user) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    std::vector<fs::path> folders;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
      folders.push_back(it->path());
    std::sort(folders.begin(), folders.end());
    for (const auto& folder : folders) {
      if (!fs::is_directory(folder, ec) && !fs::is_symlink(folder, ec)) continue;
      Scheme scheme;
      std::string why;
      // The installer owns reserved packages in the install root, including
      // the optional PS3 import. User packages cannot shadow any reserved id.
      const std::string id = folder.filename().string();
      const bool trusted = !user;
      if (!LoadScheme(folder, trusted, decoder, &scheme, &why)) {
        registry.rejected.push_back({folder, why}); continue;
      }
      auto it = registry.schemes.find(scheme.id);
      if (it != registry.schemes.end()) {
        if (Reserved(scheme.id)) { registry.rejected.push_back({folder, "reserved family duplicate"}); continue; }
        registry.shadowed.push_back(scheme.id + ": user package shadows install package");
      }
      registry.schemes[scheme.id] = std::move(scheme);
    }
  };
  scan(install_root, false); scan(user_root, true);
  return registry;
}

Image Fit(const Image& source, uint32_t width, uint32_t height) {
  Image out{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
  if (!ValidImage(source) || !width || !height) return out;
  const double scale = std::min(double(width) / source.width, double(height) / source.height);
  const uint32_t w = std::clamp(uint32_t(source.width * scale + 0.5), 1u, width);
  const uint32_t h = std::clamp(uint32_t(source.height * scale + 0.5), 1u, height);
  Image resized{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  assets::ResizeRgba(source.rgba.data(), source.width, source.height, resized.rgba.data(), w, h);
  const uint32_t x = (width - w) / 2, y = (height - h) / 2;
  for (uint32_t row = 0; row < h; ++row)
    std::copy_n(resized.rgba.data() + size_t(row) * w * 4, size_t(w) * 4,
                out.rgba.data() + (size_t(y + row) * width + x) * 4);
  return out;
}

bool ComposeButtons(const std::vector<std::vector<uint8_t>>& original, const Scheme& scheme,
                    std::vector<std::vector<uint8_t>>* levels) {
  if (original.empty() || original.size() > 10 || scheme.buttons.empty()) return false;
  levels->clear();
  for (size_t mip = 0; mip < original.size(); ++mip) {
    const uint32_t dim = std::max(1u, 512u >> mip);
    if (original[mip].size() != assets::MipLevelBytes(assets::TextureFormat::kDxt5, dim, dim, 0))
      return false;
    std::vector<uint8_t> rgba(size_t(dim) * dim * 4);
    assets::DecodeDxt5(original[mip].data(), dim, dim, rgba.data());
    std::vector<uint8_t> touched(size_t((dim + 3) / 4) * ((dim + 3) / 4));
    for (const auto& [id, image] : scheme.buttons) {
      const Slot* slot = FindButton(id);
      if (!slot) return false;
      const Rect& r = slot->rect;
      // The font's UV rectangle is the image destination, but the retail BC3
      // pixels can extend a couple of pixels beyond it. A's circle reaches
      // rows 81-82 while its measured UV rectangle ends at row 80. Clear the
      // entire atlas cell so those old pixels cannot remain under a partial
      // replacement, then fit the new image into the exact measured rectangle.
      const uint32_t cell_x = (r.x / 85) * 85;
      const uint32_t cell_y = (r.y / 85) * 85;
      const uint32_t cell_right = std::min(512u, cell_x + 85);
      const uint32_t cell_bottom = std::min(512u, cell_y + 85);
      const uint32_t clear_x = cell_x >> mip, clear_y = cell_y >> mip;
      const uint32_t clear_right = std::min(dim, (cell_right + (1u << mip) - 1) >> mip);
      const uint32_t clear_bottom = std::min(dim, (cell_bottom + (1u << mip) - 1) >> mip);
      for (uint32_t row = clear_y; row < clear_bottom; ++row)
        std::fill_n(rgba.data() + (size_t(row) * dim + clear_x) * 4,
                    size_t(clear_right - clear_x) * 4, 0);
      uint32_t x = r.x >> mip, y = r.y >> mip;
      uint32_t right = std::min(dim, (r.x + r.width + (1u << mip) - 1) >> mip);
      uint32_t bottom = std::min(dim, (r.y + r.height + (1u << mip) - 1) >> mip);
      if (right <= x || bottom <= y) continue;
      BlitFit(image, &rgba, dim, x, y, right - x, bottom - y);
      const uint32_t pitch = (dim + 3) / 4;
      for (uint32_t by = clear_y / 4; by < (clear_bottom + 3) / 4; ++by)
        for (uint32_t bx = clear_x / 4; bx < (clear_right + 3) / 4; ++bx)
          touched[size_t(by) * pitch + bx] = 1;
    }
    std::vector<uint8_t> encoded;
    if (dim >= 4) {
      assets::EncodeDxt5(rgba.data(), dim, dim, &encoded);
    } else {
      std::array<uint8_t, 4 * 4 * 4> padded{};
      for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x)
          std::copy_n(rgba.data() + (size_t(std::min(y, dim - 1)) * dim +
                                      std::min(x, dim - 1)) * 4, 4,
                      padded.data() + (y * 4 + x) * 4);
      assets::EncodeDxt5(padded.data(), 4, 4, &encoded);
    }
    auto output = original[mip];
    for (size_t block = 0; block < touched.size(); ++block)
      if (touched[block]) std::copy_n(encoded.data() + block * 16, 16, output.data() + block * 16);
    levels->push_back(std::move(output));
  }
  return true;
}
std::vector<std::vector<uint8_t>> ComposePad(const Image& image, uint32_t mip_count) {
  std::vector<std::vector<uint8_t>> levels;
  if (!ValidImage(image) || mip_count > 11) return levels;
  Image base = Fit(image, 1024, 1024);
  for (uint32_t mip = 0; mip < mip_count; ++mip) {
    const uint32_t dim = 1024u >> mip;
    std::vector<uint8_t> rgba(size_t(dim) * dim * 4);
    if (mip == 0) rgba = base.rgba;
    else assets::ResizeRgba(base.rgba.data(), 1024, 1024, rgba.data(), dim, dim);
    std::vector<uint8_t> blocks;
    if (dim >= 4) assets::EncodeDxt5(rgba.data(), dim, dim, &blocks);
    else {
      std::array<uint8_t, 4 * 4 * 4> padded{};
      for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x)
          std::copy_n(rgba.data() + (size_t(std::min(y, dim - 1)) * dim +
                                      std::min(x, dim - 1)) * 4, 4,
                      padded.data() + (y * 4 + x) * 4);
      assets::EncodeDxt5(padded.data(), 4, 4, &blocks);
    }
    levels.push_back(std::move(blocks));
  }
  return levels;
}
std::vector<std::vector<uint8_t>> ComposeIcons(const Image& image, uint32_t mip_count) {
  std::vector<std::vector<uint8_t>> levels;
  if (!ValidImage(image) || image.width != 1024 || image.height != 512 ||
      mip_count > 11) return levels;
  for (uint32_t mip = 0; mip < mip_count; ++mip) {
    const uint32_t width = std::max(1u, 1024u >> mip);
    const uint32_t height = std::max(1u, 512u >> mip);
    std::vector<uint8_t> rgba(size_t(width) * height * 4);
    if (mip == 0) rgba = image.rgba;
    else assets::ResizeRgba(image.rgba.data(), 1024, 512, rgba.data(), width, height);
    std::vector<uint8_t> blocks;
    if (width >= 4 && height >= 4) assets::EncodeDxt5(rgba.data(), width, height, &blocks);
    else {
      const uint32_t padded_width = std::max(4u, width);
      const uint32_t padded_height = std::max(4u, height);
      std::vector<uint8_t> padded(size_t(padded_width) * padded_height * 4);
      for (uint32_t y = 0; y < padded_height; ++y)
        for (uint32_t x = 0; x < padded_width; ++x)
          std::copy_n(rgba.data() + (size_t(std::min(y, height - 1)) * width +
                                      std::min(x, width - 1)) * 4, 4,
                      padded.data() + (size_t(y) * padded_width + x) * 4);
      assets::EncodeDxt5(padded.data(), padded_width, padded_height, &blocks);
    }
    levels.push_back(std::move(blocks));
  }
  return levels;
}
bool UntileBc3Base(const uint8_t* guest, size_t guest_size, uint32_t width,
                   bool tiled, uint32_t endianness, std::vector<uint8_t>* canonical) {
  if (!guest || !canonical || !width || width % 4 || width > 2048 ||
      guest_size < size_t(width) * width || endianness > 3) return false;
  const uint32_t pitch = width / 4;
  canonical->resize(size_t(width) * width);
  for (uint32_t y = 0; y < pitch; ++y) {
    for (uint32_t x = 0; x < pitch; ++x) {
      uint32_t source = (y * pitch + x) * 16;
      if (tiled) {
        const uint32_t macro = ((x >> 5) + (y >> 5) * ((pitch + 31) / 32)) << 11;
        const uint32_t micro = ((x & 7) + ((y & 14) << 2)) << 4;
        const uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15u) + ((y & 1) << 4);
        source = ((offset & ~511u) << 3) + ((y & 16) << 7) + ((offset & 448) << 2) +
                 (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 63);
      }
      if (size_t(source) + 16 > guest_size) return false;
      uint8_t* destination = canonical->data() + (size_t(y) * pitch + x) * 16;
      std::copy_n(guest + source, 16, destination);
      switch (endianness) {
        case 1: assets::SwapSixteenBitWords(destination, 16); break;
        case 2:
          for (uint32_t i = 0; i < 16; i += 4) {
            std::swap(destination[i], destination[i + 3]);
            std::swap(destination[i + 1], destination[i + 2]);
          }
          break;
        case 3:
          for (uint32_t i = 0; i < 16; i += 4) {
            std::swap(destination[i], destination[i + 2]);
            std::swap(destination[i + 1], destination[i + 3]);
          }
          break;
      }
    }
  }
  return true;
}
}  // namespace rb_blitz::controller_schemes
