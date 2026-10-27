// SPDX-License-Identifier: GPL-2.0-only
#include "controller_schemes/schemes.h"
#include "assets/texture_codec.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace rb_blitz::controller_schemes;

static bool FakePng(const fs::path& path, Image* out, std::string* why) {
  const auto name = path.filename().string();
  if (name == "corrupt.png") { *why = "corrupt PNG"; return false; }
  uint32_t w = 128, h = 128;
  if (name == "wide.png") { w = 64; h = 50; }
  if (name == "oversize.png") { w = 2049; h = 1; }
  if (name == "zero.png") { w = 0; h = 1; }
  *out = {w, h, std::vector<uint8_t>(size_t(w) * h * 4, 255)};
  return true;
}
static void Write(const fs::path& path, const std::string& contents) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << contents;
}
static void WriteHex(const fs::path& path, const std::string& hex) {
  std::string bytes;
  for (size_t i = 0; i < hex.size(); i += 2)
    bytes.push_back(char(std::stoi(hex.substr(i, 2), nullptr, 16)));
  Write(path, bytes);
}
static fs::path Package(const fs::path& root, const std::string& id,
                        const std::string& declarations) {
  const auto folder = root / id;
  fs::create_directories(folder);
  Write(folder / "scheme.toml", "schema_version = 1\nid = \"" + id +
      "\"\nname = \"Test\"\nkind = \"controller\"\n" + declarations);
  return folder;
}
static void Check(bool value, const char* message) {
  if (!value) { std::cerr << message << '\n'; std::abort(); }
}
int main() {
  const fs::path root = fs::temp_directory_path() / "rb_blitz_button_scheme_tests";
  std::error_code ec;
  fs::remove_all(root, ec);
  const auto install = root / "install", user = root / "user";
  auto one = Package(install, "one", "[buttons]\nconfirm_a = \"buttons/a.png\"\n");
  Write(one / "buttons/a.png", "PNG");
  Scheme scheme;
  std::string why;
  Check(LoadScheme(one, false, FakePng, &scheme, &why), "one-slot package rejected");
  Check(scheme.buttons.size() == 1 && scheme.Pad("xbox_0") == nullptr,
        "omitted slots were populated");
  Write(one / "extra.txt", "ignored");
  Check(LoadScheme(one, false, FakePng, &scheme, &why) && scheme.extra_files.size() == 1,
        "extra file was not reported");
  Check(!LoadScheme(Package(install, "keyboard", "[buttons]\nconfirm_a = \"a.png\"\n"),
                    false, FakePng, &scheme, &why), "keyboard package accepted");
  auto pad = Package(install, "pads", "[controller]\nfile = \"pads/shared.png\"\n"
      "[controller.slots]\nps_1 = \"pads/specific.png\"\n");
  Write(pad / "pads/shared.png", "PNG"); Write(pad / "pads/specific.png", "PNG");
  Check(LoadScheme(pad, false, FakePng, &scheme, &why), "pad-only package rejected");
  Check(scheme.Pad("ps_0") == &scheme.shared_pad && scheme.Pad("ps_1") == &scheme.pads.at("ps_1"),
        "specific pad did not override shared pad");
  auto pad_levels = ComposePad(*scheme.Pad("ps_1"), 7);
  Check(pad_levels.size() == 7 && pad_levels.front().size() == 1024 * 1024 &&
        pad_levels.back().size() == 16 * 16, "pad BC3 mip chain wrong");
  Image icons{1024, 512, std::vector<uint8_t>(1024 * 512 * 4, 255)};
  auto icon_levels = ComposeIcons(icons, 11);
  Check(icon_levels.size() == 11 && icon_levels.front().size() == 1024 * 512 &&
        icon_levels.back().size() == 16, "extra icons BC3 mip chain wrong");
  auto invalid = Package(install, "invalid", "[buttons]\nno_such_slot = \"a.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "unknown slot accepted");
  invalid = Package(install, "invalid", "[buttons]\nconfirm_a = \"missing.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "missing image accepted");
  Write(invalid / "bad.png", "PNG");
  invalid = Package(install, "invalid", "[buttons]\nconfirm_a = \"bad.png\"\n"
      "back_b = \"bad.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "duplicate image path accepted");
  Write(invalid / "oversize.png", "PNG");
  invalid = Package(install, "invalid", "[buttons]\nconfirm_a = \"oversize.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "oversized pixels accepted");
  Write(invalid / "corrupt.png", "PNG");
  invalid = Package(install, "invalid", "[buttons]\nconfirm_a = \"corrupt.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "corrupt image accepted");
  invalid = Package(install, "invalid", "kind = \"keyboard\"\n[buttons]\nconfirm_a = \"bad.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "keyboard kind accepted");
  invalid = Package(install, "invalid", "[buttons]\nconfirm_a = \"../outside.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "traversal accepted");
  Write(invalid / "zero.png", "PNG");
  invalid = Package(install, "invalid", "[buttons]\nconfirm_a = \"zero.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "zero-size image accepted");
  Write(invalid / "scheme.toml", "schema_version = \"1\"\nid = \"invalid\"\n"
      "name = \"Test\"\nkind = \"controller\"\n[buttons]\nconfirm_a = \"zero.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "quoted schema version accepted");
  auto reserved = Package(user, "xbox-series", "[buttons]\nconfirm_a = \"a.png\"\n");
  Write(reserved / "a.png", "PNG");
  Check(!LoadScheme(reserved, false, FakePng, &scheme, &why), "reserved user family accepted");
#ifdef _WIN32
  const fs::path rgba_file = root / "rgba.png", rgb_file = root / "rgb.png";
  WriteHex(rgba_file, "89504e470d0a1a0a0000000d4948445200000001000000010806000000"
                      "1f15c4890000000d49444154789c63f8cfc0c00000040101004706cade"
                      "0000000049454e44ae426082");
  WriteHex(rgb_file, "89504e470d0a1a0a0000000d4948445200000001000000010802000000"
                     "907753de0000000c49444154789c63f8cfc0000003010100c9fe92ef"
                     "0000000049454e44ae426082");
  Image decoded_png;
  Check(DecodePngWic(rgba_file, &decoded_png, &why) && decoded_png.width == 1 &&
        decoded_png.height == 1 && decoded_png.rgba.size() == 4, "RGBA PNG rejected");
  Check(!DecodePngWic(rgb_file, &decoded_png, &why), "RGB PNG accepted without alpha");
#endif

  Image square{128, 128, std::vector<uint8_t>(128 * 128 * 4, 255)};
  Image fitted = Fit(square, 32, 32);
  Check(fitted.width == 32 && fitted.height == 32 && fitted.rgba.front() == 255,
        "square fit wrong");
  Image wide{64, 50, std::vector<uint8_t>(64 * 50 * 4, 255)};
  fitted = Fit(wide, 32, 32);
  Check(fitted.rgba[3] == 0 && fitted.rgba[(size_t(3) * 32 + 16) * 4 + 3] == 255 &&
        fitted.rgba[(size_t(29) * 32 + 16) * 4 + 3] == 0, "wide fit/centering wrong");

  // Original sheet is synthetic: this checks exact BC3 preservation without game art.
  std::vector<uint8_t> original_rgba(512 * 512 * 4, 0);
  for (size_t i = 0; i < original_rgba.size(); i += 4) {
    original_rgba[i] = 17; original_rgba[i + 1] = 35;
    original_rgba[i + 2] = 51; original_rgba[i + 3] = 255;
  }
  std::vector<uint8_t> blocks;
  rb_blitz::assets::EncodeDxt5(original_rgba.data(), 512, 512, &blocks);
  Scheme one_slot; one_slot.buttons["confirm_a"] = {128, 128, std::vector<uint8_t>(128 * 128 * 4)};
  std::vector<std::vector<uint8_t>> composed;
  Check(ComposeButtons({blocks}, one_slot, &composed), "button composition failed");
  const size_t far_block = (size_t(100) * 128 + 100) * 16;
  Check(std::equal(blocks.begin() + far_block, blocks.begin() + far_block + 16,
                   composed[0].begin() + far_block), "untouched BC3 block changed");
  std::vector<uint8_t> decoded(512 * 512 * 4);
  rb_blitz::assets::DecodeDxt5(composed[0].data(), 512, 512, decoded.data());
  Check(decoded[(size_t(40) * 512 + 40) * 4 + 3] == 0,
        "transparent replacement leaked old glyph");
  Check(decoded[(size_t(82) * 512 + 40) * 4 + 3] == 0,
        "retail art just outside the UV rectangle was not cleared");
  Check(decoded[(size_t(200) * 512 + 200) * 4 + 3] == 255,
        "omitted glyph changed");

  // The P1 callback presents tiled, 8-in-16-endian guest bytes. The inverse
  // must recover the exact canonical BC3 blocks the composer just used.
  std::vector<uint8_t> tiled(blocks.size());
  for (uint32_t y = 0; y < 128; ++y) {
    for (uint32_t x = 0; x < 128; ++x) {
      const uint32_t macro = ((x >> 5) + (y >> 5) * 4) << 11;
      const uint32_t micro = ((x & 7) + ((y & 14) << 2)) << 4;
      const uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15u) + ((y & 1) << 4);
      const uint32_t target = ((offset & ~511u) << 3) + ((y & 16) << 7) +
          ((offset & 448) << 2) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 63);
      std::copy_n(blocks.data() + (size_t(y) * 128 + x) * 16, 16, tiled.data() + target);
      rb_blitz::assets::SwapSixteenBitWords(tiled.data() + target, 16);
    }
  }
  std::vector<uint8_t> canonical;
  Check(UntileBc3Base(tiled.data(), tiled.size(), 512, true, 1, &canonical) &&
        canonical == blocks, "tiled endian-swapped BC3 did not round-trip");

  auto shadow = Package(user, "one", "[buttons]\nconfirm_a = \"a.png\"\n");
  Write(shadow / "a.png", "PNG");
  auto registry = Discover(install, user, FakePng);
  Check(registry.Find("one") && registry.Find("one")->folder == shadow &&
        registry.shadowed.size() == 1, "user shadow failed");
  fs::remove_all(root, ec);
  std::cout << "button_scheme tests passed\n";
}
