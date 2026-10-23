// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for P3's album art: the platform texture codec (src/assets/texture_codec.h) and the
// key/URL/cache rules (src/store/album_art.h). SDK-free and hermetic - no boot, no guest, no
// network, and no game data: the envelope cases are written from the *measured* field values of the
// shipped files rather than from the files themselves.
//
// What these pin down:
//   * the envelope layout measured on 201 shipped `.png_xbox` files (version, format word at 5,
//     mip count at 10, width at 11, height at 13, the largest mip's row pitch / 4 at 15), including
//     the exact header bytes of three real textures;
//   * the DXT block codec round trip, the 16-bit word swap that makes the Xbox twin, and the mip
//     chain's size arithmetic;
//   * the key/path rule (`offers/<16 hex>/cover_art.png_xbox`); the URL policy behind the API's
//     `album_art` field is `rhythmverse_client.h`'s, and the `store_client` suite already pins it;
//   * the texture cache the guest's art request reads.

#include "check.h"

#include "assets/texture_codec.h"
#include "store/album_art.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::test;
using rb_blitz::assets::BuildPlatformTexture;
using rb_blitz::assets::DecodePlatformTexture;
using rb_blitz::assets::MipChainBytes;
using rb_blitz::assets::ParseTextureEnvelope;
using rb_blitz::assets::TextureEnvelope;
using rb_blitz::assets::TextureFormat;

void CheckStrEq(const std::string& actual, const std::string& expected) {
  if (actual == expected) {
    ++g_checks;
    return;
  }
  Fail(__FILE__, __LINE__, "expected \"" + expected + "\", got \"" + actual + "\"");
}

#define CHECK_STR_EQ(actual, expected) CheckStrEq((actual), (expected))

// A header-only file: the measured 32 bytes plus a zero payload of the size the header describes,
// so `ParseTextureEnvelope` accepts exactly what a real file would.
std::vector<std::uint8_t> HeaderOnlyFile(const std::uint8_t (&header)[32],
                                         TextureFormat format, std::uint32_t width,
                                         std::uint32_t height, std::uint32_t levels) {
  std::vector<std::uint8_t> file(header, header + 32);
  file.resize(32 + MipChainBytes(format, width, height, levels), 0);
  return file;
}

void TestShippedEnvelopes() {
  BeginCase("the shipped textures' envelopes parse to their real dimensions");
  // `ui/image/gen/default_album_art_keep.png_xbox`, its measured 32-byte header (174,752 total,
  // 512x512 DXT1 with 6 mips). Only the structural bytes are copied - no artwork is in them.
  const std::uint8_t album_art[32] = {0x02, 0xE6, 0xBB, 0xC4, 0x2E, 0x04, 0x08, 0x00,
                                      0x00, 0x00, 0x05, 0x00, 0x02, 0x00, 0x02, 0x00,
                                      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  std::vector<std::uint8_t> file = HeaderOnlyFile(album_art, TextureFormat::kDxt1, 512, 512, 6);
  CHECK_EQ(file.size(), 174752u);  // the shipped file's own length
  TextureEnvelope envelope;
  std::string why;
  CHECK_TRUE(ParseTextureEnvelope(file.data(), file.size(), &envelope, &why));
  CHECK_EQ(envelope.version, 2);
  CHECK_TRUE(envelope.format == TextureFormat::kDxt1);
  CHECK_EQ(envelope.width, 512);
  CHECK_EQ(envelope.height, 512);
  CHECK_EQ(envelope.mip_levels, 6);
  CHECK_EQ(envelope.pitch_dwords, 256);  // (512 / 4) * 8 / 4

  // A song cover: 256x256 DXT1, 5 mips, pitch 128.
  const std::uint8_t cover[32] = {0x02, 0x28, 0xE1, 0xB5, 0xE3, 0x04, 0x08, 0x00,
                                  0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x80,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  std::vector<std::uint8_t> cover_file = HeaderOnlyFile(cover, TextureFormat::kDxt1, 256, 256, 5);
  CHECK_EQ(cover_file.size(), 43680u);  // `deathontwolegs_keep.png_xbox`'s own length
  CHECK_TRUE(ParseTextureEnvelope(cover_file.data(), cover_file.size(), &envelope, &why));
  CHECK_TRUE(envelope.format == TextureFormat::kDxt1);
  CHECK_EQ(envelope.width, 256);
  CHECK_EQ(envelope.height, 256);
  CHECK_EQ(envelope.mip_levels, 5);
  CHECK_EQ(envelope.pitch_dwords, 128);

  // A DXT5 sheet of the same size carries twice the pitch: (256 / 4) * 16 / 4 = 256.
  const std::uint8_t sheet[32] = {0x02, 0x01, 0x02, 0x03, 0x04, 0x08, 0x18, 0x00,
                                  0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x00,
                                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  std::vector<std::uint8_t> sheet_file = HeaderOnlyFile(sheet, TextureFormat::kDxt5, 256, 256, 5);
  CHECK_TRUE(ParseTextureEnvelope(sheet_file.data(), sheet_file.size(), &envelope, &why));
  CHECK_TRUE(envelope.format == TextureFormat::kDxt5);
  CHECK_EQ(envelope.pitch_dwords, 256);
  CHECK_EQ(sheet_file.size(), 32u + 65536u + 16384u + 4096u + 1024u + 256u);
}

void TestEnvelopeRefusals() {
  BeginCase("an envelope that lies about its size, version or format is refused");
  std::vector<std::uint8_t> file(32 + 32 * 32, 0);
  file[0] = 2;
  file[5] = 0x08;
  file[6] = 0x18;  // DXT5
  file[10] = 0;    // one mip
  file[11] = 32;
  file[12] = 0;
  file[13] = 32;
  file[14] = 0;
  file[15] = 32;  // (32 / 4) * 16 / 4
  file[16] = 0;
  TextureEnvelope envelope;
  std::string why;
  CHECK_TRUE(ParseTextureEnvelope(file.data(), file.size(), &envelope, &why));
  CHECK_EQ(envelope.pitch_dwords, 32);

  CHECK_FALSE(ParseTextureEnvelope(file.data(), file.size() - 1, &envelope, &why));
  CHECK_TRUE(why.find("header describes") != std::string::npos);

  std::vector<std::uint8_t> short_file(file.begin(), file.begin() + 16);
  CHECK_FALSE(ParseTextureEnvelope(short_file.data(), short_file.size(), &envelope, &why));

  std::vector<std::uint8_t> bad_format = file;
  bad_format[5] = 0x00;
  bad_format[6] = 0x00;
  CHECK_FALSE(ParseTextureEnvelope(bad_format.data(), bad_format.size(), &envelope, &why));
  CHECK_TRUE(why.find("format word") != std::string::npos);

  std::vector<std::uint8_t> version_one = file;
  version_one[0] = 1;
  CHECK_FALSE(ParseTextureEnvelope(version_one.data(), version_one.size(), &envelope, &why));
  CHECK_TRUE(why.find("version 1") != std::string::npos);

  std::vector<std::uint8_t> zero_width = file;
  zero_width[11] = 0;
  zero_width[12] = 0;
  CHECK_FALSE(ParseTextureEnvelope(zero_width.data(), zero_width.size(), &envelope, &why));
}

// A 64x64 RGBA image with a left-to-right gradient, a vertical colour shift and an alpha ramp, so a
// block codec has real work to do at every corner.
std::vector<std::uint8_t> MakeGradient(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> rgba(std::size_t(width) * height * 4);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      std::uint8_t* p = rgba.data() + (std::size_t(y) * width + x) * 4;
      p[0] = static_cast<std::uint8_t>(x * 255 / std::max(1u, width - 1));
      p[1] = static_cast<std::uint8_t>(y * 255 / std::max(1u, height - 1));
      p[2] = static_cast<std::uint8_t>((x + y) * 255 / std::max(1u, width + height - 2));
      p[3] = static_cast<std::uint8_t>(x < width / 2 ? 255 : 64);
    }
  }
  return rgba;
}

void TestCodecRoundTrip() {
  BeginCase("DXT5 and DXT1 round-trip a gradient within the block codec's error");
  const std::uint32_t width = 64;
  const std::uint32_t height = 64;
  const std::vector<std::uint8_t> source = MakeGradient(width, height);

  std::vector<std::uint8_t> dxt5;
  rb_blitz::assets::EncodeDxt5(source.data(), width, height, &dxt5);
  CHECK_EQ(dxt5.size(), (width / 4) * (height / 4) * 16);
  std::vector<std::uint8_t> decoded(std::size_t(width) * height * 4);
  rb_blitz::assets::DecodeDxt5(dxt5.data(), width, height, decoded.data());
  double colour_error = 0.0;
  double alpha_error = 0.0;
  for (std::size_t i = 0; i < source.size(); i += 4) {
    colour_error += std::abs(int(source[i]) - int(decoded[i]));
    colour_error += std::abs(int(source[i + 1]) - int(decoded[i + 1]));
    colour_error += std::abs(int(source[i + 2]) - int(decoded[i + 2]));
    alpha_error += std::abs(int(source[i + 3]) - int(decoded[i + 3]));
  }
  const double pixels = double(width) * height;
  // A bounding-box fit on a smooth gradient stays well under 8 levels per channel; the alpha ramp
  // is two flat levels, and the eight-step palette lands on both exactly.
  CHECK_TRUE(colour_error / (pixels * 3.0) < 8.0);
  CHECK_TRUE(alpha_error / pixels < 1.0);

  // A flat colour is reproduced exactly, which is what the glyph sheets the button-layout plan
  // builds are made of.
  std::vector<std::uint8_t> flat(std::size_t(width) * height * 4);
  for (std::size_t i = 0; i < flat.size(); i += 4) {
    flat[i] = 0x33;
    flat[i + 1] = 0x99;
    flat[i + 2] = 0xCC;
    flat[i + 3] = 0xFF;
  }
  rb_blitz::assets::EncodeDxt5(flat.data(), width, height, &dxt5);
  rb_blitz::assets::DecodeDxt5(dxt5.data(), width, height, decoded.data());
  int worst = 0;
  for (std::size_t i = 0; i < flat.size(); ++i) {
    worst = std::max(worst, std::abs(int(flat[i]) - int(decoded[i])));
  }
  CHECK_TRUE(worst <= 8);  // 5:6:5 quantisation of the endpoints

  std::vector<std::uint8_t> dxt1;
  rb_blitz::assets::EncodeDxt1(source.data(), width, height, &dxt1);
  CHECK_EQ(dxt1.size(), (width / 4) * (height / 4) * 8);
  std::vector<std::uint8_t> decoded1(std::size_t(width) * height * 4);
  rb_blitz::assets::DecodeDxt1(dxt1.data(), width, height, decoded1.data());
  double error1 = 0.0;
  for (std::size_t i = 0; i < source.size(); i += 4) {
    error1 += std::abs(int(source[i]) - int(decoded1[i]));
    error1 += std::abs(int(source[i + 1]) - int(decoded1[i + 1]));
    error1 += std::abs(int(source[i + 2]) - int(decoded1[i + 2]));
  }
  CHECK_TRUE(error1 / (pixels * 3.0) < 10.0);
}

void TestBuildPlatformTexture() {
  BeginCase("BuildPlatformTexture writes the measured envelope and the swapped Xbox payload");
  const std::uint32_t width = 64;
  const std::uint32_t height = 64;
  const std::vector<std::uint8_t> source = MakeGradient(width, height);

  const std::string ps3 = BuildPlatformTexture(source.data(), width, height,
                                               TextureFormat::kDxt5, 0x11223344u,
                                               /*swap_words=*/false, 16);
  const std::string xbox = BuildPlatformTexture(source.data(), width, height,
                                                TextureFormat::kDxt5, 0x11223344u,
                                                /*swap_words=*/true, 16);
  CHECK_FALSE(ps3.empty());
  CHECK_EQ(ps3.size(), xbox.size());
  // 64..16 DXT5: 4096 + 1024 + 256 blocks' bytes.
  CHECK_EQ(ps3.size(), 32u + 4096u + 1024u + 256u);

  const auto* header = reinterpret_cast<const std::uint8_t*>(ps3.data());
  CHECK_EQ(header[0], 2);
  CHECK_EQ(header[1], 0x44);
  CHECK_EQ(header[2], 0x33);
  CHECK_EQ(header[3], 0x22);
  CHECK_EQ(header[4], 0x11);
  CHECK_EQ(header[5], 0x08);
  CHECK_EQ(header[6], 0x18);  // 0x1808 little-endian = DXT5
  CHECK_EQ(header[7], 0);
  CHECK_EQ(header[8], 0);
  CHECK_EQ(header[9], 0);
  CHECK_EQ(header[10], 2);  // 64/32/16 = three levels
  CHECK_EQ(header[11], 64);
  CHECK_EQ(header[12], 0);
  CHECK_EQ(header[13], 64);
  CHECK_EQ(header[14], 0);
  CHECK_EQ(header[15], 64);  // (64 / 4) * 16 / 4
  CHECK_EQ(header[16], 0);
  for (std::size_t i = 17; i < 32; ++i) {
    CHECK_EQ(header[static_cast<std::uint8_t>(i)], 0);
  }

  // The twin: every 16-bit word of the payload swapped, the envelope untouched.
  CHECK_MEM_EQ(reinterpret_cast<const std::uint8_t*>(xbox.data()),
               reinterpret_cast<const std::uint8_t*>(ps3.data()), 32);
  bool all_swapped = true;
  for (std::size_t i = 32; i + 1 < ps3.size(); i += 2) {
    if (xbox[i] != ps3[i + 1] || xbox[i + 1] != ps3[i]) {
      all_swapped = false;
      break;
    }
  }
  CHECK_TRUE(all_swapped);

  // Decoding the Xbox twin gives back the same pixels as decoding the PS3 file.
  TextureEnvelope envelope;
  std::string why;
  std::vector<std::uint8_t> from_xbox;
  CHECK_TRUE(DecodePlatformTexture(reinterpret_cast<const std::uint8_t*>(xbox.data()), xbox.size(),
                                   /*swapped=*/true, &from_xbox, &envelope, &why));
  CHECK_EQ(envelope.width, 64);
  CHECK_EQ(envelope.height, 64);
  CHECK_EQ(envelope.mip_levels, 3);
  std::vector<std::uint8_t> from_ps3;
  CHECK_TRUE(DecodePlatformTexture(reinterpret_cast<const std::uint8_t*>(ps3.data()), ps3.size(),
                                   /*swapped=*/false, &from_ps3, &envelope, &why));
  CHECK_MEM_EQ(from_xbox.data(), from_ps3.data(), from_ps3.size());
  CHECK_EQ(from_xbox.size(), 64u * 64u * 4u);

  // Sizes that are not whole DXT blocks are refused rather than silently truncated.
  CHECK_TRUE(BuildPlatformTexture(source.data(), 62, 64, TextureFormat::kDxt5, 1, true).empty());
}

void TestResize() {
  BeginCase("the resampler preserves a flat field and averages a split one");
  std::vector<std::uint8_t> flat(8 * 8 * 4);
  for (std::size_t i = 0; i < flat.size(); i += 4) {
    flat[i] = 40;
    flat[i + 1] = 80;
    flat[i + 2] = 120;
    flat[i + 3] = 255;
  }
  std::vector<std::uint8_t> small(4 * 4 * 4);
  rb_blitz::assets::ResizeRgba(flat.data(), 8, 8, small.data(), 4, 4);
  CHECK_MEM_EQ(small.data(), flat.data(), 4 * 4 * 4);

  std::vector<std::uint8_t> split(8 * 8 * 4, 255);
  for (std::uint32_t y = 0; y < 8; ++y) {
    for (std::uint32_t x = 0; x < 4; ++x) {
      std::uint8_t* p = split.data() + (std::size_t(y) * 8 + x) * 4;
      p[0] = p[1] = p[2] = 0;
    }
  }
  std::vector<std::uint8_t> halves(8 * 8 * 4);
  rb_blitz::assets::ResizeRgba(split.data(), 8, 8, halves.data(), 2, 2);
  // The left half averages to black, the right half to white.
  CHECK_EQ(halves[0], 0);
  CHECK_EQ(halves[4], 255);
}

void TestArtKeys() {
  BeginCase("art keys are stable 16-hex names and their path round-trips");
  rb_blitz::store::ResetAlbumArtForTests();
  const std::string key = rb_blitz::store::ArtKeyForSong("5b947da4929df7.93833456", "Artist", "");
  const std::string again = rb_blitz::store::ArtKeyForSong("5b947da4929df7.93833456", "Other", "");
  CHECK_STR_EQ(key, again);  // the id wins over the names
  CHECK_EQ(key.size(), 16u);
  const std::string by_name = rb_blitz::store::ArtKeyForSong("", "Artist", "Title");
  CHECK_EQ(by_name.size(), 16u);
  CHECK_FALSE(by_name == key);
  CHECK_TRUE(rb_blitz::store::ArtKeyForSong("", "", "").empty());

  const std::string path = rb_blitz::store::ArtPathForKey(key);
  CHECK_STR_EQ(path, "/offers/" + key + "/cover_art.png_xbox");
  CHECK_STR_EQ(rb_blitz::store::ArtKeyFromPath(path), key);
  // The panel composes its request prefix in front of the value; that prefix is measured as
  // `dlc_store` with no trailing slash, which is what the value's leading slash joins onto.
  CHECK_STR_EQ(rb_blitz::store::ArtKeyFromPath("dlc_store" + path), key);
  CHECK_STR_EQ(rb_blitz::store::ArtKeyFromPath("dlc_store/europe" + path), key);
  CHECK_TRUE(rb_blitz::store::IsArtRequestPath("dlc_store" + path));

  CHECK_TRUE(rb_blitz::store::ArtPathForKey("not-a-key").empty());
  CHECK_STR_EQ(rb_blitz::store::ArtKeyFromPath("offers/" + key + "/other.png_xbox"), "");
  CHECK_STR_EQ(rb_blitz::store::ArtKeyFromPath("art/" + key + "/cover_art.png_xbox"), "");
  CHECK_STR_EQ(rb_blitz::store::ArtKeyFromPath("offers/XYZ/cover_art.png_xbox"), "");
  CHECK_STR_EQ(rb_blitz::store::ArtKeyFromPath("dlc_store/europe/dlc_top_xbox_eng.dta"), "");
}

void TestCache() {
  BeginCase("the art cache publishes and answers a texture per key");
  rb_blitz::store::ResetAlbumArtForTests();
  CHECK_EQ(rb_blitz::store::ArtTextureCount(), 0u);
  CHECK_EQ(rb_blitz::store::ArtUrlCount(), 0u);

  rb_blitz::store::PublishArtUrl("0123456789abcdef", "https://rhythmverse.co/a.png");
  CHECK_EQ(rb_blitz::store::ArtUrlCount(), 1u);
  CHECK_STR_EQ(rb_blitz::store::ArtUrlForKey("0123456789abcdef"),
               "https://rhythmverse.co/a.png");
  CHECK_STR_EQ(rb_blitz::store::ArtUrlForKey("fedcba9876543210"), "");

  CHECK_FALSE(rb_blitz::store::HasArtTexture("0123456789abcdef"));
  CHECK_STR_EQ(rb_blitz::store::ArtTextureForKey("0123456789abcdef"), "");
  rb_blitz::store::PublishArtTexture("0123456789abcdef", std::string(64, 'X'));
  CHECK_TRUE(rb_blitz::store::HasArtTexture("0123456789abcdef"));
  CHECK_EQ(rb_blitz::store::ArtTextureForKey("0123456789abcdef").size(), 64u);
  CHECK_EQ(rb_blitz::store::ArtTextureCount(), 1u);

  // Empty values are not published (an empty texture would answer "served" with no bytes).
  rb_blitz::store::PublishArtTexture("1111111111111111", "");
  CHECK_EQ(rb_blitz::store::ArtTextureCount(), 1u);
  rb_blitz::store::PublishArtUrl("2222222222222222", "");
  CHECK_EQ(rb_blitz::store::ArtUrlCount(), 1u);

  rb_blitz::store::ResetAlbumArtForTests();
  CHECK_EQ(rb_blitz::store::ArtTextureCount(), 0u);
  CHECK_EQ(rb_blitz::store::ArtUrlCount(), 0u);
}

void TestSelectedArtBridge() {
  BeginCase("selected covers become ready independently after the page was listed");
  using namespace rb_blitz::store;
  ResetAlbumArtForTests();
  const auto first = ArtPathForKey("0123456789abcdef");
  const auto second = ArtPathForKey("fedcba9876543210");
  bool ready = true;
  std::vector<std::string> requested;
  SetArtTextureRequest([&](std::string_view key) { requested.emplace_back(key); });
  CHECK_TRUE(HandleArtWrite(std::string(kArtRequestWritePrefix) + second, true));
  CHECK_EQ(requested.size(), 1u);
  CHECK_STR_EQ(requested.back(), "fedcba9876543210");
  CHECK_TRUE(AnswerArtQuery(std::string(kArtReadyQueryPrefix) + second, &ready));
  CHECK_FALSE(ready);
  PublishArtTexture("0123456789abcdef", "first cover");
  CHECK_TRUE(AnswerArtQuery(std::string(kArtReadyQueryPrefix) + second, &ready));
  CHECK_FALSE(ready);  // the first cover cannot satisfy another song's request
  PublishArtTexture("fedcba9876543210", "second cover");
  CHECK_TRUE(AnswerArtQuery(std::string(kArtReadyQueryPrefix) + second, &ready));
  CHECK_TRUE(ready);  // no page reload or new selection required
  CHECK_TRUE(AnswerArtQuery(std::string(kArtReadyQueryPrefix) + first, &ready));
  CHECK_TRUE(ready);
  CHECK_FALSE(AnswerArtQuery("rbbz-store/search/show", &ready));
  CHECK_TRUE(AnswerArtQuery(std::string(kArtReadyQueryPrefix) + second + "/bad", &ready));
  CHECK_FALSE(ready);
  CHECK_TRUE(HandleArtWrite(std::string(kArtRequestWritePrefix) + "bad", true));
  CHECK_TRUE(HandleArtWrite(std::string(kArtRequestWritePrefix) + ArtPathForKey("1111111111111111"), false));
  CHECK_EQ(requested.size(), 1u);
  SetAlbumArtEnabled(false);
  CHECK_TRUE(AnswerArtQuery(std::string(kArtReadyQueryPrefix) + first, &ready));
  CHECK_FALSE(ready);
  ResetAlbumArtForTests();
}

}  // namespace

int main() {
  TestShippedEnvelopes();
  TestEnvelopeRefusals();
  TestCodecRoundTrip();
  TestBuildPlatformTexture();
  TestResize();
  TestArtKeys();
  TestCache();
  TestSelectedArtBridge();
  return rb_blitz::test::Finish();
}
