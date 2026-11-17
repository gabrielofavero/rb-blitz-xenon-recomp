// SPDX-License-Identifier: GPL-2.0-only
// Exercise the real presenter/window state machines without an OS window or GPU.
#include "check.h"
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

namespace {
using namespace rex::ui;
class Context final : public WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};
class TestSurface final : public Surface {
 public:
  bool available = true;
  TypeIndex GetType() const override { return kTypeIndex_CAMetalLayer; }
 private:
  bool GetSizeImpl(uint32_t& width, uint32_t& height) const override {
    width = 405; height = 720; return available;
  }
};
class TestWindow final : public Window {
 public:
  explicit TestWindow(Context& context) : Window(context, "test", 405, 720) {}
  ~TestWindow() override { EnterDestructor(); }
  int requests = 0;
  TestSurface* surface = nullptr;
  void Paint() { OnPaint(true); }
  void Focus(bool focused) {
    WindowDestructionReceiver receiver(this);
    OnFocusUpdate(focused, receiver);
  }
 private:
  bool OpenImpl() override { return true; }
  void RequestCloseImpl() override {
    WindowDestructionReceiver receiver(this);
    OnBeforeClose(receiver); OnAfterClose();
  }
  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags) override {
    auto result = std::make_unique<TestSurface>(); surface = result.get(); return result;
  }
  void RequestPaintImpl() override { ++requests; }
};
class TestPresenter final : public Presenter {
 public:
  TestPresenter() : Presenter({}) {}
  int connections = 0, paints = 0;
  Surface::TypeFlags GetSupportedSurfaceTypes() const override { return Surface::kTypeFlag_CAMetalLayer; }
  bool CaptureGuestOutput(RawImage&) override { return false; }
 private:
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      Surface&, uint32_t, uint32_t, bool was_paintable, bool& vsync) override {
    vsync = false;
    if (was_paintable) return SurfacePaintConnectResult::kSuccessUnchanged;
    ++connections; return SurfacePaintConnectResult::kSuccess;
  }
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override {}
  bool RefreshGuestOutputImpl(uint32_t, uint32_t, uint32_t,
      std::function<bool(GuestOutputRefreshContext&)>, bool&) override { return true; }
  PaintResult PaintAndPresentImpl(bool) override { ++paints; return PaintResult::kPresented; }
};
class Drawer final : public UIDrawer {
  void Draw(UIDrawContext&) override {}
};
}

int main() {
  using namespace rb_blitz::test;
  Context context;
  TestPresenter presenter;
  TestWindow window(context);
  Drawer drawer;
  CHECK_TRUE(window.Open());
  window.SetPresenter(&presenter);
  presenter.AddUIDrawerFromUIThread(&drawer, 0);
  window.Paint();
  const auto refresh = [&] { CHECK_TRUE(presenter.RefreshGuestOutput(1, 1, 1, 1, {})); };

  BeginCase("skipped occluded paint permits a later guest frame to queue another event");
  int requests = window.requests;
  refresh();
  CHECK_EQ(window.requests, requests + 1);
  refresh();
  CHECK_EQ(window.requests, requests + 1); // coalesce while the event is still queued
  presenter.OnPaintSkippedFromUIThread();
  refresh();
  CHECK_EQ(window.requests, requests + 2);
  window.Paint();

  BeginCase("same-size Mac focus gains repaint without replacing the swapchain");
  requests = window.requests;
  const int connections = presenter.connections;
  window.Focus(true);
  CHECK_TRUE(window.requests > requests);
  requests = window.requests;
  window.Focus(true); // repeated gain also wakes a consumed paint
  CHECK_TRUE(window.requests > requests);
  CHECK_EQ(presenter.connections, connections);

  BeginCase("expose retries a surface that was unavailable without requiring a resize");
  window.surface->available = false;
  presenter.OnSurfaceResizeFromUIThread();
  window.surface->available = true;
  const int paints = presenter.paints;
  window.Paint();
  CHECK_EQ(presenter.connections, connections + 1);
  CHECK_EQ(presenter.paints, paints + 1);
  presenter.RemoveUIDrawerFromUIThread(&drawer);
  window.SetPresenter(nullptr);
  return Finish();
}
