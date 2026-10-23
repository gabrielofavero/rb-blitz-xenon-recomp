// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::input {

// A system dialog owns every device. After it closes, consume the closing press
// through its release so Enter/Escape/click cannot also act on the underlying UI.
class ModalInputGate {
 public:
  bool Block(bool modal, bool held) {
    if (modal) waiting_for_release_ = true;
    const bool blocked = modal || waiting_for_release_;
    if (!modal && !held) waiting_for_release_ = false;
    return blocked;
  }

 private:
  bool waiting_for_release_ = false;
};

}  // namespace rb_blitz::input
