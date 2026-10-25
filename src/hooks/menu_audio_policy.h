// SPDX-License-Identifier: GPL-2.0-only
#pragma once
namespace rb_blitz::shell_music {
// StandardStream's f2 is seconds of decoded stream capacity, not device latency.
// Zero means the title's stream_buf_size default (1.2 s on the supported image).
inline double StreamBufferSeconds(bool menu_construction, double requested) {
  return menu_construction && requested >= 0.0 && requested < 6.0 ? 6.0 : requested;
}
}
