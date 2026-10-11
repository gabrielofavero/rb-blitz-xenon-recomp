// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/hooks/settings_bridge.{h,cpp} - the typed guest<->host settings channel
// (P1 of docs/plans/in-game-settings-plan.md).
//
// What is testable here is exactly the part that must not be discovered by booting: the
// namespace is matched and not merely contained, a frame is bounded and checksummed before any
// field is interpreted, a bad version or unknown op is refused without reaching the service,
// and a replayed request id returns the first answer instead of applying a change twice. The
// guest hook that carries these frames needs the SDK and a boot; this half does not.
//
// SDK-free: no rex::, no window, no boot.

#include "check.h"

#include "hooks/settings_bridge.h"

#include <string>
#include <vector>

namespace {

using namespace rb_blitz::settings::bridge;
using namespace rb_blitz::test;

void TestNamespaceExactness() {
  BeginCase("the namespace is matched exactly, never as a substring");

  CHECK_TRUE(IsNamespacedPath("rbbz-settings/00"));
  CHECK_TRUE(IsNamespacedPath("rbbz-settings/deadbeef"));

  // The prefix alone is not a request: there is nothing after the separator.
  CHECK_FALSE(IsNamespacedPath("rbbz-settings/"));
  CHECK_FALSE(IsNamespacedPath("rbbz-settings"));
  // A different but similar-looking name is not ours.
  CHECK_FALSE(IsNamespacedPath("rbbz-settings-x/00"));
  CHECK_FALSE(IsNamespacedPath("rbbzsettings/00"));
  // The DLC sentinel's rule is deliberately the opposite: a *substring* anywhere counts. A
  // settings command must not be smuggled in behind another device or a longer path.
  CHECK_FALSE(IsNamespacedPath("songcache:/rbbz-settings/00"));
  CHECK_FALSE(IsNamespacedPath("d:/content/rbbz-settings/00"));
  CHECK_FALSE(IsNamespacedPath(""));

  Request request;
  std::string error;
  CHECK_FALSE(ParseRequestPath("songcache:/rbbz-settings/00", &request, &error));
  CHECK_FALSE(error.empty());
}

void TestRequestRoundTrip() {
  BeginCase("every op and value kind survives a request round trip");

  const ValueKind kinds[] = {ValueKind::kNone, ValueKind::kBool, ValueKind::kInt,
                             ValueKind::kText, ValueKind::kList};
  for (ValueKind kind : kinds) {
    Request sent;
    sent.op = Op::kSet;
    sent.request_id = 0x1122334455667788ull;
    sent.setting = "graphics.renderer";
    switch (kind) {
      case ValueKind::kNone:
        break;
      case ValueKind::kBool:
        sent.value = WireValue::Bool(true);
        break;
      case ValueKind::kInt:
        sent.value = WireValue::Int(-7);
        break;
      case ValueKind::kText:
        sent.value = WireValue::Text("fxaa_extreme");
        break;
      case ValueKind::kList:
        sent.value = WireValue::List({"splash_leaderboard", "splash_dlc"});
        break;
    }

    const std::string path = EncodeRequestPath(sent);
    CHECK_TRUE(IsNamespacedPath(path));

    Request got;
    std::string error;
    CHECK_TRUE(ParseRequestPath(path, &got, &error));
    CHECK_EQ(got.version, kProtocolVersion);
    CHECK_TRUE(got.op == Op::kSet);
    CHECK_EQ(got.request_id, sent.request_id);
    CHECK_TRUE(got.setting == sent.setting);
    CHECK_TRUE(got.value.kind == sent.value.kind);
    CHECK_TRUE(got.value.boolean == sent.value.boolean);
    CHECK_EQ(static_cast<uint64_t>(got.value.integer), static_cast<uint64_t>(sent.value.integer));
    CHECK_TRUE(got.value.text == sent.value.text);
    CHECK_EQ(got.value.list.size(), sent.value.list.size());
    for (size_t i = 0; i < got.value.list.size(); ++i) {
      CHECK_TRUE(got.value.list[i] == sent.value.list[i]);
    }
  }
}

void TestMalformedFrames() {
  BeginCase("a frame that is not whole, not hex, or not ours is refused");

  Request request;
  std::string error;

  // Not the namespace at all.
  CHECK_FALSE(ParseRequestPath("", &request, &error));
  CHECK_FALSE(ParseRequestPath("rbbz-settings/", &request, &error));

  // Odd number of hex digits, and a non-hex character.
  CHECK_FALSE(ParseRequestPath("rbbz-settings/abc", &request, &error));
  CHECK_FALSE(ParseRequestPath("rbbz-settings/zz", &request, &error));

  // A valid frame with one hex digit changed: the checksum no longer matches, and nothing is
  // interpreted - the whole point of checksumming before parsing.
  Request sent;
  sent.op = Op::kSnapshot;
  sent.request_id = 9;
  sent.setting = "graphics.vsync";
  sent.value = WireValue::Bool(false);
  std::string path = EncodeRequestPath(sent);
  CHECK_TRUE(ParseRequestPath(path, &request, &error));
  // Flip a byte inside the payload, past the fixed header.
  path[static_cast<size_t>(sizeof(kNamespacePrefix) - 1) + 20] =
      path[static_cast<size_t>(sizeof(kNamespacePrefix) - 1) + 20] == '0' ? '1' : '0';
  CHECK_FALSE(ParseRequestPath(path, &request, &error));
  CHECK_TRUE(error.find("checksum") != std::string::npos);

  // Truncated: drop the last four hex digits (two bytes) so the frame cannot even hold its
  // checksum at the stated length.
  CHECK_FALSE(ParseRequestPath(EncodeRequestPath(sent).substr(0, 40), &request, &error));

  // Too large for the frame bound: a value big enough to push the decoded frame past the limit.
  Request big;
  big.op = Op::kSet;
  big.request_id = 1;
  big.value = WireValue::Text(std::string(9000, 'x'));
  CHECK_FALSE(ParseRequestPath(EncodeRequestPath(big), &request, &error));

  // Too long for the path bound: the hex of the same oversized value exceeds the path limit
  // before the frame limit is even reached.
  Request enormous;
  enormous.op = Op::kSet;
  enormous.request_id = 1;
  enormous.value = WireValue::Text(std::string(40000, 'x'));
  CHECK_FALSE(ParseRequestPath(EncodeRequestPath(enormous), &request, &error));

  // A list beyond the item bound is refused even though the frame itself is small.
  Request many;
  many.op = Op::kSet;
  many.request_id = 1;
  std::vector<std::string> items;
  for (int i = 0; i < 300; ++i) {
    items.push_back("row" + std::to_string(i));
  }
  many.value = WireValue::List(items);
  CHECK_FALSE(ParseRequestPath(EncodeRequestPath(many), &request, &error));
}

void TestResponseRoundTrip() {
  BeginCase("a response round-trips, and its message cannot smuggle data or growth");

  Response sent;
  sent.status = Status::kConflict;
  sent.request_id = 42;
  sent.value = WireValue::Text("graphics.resolution");
  sent.message = "changed by another writer";

  Response got;
  std::string error;
  CHECK_TRUE(DecodeResponse(EncodeResponse(sent), &got, &error));
  CHECK_TRUE(got.status == Status::kConflict);
  CHECK_EQ(got.request_id, sent.request_id);
  CHECK_TRUE(got.value.kind == ValueKind::kText);
  CHECK_TRUE(got.value.text == sent.value.text);
  CHECK_TRUE(got.message == sent.message);

  // Control characters are replaced, so a message can never carry a newline into a log line.
  Response noisy;
  noisy.status = Status::kOk;
  noisy.message = "one\ntwo\tthree";
  CHECK_TRUE(DecodeResponse(EncodeResponse(noisy), &got, &error));
  CHECK_TRUE(got.message.find('\n') == std::string::npos);
  CHECK_TRUE(got.message.find('\t') == std::string::npos);

  // A message longer than the bound is truncated to a fixed ceiling, not carried whole.
  Response longMessage;
  longMessage.status = Status::kOk;
  longMessage.message = std::string(4096, 'm');
  CHECK_TRUE(DecodeResponse(EncodeResponse(longMessage), &got, &error));
  CHECK_TRUE(got.message.size() <= 256);

  // A response that is not hex, or is truncated, is refused.
  CHECK_FALSE(DecodeResponse("nope", &got, &error));
  CHECK_FALSE(DecodeResponse(EncodeResponse(sent).substr(0, 10), &got, &error));
}

void TestDispatcherStatuses() {
  BeginCase("version, op and service presence decide the status before the sink is reached");

  StubSink sink;
  Dispatcher dispatcher(&sink);

  // An unsupported version is refused without reaching the sink.
  Request badVersion;
  badVersion.version = kProtocolVersion + 1;
  badVersion.op = Op::kSnapshot;
  badVersion.request_id = 1;
  Response r = dispatcher.Dispatch(badVersion);
  CHECK_TRUE(r.status == Status::kBadVersion);
  CHECK_EQ(sink.calls(), 0);
  CHECK_TRUE(dispatcher.last_outcome() == Dispatcher::Outcome::kRejected);

  // The unknown op is answered as such and also never reaches the sink.
  Request unknown;
  unknown.op = Op::kUnknown;
  unknown.request_id = 2;
  r = dispatcher.Dispatch(unknown);
  CHECK_TRUE(r.status == Status::kUnknownOp);
  CHECK_EQ(sink.calls(), 0);

  // No sink installed: a well-formed request is 'unsupported', not silently dropped.
  Dispatcher bare(nullptr);
  Request good;
  good.op = Op::kSnapshot;
  good.request_id = 3;
  r = bare.Dispatch(good);
  CHECK_TRUE(r.status == Status::kUnsupported);

  // With a sink, the request is forwarded and the answer is the sink's.
  r = dispatcher.Dispatch(good);
  CHECK_TRUE(r.status == Status::kOk);
  CHECK_EQ(sink.calls(), 1);
  CHECK_TRUE(dispatcher.last_outcome() == Dispatcher::Outcome::kForwarded);
}

void TestIdempotency() {
  BeginCase("a repeated request id returns the first answer and does not re-apply");

  StubSink sink;
  Dispatcher dispatcher(&sink);

  Request request;
  request.op = Op::kCommit;
  request.request_id = 77;

  const Response first = dispatcher.Dispatch(request);
  CHECK_EQ(sink.calls(), 1);
  CHECK_TRUE(dispatcher.last_outcome() == Dispatcher::Outcome::kForwarded);

  // The same id and the same op is a replay: the sink is not asked again.
  const Response second = dispatcher.Dispatch(request);
  CHECK_EQ(sink.calls(), 1);
  CHECK_TRUE(dispatcher.last_outcome() == Dispatcher::Outcome::kReplayed);
  CHECK_TRUE(second.status == first.status);
  CHECK_EQ(second.request_id, first.request_id);
  CHECK_TRUE(second.value.kind == first.value.kind);
  CHECK_TRUE(second.value.boolean == first.value.boolean);

  // A different id with the same body is a new request and does reach the sink.
  Request other = request;
  other.request_id = 78;
  dispatcher.Dispatch(other);
  CHECK_EQ(sink.calls(), 2);

  // The window is bounded: after kRecentRequests more requests, the oldest id is forgotten and
  // asking for it again is a new request. That is deliberate - the window is not a durable
  // record, and the durable revision check is the service's job (contracts doc §6).
  for (uint64_t id = 100; id < 100 + Dispatcher::kRecentRequests; ++id) {
    Request filler = request;
    filler.request_id = id;
    dispatcher.Dispatch(filler);
  }
  const uint64_t before = sink.calls();
  dispatcher.Dispatch(request);
  CHECK_EQ(sink.calls(), before + 1);

  // Reset clears the window entirely.
  dispatcher.Reset();
  dispatcher.Dispatch(request);
  CHECK_EQ(sink.calls(), before + 2);
}

void TestEndToEndStubRoundTrip() {
  BeginCase("a whole trip: guest path -> request -> stub service -> response -> guest read");

  // This is the host half of the round trip the guest will perform: it builds the path a guest
  // script would pass to a file builtin, parses it, dispatches it, and encodes the answer the
  // guest would read back. Until the authored screen exists this is the strongest available
  // proof that the channel's two ends agree.
  StubSink sink;
  Dispatcher dispatcher(&sink);

  Request request;
  request.op = Op::kSnapshot;
  request.request_id = 0xABCDEF;
  const std::string guest_path = EncodeRequestPath(request);

  Request parsed;
  std::string error;
  CHECK_TRUE(ParseRequestPath(guest_path, &parsed, &error));
  CHECK_TRUE(dispatcher.Dispatch(parsed).status == Status::kOk);

  const Response answer = dispatcher.Dispatch(parsed);
  CHECK_TRUE(dispatcher.last_outcome() == Dispatcher::Outcome::kReplayed);
  const std::string guest_readback = EncodeResponse(answer);

  Response decoded;
  CHECK_TRUE(DecodeResponse(guest_readback, &decoded, &error));
  CHECK_TRUE(decoded.status == Status::kOk);
  // The stub echoes the request id as the value, so the trip is observable end to end.
  CHECK_TRUE(decoded.value.kind == ValueKind::kInt);
  CHECK_EQ(static_cast<uint64_t>(decoded.value.integer), request.request_id);
}

void TestTextRequestGrammar() {
  BeginCase("the text form a DTA script writes parses into the same request");

  struct Case {
    const char* path;
    Op op;
    const char* setting;
    ValueKind kind;
  };
  const Case cases[] = {
      {"rbbz-settings/snapshot/interface.hide_hud/b:1", Op::kSnapshot, "interface.hide_hud",
       ValueKind::kBool},
      {"rbbz-settings/set/interface.hide_hud/b:0", Op::kSet, "interface.hide_hud",
       ValueKind::kBool},
      {"rbbz-settings/set/graphics.resolution/t:1920x1080", Op::kSet, "graphics.resolution",
       ValueKind::kText},
      {"rbbz-settings/set/graphics.plots.force/i:-1", Op::kSet, "graphics.plots.force",
       ValueKind::kInt},
      {"rbbz-settings/set/general.hidden/l:a,b,c", Op::kSet, "general.hidden", ValueKind::kList},
      {"rbbz-settings/commit/-/-", Op::kCommit, "", ValueKind::kNone},
      {"rbbz-settings/begin/-/-", Op::kBeginEdit, "", ValueKind::kNone},
      {"rbbz-settings/cancel/-/-", Op::kCancelEdit, "", ValueKind::kNone},
      {"rbbz-settings/cancel-field/interface.hide_hud/-", Op::kCancelField,
       "interface.hide_hud", ValueKind::kNone},
      // A path with no trailing segments at all is the minimal legal form.
      {"rbbz-settings/invoke", Op::kInvoke, "", ValueKind::kNone},
  };
  for (const Case& item : cases) {
    Request request;
    std::string error;
    const bool ok = ParseTextRequest(item.path, &request, &error);
    CHECK_TRUE(ok);
    if (!ok) {
      std::printf("         %s refused: %s\n", item.path, error.c_str());
      continue;
    }
    CHECK_TRUE(request.op == item.op);
    CHECK_TRUE(request.setting == item.setting);
    CHECK_TRUE(request.value.kind == item.kind);
    // A text request carries no id, which is what keeps the store's ack honest.
    CHECK_EQ(request.request_id, 0u);
    // Re-encoding gives the canonical spelling, which parses back to the same request - the
    // property the log and a paste-back-into-a-test both rest on. The literal path may be a
    // shorter legal form (`commit` rather than `commit/-/-`), so the invariant is that the
    // canonical form is stable, not that it is byte-identical to the input.
    Request again;
    std::string again_error;
    const std::string canonical = EncodeTextRequest(request);
    CHECK_TRUE(ParseTextRequest(canonical, &again, &again_error));
    CHECK_TRUE(again.op == request.op);
    CHECK_TRUE(again.setting == request.setting);
    CHECK_TRUE(again.value.kind == request.value.kind);
    CHECK_TRUE(EncodeTextRequest(again) == canonical);
  }

  // One canonical example spelled out, so the shape itself is pinned.
  Request request;
  std::string error;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/set/interface.hide_hud/b:0", &request, &error));
  CHECK_TRUE(EncodeTextRequest(request) == "rbbz-settings/set/interface.hide_hud/b:0");

  // The op vocabulary is shared with the binary form's names.
  CHECK_TRUE(OpFromToken("set") == Op::kSet);
  CHECK_TRUE(OpFromToken("commit") == Op::kCommit);
  CHECK_TRUE(OpFromToken("nonsense") == Op::kUnknown);
  CHECK_TRUE(std::string(OpToken(Op::kSnapshot)) == "snapshot");
}

void TestTextRequestRefusals() {
  BeginCase("a text path off the grammar is refused, never half-applied");

  const char* bad[] = {
      "rbbz-settingsx/set/interface.hide_hud/b:1",  // not our namespace
      "rbbz-settings/",                             // no op
      "rbbz-settings//",                            // empty op
      "rbbz-settings/nonsense/x/-",                 // unknown op
      "rbbz-settings/set//b:1",                     // empty setting
      "rbbz-settings/set/a/b/c",                    // too many segments
      "rbbz-settings/set/interface hide hud/b:1",   // a space in the token
      "rbbz-settings/set/interface.hide_hud/x:1",   // unknown value kind
      "rbbz-settings/set/interface.hide_hud/b:2",   // bool is 0 or 1
      "rbbz-settings/set/interface.hide_hud/t:",    // empty text
      "rbbz-settings/set/interface.hide_hud/l:",    // empty list
      "rbbz-settings/set/interface.hide_hud/l:a,,b",  // empty list item
      "rbbz-settings/set/interface.hide_hud/i:12x",  // trailing junk in an int
      "rbbz-settings/set/interface.hide_hud/1",     // no kind prefix
      "rbbz-settings/set/interface.hide_hud/b:1\n",  // a line break
  };
  for (const char* path : bad) {
    Request request;
    std::string error;
    CHECK_FALSE(ParseTextRequest(path, &request, &error));
    CHECK_TRUE(!error.empty());
  }

  // A 65-character setting token is over the bound.
  std::string long_token = "rbbz-settings/set/";
  long_token += std::string(65, 'a');
  long_token += "/-";
  Request request;
  std::string error;
  CHECK_FALSE(ParseTextRequest(long_token, &request, &error));
}

void TestTextRequestsAlwaysDispatch() {
  BeginCase("an id-less text request is never treated as a replay");

  MemorySink store;
  Dispatcher dispatcher(&store);

  Request first;
  std::string error;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/set/graphics.resolution/t:1280x720", &first, &error));
  CHECK_TRUE(dispatcher.Dispatch(first).status == Status::kOk);

  // The same op with a different value must reach the store: a text request has no id, so the
  // replay window cannot be what decides its fate.
  Request second;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/set/graphics.resolution/t:1920x1080", &second, &error));
  CHECK_TRUE(dispatcher.Dispatch(second).status == Status::kOk);
  CHECK_TRUE(dispatcher.last_outcome() == Dispatcher::Outcome::kForwarded);

  // A read of the staged value is still false: only a commit makes it readable.
  Request query;
  std::string query_error;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/snapshot/graphics.resolution/t:1920x1080", &query,
                              &query_error));
  CHECK_FALSE(store.Matches(query));

  Request commit;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/commit/-/-", &commit, &error));
  CHECK_TRUE(dispatcher.Dispatch(commit).status == Status::kOk);

  WireValue stored;
  CHECK_TRUE(store.Get("graphics.resolution", &stored));
  CHECK_TRUE(stored.kind == ValueKind::kText);
  CHECK_TRUE(stored.text == "1920x1080");
  CHECK_TRUE(store.Matches(query));

  // While a request that *does* carry an id still gets the replay protection.
  Request framed = first;
  framed.request_id = 99;
  framed.op = Op::kSet;
  framed.value = WireValue::Bool(true);
  CHECK_TRUE(dispatcher.Dispatch(framed).status == Status::kOk);
  CHECK_TRUE(dispatcher.last_outcome() == Dispatcher::Outcome::kForwarded);
  CHECK_TRUE(dispatcher.Dispatch(framed).status == Status::kOk);
  CHECK_TRUE(dispatcher.last_outcome() == Dispatcher::Outcome::kReplayed);
}

void TestMemorySinkReadWriteAck() {
  BeginCase("the in-memory store reads, writes and acknowledges what it applied");

  MemorySink store;
  CHECK_TRUE(store.committed_count() == 0);

  // Nothing to read yet.
  Request query;
  std::string error;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/snapshot/interface.hide_hud/b:1", &query, &error));
  CHECK_FALSE(store.Matches(query));

  // A staged value is not committed, so a read of it is still false until the commit.
  Request set;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/set/interface.hide_hud/b:1", &set, &error));
  CHECK_TRUE(store.Handle(set).status == Status::kOk);
  CHECK_EQ(store.staged_count(), 1u);
  CHECK_FALSE(store.Matches(query));

  Request commit;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/commit/-/-", &commit, &error));
  CHECK_TRUE(store.Handle(commit).status == Status::kOk);
  CHECK_EQ(store.committed_count(), 1u);
  CHECK_EQ(store.staged_count(), 0u);
  CHECK_TRUE(store.Matches(query));

  // The other value is the same setting, read differently.
  Request other;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/snapshot/interface.hide_hud/b:0", &other, &error));
  CHECK_FALSE(store.Matches(other));

  // A kind that does not match the stored one is false, not a match.
  Request wrong_kind;
  CHECK_TRUE(ParseTextRequest("rbbz-settings/snapshot/interface.hide_hud/t:1", &wrong_kind, &error));
  CHECK_FALSE(store.Matches(wrong_kind));

  // Cancelling a staged field leaves the committed value alone.
  Request cancel_field;
  CHECK_TRUE(
      ParseTextRequest("rbbz-settings/cancel-field/interface.hide_hud/-", &cancel_field, &error));
  CHECK_TRUE(store.Handle(cancel_field).status == Status::kOk);
  CHECK_TRUE(store.Matches(query));

  // The ack answers for an id that was applied, and not for one that was never seen. An id-less
  // request is never acknowledged: the read half is what a text caller uses.
  Request framed = set;
  framed.request_id = 7;
  CHECK_TRUE(store.Handle(framed).status == Status::kOk);
  CHECK_TRUE(store.Acknowledged(7));
  CHECK_FALSE(store.Acknowledged(8));
  CHECK_FALSE(store.Acknowledged(0));

  // A request the store refuses is not an acknowledgement.
  Request invalid = set;
  invalid.request_id = 9;
  invalid.setting.clear();
  CHECK_TRUE(store.Handle(invalid).status == Status::kInvalid);
  CHECK_FALSE(store.Acknowledged(9));
  CHECK_EQ(store.handled_count(), 2u);  // id 7 applied, id 9 refused, id 0 never recorded

  store.Reset();
  CHECK_EQ(store.committed_count(), 0u);
  CHECK_FALSE(store.Matches(query));
  CHECK_FALSE(store.Acknowledged(7));
}

}  // namespace

int main() {
  TestNamespaceExactness();
  TestRequestRoundTrip();
  TestMalformedFrames();
  TestResponseRoundTrip();
  TestDispatcherStatuses();
  TestIdempotency();
  TestEndToEndStubRoundTrip();
  TestTextRequestGrammar();
  TestTextRequestRefusals();
  TestTextRequestsAlwaysDispatch();
  TestMemorySinkReadWriteAck();
  return Finish();
}
