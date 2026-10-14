// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P9's runtime applier. See src/hooks/hot_apply_runtime.h for the two constraints this file obeys;
// this is their implementation.

#include "hooks/hot_apply_runtime.h"

#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "settings/schema.h"

namespace rb_blitz::settings {
namespace {

// The cvar a hot row writes, by name, and the wire token the guest names it by. One entry per row
// the applier knows; a row that is `hot` in the schema but absent here is refused honestly (the
// seam reports `kNoApplier` and the row keeps its restart).
struct HotRow {
  SettingId id;
  const char* token;
  const char* cvar;
};

constexpr HotRow kHotRows[] = {
    {SettingId::kVsync, "graphics.vsync", "vsync"},
    // The caption overlay's switch (owner request, 2026-10-14). Its cvar is defined by the game
    // itself (src/ui/help_overlay.cpp) and read on every draw, so the write takes effect on the
    // next frame; the row is `kHot` in the schema for that measured reason.
    {SettingId::kSettingsLabels, "interface.settings_labels", "settings_help_overlay"},
};

constexpr char kCvarError[] = "the applier has no cvar for this row";

// The value as the cvar's own text form. `vsync` is a bool cvar; a value of the wrong kind is
// refused rather than spelled, because a guessed spelling is how a write silently does nothing.
bool CvarText(const Value& value, std::string* out) {
  if (value.kind != ValueKind::kBool) {
    return false;
  }
  *out = value.boolean ? "true" : "false";
  return true;
}

// The write, performed on its own thread: this never runs on the guest thread.
bool WriteCvar(const std::string& name, const std::string& text, std::string* error) {
  const std::string before = rex::cvar::GetFlagByName(name);
  if (before.empty()) {
    *error = "the cvar '" + name + "' is not defined in this build";
    return false;
  }
  if (before == text) {
    REXLOG_INFO("hot_apply: '{}' is already {}; nothing to write", name, text);
    return true;
  }
  if (!rex::cvar::SetFlagByName(name, text)) {
    *error = "the cvar '" + name + "' refused the value '" + text + "'";
    return false;
  }
  // Read it back: "the setter returned true" is not the evidence the seam promises, and a
  // lifecycle that quietly ignores the write must not be reported as applied.
  const std::string after = rex::cvar::GetFlagByName(name);
  if (after != text) {
    *error = "the cvar '" + name + "' still reads '" + after + "' after the write";
    return false;
  }
  REXLOG_INFO("hot_apply: {} = {} (was {}), applied to the running game", name, after, before);
  return true;
}

class CvarHotApplier : public HotApplier {
 public:
  bool Knows(SettingId id) const override {
    for (const HotRow& row : kHotRows) {
      if (row.id == id) {
        return true;
      }
    }
    return false;
  }

  bool Apply(SettingId id, const Value& value, std::string* error) override {
    const HotRow* row = nullptr;
    for (const HotRow& candidate : kHotRows) {
      if (candidate.id == id) {
        row = &candidate;
        break;
      }
    }
    if (row == nullptr) {
      *error = kCvarError;
      return false;
    }
    std::string text;
    if (!CvarText(value, &text)) {
      *error = "the row's value is not the kind its cvar takes";
      return false;
    }
    // The host thread, and the join that makes "applied" mean "written and read back".
    bool ok = false;
    std::string thread_error;
    std::thread worker([&row, &text, &ok, &thread_error] {
      ok = WriteCvar(row->cvar, text, &thread_error);
    });
    worker.join();
    if (!ok) {
      *error = thread_error;
    }
    return ok;
  }
};

CvarHotApplier& Instance() {
  static CvarHotApplier applier;
  return applier;
}

}  // namespace

HotApplier* RuntimeHotApplier() { return &Instance(); }

SettingId IdForToken(std::string_view token) {
  // The vocabulary now lives with the table it searches (P11a moved it to schema.cpp), because the
  // boot's service-backed sink needs it and that module is SDK-free. This stays as the name the
  // channel has always called.
  return schema::IdForToken(token);
}

std::string_view HotRowToken(SettingId id) {
  for (const HotRow& row : kHotRows) {
    if (row.id == id) {
      return row.token;
    }
  }
  return {};
}

std::string HotApplierSummary() {
  std::string out;
  for (const HotRow& row : kHotRows) {
    if (!out.empty()) {
      out += ", ";
    }
    out += row.token;
    out += " -> cvar '";
    out += row.cvar;
    out += "'";
  }
  return out.empty() ? std::string("none") : out;
}

}  // namespace rb_blitz::settings
