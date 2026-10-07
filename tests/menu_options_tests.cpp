// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the menu filter's edit (src/ui/menu_options.{h,cpp}), against a
// .dtb built here rather than taken from the game: what has to be right is the
// shape of the rewrite, and the shape is decidable without a boot. No retail
// content is used or needed (customization-plan.md D14).
//
// What is pinned is the contract the read hook relies on. The file's length is
// the ark index's, so an edit that changes it is not an edit but a damaged
// archive: the length is checked byte for byte, together with the bytes that
// precede the option list and whatever follows the file in the block it was read
// in. The rows leave the list the guest navigates. The bytes they take with them
// come back as extra characters in the list's own `#ifdef` macro name, which
// makes that macro undefined, and the title's loader *skips* everything up to
// the matching `#endif` when that happens - so the block between the two has to
// end up empty, or a row the player asked to keep would vanish with it.
//
// The cases are the ones a user's own row list can produce: the compiled
// default, a single row, a row inside the conditional's block, a row that is not
// there, an empty list, a name the file only uses as structure, and a list with
// no conditional to pay the bytes back with.

#include "check.h"

#include "ui/menu_options.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::menu_options;
using namespace rb_blitz::test;

// --- the fixture ----------------------------------------------------------
//
// A compiled DTA carrying the menu's option array, shaped like the one the title
// ships: a root array, the command that defines the panel, and the option list
// as a `{array (...)}` whose elements include an `#ifdef` block. The row names
// are the title's own, because they are what the toggle's default list matches.

constexpr uint32_t kInt = 0x00;
constexpr uint32_t kVar = 0x02;
constexpr uint32_t kSymbol = 0x05;
constexpr uint32_t kIfdef = 0x07;
constexpr uint32_t kElse = 0x08;
constexpr uint32_t kEndif = 0x09;
constexpr uint32_t kArray = 0x10;
constexpr uint32_t kCommand = 0x11;
constexpr uint32_t kString = 0x12;
constexpr uint32_t kDefine = 0x20;
constexpr uint32_t kSeed = 0x1234ABCDu;

void PushU16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value & 0xFF));
  out->push_back(static_cast<uint8_t>(value >> 8));
}

void PushU32(std::vector<uint8_t>* out, uint32_t value) {
  out->push_back(static_cast<uint8_t>(value & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

std::vector<uint8_t> Cat(std::initializer_list<std::vector<uint8_t>> parts) {
  std::vector<uint8_t> out;
  for (const std::vector<uint8_t>& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

std::vector<uint8_t> Name(uint32_t tag, const std::string& text) {
  std::vector<uint8_t> out;
  PushU32(&out, tag);
  PushU32(&out, static_cast<uint32_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
  return out;
}

std::vector<uint8_t> Scalar(uint32_t tag, uint32_t value) {
  std::vector<uint8_t> out;
  PushU32(&out, tag);
  PushU32(&out, value);
  return out;
}

std::vector<uint8_t> BlockOf(uint32_t tag, uint16_t line,
                             const std::vector<std::vector<uint8_t>>& kids) {
  std::vector<uint8_t> out;
  PushU32(&out, tag);
  PushU16(&out, static_cast<uint16_t>(kids.size()));
  PushU16(&out, line);
  PushU16(&out, 0);
  for (const std::vector<uint8_t>& kid : kids) {
    out.insert(out.end(), kid.begin(), kid.end());
  }
  return out;
}

std::vector<uint8_t> Block(uint32_t tag, uint16_t line,
                           std::initializer_list<std::vector<uint8_t>> kids) {
  return BlockOf(tag, line, std::vector<std::vector<uint8_t>>(kids));
}

// The option array: the rows the menu opens and closes with, the achievement
// list behind an `#ifdef` block, and the rest. `second_ifdef` adds the second
// conditional the retail file carries around its exit row.
std::vector<uint8_t> OptionArray(bool second_ifdef) {
  std::vector<std::vector<uint8_t>> kids{
      Name(kSymbol, "splash_start"),
      Name(kSymbol, "splash_leaderboard"),
      Name(kIfdef, "HX_XBOX"),
      Name(kSymbol, "splash_achievements"),
      Scalar(kEndif, 0),
      Name(kSymbol, "splash_options"),
      Name(kSymbol, "splash_upgrade"),
      Name(kSymbol, "splash_dlc"),
  };
  if (second_ifdef) {
    kids.push_back(Name(kIfdef, "HX_XBOX"));
  }
  kids.push_back(Name(kSymbol, "splash_exit"));
  if (second_ifdef) {
    kids.push_back(Scalar(kEndif, 0));
  }
  std::vector<uint8_t> out;
  PushU32(&out, kArray);
  PushU16(&out, static_cast<uint16_t>(kids.size()));
  PushU16(&out, 44);
  PushU16(&out, 0);
  for (const std::vector<uint8_t>& kid : kids) {
    out.insert(out.end(), kid.begin(), kid.end());
  }
  return out;
}

std::vector<uint8_t> FixtureBody(bool second_ifdef) {
  const std::vector<uint8_t> rows = OptionArray(second_ifdef);
  const std::vector<uint8_t> assignment = Block(
      kArray, 42,
      {Name(kSymbol, "options_array"), Block(kCommand, 43, {Name(kSymbol, "array"), rows})});
  const std::vector<uint8_t> panel =
      Block(kCommand, 2,
            {Name(kSymbol, "new"), Name(kSymbol, "splash_panel"),
             Block(kArray, 18,
                   {Name(kSymbol, "check_trial"),
                    Block(kCommand, 19, {Name(kSymbol, "do"), assignment})})});
  const std::vector<uint8_t> body =
      Cat({std::vector<uint8_t>{0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00}, panel,
           Name(kSymbol, "game_exit_screen"),
           Block(kCommand, 3, {Name(kSymbol, "set"), Scalar(kInt, 1)}), Scalar(kEndif, 0)});
  return body;
}

std::vector<uint8_t> FixtureFile(bool second_ifdef) {
  const std::vector<uint8_t> body = FixtureBody(second_ifdef);
  std::vector<uint8_t> file(body.size() + 4);
  EncodeFile(kSeed, body.data(), body.size(), file.data());
  return file;
}

// --- the locale fixture ---------------------------------------------------
//
// A compiled locale file, shaped like the one the Ultimate mod ships: entries are
// two-element arrays of `[key text]`, and the strings for the platform this build
// is *not* run on sit behind an `#ifdef` on that platform's macro. That macro does
// not exist here, so the conditional is skipped - which is exactly why its name can
// be shortened to pay for a longer label without changing which branch the loader
// takes.

std::vector<uint8_t> LocaleEntry(const std::string& key, const std::string& text) {
  return Block(kArray, 1, {Name(kSymbol, key), Name(kString, text)});
}

constexpr char kSecondPlatformMacro[] = "HX_PS3";

// `spare_name` is the name of the macro the unused branch is guarded by, so a test
// can make it too short to pay with; empty leaves the conditional out entirely.
std::vector<uint8_t> LocaleBody(const std::string& key, const std::string& text,
                                const std::string& spare_name) {
  std::vector<std::vector<uint8_t>> kids{
      LocaleEntry("screenshot_taken", "Screenshot Saved"),
      LocaleEntry(key, text),
      LocaleEntry("reboot_warning", "Some changes to settings require a reboot."),
  };
  if (!spare_name.empty()) {
    kids.push_back(Name(kIfdef, spare_name));
    kids.push_back(LocaleEntry("os_online_acc", "Unlock Online Only Trophies"));
    kids.push_back(Scalar(kElse, 0));
    kids.push_back(LocaleEntry("os_online_acc", "Unlock Online Only Achievements"));
    kids.push_back(Scalar(kEndif, 0));
  }
  std::vector<uint8_t> body{0x01};
  PushU16(&body, static_cast<uint16_t>(kids.size()));
  PushU16(&body, 1);
  PushU16(&body, 0);
  for (const std::vector<uint8_t>& kid : kids) {
    body.insert(body.end(), kid.begin(), kid.end());
  }
  return body;
}

std::vector<uint8_t> LocaleFile(const std::string& key, const std::string& text,
                                const std::string& spare_name) {
  const std::vector<uint8_t> body = LocaleBody(key, text, spare_name);
  std::vector<uint8_t> file(body.size() + 4);
  EncodeFile(kSeed, body.data(), body.size(), file.data());
  return file;
}

// --- the server-connect fixture -------------------------------------------
//
// A compiled DTA shaped like `ui/net/gen/server_connect.dtb`: the states as
// root-level `#define NAME (value)` pairs, the panel object whose `update_state`
// handler the edit adds its transitions to, the conditional this build skips -
// the one place leftover bytes can be parked - and the button handler that sets
// the same state, which is the code the skip stands in for. That second handler
// is what makes a second-pass check necessary: the file already says `set_state`,
// just not in the handler being edited.
//
// What a case has to be able to vary is the payment. Each constant's value list
// carries filler of its own, so a fixture can pay with more than the transitions
// cost, less, or exactly as much.

constexpr const char* kPaidConstants[] = {
    "kServerConnectPanel_StartProcess",
    "kServerConnectPanel_StartSongCache",
    "kServerConnectPanel_EndSongCache",
    "kServerConnectPanel_StartPostLogin",
    "kServerConnectPanel_StartEnumeratingContent",
    "kServerConnectPanel_CheckingFacebookPermission",
    "kServerConnectPanel_RequestingFacebookToken",
    "kServerConnectPanel_WaitingForTrialEnumeration",
};
constexpr size_t kPaidConstantCount = sizeof(kPaidConstants) / sizeof(kPaidConstants[0]);

constexpr char kPanelHandlerName[] = "update_state";
constexpr char kSkippedMacro[] = "HX_PS3";

// The fewest bytes a value list element can take: a text node's own header, with an
// empty text behind it.
constexpr size_t kFillerHeader = 8;

// A root-level `#define NAME ( 37 "xxxx..." )`: two nodes, and `filler` extra bytes
// in the value list, which keep it a valid list of values and are what the edit
// spends. The pair is returned as two nodes because the root count in the file's
// header counts nodes, not statements. Without `state_values` a constant keeps an
// empty list, which is 8 bytes less to pay with - the shape a file needs to be
// refused with.
std::vector<std::vector<uint8_t>> StateConstant(const std::string& name, size_t filler,
                                                bool state_values) {
  std::vector<std::vector<uint8_t>> value;
  if (state_values) {
    value.push_back(Scalar(kInt, 37));
  }
  if (filler > 0) {
    value.push_back(Name(kString, std::string(filler - kFillerHeader, 'x')));
  }
  return {Name(kDefine, name), BlockOf(kArray, 3, value)};
}

void PushAll(std::vector<std::vector<uint8_t>>* to,
             const std::vector<std::vector<uint8_t>>& nodes) {
  to->insert(to->end(), nodes.begin(), nodes.end());
}

std::vector<uint8_t> ServerConnectBody(const std::vector<size_t>& filler, bool skipped_block,
                                      size_t constants = kPaidConstantCount,
                                      bool state_values = true) {
  std::vector<std::vector<uint8_t>> root;
  // The states the panel's own code names, which a rewritten file still resolves.
  for (const char* state : {"kServerConnectPanel_Inactive", "kServerConnectPanel_Failed",
                            "kServerConnectPanel_NoValidLoginCandidate",
                            "kServerConnectPanel_OfflineMode", "kServerConnectPanel_Connected"}) {
    PushAll(&root, StateConstant(state, 0, true));
  }
  for (size_t i = 0; i < constants; ++i) {
    PushAll(&root, StateConstant(kPaidConstants[i], filler[i], state_values));
  }

  // The statement that draws the state's label is the handler's first command; the
  // statements around it are its siblings, which is where the edit's own go.
  std::vector<std::vector<uint8_t>> handler{
      Name(kSymbol, kPanelHandlerName),
      BlockOf(kArray, 59, {Name(kVar, "state")}),
      BlockOf(kCommand, 60,
              {Name(kSymbol, "status.lbl"), Name(kSymbol, "set"), Name(kSymbol, "text_token"),
               BlockOf(kCommand, 61,
                       {Name(kSymbol, "switch"), Name(kVar, "state"),
                        BlockOf(kArray, 62, {Name(kSymbol, "kServerConnectPanel_Inactive"),
                                             Name(kString, "")})})})};
  if (skipped_block) {
    handler.push_back(Name(kIfdef, kSkippedMacro));
    handler.push_back(BlockOf(kCommand, 89,
                              {Name(kSymbol, "if"),
                               BlockOf(kCommand, 89, {Name(kSymbol, "=="), Name(kVar, "state"),
                                                      Name(kSymbol, "kServerConnectPanel_Failed")}),
                               BlockOf(kCommand, 90, {Name(kSymbol, "status.lbl"),
                                                      Name(kSymbol, "set_token_fmt"),
                                                      Name(kSymbol, "trophy_disk_space_error")})}));
    handler.push_back(Scalar(kEndif, 0));
  }
  handler.push_back(BlockOf(kCommand, 94, {Name(kSymbol, "back.ihp"),
                                           Name(kSymbol, "set_showing"), Name(kSymbol, "FALSE")}));

  root.push_back(BlockOf(
      kCommand, 52,
      {Name(kSymbol, "new"), Name(kSymbol, "ServerConnectPanel"),
       Name(kSymbol, "server_connect_panel"),
       BlockOf(kArray, 53, {Name(kSymbol, "file"), Name(kString, "server_connect.milo")}),
       BlockOf(kArray, 59, handler),
       BlockOf(kArray, 149,
               {Name(kSymbol, "BUTTON_DOWN_MSG"),
                BlockOf(kCommand, 166, {Name(kVar, "this"), Name(kSymbol, "set_state"),
                                        Name(kSymbol, "kServerConnectPanel_OfflineMode")})})}));

  std::vector<uint8_t> body{0x01};
  PushU16(&body, static_cast<uint16_t>(root.size()));
  PushU16(&body, 1);
  PushU16(&body, 0);
  for (const std::vector<uint8_t>& node : root) {
    body.insert(body.end(), node.begin(), node.end());
  }
  return body;
}

std::vector<uint8_t> ServerConnectFile(const std::vector<size_t>& filler, bool skipped_block,
                                      size_t constants = kPaidConstantCount,
                                      bool state_values = true) {
  const std::vector<uint8_t> body = ServerConnectBody(filler, skipped_block, constants, state_values);
  std::vector<uint8_t> file(body.size() + 4);
  EncodeFile(kSeed, body.data(), body.size(), file.data());
  return file;
}

// A fixture whose payment is spread over the constants evenly, which is what the
// cases that only need the edit to apply use.
std::vector<uint8_t> ServerConnectFile(size_t filler_per_constant, bool skipped_block,
                                       size_t constants = kPaidConstantCount,
                                       bool state_values = true) {
  return ServerConnectFile(std::vector<size_t>(kPaidConstantCount, filler_per_constant),
                           skipped_block, constants, state_values);
}

// --- the downloadable-content fixture -------------------------------------
//
// A compiled DTA shaped like `ui/splash/gen/splash.dtb` where R7 matters: the
// command that turns the highlight into a case - a `switch` over the panel's state,
// whose case list is one array per row, each beginning with the row's own name - and
// the downloadable-content case, the one TapRefreshCache rewrites. The case's action
// is an `if_else` of whatever size the case asks for, because that size is the whole
// budget the replacement gets: the case's bytes go back exactly as they came.

constexpr char kDlcFixtureRow[] = "splash_dlc";

std::vector<uint8_t> DlcAction(size_t filler) {
  // `{if_else {is_online} "xxx..."}`: what the action *is* does not matter to the
  // edit, only that the case carries exactly one action of a size the test can aim
  // at - `filler` 0 leaves an action shorter than the call it has to hold.
  return Block(kCommand, 71,
               {Name(kSymbol, "if_else"),
                Block(kCommand, 71, {Name(kSymbol, "is_online")}),
                Name(kString, std::string(filler, 'x'))});
}

// `with_row` false leaves the case out, which is the file the edit has to refuse;
// `row` renames it, which is the file it has to leave alone.
std::vector<uint8_t> DlcBody(size_t action_filler, bool with_row = true,
                             const char* row = kDlcFixtureRow) {
  std::vector<std::vector<uint8_t>> cases{
      BlockOf(kArray, 70, {Name(kSymbol, "splash_start"),
                           BlockOf(kCommand, 70, {Name(kSymbol, "do"), Scalar(kInt, 2)})}),
      BlockOf(kArray, 70, {Name(kSymbol, "splash_options")}),
  };
  if (with_row) {
    cases.push_back(BlockOf(kArray, 70, {Name(kSymbol, row), DlcAction(action_filler)}));
  }
  const std::vector<uint8_t> switch_command =
      BlockOf(kCommand, 69, {Name(kSymbol, "switch"), Name(kVar, "state"),
                             BlockOf(kArray, 68, cases)});

  std::vector<uint8_t> body{0x01};
  PushU16(&body, 1);
  PushU16(&body, 1);
  PushU16(&body, 0);
  body.insert(body.end(), switch_command.begin(), switch_command.end());
  return body;
}

std::vector<uint8_t> DlcCaseFile(size_t action_filler, bool with_row = true,
                                 const char* row = kDlcFixtureRow) {
  const std::vector<uint8_t> body = DlcBody(action_filler, with_row, row);
  std::vector<uint8_t> file(body.size() + 4);
  EncodeFile(kSeed, body.data(), body.size(), file.data());
  return file;
}

// --- reading the fixture back ---------------------------------------------
//
// The test parses the file itself, with its own reading of the grammar, so a
// mistake in the module's encoder cannot hide behind a matching mistake in the
// reader: what is asserted on is the option list the guest would navigate.

struct Node {
  uint32_t tag = 0;
  std::string text;
  uint32_t scalar = 0;
  std::vector<Node> kids;
};

bool ParseNode(const std::vector<uint8_t>& body, size_t* pos, Node* out) {
  auto take32 = [&](uint32_t* value) {
    if (*pos + 4 > body.size()) {
      return false;
    }
    *value = static_cast<uint32_t>(body[*pos]) | (static_cast<uint32_t>(body[*pos + 1]) << 8) |
             (static_cast<uint32_t>(body[*pos + 2]) << 16) |
             (static_cast<uint32_t>(body[*pos + 3]) << 24);
    *pos += 4;
    return true;
  };
  auto take16 = [&](uint16_t* value) {
    if (*pos + 2 > body.size()) {
      return false;
    }
    *value = static_cast<uint16_t>(body[*pos] | (body[*pos + 1] << 8));
    *pos += 2;
    return true;
  };
  if (!take32(&out->tag)) {
    return false;
  }
  if (out->tag == kArray || out->tag == kCommand || out->tag == 0x13) {
    uint16_t arity = 0;
    uint16_t line = 0;
    uint16_t deprecated = 0;
    if (!take16(&arity) || !take16(&line) || !take16(&deprecated)) {
      return false;
    }
    for (uint16_t i = 0; i < arity; ++i) {
      Node kid;
      if (!ParseNode(body, pos, &kid)) {
        return false;
      }
      out->kids.push_back(kid);
    }
    return true;
  }
  if (out->tag == kInt || out->tag == 0x06 || out->tag == kEndif || out->tag == kElse) {
    return take32(&out->scalar);
  }
  uint32_t length = 0;
  if (!take32(&length) || *pos + length > body.size()) {
    return false;
  }
  out->text.assign(reinterpret_cast<const char*>(body.data() + *pos), length);
  *pos += length;
  return true;
}

std::vector<Node> ReadRoot(const std::vector<uint8_t>& file, size_t* end = nullptr) {
  // The keystream is its own inverse, so encoding the stored body hands back the
  // decoded one.
  std::vector<uint8_t> scratch(file.size());
  EncodeFile(kSeed, file.data() + 4, file.size() - 4, scratch.data());
  const std::vector<uint8_t> body(scratch.begin() + 4, scratch.end());
  std::vector<Node> root;
  size_t pos = 7;  // past the root array's own header
  const uint16_t arity = static_cast<uint16_t>(body[1] | (body[2] << 8));
  for (uint16_t i = 0; i < arity; ++i) {
    Node node;
    if (!ParseNode(body, &pos, &node)) {
      return {};
    }
    root.push_back(node);
  }
  if (end != nullptr) {
    // Where the file's own root count says the last node ends: a header count that
    // does not add up to the body is the desync the read hook cannot survive.
    *end = pos;
  }
  return root;
}

Node* FindOptionArray(Node* node) {
  if (node->tag == kArray) {
    bool first = false;
    bool last = false;
    for (const Node& kid : node->kids) {
      first = first || kid.text == "splash_start";
      last = last || kid.text == "splash_exit";
    }
    if (first && last) {
      return node;
    }
  }
  for (Node& kid : node->kids) {
    if (Node* found = FindOptionArray(&kid)) {
      return found;
    }
  }
  return nullptr;
}

Node* OptionArrayOf(std::vector<Node>* root) {
  for (Node& node : *root) {
    if (Node* found = FindOptionArray(&node)) {
      return found;
    }
  }
  return nullptr;
}

// The list's rows, in the order the menu would draw them: the elements that are
// not structure.
std::vector<std::string> Rows(const Node& array) {
  std::vector<std::string> rows;
  for (const Node& kid : array.kids) {
    if (kid.tag == kIfdef || kid.tag == kEndif || kid.tag == 0x08) {
      continue;
    }
    rows.push_back(kid.text);
  }
  return rows;
}

// How many elements sit between the array's first `#ifdef` and its `#endif`.
size_t BlockSize(const Node& array) {
  size_t directive = array.kids.size();
  for (size_t i = 0; i < array.kids.size(); ++i) {
    if (array.kids[i].tag == kIfdef) {
      directive = i;
      break;
    }
  }
  size_t endif = array.kids.size();
  for (size_t i = directive + 1; i < array.kids.size(); ++i) {
    if (array.kids[i].tag == kEndif) {
      endif = i;
      break;
    }
  }
  return endif - directive - 1;
}

std::string FirstIfdefName(const Node& array) {
  for (const Node& kid : array.kids) {
    if (kid.tag == kIfdef) {
      return kid.text;
    }
  }
  return {};
}

size_t CountTag(const Node& node, uint32_t tag) {
  size_t count = node.tag == tag ? 1 : 0;
  for (const Node& kid : node.kids) {
    count += CountTag(kid, tag);
  }
  return count;
}

std::string Join(const std::vector<std::string>& rows) {
  std::string text;
  for (const std::string& row : rows) {
    if (!text.empty()) {
      text += ",";
    }
    text += row;
  }
  return text;
}

// Every text filed under a locale key, in file order - there can be more than one,
// which is how a file carries a string per platform. The locale fixture's entries
// are root-level siblings, so these walk the whole root.
void CollectLocaleIn(const Node& node, const std::string& key, std::vector<std::string>* out) {
  if (node.tag == kArray && node.kids.size() == 2 && node.kids[0].tag == kSymbol &&
      node.kids[0].text == key) {
    out->push_back(node.kids[1].text);
  }
  for (const Node& kid : node.kids) {
    CollectLocaleIn(kid, key, out);
  }
}

std::vector<std::string> CollectLocale(const std::vector<Node>& root, const std::string& key) {
  std::vector<std::string> texts;
  for (const Node& node : root) {
    CollectLocaleIn(node, key, &texts);
  }
  return texts;
}

std::string LocaleTextOf(const std::vector<Node>& root, const std::string& key) {
  const std::vector<std::string> texts = CollectLocale(root, key);
  return texts.empty() ? std::string() : texts.front();
}

size_t CountTagIn(const std::vector<Node>& root, uint32_t tag) {
  size_t count = 0;
  for (const Node& node : root) {
    count += CountTag(node, tag);
  }
  return count;
}

std::string FirstMacroIn(const Node& node) {
  if (node.tag == kIfdef) {
    return node.text;
  }
  for (const Node& kid : node.kids) {
    const std::string found = FirstMacroIn(kid);
    if (!found.empty()) {
      return found;
    }
  }
  return {};
}

std::string FirstMacro(const std::vector<Node>& root) {
  for (const Node& node : root) {
    const std::string found = FirstMacroIn(node);
    if (!found.empty()) {
      return found;
    }
  }
  return {};
}

// --- reading the server-connect fixture back ------------------------------

bool HasText(const Node& node, const std::string& text) {
  if (node.text == text) {
    return true;
  }
  for (const Node& kid : node.kids) {
    if (HasText(kid, text)) {
      return true;
    }
  }
  return false;
}

size_t CountText(const Node& node, const std::string& text) {
  size_t count = node.text == text ? 1 : 0;
  for (const Node& kid : node.kids) {
    count += CountText(kid, text);
  }
  return count;
}

size_t CountTextIn(const std::vector<Node>& root, const std::string& text) {
  size_t count = 0;
  for (const Node& node : root) {
    count += CountText(node, text);
  }
  return count;
}

// The panel's `update_state` array: its first element names the handler, and the
// statements that follow are its siblings.
const Node* FindHandler(const std::vector<Node>& root) {
  for (const Node& node : root) {
    if (node.tag != kCommand) {
      continue;
    }
    for (const Node& kid : node.kids) {
      if (kid.tag == kArray && !kid.kids.empty() && kid.kids[0].text == kPanelHandlerName) {
        return &kid;
      }
    }
  }
  return nullptr;
}

// The handler's first command: the statement that draws the state's label. A
// command's children are the arguments of its call, so an edit that appends to
// this one hands `set` more arguments instead of adding a statement.
const Node* LabelStatement(const Node& handler) {
  for (const Node& kid : handler.kids) {
    if (kid.tag == kCommand) {
      return &kid;
    }
  }
  return nullptr;
}

// How many of an array's elements are commands. Null answers zero, so a lookup that
// failed fails a check rather than the run.
size_t CountCommands(const Node* array) {
  if (array == nullptr) {
    return 0;
  }
  size_t count = 0;
  for (const Node& kid : array->kids) {
    count += kid.tag == kCommand ? 1 : 0;
  }
  return count;
}

// The switch case for a row: an array whose *first* element names the row. The case
// list and the option list both hold the name - only the case holds it first - so
// this is the same rule the edit itself looks the row up with.
const Node* FindCase(const std::vector<Node>& root, const std::string& name) {
  for (const Node& node : root) {
    if (node.tag == kArray && !node.kids.empty() && node.kids[0].text == name) {
      return &node;
    }
    if (const Node* found = FindCase(node.kids, name)) {
      return found;
    }
  }
  return nullptr;
}



}  // namespace

int main() {
  BeginCase("the fixture is a .dtb this module accepts");
  {
    const std::vector<uint8_t> file = FixtureFile(true);
    CHECK_TRUE(LooksLikeDtb(file.data(), file.size()));
    std::vector<Node> root = ReadRoot(file);
    Node* array = OptionArrayOf(&root);
    CHECK_TRUE(array != nullptr);
    CHECK_TRUE(Join(Rows(*array)) ==
               "splash_start,splash_leaderboard,splash_achievements,splash_options,"
               "splash_upgrade,splash_dlc,splash_exit");
    CHECK_EQ(CountTag(root[0], kIfdef), 2);
    CHECK_EQ(CountTag(root[0], kEndif), 2);
  }

  BeginCase("the default rows are hidden, and the file keeps its length");
  {
    const std::vector<uint8_t> before = FixtureFile(true);
    std::vector<uint8_t> file = before;
    const Outcome outcome = HideRows(file.data(), file.size(), ParseRowNames(kDefaultRows));
    CHECK_TRUE(outcome.applied);
    CHECK_EQ(outcome.file_size, before.size());
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) != 0);

    std::vector<Node> root = ReadRoot(file);
    Node* array = OptionArrayOf(&root);
    CHECK_TRUE(array != nullptr);
    // The three dead rows are gone and everything else is still there, in order.
    CHECK_TRUE(Join(Rows(*array)) ==
               "splash_start,splash_options,splash_upgrade,splash_exit");
    // The conditional that paid for them is unbalanced no more: it has the same
    // endif, its block is empty, and its macro name is the freed bytes longer.
    CHECK_EQ(CountTag(root[0], kIfdef), 2);
    CHECK_EQ(CountTag(root[0], kEndif), 2);
    CHECK_EQ(BlockSize(*array), 0);
    CHECK_EQ(FirstIfdefName(*array).size(), std::strlen("HX_XBOX") + outcome.freed);
    CHECK_EQ(FirstIfdefName(*array).compare(0, 7, "HX_XBOX"), 0);
    CHECK_EQ(FirstIfdefName(*array).find_first_not_of('z', 7), std::string::npos);
    // The second conditional is untouched: still 7 bytes, still the same macro.
    size_t ifdefs = 0;
    for (const Node& kid : array->kids) {
      if (kid.tag == kIfdef && ++ifdefs == 2) {
        CHECK_TRUE(kid.text == "HX_XBOX");
      }
    }
    // No name the file carries outside the row list was disturbed.
    const Outcome again = HideRows(file.data(), file.size(), ParseRowNames(kDefaultRows));
    CHECK_FALSE(again.applied);
    CHECK_TRUE(again.reason.find("none of the named rows") != std::string::npos);
  }

  BeginCase("a row inside the conditional's block leaves with it");
  {
    // The achievement list is the block's own content, so removing it empties
    // the block exactly rather than needing the block moved out of the way.
    std::vector<uint8_t> file = FixtureFile(true);
    const Outcome outcome = HideRows(file.data(), file.size(),
                                     ParseRowNames("splash_achievements"));
    CHECK_TRUE(outcome.applied);
    CHECK_EQ(outcome.removed.size(), 1);
    std::vector<Node> root = ReadRoot(file);
    Node* array = OptionArrayOf(&root);
    CHECK_TRUE(array != nullptr);
    CHECK_TRUE(Join(Rows(*array)) == "splash_start,splash_leaderboard,splash_options,"
                                    "splash_upgrade,splash_dlc,splash_exit");
    CHECK_EQ(BlockSize(*array), 0);
  }

  BeginCase("a row outside the block takes the block's content with it unless it is moved out");
  {
    // Hiding one row makes the conditional's macro undefined, which would drop
    // whatever is left inside the block: the achievement row has to be moved in
    // front of the `#ifdef` so the menu keeps it, and the rows' own order must
    // not change.
    std::vector<uint8_t> file = FixtureFile(true);
    const Outcome outcome = HideRows(file.data(), file.size(), ParseRowNames("splash_dlc"));
    CHECK_TRUE(outcome.applied);
    CHECK_EQ(outcome.removed.size(), 1);
    std::vector<Node> root = ReadRoot(file);
    Node* array = OptionArrayOf(&root);
    CHECK_TRUE(array != nullptr);
    CHECK_TRUE(Join(Rows(*array)) ==
               "splash_start,splash_leaderboard,splash_achievements,splash_options,"
               "splash_upgrade,splash_exit");
    CHECK_EQ(BlockSize(*array), 0);
    CHECK_EQ(CountTag(root[0], kIfdef), 2);
    CHECK_EQ(CountTag(root[0], kEndif), 2);
    CHECK_EQ(FirstIfdefName(*array).size(), std::strlen("HX_XBOX") + outcome.freed);
  }

  BeginCase("a row that is not there, and an empty list");
  {
    std::vector<uint8_t> file = FixtureFile(true);
    const Outcome missing = HideRows(file.data(), file.size(), ParseRowNames("splash_none"));
    CHECK_FALSE(missing.applied);
    CHECK_TRUE(missing.reason.find("none of the named rows") != std::string::npos);
    const Outcome empty = HideRows(file.data(), file.size(), {});
    CHECK_FALSE(empty.applied);
    CHECK_TRUE(empty.reason.find("no rows were named") != std::string::npos);
  }

  BeginCase("a name the file only uses as structure is never removed");
  {
    // The `#ifdef` nodes are not rows, and removing one would leave the file's
    // conditionals unbalanced - which the title's loader does not survive.
    std::vector<uint8_t> file = FixtureFile(true);
    const std::vector<uint8_t> before = file;
    const Outcome outcome = HideRows(file.data(), file.size(), ParseRowNames("HX_XBOX"));
    CHECK_FALSE(outcome.applied);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("the bytes after the file are left alone");
  {
    // The read hook hands over a whole 64 KiB ark block, so the file it patches
    // usually has other entries behind it: only the file itself may change.
    const std::vector<uint8_t> before = FixtureFile(true);
    std::vector<uint8_t> block(before.size() + 4096, 0xA5);
    std::memcpy(block.data(), before.data(), before.size());
    const Outcome outcome = HideRows(block.data(), block.size(), ParseRowNames(kDefaultRows));
    CHECK_TRUE(outcome.applied);
    CHECK_EQ(outcome.file_size, before.size());
    bool tail_intact = true;
    for (size_t i = before.size(); i < block.size(); ++i) {
      tail_intact = tail_intact && block[i] == 0xA5;
    }
    CHECK_TRUE(tail_intact);
  }

  BeginCase("bytes that are not a .dtb are refused");
  {
    std::vector<uint8_t> junk(4096, 0x00);
    CHECK_FALSE(LooksLikeDtb(junk.data(), junk.size()));
    CHECK_FALSE(LooksLikeDtb(nullptr, 0));
    const std::vector<uint8_t> file = FixtureFile(true);
    CHECK_TRUE(LooksLikeDtb(file.data(), file.size()));
    const Outcome outcome = HideRows(junk.data(), junk.size(), ParseRowNames(kDefaultRows));
    CHECK_FALSE(outcome.applied);
    // A truncated file is not guessed at either.
    std::vector<uint8_t> short_file(file.begin(), file.begin() + 40);
    const Outcome truncated =
        HideRows(short_file.data(), short_file.size(), ParseRowNames(kDefaultRows));
    CHECK_FALSE(truncated.applied);
  }

  BeginCase("a list with no conditional to pay the bytes back with is refused");
  {
    const std::vector<uint8_t> rows =
        Block(kArray, 5, {Name(kSymbol, "splash_start"), Name(kSymbol, "splash_leaderboard"),
                          Name(kSymbol, "splash_exit")});
    const std::vector<uint8_t> body =
        Cat({std::vector<uint8_t>{0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00},
             Block(kCommand, 2, {Name(kSymbol, "new"), rows})});
    std::vector<uint8_t> file(body.size() + 4);
    EncodeFile(0x0BADF00Du, body.data(), body.size(), file.data());
    const std::vector<uint8_t> before = file;
    const Outcome outcome = HideRows(file.data(), file.size(), ParseRowNames("splash_leaderboard"));
    CHECK_FALSE(outcome.applied);
    CHECK_TRUE(outcome.reason.find("no conditional") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("the title's own digest");
  {
    // The standard vectors: the empty string, "abc", and the 56-byte sentence
    // that exercises a message split across two blocks.
    const uint8_t empty[1] = {0};
    const uint8_t abc[3] = {'a', 'b', 'c'};
    const char* sentence = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const uint8_t expected_empty[20] = {0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
                                        0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09};
    const uint8_t expected_abc[20] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                      0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    const uint8_t expected_sentence[20] = {0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae,
                                           0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1};
    uint8_t digest[20];
    Sha1Digest(empty, 0, digest);
    CHECK_MEM_EQ(digest, expected_empty, 20);
    Sha1Digest(abc, sizeof(abc), digest);
    CHECK_MEM_EQ(digest, expected_abc, 20);
    Sha1Digest(reinterpret_cast<const uint8_t*>(sentence), std::strlen(sentence), digest);
    CHECK_MEM_EQ(digest, expected_sentence, 20);
  }

  BeginCase("the row list grammar");
  {
    const std::vector<std::string> names =
        ParseRowNames(" splash_start,splash_exit , ,splash_dlc");
    CHECK_EQ(names.size(), 3);
    CHECK_TRUE(names[0] == "splash_start");
    CHECK_TRUE(names[1] == "splash_exit");
    CHECK_TRUE(names[2] == "splash_dlc");
    CHECK_EQ(ParseRowNames("").size(), 0);
    CHECK_EQ(ParseRowNames(" , , ").size(), 0);
    CHECK_EQ(ParseRowNames("one").size(), 1);
  }

  BeginCase("the Ultimate label grows into the unused macro's name");
  {
    // The mod's settings row is drawn from a locale value, and "Ultimate Settings"
    // is five characters longer than the "Mod Settings" it replaces. The five bytes
    // come out of the name of the macro that guards the other platform's strings:
    // that macro is not defined here, so the conditional is skipped before and
    // after the name shrinks, and the guest takes the same branch it always took.
    std::vector<uint8_t> file =
        LocaleFile(kModSettingsKey, kModSettingsLabel, kSecondPlatformMacro);
    const std::vector<uint8_t> before = file;
    const Renamed renamed = RenameLabel(file.data(), file.size(), kModSettingsKey,
                                        kModSettingsLabel, kUltimateSettingsLabel);
    CHECK_TRUE(renamed.applied);
    CHECK_EQ(renamed.file_size, before.size());
    CHECK_EQ(renamed.borrowed, 5);
    CHECK_TRUE(renamed.from == kModSettingsLabel);
    CHECK_TRUE(renamed.to == kUltimateSettingsLabel);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) != 0);

    std::vector<Node> root = ReadRoot(file);
    CHECK_TRUE(LocaleTextOf(root, kModSettingsKey) == kUltimateSettingsLabel);
    // Six characters gave up exactly the five the label needed.
    CHECK_TRUE(FirstMacro(root) == "H");
    // The conditional is still one balanced pair, and the string behind it - the
    // one this build never reads - says what it said before.
    CHECK_EQ(CountTagIn(root, kIfdef), 1);
    CHECK_EQ(CountTagIn(root, kElse), 1);
    CHECK_EQ(CountTagIn(root, kEndif), 1);
    const std::vector<std::string> acc = CollectLocale(root, "os_online_acc");
    CHECK_EQ(acc.size(), 2);
    CHECK_TRUE(acc[0] == "Unlock Online Only Trophies");
    CHECK_TRUE(acc[1] == "Unlock Online Only Achievements");
    // Nothing else in the file moved.
    CHECK_TRUE(LocaleTextOf(root, "screenshot_taken") == "Screenshot Saved");
    CHECK_TRUE(LocaleTextOf(root, "reboot_warning") ==
               "Some changes to settings require a reboot.");
  }

  BeginCase("a label that is not there, or under another key, is refused");
  {
    std::vector<uint8_t> file =
        LocaleFile(kModSettingsKey, kModSettingsLabel, kSecondPlatformMacro);
    const std::vector<uint8_t> before = file;
    const Renamed missed = RenameLabel(file.data(), file.size(), kModSettingsKey,
                                       "Mod Settings!", kUltimateSettingsLabel);
    CHECK_FALSE(missed.applied);
    CHECK_TRUE(missed.reason.find("no such label") != std::string::npos);
    // The text is the anchor, so a file that has the words under a different key is
    // not this feature's business.
    const Renamed other = RenameLabel(file.data(), file.size(), "mod_game",
                                      kModSettingsLabel, kUltimateSettingsLabel);
    CHECK_FALSE(other.applied);
    CHECK_TRUE(other.reason.find("no such label") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("a label of the same length needs no donor at all");
  {
    // No conditional in the file at all: the swap is byte-for-byte.
    std::vector<uint8_t> file = LocaleFile(kModSettingsKey, kModSettingsLabel, "");
    const Renamed renamed = RenameLabel(file.data(), file.size(), kModSettingsKey,
                                        kModSettingsLabel, "Mods Setting");
    CHECK_TRUE(renamed.applied);
    CHECK_EQ(renamed.borrowed, 0);
    CHECK_EQ(renamed.file_size, file.size());
    std::vector<Node> root = ReadRoot(file);
    CHECK_TRUE(LocaleTextOf(root, kModSettingsKey) == "Mods Setting");
  }

  BeginCase("a shorter label gives the bytes back to the macro name");
  {
    std::vector<uint8_t> file =
        LocaleFile(kModSettingsKey, kModSettingsLabel, kSecondPlatformMacro);
    const size_t size = file.size();
    const Renamed renamed =
        RenameLabel(file.data(), file.size(), kModSettingsKey, kModSettingsLabel, "Mod");
    CHECK_TRUE(renamed.applied);
    CHECK_EQ(renamed.file_size, size);
    CHECK_EQ(renamed.borrowed, -9);
    std::vector<Node> root = ReadRoot(file);
    CHECK_TRUE(FirstMacro(root) == std::string(kSecondPlatformMacro) + "zzzzzzzzz");
    CHECK_TRUE(LocaleTextOf(root, kModSettingsKey) == "Mod");
  }

  BeginCase("a growth with nothing to pay with is refused");
  {
    std::vector<uint8_t> file = LocaleFile(kModSettingsKey, kModSettingsLabel, "");
    const std::vector<uint8_t> before = file;
    const Renamed refused = RenameLabel(file.data(), file.size(), kModSettingsKey,
                                        kModSettingsLabel, kUltimateSettingsLabel);
    CHECK_FALSE(refused.applied);
    CHECK_TRUE(refused.reason.find("no unused macro name") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("only the macro this build does not define may pay for the bytes");
  {
    // A conditional on some *other* name is not a donor: whether that macro is
    // defined is the guest's business, and shortening a defined one would throw
    // away the branch it guards.
    std::vector<uint8_t> file = LocaleFile(kModSettingsKey, kModSettingsLabel, "HX_WII");
    const std::vector<uint8_t> before = file;
    const Renamed refused = RenameLabel(file.data(), file.size(), kModSettingsKey,
                                        kModSettingsLabel, kUltimateSettingsLabel);
    CHECK_FALSE(refused.applied);
    CHECK_TRUE(refused.reason.find("no unused macro name") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("a growth the name cannot pay for is refused");
  {
    const std::string longer = "Ultimate Settings, and more besides";
    std::vector<uint8_t> file =
        LocaleFile(kModSettingsKey, kModSettingsLabel, kSecondPlatformMacro);
    const std::vector<uint8_t> before = file;
    const Renamed refused = RenameLabel(file.data(), file.size(), kModSettingsKey,
                                        kModSettingsLabel, longer);
    CHECK_FALSE(refused.applied);
    CHECK_TRUE(refused.reason.find("too short") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("the bytes after the locale file are left alone");
  {
    const std::vector<uint8_t> before =
        LocaleFile(kModSettingsKey, kModSettingsLabel, kSecondPlatformMacro);
    std::vector<uint8_t> block(before.size() + 4096, 0xA5);
    std::memcpy(block.data(), before.data(), before.size());
    const Renamed renamed = RenameLabel(block.data(), block.size(), kModSettingsKey,
                                        kModSettingsLabel, kUltimateSettingsLabel);
    CHECK_TRUE(renamed.applied);
    CHECK_EQ(renamed.file_size, before.size());
    bool tail_intact = true;
    for (size_t i = before.size(); i < block.size(); ++i) {
      tail_intact = tail_intact && block[i] == 0xA5;
    }
    CHECK_TRUE(tail_intact);
  }

  // --- the offline prompts --------------------------------------------------

  BeginCase("the server-connect fixture is refused when its constants cannot pay");
  {
    // Value lists with nothing in them: the same names as the retail file, 8 bytes
    // less to pay with each, which is 64 bytes short of what the transitions cost.
    std::vector<uint8_t> file = ServerConnectFile(0, true, kPaidConstantCount, false);
    const std::vector<uint8_t> before = file;
    const Skipped refused = SkipOfflinePrompts(file.data(), file.size());
    CHECK_FALSE(refused.applied);
    CHECK_TRUE(refused.reason.find("cost more than the constants pay") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
    // Both sides of the arithmetic are reported even when it refuses, so a caller
    // can tell how far off the payment was: an empty value list is 8 bytes a
    // constant short, 64 in all.
    CHECK_TRUE(refused.added > 0);
    CHECK_TRUE(refused.removed > 0);
    CHECK_EQ(refused.added - refused.removed, 64);
  }

  BeginCase("the offline prompts are skipped on the panel's own file");
  {
    std::vector<uint8_t> file = ServerConnectFile(200, true);
    const size_t size = file.size();
    std::vector<uint8_t> untouched = file;

    const Skipped skipped = SkipOfflinePrompts(file.data(), file.size());
    CHECK_TRUE(skipped.applied);
    CHECK_EQ(skipped.file_size, size);
    CHECK_EQ(skipped.padding, skipped.removed - skipped.added);
    CHECK_TRUE(skipped.digest_before[0] != skipped.digest_after[0]);

    // The file still parses as one file, and its root count still adds up: a header
    // count that no longer matches the body is the desync the loader would read
    // garbage from.
    size_t end = 0;
    const std::vector<Node> root = ReadRoot(file, &end);
    CHECK_TRUE(!root.empty());
    CHECK_EQ(end, size - 4);

    // Every constant the payment named is gone, and the states the panel's own code
    // names are still there: the define, the button handler's own mention of it, and
    // one per added transition.
    for (const char* name : kPaidConstants) {
      CHECK_EQ(CountTextIn(root, name), 0);
    }
    CHECK_EQ(CountTextIn(root, "kServerConnectPanel_Failed"), 3);
    CHECK_EQ(CountTextIn(root, "kServerConnectPanel_OfflineMode"), 4);

    // The two transitions join the handler's statements, as commands of their own.
    std::vector<Node> before_root = ReadRoot(untouched);
    const Node* handler = FindHandler(root);
    const Node* fresh = FindHandler(before_root);
    const Node* label = handler != nullptr ? LabelStatement(*handler) : nullptr;
    const Node* fresh_label = fresh != nullptr ? LabelStatement(*fresh) : nullptr;
    // Looked up through helpers that answer null rather than throwing, so a fixture
    // that grew a different shape fails a check instead of taking the run down.
    const auto last = [](const Node* array, size_t back) -> const Node* {
      return array == nullptr || array->kids.size() < back ? nullptr
                                                          : &array->kids[array->kids.size() - back];
    };
    const Node* advance = last(handler, 2);
    const Node* accept = last(handler, 1);
    CHECK_TRUE(handler != nullptr && fresh != nullptr && label != nullptr &&
               fresh_label != nullptr && advance != nullptr && accept != nullptr);
    CHECK_EQ(CountCommands(handler), CountCommands(fresh) + 2);
    CHECK_EQ(advance != nullptr ? advance->tag : 0, kCommand);
    CHECK_TRUE(advance != nullptr && HasText(*advance, "||"));
    CHECK_TRUE(advance != nullptr && HasText(*advance, "kServerConnectPanel_Failed"));
    CHECK_TRUE(advance != nullptr && HasText(*advance, "set_state"));
    CHECK_EQ(accept != nullptr ? accept->tag : 0, kCommand);
    CHECK_TRUE(accept != nullptr && HasText(*accept, "goto_screen"));
    CHECK_TRUE(accept != nullptr && HasText(*accept, "main_menu"));

    // The statement that draws the label is untouched: its children are the
    // arguments of a `set` call, so a statement appended there would be one more
    // argument - a fault in the guest rather than a skipped prompt.
    CHECK_TRUE(label != nullptr && fresh_label != nullptr &&
               label->kids.size() == fresh_label->kids.size());
    CHECK_TRUE(label != nullptr && HasText(*label, "text_token"));

    // The leftover is parked in the block the build skips, which is the only place
    // in the file bytes can be added and never read.
    CHECK_EQ(CountTextIn(root, kSkippedMacro), 1);
    CHECK_TRUE(CountTextIn(root, std::string(skipped.padding - 8, ' ')) == 1);
  }

  BeginCase("the payment can be exactly what the transitions cost");
  {
    // No filler at all: the eight constants the edit spends have the lengths the
    // retail file's own do, and they add up to exactly what the two transitions
    // cost - which is why the shipped file needs no padding, and why a file that
    // pays to the byte is still the same length afterwards.
    std::vector<uint8_t> file = ServerConnectFile(0, true);
    const size_t size = file.size();
    const Skipped exact = SkipOfflinePrompts(file.data(), file.size());
    CHECK_TRUE(exact.applied);
    CHECK_EQ(exact.padding, 0);
    CHECK_EQ(exact.removed, exact.added);
    CHECK_EQ(exact.file_size, size);
    size_t end = 0;
    const std::vector<Node> root = ReadRoot(file, &end);
    CHECK_TRUE(!root.empty());
    CHECK_EQ(end, size - 4);
    CHECK_TRUE(FindHandler(root) != nullptr);
    CHECK_TRUE(FindHandler(root) != nullptr && HasText(*FindHandler(root), "goto_screen"));
  }

  BeginCase("a file that is not the panel's is left alone");
  {
    std::vector<uint8_t> file = FixtureFile(false);
    const std::vector<uint8_t> before = file;
    const Skipped refused = SkipOfflinePrompts(file.data(), file.size());
    CHECK_FALSE(refused.applied);
    CHECK_TRUE(refused.reason.find("does not define the server-connect panel") !=
               std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("a panel missing one of the constants it pays with is refused");
  {
    std::vector<uint8_t> file = ServerConnectFile(200, true, kPaidConstantCount - 1);
    const std::vector<uint8_t> before = file;
    const Skipped refused = SkipOfflinePrompts(file.data(), file.size());
    CHECK_FALSE(refused.applied);
    CHECK_TRUE(refused.reason.find(kPaidConstants[kPaidConstantCount - 1]) != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("a second pass over a patched file is refused");
  {
    std::vector<uint8_t> file = ServerConnectFile(200, true);
    CHECK_TRUE(SkipOfflinePrompts(file.data(), file.size()).applied);
    const std::vector<uint8_t> before = file;
    const Skipped again = SkipOfflinePrompts(file.data(), file.size());
    CHECK_FALSE(again.applied);
    CHECK_TRUE(again.reason.find("already skips the prompts") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("a leftover with no skipped block to park it in is refused");
  {
    // The file hands over more than the edit spends and has no conditional to put
    // the remainder in, so there is nowhere for those bytes to live.
    std::vector<uint8_t> file = ServerConnectFile(200, false);
    const std::vector<uint8_t> before = file;
    const Skipped refused = SkipOfflinePrompts(file.data(), file.size());
    CHECK_FALSE(refused.applied);
    CHECK_TRUE(refused.reason.find("no skipped block") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("the bytes after the server-connect file are left alone");
  {
    const std::vector<uint8_t> before = ServerConnectFile(200, true);
    std::vector<uint8_t> block(before.size() + 4096, 0xA5);
    std::memcpy(block.data(), before.data(), before.size());
    const Skipped skipped = SkipOfflinePrompts(block.data(), block.size());
    CHECK_TRUE(skipped.applied);
    CHECK_EQ(skipped.file_size, before.size());
    bool tail_intact = true;
    for (size_t i = before.size(); i < block.size(); ++i) {
      tail_intact = tail_intact && block[i] == 0xA5;
    }
    CHECK_TRUE(tail_intact);
  }

  BeginCase("the downloadable-content row is given the refresh");
  {
    std::vector<uint8_t> file = DlcCaseFile(400);
    const size_t size = file.size();
    std::vector<uint8_t> untouched = file;

    const Tapped tapped = TapRefreshCache(file.data(), file.size());
    CHECK_TRUE(tapped.applied);
    CHECK_EQ(tapped.file_size, size);
    // The retail action is longer than the call, so the difference is padding: the
    // ark index fixes this file's length, so a shorter action has to give its bytes
    // back rather than shorten the file.
    CHECK_TRUE(tapped.padding > 0);
    // The digest is the whole 20 bytes, not its first one: a longer path can leave the
    // first byte of the two digests agreeing by chance.
    CHECK_TRUE(std::memcmp(tapped.digest_before, tapped.digest_after, 20) != 0);

    // Still one file, with a root count that still adds up.
    size_t end = 0;
    const std::vector<Node> root = ReadRoot(file, &end);
    CHECK_TRUE(!root.empty());
    CHECK_EQ(end, size - 4);

    // The row's action is now the sentinel call and nothing else: one argument that
    // names the call, one that names the file no file has, one value the block
    // evaluates and discards.
    const Node* row = FindCase(root, kDlcFixtureRow);
    CHECK_TRUE(row != nullptr);
    if (row != nullptr) {
      CHECK_EQ(row->kids.size(), 2);
      CHECK_TRUE(HasText(*row, "file_exists"));
      // The mount prefix is part of the path the host sees, so it is part of what the
      // module writes; a bare name is answered inside the engine and never reaches a
      // file call (docs/engine/main-menu-flow.md §7.1).
      CHECK_TRUE(HasText(*row, "songcache:/rbbz_dlc_refresh"));
      CHECK_EQ(CountText(*row, std::string(tapped.padding, 'z')), 1);
      CHECK_EQ(CountText(*row, std::string(400, 'x')), 0);
    }
    // Its neighbours are untouched.
    std::vector<Node> before_root = ReadRoot(untouched);
    CHECK_EQ(CountTextIn(root, "splash_start"), CountTextIn(before_root, "splash_start"));
    CHECK_EQ(CountTextIn(root, "splash_options"), CountTextIn(before_root, "splash_options"));
    CHECK_EQ(CountTextIn(root, "switch"), CountTextIn(before_root, "switch"));
  }

  BeginCase("a row that already refreshes, and a file without the row");
  {
    std::vector<uint8_t> file = DlcCaseFile(400);
    CHECK_TRUE(TapRefreshCache(file.data(), file.size()).applied);
    const std::vector<uint8_t> before = file;
    const Tapped again = TapRefreshCache(file.data(), file.size());
    CHECK_FALSE(again.applied);
    CHECK_TRUE(again.reason.find("already refreshes") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);

    // The retail file has the row; a payload that renamed it does not, and the edit
    // refuses rather than adding one (the option list is the file's own business).
    std::vector<uint8_t> renamed = DlcCaseFile(400, true, "splash_store");
    const std::vector<uint8_t> renamed_before = renamed;
    const Tapped missing = TapRefreshCache(renamed.data(), renamed.size());
    CHECK_FALSE(missing.applied);
    CHECK_TRUE(missing.reason.find("no downloadable-content row") != std::string::npos);
    CHECK_TRUE(std::memcmp(renamed.data(), renamed_before.data(), renamed_before.size()) == 0);

    std::vector<uint8_t> absent = DlcCaseFile(400, false);
    CHECK_FALSE(TapRefreshCache(absent.data(), absent.size()).applied);
  }

  BeginCase("a row whose action cannot hold the call is refused");
  {
    // The replacement needs the call plus the padding's own empty text node; an
    // action shorter than that cannot be balanced, so the file is left alone.
    std::vector<uint8_t> file = DlcCaseFile(0);
    const std::vector<uint8_t> before = file;
    const Tapped refused = TapRefreshCache(file.data(), file.size());
    CHECK_FALSE(refused.applied);
    CHECK_TRUE(refused.reason.find("too short") != std::string::npos);
    CHECK_TRUE(std::memcmp(file.data(), before.data(), before.size()) == 0);
  }

  BeginCase("the bytes after the downloadable-content file are left alone");
  {
    const std::vector<uint8_t> before = DlcCaseFile(400);
    std::vector<uint8_t> block(before.size() + 4096, 0xA5);
    std::memcpy(block.data(), before.data(), before.size());
    const Tapped tapped = TapRefreshCache(block.data(), block.size());
    CHECK_TRUE(tapped.applied);
    CHECK_EQ(tapped.file_size, before.size());
    bool tail_intact = true;
    for (size_t i = before.size(); i < block.size(); ++i) {
      tail_intact = tail_intact && block[i] == 0xA5;
    }
    CHECK_TRUE(tail_intact);
  }

  return Finish();
}



