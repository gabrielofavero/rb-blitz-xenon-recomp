// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The frame codec and dispatcher behind src/hooks/settings_bridge.h. See that header for the
// transport decision and the measurements it rests on; this file is the arithmetic and the
// rules. SDK-free on purpose.

#include "hooks/settings_bridge.h"

#include <algorithm>

namespace rb_blitz::settings::bridge {

namespace {

// The frame's magic. A frame that does not start with it is not ours, whatever else it looks
// like, so a guest string that happens to sit in our namespace still cannot smuggle a request.
constexpr char kMagic[4] = {'R', 'B', 'Z', '1'};

// 0xEDB88320, the reflected CRC-32 polynomial. A plain table-less implementation: a frame is at
// most a few KiB and this runs once per request, so the loop cost is irrelevant next to the
// guest call it rides on.
uint32_t Crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
  }
  return ~crc;
}

std::string ToHex(const uint8_t* data, size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(size * 2);
  for (size_t i = 0; i < size; ++i) {
    out[2 * i] = kHex[data[i] >> 4];
    out[2 * i + 1] = kHex[data[i] & 0x0F];
  }
  return out;
}

int HexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Hex -> bytes. Rejects an odd length and any non-hex character rather than guessing.
bool FromHex(std::string_view text, std::vector<uint8_t>* out) {
  if (text.empty() || text.size() % 2 != 0) {
    return false;
  }
  out->clear();
  out->reserve(text.size() / 2);
  for (size_t i = 0; i < text.size(); i += 2) {
    const int hi = HexNibble(text[i]);
    const int lo = HexNibble(text[i + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out->push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return true;
}

// A cursor over a decoded frame that never reads past its end and never trusts a length. The
// view deliberately excludes the trailing checksum, so a well-formed frame ends exactly at its
// last field and `AtEnd` is a real check rather than "four bytes to go".
class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool u8(uint8_t* out) {
    if (pos_ + 1 > size_) return false;
    *out = data_[pos_++];
    return true;
  }
  bool u16(uint16_t* out) {
    if (pos_ + 2 > size_) return false;
    *out = static_cast<uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
    pos_ += 2;
    return true;
  }
  bool u32(uint32_t* out) {
    if (pos_ + 4 > size_) return false;
    *out = static_cast<uint32_t>(data_[pos_]) | (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
           (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
           (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return true;
  }
  bool u64(uint64_t* out) {
    uint32_t lo = 0, hi = 0;
    if (!u32(&lo) || !u32(&hi)) return false;
    *out = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
    return true;
  }
  // A length-prefixed byte string, bounded before anything is copied.
  bool Str(std::string* out) {
    uint32_t length = 0;
    if (!u32(&length)) return false;
    if (length > kMaxStringBytes) return false;
    if (pos_ + length > size_) return false;
    out->assign(reinterpret_cast<const char*>(data_ + pos_), length);
    pos_ += length;
    return true;
  }
  bool AtEnd() const { return pos_ == size_; }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
};

class Writer {
 public:
  void u8(uint8_t v) { bytes_.push_back(v); }
  void u16(uint16_t v) {
    bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
    bytes_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
  }
  void u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) bytes_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
  }
  void Bytes(const char* data, size_t size) {
    bytes_.insert(bytes_.end(), data, data + size);
  }
  void Str(std::string_view s) {
    u32(static_cast<uint32_t>(s.size()));
    Bytes(s.data(), s.size());
  }
  const std::vector<uint8_t>& bytes() const { return bytes_; }

 private:
  std::vector<uint8_t> bytes_;
};

// One value's payload, shared by requests and responses.
void WriteValue(Writer* w, const WireValue& value) {
  w->u8(static_cast<uint8_t>(value.kind));
  switch (value.kind) {
    case ValueKind::kBool:
      w->u8(value.boolean ? 1 : 0);
      break;
    case ValueKind::kInt:
      w->u64(static_cast<uint64_t>(value.integer));
      break;
    case ValueKind::kText:
      w->Str(value.text);
      break;
    case ValueKind::kList:
      w->u32(static_cast<uint32_t>(value.list.size()));
      for (const std::string& item : value.list) {
        w->Str(item);
      }
      break;
    case ValueKind::kNone:
      break;
  }
}

bool ReadValue(Reader* r, WireValue* value) {
  uint8_t kind = 0;
  if (!r->u8(&kind)) return false;
  if (kind > static_cast<uint8_t>(ValueKind::kList)) return false;
  value->kind = static_cast<ValueKind>(kind);
  value->boolean = false;
  value->integer = 0;
  value->text.clear();
  value->list.clear();
  switch (value->kind) {
    case ValueKind::kNone:
      return true;
    case ValueKind::kBool: {
      uint8_t b = 0;
      if (!r->u8(&b) || b > 1) return false;
      value->boolean = b != 0;
      return true;
    }
    case ValueKind::kInt: {
      uint64_t raw = 0;
      if (!r->u64(&raw)) return false;
      value->integer = static_cast<int64_t>(raw);
      return true;
    }
    case ValueKind::kText:
      return r->Str(&value->text);
    case ValueKind::kList: {
      uint32_t count = 0;
      if (!r->u32(&count) || count > kMaxListItems) return false;
      value->list.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        std::string item;
        if (!r->Str(&item)) return false;
        value->list.push_back(std::move(item));
      }
      return true;
    }
  }
  return false;
}

// A message is for a person reading a log or a help bar; it never carries data and never grows
// past a bound, whatever the sink produced.
std::string BoundMessage(std::string text) {
  constexpr size_t kMaxMessage = 256;
  for (char& c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7F) {
      c = ' ';
    }
  }
  if (text.size() > kMaxMessage) {
    text.resize(kMaxMessage);
  }
  return text;
}

}  // namespace

WireValue WireValue::Bool(bool value) {
  WireValue v;
  v.kind = ValueKind::kBool;
  v.boolean = value;
  return v;
}
WireValue WireValue::Int(int64_t value) {
  WireValue v;
  v.kind = ValueKind::kInt;
  v.integer = value;
  return v;
}
WireValue WireValue::Text(std::string value) {
  WireValue v;
  v.kind = ValueKind::kText;
  v.text = std::move(value);
  return v;
}
WireValue WireValue::List(std::vector<std::string> values) {
  WireValue v;
  v.kind = ValueKind::kList;
  v.list = std::move(values);
  return v;
}

bool IsNamespacedPath(std::string_view path) {
  constexpr std::string_view kPrefix = kNamespacePrefix;
  return path.size() > kPrefix.size() && path.substr(0, kPrefix.size()) == kPrefix;
}

const char* OpToken(const Op op) {
  switch (op) {
    case Op::kSnapshot: return "snapshot";
    case Op::kBeginEdit: return "begin";
    case Op::kSet: return "set";
    case Op::kCancelField: return "cancel-field";
    case Op::kCommit: return "commit";
    case Op::kCancelEdit: return "cancel";
    case Op::kInvoke: return "invoke";
    case Op::kRequestRestart: return "restart";
    case Op::kUnknown: return "";
  }
  return "";
}

Op OpFromToken(const std::string_view token) {
  for (const Op op : {Op::kSnapshot, Op::kBeginEdit, Op::kSet, Op::kCancelField, Op::kCommit,
                      Op::kCancelEdit, Op::kInvoke, Op::kRequestRestart}) {
    if (token == OpToken(op)) {
      return op;
    }
  }
  return Op::kUnknown;
}

namespace {

// The alphabet a setting token or a text value may use. Deliberately narrow: the whole point of the
// exact namespace is that a malformed request is refused rather than passed on, and a character
// that has to be escaped to reach a log or a file name has no business in one.
bool IsTokenChar(const char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
         c == '.' || c == '-' || c == ':' || c == '+';
}

bool IsToken(const std::string_view text, const size_t max_length) {
  if (text.empty() || text.size() > max_length) {
    return false;
  }
  for (const char c : text) {
    if (!IsTokenChar(c)) {
      return false;
    }
  }
  return true;
}

// Splits "a/b/c" into at most three segments on '/' alone. A segment may be empty, which is what
// makes a malformed path detectable rather than silently shortened.
std::vector<std::string_view> SplitSegments(const std::string_view rest) {
  std::vector<std::string_view> out;
  size_t start = 0;
  for (size_t i = 0; i <= rest.size(); ++i) {
    if (i == rest.size() || rest[i] == '/') {
      out.push_back(rest.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

}  // namespace

bool ParseTextRequest(const std::string_view path, Request* out, std::string* error) {
  auto fail = [error](const char* why) {
    if (error != nullptr) *error = why;
    return false;
  };
  if (!IsNamespacedPath(path)) {
    return fail("not in the settings namespace");
  }
  if (path.size() > kMaxPathChars) {
    return fail("path longer than the bound");
  }
  const std::string_view rest = path.substr(sizeof(kNamespacePrefix) - 1);
  if (rest.find_first_of("\r\n") != std::string_view::npos) {
    return fail("a path segment carries a line break");
  }
  const std::vector<std::string_view> segments = SplitSegments(rest);
  if (segments.empty() || segments.size() > 3) {
    return fail("a text request is op[/setting[/value]]");
  }

  Request request;
  request.version = kProtocolVersion;
  request.op = OpFromToken(segments[0]);
  if (request.op == Op::kUnknown) {
    return fail("unknown operation token");
  }

  if (segments.size() >= 2 && segments[1] != "-") {
    if (!IsToken(segments[1], 64)) {
      return fail("setting token is empty, too long or off-alphabet");
    }
    request.setting.assign(segments[1]);
  }

  if (segments.size() == 3 && segments[2] != "-") {
    const std::string_view value = segments[2];
    if (value.size() < 2 || value[1] != ':') {
      return fail("a value needs a kind prefix");
    }
    const std::string_view body = value.substr(2);
    switch (value[0]) {
      case 'b':
        if (body == "1") {
          request.value = WireValue::Bool(true);
        } else if (body == "0") {
          request.value = WireValue::Bool(false);
        } else {
          return fail("a bool value is b:0 or b:1");
        }
        break;
      case 'i': {
        if (!IsToken(body, 20)) {
          return fail("an int value is off-alphabet or too long");
        }
        int64_t number = 0;
        try {
          size_t used = 0;
          number = std::stoll(std::string(body), &used);
          if (used != body.size()) {
            return fail("an int value has trailing characters");
          }
        } catch (...) {
          return fail("an int value does not parse");
        }
        request.value = WireValue::Int(number);
        break;
      }
      case 't':
        if (!IsToken(body, 64)) {
          return fail("a text value is empty, too long or off-alphabet");
        }
        request.value = WireValue::Text(std::string(body));
        break;
      case 'l': {
        std::vector<std::string> items;
        size_t start = 0;
        for (size_t i = 0; i <= body.size(); ++i) {
          if (i == body.size() || body[i] == ',') {
            const std::string_view item = body.substr(start, i - start);
            if (!IsToken(item, 64) || items.size() >= kMaxListItems) {
              return fail("a list item is empty, too long, off-alphabet or one too many");
            }
            items.emplace_back(item);
            start = i + 1;
          }
        }
        if (items.empty()) {
          return fail("a list value needs at least one item");
        }
        request.value = WireValue::List(std::move(items));
        break;
      }
      default:
        return fail("unknown value kind");
    }
  }

  *out = std::move(request);
  if (error != nullptr) error->clear();
  return true;
}

std::string EncodeTextRequest(const Request& request) {
  std::string out = kNamespacePrefix;
  out += OpToken(request.op);
  out += '/';
  out += request.setting.empty() ? "-" : request.setting;
  out += '/';
  switch (request.value.kind) {
    case ValueKind::kNone:
      out += '-';
      break;
    case ValueKind::kBool:
      out += request.value.boolean ? "b:1" : "b:0";
      break;
    case ValueKind::kInt:
      out += "i:" + std::to_string(request.value.integer);
      break;
    case ValueKind::kText:
      out += "t:" + request.value.text;
      break;
    case ValueKind::kList: {
      out += "l:";
      for (size_t i = 0; i < request.value.list.size(); ++i) {
        if (i != 0) out += ',';
        out += request.value.list[i];
      }
      break;
    }
  }
  return out;
}

bool ParseRequestPath(std::string_view path, Request* out, std::string* error) {
  auto fail = [error](const char* why) {
    if (error != nullptr) *error = why;
    return false;
  };
  if (!IsNamespacedPath(path)) {
    return fail("not in the settings namespace");
  }
  if (path.size() > kMaxPathChars) {
    return fail("path longer than the bound");
  }
  const std::string_view hex = path.substr(sizeof(kNamespacePrefix) - 1);

  std::vector<uint8_t> frame;
  if (!FromHex(hex, &frame)) {
    return fail("payload is not whole-byte hex");
  }
  if (frame.size() < sizeof(kMagic) + 2 + 2 + 8 + 4 + 1 + 4) {
    return fail("frame shorter than its own header");
  }
  if (frame.size() > kMaxFrameBytes) {
    return fail("frame larger than the bound");
  }
  // The checksum covers every byte before it, so a truncated or edited frame is refused before
  // any field is interpreted.
  uint32_t stored = 0;
  for (int i = 0; i < 4; ++i) {
    stored |= static_cast<uint32_t>(frame[frame.size() - 4 + i]) << (8 * i);
  }
  const uint32_t computed = Crc32(frame.data(), frame.size() - 4);
  if (stored != computed) {
    return fail("frame checksum mismatch");
  }
  if (!std::equal(kMagic, kMagic + 4, frame.begin())) {
    return fail("frame magic mismatch");
  }

  Reader reader(frame.data(), frame.size() - 4);
  for (int i = 0; i < 4; ++i) {
    uint8_t byte = 0;
    if (!reader.u8(&byte)) return fail("frame truncated in the magic");
  }
  Request request;
  if (!reader.u16(&request.version)) return fail("frame truncated in the version");
  uint16_t op = 0;
  if (!reader.u16(&op)) return fail("frame truncated in the op");
  request.op = static_cast<Op>(op);
  if (!reader.u64(&request.request_id)) return fail("frame truncated in the request id");
  if (!reader.Str(&request.setting)) return fail("frame truncated in the setting token");
  if (!ReadValue(&reader, &request.value)) return fail("frame truncated in the value");
  if (!reader.AtEnd()) return fail("frame has trailing bytes");

  *out = std::move(request);
  if (error != nullptr) error->clear();
  return true;
}

std::string EncodeRequestPath(const Request& request) {
  Writer w;
  w.Bytes(kMagic, sizeof(kMagic));
  w.u16(request.version);
  w.u16(static_cast<uint16_t>(request.op));
  w.u64(request.request_id);
  w.Str(request.setting);
  WriteValue(&w, request.value);
  const std::vector<uint8_t>& bytes = w.bytes();
  const uint32_t crc = Crc32(bytes.data(), bytes.size());
  std::string out = kNamespacePrefix;
  out += ToHex(bytes.data(), bytes.size());
  const uint8_t tail[4] = {static_cast<uint8_t>(crc & 0xFF), static_cast<uint8_t>((crc >> 8) & 0xFF),
                           static_cast<uint8_t>((crc >> 16) & 0xFF),
                           static_cast<uint8_t>((crc >> 24) & 0xFF)};
  out += ToHex(tail, sizeof(tail));
  return out;
}

std::string EncodeResponse(const Response& response) {
  Writer w;
  w.Bytes(kMagic, sizeof(kMagic));
  w.u16(response.version);
  w.u16(static_cast<uint16_t>(response.status));
  w.u64(response.request_id);
  WriteValue(&w, response.value);
  w.Str(BoundMessage(response.message));
  const std::vector<uint8_t>& bytes = w.bytes();
  const uint32_t crc = Crc32(bytes.data(), bytes.size());
  std::string out = ToHex(bytes.data(), bytes.size());
  const uint8_t tail[4] = {static_cast<uint8_t>(crc & 0xFF), static_cast<uint8_t>((crc >> 8) & 0xFF),
                           static_cast<uint8_t>((crc >> 16) & 0xFF),
                           static_cast<uint8_t>((crc >> 24) & 0xFF)};
  out += ToHex(tail, sizeof(tail));
  return out;
}

bool DecodeResponse(std::string_view text, Response* out, std::string* error) {
  auto fail = [error](const char* why) {
    if (error != nullptr) *error = why;
    return false;
  };
  std::vector<uint8_t> frame;
  if (!FromHex(text, &frame)) {
    return fail("response is not whole-byte hex");
  }
  if (frame.size() < sizeof(kMagic) + 2 + 2 + 8 + 1 + 4) {
    return fail("response shorter than its own header");
  }
  if (frame.size() > kMaxFrameBytes) {
    return fail("response larger than the bound");
  }
  uint32_t stored = 0;
  for (int i = 0; i < 4; ++i) {
    stored |= static_cast<uint32_t>(frame[frame.size() - 4 + i]) << (8 * i);
  }
  if (stored != Crc32(frame.data(), frame.size() - 4)) {
    return fail("response checksum mismatch");
  }
  if (!std::equal(kMagic, kMagic + 4, frame.begin())) {
    return fail("response magic mismatch");
  }

  Reader reader(frame.data(), frame.size() - 4);
  for (int i = 0; i < 4; ++i) {
    uint8_t byte = 0;
    if (!reader.u8(&byte)) return fail("response truncated in the magic");
  }
  Response response;
  if (!reader.u16(&response.version)) return fail("response truncated in the version");
  uint16_t status = 0;
  if (!reader.u16(&status)) return fail("response truncated in the status");
  response.status = static_cast<Status>(status);
  if (!reader.u64(&response.request_id)) return fail("response truncated in the request id");
  if (!ReadValue(&reader, &response.value)) return fail("response truncated in the value");
  if (!reader.Str(&response.message)) return fail("response truncated in the message");
  if (!reader.AtEnd()) return fail("response has trailing bytes");

  *out = std::move(response);
  if (error != nullptr) error->clear();
  return true;
}

Response Dispatcher::Dispatch(const Request& request) {
  // A version this build does not know is refused before anything is interpreted, and refused
  // without being remembered: it cannot be a replay of one of our own requests.
  if (request.version != kProtocolVersion) {
    last_outcome_ = Outcome::kRejected;
    Response response;
    response.version = kProtocolVersion;
    response.status = Status::kBadVersion;
    response.request_id = request.request_id;
    response.message = "unsupported protocol version";
    return response;
  }

  // The idempotency window: the same id and op returns the first answer, so a re-sent frame
  // cannot stage or commit a change twice. A request with **no** id (the text form a DTA script
  // writes) is not cached and not replayed: every text request is applied as it arrives, and the
  // ops it can spell are idempotent by construction (setting a value twice is setting it once,
  // committing with nothing staged writes nothing).
  if (request.request_id != 0) {
    for (const Recent& recent : recent_) {
      if (recent.request_id == request.request_id && recent.op == request.op) {
        last_outcome_ = Outcome::kReplayed;
        return recent.response;
      }
    }
  }

  Response response;
  response.version = kProtocolVersion;
  response.request_id = request.request_id;
  if (request.op == Op::kUnknown) {
    // Answered, but never cached: an unknown op is not a successful request to replay.
    last_outcome_ = Outcome::kRejected;
    response.status = Status::kUnknownOp;
    response.message = "unknown operation";
    return response;
  }
  if (sink_ == nullptr) {
    last_outcome_ = Outcome::kRejected;
    response.status = Status::kUnsupported;
    response.message = "no settings service is installed";
    return response;
  }
  response = sink_->Handle(request);
  response.version = kProtocolVersion;
  response.request_id = request.request_id;
  response.message = BoundMessage(response.message);
  last_outcome_ = Outcome::kForwarded;

  if (request.request_id != 0) {
    recent_.push_back(Recent{request.request_id, request.op, response});
    while (recent_.size() > kRecentRequests) {
      recent_.pop_front();
    }
  }
  return response;
}

void Dispatcher::Reset() { recent_.clear(); }

namespace {

// Two wire values are the same when their kind and their content match. Used by the membership
// query, so a probe compares like with like: a text token is never equal to an integer that
// happens to share its digits.
bool SameValue(const WireValue& a, const WireValue& b) {
  if (a.kind != b.kind) {
    return false;
  }
  switch (a.kind) {
    case ValueKind::kNone:
      return true;
    case ValueKind::kBool:
      return a.boolean == b.boolean;
    case ValueKind::kInt:
      return a.integer == b.integer;
    case ValueKind::kText:
      return a.text == b.text;
    case ValueKind::kList:
      return a.list == b.list;
  }
  return false;
}

}  // namespace

Response MemorySink::Handle(const Request& request) {
  Response response;
  response.version = kProtocolVersion;
  response.request_id = request.request_id;
  Status status = Status::kOk;

  switch (request.op) {
    case Op::kSnapshot:
      // The one-bit channel cannot carry a snapshot. The read half is the membership query
      // (Matches), so this answers with the count and says what it is.
      response.value = WireValue::Int(static_cast<int64_t>(committed_.size()));
      response.message = "in-memory store";
      break;
    case Op::kBeginEdit:
      staged_.clear();
      response.message = "edit begun";
      break;
    case Op::kSet:
      if (request.setting.empty()) {
        status = Status::kInvalid;
        response.message = "a set needs a setting token";
      } else {
        staged_[request.setting] = request.value;
        response.message = "staged";
      }
      break;
    case Op::kCancelField:
      staged_.erase(request.setting);
      response.message = "field restored";
      break;
    case Op::kCancelEdit:
      staged_.clear();
      response.message = "edit cancelled";
      break;
    case Op::kCommit: {
      last_commit_changes_.clear();
      if (staged_.empty()) {
        response.message = "nothing to commit";
      }
      for (auto& entry : staged_) {
        const auto before = committed_.find(entry.first);
        const bool changed = before == committed_.end() || !SameValue(before->second, entry.second);
        committed_[entry.first] = entry.second;
        if (changed) {
          last_commit_changes_.push_back(entry.first);
        }
      }
      staged_.clear();
      response.value = WireValue::Bool(true);
      response.message = "committed";
      break;
    }
    case Op::kInvoke:
      response.message = "action taken";
      break;
    case Op::kRequestRestart:
      response.message = "restart requested";
      break;
    case Op::kUnknown:
      status = Status::kUnknownOp;
      response.message = "unknown operation";
      break;
  }

  response.status = status;
  // Only a request this store actually applied is acknowledged, so a guest polling the ack
  // cannot mistake a rejection for a completed write. An id-less text request is never recorded:
  // "has request 0 been done" is not a question with an answer, and the read half (the snapshot
  // membership query) is what a text caller uses to see its own state.
  if (request.request_id != 0) {
    handled_[request.request_id] = status;
  }
  return response;
}

bool MemorySink::Matches(const Request& request) const {
  const auto found = committed_.find(request.setting);
  if (found == committed_.end()) {
    return false;
  }
  return SameValue(found->second, request.value);
}

bool MemorySink::Acknowledged(const uint64_t request_id) const {
  const auto found = handled_.find(request_id);
  return found != handled_.end() && found->second == Status::kOk;
}

bool MemorySink::Get(const std::string_view setting, WireValue* out) const {
  const auto found = committed_.find(std::string(setting));
  if (found == committed_.end()) {
    return false;
  }
  if (out != nullptr) {
    *out = found->second;
  }
  return true;
}

void MemorySink::Reset() {
  committed_.clear();
  staged_.clear();
  handled_.clear();
  last_commit_changes_.clear();
}

Response StubSink::Handle(const Request& request) {
  ++calls_;
  last_request_id_ = request.request_id;
  Response response;
  response.version = kProtocolVersion;
  response.request_id = request.request_id;
  response.status = Status::kOk;
  switch (request.op) {
    case Op::kSnapshot:
      // Echo the request id back as a value so a round trip is observable end to end without a
      // real settings service.
      response.value = WireValue::Int(static_cast<int64_t>(request.request_id));
      response.message = "stub snapshot";
      break;
    case Op::kSet:
      response.value = request.value;
      response.message = "stub set";
      break;
    case Op::kCommit:
      response.value = WireValue::Bool(true);
      response.message = "stub commit";
      break;
    default:
      response.message = "stub ok";
      break;
  }
  return response;
}

}  // namespace rb_blitz::settings::bridge
