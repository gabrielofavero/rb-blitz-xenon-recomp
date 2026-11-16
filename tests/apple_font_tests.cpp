// SPDX-License-Identifier: GPL-2.0-only
#include "ui/apple_font.h"

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
  ImGui::DestroyContext();
  return ok ? 0 : 1;
}
