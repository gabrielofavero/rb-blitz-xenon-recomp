// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The engine's platform texture container (`<name>.png_xbox` / `.png_ps3`) and the DXT blocks
// inside it. This is the decoder/encoder the store's album art needs (RhythmVerse store plan
// §5/P3) and the one the button-layout plan reuses for its PS3 import and its slot sheets.
//
// ## The envelope - measured 2026-10-21
//
// Every shipped `.png_xbox` under `assets/game/360` (201 files) was read byte by byte. The first
// 32 bytes are an envelope, everything after it is the mip chain, largest first:
//
//     [0]        envelope version (2 in all but three of the shipped files; this codec writes and
//                writes version 2; version-1 decoding uses the same signature at offset 1,
//                without the ID, as measured on local single-song and merged-pack covers)
//     [1..4]     per-asset 32-bit id (version 2 only). Not a checksum: files with identical
//                pixels carry different ids, so it identifies the asset. Kept, never interpreted
//     [5..6]     format word, little-endian: 0x0804 DXT1, 0x1808 DXT5, 0x2008 two-channel normal
//     [7..9]     zero (the high byte of the format word is repeated at [6] - retail writes it)
//     [10]       mip levels - 1
//     [11..12]   width,  little-endian u16
//     [13..14]   height, little-endian u16
//     [15..16]   the largest mip's row pitch in bytes / 4, little-endian u16
//     [17..31]   zero
//
// The pitch field is what pins the reading: `ui/image/gen/default_album_art_keep.png_xbox` is
// 512x512 DXT1 with 6 mips and carries 256 there ((512/4)*8/4 = 256); the 256x256 DXT1 song covers
// carry 128; the 64x64 `ui_picture_keep` carries 32; the 256x256 DXT5 `highlight_keep` carries 256
// ((256/4)*16/4). The engine's loader reads exactly these three fields as `height x pitch/4`, which
// is the largest mip's byte count - and the file sizes agree with that to the byte
// (`default_album_art_keep` = 32 + 512x512..16x16 DXT1 = 174,752).
//
// The payload of the **Xbox** twin is the PS3 payload with every 16-bit word byte-swapped. The
// envelope itself is identical on both platforms (the PS3 `default_album_art_keep.png_ps3` differs
// only in its 4-byte id), which is why `SwapSixteenBitWords` is applied to the blocks and never to
// the header.
//
// ## Which format the engine actually draws - measured 2026-10-21
//
// **DXT1.** A DXT5 this module encodes is a correct BC3 - unswapped and decoded it matches its source
// to 0.8 levels per channel, and `scripts/assets/hmx_tex.py export` renders it exactly - but the store's
// texture path draws it as noise with the right shapes and wrong colours, while a shipped DXT1
// served through the same seam draws correctly and the same cover as DXT1 draws correctly too
// (`docs/reference/known-issues.md` carries the evidence paths; `docs/guides/assets.md`,
// "which formats this build actually draws"). So: generate DXT1 unless alpha is really needed, and
// treat a DXT5 that draws wrongly as an engine-path loss, not a codec bug, until the bytes have been
// decoded outside the game.
//
// ## Why this module is SDK-free
//
// It is pure bytes: no rex headers, no guest memory, no Windows. `tests/store_art_tests.cpp` pins
// the envelope, the block codec and the resampler without a boot, and the installer (a separate
// target) can compile the same translation unit for its PS3 import.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rb_blitz::assets {

// The envelope is 32 bytes in every version the engine writes.
inline constexpr std::size_t kTextureEnvelopeBytes = 32;

enum class TextureFormat : std::uint16_t {
  kDxt1 = 0x0804,  // 4 bpp, optional 1-bit alpha
  kDxt5 = 0x1808,  // 8 bpp, 8-bit alpha
};

// One parsed envelope. The caller's own struct rather than a reference to a file, so the same
// reader serves the codec's tests and the PS3-import tooling.
struct TextureEnvelope {
  std::uint8_t version = 2;
  std::uint32_t id = 0;  // version 2 only, opaque
  TextureFormat format = TextureFormat::kDxt5;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t mip_levels = 1;
  std::uint32_t pitch_dwords = 0;  // the field at [15..16]; see the header comment
};

// Bytes one DXT block occupies: 8 for DXT1, 16 for DXT5.
std::size_t BlockBytes(TextureFormat format);

// The largest mip's row pitch in bytes (blocks per row x block bytes).
std::size_t RowPitchBytes(TextureFormat format, std::uint32_t width);

// The byte size of mip `level` (0 = the largest).
std::size_t MipLevelBytes(TextureFormat format, std::uint32_t width, std::uint32_t height,
                          std::uint32_t level);

// The byte size of `levels` mips starting at the largest.
std::size_t MipChainBytes(TextureFormat format, std::uint32_t width, std::uint32_t height,
                          std::uint32_t levels);

// True when `data` carries a complete envelope plus the mip chain its own header describes.
// `why` receives the reason a file is refused (an unknown version or format word, a dimension of
// zero, a mip count the dimensions cannot carry, or a size that disagrees with the header).
bool ParseTextureEnvelope(const std::uint8_t* data, std::size_t size, TextureEnvelope* envelope,
                          std::string* why);

// In-place byte swap of every 16-bit word in `[data, data + size)`. An odd trailing byte is left
// alone, which never happens in a well-formed file (every DXT block is a multiple of 2 bytes).
void SwapSixteenBitWords(std::uint8_t* data, std::size_t size);

// --- DXT blocks (PC order) -------------------------------------------------------------------
//
// Decoding follows the DXT specification exactly; the caller sizes `rgba` as width * height * 4
// and the functions always write whole 4x4 blocks, clamping at the right/bottom edges of a texture
// whose size is not a multiple of 4.
void DecodeDxt1(const std::uint8_t* blocks, std::uint32_t width, std::uint32_t height,
                std::uint8_t* rgba);
void DecodeDxt5(const std::uint8_t* blocks, std::uint32_t width, std::uint32_t height,
                std::uint8_t* rgba);

// Encoding is the bounding-box fit: the block's per-channel minimum and maximum become the two
// endpoints and each pixel takes the nearest of the interpolated colours (and, for DXT5, the
// nearest of the eight alpha steps). It is not a rate-distortion optimiser, and it does not need to
// be: the art it encodes is a 256-pixel cover drawn at a fraction of that size, and the same codec
// is what the texture tooling uses to build sheets where the source is already flat colour.
// `width` and `height` must be multiples of 4; `blocks` receives the exact block bytes.
void EncodeDxt1(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t>* blocks);
void EncodeDxt5(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                std::vector<std::uint8_t>* blocks);

// --- Whole files -----------------------------------------------------------------------------

// A box-filtered resample to `dst_width` x `dst_height` RGBA8, used to bring an arbitrary album
// art size onto a power-of-two texture. Downscaling averages the source rectangle each destination
// pixel covers (fractional edges included); upscaling is a bilinear sample at the same centres.
void ResizeRgba(const std::uint8_t* src, std::uint32_t src_width, std::uint32_t src_height,
                std::uint8_t* dst, std::uint32_t dst_width, std::uint32_t dst_height);

// Builds a complete `.png_xbox` (or `.png_ps3`) file from RGBA8: the envelope above, then a full
// mip chain generated by `ResizeRgba` down to `min_level_size` (inclusive), with the 16-bit swap
// applied to the blocks when `swap_words` is set. Returns an empty string when the dimensions are
// not multiples of 4 or the copy cannot be built.
//
// The largest mip is encoded at `MipLevelBytes(...)` so the file's pitch field is the one the
// engine's loader multiplies by the height.
std::string BuildPlatformTexture(const std::uint8_t* rgba, std::uint32_t width,
                                 std::uint32_t height, TextureFormat format, std::uint32_t id,
                                 bool swap_words, std::uint32_t min_level_size = 16);

// The inverse of `BuildPlatformTexture` for one level: validates the envelope and decodes the
// largest mip into RGBA8. `swapped` says whether the payload is the Xbox twin.
bool DecodePlatformTexture(const std::uint8_t* data, std::size_t size, bool swapped,
                           std::vector<std::uint8_t>* rgba, TextureEnvelope* envelope,
                           std::string* why);

}  // namespace rb_blitz::assets
