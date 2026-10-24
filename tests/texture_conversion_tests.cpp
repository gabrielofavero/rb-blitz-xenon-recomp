// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"

#include <array>
#include <cstring>
#include <rex/graphics/pipeline/texture/util.h>

namespace {
using namespace rb_blitz::test;
using rex::graphics::texture_util::ConvertNonFloatGuestRange;
using rex::graphics::texture_util::IsNonFloat32bpcFormat;
using rex::graphics::xenos::Endian;
using rex::graphics::xenos::GpuSwap;
using rex::graphics::xenos::TextureFormat;

void TestComponents(Endian endian, bool signed_data, bool integer) {
  const std::array<uint32_t, 5> words = {0, 1, 0x7fffffff, 0x80000000, 0xffffffff};
  std::array<uint32_t, 5> source;
  std::array<uint32_t, 7> destination;
  destination.fill(0xdeadbeef);
  for (size_t i = 0; i < words.size(); ++i) source[i] = GpuSwap(words[i], endian);
  ConvertNonFloatGuestRange(reinterpret_cast<const uint8_t*>(source.data()),
      reinterpret_cast<uint8_t*>(destination.data() + 1), sizeof(source),
      endian, signed_data, integer);
  CHECK_EQ(destination.front(), 0xdeadbeef);
  CHECK_EQ(destination.back(), 0xdeadbeef);
  // Explicit float32 boundary values: UNORM/SNORM endpoints and rounded 32-bit integers.
  const std::array<float, 5> expected = integer
      ? (signed_data ? std::array<float, 5>{0, 1, 2147483648.0f, -2147483648.0f, -1}
                     : std::array<float, 5>{0, 1, 2147483648.0f, 2147483648.0f, 4294967296.0f})
      : (signed_data ? std::array<float, 5>{0, 0x1p-31f, 1, -1, -0x1p-31f}
                     : std::array<float, 5>{0, 0x1p-32f, 0.5f, 0.5f, 1});
  for (size_t i = 0; i < words.size(); ++i) {
    const uint32_t bits = GpuSwap(destination[i + 1], endian);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    CHECK_TRUE(value == expected[i]);
    CHECK_EQ(source[i], GpuSwap(words[i], endian));
  }
}

void TestOpaqueAlpha() {
  BeginCase("RGBA32 fixed-point white stays opaque instead of becoming a null texture");
  const std::array<uint32_t, 4> source = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
  std::array<uint32_t, 4> converted = {};
  ConvertNonFloatGuestRange(reinterpret_cast<const uint8_t*>(source.data()),
      reinterpret_cast<uint8_t*>(converted.data()), sizeof(source), Endian::k8in32, false, false);
  for (const auto component : converted) CHECK_EQ(GpuSwap(component, Endian::k8in32), 0x3f800000);
}
}  // namespace

int main() {
  BeginCase("only non-floating 32-bit component formats need CPU conversion");
  CHECK_TRUE(IsNonFloat32bpcFormat(TextureFormat::k_32));
  CHECK_TRUE(IsNonFloat32bpcFormat(TextureFormat::k_32_32));
  CHECK_TRUE(IsNonFloat32bpcFormat(TextureFormat::k_32_32_32_32));
  CHECK_FALSE(IsNonFloat32bpcFormat(TextureFormat::k_32_32_32_32_FLOAT));
  CHECK_FALSE(IsNonFloat32bpcFormat(TextureFormat::k_8_8_8_8));
  BeginCase("signed and unsigned fractions and integers preserve all four guest byte orders");
  for (const auto endian : {Endian::kNone, Endian::k8in16, Endian::k8in32, Endian::k16in32}) {
    for (const bool signed_data : {false, true}) {
      for (const bool integer : {false, true}) TestComponents(endian, signed_data, integer);
    }
  }
  TestOpaqueAlpha();
  return Finish();
}
