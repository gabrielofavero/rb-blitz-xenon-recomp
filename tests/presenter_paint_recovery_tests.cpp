// SPDX-License-Identifier: GPL-2.0-only
// Exercise the real presenter state machine without an OS window or GPU.
#include "check.h"
#include "gpu/xenos/host/presenter.h"
#include "gpu/xenos/xenos_graphics_driver_base.h"
#include "gpu/xenos/command_processor.h"

namespace {
using namespace rex::gpu;
class TestDriver final : public XenosGraphicsDriverBase {
 public:
  std::string_view GetName() const override { return "test"; }
 private:
  void CreateProvider(bool) override {}
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override { return {}; }
};
class TestSurface final : public Surface {
 public:
  bool available = true;
  TypeIndex GetType() const override { return kTypeIndexCAMetalLayer; }
 private:
  bool ReadSize(rex::u32& width, rex::u32& height) const override {
    width = 405; height = 720; return available;
  }
};
class Host final : public IPresentationHost {
 public:
  int requests = 0;
  void RequestPaint() override { ++requests; }
  void OnPresenterDetached() override {}
};
class TestPresenter final : public Presenter {
 public:
  explicit TestPresenter(const TestDriver& driver) : Presenter({}, driver) {
    InitializeCommonSurfaceIndependent();
  }
  int connections = 0, paints = 0;
  Surface::TypeFlags GetSupportedSurfaceTypes() const override { return Surface::kTypeFlagCAMetalLayer; }
 private:
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      Surface&, rex::u32, rex::u32, bool was_paintable, bool& vsync) override {
    vsync = false;
    if (was_paintable) return SurfacePaintConnectResult::kSuccessUnchanged;
    ++connections; return SurfacePaintConnectResult::kSuccess;
  }
  void DriverDisconnectPaintingFromSurface() override {}
  bool DriverRefreshGuestOutput(rex::u32, rex::u32, rex::u32,
      rex::MoveOnlyFunction<bool(GuestOutputRefreshContext&)>, bool&) override { return true; }
  PaintResult DriverPaintAndPresent(bool) override { ++paints; return PaintResult::kPresented; }
};
class Layer final : public IOverlayLayer {
  void Draw(OverlayDrawContext&) override {}
};
}

int main() {
  using namespace rb_blitz::test;
  TestDriver driver;
  TestPresenter presenter(driver);
  TestSurface surface;
  Host host;
  Layer layer;
  CHECK_TRUE(presenter.Attach(&host, &surface));
  presenter.AddOverlayLayer(&layer, 0);
  presenter.Paint(true);
  const auto refresh = [&] { CHECK_TRUE(presenter.RefreshGuestOutput(1, 1, 1, 1, {})); };

  BeginCase("continuous forced paint resumes after skipped occluded frames");
  const int requests = host.requests;
  refresh();
  CHECK_EQ(host.requests, requests + 1);
  refresh();
  CHECK_EQ(host.requests, requests + 1);
  const int before_paint = presenter.paints;
  // The present loop skips unavailable drawables, then forces a paint when visible.
  presenter.Paint(true);
  CHECK_EQ(presenter.paints, before_paint + 1);
  refresh();
  CHECK_EQ(host.requests, requests + 2);
  presenter.Paint(true);

  BeginCase("same-size Apple surfaces keep their presentation connection");
  const int connections = presenter.connections;
  presenter.OnSurfaceResize();
  presenter.Paint(true);
  CHECK_EQ(presenter.connections, connections);

  BeginCase("expose retries an unavailable surface without a size change");
  surface.available = false;
  presenter.OnSurfaceResize();
  surface.available = true;
  const int paints = presenter.paints;
  presenter.Paint(true);
  CHECK_EQ(presenter.connections, connections + 1);
  CHECK_EQ(presenter.paints, paints + 1);
  presenter.RemoveOverlayLayer(&layer);
  presenter.Attach(nullptr, nullptr);
  return Finish();
}
