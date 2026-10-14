// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The typed guest<->host channel for in-game settings (P1 of
// docs/plans/in-game-settings-plan.md; contracts in docs/plans/in-game-settings-contracts.md).
//
// Why this file exists on its own and is SDK-free: the *frames* the channel carries are host
// logic - namespace matching, bounds, checksums, idempotency, structured status - and none of
// that needs the emulator, a window or a boot. Splitting it out is what lets
// tests/settings_bridge_tests.cpp pin the framing rules without starting the game, the same
// reason src/fs/refresh_state.h and src/ui/menu_options.h are separate from their hooks.
//
// ---------------------------------------------------------------------------------------------
// The transport decision (P1), and the measurements behind it
// ---------------------------------------------------------------------------------------------
//
// The guest reaches the host through the **title's own script builtins**, which are ordinary
// recompiled functions and therefore hookable exactly like the Ultimate and shell-music hooks
// (src/hooks/ultimate.cpp, src/hooks/shell_music.cpp). Two facts were measured on the pinned
// retail image (docs/plans/in-game-settings-bridge-findings.md records the evidence):
//
//   * the builtin name table is a NUL-separated block in the image at 0x82080DD0:
//     `file_exists`, `write_string_to_file`, `write_file`, `read_file`, `run`, `--`, `++`,
//     ... (dumped from out/_swap/blitz_image.bin);
//   * a builtin has the shape `void f(PPCContext&, uint8_t* base)` with
//     `r3` = a hidden 8-byte result cell `{u32 value, u32 type}` and `r4` = a pointer to the
//     argument array whose first data node is `*(u32*)r4`, element i at `node + 8 + 8*i`.
//     `file_exists` writes `{bool, type 0}` into that cell and also returns `r3`; `write_file`
//     writes `{0, 0}`. Writing only `r3` would corrupt the ABI - the caller reads the cell.
//
// So the channel is: the guest asks for a **file whose path is in our exact namespace**, the
// host recognises it and answers, and anything that is not ours is forwarded untouched. This
// reuses the two builtins whose result shape is already safe to produce - `write_file` (no
// result to build) and `file_exists` (a boolean) - and never introduces a second `Nt*` hook
// definition (the read/query hooks are owned by src/hooks/menu_filter.cpp and
// src/hooks/dlc_refresh.cpp).
//
// The namespace is matched **exactly**, not by substring: the DLC refresh sentinel
// (src/hooks/dlc_refresh.cpp) deliberately searches for a name anywhere in the path, which is
// fine for a one-way ping and is exactly what must *not* carry settings commands.
//
// This module is the host half: it turns a namespaced path into a Request, hands it to a sink,
// and turns the answer into a Response the guest can read back. The guest hook that calls it
// lives beside it once the authored screen exists (see "the remaining gap" in the findings).
//
// Faithful behaviour is the namespace not being used: no frame is ever produced, the hooks
// forward every call, and the game is byte-for-byte the game it was.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::settings::bridge {

// The exact namespace. A path is ours only when it begins with this whole prefix; nothing that
// merely contains it qualifies.
inline constexpr char kNamespacePrefix[] = "rbbz-settings/";

// The frame format revision, independent of the on-disk settings schema version.
inline constexpr uint16_t kProtocolVersion = 1;

// Bounds. A frame is decoded from a path or a string the guest owns, so both the frame and the
// individual strings inside it are bounded before anything is copied.
inline constexpr size_t kMaxFrameBytes = 8 * 1024;
inline constexpr size_t kMaxPathChars = 32 * 1024;
inline constexpr size_t kMaxStringBytes = 4 * 1024;
inline constexpr size_t kMaxListItems = 256;

// One operation the guest can ask for. Values are wire-stable; never renumber or reuse.
enum class Op : uint16_t {
  kUnknown = 0,
  kSnapshot = 1,        // read the current snapshot
  kBeginEdit = 2,       // open a transaction at a base revision
  kSet = 3,             // stage one field in a transaction
  kCancelField = 4,     // discard one staged field
  kCommit = 5,          // write the transaction durably
  kCancelEdit = 6,      // drop the whole transaction
  kInvoke = 7,          // one-shot action (e.g. the achievements action)
  kRequestRestart = 8,  // save-barrier restart request
  // One-way caption event, owner request 2026-10-13: the highlighted row changed, so the help
  // overlay should describe `setting` (a stable row symbol), or clear when it is empty. It is
  // emitted by the generated DTA on every selection move and never queried, so it carries no
  // value, expects no response and deliberately never reaches the settings store: the hook half
  // routes it to the registered HelpSink and answers the guest's write like any other.
  kHelp = 9,
};

// The status of a response. Values are wire-stable.
enum class Status : uint16_t {
  kOk = 0,
  kUnsupported = 1,  // the op is well-formed but this build does not implement it
  kBadVersion = 2,
  kMalformed = 3,
  kTooLarge = 4,
  kUnknownOp = 5,
  kStaleRequest = 6,
  kConflict = 7,
  kInvalid = 8,
  kIoError = 9,
};

// A value on the wire. Deliberately *not* the on-disk model: the file stores tokens and the
// bridge carries them as text, so this is a transport shape, not a second source of truth.
enum class ValueKind : uint8_t { kNone = 0, kBool = 1, kInt = 2, kText = 3, kList = 4 };

struct WireValue {
  ValueKind kind = ValueKind::kNone;
  bool boolean = false;
  int64_t integer = 0;
  std::string text;               // kText: a stable token or a short string
  std::vector<std::string> list;  // kList: stable tokens

  static WireValue Bool(bool value);
  static WireValue Int(int64_t value);
  static WireValue Text(std::string value);
  static WireValue List(std::vector<std::string> values);
};

struct Request {
  uint16_t version = kProtocolVersion;
  Op op = Op::kUnknown;
  uint64_t request_id = 0;
  // The field being addressed, as the stable token the contracts doc names ("graphics.renderer"),
  // empty when the op takes none. The host maps token -> SettingId; the bridge never guesses ids.
  std::string setting;
  WireValue value;
};

struct Response {
  uint16_t version = kProtocolVersion;
  Status status = Status::kOk;
  uint64_t request_id = 0;
  WireValue value;      // e.g. the snapshot payload for kSnapshot
  std::string message;  // short, printable, already escaped; never carries data
};

// Exact-namespace test. True only for a string that begins with the whole prefix.
bool IsNamespacedPath(std::string_view path);

// ---------------------------------------------------------------------------------------------
// The two wire forms of a request, and which caller uses which
// ---------------------------------------------------------------------------------------------
//
// A request travels as the *path* of the builtin call, because that is the only argument the guest
// can fill in without the host's help. There are two spellings of it, and both parse into the same
// `Request`:
//
//   * **the text form** - `rbbz-settings/<op>/<setting>/<value>`, e.g.
//     `rbbz-settings/set/interface.hide_hud/b:1` or `rbbz-settings/commit/-/-`. This is the form
//     the *guest script* writes, and it has to be: a DTA script can concatenate constants and
//     format integers, but it has no way to compute the CRC-32 the binary frame carries. Static
//     literals are enough for a row, so nothing in a DTA panel needs a runtime-computed path.
//   * **the binary frame** - the checksummed, versioned form below. A caller that *can* build
//     bytes (the host's own tests, or an authored guest function) uses it, it carries a request
//     id, and it is what a future value-carrying response channel would be built on.
//
// The hook accepts either, in that order, and only inside the exact namespace. Everything else is
// forwarded to the original, once.
//
// Grammar of the text form (all segments ASCII, no spaces):
//
//   path    = "rbbz-settings/" op [ "/" setting [ "/" value ] ]
//   op      = begin | cancel | cancel-field | set | commit | snapshot | invoke | restart | help
//   setting = "-" (none) | [A-Za-z0-9_.-]{1,64}
//   value   = "-" (none) | "b:0" | "b:1" | "i:" <int64> | "t:" <token> | "l:" <token,token,...>
//   token   = [A-Za-z0-9_.:+-]{1,64}
//
// The help op is the one direction-only request: `rbbz-settings/help/<row-symbol>/-` is the
// highlighted row, `rbbz-settings/help/-/-` is "no row" (hide). It is answered like every other
// write (`write_file` has no result to build) and is never the subject of a query.
//
// A malformed text path in our namespace is *refused*, not forwarded: a name in this namespace is
// a name no content file can have, so the original could only have answered "not found" anyway.
bool ParseTextRequest(std::string_view path, Request* out, std::string* error);

// The text form of a request, for the tests and for the boot log. The inverse of
// ParseTextRequest on everything it can express; a value it cannot spell is written as "-" and
// the caller is expected to have checked the request first.
std::string EncodeTextRequest(const Request& request);

// The op name the text form spells, for the log.
const char* OpToken(Op op);

// The op a text token names, or kUnknown.
Op OpFromToken(std::string_view token);

// "<prefix><hex frame>" -> Request. Returns false and fills `error` - never partially applies -
// when the remainder is not valid hex, is empty, exceeds a bound, is truncated, or fails its
// checksum. `error` is a short host-side description for the log; it never reaches the guest.
bool ParseRequestPath(std::string_view path, Request* out, std::string* error);

// The inverse of ParseRequestPath: "<prefix><hex frame>". The guest-side builder uses this shape,
// and the tests build their fixtures with it; the game itself never calls it directly.
std::string EncodeRequestPath(const Request& request);

// A Response as hex, for the return channel (the content of a nested read the guest asked for).
std::string EncodeResponse(const Response& response);
// The inverse. Rejects a wrong version marker, bad hex, a truncated frame or a bad checksum.
bool DecodeResponse(std::string_view text, Response* out, std::string* error);

// The host side of the channel. P1 wires a stub; P3 supplies the real settings Service.
class RequestSink {
 public:
  virtual ~RequestSink() = default;
  virtual Response Handle(const Request& request) = 0;
};

// The caption channel's receiving end (kHelp): the help overlay registers one when it exists, and
// the hook half routes a `help` request to it *before* the store, because a caption event is not
// an edit. SDK-free on purpose: the DTA->sink path is testable without a window or a boot, and a
// build with no overlay (tests, or a headless boot) simply has no sink registered.
class HelpSink {
 public:
  virtual ~HelpSink() = default;
  // The stable symbol of the highlighted row, or empty for "the surface was left" (hide).
  virtual void Show(std::string_view symbol) = 0;
};

// The registered sink, or nullptr. The overlay registers itself when its dialog is created, on the
// UI thread, before the first guest frame can send a help request; the hook reads it on the guest
// thread, so registration happens-before any request by construction (the dialog is created before
// the guest thread starts).
void SetHelpSink(HelpSink* sink);
HelpSink* GetHelpSink();

// The provider gate the merged menu draws by (`provider.active`, i.e. "an Ultimate payload is
// installed"). Cached when the channel seeds at Configure() so a guest-thread caller - the help
// observer, which needs it to know whether the in-song pause list has five or six rows - can ask
// it without touching the store or the cvar registry. False until Configure() has run.
void SetProviderGateActive(bool active);
bool ProviderGateActive();

// The queries the *hook* half needs beside `Handle`: they are what the one-bit return channel is
// built from (`file_exists` can answer one boolean), and they are deliberately separate from
// `RequestSink` so a sink can be swapped in without the hooks knowing which store is behind it.
// P11a: the boot installs a sink backed by the real settings Service; the defaults here are P1's
// in-memory store, which is what a boot with no service - a test, or a workspace that has not
// wired one - keeps using.
class SinkQueries {
 public:
  virtual ~SinkQueries() = default;
  // True when the setting currently holds the request's value: the menu's row-state probe.
  virtual bool Matches(const Request& request) const = 0;
  // True only for a request id this store applied with status kOk.
  virtual bool Acknowledged(uint64_t request_id) const = 0;
  // The committed value of one setting, if there is one.
  virtual bool Get(std::string_view setting, WireValue* out) const = 0;
  // The setting tokens the **last commit changed** (§5.4's no-op rule).
  virtual const std::vector<std::string>& last_commit_changes() const = 0;
};

// Validates, de-duplicates and forwards. A request id seen before returns the first response
// without reaching the sink again, so a guest that re-asks (a repeated frame, a redrawn panel)
// cannot apply a change twice. Not thread-safe: the caller serialises, which is the guest/UI
// thread the hooks run on.
class Dispatcher {
 public:
  static constexpr size_t kRecentRequests = 16;

  explicit Dispatcher(RequestSink* sink) : sink_(sink) {}

  Response Dispatch(const Request& request);

  // P11a: point the channel at another store. The dispatcher's idempotency window belongs to the
  // *guest session*, not to a store, so it is kept: a re-asked request is still answered from the
  // first response rather than reaching the new sink twice.
  void SetSink(RequestSink* sink) { sink_ = sink; }

  // Forgets the idempotency window. Called when the guest session ends, not per request.
  void Reset();

  // What the last Dispatch did, for the boot log and the tests.
  enum class Outcome { kForwarded, kReplayed, kRejected };
  Outcome last_outcome() const { return last_outcome_; }

 private:
  struct Recent {
    uint64_t request_id = 0;
    Op op = Op::kUnknown;
    Response response;
  };

  RequestSink* sink_ = nullptr;
  std::deque<Recent> recent_;
  Outcome last_outcome_ = Outcome::kRejected;
};

// ---------------------------------------------------------------------------------------------
// P1's runtime store: the stand-in for the settings Service P3 owns.
// ---------------------------------------------------------------------------------------------
//
// The two builtins the transport rides on can only carry a boolean back to the guest
// (`file_exists`'s result cell is `{u32 value, u32 type}` with type 0 = int). So the channel is
// split into three calls, and the *read* is a **membership query** rather than a data-returning
// call:
//
//   edit    guest -> host, typed value   write_file("rbbz-settings/<frame kSet>", <ignored>)
//   commit  guest -> host                write_file("rbbz-settings/<frame kCommit>", <ignored>)
//   read    guest -> host, typed value   file_exists("rbbz-settings/<frame kSnapshot + candidate>")
//                                        -> true when the setting currently holds that value
//   ack     guest -> host, one bit        file_exists("rbbz-settings/<frame other op>")
//                                        -> true when that request id was applied durably
//
// A one-bit answer per call is exactly what a menu row needs to draw its own state ("is this
// setting on?"), and it deliberately does not need the guest DataNode/array construction that
// `in-game-settings-bridge-findings.md` §5 defers to the pass that traces the allocator. The
// membership query is *not* a substitute for a data-carrying response channel: it cannot return a
// snapshot, and it says so in the log when asked for one.
class MemorySink : public RequestSink, public SinkQueries {
 public:
  // Applies a request to the store and records the outcome, which is what the ack answers with.
  Response Handle(const Request& request) override;

  // The read half. True when `request.setting` currently holds `request.value`. A kind that does
  // not match is false, not an error, so a state probe can never disturb the store.
  bool Matches(const Request& request) const override;

  // The acknowledgement half: true only for a request id this store applied with status kOk.
  bool Acknowledged(uint64_t request_id) const override;

  // The committed value of one setting, if it has been committed.
  bool Get(const std::string_view setting, WireValue* out) const override;

  size_t committed_count() const { return committed_.size(); }
  size_t staged_count() const { return staged_.size(); }
  size_t handled_count() const { return handled_.size(); }

  // P9: the setting tokens the **last commit changed** - staged values whose committed value
  // differs from what was already there. A commit that writes a value back unchanged (the
  // change-then-revert case §5.4 calls a no-op) reports nothing, which is what keeps a hot row
  // from being re-applied for a session that did not really alter it.
  const std::vector<std::string>& last_commit_changes() const override { return last_commit_changes_; }

  void Reset();

 private:
  std::map<std::string, WireValue> committed_;
  std::map<std::string, WireValue> staged_;
  std::map<uint64_t, Status> handled_;
  std::vector<std::string> last_commit_changes_;
};

// A deterministic sink for the tests and for P1's host-side round trip: it answers every op with
// a well-formed response and echoes the request id, so a frame's whole journey is checkable
// without a settings service. It is not wired into the game.
class StubSink : public RequestSink {
 public:
  // How many requests have reached the sink, and the id of the last one.
  uint64_t calls() const { return calls_; }
  uint64_t last_request_id() const { return last_request_id_; }

  Response Handle(const Request& request) override;

 private:
  uint64_t calls_ = 0;
  uint64_t last_request_id_ = 0;
};

}  // namespace rb_blitz::settings::bridge
