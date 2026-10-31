// SPDX-License-Identifier: GPL-2.0-only
#include "controller_schemes/schemes.h"
#include "assets/texture_codec.h"
#include "assets/input_font_atlas.h"
#include "ui/input_font_scene.h"

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
  auto one = Package(install, "one", "[buttons]\na_button = \"buttons/a.png\"\n");
  Write(one / "buttons/a.png", "PNG");
  Scheme scheme;
  std::string why;
  Check(LoadScheme(one, false, FakePng, &scheme, &why), "one-slot package rejected");
  Check(scheme.buttons.size() == 1 && scheme.Pad("xbox_0") == nullptr,
        "omitted slots were populated");
  Write(one / "extra.txt", "ignored");
  Check(LoadScheme(one, false, FakePng, &scheme, &why) && scheme.extra_files.size() == 1,
        "extra file was not reported");
  const fs::path art = fs::path(RBBLITZ_SOURCE_DIR) / "assets/controller";
  const char* authored_ids[] = {"xbox-series", "playstation-4", "playstation-5",
      "nintendo-switch-joycon", "nintendo-switch-pro-controller",
      "nintendo-switch-2-joycon", "nintendo-switch-2-pro-controller", "steam-deck"};
  for (const char* id : authored_ids) {
    Check(LoadScheme(art / id, false, FakePng, &scheme, &why),
          "authored v2 controller rejected");
    Check(scheme.schema_version == 2 && scheme.id == id && scheme.inputs.size() >= 26 &&
          scheme.InputFor("pad:a") &&
          (scheme.Pad("xbox_0") != nullptr) ==
              (std::string(id) != "playstation-4" && std::string(id) != "playstation-5"),
          "authored v2 lookup wrong");
    if (std::string(id) == "steam-deck")
      Check(scheme.InputFor("sdl:button:misc1") &&
            scheme.InputFor("sdl:button:left_paddle1"), "Deck extra source art missing");
  }
  Check(!LoadScheme(art / "keyboard-mouse", true, FakePng, &scheme, &why),
        "internal keyboard became selectable");
  Check(LoadInternalKeyboard(art / "keyboard-mouse", FakePng, &scheme, &why),
        "internal keyboard rejected");
  Check(scheme.inputs.size() == 92 && scheme.declared_files.size() == 92 &&
        scheme.InputFor("key:Return") && scheme.InputFor("key:KP Enter") &&
        scheme.InputFor("mouse:left") && scheme.extra_files.size() == 3 &&
        scheme.InputFor("key:8", {}, "shift") &&
        scheme.InputFor("key:8", {}, "shift")->id == "asterisk",
        "internal keyboard manifest or declared-file audit wrong");
  const auto v2 = root / "v2";
  fs::create_directories(v2);
  Write(v2 / "scheme.toml", "schema_version = 2\nid = \"v2\"\nname = \"V2\"\n"
      "kind = \"controller\"\n[[input]]\nid = \"a\"\nfile = \"a.png\"\n"
      "source = \"pad:a\"\nslot = \"a_button\"\n"
      "[[input]]\nid = \"a-alt\"\nfile = \"alt.png\"\n"
      "source = \"pad:a\"\nvariant = \"alternate\"\n");
  Write(v2 / "a.png", "PNG");
  Write(v2 / "alt.png", "PNG");
  Check(LoadScheme(v2, false, FakePng, &scheme, &why) &&
        scheme.declared_files.size() == 2 && scheme.InputFor("pad:a") &&
        scheme.InputFor("pad:a", "alternate") &&
        scheme.InputFor("pad:a")->id == "a" &&
        scheme.InputFor("pad:a", "alternate")->id == "a-alt",
        "v2 source variant/modifier lookup wrong");
  Write(v2 / "a.png", "");
  fs::remove(v2 / "a.png");
  Check(!LoadScheme(v2, false, FakePng, &scheme, &why), "v2 missing file accepted");
  Check(!LoadScheme(Package(install, "keyboard", "[buttons]\na_button = \"a.png\"\n"),
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
  invalid = Package(install, "invalid", "[buttons]\na_button = \"missing.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "missing image accepted");
  Write(invalid / "bad.png", "PNG");
  invalid = Package(install, "invalid", "[buttons]\na_button = \"bad.png\"\n"
      "b_button = \"bad.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "duplicate image path accepted");
  Write(invalid / "oversize.png", "PNG");
  invalid = Package(install, "invalid", "[buttons]\na_button = \"oversize.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "oversized pixels accepted");
  Write(invalid / "corrupt.png", "PNG");
  invalid = Package(install, "invalid", "[buttons]\na_button = \"corrupt.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "corrupt image accepted");
  invalid = Package(install, "invalid", "kind = \"keyboard\"\n[buttons]\na_button = \"bad.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "keyboard kind accepted");
  invalid = Package(install, "invalid", "[buttons]\na_button = \"../outside.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "traversal accepted");
  Write(invalid / "zero.png", "PNG");
  invalid = Package(install, "invalid", "[buttons]\na_button = \"zero.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "zero-size image accepted");
  Write(invalid / "scheme.toml", "schema_version = \"1\"\nid = \"invalid\"\n"
      "name = \"Test\"\nkind = \"controller\"\n[buttons]\na_button = \"zero.png\"\n");
  Check(!LoadScheme(invalid, false, FakePng, &scheme, &why), "quoted schema version accepted");
  auto authored = Package(install, "xbox-series", "[buttons]\na_button = \"a.png\"\n");
  Write(authored / "a.png", "PNG");
  auto customized = Package(user, "xbox-series", "[buttons]\na_button = \"a.png\"\n");
  Write(customized / "a.png", "PNG");
  Check(LoadScheme(customized, false, FakePng, &scheme, &why),
        "custom package with authored family id rejected");
  auto discovered = Discover(install, user, FakePng);
  Check(discovered.Find("xbox-series") &&
        discovered.Find("xbox-series")->folder == customized &&
        discovered.shadowed.size() == 1, "user package did not shadow authored package");
  auto reserved = Package(user, "ps3", "[buttons]\na_button = \"a.png\"\n");
  Write(reserved / "a.png", "PNG");
  Check(!LoadScheme(reserved, false, FakePng, &scheme, &why), "reserved PS3 import id accepted");
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
  Scheme one_slot; one_slot.buttons["a_button"] = {128, 128, std::vector<uint8_t>(128 * 128 * 4)};
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

  // The guest mapping font gets per-row images while all original font blocks stay byte exact.
  std::vector<uint8_t> font_chain;
  for (uint32_t mip = 0; mip < rb_blitz::ui::input_font::kMipCount; ++mip) {
    const uint32_t dim = 1024 >> mip;
    std::vector<uint8_t> blank(size_t(dim) * dim * 4, 0);
    std::vector<uint8_t> level;
    rb_blitz::assets::EncodeDxt5(blank.data(), dim, dim, &level);
    font_chain.insert(font_chain.end(), level.begin(), level.end());
  }
  rb_blitz::input::icons::Icon key_icon;
  key_icon.art = rb_blitz::input::icons::Art::kBuiltIn;
  key_icon.width = key_icon.height = 16;
  key_icon.rgba.resize(16 * 16 * 4, 255);
  std::vector<std::vector<uint8_t>> font_levels;
  Check(rb_blitz::assets::input_font_atlas::Compose(
      font_chain, blocks, nullptr, {key_icon}, &font_levels),
      "mapping font composition failed");
  Check(font_levels.size() == 7 && font_levels[0].size() == 1024 * 1024 &&
        font_levels[6].size() == 16 * 16, "mapping font mip chain wrong");
  size_t font_offset = 0;
  for (uint32_t mip = 0; mip < 7; ++mip) {
    const uint32_t dim = 1024 >> mip;
    const size_t untouched = size_t(rb_blitz::ui::input_font::kIconTop >> mip) * dim;
    Check(std::equal(font_chain.begin() + font_offset,
                     font_chain.begin() + font_offset + untouched,
                     font_levels[mip].begin()), "original font blocks changed");
    font_offset += size_t(dim) * dim;
  }
  std::vector<uint8_t> decoded_font(1024 * 1024 * 4);
  rb_blitz::assets::DecodeDxt5(font_levels[0].data(), 1024, 1024, decoded_font.data());
  Check(decoded_font[(size_t(rb_blitz::ui::input_font::kIconTop + 8) * 1024 + 32) * 4 + 3] > 200,
        "mapping icon was not drawn in the spare atlas cell");
  Check(decoded_font[(size_t(500) * 1024 + 32) * 4 + 3] == 0,
        "original font area was modified");

  rb_blitz::input::icons::Icon source_image;
  source_image.art = rb_blitz::input::icons::Art::kPackageImage;
  source_image.width = source_image.height = 16;
  source_image.rgba.resize(16 * 16 * 4, 255);
  for (size_t i = 0; i < source_image.rgba.size(); i += 4) {
    source_image.rgba[i] = 255;
    source_image.rgba[i + 1] = 0;
    source_image.rgba[i + 2] = 0;
  }
  Check(rb_blitz::assets::input_font_atlas::Compose(
      font_chain, blocks, nullptr, {source_image}, &font_levels),
      "source-keyed image without an atlas slot was rejected");
  rb_blitz::assets::DecodeDxt5(font_levels[0].data(), 1024, 1024, decoded_font.data());
  const size_t source_pixel =
      (size_t(rb_blitz::ui::input_font::kIconTop + 40) * 1024 + 34) * 4;
  Check(decoded_font[source_pixel] > 200 && decoded_font[source_pixel + 1] < 50 &&
        decoded_font[source_pixel + 2] < 50,
        "source-keyed image lost its own color in the mapping font");

  // The measured font UV rectangle for A ends at y=80, while the installed
  // circle still has antialiased pixels at y=82. Atlas composition needs the
  // full 85-square retail cell so that lower arc is never amputated.
  std::vector<uint8_t> edge_rgba(512 * 512 * 4, 0);
  for (uint32_t y = 80; y <= 82; ++y)
    for (uint32_t x = 36; x <= 44; ++x) {
      const size_t p = (size_t(y) * 512 + x) * 4;
      edge_rgba[p] = 60; edge_rgba[p + 1] = 190;
      edge_rgba[p + 2] = 70; edge_rgba[p + 3] = 255;
    }
  std::vector<uint8_t> edge_blocks;
  rb_blitz::assets::EncodeDxt5(edge_rgba.data(), 512, 512, &edge_blocks);
  rb_blitz::input::icons::Icon retail_a;
  retail_a.art = rb_blitz::input::icons::Art::kSchemeSlot;
  retail_a.slot = "a_button";
  Check(rb_blitz::assets::input_font_atlas::Compose(
      font_chain, edge_blocks, nullptr, {retail_a}, &font_levels),
      "retail mapping glyph composition failed");
  rb_blitz::assets::DecodeDxt5(font_levels[0].data(), 1024, 1024, decoded_font.data());
  Check(decoded_font[(size_t(rb_blitz::ui::input_font::kIconTop + 8 + 61) * 1024 + 30) * 4 + 3] > 100,
        "retail A lower arc was cropped from the mapping glyph");

  // RndFont always samples 79.5 pixels vertically. Adjacent glyph rows must
  // leave that complete window clear above/below the centered 64-pixel image.
  std::vector<rb_blitz::input::icons::Icon> full_page(
      rb_blitz::ui::input_font::kIconCount, key_icon);
  Check(rb_blitz::assets::input_font_atlas::Compose(
      font_chain, blocks, nullptr, full_page, &font_levels), "full mapping atlas failed");
  rb_blitz::assets::DecodeDxt5(font_levels[0].data(), 1024, 1024, decoded_font.data());
  for (uint32_t i = 0; i < rb_blitz::ui::input_font::kIconCount; ++i) {
    const uint32_t x = (i % 15) * 68 + 2 + 32;
    const uint32_t y = rb_blitz::ui::input_font::kIconTop + (i / 15) * 80;
    auto alpha = [&](uint32_t dy) { return decoded_font[(size_t(y + dy) * 1024 + x) * 4 + 3]; };
    Check(alpha(0) == 0 && alpha(7) == 0 && alpha(72) == 0 && alpha(79) == 0,
          "neighbor icon leaks into the font's fixed-height sampling window");
    Check(alpha(8) > 200 && alpha(71) > 200, "icon is not centered in its font cell");
    const uint32_t left = (i % 15) * 68;
    Check(decoded_font[(size_t(y + 40) * 1024 + left) * 4 + 3] == 0 &&
          decoded_font[(size_t(y + 40) * 1024 + left + 67) * 4 + 3] == 0,
          "adjacent icons have no transparent sampling gutter");
  }

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

  auto shadow = Package(user, "one", "[buttons]\na_button = \"a.png\"\n");
  Write(shadow / "a.png", "PNG");
  auto registry = Discover(install, user, FakePng);
  Check(registry.Find("one") && registry.Find("one")->folder == shadow &&
        registry.shadowed.size() == 2, "user shadow failed");
  fs::remove_all(root, ec);
  std::cout << "button_scheme tests passed\n";
}
