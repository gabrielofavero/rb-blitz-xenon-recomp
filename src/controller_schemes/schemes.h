// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::controller_schemes {

struct Image {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;
};
struct Rect { uint32_t x, y, width, height; };
struct Slot { const char* id; Rect rect; };
const std::vector<Slot>& ButtonSlots();
const std::vector<std::string>& PadSlots();

struct InputImage {
  std::string id, source, slot, variant, file;
  std::vector<std::string> components, modifiers;
  Image image;
};

struct Scheme {
  std::string id, name;
  int schema_version = 1;
  std::string kind = "controller";
  std::filesystem::path folder;
  std::map<std::string, Image> buttons;
  std::map<std::string, Image> pads;
  Image shared_pad, icons;
  bool has_shared_pad = false, has_icons = false;
  std::vector<std::filesystem::path> extra_files;
  std::vector<std::filesystem::path> declared_files;
  std::vector<InputImage> inputs;
  const Image* Pad(const std::string& slot) const;
  const InputImage* InputFor(std::string_view source,
                             std::string_view variant = {},
                             std::string_view modifier = {}) const;
};
struct Rejection { std::filesystem::path folder; std::string reason; };

// The image decoder is supplied by the host; parsing/composition remain SDK-free.
using DecodePng = bool (*)(const std::filesystem::path&, Image*, std::string*);
bool LoadScheme(const std::filesystem::path& folder, bool trusted_reserved,
                DecodePng decoder, Scheme* result, std::string* why);
bool LoadInternalKeyboard(const std::filesystem::path& folder, DecodePng decoder,
                          Scheme* result, std::string* why);
struct Registry {
  std::map<std::string, Scheme> schemes;
  Scheme keyboard_mouse;
  bool has_keyboard_mouse = false;
  std::vector<Rejection> rejected;
  std::vector<std::string> shadowed;
  const Scheme* Find(const std::string& id) const;
  std::string Summary() const;
};
Registry Discover(const std::filesystem::path& install_root,
                  const std::filesystem::path& user_root, DecodePng decoder);
bool DecodePngWic(const std::filesystem::path& path, Image* image, std::string* why);

// The whole source canvas is resized proportionally and centered on transparent pixels.
Image Fit(const Image& source, uint32_t width, uint32_t height);
// `original` is canonical linear BC3. Unaffected blocks are copied byte-for-byte.
bool ComposeButtons(const std::vector<std::vector<uint8_t>>& original,
                    const Scheme& scheme, std::vector<std::vector<uint8_t>>* levels);
std::vector<std::vector<uint8_t>> ComposePad(const Image& image, uint32_t mip_count);
std::vector<std::vector<uint8_t>> ComposeIcons(const Image& image, uint32_t mip_count);
bool UntileBc3Base(const uint8_t* guest, size_t guest_size, uint32_t width,
                   bool tiled, uint32_t endianness, std::vector<uint8_t>* canonical);

}  // namespace rb_blitz::controller_schemes
