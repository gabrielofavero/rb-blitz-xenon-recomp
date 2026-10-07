// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R11, a way out of a song load that never finishes.
//
// The title's song load ends on its own prompt: `ui/loading/gen/loading.dtb`'s
// LoadingScreen shows `loading.lbl` ("PRESS <alt1>A</alt1>TO BEGIN", the locale entry
// `loading_press_button`) once the DTA function `load_finished` has run, and its
// `BUTTON_DOWN_MSG` starts the song from that prompt. Everything before that prompt is the
// load itself: the tip list, the progress bar, and - because that label is shown by the
// state move that owns it and by nothing else - no prompt at all. A load that cannot finish
// (a package whose song files are missing is the reported case) leaves the screen in
// exactly that state, and the reported experience is a softlock: nothing on screen says any
// button would leave.
//
// What the title does not say, it already does. The loading screen's `BUTTON_DOWN_MSG`
// handles the confirm actions only and falls through to `unhandled(0)`, and the screen
// underneath treats cancel as "go back": B leaves a load that is still running, prompt or
// not, and lands on the song selection screen the load was started from (measured, see
// docs/engine/loading-cancel.md). So the missing piece is not a way out - it is *saying*
// so, on the title's own prompt, once waiting has stopped being reasonable.
//
// That is all this module adds. At `enhancements_loading_cancel_seconds` into a load that
// has not prompted yet, the loading screen's own prompt label is given
// "PRESS <alt1>B</alt1>TO CANCEL".
//
// MEASURED, and why the toggle ships off and says Not implemented: the write lands and is
// safe, but the label is not drawn in this state. Nothing in this state draws it - the
// title's own state move is the only thing that does, and that move faults here - so the
// text arrives on a label the player cannot see. See docs/engine/loading-cancel.md §3.
//
// Everything else is left alone, on purpose:
//
//   * A is not taken away, and needs no taking away: the prompt's confirm branch is gated
//     on `is_load_done`, which only the title's own completion sets, so while the load is
//     still running A does nothing - which is the state the cancel prompt is offered in.
//   * B is not wired to anything: it already cancels, and the song selection screen is
//     where it lands.
//   * If the load finishes after the prompt has appeared, the title's own completion sets
//     the label's text and state, so "PRESS A TO BEGIN" replaces "PRESS B TO CANCEL" by
//     itself - the one transition the request asks for by name. What this module does then
//     is get out of the way (see BeforeLoadFinished) and put the title's text back if that
//     completion does not re-set it.
//
// The faithful behaviour (R11 off) is this file doing nothing: the hooks call the guest's
// own functions unchanged, and no state is written anywhere.

#include "hooks/loading_cancel.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/stack.h>
#include <rex/system/xmemory.h>

REXCVAR_DECLARE(bool, enhancements_loading_cancel);
REXCVAR_DECLARE(double, enhancements_loading_cancel_seconds);

// The guest functions this module sits on. Each was identified at runtime with the probe;
// docs/engine/loading-cancel.md has the traces and the measurements.
//
//   sub_822C45A8  the loading screen's message handler. The load pump calls it on a periodic
//                 tick while the screen is up, from 0x822C37E8, with the LoadingScreen object
//                 in r4. The tick is what says "a song load has started". It is not the
//                 clock: it stops when the load pump stops, which is the state to be timed.
//   sub_827266E0  the guest's own pad read (the wrapper around __imp__XamInputGetState),
//                 called about a thousand times a second - measured - and still called while
//                 a load is stuck. That is the property the clock needs, and it is the same
//                 one the cancel depends on: if this is not running, no button reaches the
//                 screen either.
//   sub_822C38F8  the title's own "the load is done" entry point: it runs the DTA function
//                 `load_finished` on the LoadingScreen in r4. Hooked to hear the title
//                 finish a load this module is waiting on.
//   sub_8229D7A0  UILabel::SetText(const char*): r3 is the label, r4 the guest string. The
//                 prompt's text, and the one thing this module rewrites.
REX_EXTERN(__imp__sub_822C45A8);
REX_EXTERN(__imp__sub_827266E0);
REX_EXTERN(__imp__sub_822C38F8);
REX_EXTERN(__imp__sub_8229D7A0);

namespace rb_blitz::loading_cancel {

namespace {

// The call site of the load's periodic tick inside sub_822C45A8. The same function handles
// the screen's other messages, so the caller's return address is what says "this is the
// tick".
constexpr uint32_t kLoadTickCallSite = 0x822C37E8;

// The title's prompt text is recognised by its tail rather than the whole of it, so a
// localisation that reorders the markup still matches. The replacement is written in the
// title's own markup: `<alt1>X</alt1>` is how a button glyph is drawn (see
// `loading_press_button`, and the rest of `ui/locale/eng/gen/locale_keep.dtb`).
constexpr const char* kPromptNeedle = "TO BEGIN";
constexpr const char* kCancelPrompt = "PRESS <alt1>B</alt1>TO CANCEL";

struct Settings {
  bool enabled = false;
  double seconds = 60.0;
};
Settings g_settings;

std::chrono::steady_clock::time_point Now() { return std::chrono::steady_clock::now(); }

// Guest memory, read the way the guest writes it: big endian.
uint32_t ReadU32(uint8_t* base, uint32_t address) {
  const uint8_t* p = rex::memory::GuestPtr<const uint8_t*>(base, address);
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Guest-thread state. The hooks run on the guest's own thread, so these are plain values.
std::atomic<uint32_t> g_screen{0};         // the LoadingScreen object this load belongs to
std::atomic<uint32_t> g_screen_vtable{0};  // its vtable, which is how "still alive" is read
std::atomic<uint32_t> g_label{0};          // its prompt label
std::atomic<bool> g_watching{false};       // a load is running and the clock is on it
std::atomic<bool> g_armed{false};          // the cancel prompt is up
std::atomic<bool> g_due{false};            // the timeout has passed; the UI thread acts on it

bool g_rewrote = false;          // the label is showing this module's text
bool g_title_text_back = false;  // the title set the label's own text again
bool g_restore_title_text = false;  // the completion ran with the cancel prompt up
char g_prompt_text[96] = {};     // the title's prompt text, kept for the restore below
std::chrono::steady_clock::time_point g_load_started;
unsigned g_prompts = 0;  // how many cancel prompts this boot has shown, for the log line

bool Enabled() { return g_settings.enabled; }
uint32_t Screen() { return g_screen.load(std::memory_order_relaxed); }
uint32_t Label() { return g_label.load(std::memory_order_relaxed); }
bool Armed() { return g_armed.load(std::memory_order_relaxed); }
bool Due() { return g_due.load(std::memory_order_relaxed); }

double SinceLoad() { return std::chrono::duration<double>(Now() - g_load_started).count(); }

// The title's own text, put back by hand. The title sets it again by itself when the state
// move that owns it runs (the observed completion does); this covers the case where it does
// not, because the prompt was already up and nothing about it changed.
void RestorePromptText(PPCContext& ctx, uint8_t* base) {
  if (!g_rewrote || g_title_text_back || Label() == 0 || g_prompt_text[0] == '\0') {
    g_rewrote = false;
    return;
  }
  rex::ppc::stack_guard guard(ctx);
  const uint32_t text = rex::ppc::stack_push_string(ctx, base, g_prompt_text);
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u32 = Label();
  frame.ctx.r4.u32 = text;
  __imp__sub_8229D7A0(frame.ctx, base);
  g_rewrote = false;
  REXLOG_INFO("loading_cancel: the load finished, so the prompt reads \"{}\" again", g_prompt_text);
}

void SetLabelText(PPCContext& ctx, uint8_t* base, const char* text) {
  rex::ppc::stack_guard guard(ctx);
  const uint32_t guest_text = rex::ppc::stack_push_string(ctx, base, text);
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u32 = Label();
  frame.ctx.r4.u32 = guest_text;
  __imp__sub_8229D7A0(frame.ctx, base);
}

// The waiting is over. The action has to happen on the UI thread - the title's screen object
// and its labels belong to it, and a call from the pad thread faults (measured: a cross-thread
// write at guest 0x00000000). So the clock only raises a flag; the next call of the loading
// screen's own tick, which is UI thread, does the work.
void RequestCancel() {
  if (Due()) {
    return;
  }
  g_due.store(true, std::memory_order_relaxed);
  REXLOG_INFO("loading_cancel: the load on object {:08X} has not prompted after {:.1f}s; the "
              "cancel prompt is offered at the next chance the loading screen is on the UI thread",
              Screen(), g_settings.seconds);
}

// Say it on the title's own prompt label: the one other thing that can be done from here.
//
// MEASURED, and it is not enough on its own: the label takes the text, and a load that has not
// prompted yet does not show that label - this state is not the state the title draws its
// prompt in. The label is not the gate: the same 128 bytes of it are identical in this state
// and in the state where the prompt is up (docs/engine/loading-cancel.md §2.1), so what shows
// it is an ancestor, a panel state, or the draw list, all of which the title's own state move
// is what drives - and that move is the DTA function `load_finished`, whose own invocation
// faults here. So the text is written where a prompt would read it, and the toggle says Not
// implemented because a player cannot see it.
//
// The write itself is safe and does land (measured in this state, no fault), and in a load
// that goes on to finish, the "PRESS A TO BEGIN" hand-back below is the request's own
// transition, working.
void OfferCancel(PPCContext& ctx, uint8_t* base) {
  g_due.store(false, std::memory_order_relaxed);
  g_armed.store(true, std::memory_order_relaxed);
  if (Label() == 0) {
    // The label is filled when the screen is built, before the load's first tick, so this
    // means the screen is not the one this module recorded. It is logged rather than
    // guessed at, and the load is left as it was.
    REXLOG_WARN(
        "loading_cancel: the loading screen's prompt label was never seen, so no cancel prompt "
        "is offered");
    return;
  }
  SetLabelText(ctx, base, kCancelPrompt);
  g_rewrote = true;
  ++g_prompts;
  REXLOG_INFO(
      "loading_cancel: the load on object {:08X} has not prompted after {:.1f}s, so the prompt "
      "label reads \"{}\" (prompt {} of this boot); B leaves the load, and the title's own "
      "\"{}\" replaces it if the load finishes",
      Screen(), g_settings.seconds, kCancelPrompt, g_prompts, g_prompt_text);
}

void OnTick(PPCContext& ctx, uint8_t* base) {
  const uint32_t screen = ctx.r4.u32;
  if (screen == 0) {
    return;
  }
  if (Screen() != screen) {
    // A song load starts here. The screen object is built per load, so its arrival is both the
    // "a load is running" signal and the clock's start.
    g_screen.store(screen, std::memory_order_relaxed);
    g_screen_vtable.store(ReadU32(base, screen), std::memory_order_relaxed);
    g_armed.store(false, std::memory_order_relaxed);
    g_due.store(false, std::memory_order_relaxed);
    g_watching.store(true, std::memory_order_relaxed);
    g_rewrote = false;
    g_title_text_back = false;
    g_load_started = Now();
    REXLOG_INFO("loading_cancel: watching the load on object {:08X} (timeout {:.1f}s)", screen,
                g_settings.seconds);
    return;
  }
  // The loading screen's own tick, on the UI thread: the one place a cancel prompt that has
  // come due is acted on.
  if (Due()) {
    OfferCancel(ctx, base);
    return;
  }
}

// The clock, on the guest's pad read. It is called about a thousand times a second, so the
// case that costs anything has to be the rare one: one relaxed load of a flag is the whole
// of a normal frame of a normal screen.
void OnPadPoll(PPCContext& ctx, uint8_t* base) {
  if (!g_watching.load(std::memory_order_relaxed)) {
    return;
  }
  if (Armed()) {
    // The prompt has already been offered; the clock's work is done until the load finishes
    // (BeforeLoadFinished) or the screen goes away.
    return;
  }
  if (SinceLoad() < g_settings.seconds) {
    return;
  }
  // The screen the load belonged to is still there: waiting is what this is for. A screen
  // object that is gone takes the clock with it - the title builds one per load, so a load
  // that ends some other way (the engine failing it, say) must not leave this module holding
  // an address that is no longer a screen.
  if (Screen() == 0 || ReadU32(base, Screen()) != g_screen_vtable.load(std::memory_order_relaxed)) {
    REXLOG_INFO("loading_cancel: the loading screen for object {:08X} is gone; the clock is off",
                Screen());
    g_watching.store(false, std::memory_order_relaxed);
    return;
  }
  RequestCancel();
}

// The title's own completion, in two halves: everything this module does to *its* call has to
// happen on either side of the one invocation of it.
//
// MEASURED, and it cost a crash to find: running the completion from the module's own handler
// as well as from the hook's call of the guest function - which is what this module did before
// - evaluates the DTA function `load_finished` twice, and the second evaluation reads a screen
// the first one has already advanced: `Unhandled guest access violation: read of guest
// 0x00000014 on thread 0xF8000028`, on a load that failed, with the toggle on and a control run
// (toggle off, same load) clean.
void BeforeLoadFinished(uint32_t screen) {
  if (screen != Screen()) {
    // The title calls this entry point for the loads that are not this one; only the screen
    // this module is watching is the load it is timing.
    return;
  }
  g_restore_title_text = Armed();
  if (g_restore_title_text) {
    // The load finished with the cancel prompt up. The armed flag goes down first, so the
    // title's own state, which this call is about to set, is left alone - and its text, if it
    // sets one, is not rewritten.
    REXLOG_INFO(
        "loading_cancel: the load finished while the cancel prompt was up; the title's own "
        "prompt takes over");
  }
  g_armed.store(false, std::memory_order_relaxed);
  g_watching.store(false, std::memory_order_relaxed);
}

void AfterLoadFinished(PPCContext& ctx, uint8_t* base) {
  if (!g_restore_title_text) {
    return;
  }
  g_restore_title_text = false;
  RestorePromptText(ctx, base);
}

void OnLoadFinishedHookBegin(PPCContext& ctx, uint8_t* base) {
  if (Enabled()) {
    BeforeLoadFinished(ctx.r4.u32);
  }
}

void OnLoadFinishedHookEnd(PPCContext& ctx, uint8_t* base) {
  if (Enabled()) {
    AfterLoadFinished(ctx, base);
  }
}

void OnSetText(PPCContext& ctx, uint8_t* base) {
  // A label is cleared by passing 0, and 0 is not a string: reading through it takes the
  // whole process down (`Unhandled guest access violation: read of guest 0x00000014`), which
  // is what the title's own completion does to a load that failed.
  if (ctx.r4.u32 == 0) {
    return;
  }
  const char* text = rex::memory::GuestPtr<const char*>(base, ctx.r4.u32);
  if (std::strstr(text, kPromptNeedle) == nullptr) {
    return;
  }
  // The title fills this label when the screen is built, before anything is known about the
  // load: that is where the label to write on is learned from, and the title's own text with
  // it (the restore above has nothing else to put back).
  g_label.store(ctx.r3.u32, std::memory_order_relaxed);
  if (!Armed()) {
    std::snprintf(g_prompt_text, sizeof(g_prompt_text), "%s", text);
    g_title_text_back = true;  // the title's own prompt text is on the label
    return;
  }
  ctx.r4.u32 = rex::ppc::stack_push_string(ctx, base, kCancelPrompt);
  g_rewrote = true;
}

}  // namespace

void Configure() {
  g_settings.enabled = REXCVAR_GET(enhancements_loading_cancel);
  g_settings.seconds = REXCVAR_GET(enhancements_loading_cancel_seconds);
  if (g_settings.seconds < 1.0) {
    g_settings.seconds = 1.0;
  }
  REXLOG_INFO("loading_cancel: {} (timeout {:.1f}s): {}", g_settings.enabled ? "on" : "off",
              g_settings.seconds,
              g_settings.enabled
                  ? "a song load that has not prompted by then is offered the title's own prompt "
                    "with B on it (docs/engine/loading-cancel.md)"
                  : "the title's own prompt, and nothing said about a load that never ends");
}

void OnLoadTickHook(PPCContext& ctx, uint8_t* base) {
  if (Enabled() && ctx.lr == kLoadTickCallSite) {
    OnTick(ctx, base);
  }
}

void OnPadPollHook(PPCContext& ctx, uint8_t* base) {
  if (Enabled()) {
    OnPadPoll(ctx, base);
  }
}

void OnSetTextHook(PPCContext& ctx, uint8_t* base) {
  if (Enabled()) {
    OnSetText(ctx, base);
  }
}

}  // namespace rb_blitz::loading_cancel

REX_HOOK_RAW(sub_822C45A8) {
  rb_blitz::loading_cancel::OnLoadTickHook(ctx, base);
  __imp__sub_822C45A8(ctx, base);
}

REX_HOOK_RAW(sub_827266E0) {
  rb_blitz::loading_cancel::OnPadPollHook(ctx, base);
  __imp__sub_827266E0(ctx, base);
}

REX_HOOK_RAW(sub_822C38F8) {
  rb_blitz::loading_cancel::OnLoadFinishedHookBegin(ctx, base);
  __imp__sub_822C38F8(ctx, base);
  rb_blitz::loading_cancel::OnLoadFinishedHookEnd(ctx, base);
}

REX_HOOK_RAW(sub_8229D7A0) {
  rb_blitz::loading_cancel::OnSetTextHook(ctx, base);
  __imp__sub_8229D7A0(ctx, base);
}
