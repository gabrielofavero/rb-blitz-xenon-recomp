// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_audio_policy.h"
#include "hooks/audio_stream_policy.h"
#include <rex/app/app.h>
#include <rex/apu/audio_system.h>
#include <rex/cli.h>
#include <rex/driver_module.h>
#include <rex/driver_registration.h>
#include <rex/ui/ui_system.h>
#include <rex/hid/input_system.h>
#include <rex/gpu/graphics_system.h>
#include "gpu/xenos/preparation_budget.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace {
rex::apu::AudioFill fill_audio;
class ManualDriver final : public rex::apu::IAudioDriver {
 public:
  std::string_view GetName() const override { return "manual-test"; }
  rex::Result<void> Initialize(rex::IDriverContext&) override { return {}; }
  void Shutdown() override {}
  rex::Result<rex::u32> OpenDevice(rex::u32 channels, rex::apu::AudioFill fill) override {
    fill_audio = std::move(fill);
    return channels;
  }
  void CloseDevice() override { fill_audio = nullptr; }
  bool IsDeviceLost() const override { return false; }
};
std::unique_ptr<rex::IDriver> CreateManualDriver() { return std::make_unique<ManualDriver>(); }
class ManualProvider final : public rex::IModule, public rex::IDriverModule {
 public:
  std::string_view GetName() const override { return "manual-audio-test"; }
  rex::Result<void> Connect(rex::IInterfaceFactory&) override { return {}; }
  void Disconnect() override {}
  rex::Result<void> Init() override { return {}; }
  void Shutdown() override {}
  rex::IInterface* QueryInterface(std::string_view name, rex::u32 version) override {
    return rex::FindModuleInterface<rex::IDriverModule>(*this, name, version);
  }
  std::span<const rex::DriverRegistration> GetDrivers() const override { return drivers; }
 private:
  static constexpr std::array drivers{rex::DriverRegistration{
      rex::DriverType::kAudio, "manual-test", {}, &CreateManualDriver}};
};
class AudioApp final : public rex::app::App {
 public:
  AudioApp() : App("audio_stability_test") {}
 protected:
  void OnRegisterModules(std::vector<rex::IModule*>& modules) override {
    App::OnRegisterModules(modules);
    modules.push_back(&provider_);
  }
  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    REX_TRY_VOID(root.Set(std::filesystem::current_path()));
    REX_TRY_VOID(GetUI().GetSettings().driver.Set("none"));
    REX_TRY_VOID(GetInput().GetSettings().drivers.Set("none"));
    REX_TRY_VOID(GetGraphics().GetSettings().driver.Set("none"));
    REX_TRY_VOID(GetAudio().GetSettings().driver.Set("manual-test"));
    REX_TRY_VOID(GetAudio().GetSettings().max_queued_frames.Set(8));
    return registry.SetText(rex::SettingLayer::kTitle, "log.file", "false");
  }
 private:
  ManualProvider provider_;
};
using Frame = std::array<float, rex::apu::kAudioFrameSamples>;
// Every channel and period has a distinct finite sample inside the mixer's range.
float Sample(unsigned period, unsigned channel, unsigned sample) {
  return float(period * 1536 + channel * 256 + sample + 1) / 33554432.0f;
}
Frame MakeFrame(unsigned period) {
  Frame frame;
  for (unsigned c = 0; c < 6; ++c)
    for (unsigned i = 0; i < 256; ++i) frame[c * 256 + i] = Sample(period, c, i);
  return frame;
}
bool Matches(const Frame& interleaved, unsigned period) {
  for (unsigned i = 0; i < 256; ++i)
    for (unsigned c = 0; c < 6; ++c)
      if (interleaved[i * 6 + c] != Sample(period, c, i)) return false;
  return true;
}
}  // namespace

int main() {
  using namespace rb_blitz::test;
  using rb_blitz::shell_music::StreamBufferSeconds;
  BeginCase("menu music gets decoded reserve without changing gameplay stream capacity");
  CHECK_TRUE(StreamBufferSeconds(true,0)==6);
  CHECK_TRUE(StreamBufferSeconds(true,1.2)==6);
  CHECK_TRUE(StreamBufferSeconds(true,10)==10);
  CHECK_TRUE(StreamBufferSeconds(false,0)==0);
  CHECK_TRUE(StreamBufferSeconds(false,1.2)==1.2);
  CHECK_TRUE(StreamBufferSeconds(true,-1)==-1);
  CHECK_TRUE(std::isnan(StreamBufferSeconds(true,std::numeric_limits<double>::quiet_NaN())));

  BeginCase("stream refill catches skipped blocks in either direction around the ring");
  using namespace rb_blitz::audio_stream;
  CHECK_FALSE(MissedRefill(8, 0, 3 * kBlockBytes));
  CHECK_FALSE(MissedRefill(8, 0, 4 * kBlockBytes)); // The retail check handles equality.
  CHECK_TRUE(MissedRefill(8, 0, 5 * kBlockBytes));
  CHECK_TRUE(MissedRefill(8, 3, 0)); // Cursor wrapped; the write cursor has not.
  CHECK_FALSE(MissedRefill(8, 7, 0));
  CHECK_FALSE(MissedRefill(0, 0, 0));
  CHECK_FALSE(MissedRefill(7, 0, 0));
  CHECK_FALSE(MissedRefill(8, 8, 0));
  CHECK_FALSE(MissedRefill(8, 0, 8 * kBlockBytes));
  CHECK_TRUE(NeedsDecodedAudio(8, 12, 3 * kBlockBytes, 44100));
  CHECK_FALSE(NeedsDecodedAudio(8, 12, 0, 44100));
  CHECK_FALSE(NeedsDecodedAudio(0, 0, 0, 44100));
  CHECK_FALSE(NeedsDecodedAudio(8, 0, 0, 0));

  BeginCase("audio read expansion requires the paired allocation and exact call site");
  PendingRead pending;
  CHECK_EQ(pending.Consume(kReadFileCaller, 0x40010000, kBlockBytes), kBlockBytes);
  pending.buffer = 0x40010000;
  CHECK_EQ(pending.Consume(0x1234, pending.buffer, kBlockBytes), kBlockBytes);
  CHECK_EQ(pending.Consume(kReadFileCaller, 0x40010000, kBlockBytes), kReadBytes);
  CHECK_EQ(pending.Consume(kReadFileCaller, 0x40010000, kBlockBytes), kBlockBytes);
  pending.buffer = 0x40010000;
  CHECK_EQ(pending.Consume(kReadFileCaller, 0x40020000, kBlockBytes), kBlockBytes);
  CHECK_EQ(pending.buffer, 0u);
  pending.buffer = 0x40010000;
  CHECK_EQ(pending.Consume(kReadFileCaller, pending.buffer, 4096), 4096u);
  CHECK_EQ(pending.buffer, 0u);

  BeginCase("preparation reserves CPU headroom and has no workers for empty input");
  using rex::gpu::PreparationWorkerCount;
  CHECK_EQ(PreparationWorkerCount(0,16),0);
  CHECK_EQ(PreparationWorkerCount(400,1),1);
  CHECK_EQ(PreparationWorkerCount(400,2),1);
  CHECK_EQ(PreparationWorkerCount(400,4),2);
  CHECK_EQ(PreparationWorkerCount(400,8),4);
  CHECK_EQ(PreparationWorkerCount(400,64),4);
  CHECK_EQ(PreparationWorkerCount(1,64),1);

  AudioApp app;
  CHECK_TRUE(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{})).has_value());
  auto& audio = app.GetAudio();
  CHECK_EQ(audio.GetOutputChannels(), 6u);
  CHECK_TRUE(static_cast<bool>(fill_audio));
  std::atomic<unsigned> callbacks{0};
  auto stream_result = audio.OpenStream([&] { ++callbacks; });
  CHECK_TRUE(stream_result.has_value());
  auto stream = *stream_result;
  Frame output{};
  BeginCase("PCM ownership, full queue, wraparound and close retain exact samples");
  fill_audio(output, 6);
  for (auto sample : output) CHECK_TRUE(sample == 0.0f);
  for (unsigned n = 0; n < 8; ++n) {
    auto input = MakeFrame(n);
    CHECK_TRUE(audio.Submit(stream, input).has_value());
    input.fill(-1.0f); // Submit must own its copy before returning.
  }
  CHECK_EQ(audio.GetStats().queued_frames, 8u);
  CHECK_FALSE(audio.Submit(stream, MakeFrame(99)).has_value());
  for (unsigned n = 0; n < 4; ++n) {
    fill_audio(output, 6);
    CHECK_TRUE(Matches(output, n));
  }
  for (unsigned n = 8; n < 12; ++n) CHECK_TRUE(audio.Submit(stream, MakeFrame(n)).has_value());
  for (unsigned n = 4; n < 12; ++n) {
    fill_audio(output, 6);
    CHECK_TRUE(Matches(output, n));
  }
  CHECK_EQ(callbacks.load(), 12u);
  fill_audio(output, 6);
  for (auto sample : output) CHECK_TRUE(sample == 0.0f);
  CHECK_TRUE(audio.Submit(stream, MakeFrame(99)).has_value());
  audio.CloseStream(stream);
  fill_audio(output, 6);
  for (auto sample : output) CHECK_TRUE(sample == 0.0f);
  CHECK_EQ(callbacks.load(), 12u);
  stream_result = audio.OpenStream([&] { ++callbacks; });
  CHECK_TRUE(stream_result.has_value());
  stream = *stream_result;

  BeginCase("a delayed producer and consumer never replay, reorder or tear PCM frames");
  constexpr unsigned count = 20000;
  std::atomic<unsigned> mismatches{0};
  std::atomic<bool> stop{false};
  std::thread producer([&] {
    for (unsigned n = 0; n < count && !stop; ++n) {
      const auto input = MakeFrame(n);
      while (!audio.Submit(stream, input)) {
        if (stop) return;
        std::this_thread::yield();
      }
      if (n % 1024 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  unsigned consumed = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (consumed < count && std::chrono::steady_clock::now() < deadline) {
    fill_audio(output, 6);
    if (output[0] == 0.0f) { std::this_thread::yield(); continue; }
    if (!Matches(output, consumed)) ++mismatches;
    if (consumed % 997 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ++consumed;
  }
  stop = true;
  producer.join();
  CHECK_EQ(consumed, count);
  CHECK_EQ(mismatches.load(), 0u);
  CHECK_EQ(callbacks.load(), count + 12);
  CHECK_EQ(audio.GetStats().queued_frames, 0u);
  fill_audio(output, 6);
  for (auto sample : output) CHECK_TRUE(sample == 0.0f);
  audio.CloseStream(stream);
  CHECK_EQ(app.Shutdown(), 0);
  return Finish();
}
