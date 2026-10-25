// SPDX-License-Identifier: GPL-2.0-only
#include "platform/virtual_keyboard.h"

#include <rex/logging.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <InputPaneInterop.h>
#include <roapi.h>
#include <windows.ui.viewmanagement.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#endif

namespace rb_blitz::platform {
namespace {
bool SetKeyboardVisible(void* native_window, bool visible) {
  if (!native_window) return false;
#ifdef _WIN32
  // Balance only our initialization. SDL may already own an apartment with a
  // different model; RPC_E_CHANGED_MODE leaves that existing apartment intact.
  struct Apartment {
    HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
    ~Apartment() { if (SUCCEEDED(result)) RoUninitialize(); }
  } apartment;
  if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return false;
  Microsoft::WRL::ComPtr<IInputPaneInterop> factory;
  const auto name = Microsoft::WRL::Wrappers::HStringReference(
      L"Windows.UI.ViewManagement.InputPane");
  HRESULT result = RoGetActivationFactory(name.Get(), __uuidof(IInputPaneInterop),
      reinterpret_cast<void**>(factory.GetAddressOf()));
  Microsoft::WRL::ComPtr<ABI::Windows::UI::ViewManagement::IInputPane2> pane;
  if (SUCCEEDED(result)) {
    result = factory->GetForWindow(static_cast<HWND>(native_window),
        __uuidof(ABI::Windows::UI::ViewManagement::IInputPane2),
        reinterpret_cast<void**>(pane.GetAddressOf()));
  }
  boolean accepted = false;
  if (SUCCEEDED(result)) result = visible ? pane->TryShow(&accepted) : pane->TryHide(&accepted);
  if (visible) {
    REXLOG_INFO("keyboard: Windows input pane request accepted={} hr={:08X}",
                accepted != 0, static_cast<unsigned long>(result));
  }
  return SUCCEEDED(result) && accepted;
#else
  if (visible) REXLOG_WARN("keyboard: OS input pane request is unavailable on this platform");
  return false;
#endif
}
}  // namespace

bool RequestVirtualKeyboard(void* native_window) { return SetKeyboardVisible(native_window, true); }
void DismissVirtualKeyboard(void* native_window) { SetKeyboardVisible(native_window, false); }
}  // namespace rb_blitz::platform
