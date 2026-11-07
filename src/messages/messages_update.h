// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace rb_blitz::messages {

inline constexpr std::string_view kReleasesUrl =
    "https://github.com/gabrielofavero/rb-blitz-xenon-recomp/releases";

enum class OpenState { kIdle, kPending, kFailed, kOpened };

// The guest sends only a command. The opener runs on a host worker, and a successful handoff
// schedules the close after the browser has accepted the fixed URL.
class UpdateAction {
 public:
  using Opener = std::function<bool(std::string_view)>;
  using Close = std::function<bool()>;
  using Dispatch = std::function<void(std::function<void()>)>;

  UpdateAction(Opener opener, Close close, Dispatch dispatch);
  bool Press();
  OpenState state() const;

 private:
  void Run();
  Opener opener_;
  Close close_;
  Dispatch dispatch_;
  mutable std::mutex mutex_;
  OpenState state_ = OpenState::kIdle;
};

// Windows boot adapter and the two guest bridge routes.
void InstallUpdateAction(UpdateAction::Close close);
bool AnswerUpdateQuery(std::string_view path, bool* answer);
bool HandleUpdateWrite(std::string_view path);
std::string UpdateMessage();
}  // namespace rb_blitz::messages
