// SPDX-License-Identifier: GPL-2.0-only
#include "settings/graphics_settings.h"
#include "settings/runtime_values.h"
#include "settings/runtime_settings.h"
#include "settings/display_runtime.h"
#include "settings/display_hotkey.h"
#include "settings/display_trial.h"
#include <SDL3/SDL.h>
#include <deque>
#include <mutex>
#include <thread>
#include <rex/app/app.h>
#include <rex/apu/audio_system.h>
#include <rex/cli.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/ui/ui_system.h>
#include <rex/ui/native_window_host.h>
#include <cstdio>
#include <vector>
using namespace rb_blitz::settings;
using rex::SettingLayer;
class Probe final : public rex::app::App {
 public: Probe() : App("graphics_inputs_probe") {}
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    REX_TRY_VOID(RegisterTitleSettings(registry));
    REX_TRY_VOID(RegisterRuntimeSettingAliases(registry));
    REX_TRY_VOID(root.Set(std::filesystem::current_path()));
    REX_TRY_VOID(GetUI().GetSettings().driver.SetText(SettingLayer::kTitle,"none"));
    REX_TRY_VOID(GetInput().GetSettings().drivers.SetText(SettingLayer::kTitle,"none"));
    REX_TRY_VOID(GetAudio().GetSettings().driver.SetText(SettingLayer::kTitle,"none"));
    REX_TRY_VOID(GetGraphics().GetSettings().driver.SetText(SettingLayer::kTitle,"none"));
    return registry.SetText(SettingLayer::kTitle,"log.file","false");
  }
};
using namespace rex;
using namespace rex::ui;
class TestUI final : public rex::ui::IUISystem {
 public:
  TestUI(rex::ui::IUISystem& value, SDL_Window* native) : delegate(value),window(native) {}
  rex::ui::IUISystem& delegate;
  SDL_Window* window;
  bool accepting = true;
  const std::thread::id owner=std::this_thread::get_id();
  std::mutex mutex;
  std::deque<rex::MoveOnlyFunction<void()>> posts;
  void Pump() {
    std::deque<rex::MoveOnlyFunction<void()>> ready;
    { std::lock_guard lock(mutex); ready.swap(posts); }
    for(auto& fn : ready) fn();
  }
  UISettings& GetSettings() override { return delegate.GetSettings(); }
  bool IsHeadless() const override { return delegate.IsHeadless(); }
  bool HasWindow() const override { return delegate.HasWindow(); }
  bool IsFocused() const override { return delegate.IsFocused(); }
  VideoMode GetVideoMode() const override { return delegate.GetVideoMode(); }
  Result<NativeWindow> GetNativeWindow() const override { rex::NativeWindow native{}; native.sdl=window; return rex::Ok<rex::NativeWindow>(std::move(native)); }
  void RaiseWindow() override { return delegate.RaiseWindow(); }
  bool IsUIThread() const override { return std::this_thread::get_id()==owner; }
  void PostTask(MoveOnlyFunction<void()> fn) override { std::lock_guard lock(mutex); posts.push_back(std::move(fn)); }
  bool RunSynchronous(MoveOnlyFunction<void()> fn) override { if (!accepting || !IsUIThread()) return false; fn(); return true; }
  ScopeGuard AddEventListener(IUIEventListener& listener) override { return delegate.AddEventListener(listener); }
  ImFontAtlas& GetFonts() override { return delegate.GetFonts(); }
  ImGuiStyle& GetImGuiStyle() override { return delegate.GetImGuiStyle(); }
  ImGuiIO& GetImGuiIO() override { return delegate.GetImGuiIO(); }
  ui::Style& GetStyle() override { return delegate.GetStyle(); }
  Result<void> AddOverlays(std::span<const OverlayConfig> overlays) override { return delegate.AddOverlays(overlays); }
  void ShowOverlay(std::string_view name, bool visible) override { return delegate.ShowOverlay(name, visible); }
  void ToggleOverlay(std::string_view name) override { return delegate.ToggleOverlay(name); }
  Result<ui::IDialog*> GetOverlay(std::string_view name) override { return delegate.GetOverlay(name); }
  bool IsAnyOverlayVisible() const override { return delegate.IsAnyOverlayVisible(); }
  ScopeGuard AddDialog(ui::IDialog& dialog) override { return delegate.AddDialog(dialog); }
  void AddDialog(std::unique_ptr<ui::IDialog> dialog) override { return delegate.AddDialog(std::move(dialog)); }
  bool HasDialogs() const override { return delegate.HasDialogs(); }
  void DrawOverlays(IOverlayRenderer& renderer) override { return delegate.DrawOverlays(renderer); }
  void ShowMessage(MessageType type, std::string_view heading, std::string_view body) override { return delegate.ShowMessage(type, heading, body); }
  Result<u32> ShowChoice(const ui::ChoiceConfig& config) override { return delegate.ShowChoice(config); }
  Result<std::vector<std::filesystem::path>> ShowFileDialog( const ui::FileDialogConfig& config) override { return delegate.ShowFileDialog(config); }
  void ShowPrompt(ui::PromptConfig config, MoveOnlyFunction<void(ui::PromptResult)> done) override { return delegate.ShowPrompt(config, std::move(done)); }
  void CancelPrompts() override { return delegate.CancelPrompts(); }
  std::vector<ui::Monitor> GetDisplays() override { return delegate.GetDisplays(); }
  std::vector<ui::HostLocale> GetPreferredLocales() override { return delegate.GetPreferredLocales(); }
};

class Host final : public rex::ui::INativeWindowHost {
 public:
  bool Attach(void*, std::function<void(bool)>) override { return true; }
  bool SetDisplayMode(rex::ui::NativeHostDisplayMode requested) override {
    ++requests;
    if (refuse && requested == rex::ui::NativeHostDisplayMode::kFullscreen) return false;
    if (!ignore && acknowledged) mode = requested;
    return true;
  }
  void SetTitle(std::string_view) override {}
  std::optional<rex::ui::NativeHostDisplayMode> GetDisplayMode() const override {
    return acknowledged ? std::optional(mode) : std::nullopt;
  }
  rex::ui::NativeHostDisplayMode mode = rex::ui::NativeHostDisplayMode::kWindowed;
  bool refuse = false, ignore = false, acknowledged = true;
  int requests = 0;
};

#define VERIFY(expression, code) do { if (!(expression)) { std::fprintf(stderr,"failed %d: %s\n",code,#expression); return code; } } while(false)
int main() {
  Probe app;
  VERIFY(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{})),1);
  VERIFY(SDL_Init(SDL_INIT_VIDEO),2);
  auto* native=SDL_CreateWindow("Display mode regression",1280,720,SDL_WINDOW_HIDDEN);
  VERIFY(native,3);
  TestUI ui(app.GetUI(),native);
  const auto directory = std::filesystem::temp_directory_path() /
      ("rb-blitz-display-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto file = directory / "settings.toml";
  std::string error, previous;
  bool pending=false;
  VERIFY(ClearUnconfirmedDisplayPreview(file,&error),4);
  VERIFY(RuntimeDisplayMode(ui)=="windowed",5);
  VERIFY(!ApplyRuntimeDisplay(ui,"invalid",&error),6);
  VERIFY(RuntimeDisplayMode(ui)=="windowed",7);
  VERIFY(ApplyRuntimeDisplay(ui,"borderless",&error),8);
  VERIFY(RuntimeDisplayMode(ui)=="borderless",9);
  VERIFY(InspectRuntimeSetting("ui.fullscreen").active_value=="true",10);
  VERIFY(InspectRuntimeSetting("ui.fullscreen_exclusive").active_value=="false",11);
  VERIFY(ApplyRuntimeDisplay(ui,"windowed",&error),12);

  int width = 0, height = 0;
  VERIFY(SDL_GetWindowSize(native, &width, &height) && width == 1280 && height == 720, 80);
  DisplayHotkey hotkey;
  auto key = [&](InputEvent::Type type, KeyModifiers modifiers = KeyModifiers::kNone,
                 bool repeat = false) {
    InputEvent event{};
    event.type = type;
    event.key = VirtualKey::kReturn;
    event.modifiers = modifiers;
    event.repeat = repeat;
    if (!hotkey.ShouldToggle(event)) return false;
    return ApplyRuntimeDisplay(ui,
        RuntimeDisplayMode(ui) == "windowed" ? "borderless" : "windowed", &error);
  };
  VERIFY(!key(InputEvent::Type::kKeyDown), 81);
  VERIFY(key(InputEvent::Type::kKeyDown, KeyModifiers::kAlt), 82);
  VERIFY(RuntimeDisplayMode(ui) == "borderless", 83);
  VERIFY(!key(InputEvent::Type::kKeyDown, KeyModifiers::kAlt, true), 84);
  VERIFY(!key(InputEvent::Type::kKeyDown, KeyModifiers::kAlt), 85);
  VERIFY(RuntimeDisplayMode(ui) == "borderless", 86);
  VERIFY(!key(InputEvent::Type::kKeyUp), 87);
  VERIFY(key(InputEvent::Type::kKeyDown, KeyModifiers::kAlt), 88);
  VERIFY(RuntimeDisplayMode(ui) == "windowed", 89);
  VERIFY(!key(InputEvent::Type::kFocusLost), 90);
  VERIFY(key(InputEvent::Type::kKeyDown, KeyModifiers::kAlt), 91);
  VERIFY(RuntimeDisplayMode(ui) == "borderless", 92);
  VERIFY(!key(InputEvent::Type::kKeyUp), 93);
  VERIFY(ApplyRuntimeDisplay(ui, "fullscreen", &error), 94);
  VERIFY(key(InputEvent::Type::kKeyDown, KeyModifiers::kAlt), 95);
  VERIFY(RuntimeDisplayMode(ui) == "windowed", 96);
  VERIFY(!key(InputEvent::Type::kKeyUp), 97);
  // Every compound transition has one acknowledged mode and two matching settings.
  for (const auto from : {"windowed", "borderless", "fullscreen"}) {
    VERIFY(ApplyRuntimeDisplay(ui, from, &error), 98);
    for (const auto to : {"windowed", "borderless", "fullscreen"}) {
      VERIFY(ApplyRuntimeDisplay(ui, to, &error), 99);
      VERIFY(RuntimeDisplayMode(ui) == to, 100);
    }
  }
  VERIFY(ApplyRuntimeDisplay(ui, "windowed", &error), 101);

  // The entire hotkey chord is consumed, even after its modifier was released.
  {
    DisplayHotkey chords;
    InputEvent event{.type = InputEvent::Type::kKeyDown, .key = VirtualKey::kReturn,
                     .modifiers = KeyModifiers::kAlt};
    VERIFY(chords.Handle(event) == DisplayHotkey::Action::kToggle && event.handled, 120);
    event.handled = false; event.repeat = true;
    VERIFY(chords.Handle(event) == DisplayHotkey::Action::kNone && event.handled, 121);
    event.handled = false; event.repeat = false; event.modifiers = KeyModifiers::kNone;
    event.type = InputEvent::Type::kKeyUp;
    VERIFY(chords.Handle(event) == DisplayHotkey::Action::kNone && event.handled, 122);
    event.handled = false; event.type = InputEvent::Type::kKeyDown;
    VERIFY(chords.Handle(event) == DisplayHotkey::Action::kNone && !event.handled, 123);
    event.key = VirtualKey::kF11; event.modifiers = KeyModifiers::kAlt;
    VERIFY(chords.Handle(event) == DisplayHotkey::Action::kNone && !event.handled, 124);
    event.modifiers = static_cast<KeyModifiers>(static_cast<unsigned>(KeyModifiers::kAlt) |
                                               static_cast<unsigned>(KeyModifiers::kCtrl));
    VERIFY(chords.Handle(event) == DisplayHotkey::Action::kTrial && event.handled, 125);
    event.handled = false; event.repeat = true;
    VERIFY(chords.Handle(event) == DisplayHotkey::Action::kNone && event.handled, 126);
    event.handled = false; event.type = InputEvent::Type::kKeyUp;
    event.modifiers = KeyModifiers::kNone;
    VERIFY(chords.Handle(event) == DisplayHotkey::Action::kNone && event.handled, 127);
    event.type = InputEvent::Type::kFocusLost; event.handled = false;
    VERIFY(chords.Handle(event) == DisplayHotkey::Action::kNone, 128);
  }
  const auto queued = [&] {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < limit) {
      { std::lock_guard lock(ui.mutex); if (!ui.posts.empty()) return true; }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  };
  const auto unchanged_fullscreen = InspectRuntimeSetting("ui.fullscreen");
  const auto unchanged_exclusive = InspectRuntimeSetting("ui.fullscreen_exclusive");
  {
    DisplayTrial trial(ui, std::chrono::milliseconds(5));
    VERIFY(trial.Cycle(&error) && RuntimeDisplayMode(ui) == "borderless", 129);
    VERIFY(queued(), 130);
    VERIFY(trial.Cycle(&error) && RuntimeDisplayMode(ui) == "fullscreen", 131);
    ui.Pump(); // The first generation's queued rollback must not undo this cycle.
    VERIFY(trial.active() && RuntimeDisplayMode(ui) == "fullscreen", 132);
    VERIFY(queued(), 133);
    ui.Pump();
    VERIFY(!trial.active() && RuntimeDisplayMode(ui) == "windowed", 134);
  }
  {
    DisplayTrial trial(ui, std::chrono::seconds(1));
    VERIFY(trial.Cycle(&error) && RuntimeDisplayMode(ui) == "borderless", 135);
    VERIFY(trial.Cycle(&error) && RuntimeDisplayMode(ui) == "fullscreen", 136);
    VERIFY(trial.Cycle(&error) && RuntimeDisplayMode(ui) == "windowed", 137);
    VERIFY(trial.Close(&error) && RuntimeDisplayMode(ui) == "windowed", 138);
    VERIFY(trial.Close(&error), 139);
    VERIFY(!trial.Cycle(&error), 140);
  }
  {
    DisplayTrial trial(ui, std::chrono::milliseconds(5));
    VERIFY(trial.Cycle(&error), 141);
    VERIFY(queued(), 142);
  } // Close restores while the window is alive and retires the queued weak task.
  VERIFY(RuntimeDisplayMode(ui) == "windowed", 143);
  VERIFY(ApplyTransientDisplay(ui, "borderless", &error), 144);
  ui.Pump();
  VERIFY(RuntimeDisplayMode(ui) == "borderless", 145);
  VERIFY(ApplyTransientDisplay(ui, "windowed", &error), 146);
  VERIFY(InspectRuntimeSetting("ui.fullscreen").active_value == unchanged_fullscreen.active_value &&
         InspectRuntimeSetting("ui.fullscreen").staged_source == unchanged_fullscreen.staged_source, 147);
  VERIFY(InspectRuntimeSetting("ui.fullscreen_exclusive").active_value == unchanged_exclusive.active_value &&
         InspectRuntimeSetting("ui.fullscreen_exclusive").staged_source == unchanged_exclusive.staged_source, 148);
  VERIFY(!std::filesystem::exists(file) && !std::filesystem::exists(file.string() + ".display-preview.toml"), 149);

  {
    DisplayPreviewTransaction preview(ui, file);
    VERIFY(preview.Begin("windowed", "borderless", &error), 102);
    VERIFY(preview.Revert(&error) && !preview.active(), 103);
    VERIFY(RuntimeDisplayMode(ui) == "windowed", 104);
    VERIFY(preview.Begin("windowed", "borderless", &error), 105);
    auto marker = file;
    marker += ".display-preview.toml";
    std::error_code ec;
    VERIFY(std::filesystem::remove(marker, ec) && !ec, 106);
    VERIFY(std::filesystem::create_directory(marker, ec) && !ec, 107);
    VERIFY(std::filesystem::create_directory(marker / "keep-marker-nonempty", ec) && !ec, 108);
    VERIFY(!preview.Revert(&error) && preview.active(), 109);
    VERIFY(!preview.Revert(&error) && preview.active(), 110);
    std::filesystem::remove_all(marker, ec);
    VERIFY(!ec, 111);
    VERIFY(preview.Revert(&error) && !preview.active(), 112);
  }
  // A rejected UI dispatch must retain the durable recovery marker, even when
  // rollback cannot reach a native window during shutdown.
  {
    TestUI stopped(app.GetUI(), native);
    stopped.accepting = false;
    DisplayPreviewTransaction preview(stopped, file);
    bool began = true;
    std::thread worker([&] { began = preview.Begin("windowed", "borderless", &error); });
    worker.join();
    VERIFY(!began && !preview.active(), 113);
    VERIFY(ReadUnconfirmedDisplayPreview(file, &previous, &pending, &error) &&
           pending && previous == "windowed", 114);
    VERIFY(ClearUnconfirmedDisplayPreview(file, &error), 115);
  }

  {
    DisplayPreviewTransaction preview(ui,file);
    VERIFY(preview.Begin("windowed","borderless",&error),13);
    VERIFY(ReadUnconfirmedDisplayPreview(file,&previous,&pending,&error) && pending && previous=="windowed",14);
    VERIFY(!preview.Keep(false,[](auto*) { return true; },&error),15);
    VERIFY(preview.active(),16);
    VERIFY(!preview.Keep(true,[](auto*) { return false; },&error),17);
    VERIFY(!preview.active() && RuntimeDisplayMode(ui)=="windowed",18);
    VERIFY(ReadUnconfirmedDisplayPreview(file,&previous,&pending,&error) && !pending,19);
    VERIFY(preview.Begin("windowed","borderless",&error),20);
    int saves=0;
    VERIFY(preview.Keep(true,[&](auto*) { ++saves; return true; },&error),21);
    VERIFY(saves==1 && !preview.active() && RuntimeDisplayMode(ui)=="borderless",22);
  }
  {
    DisplayPreviewTransaction preview(ui,file,std::chrono::milliseconds(25));
    VERIFY(preview.Begin("borderless","windowed",&error),23);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(preview.active() && std::chrono::steady_clock::now()<deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      ui.Pump();
    }
    VERIFY(!preview.active() && RuntimeDisplayMode(ui)=="borderless",24);
    VERIFY(ReadUnconfirmedDisplayPreview(file,&previous,&pending,&error) && !pending,25);
  }
  VERIFY(ApplyRuntimeDisplay(ui,"windowed",&error),26);
  {
    DisplayPreviewTransaction preview(ui,file,std::chrono::milliseconds(5));
    VERIFY(preview.Begin("windowed","borderless",&error),27);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  // A queued timeout holds only a weak transaction state after destruction.
  ui.Pump();
  VERIFY(RuntimeDisplayMode(ui)=="borderless",28);
  VERIFY(ReadUnconfirmedDisplayPreview(file,&previous,&pending,&error) && pending && previous=="windowed",29);
  VERIFY(ClearUnconfirmedDisplayPreview(file,&error),30);
  {
    auto host = std::make_shared<Host>();
    auto registration = RegisterNativeWindowHost(host);
    VERIFY(RuntimeDisplayMode(ui) == "windowed", 60);
    VERIFY(ApplyRuntimeDisplay(ui, "borderless", &error), 61);
    VERIFY(RuntimeDisplayMode(ui) == "borderless", 62);
    VERIFY(ApplyRuntimeDisplay(ui, "fullscreen", &error), 63);
    VERIFY(RuntimeDisplayMode(ui) == "fullscreen", 64);
    VERIFY(InspectRuntimeSetting("ui.fullscreen_exclusive").active_value == "true", 65);
    VERIFY(ApplyRuntimeDisplay(ui, "windowed", &error), 66);
    host->refuse = true;
    VERIFY(!ApplyRuntimeDisplay(ui, "fullscreen", &error), 67);
    VERIFY(RuntimeDisplayMode(ui) == "windowed", 68);
    VERIFY(InspectRuntimeSetting("ui.fullscreen").active_value == "false", 69);
    host->refuse = false;
    host->ignore = true;
    VERIFY(!ApplyRuntimeDisplay(ui, "borderless", &error), 70);
    VERIFY(RuntimeDisplayMode(ui) == "windowed", 71);
    host->ignore = false;
    host->acknowledged = false;
    const int requests = host->requests;
    VERIFY(!ApplyRuntimeDisplay(ui, "borderless", &error), 72);
    VERIFY(host->requests == requests, 73);
    VERIFY(RuntimeDisplayMode(ui).empty(), 74);
    host->acknowledged = true;
    DisplayPreviewTransaction preview(ui, file);
    VERIFY(preview.Begin("windowed", "borderless", &error), 75);
    VERIFY(!preview.Keep(false, [](auto*) { return true; }, &error), 76);
    VERIFY(!preview.Keep(true, [](auto*) { return false; }, &error), 77);
    VERIFY(RuntimeDisplayMode(ui) == "windowed", 78);
  }
  VERIFY(RuntimeDisplayMode(ui) == "borderless", 79);
  VERIFY(REX_SETTINGS().SetText(SettingLayer::kLock,"ui.fullscreen","true"),31);
  VERIFY(!ApplyRuntimeDisplay(ui,"windowed",&error),32);
  VERIFY(RuntimeDisplayMode(ui)=="borderless",33);
  std::puts("PASS: native SDL mode/readback, failed-save rollback, presentation gate, timeout, shutdown weak state and locked setting refusal");
  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  VERIFY(!cleanup_error, 116);
  SDL_DestroyWindow(native);
  SDL_Quit();
  return app.Shutdown();
}
