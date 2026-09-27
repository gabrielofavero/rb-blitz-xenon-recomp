// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/input/mouse_ui.h for why the mouse is translated into left-stick
// presses, and src/input/ui_nav.h for the translation itself. This file is the
// device side: the window events that aim the aligner, the guest frames that time
// it, the pad state the guest polls out, and the hook that installs the whole
// thing.

#include "input/mouse_ui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/ui/flags.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>

REXCVAR_DEFINE_BOOL(mouse_ui_nav, true, "Input",
                    "Move the guest's menu selection by moving the mouse, and press A/B with the "
                    "left/right button, without an ImGui overlay having the pointer");
REXCVAR_DEFINE_BOOL(mouse_ui_hover, true, "Input",
                    "Make the pointer's row the guest's selected row: the pointer is positioned in "
                    "the guest's own pixels, the highlight is measured from the guest's frames, and "
                    "the stick is pulsed until the two are the same row. Off, or with no frames to "
                    "read, the mouse moves the selection by the distance the pointer moves instead");
REXCVAR_DEFINE_DOUBLE(mouse_ui_hover_delay_ms, 80.0, "Input",
                      "How long the pointer is left resting before the selection moves to the row "
                      "it is over, in milliseconds: long enough that sweeping across a list costs "
                      "one move rather than one per row crossed")
    .range(0.0, 2000.0);
REXCVAR_DEFINE_DOUBLE(mouse_ui_idle_hz, 5.0, "Input",
                      "How often the guest's own frames are read back while nothing is being "
                      "aligned, in hertz: each read copies and converts the whole frontbuffer on "
                      "the CPU, and this is what notices a screen or resolution change; during a "
                      "hover the frames are read as fast as they arrive, whatever this says")
    .range(0.1, 60.0);
REXCVAR_DEFINE_DOUBLE(mouse_ui_row_fraction, 0.0417, "Input",
                      "Fallback only, for when there are no guest frames to measure: pointer motion "
                      "that moves the menu selection by one row, as a fraction of the window's "
                      "height")
    .range(0.01, 0.25);
REXCVAR_DEFINE_DOUBLE(mouse_ui_press_ms, 24.0, "Input",
                      "Fallback only: how long one menu step holds the stick, in milliseconds - a "
                      "step the guest's frame cannot see is a row that never moves. Hover "
                      "alignment needs no setting here, its pulses end when the guest's frame "
                      "shows them")
    .range(1.0, 1000.0);
REXCVAR_DEFINE_DOUBLE(mouse_ui_release_ms, 24.0, "Input",
                      "Fallback only: how long one menu step releases the stick before the next "
                      "one, in milliseconds - the next row only moves if the guest sees this gap")
    .range(0.0, 1000.0);
REXCVAR_DEFINE_BOOL(mouse_ui_probe, false, "Input",
                    "Bring-up instrumentation: log every guest frame that is read back, with "
                    "per-row brightness, saturation and difference band counts, to "
                    "mouse_ui_probe_path (slows nothing else)");
REXCVAR_DEFINE_BOOL(mouse_ui_hover_log, false, "Input",
                    "Bring-up instrumentation: log what each hover did - how far the selection had "
                    "to move, the row pitch measured on the way, and why the burst ended - to the "
                    "same file as the probe");
REXCVAR_DEFINE_INT32(mouse_ui_probe_thumb_every, 10, "Input",
                     "Log a 96x36 ASCII thumbnail of every Nth frame the probe reads (0 for none)")
    .range(0, 1000);
REXCVAR_DEFINE_STRING(mouse_ui_probe_path, "out/mouse-probe/guestframes.log", "Input",
                      "Where the guest-frame probe and the hover log write their text log");

namespace rb_blitz::input {

using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::InputDriver;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;
using rex::X_RESULT;
using rex::X_STATUS;

namespace {

// Used only until an interval has been measured. 2.4 ms is the cadence the guest
// log shows here, ~420 reads of this device a second.
constexpr double kAssumedPollIntervalMs = 2.4;

// A gap longer than this is a stall - a breakpoint, a resize, a sleeping
// process - and not a cadence, so it is dropped instead of being smoothed in.
constexpr double kMaxPollIntervalMs = 500.0;

// Enough smoothing to ignore one short gap between two reads without taking
// seconds to follow a real change of cadence.
constexpr double kPollIntervalSmoothing = 0.125;

// What a millisecond pulse is worth in polls at the cadence being measured. The
// stepper clamps what this returns: only the arithmetic belongs here.
int PollsForMilliseconds(double milliseconds, double poll_interval_ms) {
  const double interval = poll_interval_ms > 0.0 ? poll_interval_ms : kAssumedPollIntervalMs;
  return static_cast<int>(std::lround(milliseconds / interval));
}

// The pulser's numbering, named once so that the mouse handlers and the
// guest-side mapping cannot drift apart: index 0 is the left button, which is the
// guest's A, and 1 the right, which is B.
constexpr int kLeftClick = 0;
constexpr int kRightClick = 1;

// How long a frame may go unread before the mouse stops trusting the one it has:
// a presenter that has gone away, a guest that has stopped presenting, a window
// that has only just been created. Without frames there is nothing to measure, so
// the travel fallback does the navigating until one is read again.
constexpr double kFrameStaleMs = 1500.0;

// How long a click waits for the selection to arrive under the pointer before it
// goes through anyway: an alignment that cannot finish must not swallow the click.
constexpr double kClickAlignTimeoutMs = 400.0;

// How often the frame thread wakes to check whether a hover wants frames read at
// full speed. A wakeup this often costs nothing and is what keeps the wait for the
// next frame from delaying the first press of a burst.
constexpr double kFrameThreadSliceMs = 5.0;

// A presenter that does not exist yet, or has gone, is worth coming back to - but
// not as often as a frame is.
constexpr double kNoPresenterPauseMs = 50.0;

// ---------------------------------------------------------------------------
// Guest-frame readback
//
// The measuring stick the hover bridge needs: the guest exposes no hit test, so
// the only way to learn where it drew its menu highlight - and so which row the
// pointer is over - is to look at the frame it drew it in. CaptureGuestOutput
// copies the newest guest frame back to the CPU and converts it in software, which
// is why the readback runs at a few hertz while nothing is being aligned and as
// fast as the frames arrive while a hover is: what the hover needs from it is the
// frame the guest drew *after* a press, and there is no way to get that sooner than
// the guest draws it.
//
// Two things are read out of the same readback. Every frame that changed is fed to
// the aligner, sampled down to what the detector in src/input/nav_detect.h works on
// (four bytes per pixel in, three bytes per two-pixel step out). And, when the probe
// cvar asks, the frame is also written to a text log with per-row brightness,
// saturation and frame-to-frame difference counts plus an ASCII thumbnail, which is
// how a screen can be looked at without a debugger attached.
// ---------------------------------------------------------------------------

// Every other column is enough for a bar hundreds of pixels wide, and halves the
// work: the sample counts below are per row, out of `width / kProbeSampleStepX`.
constexpr int kProbeSampleStepX = 2;
constexpr int kProbeBrightThreshold = 190;
constexpr int kProbeSaturatedThreshold = 150;
constexpr int kProbeSaturationSpread = 60;
constexpr int kProbeChangedThreshold = 24;
constexpr int kProbeRunMinSamples = 16;
constexpr int kProbeMaxBands = 4;
constexpr int kProbeThumbColumns = 96;
constexpr int kProbeThumbRows = 36;

struct ProbeRowCounts {
  int bright = 0;
  int saturated = 0;
  int changed = 0;
};

struct ProbeBand {
  int first = 0;
  int last = 0;
  int peak = 0;
};

// Rows above the sample minimum, strongest first, as "label=n[y0-y1:peak]...".
template <typename Getter>
void AppendProbeBands(const std::vector<ProbeRowCounts>& rows, std::string& out, const char* label,
                      const Getter& get) {
  std::vector<ProbeBand> bands;
  const int row_count = static_cast<int>(rows.size());
  int first = -1;
  int peak = 0;
  for (int y = 0; y <= row_count; ++y) {
    const int count = y < row_count ? get(rows[y]) : 0;
    if (count >= kProbeRunMinSamples) {
      if (first < 0) {
        first = y;
        peak = 0;
      }
      peak = std::max(peak, count);
    } else if (first >= 0) {
      bands.push_back(ProbeBand{first, y - 1, peak});
      first = -1;
    }
  }
  std::sort(bands.begin(), bands.end(),
            [](const ProbeBand& a, const ProbeBand& b) { return a.peak > b.peak; });
  if (bands.size() > kProbeMaxBands) {
    bands.resize(kProbeMaxBands);
  }
  out += " ";
  out += label;
  out += "=";
  out += std::to_string(bands.size());
  for (const ProbeBand& band : bands) {
    out += "[";
    out += std::to_string(band.first);
    out += "-";
    out += std::to_string(band.last);
    out += ":";
    out += std::to_string(band.peak);
    out += "]";
  }
}

std::string ProbeThumbnail(const rex::ui::RawImage& image) {
  static constexpr char kLevels[] = " .:-=+*#%@";
  constexpr int kLevelCount = 10;
  std::string out;
  if (image.width == 0 || image.height == 0) {
    return out;
  }
  const uint8_t* pixels = image.data.data();
  for (int ty = 0; ty < kProbeThumbRows; ++ty) {
    const uint32_t y0 = static_cast<uint32_t>(ty) * image.height / kProbeThumbRows;
    const uint32_t y1 = std::max(y0 + 1, (static_cast<uint32_t>(ty) + 1) * image.height /
                                              kProbeThumbRows);
    for (int tx = 0; tx < kProbeThumbColumns; ++tx) {
      const uint32_t x0 = static_cast<uint32_t>(tx) * image.width / kProbeThumbColumns;
      const uint32_t x1 =
          std::max(x0 + 1, (static_cast<uint32_t>(tx) + 1) * image.width / kProbeThumbColumns);
      int sum = 0;
      int count = 0;
      for (uint32_t y = y0; y < y1; y += 2) {
        const uint8_t* row = pixels + static_cast<size_t>(y) * image.stride;
        for (uint32_t x = x0; x < x1; x += 2) {
          const uint8_t* pixel = row + static_cast<size_t>(x) * 4;
          const int high = std::max(std::max(pixel[0], pixel[1]), pixel[2]);
          const int low = std::min(std::min(pixel[0], pixel[1]), pixel[2]);
          sum += (high + low) / 2;
          ++count;
        }
      }
      out += kLevels[count ? sum / count * (kLevelCount - 1) / 255 : 0];
    }
    out += '\n';
  }
  return out;
}

// The readback's state between frames, and the log it writes. The frame thread is
// the only thread that reads frames, but the guest thread adds a line per finished
// hover to the same file, so the file has a lock and the rest of this does not.
struct FrameReadback {
  std::mutex log_mutex;
  std::ofstream log;
  bool log_open = false;

  // Frame thread only, from here on.
  std::vector<uint8_t> previous;
  uint32_t previous_width = 0;
  uint32_t previous_height = 0;
  std::vector<ProbeRowCounts> rows;
  uint32_t frame = 0;
};

FrameReadback& Readback() {
  static FrameReadback readback;
  return readback;
}

// Whether the frame just read is the one before it again, which is how a frame the
// guest has not redrawn is told from one it has: CaptureGuestOutput hands back the
// newest image either way, so pixels that have not moved are the only evidence that
// there is nothing new to measure.
bool SamePixels(const FrameReadback& readback, const rex::ui::RawImage& image) {
  return readback.previous.size() == image.data.size() &&
         readback.previous_width == image.width && readback.previous_height == image.height &&
         std::memcmp(readback.previous.data(), image.data.data(), image.data.size()) == 0;
}

void AppendFrameLine(FrameReadback* readback, const rex::ui::RawImage& image, double capture_ms,
                     uint32_t frame, double time_ms, bool thumbnail, std::string& out) {
  const uint32_t width = image.width;
  const uint32_t height = image.height;
  const bool have_previous = readback->previous_width == width &&
                             readback->previous_height == height &&
                             readback->previous.size() == image.data.size();
  readback->rows.assign(height, ProbeRowCounts{});
  std::vector<ProbeRowCounts>& rows = readback->rows;
  const uint8_t* pixels = image.data.data();
  const uint8_t* previous_pixels = have_previous ? readback->previous.data() : nullptr;
  int64_t sum_r = 0;
  int64_t sum_g = 0;
  int64_t sum_b = 0;
  int64_t samples = 0;
  for (uint32_t y = 0; y < height; ++y) {
    const size_t row_offset = static_cast<size_t>(y) * image.stride;
    const uint8_t* row = pixels + row_offset;
    const uint8_t* previous_row = previous_pixels ? previous_pixels + row_offset : nullptr;
    ProbeRowCounts counts;
    for (uint32_t x = 0; x < width; x += kProbeSampleStepX) {
      const uint8_t* pixel = row + static_cast<size_t>(x) * 4;
      const int r = pixel[0];
      const int g = pixel[1];
      const int b = pixel[2];
      const int high = std::max(std::max(r, g), b);
      const int low = std::min(std::min(r, g), b);
      if (high >= kProbeBrightThreshold) {
        ++counts.bright;
      }
      if (high >= kProbeSaturatedThreshold && high - low >= kProbeSaturationSpread) {
        ++counts.saturated;
      }
      sum_r += r;
      sum_g += g;
      sum_b += b;
      ++samples;
      if (previous_row) {
        const uint8_t* before = previous_row + static_cast<size_t>(x) * 4;
        if (std::abs(r - static_cast<int>(before[0])) >= kProbeChangedThreshold ||
            std::abs(g - static_cast<int>(before[1])) >= kProbeChangedThreshold ||
            std::abs(b - static_cast<int>(before[2])) >= kProbeChangedThreshold) {
          ++counts.changed;
        }
      }
    }
    rows[y] = counts;
  }
  out = "F " + std::to_string(frame) + " t=" + std::to_string(static_cast<int64_t>(time_ms)) + " " +
        std::to_string(width) + "x" + std::to_string(height);
  out += " stride=" + std::to_string(image.stride);
  out += " cap=" + std::to_string(capture_ms) + "ms";
  if (samples > 0) {
    out += " mean=(" + std::to_string(sum_r / samples) + "," + std::to_string(sum_g / samples) +
           "," + std::to_string(sum_b / samples) + ")";
  }
  AppendProbeBands(rows, out, "bright",
                   [](const ProbeRowCounts& counts) { return counts.bright; });
  AppendProbeBands(rows, out, "satur",
                   [](const ProbeRowCounts& counts) { return counts.saturated; });
  if (have_previous) {
    AppendProbeBands(rows, out, "diff",
                     [](const ProbeRowCounts& counts) { return counts.changed; });
  }
  if (thumbnail) {
    out += "\n";
    out += ProbeThumbnail(image);
  }
}

}  // namespace

MouseUiInputDriver::~MouseUiInputDriver() {
  // The window outlives the driver and would otherwise keep calling into it.
  DetachFromWindow();
}

X_STATUS MouseUiInputDriver::Setup() {
  return X_STATUS_SUCCESS;
}

void MouseUiInputDriver::OnWindowAvailable(rex::ui::Window* window) {
  if (!window) {
    return;
  }
  {
    std::lock_guard lock(state_mutex_);
    attached_window_ = window;
    UpdateRowPixels();
    aligner_.SetHoverDelayMs(REXCVAR_GET(mouse_ui_hover_delay_ms));
    if (REXCVAR_GET(mouse_ui_hover_log)) {
      aligner_.SetStepLogger([this](const MenuHoverAligner::StepLog& step) { LogStep(step); });
    }
    REXLOG_INFO("mouse_ui: a menu row is {} px on a {}x{} window (fallback pitch), hover "
                "alignment {}, guest frames at {} Hz",
                nav_.row_pixels(), window->GetActualPhysicalWidth(),
                window->GetActualPhysicalHeight(), REXCVAR_GET(mouse_ui_hover),
                REXCVAR_GET(mouse_ui_idle_hz));
  }
  window->AddInputListener(this, window_z_order());
  window->AddListener(this);
  StartFrameThread();
}

void MouseUiInputDriver::OnClosing(rex::ui::UIEvent&) {
  DetachFromWindow();
}

// Safe to call from any thread.
void MouseUiInputDriver::DetachFromWindow() {
  // Before the window goes away, whatever happens to it: the frame thread reads the
  // presenter through the provider callback, and the presenter goes with the window
  // (and the runtime) it belongs to.
  StopFrameThread();
  rex::ui::Window* window = attached_window_;
  if (!window) {
    return;
  }
  window->app_context().CallInUIThreadSynchronous([this, window] {
    {
      std::lock_guard lock(state_mutex_);
      attached_window_ = nullptr;
      client_width_ = 0;
      client_height_ = 0;
      aligner_.Forget();
      mapping_ = GuestImageMapping{};
      have_pointer_ = false;
    }
    window->RemoveInputListener(this);
    window->RemoveListener(this);
  });
}

bool MouseUiInputDriver::IsEnabled() const {
  return REXCVAR_GET(mouse_ui_nav);
}

void MouseUiInputDriver::UpdateRowPixels() {
  rex::ui::Window* window = attached_window_;
  if (!window) {
    return;
  }
  const uint32_t width = window->GetActualPhysicalWidth();
  const uint32_t height = window->GetActualPhysicalHeight();
  // Zero is documented for a window whose surface has not been given a size yet;
  // until there is a height to scale by, the stepper's own default is a better
  // answer than a row of no pixels. The size is kept because both threads need it
  // to map the pointer into the guest's pixels, and only the UI thread may ask the
  // window.
  client_width_ = static_cast<int>(width);
  client_height_ = static_cast<int>(height);
  if (height == 0) {
    return;
  }
  nav_.SetRowPixels(REXCVAR_GET(mouse_ui_row_fraction) * static_cast<double>(height));
}

bool MouseUiInputDriver::RefreshMapping() {
  const int guest_width = guest_frame_width_.load();
  const int guest_height = guest_frame_height_.load();
  if (attached_window_ == nullptr || client_width_ <= 0 || client_height_ <= 0 ||
      guest_width <= 0 || guest_height <= 0) {
    mapping_ = GuestImageMapping{};
    aligner_.ClearTarget();
    return false;
  }
  const GuestImageMapping mapping = ComputeGuestImageMapping(
      client_width_, client_height_, guest_width, guest_height,
      REXCVAR_GET(present_letterbox));
  const bool moved = !mapping.valid || !mapping_.valid || mapping.scale != mapping_.scale ||
                     mapping.offset_x != mapping_.offset_x || mapping.offset_y != mapping_.offset_y;
  mapping_ = mapping;
  if (!mapping_.valid) {
    aligner_.ClearTarget();
    return false;
  }
  if (moved && have_pointer_) {
    // The pointer has not moved, but the pixels it was measured in mean something
    // else now - a resize, a DPI change, or a guest output of another size. Aim the
    // aligner at the same spot on the glass instead of leaving the old pixels behind.
    aligner_.SetTargetY(mapping_.ClientToGuestY(pointer_y_), NowMs());
  }
  return true;
}

void MouseUiInputDriver::StartFrameThread() {
  if (!presenter_provider_ || frame_started_.exchange(true)) {
    return;
  }
  frame_stop_.store(false);
  frame_thread_ = std::thread([this] { FrameThreadMain(); });
}

void MouseUiInputDriver::StopFrameThread() {
  if (!frame_started_.exchange(false)) {
    return;
  }
  frame_stop_.store(true);
  if (frame_thread_.joinable()) {
    frame_thread_.join();
  }
}

double MouseUiInputDriver::NowMs() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void MouseUiInputDriver::LogBurstReport(const MenuHoverAligner::Report& report) {
  FrameReadback& readback = Readback();
  std::lock_guard lock(readback.log_mutex);
  if (!readback.log_open) {
    return;
  }
  readback.log << "H t=" << static_cast<int64_t>(NowMs()) << " outcome=" << report.outcome
               << " steps=" << report.steps << " presses=" << report.presses
               << " pitch=" << report.pitch << " bands=" << report.bands
               << " target=" << static_cast<int>(report.target_y)
               << " highlight=" << (report.have_highlight
                                        ? std::to_string(static_cast<int>(report.highlight_y))
                                        : std::string("none"))
               << '\n';
  readback.log.flush();
}

void MouseUiInputDriver::LogStep(const MenuHoverAligner::StepLog& step) {
  FrameReadback& readback = Readback();
  std::lock_guard lock(readback.log_mutex);
  if (!readback.log_open) {
    return;
  }
  readback.log << "S t=" << static_cast<int64_t>(NowMs()) << " attempt=" << step.attempt
               << " pulse=" << step.press_frames << "/" << step.release_frames
               << " frame_ms=" << static_cast<int>(step.frame_ms)
               << " poll_ms=" << static_cast<int>(step.poll_ms)
               << " dir=" << (step.direction < 0 ? "down" : "up")
               << " press_ms=" << static_cast<int>(step.press_ms)
               << " release_ms=" << static_cast<int>(step.release_ms)
               << " bands=" << step.bands << " moved=" << (step.moved ? 1 : 0)
               << (step.merged ? " merged" : "") << " pitch=" << step.pitch
               << " centre=" << static_cast<int>(step.centre)
               << " target=" << static_cast<int>(step.target) << " action=" << step.action
               << '\n';
  readback.log.flush();
}

void MouseUiInputDriver::FrameThreadMain() {
  FrameReadback& readback = Readback();
  const bool probe = REXCVAR_GET(mouse_ui_probe);
  const bool hover_log = REXCVAR_GET(mouse_ui_hover_log);
  const int32_t thumbnail_every = REXCVAR_GET(mouse_ui_probe_thumb_every);
  if (probe || hover_log) {
    std::lock_guard lock(readback.log_mutex);
    const std::string log_path = REXCVAR_GET(mouse_ui_probe_path);
    // The path is a cvar and its directory need not exist yet: the log is what the
    // measurements in this file come from, so it is worth making the way for it rather
    // than failing to open it.
    std::error_code error;
    const std::filesystem::path parent = std::filesystem::path(log_path).parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, error);
    }
    readback.log.open(log_path, std::ios::trunc);
    readback.log_open = readback.log.is_open();
    if (!readback.log_open) {
      REXLOG_WARN("mouse_ui: cannot write the guest-frame log to {}", log_path);
    } else {
      readback.log << "# mouse_ui guest-frame log: frames are read at "
                   << REXCVAR_GET(mouse_ui_idle_hz) << " Hz while idle and as fast as they arrive "
                   << "while a hover is being aligned; probe=" << (probe ? 1 : 0)
                   << " hover_log=" << (hover_log ? 1 : 0) << '\n';
      readback.log.flush();
    }
  }

  // The frame is kept across iterations so the readback writes into the same
  // allocation every time instead of churning megabytes a second.
  rex::ui::RawImage image;
  NavSampledFrame sampled;
  std::string line;
  double next_idle_ms = 0.0;
  while (!frame_stop_.load()) {
    const bool wanted = align_wanted_.load();
    const double now_ms = NowMs();
    if (IsEnabled() && (wanted || now_ms >= next_idle_ms)) {
      rex::ui::Presenter* presenter = presenter_provider_ ? presenter_provider_() : nullptr;
      if (presenter != nullptr) {
        const auto begin = std::chrono::steady_clock::now();
        const bool captured = presenter->CaptureGuestOutput(image);
        const double capture_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                .count();
        if (captured && image.width != 0 && image.height != 0) {
          const double read_ms = NowMs();
          // A frame that has not changed is a frame the guest has not drawn: nothing
          // to measure, nothing to hand over, and no reason to believe another one
          // is coming. While a hover is being decided, though, it is handed over all
          // the same: what the press is measured against has to be the screen as it is
          // now, and a frame that did not change is still the screen as it is now.
          guest_frame_seen_ms_.store(static_cast<int64_t>(read_ms));
          const bool changed = !SamePixels(readback, image);
          if (changed && readback.log_open && probe) {
            line.clear();
            const bool thumbnail =
                thumbnail_every > 0 &&
                readback.frame % static_cast<uint32_t>(thumbnail_every) == 0;
            AppendFrameLine(&readback, image, capture_ms, readback.frame, read_ms, thumbnail,
                            line);
            std::lock_guard lock(readback.log_mutex);
            readback.log << line << '\n';
            readback.log.flush();
          }
          if (changed || wanted) {
            guest_frame_width_.store(static_cast<int>(image.width));
            guest_frame_height_.store(static_cast<int>(image.height));
            NavFrameView view;
            view.pixels = image.data.data();
            view.width = static_cast<int>(image.width);
            view.height = static_cast<int>(image.height);
            view.stride = image.stride;
            SampleNavFrame(view, &sampled);
            {
              std::lock_guard lock(state_mutex_);
              aligner_.FeedFrame(sampled, read_ms);
            }
            if (readback.previous.size() != image.data.size()) {
              readback.previous.resize(image.data.size());
            }
            std::memcpy(readback.previous.data(), image.data.data(), image.data.size());
            readback.previous_width = image.width;
            readback.previous_height = image.height;
          }
          ++readback.frame;
          const double idle_hz = std::clamp(REXCVAR_GET(mouse_ui_idle_hz), 0.1, 60.0);
          next_idle_ms = read_ms + 1000.0 / idle_hz;
        }
      } else {
        // No presenter yet - the graphics system may not exist at all. Wait, and do
        // not spin on the callback while waiting.
        next_idle_ms = now_ms + kNoPresenterPauseMs;
      }
    }
    // A short slice rather than one long sleep: that is what lets a hover that
    // starts while this thread is idle get its frames read immediately instead of
    // after the idle interval, which is the difference between a hover that lands
    // now and one that lands a fifth of a second from now.
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(kFrameThreadSliceMs));
  }

  if (readback.log_open) {
    std::lock_guard lock(readback.log_mutex);
    readback.log << "# end after " << readback.frame << " frames\n";
    readback.log.flush();
    readback.log.close();
    readback.log_open = false;
  }
}

void MouseUiInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  // Disabled means no device at all, so it never occupies the guest user slot
  // it shares with the keyboard emulation.
  if (!IsEnabled()) {
    return;
  }
  DeviceInfo info;
  info.id = kDevice;
  info.name = "Mouse (menu navigation)";
  // What routes it to guest user 0 - see SlotAssignment::OnDevicesChanged.
  info.synthetic = true;
  out.push_back(info);
}

X_RESULT MouseUiInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                                   X_INPUT_CAPABILITIES* out_caps) {
  if (!IsEnabled() || id != kDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = 0x01;
    out_caps->sub_type = 0x01;
    out_caps->flags = 0;
    // Only A, B and the left stick are ever produced, but the caps mirror the
    // keyboard emulation's: a guest that sizes its input off the capability
    // mask must not conclude this pad is a stripped-down one.
    out_caps->gamepad.buttons = 0xFFFF;
    out_caps->gamepad.left_trigger = 0xFF;
    out_caps->gamepad.right_trigger = 0xFF;
    out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
    out_caps->vibration.left_motor_speed = 0xFFFF;
    out_caps->vibration.right_motor_speed = 0xFFFF;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MouseUiInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  if (!IsEnabled() || id != kDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // How fast the guest is reading this device decides how many polls a press has
  // to last to be one the guest can see. See UiNavStepper::SetPulsePolls. Only the
  // travel fallback needs this: the aligner's pulses end when the frames the guest
  // drew show them, whatever its frame rate is.
  const double now_ms = NowMs();
  if (has_poll_time_) {
    const double interval_ms = now_ms - last_poll_ms_;
    if (interval_ms > 0.0 && interval_ms <= kMaxPollIntervalMs) {
      poll_interval_ms_ = poll_interval_ms_ > 0.0
                              ? poll_interval_ms_ * (1.0 - kPollIntervalSmoothing) +
                                    interval_ms * kPollIntervalSmoothing
                              : interval_ms;
    }
  }
  last_poll_ms_ = now_ms;
  has_poll_time_ = true;

  const int press_polls = PollsForMilliseconds(REXCVAR_GET(mouse_ui_press_ms), poll_interval_ms_);
  const int release_polls =
      PollsForMilliseconds(REXCVAR_GET(mouse_ui_release_ms), poll_interval_ms_);

  int16_t lx = 0;
  int16_t ly = 0;
  uint16_t buttons = 0;
  bool have_report = false;
  MenuHoverAligner::Report report;
  {
    std::lock_guard lock(state_mutex_);
    nav_.SetPulsePolls(press_polls, release_polls);
    clicks_.SetPulsePolls(press_polls, release_polls);
    if (is_active() && has_focus_) {
      // A hover is measured from the guest's own frames and timed by them, so which
      // of the two paths is in charge is decided by whether there are any: a frame
      // the guest drew recently, and a window whose pixels the pointer's can be
      // mapped through.
      const int64_t seen_ms = guest_frame_seen_ms_.load();
      const bool frames_fresh =
          seen_ms > 0 && now_ms - static_cast<double>(seen_ms) < kFrameStaleMs;
      const bool aligning = REXCVAR_GET(mouse_ui_hover) && frames_fresh && RefreshMapping();
      if (aligning != using_aligner_) {
        // Switching paths starts a new gesture: the rows the fallback had queued, or
        // the hover the aligner was following, were measured in the other path's
        // pixels.
        using_aligner_ = aligning;
        nav_.ForgetPointer();
        aligner_.ClearTarget();
      }
      if (aligning) {
        // The guest asks for the pad state once per frame, which is the only direct
        // measure of its frame interval there is.
        aligner_.SetGuestFrameMs(poll_interval_ms_);
        ly = aligner_.PollStick(now_ms);
      } else {
        nav_.Poll(&lx, &ly);
      }
      // Tell the frame thread whether to be reading at the guest's rate: a press
      // cannot be measured against a frame read before the pointer even rested, so a
      // hover that has not got one yet is worth reading for.
      align_wanted_.store(aligning && aligner_.WantsFrames(now_ms));
      // A click waits for the selection to arrive under the pointer, so that it
      // lands on the row the hand is on - but not for longer than a click can be
      // held, whatever is stuck.
      const bool align_busy = aligning && aligner_.Busy();
      if (align_busy || nav_.HasPendingRows()) {
        if (!click_waiting_) {
          click_waiting_ = true;
          click_wait_started_ms_ = now_ms;
        }
      } else {
        click_waiting_ = false;
      }
      const bool waited_too_long =
          click_waiting_ && now_ms - click_wait_started_ms_ > kClickAlignTimeoutMs;
      const uint16_t pressed =
          clicks_.Poll(nav_.HasPendingRows() || (align_busy && !waited_too_long));
      if (pressed & UiClickPulser::MaskFor(kLeftClick)) {
        buttons |= rex::input::X_INPUT_GAMEPAD_A;
      }
      if (pressed & UiClickPulser::MaskFor(kRightClick)) {
        buttons |= rex::input::X_INPUT_GAMEPAD_B;
      }
      have_report = aligner_.TakeReport(&report);
    } else {
      // Travel and clicks gathered while an overlay owned the pointer, or while
      // the window was in the background, are dropped rather than replayed into
      // the guest once it is in charge again.
      nav_.Reset();
      clicks_.Reset();
      aligner_.ForgetBurst();
      align_wanted_.store(false);
      click_waiting_ = false;
      have_report = aligner_.TakeReport(&report);
    }
  }

  // Outside the lock: this is a file write, and no pad state waits for it.
  if (have_report) {
    LogBurstReport(report);
  }

  // A real pad only advances the packet number when the state really moved, and
  // the guest uses that to tell a fresh reading from a repeat of the last one.
  if (buttons != last_buttons_ || lx != last_lx_ || ly != last_ly_) {
    last_buttons_ = buttons;
    last_lx_ = lx;
    last_ly_ = ly;
    ++packet_number_;
  }

  if (out_state) {
    std::memset(out_state, 0, sizeof(*out_state));
    out_state->packet_number = packet_number_;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.thumb_lx = lx;
    out_state->gamepad.thumb_ly = ly;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MouseUiInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  if (!IsEnabled() || id != kDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MouseUiInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags,
                                                X_INPUT_KEYSTROKE* out_keystroke) {
  if (!IsEnabled() || id != kDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  // Buttons are reported as pad state, not as keystrokes: XInputGetKeystroke is
  // what the guest uses for text entry, and the mouse produces no text.
  return X_ERROR_EMPTY;
}

void MouseUiInputDriver::OnMouseDown(rex::ui::MouseEvent& e) {
  if (!IsEnabled() || !has_focus_ || !is_active()) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  RefreshMapping();
  // A click usually changes the screen, so the next position the pointer is
  // reported at is the start of a new gesture rather than travel from wherever it
  // was when the mouse was last over a menu.
  nav_.ForgetPointer();
  // And it is meant for the row the pointer is over, so a hover that has not
  // finished its rest is aligned now rather than a hover's delay from now.
  aligner_.AlignNow(NowMs());
  switch (e.button()) {
    case rex::ui::MouseEvent::Button::kLeft:
      clicks_.OnButtonDown(kLeftClick);
      break;
    case rex::ui::MouseEvent::Button::kRight:
      clicks_.OnButtonDown(kRightClick);
      break;
    default:
      break;
  }
}

void MouseUiInputDriver::OnMouseUp(rex::ui::MouseEvent& e) {
  if (!IsEnabled()) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  switch (e.button()) {
    case rex::ui::MouseEvent::Button::kLeft:
      clicks_.OnButtonUp(kLeftClick);
      break;
    case rex::ui::MouseEvent::Button::kRight:
      clicks_.OnButtonUp(kRightClick);
      break;
    default:
      break;
  }
}

void MouseUiInputDriver::OnMouseMove(rex::ui::MouseEvent& e) {
  if (!IsEnabled() || !has_focus_ || !is_active()) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  const double x = static_cast<double>(e.x());
  const double y = static_cast<double>(e.y());
  // SetRelativeMouseMode, which mouse look turns on, freezes x/y and reports only
  // deltas. There is no position to hover with then, so that case is the fallback's.
  const bool delta_motion =
      have_motion_event_ && e.dx() != 0.0f && x == last_event_x_ && y == last_event_y_;
  have_motion_event_ = true;
  last_event_x_ = x;
  last_event_y_ = y;

  UpdateRowPixels();
  RefreshMapping();
  const bool can_align = REXCVAR_GET(mouse_ui_hover) && mapping_.valid && !delta_motion;
  if (can_align != using_aligner_) {
    // Switching paths starts a new gesture rather than joining the old one: the
    // rows the fallback had queued, or the row the aligner was aiming at, were
    // measured in the other path's pixels.
    using_aligner_ = can_align;
    nav_.ForgetPointer();
    aligner_.ClearTarget();
  }
  if (can_align) {
    have_pointer_ = true;
    pointer_x_ = x;
    pointer_y_ = y;
    // The aligner works in the guest's pixels, because that is what the highlight it
    // measures against is drawn in.
    aligner_.SetTargetY(mapping_.ClientToGuestY(y), NowMs());
    return;
  }
  if (delta_motion) {
    // A delta is a sum of whatever the platform rounded each event to, which drifts
    // against a fixed scale - but it is the only motion there is under mouse look.
    nav_.OnPointerMotion(static_cast<double>(e.dx()), static_cast<double>(e.dy()));
  } else {
    // Absolute positions rather than deltas, which is what the fallback has when
    // there is no frame to measure the pointer's row against.
    nav_.OnPointer(x, y);
  }
}

void MouseUiInputDriver::OnGotFocus(rex::ui::UISetupEvent&) {
  has_focus_ = true;
  // The pointer may have moved over other windows while this one was unfocused,
  // and none of that travel is meant for the menu.
  std::lock_guard lock(state_mutex_);
  nav_.ForgetPointer();
  aligner_.ClearTarget();
  have_pointer_ = false;
  click_waiting_ = false;
}

void MouseUiInputDriver::OnLostFocus(rex::ui::UISetupEvent&) {
  has_focus_ = false;
  // A button released while unfocused never delivers its up event, and neither
  // does a click whose press has not been emitted yet: nothing in flight here is
  // meant for the menu. The guest's own screen is untouched by the focus going
  // away, so what was measured on it - the pitch, the row the highlight is on - is
  // kept: the pointer comes back to the same menu, and measuring it again would cost
  // a press and a visible flick of the selection.
  std::lock_guard lock(state_mutex_);
  clicks_.Reset();
  nav_.Reset();
  aligner_.ForgetBurst();
  align_wanted_.store(false);
  have_pointer_ = false;
  click_waiting_ = false;
}

void MouseUiInputDriver::OnResize(rex::ui::UISetupEvent&) {
  // A row is a fraction of the window's height, so resizing changes the pixels it
  // is measured in, and anything already queued was queued in the old ones. The
  // guest's own frames are unchanged, so what the aligner measured on the screen
  // survives - only the pointer has to be mapped into it again.
  std::lock_guard lock(state_mutex_);
  UpdateRowPixels();
  nav_.Reset();
  RefreshMapping();
}

void MouseUiInputDriver::OnDpiChanged(rex::ui::UISetupEvent&) {
  // The same pixels now mean a different distance on the glass.
  std::lock_guard lock(state_mutex_);
  UpdateRowPixels();
  nav_.Reset();
  RefreshMapping();
}

void InstallMouseUiNavigation(rex::RuntimeConfig& config,
                              std::function<rex::ui::Presenter*()> presenter_provider) {
  // Tool mode has no window and no guest to steer.
  if (config.tool_mode) {
    return;
  }

  std::function<std::unique_ptr<rex::system::IInputSystem>(bool)> backend;
  if (config.input_factory) {
    backend = std::move(config.input_factory);
  } else {
    backend = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
  }

  config.input_factory = [backend = std::move(backend),
                          presenter_provider = std::move(presenter_provider)](bool tool_mode) {
    std::unique_ptr<rex::system::IInputSystem> system = backend(tool_mode);
    if (tool_mode || !system) {
      return system;
    }
    // ReXApp casts this interface to InputSystem on the way to AttachWindow, so
    // anything else could not have worked anyway; CreateDefaultInputSystem, the
    // only backend the SDK and this project ship, returns one.
    static_cast<rex::input::InputSystem*>(system.get())
        ->AddDriver(std::make_unique<MouseUiInputDriver>(presenter_provider));
    REXLOG_INFO("mouse_ui: the mouse navigates the menus (hover alignment {}, a row of travel per "
                "{} of the window's height as a fallback, --no-mouse_ui_nav to disable)",
                REXCVAR_GET(mouse_ui_hover), REXCVAR_GET(mouse_ui_row_fraction));
    return system;
  };
}

}  // namespace rb_blitz::input
