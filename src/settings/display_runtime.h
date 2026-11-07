// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace rex::ui {
class Window;
class WindowedAppContext;
}

namespace rb_blitz::settings {

// Applies the display token and its two cvars as one runtime operation. The
// caller may be a guest thread; SDL and cvar work runs on the host UI thread.
// A successful return confirms native window flags and the cvar readback.
bool ApplyRuntimeDisplay(rex::ui::WindowedAppContext& context, rex::ui::Window& window,
                         std::string_view token, std::string* error);

// The marker is written before a preview changes the native window. Boot must
// read it before selecting Display, use previous as the safe choice, and clear
// it only after that choice has actually presented. A missing marker sets
// pending to false. Invalid markers are left untouched and reported.
bool ReadUnconfirmedDisplayPreview(const std::filesystem::path& settings_file,
                                   std::string* previous, bool* pending, std::string* error);
bool ClearUnconfirmedDisplayPreview(const std::filesystem::path& settings_file,
                                    std::string* error);

// Guest UI owns the confirmation. Begin does not persist the requested value;
// Keep requires a real presentation acknowledgement and a durable save callback.
// Timeout runs from a steady-clock worker even if the guest stops presenting,
// and routes the native rollback through the host UI thread. The context and
// window must outlive this transaction.
class DisplayPreviewTransaction {
 public:
  using Persist = std::function<bool(std::string*)>;
  DisplayPreviewTransaction(rex::ui::WindowedAppContext& context, rex::ui::Window& window,
                            std::filesystem::path settings_file,
                            std::chrono::steady_clock::duration timeout = std::chrono::seconds(15));
  ~DisplayPreviewTransaction();
  DisplayPreviewTransaction(const DisplayPreviewTransaction&) = delete;
  DisplayPreviewTransaction& operator=(const DisplayPreviewTransaction&) = delete;

  bool Begin(std::string_view previous, std::string_view requested, std::string* error);
  bool Keep(bool presented, const Persist& persist, std::string* error);
  bool Revert(std::string* error);
  bool active() const;
  std::chrono::steady_clock::time_point deadline() const;

 private:
  struct State;
  static bool RevertState(const std::shared_ptr<State>& state, uint64_t generation,
                          std::string* error);
  std::shared_ptr<State> state_;
  std::jthread timeout_;
  std::chrono::steady_clock::duration duration_;
};

}  // namespace rb_blitz::settings
