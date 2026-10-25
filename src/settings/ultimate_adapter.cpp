// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P6's provider adapter; src/settings/ultimate_adapter.h explains the design and the four
// things it does. This file is the implementation: the ark index reader, the compiled-DTA
// shape check, the frozen row bindings, and the provider's own settings file.

#include "settings/ultimate_adapter.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

#include "ui/dtb_author.h"
#include "fs/atomic_file.h"

namespace rb_blitz::settings::ultimate_adapter {
namespace {

// ------------------------------------------------------------------------------------------------
// The record table
// ------------------------------------------------------------------------------------------------
// Every path is the payload's own spelling, exactly as its ark index stores it (verified against
// `game/ultimate/gen/patch_xbox_0.ark`: 62 records, 42 of them .dtb).

constexpr std::array<RecordSpec, kRecordCount> kRecords{{
    {Record::kModSettings, "ulti/gen/mod_settings.dtb", "dtb",
     "the panels, their rows, each row's kind and variable - without it there is nothing to bind"},
    {Record::kModSettingsRw, "ulti/gen/mod_settings_rw.dtb", "dtb",
     "the persisted variable list and the save/load statements"},
    {Record::kUltiMacros, "ulti/gen/ulti_macros.dtb", "dtb",
     "the row-kind macros and MOD_SETTINGS_PATH (GAME:/ulti_settings.dta)"},
    {Record::kUltiFuncs, "ulti/gen/ulti_funcs.dtb", "dtb",
     "row formatting/selection and the mod's second writer"},
    {Record::kUltiInit, "ulti/gen/ulti_init.dtb", "dtb",
     "the compiled defaults a row falls back to when the file has no pair for it"},
    {Record::kListResource, "ui/resource/lists/gen/list_mod_settings.milo_xbox", "milo_xbox",
     "the mod's own row presentation for the pause scene; listed so a payload is reported whole, "
     "and deliberately not reused - the merged screen draws retail list primitives"},
}};

// ------------------------------------------------------------------------------------------------
// The payload's container format (Harmonix ARK v6)
// ------------------------------------------------------------------------------------------------
// The header is XORed with a Park-Miller (16807) keystream seeded from its own first word, and an
// unencrypted header starts with its version (<= 10) and is read as-is. This is the same public
// format `scripts/hmx_ark.py` implements; this reader is an independent one, cut down to the
// index. Only the fields the index needs are decoded, and every count is checked against the
// bytes actually left, so a truncated or hostile header is an error rather than a crash.

constexpr int32_t kLcgMod = 0x7FFFFFFF;
constexpr int32_t kLcgDiv = 0x1F31D;   // 2147483647 / 16807
constexpr int32_t kLcgRem = 0xB14;     // 2147483647 % 16807
constexpr uint32_t kMaxPlaintextVersion = 10;

// `#define name value` is a text node carrying the macro's name, with the value as the next root.
// ui/dtb_author.h exposes the tags this project *writes*; `#define` is not one of them, so its
// number is repeated here from the one place it is written down (src/ui/menu_options.cpp's tag
// table) and checked against the parser's own kind table before it is relied on.
constexpr uint32_t kDefineTag = 0x20;

// One Park-Miller step with Schrage's method, in the 32-bit signed arithmetic the title uses:
// the product is taken in 64 bits and narrowed back to the low 32, and a non-positive result is
// shifted up by the modulus. `q` is C's truncating division, which is the reference's own.
int32_t CryptRound(int32_t key) {
  const int64_t q = static_cast<int64_t>(key) / kLcgDiv;
  const int64_t product = (static_cast<int64_t>(key) - q * kLcgDiv) * 16807 - q * kLcgRem;
  const int32_t narrowed =
      static_cast<int32_t>(static_cast<uint32_t>(product & 0xFFFFFFFFll));
  return narrowed <= 0 ? static_cast<int32_t>(narrowed + kLcgMod) : narrowed;
}

uint32_t LoadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t LoadLe64(const uint8_t* p) {
  return static_cast<uint64_t>(LoadLe32(p)) | (static_cast<uint64_t>(LoadLe32(p + 4)) << 32);
}

// Reads the header's plaintext bytes. Returns false when the file is too small to hold a version.
//
// A header whose first word is at or below 10 is already plaintext. Otherwise the seed is the
// first word run through one generator step, the keystream's successive low bytes are XORed over
// everything after it - and when that leaves the version word looking negative, the file carries
// the *complemented* stream (the reference implementation's own disambiguation, kept because the
// real files need it).
bool DecryptHeader(const uint8_t* bytes, size_t size, std::vector<uint8_t>* out) {
  if (size < 8) {
    return false;
  }
  out->clear();
  if (LoadLe32(bytes) <= kMaxPlaintextVersion) {
    out->assign(bytes, bytes + size);
    return true;
  }
  const size_t body_size = size - 4;
  std::vector<uint8_t> plain(body_size, 0);
  std::vector<uint8_t> stream(body_size, 0);
  int32_t key = CryptRound(static_cast<int32_t>(LoadLe32(bytes)));
  for (size_t i = 0; i < body_size; ++i) {
    stream[i] = static_cast<uint8_t>(key & 0xFF);
    key = CryptRound(key);
  }
  for (size_t i = 0; i < body_size; ++i) {
    plain[i] = static_cast<uint8_t>(bytes[4 + i] ^ stream[i]);
  }
  if (body_size >= 4 && (LoadLe32(plain.data()) & 0x80000000u) != 0) {
    for (size_t i = 0; i < body_size; ++i) {
      plain[i] = static_cast<uint8_t>(bytes[4 + i] ^ (stream[i] ^ 0xFF));
    }
  }
  *out = std::move(plain);
  return true;
}

// A bounded cursor over the decrypted header.
class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool Take(size_t count, const uint8_t** out) {
    if (pos_ + count > size_) {
      return false;
    }
    *out = data_ + pos_;
    pos_ += count;
    return true;
  }
  bool U32(uint32_t* out) {
    const uint8_t* p = nullptr;
    if (!Take(4, &p)) return false;
    *out = LoadLe32(p);
    return true;
  }
  bool I32(int32_t* out) {
    uint32_t value = 0;
    if (!U32(&value)) return false;
    *out = static_cast<int32_t>(value);
    return true;
  }
  bool U64(uint64_t* out) {
    const uint8_t* p = nullptr;
    if (!Take(8, &p)) return false;
    *out = LoadLe64(p);
    return true;
  }
  bool I64(int64_t* out) {
    uint64_t value = 0;
    if (!U64(&value)) return false;
    *out = static_cast<int64_t>(value);
    return true;
  }
  bool Bytes(size_t count, std::string* out) {
    const uint8_t* p = nullptr;
    if (!Take(count, &p)) return false;
    out->assign(reinterpret_cast<const char*>(p), count);
    return true;
  }
  size_t Left() const { return size_ - pos_; }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
};

// ------------------------------------------------------------------------------------------------
// Walking a compiled-DTA tree
// ------------------------------------------------------------------------------------------------
// The provider's definitions are ordinary compiled DTA, and the project already has the parser
// (ui/dtb_author.h - the same one that round-trips the shipped retail files byte for byte). What
// this file adds is only the *reading* of that tree, and it deliberately reads it loosely:
// structure comes from a block's first child being a name ("new"/"UIPanel"/"data"/"type"/...), not
// from the exact tag numbers, because the tag numbers are the parser's business and a payload
// should not be rejected over how a node was spelled.

bool IsBlock(const ui::dtb::Node& node) { return !node.kids.empty(); }

// The three tags that can carry a *name*. The same rule as src/ui/menu_options.cpp's `IsRowName`
// ("a row of the option list is a name, and the three kinds that can carry one"), and the reason
// it is needed here: a compiled list can hold `#ifdef` / `#else` / `#endif` markers between its
// elements, and a marker is a text node too. Without the tag test, `HX_PS3` reads as a choice.
bool IsNameTag(uint32_t tag) {
  return tag == ui::dtb::kVar || tag == ui::dtb::kSymbol || tag == ui::dtb::kString;
}

const ui::dtb::Node* Kid(const ui::dtb::Node& node, size_t index) {
  return index < node.kids.size() ? &node.kids[index] : nullptr;
}

const std::string& KidText(const ui::dtb::Node& node, size_t index) {
  static const std::string kEmpty;
  const ui::dtb::Node* kid = Kid(node, index);
  return kid != nullptr ? kid->text : kEmpty;
}

// A block whose first child names `name`, e.g. `(type uOnOFF)` or `(data (...))`.
const ui::dtb::Node* BlockNamed(const ui::dtb::Node& node, std::string_view name) {
  for (const ui::dtb::Node& kid : node.kids) {
    if (IsBlock(kid) && kid.kids.front().text == name) {
      return &kid;
    }
  }
  return nullptr;
}

// Does this subtree mention `text` anywhere? Used for the two facts that are presence, not
// structure: the panel's `SETTINGS_LIST_FILE` member and a row's `reboot` tag.
bool Mentions(const ui::dtb::Node& node, std::string_view text) {
  if (!IsBlock(node)) {
    return node.text == text;
  }
  for (const ui::dtb::Node& kid : node.kids) {
    if (Mentions(kid, text)) {
      return true;
    }
  }
  return false;
}

// Every text in the subtree containing `needle`, in document order. The provider's settings path
// is a string nested inside an `#else` branch of its macro file, so a root-level scan misses it -
// and there is more than one: the macro file carries the *PlayStation* spelling under `HX_PS3`
// first, so which one is ours has to be decided by its content, not by which came first.
void CollectTextsContaining(const ui::dtb::Node& node, std::string_view needle,
                            std::vector<std::string>* out) {
  if (!node.text.empty() && node.text.find(needle) != std::string::npos) {
    out->push_back(node.text);
  }
  for (const ui::dtb::Node& kid : node.kids) {
    CollectTextsContaining(kid, needle, out);
  }
}

// The first command `{name arg ...}` in the subtree, and its first argument. This is how a
// submenu row's destination is read (`{pop_and_goto_screen mod_hud_settings_screen}`).
const ui::dtb::Node* FindNamedBlock(const ui::dtb::Node& node, std::string_view name) {
  if (IsBlock(node) && node.kids.front().text == name) {
    return &node;
  }
  for (const ui::dtb::Node& kid : node.kids) {
    if (const ui::dtb::Node* found = FindNamedBlock(kid, name); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

// A scalar rendered the way the provider's own source spells it, so `ulti_init`'s defaults can be
// reported (and compared) as text.
std::string ScalarText(const ui::dtb::Node& node) {
  char buffer[64];
  if (node.tag == ui::dtb::kFloat) {
    float value = 0.0f;
    std::memcpy(&value, &node.scalar, sizeof(value));
    std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%u", node.scalar);
  }
  return buffer;
}

// The text of a node that is either a name (symbol/var/string) or a number, which is what a
// provider variable's value is in every form it appears in.
std::string AtomText(const ui::dtb::Node& node) {
  if (node.tag == ui::dtb::kInt || node.tag == ui::dtb::kFloat) {
    return ScalarText(node);
  }
  return node.text;
}

// ------------------------------------------------------------------------------------------------
// The row-kind macros
// ------------------------------------------------------------------------------------------------
// `ulti_macros.dtb` defines them, and the panel declarations reference them **by name** (the DTB
// carries a `#define` as a separate root and the reference as a plain symbol, resolved when the
// script is evaluated). So a row's kind is the macro's *name*, which is why an unrecognized
// payload is recognizable at all: a row naming a kind this table does not have is reported.

RowKind KindOfMacro(std::string_view macro) {
  if (macro == "uNormal") return RowKind::kSubmenu;
  if (macro == "uOnOFF") return RowKind::kBool;
  if (macro == "uFakeSld") return RowKind::kSelect;
  if (macro == "uSld") return RowKind::kSlider;
  return RowKind::kUnknown;
}

// ------------------------------------------------------------------------------------------------
// The bindings
// ------------------------------------------------------------------------------------------------
// P5b's frozen row identity on the left (the token the merged screen writes into a request path),
// the provider's own vocabulary on the right. The `why not hot` column is the point of §5.5: a row
// is `restart` until a live effect has been *observed*, and none has.

const std::vector<RowBinding>& AllBindings() {
  static const std::vector<RowBinding> bindings{
      // Gameplay (plan §3.4)
      {SettingId::kPrecisionMode, "gameplay.precision_mode", "freq_ms", RowKind::kBool,
       Strategy::kRestart, ""},
      {SettingId::kDisableRandomWiggles, "gameplay.disable_random_wiggles",
       "disable_random_wiggles", RowKind::kBool, Strategy::kRestart, ""},
      {SettingId::kCameraAngle, "gameplay.camera_angle", "camera_angle", RowKind::kSlider,
       Strategy::kRestart, ""},
      {SettingId::kGameSpeed, "gameplay.game_speed", "speedmod", RowKind::kSlider,
       Strategy::kRestart, ""},
      {SettingId::kDisableDof, "gameplay.disable_dof", "disable_dof", RowKind::kBool,
       Strategy::kRestart, ""},
      {SettingId::kDisablePlots, "gameplay.disable_plots", "disable_plots", RowKind::kBool,
       Strategy::kRestart, ""},
      // The boolean-with-submenu pair: the payload's row toggles, the panel behind it chooses, and
      // what is stored is the position (see `RowBinding::position_value`).
      {SettingId::kForcePlot, "gameplay.forceplot", "forceplot", RowKind::kSelect,
       Strategy::kRestart, "", true},
      {SettingId::kForceSkybox, "gameplay.which_skybox", "which_skybox", RowKind::kSelect,
       Strategy::kRestart, "", true},
      {SettingId::kGems, "gameplay.gems", "gems", RowKind::kSelect, Strategy::kRestart, ""},
      // Interface (plan §3.5)
      {SettingId::kLetterbox, "interface.letterbox", "letterbox", RowKind::kBool,
       Strategy::kRestart, ""},
      {SettingId::kHideHud, "interface.hide_hud", "hide_hud", RowKind::kBool, Strategy::kRestart,
       ""},
      {SettingId::kGoldProgress, "interface.goldprogress", "goldprogress", RowKind::kBool,
       Strategy::kRestart, ""},
      {SettingId::kProgressBar, "interface.progressbar", "progressbar", RowKind::kBool,
       Strategy::kRestart, ""},
      {SettingId::kShowTimer, "interface.show_timer", "show_timer", RowKind::kBool,
       Strategy::kRestart, ""},
      {SettingId::kShowPb, "interface.show_pb", "show_pb", RowKind::kBool, Strategy::kRestart, ""},
      {SettingId::kShowSteak, "interface.show_steak", "show_steak", RowKind::kBool,
       Strategy::kRestart, ""},
      {SettingId::kInputViewer, "interface.input_viewer", "input_viewer", RowKind::kBool,
       Strategy::kRestart, ""},
  };
  return bindings;
}

// The persisted variable names `mod_settings_rw`'s `vars` list is expected to carry. Kept next to
// the bindings so a payload that drops one is reported by name rather than as "something changed".
const std::vector<std::string>& RequiredVariables() {
  static const std::vector<std::string> variables{
      "freq_ms",       "disable_dof",  "which_skybox", "disable_random_wiggles",
      "disable_plots", "forceplot",    "hide_hud",     "goldprogress",
      "progressbar",   "show_timer",   "show_pb",      "show_steak",
      "input_viewer",  "camera_angle", "letterbox",    "gems",
      "speedmod",
  };
  return variables;
}

// The panels the integration reads rows out of. `mod_settings_panel` is the provider's own entry
// point: a working integration routes it into the merged screen and keeps it only as the
// unsupported-payload fallback, so it has to exist either way.
const std::vector<std::string>& RequiredPanels() {
  static const std::vector<std::string> panels{
      "mod_settings_panel",      "mod_game_settings_panel", "mod_visual_settings_panel",
      "mod_hud_settings_panel",  "mod_other_settings_panel"};
  return panels;
}

// ------------------------------------------------------------------------------------------------
// The provider's settings file: a small tolerant DTA-text reader
// ------------------------------------------------------------------------------------------------
// The provider's `write_file` emits the array's children: top-level `(name value)` pairs,
// without an outer array or version marker. Older host builds wrote `(0 (name value) ...)`;
// accept those files as an import, but always write the native format (architecture part 5 §4).
// The reader keeps each element's source text, so an unknown pair survives a rewrite
// (a value we do not model may be a nested array, a string, a symbol, or a float we would round).

struct TextNode {
  bool array = false;
  std::string raw;               // an atom's own text, without surrounding whitespace
  std::vector<TextNode> kids;    // an array's elements
};

class TextScanner {
 public:
  explicit TextScanner(std::string_view text) : text_(text) {}

  bool ParseValue(TextNode* out) {
    SkipSpace();
    if (AtEnd()) {
      return false;
    }
    if (Peek() == '(') {
      ++pos_;
      out->array = true;
      while (true) {
        SkipSpace();
        if (AtEnd()) {
          return false;
        }
        if (Peek() == ')') {
          ++pos_;
          return true;
        }
        TextNode kid;
        if (!ParseValue(&kid)) {
          return false;
        }
        out->kids.push_back(std::move(kid));
      }
    }
    if (Peek() == '{' || Peek() == '[') {
      const char open = Peek();
      const char close = open == '{' ? '}' : ']';
      const size_t start = pos_;
      int depth = 0;
      bool closed = false;
      while (!AtEnd()) {
        const char c = text_[pos_++];
        if (c == open) {
          ++depth;
        } else if (c == close && --depth == 0) {
          closed = true;
          break;
        }
      }
      if (!closed) {
        return false;
      }
      out->raw = std::string(text_.substr(start, pos_ - start));
      return true;
    }
    if (Peek() == '"') {
      const size_t start = pos_++;
      while (!AtEnd() && text_[pos_] != '"') {
        if (text_[pos_] == '\\' && pos_ + 1 < text_.size()) ++pos_;
        ++pos_;
      }
      if (AtEnd()) return false;
      ++pos_;
      out->raw = std::string(text_.substr(start, pos_ - start));
      return true;
    }
    const size_t start = pos_;
    while (!AtEnd() && !IsDelimiter(text_[pos_])) {
      ++pos_;
    }
    out->raw = std::string(text_.substr(start, pos_ - start));
    return !out->raw.empty();
  }

  void SkipSpace() {
    while (!AtEnd()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') {
        ++pos_;
      } else if (c == ';') {  // DTA's comment to end of line
        while (!AtEnd() && text_[pos_] != '\n') ++pos_;
      } else {
        break;
      }
    }
  }

  bool AtEnd() const { return pos_ >= text_.size(); }
  size_t pos() const { return pos_; }

 private:
  static bool IsDelimiter(char c) {
    return c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || c == '"' ||
           c == ' ' || c == '\t' || c == '\r' || c == '\n';
  }
  char Peek() const { return text_[pos_]; }

  std::string_view text_;
  size_t pos_ = 0;
};

void Emit(const TextNode& node, std::string* out) {
  if (!node.array) {
    out->append(node.raw);
    return;
  }
  out->push_back('(');
  bool first = true;
  for (const TextNode& kid : node.kids) {
    if (!first) out->push_back(' ');
    first = false;
    Emit(kid, out);
  }
  out->push_back(')');
}

}  // namespace

// ------------------------------------------------------------------------------------------------
// The record table
// ------------------------------------------------------------------------------------------------

const RecordSpec& RecordAt(size_t index) { return kRecords[index < kRecordCount ? index : 0]; }

std::string_view RecordName(Record record) {
  for (const RecordSpec& spec : kRecords) {
    if (spec.id == record) {
      return spec.path;
    }
  }
  return {};
}

// ------------------------------------------------------------------------------------------------
// The ark index
// ------------------------------------------------------------------------------------------------

bool ReadArkIndex(const uint8_t* bytes, size_t size, ArkIndex* out, std::string* error) {
  std::vector<uint8_t> header;
  if (!DecryptHeader(bytes, size, &header)) {
    if (error != nullptr) *error = "header is too small to hold a version";
    return false;
  }
  Reader reader(header.data(), header.size());
  ArkIndex index;
  if (!reader.U32(&index.version)) {
    if (error != nullptr) *error = "header ends before its version";
    return false;
  }
  auto expect = [&](uint32_t value, uint32_t expected, const char* what) {
    if (value != expected) {
      if (error != nullptr) *error = std::string(what) + " mismatch";
      return false;
    }
    return true;
  };
  auto read_count = [&](uint32_t* count, const char* what) {
    if (!reader.U32(count)) {
      if (error != nullptr) *error = std::string("header ends before ") + what;
      return false;
    }
    if (*count > 4096) {
      if (error != nullptr) *error = std::string(what) + " is not a plausible count";
      return false;
    }
    return true;
  };

  if (index.version != 6) {
    if (error != nullptr) {
      *error = "ark version " + std::to_string(index.version) + " is not the payload's (v6)";
    }
    return false;
  }
  {  // v6's leading hash/key block, before any index field
    const uint8_t* ignored = nullptr;
    if (!reader.Take(20, &ignored)) {
      if (error != nullptr) *error = "header ends inside the v6 key block";
      return false;
    }
    index.extra.assign(ignored, ignored + 20);
  }

  uint32_t ark_count = 0;
  uint32_t echo = 0;
  if (!read_count(&ark_count, "the ark count") || !reader.U32(&echo) ||
      !expect(echo, ark_count, "ark count")) {
    return false;
  }
  const uint32_t part_count = ark_count;
  std::vector<uint32_t> part_sizes(part_count, 0);
  for (uint32_t& part_size : part_sizes) {
    if (!reader.U32(&part_size)) {
      if (error != nullptr) *error = "header ends inside the ark sizes";
      return false;
    }
  }
  index.part_sizes = part_sizes;
  // The path list is introduced by the part count a second time, and the checksum block by it a
  // third: three counts that must agree, which is what makes a truncated header detectable here
  // rather than as a bogus entry list.
  uint32_t path_count = 0;
  if (!reader.U32(&path_count) || !expect(path_count, part_count, "ark path count")) {
    return false;
  }
  for (uint32_t i = 0; i < path_count; ++i) {
    uint32_t length = 0;
    std::string part;
    if (!reader.U32(&length) || length > 4096 || !reader.Bytes(length, &part)) {
      if (error != nullptr) *error = "header ends inside the ark path list";
      return false;
    }
    index.parts.push_back(std::move(part));
  }
  uint32_t checksum_count = 0;
  if (!reader.U32(&checksum_count) || !expect(checksum_count, part_count, "checksum count")) {
    return false;
  }
  {
    for (uint32_t i = 0; i < checksum_count; ++i) {
      uint32_t checksum = 0;
      if (!reader.U32(&checksum)) {
        if (error != nullptr) *error = "header ends inside the checksum block";
        return false;
      }
      index.checksums.push_back(checksum);
    }
  }

  // The string table and its offsets, then the entries themselves.
  uint32_t strings_size = 0;
  std::string strings;
  if (!reader.U32(&strings_size) || !reader.Bytes(strings_size, &strings)) {
    if (error != nullptr) *error = "header ends inside the string table";
    return false;
  }
  uint32_t offset_count = 0;
  if (!reader.U32(&offset_count) || offset_count > (1u << 20) || offset_count > size / 4) {
    if (error != nullptr) *error = "invalid string offset count";
    return false;
  }
  std::vector<uint32_t> offsets(offset_count, 0);
  for (uint32_t& offset : offsets) {
    if (!reader.U32(&offset)) {
      if (error != nullptr) *error = "header ends inside the string offsets";
      return false;
    }
  }
  auto resolve = [&](int32_t index_or_minus_one, std::string* out) {
    if (index_or_minus_one < 0 || size_t(index_or_minus_one) >= offsets.size()) {
      return index_or_minus_one < 0;
    }
    const size_t start = offsets[size_t(index_or_minus_one)];
    if (start > strings.size()) {
      return false;
    }
    const size_t end = strings.find('\0', start);
    if (end == std::string::npos) {
      return false;
    }
    out->assign(strings, start, end - start);
    return true;
  };

  // The entry list: a 32-bit count, then per entry a 64-bit offset and the rest 32-bit. The
  // count is narrower than the offset it introduces, which is the one asymmetry worth spelling
  // out here - reading it as a 64-bit word shifts every entry and turns the whole index into
  // noise.
  uint32_t entry_count = 0;
  if (!reader.U32(&entry_count) || entry_count > (1u << 20)) {
    if (error != nullptr) *error = "entry count is not plausible";
    return false;
  }
  index.entries.reserve(entry_count);
  for (uint32_t i = 0; i < entry_count; ++i) {
    ArkEntry entry;
    int64_t offset = 0;
    int32_t name_index = 0;
    int32_t folder_index = -1;
    uint32_t stored = 0;
    uint32_t inflate = 0;
    if (!reader.I64(&offset) || !reader.I32(&name_index) || !reader.I32(&folder_index) ||
        !reader.U32(&stored) || !reader.U32(&inflate)) {
      if (error != nullptr) *error = "header ends inside the entry list";
      return false;
    }
    std::string name;
    std::string folder;
    if (!resolve(name_index, &name) || !resolve(folder_index, &folder)) {
      if (error != nullptr) *error = "an entry names a string outside the table";
      return false;
    }
    entry.offset = static_cast<uint64_t>(offset);
    entry.size = stored;
    entry.inflate = inflate;
    entry.path = folder.empty() ? name : folder + "/" + name;
    index.entries.push_back(std::move(entry));
  }
  if (offset_count != 0 && strings_size == 0) {
    if (error != nullptr) *error = "the string table is empty but offsets point into it";
    return false;
  }
  *out = std::move(index);
  return true;
}

const ArkEntry* ArkIndex::Find(std::string_view path) const {
  for (const ArkEntry& entry : entries) {
    if (entry.path == path) {
      return &entry;
    }
  }
  return nullptr;
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>* out,
                   std::string* error) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    if (error != nullptr) *error = path.string() + ": " + ec.message();
    return false;
  }
  out->resize(size_t(size));
  std::ifstream file(path, std::ios::binary);
  if (!file.read(reinterpret_cast<char*>(out->data()), std::streamsize(out->size()))) {
    if (error != nullptr) *error = path.string() + ": could not be read";
    out->clear();
    return false;
  }
  return true;
}

// ------------------------------------------------------------------------------------------------
// Installation
// ------------------------------------------------------------------------------------------------

Installation InspectInstallation(const std::filesystem::path& payload_root, std::string* error) {
  Installation installation;
  if (error != nullptr) error->clear();

  std::error_code ec;
  installation.header = std::filesystem::is_regular_file(payload_root / "gen/patch_xbox.hdr", ec);
  installation.archive =
      std::filesystem::is_regular_file(payload_root / "gen/patch_xbox_0.ark", ec);
  if (!installation.header) {
    installation.error = "no gen/patch_xbox.hdr";
    return installation;
  }
  if (!installation.archive) {
    installation.error = "gen/patch_xbox.hdr without gen/patch_xbox_0.ark is a damaged disc";
    return installation;
  }

  std::vector<uint8_t> header;
  if (!ReadFileBytes(payload_root / "gen/patch_xbox.hdr", &header, &installation.error)) {
    return installation;
  }
  ArkIndex index;
  if (!ReadArkIndex(header.data(), header.size(), &index, &installation.error)) {
    return installation;
  }
  installation.index_read = true;
  installation.ark_version = index.version;
  for (const RecordSpec& spec : kRecords) {
    const ArkEntry* entry = index.Find(spec.path);
    // A record the index carries but whose body the archive cannot serve is missing too: the
    // check is about what the integration can actually read, not about a name in a list.
    const bool usable =
        entry != nullptr && !entry->compressed() && entry->offset + entry->size <= (1ull << 40);
    (usable ? installation.present : installation.missing).push_back(spec.id);
  }
  if (error != nullptr) *error = installation.error;
  return installation;
}

bool ReadProviderRecords(const std::filesystem::path& payload_root, RecordBytes* out,
                         std::string* error) {
  std::vector<uint8_t> header;
  if (!ReadFileBytes(payload_root / "gen/patch_xbox.hdr", &header, error)) {
    return false;
  }
  ArkIndex index;
  if (!ReadArkIndex(header.data(), header.size(), &index, error)) {
    return false;
  }
  std::vector<uint8_t> archive;
  if (!ReadFileBytes(payload_root / "gen/patch_xbox_0.ark", &archive, error)) {
    return false;
  }
  auto take = [&](Record record, std::vector<uint8_t>* destination) {
    const ArkEntry* entry = index.Find(RecordName(record));
    if (entry == nullptr) {
      if (error != nullptr) *error = std::string(RecordName(record)) + " is not in the payload";
      return false;
    }
    if (entry->compressed()) {
      if (error != nullptr) {
        *error = std::string(RecordName(record)) +
                 " is stored compressed; this build reads the payload's records as stored";
      }
      return false;
    }
    if (entry->offset + entry->size > archive.size()) {
      if (error != nullptr) *error = std::string(RecordName(record)) + " lies past the archive";
      return false;
    }
    const auto begin = archive.begin() + std::ptrdiff_t(entry->offset);
    destination->assign(begin, begin + std::ptrdiff_t(entry->size));
    return true;
  };
  return take(Record::kModSettings, &out->mod_settings) &&
         take(Record::kModSettingsRw, &out->mod_settings_rw) &&
         take(Record::kUltiMacros, &out->ulti_macros) &&
         take(Record::kUltiFuncs, &out->ulti_funcs) && take(Record::kUltiInit, &out->ulti_init);
}

// ------------------------------------------------------------------------------------------------
// Shape
// ------------------------------------------------------------------------------------------------

const char* RowKindName(RowKind kind) {
  switch (kind) {
    case RowKind::kSubmenu: return "submenu";
    case RowKind::kBool: return "boolean";
    case RowKind::kSelect: return "horizontal select";
    case RowKind::kSlider: return "slider";
    case RowKind::kUnknown: break;
  }
  return "unknown";
}

std::string_view RowKindMacro(RowKind kind) {
  switch (kind) {
    case RowKind::kSubmenu: return "uNormal";
    case RowKind::kBool: return "uOnOFF";
    case RowKind::kSelect: return "uFakeSld";
    case RowKind::kSlider: return "uSld";
    case RowKind::kUnknown: break;
  }
  return {};
}

namespace {

// One `(symbol (type uOnOFF) (bool $x) ...)` element of a panel's `data` array.
RowShape ReadRowShape(const ui::dtb::Node& element) {
  RowShape row;
  if (!IsBlock(element)) {
    return row;  // a bare symbol is a *choice* of a flat list panel, handled by the caller
  }
  row.symbol = element.kids.front().text;
  if (const ui::dtb::Node* type = BlockNamed(element, "type"); type != nullptr) {
    row.macro = KidText(*type, 1);
    row.kind = KindOfMacro(row.macro);
  }
  // `(bool $freq_ms)` binds a boolean; `(var camera_angle)` binds a slider or a selector.
  for (const char* key : {"bool", "var"}) {
    if (const ui::dtb::Node* bound = BlockNamed(element, key);
        bound != nullptr && KidText(*bound, 1).empty() == false) {
      row.variable = KidText(*bound, 1);
    }
  }
  row.reboot_tagged = Mentions(element, "reboot");
  if (const ui::dtb::Node* script = BlockNamed(element, "sel_script"); script != nullptr) {
    if (const ui::dtb::Node* jump = FindNamedBlock(*script, "pop_and_goto_screen");
        jump != nullptr) {
      row.target_screen = KidText(*jump, 1);
    }
  }
  // An inline choice list: `(array (default freq amp2003 ...))`.
  if (const ui::dtb::Node* list = BlockNamed(element, "array"); list != nullptr) {
    if (const ui::dtb::Node* array = Kid(*list, 1); array != nullptr && IsBlock(*array)) {
      for (const ui::dtb::Node& choice : array->kids) {
        if (!choice.text.empty()) {
          row.choices.push_back(choice.text);
        }
      }
    }
  }
  return row;
}

}  // namespace

const PanelShape* ShapeReport::Panel(std::string_view name) const {
  for (const PanelShape& panel : panels) {
    if (panel.name == name) {
      return &panel;
    }
  }
  return nullptr;
}

const RowShape* ShapeReport::Row(std::string_view symbol) const {
  for (const PanelShape& panel : panels) {
    for (const RowShape& row : panel.rows) {
      if (row.symbol == symbol) {
        return &row;
      }
    }
  }
  return nullptr;
}

ShapeReport CheckShape(const RecordBytes& records) {
  ShapeReport report;
  auto parse = [&](const std::vector<uint8_t>& bytes, const char* what,
                   std::vector<ui::dtb::Node>* roots) {
    if (bytes.empty()) {
      report.problems.push_back(std::string(what) + " is missing from the payload");
      return false;
    }
    std::string reason;
    ui::dtb::RootHeader header;
    if (!ui::dtb::Parse(bytes.data(), bytes.size(), roots, &header, &reason)) {
      report.problems.push_back(std::string(what) + " is not compiled DTA this build reads: " +
                               reason);
      return false;
    }
    return true;
  };

  std::vector<ui::dtb::Node> mod_settings;
  std::vector<ui::dtb::Node> mod_settings_rw;
  std::vector<ui::dtb::Node> ulti_macros;
  std::vector<ui::dtb::Node> ulti_init;
  const bool have_settings = parse(records.mod_settings, "ulti/gen/mod_settings.dtb", &mod_settings);
  const bool have_rw =
      parse(records.mod_settings_rw, "ulti/gen/mod_settings_rw.dtb", &mod_settings_rw);
  const bool have_macros = parse(records.ulti_macros, "ulti/gen/ulti_macros.dtb", &ulti_macros);
  const bool have_init = parse(records.ulti_init, "ulti/gen/ulti_init.dtb", &ulti_init);
  // `ulti_funcs` is parsed for its observations, not for a requirement: the integration does not
  // call any of it, so a payload whose row formatting changed is still integrable.
  std::vector<ui::dtb::Node> ulti_funcs;
  parse(records.ulti_funcs, "ulti/gen/ulti_funcs.dtb", &ulti_funcs);

  // --- the panels and their rows ---
  //
  // A panel is `{new UIPanel <name> SETTINGS_LIST_FILE (title <key>) (data <list>)}`, and
  // `<list>` takes exactly two shapes in this payload:
  //
  //   * a list of row descriptors - `((os_precision (type uOnOFF) (bool $freq_ms) ...) ...)`;
  //   * a bare choice list - `(off plot_bodega_a plot_bodega_apartment ...)`. The screen's own
  //     SELECT_MSG stores the selected *position* of this list, so the panel owns the choices
  //     and no row does (plan §3.4: the provider's inventory is authoritative).
  if (have_settings) {
    for (const ui::dtb::Node& root : mod_settings) {
      if (!IsBlock(root) || root.kids.size() < 3 || KidText(root, 0) != "new" ||
          KidText(root, 1) != "UIPanel") {
        continue;
      }
      PanelShape panel;
      panel.name = KidText(root, 2);
      panel.draws_through_retail_list = Mentions(root, "SETTINGS_LIST_FILE");
      if (const ui::dtb::Node* title = BlockNamed(root, "title"); title != nullptr) {
        panel.title = KidText(*title, 1);
      }
      const ui::dtb::Node* data = BlockNamed(root, "data");
      const ui::dtb::Node* list = data != nullptr ? Kid(*data, 1) : nullptr;
      if (list != nullptr && IsBlock(*list)) {
        for (const ui::dtb::Node& element : list->kids) {
          if (!IsBlock(element)) {
            // A bare name is a choice of a flat list; a preprocessor marker is not.
            if (IsNameTag(element.tag) && !element.text.empty()) {
              panel.flat_choices.push_back(element.text);
            }
            continue;
          }
          if (!IsNameTag(element.kids.front().tag)) {
            continue;
          }
          RowShape row = ReadRowShape(element);
          if (row.symbol.empty()) {
            continue;
          }
          if (row.kind == RowKind::kUnknown && row.variable.empty()) {
            // A one-symbol element inside a row list is not a row: treat it as a choice.
            panel.flat_choices.push_back(row.symbol);
            continue;
          }
          if (row.kind == RowKind::kUnknown) {
            report.problems.push_back("row " + row.symbol + " in " + panel.name +
                                      " names kind '" + row.macro +
                                      "', which this build does not support");
          }
          panel.rows.push_back(std::move(row));
        }
      }
      report.panels.push_back(std::move(panel));
    }
  }

  // A panel that draws its rows through the retail pause list is a *presentation* detail; the
  // merged screen brings its own list, so a missing one is a note rather than a refusal.
  size_t flat_panels = 0;
  for (const PanelShape& panel : report.panels) {
    if (!panel.flat_choices.empty()) {
      ++flat_panels;
    }
  }

  // --- the persisted variable list and the object that owns it ---
  if (have_rw) {
    for (const ui::dtb::Node& root : mod_settings_rw) {
      if (!IsBlock(root) || KidText(root, 0) != "new") {
        continue;
      }
      if (KidText(root, 1) != "Object" || KidText(root, 2) != "mod_settings_rw") {
        continue;
      }
      // `(vars (freq_ms disable_dof ...))`: the names are one array deep, which is the shape the
      // payload ships and the shape the fixture reproduces. A flat `(vars a b c)` is accepted
      // too, because the list's *contents* are what matters and refusing the flatter spelling
      // would refuse a payload over its punctuation.
      const ui::dtb::Node* vars = BlockNamed(root, "vars");
      if (vars != nullptr) {
        size_t first = 1;
        if (Kid(*vars, 1) != nullptr && IsBlock(*Kid(*vars, 1))) {
          vars = Kid(*vars, 1);
          first = 0;
        }
        for (size_t i = first; i < vars->kids.size(); ++i) {
          const std::string& name = KidText(*vars, i);
          if (!name.empty()) {
            report.persisted.push_back(name);
          }
        }
      }
    }
    if (report.persisted.empty()) {
      report.problems.push_back(
          "ulti/gen/mod_settings_rw.dtb declares no `mod_settings_rw` object with a `vars` list, "
          "so the provider's file has no known shape");
    }
  }

  // --- the macros: the row kinds and the settings-file path ---
  if (have_macros) {
    ui::dtb::Kind define_kind = ui::dtb::Kind::kScalar;
    if (!ui::dtb::KindOfTag(kDefineTag, &define_kind) || define_kind != ui::dtb::Kind::kText) {
      // The parser's grammar moved out from under the tag above; the macro names would then be
      // silently missing, which is a report a reader can act on rather than a wrong answer.
      report.notes.push_back("this build no longer recognises `#define` as a text node");
    }
    std::vector<std::string> paths;
    for (const ui::dtb::Node& root : ulti_macros) {
      // A `#define` is a text node whose text is the macro's own name; its value is the next
      // root. Both halves are recorded, because the path is the value and the kind is the name.
      if (root.tag == kDefineTag && !root.text.empty()) {
        report.macros.push_back(root.text);
      }
      CollectTextsContaining(root, kSettingsFileName, &paths);
    }
    // The Xbox 360 spelling is the one this build redirects; the others are reported rather than
    // ignored, so a payload that moves the file is visible in the log instead of silent.
    for (const std::string& candidate : paths) {
      if (candidate.find("GAME:") != std::string::npos) {
        report.settings_path_macro = candidate;
        break;
      }
    }
    if (report.settings_path_macro.empty() && !paths.empty()) {
      report.settings_path_macro = paths.front();
    }
    for (const std::string& candidate : paths) {
      if (candidate != report.settings_path_macro) {
        report.notes.push_back("the payload also spells the settings file as '" + candidate +
                               "' for another platform");
      }
    }
    if (report.settings_path_macro.empty()) {
      report.problems.push_back(
          "ulti/gen/ulti_macros.dtb does not name " + std::string(kSettingsFileName) +
          ", so the provider's own file is not the one this build redirects");
    } else if (report.settings_path_macro.find("GAME:") == std::string::npos) {
      report.notes.push_back("the provider's settings path is spelled '" +
                             report.settings_path_macro + "', not the expected GAME:/ form");
    }
  }

  // --- the compiled defaults ---
  if (have_init) {
    for (const ui::dtb::Node& root : ulti_init) {
      if (!IsBlock(root) || KidText(root, 0) != "set") {
        continue;
      }
      const ui::dtb::Node* name = Kid(root, 1);
      const ui::dtb::Node* value = Kid(root, 2);
      if (name == nullptr || value == nullptr || name->text.empty()) {
        continue;
      }
      report.defaults[name->text] = AtomText(*value);
    }
  }

  // --- the requirements: a panel for every row, and a persisted name for every binding ---
  if (have_settings) {
    for (const std::string& name : RequiredPanels()) {
      if (report.Panel(name) == nullptr) {
        report.problems.push_back("the payload has no panel " + name);
      }
    }
    if (flat_panels != 0) {
      report.notes.push_back("the payload declares " + std::to_string(flat_panels) +
                             " flat choice panel(s); their choice sets come from the payload");
    }
    for (const RowBinding& binding : RowBindings()) {
      const RowShape* row = nullptr;
      for (const PanelShape& panel : report.panels) {
        for (const RowShape& candidate : panel.rows) {
          if (candidate.variable == binding.variable) {
            row = &candidate;
          }
        }
      }
      if (row == nullptr) {
        report.problems.push_back("no panel row binds '" + std::string(binding.variable) +
                                  "', so " + std::string(binding.token) + " has no provider row");
        continue;
      }
      // A boolean row the provider pairs with a submenu (`os_force_plot`) is a boolean whose
      // choices live behind a screen - and this build offers that whole choice set as one select
      // (`binding.position_value`), so the payload's row kind and this binding's codec kind
      // legitimately differ. The check that must still hold is that the choice set exists, below.
      const bool paired_submenu = binding.position_value && row->kind == RowKind::kBool &&
                                  binding.kind == RowKind::kSelect;
      if (row->kind != binding.kind && !paired_submenu) {
        report.problems.push_back("the provider's row for '" + std::string(binding.variable) +
                                  "' is " + RowKindName(row->kind) + ", not " +
                                  RowKindName(binding.kind));
      }
      if (!row->choices.empty()) {
        report.choices[std::string(binding.variable)] = row->choices;
      }
    }
    // The two flat panels' choice sets belong to the variable their screen writes - Force Plot
    // and Force Skybox - because the provider names the variable in the screen's SELECT_MSG
    // (`{set $which_skybox {$component selected_pos}}`) rather than in a row.
    for (const PanelShape& panel : report.panels) {
      if (panel.flat_choices.empty()) {
        continue;
      }
      if (panel.name == "mod_force_plot_panel") {
        report.choices["forceplot"] = panel.flat_choices;
      } else if (panel.name == "mod_force_skybox_panel") {
        report.choices["which_skybox"] = panel.flat_choices;
      } else {
        report.notes.push_back("panel " + panel.name + " declares a flat choice list of " +
                               std::to_string(panel.flat_choices.size()) +
                               " entries that this build does not map to a variable");
      }
    }
    // Two of the plan's rows are not ordinary bound values and are checked by name:
    // `unlockall` is the persistence exception (§6.2 - the provider's own saved-vars list does
    // not carry it), and the achievements row is a one-shot action, not a value.
    // A `position_value` binding is served entirely by its panel's flat choice list: the file holds
    // a position, and the position is only meaningful against that list. Without it the row could
    // be read and written, but never named - so its absence is a problem rather than a note.
    for (const RowBinding& binding : RowBindings()) {
      if (binding.position_value && report.choices.count(std::string(binding.variable)) == 0) {
        report.problems.push_back("the provider declares no choice list for '" +
                                  std::string(binding.variable) + "', so " +
                                  std::string(binding.token) + " cannot be named");
      }
    }
    if (const PanelShape* other = report.Panel("mod_other_settings_panel"); other != nullptr) {
      const bool has_unlock = std::any_of(other->rows.begin(), other->rows.end(),
                                          [](const RowShape& row) {
                                            return row.variable == "unlockall";
                                          });
      if (!has_unlock) {
        report.problems.push_back(
            "mod_other_settings_panel has no `unlockall` row, which the persistence exception "
            "is written against");
      }
      const bool has_action = std::any_of(other->rows.begin(), other->rows.end(),
                                          [](const RowShape& row) {
                                            return row.symbol == kAchievementVariable;
                                          });
      if (!has_action) {
        report.problems.push_back("mod_other_settings_panel has no " +
                                  std::string(kAchievementVariable) +
                                  " row, so the achievements action has no provider path");
      }
    }
  }

  for (const std::string& name : RequiredVariables()) {
    if (std::find(report.persisted.begin(), report.persisted.end(), name) ==
        report.persisted.end()) {
      report.problems.push_back("the provider's saved-variable list does not carry '" + name + "'");
    }
  }
  for (const std::string& name : report.persisted) {
    const bool ours = std::any_of(
        RowBindings().begin(), RowBindings().end(),
        [&](const RowBinding& binding) { return std::string(binding.variable) == name; });
    if (!ours) {
      report.notes.push_back("the provider persists '" + name +
                             "', which this build does not draw (kept verbatim)");
    }
  }

  report.ok = !report.HasProblem();
  return report;
}

ProviderReadiness Readiness(bool payload_present, bool active_target, const ShapeReport& shape) {
  if (!payload_present) {
    return ProviderReadiness::kAbsent;
  }
  if (!shape.ok) {
    // The distinction matters to the caller: a payload whose shape is wrong keeps the mod's own
    // settings path reachable, whereas one that is merely inactive keeps our disabled rows.
    return ProviderReadiness::kUnrecognizedShape;
  }
  return active_target ? ProviderReadiness::kCompatible : ProviderReadiness::kInactiveTarget;
}

// ------------------------------------------------------------------------------------------------
// Bindings
// ------------------------------------------------------------------------------------------------

const std::vector<RowBinding>& RowBindings() { return AllBindings(); }

const RowBinding* BindingForToken(std::string_view token) {
  for (const RowBinding& binding : AllBindings()) {
    if (binding.token == token) {
      return &binding;
    }
  }
  return nullptr;
}

const RowBinding* BindingForVariable(std::string_view variable) {
  for (const RowBinding& binding : AllBindings()) {
    if (binding.variable == variable) {
      return &binding;
    }
  }
  return nullptr;
}

const std::vector<RowBinding>& FileBackedBindings() { return AllBindings(); }

const std::vector<NativeDefault>& NativeDefaultBindings() {
  // Exactly one row: `unlockall`. The provider's own saved-vars list does not carry it (its
  // exports are all derived from that list), so a relocated toggle cannot promise persistence by
  // calling the provider's save - which is why the *value* lives under native
  // `[ultimate_overrides]`. Its *default* is still the payload's, and this is where that is read.
  static const std::vector<NativeDefault> bindings{
      {SettingId::kUnlockAll, "unlockall", RowKind::kBool},
  };
  return bindings;
}

// ------------------------------------------------------------------------------------------------
// The provider's settings file
// ------------------------------------------------------------------------------------------------

std::filesystem::path ProviderSettingsDirectory(const std::filesystem::path& user_data_root) {
  if (user_data_root.empty()) {
    return {};
  }
  return user_data_root / kUserSubdirectory;
}

std::filesystem::path ProviderSettingsPath(const std::filesystem::path& user_data_root) {
  const std::filesystem::path directory = ProviderSettingsDirectory(user_data_root);
  if (directory.empty()) {
    return {};
  }
  return directory / kSettingsFileName;
}

bool ParseProviderFile(std::string_view text, ProviderFile* out, std::string* error) {
  if (error != nullptr) error->clear();
  ProviderFile file;
  TextScanner scanner(text);
  std::vector<TextNode> elements;
  scanner.SkipSpace();
  while (!scanner.AtEnd()) {
    TextNode node;
    if (!scanner.ParseValue(&node) || !node.array) {
      if (error != nullptr) *error = "the file is not a sequence of DTA arrays";
      return false;
    }
    elements.push_back(std::move(node));
    scanner.SkipSpace();
  }
  if (elements.empty()) {
    if (error != nullptr) *error = "the provider settings file is empty";
    return false;
  }
  // Only the old host writer's numeric marker denotes a wrapper. A single native pair,
  // including a pair with an array value, must retain its first element as the name.
  const auto& first = elements.front().kids;
  if (!first.empty() && !first.front().array && !first.front().raw.empty() &&
      std::all_of(first.front().raw.begin(), first.front().raw.end(),
                  [](char c) { return c >= '0' && c <= '9'; })) {
    if (elements.size() != 1) {
      if (error != nullptr) *error = "there is content after the legacy outer array";
      return false;
    }
    file.version_marker = first.front().raw;
    std::vector<TextNode> legacy(first.begin() + 1, first.end());
    elements = std::move(legacy);
  }
  for (const TextNode& kid : elements) {
    if (!kid.array || kid.kids.empty() || kid.kids.front().array) {
      if (error != nullptr) *error = "a provider settings element has no pair name";
      return false;
    }
    std::string element;
    Emit(kid, &element);
    file.elements.push_back(std::move(element));
    // Only a `(name value)` pair whose value is a single atom is ours to interpret. Anything
    // else - a nested array, a value that serialized as several items - is still kept, in place,
    // by the writer, and simply never answers a read.
    std::string name;
    if (kid.array && kid.kids.size() == 2 && !kid.kids[0].array && !kid.kids[1].array) {
      name = kid.kids[0].raw;
      if (!name.empty()) {
        if (file.values.count(name) == 0) {
          file.names.push_back(name);
        }
        file.values[name] = kid.kids[1].raw;
      }
    }
    file.element_names.push_back(std::move(name));
  }
  file.parsed = true;
  *out = std::move(file);
  return true;
}

std::string WriteProviderFile(const ProviderFile& file,
                              const std::map<std::string, std::string>& updates) {
  std::string out;
  for (size_t i = 0; i < file.elements.size(); ++i) {
    if (!out.empty()) out.push_back('\n');
    const std::string* name =
        i < file.element_names.size() ? &file.element_names[i] : nullptr;
    const auto updated =
        name == nullptr || name->empty() ? updates.end() : updates.find(*name);
    if (updated == updates.end()) {
      // Untouched, and byte for byte what was read: an element this build does not model, or a
      // pair whose value did not change.
      out.append(file.elements[i]);
      continue;
    }
    out.push_back('(');
    out.append(*name);
    out.push_back(' ');
    out.append(updated->second);
    out.push_back(')');
  }
  // An update the file does not carry yet is appended, in the update map's own (name) order: that
  // is how the *first* save writes the pairs it owns when the provider has no file at all. A
  // provider with no pair for a variable keeps its own compiled default, so a partial file is a
  // file the provider's own loader reads correctly.
  for (const auto& [name, value] : updates) {
    if (std::find(file.names.begin(), file.names.end(), name) != file.names.end()) {
      continue;
    }
    if (!out.empty()) out.push_back('\n');
    out.push_back('(');
    out.append(name);
    out.push_back(' ');
    out.append(value);
    out.push_back(')');
  }
  if (!out.empty()) out.push_back('\n');
  return out;
}

namespace {

// TRUE/FALSE in the spelling the provider's own `toggle_var` produces, plus the numeric spellings
// a hand-edited file may carry.
bool ParseBoolText(std::string_view text, bool* out) {
  if (text == "TRUE" || text == "true" || text == "1") {
    *out = true;
    return true;
  }
  if (text == "FALSE" || text == "false" || text == "0") {
    *out = false;
    return true;
  }
  return false;
}

bool ParseIntText(std::string_view text, int32_t* out) {
  if (text.empty()) {
    return false;
  }
  std::string copy(text);
  char* end = nullptr;
  const long long value = std::strtoll(copy.c_str(), &end, 10);
  if (end == copy.c_str() || *end != '\0') {
    return false;
  }
  *out = static_cast<int32_t>(value);
  return true;
}

}  // namespace

bool DecodeProviderValue(const RowBinding& binding, std::string_view text, Value* out) {
  Value value;
  switch (binding.kind) {
    case RowKind::kBool: {
      // `which_skybox` and `forceplot` are stored as an index whose 0 means "off", and the
      // provider's own row draws them as a boolean - so an out-of-range index is "on", and only
      // the zero the provider writes for off is false. This is the documented ambiguity of the
      // provider's own representation, reproduced rather than reinterpreted.
      bool boolean = false;
      if (ParseBoolText(text, &boolean)) {
        value.kind = ValueKind::kBool;
        value.boolean = boolean;
      } else {
        int32_t index = 0;
        if (!ParseIntText(text, &index)) {
          return false;
        }
        value.kind = ValueKind::kBool;
        value.boolean = index != 0;
      }
      break;
    }
    case RowKind::kSelect: {
      if (text.empty()) {
        return false;
      }
      value.kind = ValueKind::kEnum;
      value.text = std::string(text);
      break;
    }
    case RowKind::kSlider: {
      if (binding.id == SettingId::kCameraAngle) {
        int32_t index = 0;
        if (!ParseIntText(text, &index)) {
          return false;
        }
        static const char* kTokens[] = {"default", "high", "low"};
        if (index < 0 || index > 2) {
          return false;
        }
        value.kind = ValueKind::kEnum;
        value.text = kTokens[index];
        break;
      }
      // `speedmod`: a 2-decimal string the provider's own slider steps by 0.05.
      std::string copy(text);
      char* end = nullptr;
      const double speed = std::strtod(copy.c_str(), &end);
      if (end == copy.c_str() || *end != '\0') {
        return false;
      }
      // Round rather than truncate: 1.05 parses to 1.0499999..., and truncating would step the
      // menu backwards on every round trip (plan §3.4: never float-accumulate steps).
      value.kind = ValueKind::kInt;
      value.integer = static_cast<int32_t>(speed * 100.0 + 0.5);
      break;
    }
    case RowKind::kSubmenu:
    case RowKind::kUnknown:
    default:
      return false;
  }
  *out = std::move(value);
  return true;
}

bool EncodeProviderValue(const RowBinding& binding, const Value& value, std::string* out) {
  switch (binding.kind) {
    case RowKind::kBool:
      if (value.kind != ValueKind::kBool) {
        return false;
      }
      *out = value.boolean ? "TRUE" : "FALSE";
      return true;
    case RowKind::kSelect:
      if (value.kind != ValueKind::kEnum || value.text.empty()) {
        return false;
      }
      *out = value.text;
      return true;
    case RowKind::kSlider: {
      if (binding.id == SettingId::kCameraAngle) {
        if (value.kind != ValueKind::kEnum) {
          return false;
        }
        const int32_t index = value.text == "default" ? 0 : value.text == "high" ? 1
                              : value.text == "low"   ? 2
                                                      : -1;
        if (index < 0) {
          return false;
        }
        *out = std::to_string(index);
        return true;
      }
      if (value.kind != ValueKind::kInt || value.integer < 100 || value.integer > 150) {
        return false;
      }
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "%d.%02d", value.integer / 100, value.integer % 100);
      *out = buffer;
      return true;
    }
    case RowKind::kSubmenu:
    case RowKind::kUnknown:
    default:
      return false;
  }
}

std::map<SettingId, Value> ValuesFromProviderFile(const ProviderFile& file) {
  std::map<SettingId, Value> values;
  if (!file.parsed) {
    return values;
  }
  for (const RowBinding& binding : FileBackedBindings()) {
    const auto pair = file.values.find(std::string(binding.variable));
    if (pair == file.values.end()) {
      continue;
    }
    Value value;
    if (DecodeProviderValue(binding, pair->second, &value)) {
      values[binding.id] = std::move(value);
    }
  }
  return values;
}

bool EncodeProviderValues(const std::map<SettingId, Value>& values,
                          std::map<std::string, std::string>* updates, std::string* error) {
  if (updates == nullptr) {
    return false;
  }
  updates->clear();
  if (error != nullptr) error->clear();
  bool ok = true;
  for (const auto& [id, value] : values) {
    const RowBinding* binding = nullptr;
    for (const RowBinding& candidate : FileBackedBindings()) {
      if (candidate.id == id) {
        binding = &candidate;
        break;
      }
    }
    if (binding == nullptr) {
      // A row with no provider pair (`unlock_all`'s persistence exception, or a purely native
      // row): not ours to write here, and not a failure.
      continue;
    }
    std::string text;
    if (!EncodeProviderValue(*binding, value, &text)) {
      ok = false;
      if (error != nullptr) {
        *error += (*error).empty() ? std::string() : ", ";
        *error += std::string(binding->token) + " cannot hold that value";
      }
      continue;
    }
    (*updates)[std::string(binding->variable)] = std::move(text);
  }
  return ok;
}

std::map<SettingId, Value> DefaultsFromShape(const ShapeReport& shape) {
  std::map<SettingId, Value> defaults;
  // The default is decoded with the *row's* kind, not with a kind of its own, so a payload that
  // changes a row's kind changes its default's reading too - one place to look.
  auto take = [&](SettingId id, std::string_view variable, RowKind kind) {
    const auto pair = shape.defaults.find(std::string(variable));
    if (pair == shape.defaults.end()) {
      return;
    }
    // The default is decoded with the *row's* kind, not with a kind of its own, so a payload that
    // changes a row's kind changes its default's reading too - one place to look.
    RowBinding binding;
    binding.id = id;
    binding.variable = variable;
    binding.kind = kind;
    Value value;
    if (DecodeProviderValue(binding, pair->second, &value)) {
      defaults[id] = std::move(value);
    }
  };
  for (const RowBinding& binding : FileBackedBindings()) {
    take(binding.id, binding.variable, binding.kind);
  }
  // The rows whose default is still the provider's even though their value is not persisted in
  // the provider's file (`unlockall`, plan §6.2). Honoring it is the difference between "the
  // payload ships this off" and inventing a value.
  for (const NativeDefault& entry : NativeDefaultBindings()) {
    take(entry.id, entry.variable, entry.kind);
  }
  return defaults;
}

bool PayloadWritesSettingsFile(const ShapeReport& shape) {
  return shape.settings_path_macro.find(kSettingsFileName) != std::string::npos;
}

// ------------------------------------------------------------------------------------------------
// The boot's provider state
// ------------------------------------------------------------------------------------------------

void ConfigureProvider(ProviderRuntime* runtime, const std::filesystem::path& payload_root,
                       const std::filesystem::path& user_data_root, bool active_target) {
  if (runtime == nullptr) {
    return;
  }
  *runtime = ProviderRuntime{};
  runtime->considered = true;
  runtime->active_target = active_target;
  runtime->user_root = ProviderSettingsDirectory(user_data_root);
  runtime->settings_path = ProviderSettingsPath(user_data_root);

  std::string error;
  runtime->installation = InspectInstallation(payload_root, &error);
  runtime->payload_present = runtime->installation.Mountable();
  if (!runtime->payload_present) {
    runtime->readiness = ProviderReadiness::kAbsent;
    runtime->summary = "no payload at " + payload_root.string() +
                       (error.empty() ? std::string() : " (" + error + ")");
    return;
  }

  RecordBytes records;
  if (!ReadProviderRecords(payload_root, &records, &error)) {
    // A *mountable* payload that does not carry the records this integration reads is not the same
    // thing as no payload at all, and the difference is what P11's fallback turns on: the mod's own
    // settings path must stay reachable (kUnrecognizedShape), rather than being treated as an
    // install that is not there.
    runtime->shape.problems.push_back("payload records unreadable: " + error);
    runtime->shape.ok = false;
    runtime->readiness = Readiness(runtime->payload_present, active_target, runtime->shape);
    runtime->summary = "payload mounted but not the provider's shape: " + error;
    return;
  }
  runtime->shape = CheckShape(records);
  runtime->readiness = Readiness(runtime->payload_present, active_target, runtime->shape);
  runtime->choices = runtime->shape.choices;

  // The provider's own file over the payload's compiled defaults: a pair the file does not carry
  // keeps the provider's own default rather than an invented one, and a row the payload does not
  // carry at all is simply absent from `values`.
  if (!runtime->settings_path.empty()) {
    std::vector<uint8_t> bytes;
    if (ReadFileBytes(runtime->settings_path, &bytes, &error)) {
      ProviderFile file;
      std::string parse_error;
      const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (ParseProviderFile(text, &file, &parse_error)) {
        runtime->values = ValuesFromProviderFile(file);
      } else {
        runtime->shape.notes.push_back("the player's " + runtime->settings_path.string() +
                                       " is not a provider file this build reads: " + parse_error);
      }
    }
  }
  for (const auto& [id, value] : DefaultsFromShape(runtime->shape)) {
    runtime->values.emplace(id, value);
  }
  // The payload's flat-panel writers store a **position**; the screen, the store and the requests
  // all speak the provider's tokens, so each such value is resolved here, once, against the
  // payload's own inventory. A value that names no position (a foreign payload, or a spelling an
  // earlier build wrote) is dropped rather than carried: the row then reads the compiled default
  // and the next press writes a token this installation can encode. Keeping an unnameable value
  // is what would poison the provider's file on the next commit.
  for (const RowBinding& binding : FileBackedBindings()) {
    if (!binding.position_value) continue;
    auto found = runtime->values.find(binding.id);
    if (found == runtime->values.end()) continue;
    const auto choices = runtime->choices.find(std::string(binding.variable));
    if (choices == runtime->choices.end() || choices->second.empty()) continue;
    const std::vector<std::string>& tokens = choices->second;
    const std::string& text = found->second.text;
    int32_t index = 0;
    if (const auto at = std::find(tokens.begin(), tokens.end(), text); at != tokens.end()) {
      continue;  // already a token this payload reports
    } else if (ParseIntText(text, &index) && index >= 0 &&
               static_cast<size_t>(index) < tokens.size()) {
      found->second.text = tokens[static_cast<size_t>(index)];
    } else if (text == "FALSE" || text == "false" || text == "TRUE" || text == "true") {
      // What this build used to write for the pair: a boolean. Off is position 0; `TRUE` names no
      // position, so it resolves to the same place rather than inventing one.
      found->second.text = tokens[0];
    } else {
      runtime->shape.notes.push_back("no position for " + std::string(binding.token) + " = '" +
                                     text + "' in this payload's " +
                                     std::string(binding.variable) +
                                     " list; the row keeps its own default");
      runtime->values.erase(found);
    }
  }
  // A row the provider neither persists nor defaults is *absent*, and the adapter says so rather
  // than inventing a value: the provider's own variable is simply unset, and the caller's compiled
  // default is what should stand in (`os_hide_hud` is the measured example - `ulti_init` never
  // sets `$hide_hud`). Recorded as a note so the boot log names the rows that fall back.
  {
    std::string missing;
    for (const RowBinding& binding : RowBindings()) {
      if (runtime->values.count(binding.id) != 0) {
        continue;
      }
      missing += (missing.empty() ? "" : ", ");
      missing += binding.token;
    }
    if (!missing.empty()) {
      runtime->shape.notes.push_back("no provider value for " + missing +
                                     "; those rows keep their own default");
    }
  }

  std::string summary = std::string("provider ") +
                        (runtime->Active() ? "compatible" : "not integrated") + ": " +
                        std::to_string(runtime->installation.present.size()) + "/" +
                        std::to_string(kRecordCount) + " records, " +
                        std::to_string(runtime->shape.panels.size()) + " panels, " +
                        std::to_string(runtime->values.size()) + " values";
  if (!runtime->shape.problems.empty()) {
    summary += "; refused: " + runtime->shape.problems.front() +
               (runtime->shape.problems.size() > 1
                    ? " (+" + std::to_string(runtime->shape.problems.size() - 1) + " more)"
                    : "");
  }
  runtime->summary = std::move(summary);
}

bool SaveProviderFile(const std::filesystem::path& path,
                      const std::map<std::string, std::string>& updates, std::string* error) {
  if (error != nullptr) error->clear();
  if (path.empty()) {
    if (error != nullptr) *error = "no provider settings path";
    return false;
  }
  if (updates.empty()) {
    return true;  // nothing to write, and nothing to create
  }
  rb_blitz::fs::FileLock lock;
  if (!lock.Acquire(path, error)) return false;
  ProviderFile file;
  std::vector<uint8_t> bytes;
  std::string read_error;
  if (ReadFileBytes(path, &bytes, &read_error)) {
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string parse_error;
    if (!ParseProviderFile(text, &file, &parse_error)) {
      // A file this build cannot read is not one it may rewrite: the provider's own file is the
      // only copy of values it does not show us.
      if (error != nullptr) *error = "refusing to rewrite " + path.string() + ": " + parse_error;
      return false;
    }
  } else {
    // No file yet: the first save creates the shape the provider's own loader expects.
    std::error_code status_error;
    if (std::filesystem::exists(path, status_error) || status_error) {
      if (error) *error = "cannot read existing provider settings; file left intact";
      return false;
    }
    file.parsed = true;
  }

  const std::string text = WriteProviderFile(file, updates);
  return rb_blitz::fs::WriteAtomic(path, text, error);
}

}  // namespace rb_blitz::settings::ultimate_adapter
