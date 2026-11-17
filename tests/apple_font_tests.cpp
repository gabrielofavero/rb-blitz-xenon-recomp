// SPDX-License-Identifier: GPL-2.0-only
#include "ui/apple_font.h"
#include "ui/help_layout.h"

#include <cfloat>
#include <cstdio>

int main() {
  ImGui::CreateContext();
  auto* atlas = ImGui::GetIO().Fonts;
  const char* missing[] = {"RBBlitz-NonexistentFont"};
  const char* regular[] = {"RBBlitz-NonexistentFont", "ArialMT", "Helvetica"};
  const char* bold[] = {"Arial-BoldMT", "Helvetica-Bold"};
  // Helvetica exercises a system TTC as well as Arial's standalone TTF.
  const char* collection[] = {"Helvetica"};
  const bool refused = !rb_blitz::ui::LoadAppleFont(atlas, 96, missing);
  auto* caption = rb_blitz::ui::LoadAppleFont(atlas, 96, regular);
  auto* heading = rb_blitz::ui::LoadAppleFont(atlas, 64, bold);
  auto* ttc = rb_blitz::ui::LoadAppleFont(atlas, 64, collection);
  if (!refused || !caption || !heading || !ttc) {
    std::fprintf(stderr, "Apple font lookup failed or accepted a substituted face\n");
    ImGui::DestroyContext();
    return 1;
  }
  unsigned char* pixels;
  int width, height;
  atlas->GetTexDataAsAlpha8(&pixels, &width, &height);
  bool ok = pixels && width > 0 && height > 0;
  for (auto* font : {caption, heading, ttc}) {
    // A real proportional sans must survive the legacy atlas bake and measure
    // differently from the monospaced debug face seen in the regression.
    const auto narrow = font->CalcTextSizeA(24, FLT_MAX, 0, "iiii").x;
    const auto wide = font->CalcTextSizeA(24, FLT_MAX, 0, "WWWW").x;
    ok &= narrow > 0 && wide > narrow * 2;
    std::printf("%s: iiii=%.2f, WWWW=%.2f\n", font->GetDebugName(), narrow, wide);
  }
  // Measure mobile captions with the actual Apple sans face at every supported size.
  for (const auto viewport : {ImVec2(874, 402), ImVec2(844, 390), ImVec2(1024, 768), ImVec2(640, 480)}) {
    for (const char* size : {"normal", "big", "biggest"}) {
      const auto layout = rb_blitz::ui::help::LayoutCaption(viewport.x, viewport.y, size);
      for (const char* text : {
          "Normal follows your swipe; Inverted reverses it.",
          "Tilt away or shake lightly; None disables motion.",
          "Show or hide Exit Game. Restart to apply."}) {
        const auto bounds = caption->CalcTextSizeA(layout.font_size, FLT_MAX, layout.TextWidth(), text);
        const bool fits = bounds.x <= layout.TextWidth() && bounds.y <= layout.TextHeight();
        if (!fits) std::fprintf(stderr, "Caption overflow at %.0fx%.0f %s: %s\n",
                                viewport.x, viewport.y, size, text);
        ok &= fits;
      }
    }
  }
  ImGui::DestroyContext();
  return ok ? 0 : 1;
}
