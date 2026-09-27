// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The decidable part of the mouse -> menu bridge in src/input/mouse_ui.{h,cpp}:
// turning the pointer's position into the discrete left-stick presses the guest
// menus actually read, and deciding how many of them a hover is worth.
//
// Why presses and not a held deflection. The menus are not pointer-driven: there
// is no guest cursor to move and no "activate what is under the pointer" call to
// make. What the game does read is the pad, one row per left-stick press, clamped
// at both ends and non-wrapping (docs/history/bringup-log.md, 2026-09-22). So the only
// faithful translation of a mouse is to emit the kind of press a thumb
// would - what the mouse decides is how many of them and when.
//
// Why the pointer's position can say which row is selected after all. Nothing the
// guest is asked reports where its highlight is, but the guest is asked for its own
// frames (Presenter::CaptureGuestOutput), and a menu step moves the highlight
// inside them: the difference between the frame before a press and the frame after
// it holds the highlight's old rectangle and its new one (src/input/nav_detect.h).
// That measures both where the selection is and how far apart two rows are on the
// screen being looked at, so the offset between the pointer and the highlight
// stops being untouchable - MenuHoverAligner below counts the rows between them
// and presses until they are the same row.
//
// Why not simply point. Because the guest moves on presses, and the shortest pulse
// it can see costs a frame with the stick deflected and a frame without it. A hover
// therefore arrives as that many presses, in as few frames as the guest can follow:
// a fast slide along the list rather than an instant jump, and a hover of one row
// is a single press. What it never does is leave the selection a fixed number of
// rows away from the pointer, which is what a pointer that could only add its own
// travel to the selection did.
//
// Why the pixels are measured and not chosen. The pitch between two rows is a fact
// about the guest's layout, and it is a different fact on every screen: the main
// menu measured 27 px a row in a 768-px-tall client, MOD SETTINGS 40 and the song
// list 96 (docs/history/bringup-log.md, 2026-09-22). A chosen pitch is a selection
// that gains or loses a row per gesture, so the aligner measures the pitch on
// whichever screen it is stepping on, from the first press that moves anything, and
// re-measures it as it goes.
//
// Why the travel stepper is still here. Measuring needs frames, and there are three
// ways to have none: no presenter at all (another graphics backend), a window whose
// guest output has not been captured yet, and mouse look, which freezes the
// pointer's position and reports only deltas. In those the mouse still navigates
// the way it did before - a row of travel is a row, in either direction, which
// keeps the selection under the pointer for as long as the pointer is travelling
// along the list. There is no wheel: the selection follows the pointer's position
// now, so a wheel has nothing to say that a pointer over a row does not say better.
//
// Why a click is delivered late. A press takes frames to be seen and a hover is
// several of them, so a click reported the moment the button goes down activates
// whichever row the selection happens to be passing through, not the row the hand
// stopped on. The two halves of the bridge therefore have to be ordered rather than
// independent: UiClickPulser holds a click back until both the aligner and the
// travel stepper have stopped moving the selection.
//
// Kept free of any SDK dependency for the same reason src/fs/path_policy.h is -
// the interesting part is arithmetic over pixel counters, poll counts and frame
// differences, and tests/ui_nav_tests.cpp has to cover it without booting the game
// (docs/rb3-references.md Â§7.3).

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "input/nav_detect.h"

namespace rb_blitz::input {

class UiNavStepper {
 public:
  // Full deflection, the magnitude the keyboard bindings use for the same
  // presses. The guest applies its own deadzone; nothing subtler is needed.
  static constexpr int kStepDeflection = 32767;

  // One press is the deflection held for press_polls() polls and then released
  // for release_polls().
  //
  // These are poll counts only because this class has no clock; what the guest
  // reacts to is time, and the driver converts from it (SetPulsePolls), so the
  // values below are no more than the defaults the unit tests use. Time matters
  // because the guest's two clocks are far apart: its menus advance on its frame
  // clock, while the pad is read at the device's poll rate, which the guest log
  // puts at ~420 polls a second here (docs/history/bringup-log.md, 2026-09-22). A press
  // counted in polls therefore shrinks as the poll rate climbs, and at 420 Hz a
  // three-poll press is 7 ms - less than one guest frame, which is why a burst
  // of steps used to arrive at the menu as a single step.
  static constexpr int kDefaultPressPolls = 3;
  static constexpr int kDefaultReleasePolls = 2;

  // No pulse is longer than this, whatever a caller or a cvar asks for: a press
  // that outlives a human's patience is a stuck stick.
  static constexpr int kMaxPulsePolls = 10000;

  // Retimes the pulse, in polls. The press is never shorter than one poll, since
  // a deflection of no polls is not a press at all, and the release may be zero
  // for callers that want back-to-back presses.
  void SetPulsePolls(int press_polls, int release_polls);
  int press_polls() const { return press_polls_; }
  int release_polls() const { return release_polls_; }

  // Pointer travel that moves the selection by one row, in physical pixels of the
  // client area. The guest's pitch is per screen - in a 768-px-tall client the
  // main menu measured 27 px a row, MOD SETTINGS 40 and the song list 96, since
  // its rows are 48 px apart but every second one is an artist heading the
  // selection skips (docs/history/bringup-log.md, 2026-09-22) - so no single value is
  // exactly one row on every screen. This is 32 px, the menu side of that range,
  // and src/input/mouse_ui.cpp scales it to the window it is actually looking at,
  // which is why the number here is only the default the unit tests use. It is the
  // fallback's number: MenuHoverAligner measures the real pitch instead, and this
  // is what the mouse uses when there is no frame to measure.
  static constexpr double kDefaultRowPixels = 32.0;
  // Bounds for the cvar. Below the minimum a row would be a few pixels of hand
  // shake, and above the maximum a whole row would be a gesture.
  static constexpr double kMinRowPixels = 8.0;
  static constexpr double kMaxRowPixels = 400.0;

  // Rows one gesture may queue, so that a violent flick cannot keep the menu
  // moving for seconds after the hand has stopped. The bound is per gesture and
  // not per row: 24 rows is more than a screenful at the measured pitch, so a
  // pointer thrown across the window still lands as the single jump it was meant
  // to be.
  static constexpr int kMaxQueuedRows = 24;

  void SetRowPixels(double pixels);
  double row_pixels() const { return row_pixels_; }

  // The pointer's position in the client area, in physical pixels - the same
  // units as rex::ui::MouseEvent::x/y and Window::GetActualPhysicalHeight.
  //
  // The first call after Reset() or ForgetPointer() only establishes a starting
  // point and queues nothing: where the pointer happens to be when the driver
  // starts looking says nothing about where the selection is, and treating it as
  // if it did would move the selection by the whole distance to that guess.
  void OnPointer(double x, double y);

  // Pointer motion for the case where x/y stop moving: SetRelativeMouseMode
  // freezes them for mouse look while the deltas keep coming. The first call only
  // establishes a starting point, like OnPointer's.
  void OnPointerMotion(double dx, double dy);

  // The left-stick value for one guest poll. Writes 0,0 when nothing is due.
  void Poll(int16_t* out_x, int16_t* out_y);

  // Whether a row the user asked for is still owed to the guest: queued travel, or
  // a press that is still being served. False means the guest has seen everything
  // the pointer did, which is when a click may be delivered - UiClickPulser::Poll
  // takes this as its argument.
  bool HasPendingRows() const;

  // Drops the starting point and the part of a row that has not been reached, so
  // that the next position is only a starting point again. For a click, which
  // usually changes the screen, and for a resize or a DPI change, which change
  // the pixels rows are measured in. Rows already queued are kept: they are
  // motion the user asked for, not a guess, and a click is not a reason to
  // swallow them.
  void ForgetPointer();

  // ForgetPointer, plus queued rows and any press in flight.
  void Reset();

 private:
  bool TakeStep(int16_t* out_x, int16_t* out_y);
  void QueueRows(double dx, double dy);
  void QueueRowsOnAxis(double pixels, double* residual, double* queued);

  double row_pixels_ = kDefaultRowPixels;

  // The last position seen, in pixels, and the part of a move that was too small
  // to be a row - carried rather than dropped, so that a row walked in ten small
  // steps still arrives and a wiggle that nets to nothing still does nothing.
  bool have_pointer_ = false;
  double pointer_x_ = 0.0;
  double pointer_y_ = 0.0;
  double residual_x_ = 0.0;
  double residual_y_ = 0.0;

  // Rows not yet delivered as presses, signed the way the stick is: +x is right,
  // +y is up.
  double queued_x_ = 0.0;
  double queued_y_ = 0.0;

  int press_polls_ = kDefaultPressPolls;
  int release_polls_ = kDefaultReleasePolls;
  int press_polls_left_ = 0;
  int release_polls_left_ = 0;
  int16_t press_x_ = 0;
  int16_t press_y_ = 0;
};

// Where the guest's image sits inside the window's client area, in physical
// pixels: the same aspect-preserving scale, centring and letterbox the presenter
// itself uses (rexglue-sdk/src/ui/presenter.cpp, GetGuestOutputPaintFlow, with the
// overscan cutoff off, which is how this app configures it). The pointer is
// reported in client pixels and the highlight is measured in guest pixels, so
// something has to make the two comparable before they can be counted against each
// other; this is that something.
struct GuestImageMapping {
  double scale = 1.0;
  double offset_x = 0.0;
  double offset_y = 0.0;
  bool valid = false;

  double ClientToGuestY(double client_y) const { return (client_y - offset_y) / scale; }
  double ClientToGuestX(double client_x) const { return (client_x - offset_x) / scale; }
  double GuestToClientY(double guest_y) const { return guest_y * scale + offset_y; }
};

inline GuestImageMapping ComputeGuestImageMapping(int client_width, int client_height,
                                                  int guest_width, int guest_height,
                                                  bool letterbox) {
  GuestImageMapping mapping;
  if (client_width <= 0 || client_height <= 0 || guest_width <= 0 || guest_height <= 0) {
    return mapping;
  }
  const double client_w = static_cast<double>(client_width);
  const double client_h = static_cast<double>(client_height);
  const double guest_w = static_cast<double>(guest_width);
  const double guest_h = static_cast<double>(guest_height);

  double content_w = client_w;
  double content_h = client_h;
  if (letterbox) {
    // The whole image is visible, filling one axis exactly and leaving black bars
    // along the other - which is what the presenter does unless the user has asked
    // it not to.
    const double fit = std::min(client_w / guest_w, client_h / guest_h);
    content_w = guest_w * fit;
    content_h = guest_h * fit;
  }
  mapping.scale = content_h / guest_h;
  // The presenter centres with a truncating division of the leftover pixels.
  mapping.offset_x = std::floor((client_w - content_w) * 0.5);
  mapping.offset_y = std::floor((client_h - content_h) * 0.5);
  mapping.valid = true;
  return mapping;
}

// The half of the bridge that closes the distance: which row the pointer is on,
// how far the selection is from it, and the presses that make up the difference.
//
// Why the pulses are measured in the guest's frames rather than in milliseconds.
// The guest moves its highlight when one of its own frames reads the stick, and no
// host clock knows when that is. A press shorter than a frame is a press the guest
// never sees - which is how a burst of steps used to arrive at the menu as a single
// step - and a press of a fixed number of milliseconds is a different number of
// frames on every machine. So the stick is held for a couple of the guest's own
// frames and released for one and a half of them, and the frame interval itself is
// measured from the frames the guest has drawn.
//
// Why the phases are timed rather than ended by the picture changing. The picture
// changing says a frame was drawn, but not *which* press it was drawn for: on a
// screen where the previous measurement's move had only just been read back, the
// next press would see that older move, measure a step that this press did not
// make, and leave the real move to be measured by the press after it. That is a
// step measured twice and a press that measured nothing, alternating - which is
// exactly what it looked like in the log. A phase that lasts a fixed number of
// frames cannot read an older frame's change as its own, because by the time it
// ends the frame the guest drew for it has certainly been read.
//
// Why the press grows only after the gap has. A press shorter than the guest reacts
// to is one way to move nothing; a cooldown between two steps is the other. So a
// press that moved nothing is followed by another press of the same length with a
// longer gap, which can only help and cannot turn one step into two, and only after
// that does the press itself grow - still short of the auto-repeat a human hold would
// start. When none of that moves anything, the selection is at the end of its list
// and the burst is over until the pointer moves again.
//
// Why the direction can be guessed. Which way the selection moves relative to the
// stick is a property of the game rather than of a screen, so downward is the first
// guess on a screen that has never been stepped on, and the difference that press
// produces says whether the guess was right: a measured pitch is a measured
// direction. A wrong guess costs one press before the measurement corrects it.
//
// Why the burst is closed-loop rather than counted. The pitch is known only after a
// press, the pointer may be moved mid-burst, and a list may end short of the row
// the pointer is over. Following every press with a measurement means the burst
// stops on the row the pointer is on rather than on the row an estimate predicted,
// and it stops at the end of a list without pressing into the wall.
class MenuHoverAligner {
 public:
  // Full deflection, as the travel stepper uses: the guest applies its own
  // deadzone, and nothing subtler is needed.
  static constexpr int kDeflection = UiNavStepper::kStepDeflection;

  // Stick signs. +y is up in the pad state the guest reads, and up moves the
  // selection up the list and up the screen.
  static constexpr int kStickUp = 1;
  static constexpr int kStickDown = -1;

  // The most presses one hover may cost, as a backstop: the aligner's own estimate
  // of the rows left to cross is normally far smaller than this.
  static constexpr int kMaxSteps = 32;

  // How many screens may be replaced under one burst before it gives up. A screen
  // change invalidates everything measured, and two of them in one hover is not a
  // list being stepped through any more.
  static constexpr int kMaxScreenChanges = 2;

  // The stick's waveform per attempt at one step, in the guest's frames: a press
  // long enough for any guest to see it twice over, then a gap long enough that the
  // next press is a new press rather than the same hold. The later attempts lengthen
  // the gap before they lengthen the press, because a guest that wants a settled
  // stick is likelier than one that wants a longer press, and a press long enough to
  // auto-repeat is a step the next measurement has to undo - which the menus were
  // measured to be free of up to a third of a second.
  struct Pulse {
    double press_frames;
    double release_frames;
  };
  static constexpr int kAttempts = 4;
  static constexpr Pulse kAttemptPulses[kAttempts] = {
      {2.0, 2.0}, {2.0, 3.0}, {3.0, 3.0}, {4.0, 4.0}};

  // What is added to a phase beyond its frames: the host's own overhead, and the
  // guest's own latency between reading the stick and drawing what came of it, which
  // does not shrink with the guest's frame rate.
  static constexpr double kPressSlackMs = 15.0;
  static constexpr double kReleaseSlackMs = 15.0;
  static constexpr double kMinPressMs = 40.0;
  static constexpr double kMinReleaseMs = 40.0;
  static constexpr double kMaxPhaseMs = 250.0;

  // How long the pointer rests before the selection follows it. Shorter than a
  // deliberate hover and longer than the gaps inside one movement event, so that
  // sweeping across a list costs one burst at the end of the sweep rather than one
  // per row crossed.
  static constexpr double kDefaultHoverDelayMs = 80.0;
  static constexpr double kMinHoverDelayMs = 0.0;
  static constexpr double kMaxHoverDelayMs = 2000.0;

  // A pointer that has moved less than this is still on the same row, so it does
  // not restart the hover wait - a hand is never perfectly still, and a hover that
  // never matures is a mouse that never moves the selection. Used only until a
  // pitch has been measured.
  static constexpr double kDefaultTargetEpsilon = 6.0;

  // What a guest frame is assumed to cost until the guest's own rate is known: 60 Hz,
  // the rate every one of these games runs at or above. The frame interval is never
  // taken to be shorter than this, because the frames read back can differ on every
  // capture whether or not the guest has drawn a new one, and a press timed from that
  // would be a press the guest could miss.
  static constexpr double kDefaultFrameIntervalMs = 16.7;
  static constexpr double kMinFrameIntervalMs = 16.7;
  static constexpr double kMaxFrameIntervalMs = 60.0;

  // The shortest window in which a read frame counts as the screen as it is now - and
  // the time the frame thread is asked to keep reading after a hover starts, since a
  // frame read before it cannot be measured against.
  static constexpr double kMinFreshFrameMs = 60.0;

  // What the last burst did, for the driver's log: the instrument that says whether
  // a screen is one this can measure at all.
  struct Report {
    const char* outcome = "idle";
    int steps = 0;
    int presses = 0;
    int pitch = 0;
    int bands = 0;
    double target_y = 0.0;
    double highlight_y = 0.0;
    bool have_highlight = false;
  };

  // The same instrument, one line per press rather than per burst: what a press was
  // shaped like, what the difference showed, and what was done about it. A press
  // that moves two rows instead of one and a press that moves nothing at all are
  // otherwise indistinguishable from the outcome alone.
  struct StepLog {
    int attempt = 0;
    double press_frames = 0.0;
    double release_frames = 0.0;
    int bands = 0;
    bool moved = false;
    bool merged = false;
    int pitch = 0;
    int direction = 0;
    double centre = 0.0;
    double target = 0.0;
    // How long the press and the gap that followed it actually lasted, and what the
    // guest's frames were taken to cost when they were timed. A press the guest
    // ignored and a press it drew but that moved nothing are otherwise the same line.
    double press_ms = 0.0;
    double release_ms = 0.0;
    double frame_ms = 0.0;
    double poll_ms = 0.0;
    const char* action = "";
  };
  void SetStepLogger(std::function<void(const StepLog&)> logger) {
    step_logger_ = std::move(logger);
  }

  void SetHoverDelayMs(double milliseconds);
  double hover_delay_ms() const { return hover_delay_ms_; }

  // How often the guest has been asking for the pad state, which is once per frame on
  // every guest seen so far: the frame interval the presses are timed in, to the
  // extent it is known. The frames read back cannot answer this on their own, because
  // a capture can differ from the last one without the guest having drawn anything.
  void SetGuestFrameMs(double milliseconds) {
    if (std::isfinite(milliseconds) && milliseconds > 0.0) {
      guest_frame_ms_ = milliseconds;
    }
  }
  void SetDetectConfig(const NavDetectConfig& config) {
    detect_ = config;
    detect_.max_bands = std::clamp(detect_.max_bands, 0, kMaxDetectBands);
    detect_.min_row_fraction_percent = std::clamp(detect_.min_row_fraction_percent, 1, 100);
  }
  const NavDetectConfig& detect_config() const { return detect_; }

  // Everything measured about the screen - the frames, the pitch, where the
  // highlight is - and any burst in flight. For a new window, a frame of a different
  // size, or a lost focus long enough that the screen may be another one. The
  // pointer's position is not part of the screen, so it is kept.
  void Forget();

  // The screen is the same one, but the gesture is over: any burst in flight is
  // dropped and the pointer no longer wants anything, while everything measured
  // about the screen stays - losing the focus, or the pointer leaving the window,
  // does not move the guest's highlight anywhere, and measuring it again costs a
  // press.
  void ForgetBurst();

  // The pointer is no longer over anything this can align to: a relative-motion
  // event, a window without frames, a pointer that left the window.
  void ClearTarget();

  // Where on the guest's screen the pointer is, in guest pixels, and when. A
  // position that differs from the last one by less than a fraction of a row is
  // still the same hover as far as the wait is concerned.
  void SetTargetY(double guest_y, double now_ms);

  // A click: do not make the user wait out the hover delay, the click is on the row
  // the pointer is on now.
  void AlignNow(double now_ms);

  // One guest poll: advances whatever is due and returns the left-stick y for it.
  int16_t PollStick(double now_ms);

  // A frame the guest has just drawn, sampled by the caller so that the sampling
  // cost is not paid inside the driver's lock. Only frames the driver saw change
  // are handed over.
  void FeedFrame(const NavSampledFrame& frame, double now_ms);

  // A burst is in flight or about to start: a click has to wait for it.
  bool Busy() const { return phase_ != Phase::kIdle; }
  // Whether the driver's frame thread is worth reading at the guest's own rate right
  // now: while a burst is in flight, and while a hover is waiting with a frame too old
  // to measure from. The frame a press is measured against has to be one that was read
  // after the pointer asked for it - a frame read a fifth of a second ago can be a
  // frame from before the last move the guest made, and the difference would then hold
  // two moves instead of one.
  bool WantsFrames(double now_ms) const {
    if (phase_ != Phase::kIdle) {
      return true;
    }
    return have_target_ && !blocked_ && !Aligned() && !FrameIsFresh(now_ms);
  }
  // Whether a pitch and a highlight position have been measured on this screen.
  bool HaveGeometry() const { return have_highlight_ && pitch_ > 0; }
  int pitch() const { return pitch_; }
  double highlight_centre() const { return highlight_centre_; }

  // The report of the last finished burst, once.
  bool TakeReport(Report* out);

 private:
  enum class Phase {
    // Nothing wanted; the pointer's next hover may start a burst.
    kIdle,
    // The stick is deflected and the guest has not drawn it yet.
    kPress,
    // The stick is up and the guest has not drawn that yet either.
    kRelease,
  };

  bool WantsAlignment(double now_ms) const;
  bool Aligned() const;
  int DirectionForTarget() const;
  // How long the phase now in flight is to last, and how long each phase of the
  // current attempt is: the guest's frame interval times the attempt's frames, plus
  // the host's own overhead.
  double PhaseMs() const;
  double PressMs() const;
  double ReleaseMs() const;
  double FrameIntervalMs() const;
  // Whether the newest frame was read recently enough to measure a press against.
  bool FrameIsFresh(double now_ms) const;
  // Whether a pitch measured from a step is a measurement of the pitch at all.
  bool AcceptsPitch(int measured) const;
  double TargetEpsilon() const;
  void BeginBurst(double now_ms);
  void BeginStep(double now_ms, int direction);
  void AdvancePhase(double now_ms);
  void Evaluate(double now_ms);
  void EndBurst(const char* outcome);
  void UpdateBudget();

  NavDetectConfig detect_;

  // The frames: the newest one the guest drew and the one before it, plus the frame
  // the current press is being measured from. All are for the capture thread, and
  // are only ever touched under the driver's lock along with the rest of this.
  NavSampledFrame current_;
  NavSampledFrame previous_;
  NavSampledFrame press_base_;
  bool previous_valid_ = false;
  std::vector<NavRowDiff> rows_;
  std::array<NavBand, kMaxDetectBands> bands_{};
  bool have_frame_ = false;
  bool have_base_ = false;
  double frame_ms_ = 0.0;
  // How long a frame of the guest's costs, from the two things that can say: the rate
  // it asks for the pad state at, and the shortest gap between two frames read back.
  double guest_frame_ms_ = 0.0;
  double frame_interval_ms_ = 0.0;

  // What has been measured on this screen. 'centre' is the middle of the highlight
  // in guest pixels, which is the middle of the row the guest has selected.
  bool have_highlight_ = false;
  double highlight_centre_ = 0.0;
  int pitch_ = 0;

  // What the pointer wants.
  bool have_target_ = false;
  double target_y_ = 0.0;
  double target_ms_ = 0.0;

  Phase phase_ = Phase::kIdle;
  double phase_ms_ = 0.0;
  double press_ms_ = 0.0;
  double release_ms_ = 0.0;
  int direction_ = kStickDown;
  int attempt_ = 0;
  int steps_ = 0;
  int presses_ = 0;
  int budget_ = kMaxSteps;
  int screen_changes_ = 0;
  bool flipped_ = false;
  bool blocked_ = false;
  double hover_delay_ms_ = kDefaultHoverDelayMs;
  std::function<void(const StepLog&)> step_logger_;
  Report report_;
  bool report_ready_ = false;
  int bands_this_step_ = 0;
};

// The other half of the bridge: turning mouse clicks into the short A and B
// presses the guest reads, in an order the guest's frames can follow.
//
// Why a click waits for the walk. A click and the travel that led to it arrive as
// separate events and are served on different polls, but the guest only moves its
// highlight when one of its own frames reads the stick - so a click let through
// while rows are still queued activates whichever row the walk has reached rather
// than the one the hand was on, and rows still queued when the screen changes are
// played into whatever menu comes next. Both halves of that are what "sometimes
// there is an offset, sometimes it does not work" was. Waiting until the stepper
// has nothing left to deliver costs the tail of the last row's press - tens of
// milliseconds - and nothing at all when the hand is already still, which is when
// a click is made.
//
// Why a click is owed rather than passed through. A press shorter than the guest's
// frame is a press the guest never sees, and deferring a click widens that window
// rather than narrowing it: a five-millisecond click, released long before the
// deferred press could have been emitted, would otherwise vanish. So the press the
// user made is remembered until it has been emitted, however brief the physical
// one was, and a button released early is a click that is delivered late rather
// than one that is lost.
//
// Two clicks closer together than one press and its gap are one click here, not
// two: that is finer than the guest can tell apart anyway, and a double-click that
// arrived as two presses would activate a menu item and then whatever the next
// screen put under the selection.
class UiClickPulser {
 public:
  // One button each, in the driver's numbering: index 0 is the left button and the
  // guest's A, 1 the right and the guest's B. A poll reports them as a bit per
  // index, which is the driver's to map onto whatever the guest calls them.
  static constexpr int kButtonCount = 2;
  static constexpr uint16_t MaskFor(int index) {
    return static_cast<uint16_t>(1u << index);
  }

  // As UiNavStepper::SetPulsePolls, except that the release is at least one poll:
  // two clicks a hand apart are two presses, and no release at all would report
  // them as one unbroken one, which the guest's edge-detecting menus read as a
  // single click.
  void SetPulsePolls(int press_polls, int release_polls);
  int press_polls() const { return press_polls_; }
  int release_polls() const { return release_polls_; }

  // Buttons outside the two are ignored: the driver has no third button, so an
  // index it did not produce is a bug rather than input.
  void OnButtonDown(int index);
  void OnButtonUp(int index);

  // The buttons to report for one guest poll, one bit per button index, as
  // MaskFor. travel_pending is UiNavStepper::HasPendingRows(): while it is true, a
  // click that has not been emitted yet stays owed.
  uint16_t Poll(bool travel_pending);

  // ForgetPointer's counterpart for clicks: nothing is owed, nothing is pressed
  // and nothing is owed a gap. For a focused-out window, whose releases are never
  // delivered, and for a screen change the driver did not ask for.
  void Reset();

 private:
  struct Button {
    // The physical button, which is what decides how long a press lasts once it
    // has started.
    bool held = false;
    // A click the user made and the guest has not been given yet. Cleared when it
    // is emitted, and deliberately untouched by the release.
    bool pending = false;
    // Reporting this button as pressed: either serving the click's press, or
    // holding the guest's button down for as long as the hand is still on it,
    // which is what holding a pad button does.
    bool pressing = false;
    // Polls of press still owed before the press counts as one the guest has seen.
    int press_polls_left = 0;
    // Polls of release owed before this button may be pressed again.
    int gap_polls_left = 0;
  };

  Button buttons_[kButtonCount];
  int press_polls_ = UiNavStepper::kDefaultPressPolls;
  int release_polls_ = UiNavStepper::kDefaultReleasePolls;
};

// Moves 'value' one step of 'magnitude' toward zero and stops there.
inline double Consume(double value, double magnitude) {
  if (value >= magnitude) {
    return value - magnitude;
  }
  if (value <= -magnitude) {
    return value + magnitude;
  }
  return 0.0;
}

inline void UiNavStepper::SetRowPixels(double pixels) {
  // A row that is not a number would make every comparison below false and so
  // step on every single poll: keep the previous value instead.
  if (!std::isfinite(pixels)) {
    return;
  }
  row_pixels_ = std::clamp(pixels, kMinRowPixels, kMaxRowPixels);
}

inline void UiNavStepper::SetPulsePolls(int press_polls, int release_polls) {
  press_polls_ = std::clamp(press_polls, 1, kMaxPulsePolls);
  release_polls_ = std::clamp(release_polls, 0, kMaxPulsePolls);
}

inline void UiNavStepper::OnPointer(double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y)) {
    return;
  }
  if (!have_pointer_) {
    have_pointer_ = true;
    pointer_x_ = x;
    pointer_y_ = y;
    return;
  }
  const double dx = x - pointer_x_;
  const double dy = y - pointer_y_;
  pointer_x_ = x;
  pointer_y_ = y;
  QueueRows(dx, dy);
}

inline void UiNavStepper::OnPointerMotion(double dx, double dy) {
  if (!std::isfinite(dx) || !std::isfinite(dy)) {
    return;
  }
  // Under the pointer lock the first delta is whatever the hand did while the
  // pointer was still free, so it starts the gesture rather than joining it.
  if (!have_pointer_) {
    have_pointer_ = true;
    return;
  }
  pointer_x_ += dx;
  pointer_y_ += dy;
  QueueRows(dx, dy);
}

inline void UiNavStepper::QueueRows(double dx, double dy) {
  QueueRowsOnAxis(dx, &residual_x_, &queued_x_);
  // Screen down is stick down: the list moves the way the pointer moves. This is
  // the only place the vertical sign changes.
  QueueRowsOnAxis(-dy, &residual_y_, &queued_y_);
}

inline void UiNavStepper::QueueRowsOnAxis(double pixels, double* residual, double* queued) {
  const double rows = *residual + pixels / row_pixels_;
  if (!std::isfinite(rows) || std::fabs(rows) > 1.0e9) {
    return;
  }
  long long whole = std::llround(rows);
  if (whole > kMaxQueuedRows || whole < -kMaxQueuedRows) {
    // Past the cap this is no longer a gesture the guest can follow row by row,
    // and the rows beyond it are dropped rather than banked: rows that arrive
    // after the hand has stopped are a selection running away from the user.
    whole = whole > 0 ? kMaxQueuedRows : -kMaxQueuedRows;
    *residual = 0.0;
  } else {
    *residual = rows - static_cast<double>(whole);
  }
  const double cap = static_cast<double>(kMaxQueuedRows);
  *queued = std::clamp(*queued + static_cast<double>(whole), -cap, cap);
}

inline void UiNavStepper::ForgetPointer() {
  have_pointer_ = false;
  pointer_x_ = 0.0;
  pointer_y_ = 0.0;
  residual_x_ = 0.0;
  residual_y_ = 0.0;
}

inline void UiNavStepper::Reset() {
  ForgetPointer();
  queued_x_ = 0.0;
  queued_y_ = 0.0;
  press_polls_left_ = 0;
  release_polls_left_ = 0;
  press_x_ = 0;
  press_y_ = 0;
}

// Consumes one row of whatever is due, or returns false when nothing is.
inline bool UiNavStepper::TakeStep(int16_t* out_x, int16_t* out_y) {
  const double rows_x = std::fabs(queued_x_);
  const double rows_y = std::fabs(queued_y_);
  if (rows_x < 1.0 && rows_y < 1.0) {
    return false;
  }

  // The axis that is owed more rows goes first, and pays for the step in rows
  // rather than in the motion that produced them: what a press costs is one row,
  // whatever the pointer did to ask for it. Ties go to the vertical, which is
  // the axis menus are lists of.
  if (rows_y >= rows_x) {
    *out_y = static_cast<int16_t>(queued_y_ > 0.0 ? kStepDeflection : -kStepDeflection);
    queued_y_ = Consume(queued_y_, 1.0);
  } else {
    *out_x = static_cast<int16_t>(queued_x_ > 0.0 ? kStepDeflection : -kStepDeflection);
    queued_x_ = Consume(queued_x_, 1.0);
  }
  return true;
}

inline void UiNavStepper::Poll(int16_t* out_x, int16_t* out_y) {
  int16_t x = 0;
  int16_t y = 0;

  if (press_polls_left_ > 0) {
    --press_polls_left_;
    x = press_x_;
    y = press_y_;
    if (press_polls_left_ == 0) {
      release_polls_left_ = release_polls_;
    }
  } else if (release_polls_left_ > 0) {
    --release_polls_left_;
  } else if (TakeStep(&x, &y)) {
    // This poll is the first of the press, so only the rest of it is counted.
    press_polls_left_ = press_polls_ - 1;
    press_x_ = x;
    press_y_ = y;
  }

  if (out_x) {
    *out_x = x;
  }
  if (out_y) {
    *out_y = y;
  }
}

inline bool UiNavStepper::HasPendingRows() const {
  // The residual fractions of a row are not owed to anyone: the queue holds whole
  // rows only, and a press in flight or a release owed is a row the guest has not
  // finished seeing.
  return std::fabs(queued_x_) >= 1.0 || std::fabs(queued_y_) >= 1.0 || press_polls_left_ > 0 ||
         release_polls_left_ > 0;
}

inline void MenuHoverAligner::SetHoverDelayMs(double milliseconds) {
  if (!std::isfinite(milliseconds)) {
    return;
  }
  hover_delay_ms_ = std::clamp(milliseconds, kMinHoverDelayMs, kMaxHoverDelayMs);
}

inline void MenuHoverAligner::Forget() {
  ForgetBurst();
  have_frame_ = false;
  have_highlight_ = false;
  highlight_centre_ = 0.0;
  pitch_ = 0;
  frame_interval_ms_ = 0.0;
  current_.Reset();
  previous_.Reset();
  previous_valid_ = false;
  press_base_.Reset();
}

inline void MenuHoverAligner::ForgetBurst() {
  phase_ = Phase::kIdle;
  have_base_ = false;
  attempt_ = 0;
  steps_ = 0;
  presses_ = 0;
  budget_ = kMaxSteps;
  screen_changes_ = 0;
  flipped_ = false;
  blocked_ = false;
  have_target_ = false;
  target_y_ = 0.0;
  target_ms_ = 0.0;
  press_ms_ = 0.0;
  release_ms_ = 0.0;
}

inline void MenuHoverAligner::ClearTarget() {
  have_target_ = false;
  target_y_ = 0.0;
  blocked_ = false;
  if (phase_ != Phase::kIdle) {
    EndBurst("no-target");
  }
}
inline void MenuHoverAligner::SetTargetY(double guest_y, double now_ms) {
  if (!std::isfinite(guest_y)) {
    return;
  }
  const bool moved = !have_target_ || std::fabs(guest_y - target_y_) > TargetEpsilon();
  target_y_ = guest_y;
  have_target_ = true;
  if (moved) {
    target_ms_ = now_ms;
    // A new hover is a new question: the end of a list that the last burst ran into
    // says nothing about the row the pointer has been moved to.
    blocked_ = false;
  }
}

inline void MenuHoverAligner::AlignNow(double now_ms) {
  if (!have_target_) {
    return;
  }
  // The click is meant for the row under the pointer, so the wait is over the
  // moment the button goes down.
  target_ms_ = now_ms - hover_delay_ms_;
  blocked_ = false;
}

inline int16_t MenuHoverAligner::PollStick(double now_ms) {
  if (phase_ != Phase::kIdle) {
    // The phase lasts a number of the guest's frames, not until the picture changes:
    // the change may belong to the press before this one, and reading it as this
    // press's would measure the same step twice.
    if (now_ms - phase_ms_ >= PhaseMs()) {
      AdvancePhase(now_ms);
    }
  } else if (WantsAlignment(now_ms)) {
    BeginBurst(now_ms);
  }
  return phase_ == Phase::kPress ? static_cast<int16_t>(direction_ * kDeflection) : 0;
}

inline void MenuHoverAligner::FeedFrame(const NavSampledFrame& frame, double now_ms) {
  if (!frame.IsValid()) {
    return;
  }
  if (have_frame_ && !current_.Matches(frame)) {
    // A frame of another size is another window: everything measured in the old one
    // is in pixels that no longer mean anything.
    Forget();
  }
  if (phase_ != Phase::kIdle && have_frame_) {
    // Only while a burst is in flight, which is when the frames are being read as
    // fast as they are drawn: the shortest gap between two of them is the rate the
    // guest is drawing at, and no two of its frames can be closer than that.
    const double interval = now_ms - frame_ms_;
    if (interval >= kMinFrameIntervalMs && interval <= kMaxFrameIntervalMs &&
        (frame_interval_ms_ <= 0.0 || interval < frame_interval_ms_)) {
      frame_interval_ms_ = interval;
    }
  }
  // The newest frame becomes the previous one, so the difference below spans the
  // last pair of frames the guest drew and not the last pair the driver happened to
  // look at.
  const bool had_previous = have_frame_;
  std::swap(previous_, current_);
  current_ = frame;
  previous_valid_ = had_previous;
  have_frame_ = true;
  frame_ms_ = now_ms;
  if (phase_ != Phase::kIdle) {
    return;
  }
  // Nothing is being aligned, but a screen that changed while nothing was is a screen
  // whose pitch and highlight position are unknown, and whose rows are not the rows the
  // pointer was over. All of that is dropped, and nothing is pressed until the pointer
  // moves: the screen changed under the pointer, not because of it.
  if (previous_valid_ && DiffNavFrames(previous_, current_, detect_, &rows_) &&
      LooksLikeScreenChange(current_, rows_, detect_)) {
    have_highlight_ = false;
    highlight_centre_ = 0.0;
    pitch_ = 0;
    // The row the pointer is over is a row of this screen or it is not, and the
    // pointer has not moved: whatever it was pointing at on the screen before - a
    // click on it, most likely - is not what it is pointing at now. Stepping the
    // selection through a menu nobody asked about is the one thing a hover must never
    // do, so nothing happens until the pointer moves again.
    blocked_ = true;
  }
}

inline bool MenuHoverAligner::TakeReport(Report* out) {
  if (!report_ready_) {
    return false;
  }
  report_ready_ = false;
  if (out) {
    *out = report_;
  }
  return true;
}

inline bool MenuHoverAligner::WantsAlignment(double now_ms) const {
  if (blocked_ || !have_target_ || !have_frame_) {
    return false;
  }
  if (now_ms - target_ms_ < hover_delay_ms_) {
    return false;
  }
  // The frame the press would be measured against has to be one that was read while
  // the pointer was already resting here: a press measured against an older frame
  // measures whatever moved in between, which is a step this press did not make.
  if (!FrameIsFresh(now_ms)) {
    return false;
  }
  // Whether the selection is already on the pointer's row can only be answered by a
  // measurement, and there is none yet on a screen that has not been stepped on -
  // so the first hover there is worth a press, which is what measures it.
  return !Aligned();
}

inline bool MenuHoverAligner::Aligned() const {
  if (!have_highlight_ || pitch_ <= 0 || !have_target_) {
    return false;
  }
  // A row is one pitch tall and its centre is where the highlight is drawn, so the
  // pointer is on the selected row exactly while it is within half a pitch of the
  // highlight's centre.
  return std::fabs(target_y_ - highlight_centre_) <= 0.5 * static_cast<double>(pitch_);
}

inline int MenuHoverAligner::DirectionForTarget() const {
  if (!have_highlight_ || !have_target_) {
    // Nothing measured to aim from: down is the first guess, since a list that has
    // not been stepped through sits at its beginning.
    return kStickDown;
  }
  // Pressing down moves the selection down the screen, toward a pointer that is
  // below the highlight.
  return target_y_ > highlight_centre_ ? kStickDown : kStickUp;
}

inline double MenuHoverAligner::FrameIntervalMs() const {
  // The longer of what the guest's own poll rate says and what its frames say: a
  // capture that differs from the last one does not prove a frame was drawn, so the
  // frames can only ever under-reach, and a press timed from an under-estimate is a
  // press the guest can miss.
  double interval = std::max(guest_frame_ms_, frame_interval_ms_);
  interval = std::max(interval, kDefaultFrameIntervalMs);
  return std::clamp(interval, kMinFrameIntervalMs, kMaxFrameIntervalMs);
}

// Whether a pitch measured from a step is a measurement of the pitch at all. A pair
// twice as far apart as the pitch already known is two steps read as one - which is
// what a screen shows when a press the guest ignored had its move read late - and
// taking it for the pitch would double the tolerance the alignment is judged by,
// which is how a hover ends a row short of the pointer.
inline bool MenuHoverAligner::AcceptsPitch(int measured) const {
  if (measured <= 0) {
    return false;
  }
  if (pitch_ <= 0) {
    return true;
  }
  return std::abs(measured - pitch_) <= std::max(2, pitch_ / 3);
}

inline double MenuHoverAligner::PressMs() const {
  const Pulse& pulse = kAttemptPulses[std::clamp(attempt_, 0, kAttempts - 1)];
  return std::clamp(pulse.press_frames * FrameIntervalMs() + kPressSlackMs, kMinPressMs,
                    kMaxPhaseMs);
}

inline double MenuHoverAligner::ReleaseMs() const {
  const Pulse& pulse = kAttemptPulses[std::clamp(attempt_, 0, kAttempts - 1)];
  return std::clamp(pulse.release_frames * FrameIntervalMs() + kReleaseSlackMs, kMinReleaseMs,
                    kMaxPhaseMs);
}

inline double MenuHoverAligner::PhaseMs() const {
  return phase_ == Phase::kPress ? PressMs() : ReleaseMs();
}

inline bool MenuHoverAligner::FrameIsFresh(double now_ms) const {
  if (!have_frame_) {
    return false;
  }
  // Three of the guest's frames: a frame read inside that window was read while this
  // hover was being decided, so the press that follows it can only see what the press
  // itself changed - not a move that was already on the screen before the pointer got
  // there.
  const double window = std::max(kMinFreshFrameMs, 3.0 * FrameIntervalMs());
  return now_ms - frame_ms_ <= window;
}

inline double MenuHoverAligner::TargetEpsilon() const {
  return pitch_ > 0 ? 0.5 * static_cast<double>(pitch_) : kDefaultTargetEpsilon;
}

inline void MenuHoverAligner::BeginBurst(double now_ms) {
  phase_ = Phase::kIdle;
  attempt_ = 0;
  steps_ = 0;
  presses_ = 0;
  budget_ = kMaxSteps;
  screen_changes_ = 0;
  flipped_ = false;
  report_ready_ = false;
  // Straight to the press. Waiting for the guest to draw a frame first would wait
  // for ever on a screen that is not moving, and the frame the press is measured
  // against does not have to be new - it only has to have been drawn before the
  // press, which any frame the guest has already presented was.
  BeginStep(now_ms, DirectionForTarget());
}

inline void MenuHoverAligner::BeginStep(double now_ms, int direction) {
  // The frame the press will be measured against, taken now so that the difference
  // spans exactly the press and nothing that happened before it.
  press_base_ = current_;
  have_base_ = true;
  direction_ = direction == kStickUp ? kStickUp : kStickDown;
  phase_ = Phase::kPress;
  phase_ms_ = now_ms;
  ++presses_;
}

inline void MenuHoverAligner::AdvancePhase(double now_ms) {
  const double phase_ms = now_ms - phase_ms_;
  phase_ms_ = now_ms;
  if (phase_ == Phase::kPress) {
    press_ms_ = phase_ms;
    phase_ = Phase::kRelease;
    return;
  }
  release_ms_ = phase_ms;
  Evaluate(now_ms);
}

inline void MenuHoverAligner::EndBurst(const char* outcome) {
  phase_ = Phase::kIdle;
  have_base_ = false;
  report_.outcome = outcome;
  report_.steps = steps_;
  report_.presses = presses_;
  report_.pitch = pitch_;
  report_.bands = bands_this_step_;
  report_.target_y = target_y_;
  report_.highlight_y = highlight_centre_;
  report_.have_highlight = have_highlight_;
  report_ready_ = true;
}

inline void MenuHoverAligner::UpdateBudget() {
  if (pitch_ <= 0 || !have_highlight_ || !have_target_) {
    budget_ = kMaxSteps;
    return;
  }
  // The rows still to cross, plus slack for a pitch that is not exact: a burst that
  // keeps seeing movement beyond what the measurement says is left to cross is
  // seeing something other than the highlight, and stops here rather than running
  // on.
  const double rows = std::fabs(target_y_ - highlight_centre_) / static_cast<double>(pitch_);
  budget_ = std::clamp(steps_ + static_cast<int>(std::ceil(rows)) + 2, steps_ + 1, kMaxSteps);
}

inline void MenuHoverAligner::Evaluate(double now_ms) {
  // Pressing down moves the selection down the screen, so the highlight's new
  // position is the lower of the two the difference holds.
  const int screen_direction = -direction_;
  // Taken now: the retry paths below turn the stick round, and the line the step
  // logger writes has to say which way the press that was just measured went, not
  // which way the next one will.
  const int press_direction = direction_;
  const double press_frames = kAttemptPulses[std::clamp(attempt_, 0, kAttempts - 1)].press_frames;
  const double release_frames = kAttemptPulses[std::clamp(attempt_, 0, kAttempts - 1)].release_frames;
  const int attempt = attempt_;
  const double press_ms = press_ms_;
  const double release_ms = release_ms_;
  const double frame_ms = FrameIntervalMs();
  const double poll_ms = guest_frame_ms_;

  NavStep step;
  bool screen_change = false;
  bands_this_step_ = 0;
  if (have_base_ && DiffNavFrames(press_base_, current_, detect_, &rows_)) {
    screen_change = LooksLikeScreenChange(current_, rows_, detect_);
    if (!screen_change) {
      bands_this_step_ = FindChangedBands(current_, rows_, detect_, bands_.data());
      step = FindNavStep(bands_.data(), bands_this_step_, detect_, pitch_,
                         have_highlight_ ? highlight_centre_ : -1.0);
    }
  }

  auto finish = [this, press_frames, release_frames, attempt, press_direction, press_ms,
                 release_ms, frame_ms, poll_ms](const NavStep& step, const char* action) {
    if (!step_logger_) {
      return;
    }
    StepLog log;
    log.attempt = attempt;
    log.press_frames = press_frames;
    log.release_frames = release_frames;
    log.bands = bands_this_step_;
    log.moved = step.found;
    log.merged = step.merged;
    log.pitch = step.found ? step.pitch : 0;
    log.direction = press_direction;
    log.centre = have_highlight_ ? highlight_centre_ : -1.0;
    log.target = target_y_;
    log.press_ms = press_ms;
    log.release_ms = release_ms;
    log.frame_ms = frame_ms;
    log.poll_ms = poll_ms;
    log.action = action;
    step_logger_(log);
  };

  if (screen_change) {
    // Not the screen this was measuring any more, so nothing about it survives and
    // the press that was just made cannot be read either. The burst carries on with
    // the measurement cleared - but a screen that keeps being replaced is not a list
    // being stepped through, and two of those end it.
    ++screen_changes_;
    have_highlight_ = false;
    highlight_centre_ = 0.0;
    pitch_ = 0;
    attempt_ = 0;
    budget_ = kMaxSteps;
    if (screen_changes_ > kMaxScreenChanges) {
      EndBurst("screen-changed");
      blocked_ = true;
      finish(step, "screen-changed");
      return;
    }
    BeginStep(now_ms, DirectionForTarget());
    finish(step, "screen-change-restep");
    return;
  }

  if (step.found) {
    highlight_centre_ = NewHighlightCentre(step, screen_direction);
    if (AcceptsPitch(step.pitch)) {
      // A measurement of the pitch rather than of two steps at once, so the pitch is
      // what the rest of the burst aims by: half of it, not half of two rows, is what
      // decides whether the pointer is on the selected row.
      pitch_ = step.pitch;
    }
    have_highlight_ = true;
    attempt_ = 0;
    ++steps_;
    UpdateBudget();
    if (Aligned()) {
      EndBurst("aligned");
      finish(step, "aligned");
      return;
    }
    if (steps_ >= budget_) {
      EndBurst("step-limit");
      finish(step, "step-limit");
      return;
    }
    BeginStep(now_ms, DirectionForTarget());
    finish(step, "next");
    return;
  }

  // Nothing moved: the press was too short for the guest to see, or the selection is
  // already at that end of its list. Longer gaps first, and the opposite direction
  // once when the direction was only ever a guess, then give up until the pointer
  // moves somewhere else.
  ++attempt_;
  if (attempt_ < kAttempts) {
    int direction = direction_;
    const bool flip = !have_highlight_ && !flipped_;
    if (flip) {
      direction = -direction;
      flipped_ = true;
    }
    BeginStep(now_ms, direction);
    finish(step, flip ? "retry-flip" : "retry-longer");
    return;
  }
  EndBurst(have_highlight_ ? "no-movement" : "no-measurement");
  blocked_ = true;
  finish(step, "give-up");
}

inline void UiClickPulser::SetPulsePolls(int press_polls, int release_polls) {
  press_polls_ = std::clamp(press_polls, 1, UiNavStepper::kMaxPulsePolls);
  release_polls_ = std::clamp(release_polls, 1, UiNavStepper::kMaxPulsePolls);
}

inline void UiClickPulser::OnButtonDown(int index) {
  if (index < 0 || index >= kButtonCount) {
    return;
  }
  Button& button = buttons_[index];
  button.held = true;
  button.pending = true;
}

inline void UiClickPulser::OnButtonUp(int index) {
  if (index < 0 || index >= kButtonCount) {
    return;
  }
  // Not pending: whether the press has been emitted yet is Poll's business, and a
  // click released during travel is still a click.
  buttons_[index].held = false;
}

inline uint16_t UiClickPulser::Poll(bool travel_pending) {
  uint16_t buttons = 0;
  for (int i = 0; i < kButtonCount; ++i) {
    Button& button = buttons_[i];
    if (button.gap_polls_left > 0) {
      // The press has just ended, and a button that never comes up is not a click.
      --button.gap_polls_left;
      continue;
    }
    if (!button.pressing) {
      if (!button.pending || travel_pending) {
        continue;
      }
      button.pending = false;
      button.pressing = true;
      button.press_polls_left = press_polls_;
    }
    if (button.press_polls_left > 0) {
      // A press shorter than this is one the guest's frame can miss, so the count
      // starts at the whole pulse rather than at all but its first poll.
      buttons |= MaskFor(i);
      --button.press_polls_left;
      continue;
    }
    if (button.held) {
      // The hand is still on the button, and holding a pad button holds it.
      buttons |= MaskFor(i);
      continue;
    }
    // Served, and the hand has come up: the press ends here, on the poll after
    // the release, so that what the guest sees is the release the user made. This
    // poll is the first of the gap rather than one in front of it, so the gap is
    // the length it was asked for.
    button.pressing = false;
    button.gap_polls_left = release_polls_ - 1;
  }
  return buttons;
}

inline void UiClickPulser::Reset() {
  for (Button& button : buttons_) {
    button = Button{};
  }
}

}  // namespace rb_blitz::input
