// SPDX-License-Identifier: GPL-2.0-only
#include "messages/messages_update.h"

#include <exception>
#include <utility>

namespace rb_blitz::messages {

UpdateAction::UpdateAction(Opener opener, Close close, Dispatch dispatch)
    : opener_(std::move(opener)), close_(std::move(close)), dispatch_(std::move(dispatch)) {}

bool UpdateAction::Press() {
  {
    std::lock_guard lock(mutex_);
    if (state_ == OpenState::kPending || state_ == OpenState::kOpened) return false;
    state_ = OpenState::kPending;
  }
  try {
    dispatch_([this] { Run(); });
  } catch (...) {
    std::lock_guard lock(mutex_);
    state_ = OpenState::kFailed;
    return false;
  }
  return true;
}

OpenState UpdateAction::state() const {
  std::lock_guard lock(mutex_);
  return state_;
}

void UpdateAction::Run() {
  bool opened = false;
  try {
    opened = opener_(kReleasesUrl);
  } catch (...) {
    opened = false;
  }
  {
    std::lock_guard lock(mutex_);
    state_ = opened ? OpenState::kOpened : OpenState::kFailed;
  }
  if (opened) {
    try {
      close_();
    } catch (...) {
      // The browser has already accepted the URL. Do not allow a retry to open another tab.
    }
  }
}

}  // namespace rb_blitz::messages
