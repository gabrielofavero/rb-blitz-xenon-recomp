// SPDX-License-Identifier: GPL-2.0-only
#include "ui/apple_font.h"

#include <CoreText/CoreText.h>
#include <limits.h>

namespace rb_blitz::ui {
ImFont* LoadAppleFont(ImFontAtlas* atlas, float size,
                      std::span<const char* const> names,
                      const ImWchar* ranges) {
  if (!atlas) return nullptr;
  for (const auto* name : names) {
    const auto requested = CFStringCreateWithCString(nullptr, name, kCFStringEncodingUTF8);
    const auto font = CTFontCreateWithName(requested, size, nullptr);
    const auto actual = CTFontCopyPostScriptName(font);
    // CoreText substitutes a default face for unknown names. Keep trying our
    // candidates instead, so a missing bold face cannot silently become regular.
    const bool matches = CFEqual(requested, actual);
    CFRelease(actual);
    const auto url = matches ? CTFontCopyAttribute(font, kCTFontURLAttribute) : nullptr;
    CFRelease(font);
    ImFont* loaded = nullptr;
    if (url && CFGetTypeID(url) == CFURLGetTypeID()) {
      UInt8 path[PATH_MAX];
      if (CFURLGetFileSystemRepresentation(static_cast<CFURLRef>(url), true,
                                           path, sizeof(path))) {
        // Collections may put bold/italic before regular. Select the named face
        // rather than always baking the first font in a TTC.
        const auto descriptors = CTFontManagerCreateFontDescriptorsFromURL(
            static_cast<CFURLRef>(url));
        if (descriptors) {
          for (CFIndex i = 0; i < CFArrayGetCount(descriptors); ++i) {
            const auto descriptor = static_cast<CTFontDescriptorRef>(
                CFArrayGetValueAtIndex(descriptors, i));
            const auto face = CTFontDescriptorCopyAttribute(descriptor, kCTFontNameAttribute);
            const bool found = face && CFEqual(face, requested);
            if (face) CFRelease(face);
            if (!found) continue;
            ImFontConfig config;
            config.FontNo = static_cast<int>(i);
            config.Flags |= ImFontFlags_NoLoadError;
            loaded = atlas->AddFontFromFileTTF(reinterpret_cast<const char*>(path),
                                               size, &config, ranges);
            break;
          }
          CFRelease(descriptors);
        }
      }
    }
    if (url) CFRelease(url);
    CFRelease(requested);
    if (loaded) return loaded;
  }
  return nullptr;
}
}  // namespace rb_blitz::ui
