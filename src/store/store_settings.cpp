// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_settings.h.

#include "store/store_settings.h"

#include <mutex>
#include <utility>

namespace rb_blitz::store {
namespace {

std::mutex g_mutex;
SettingsSnapshot g_settings = std::make_shared<const StoreSettings>();
std::uint64_t g_generation = 1;

bool SameFilter(const InstrumentFilter& lhs, const InstrumentFilter& rhs) {
  return lhs.guitar == rhs.guitar && lhs.bass == rhs.bass && lhs.drums == rhs.drums &&
         lhs.vocals == rhs.vocals && lhs.keys == rhs.keys;
}

bool SameSettings(const StoreSettings& lhs, const StoreSettings& rhs) {
  return lhs.enabled == rhs.enabled && SameFilter(lhs.instruments, rhs.instruments);
}

}  // namespace

SettingsSnapshot Settings() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  return g_settings;
}

std::uint64_t SettingsGeneration() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  return g_generation;
}

std::uint64_t PublishSettings(const StoreSettings& settings) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  if (SameSettings(*g_settings, settings)) {
    return g_generation;
  }
  g_settings = std::make_shared<const StoreSettings>(settings);
  return ++g_generation;
}

bool IsStoreSetting(settings::SettingId id) {
  switch (id) {
    case settings::SettingId::kRhythmVerse:
    case settings::SettingId::kRhythmVerseGuitar:
    case settings::SettingId::kRhythmVerseBass:
    case settings::SettingId::kRhythmVerseDrums:
    case settings::SettingId::kRhythmVerseVocals:
    case settings::SettingId::kRhythmVerseKeys:
      return true;
    default:
      return false;
  }
}

bool ApplyRowToSettings(settings::SettingId id, const settings::Value& value, StoreSettings* settings) {
  if (settings == nullptr || value.kind != settings::ValueKind::kBool) {
    return false;
  }
  switch (id) {
    case settings::SettingId::kRhythmVerse:
      settings->enabled = value.boolean;
      return true;
    case settings::SettingId::kRhythmVerseGuitar:
      settings->instruments.guitar = value.boolean;
      return true;
    case settings::SettingId::kRhythmVerseBass:
      settings->instruments.bass = value.boolean;
      return true;
    case settings::SettingId::kRhythmVerseDrums:
      settings->instruments.drums = value.boolean;
      return true;
    case settings::SettingId::kRhythmVerseVocals:
      settings->instruments.vocals = value.boolean;
      return true;
    case settings::SettingId::kRhythmVerseKeys:
      settings->instruments.keys = value.boolean;
      return true;
    default:
      return false;
  }
}

bool ApplyCommittedRow(settings::SettingId id, const settings::Value& value, std::string* error) {
  if (!IsStoreSetting(id)) {
    if (error != nullptr) {
      *error = "the row is not a RhythmVerse setting";
    }
    return false;
  }
  StoreSettings next = *Settings();
  if (!ApplyRowToSettings(id, value, &next)) {
    if (error != nullptr) {
      *error = "the row's value is not a boolean";
    }
    return false;
  }
  PublishSettings(next);
  return true;
}

bool AnswerStoreQuery(std::string_view path, bool* answer) {
  constexpr std::string_view kPrefix = "rbbz-store/";
  if (!path.starts_with(kPrefix) || answer == nullptr) {
    return false;
  }
  const std::string_view rest = path.substr(kPrefix.size());
  const SettingsSnapshot snapshot = Settings();
  if (rest == "enabled") {
    *answer = snapshot->enabled;
    return true;
  }
  constexpr std::string_view kInstrument = "instrument/";
  if (rest.starts_with(kInstrument)) {
    const std::optional<Instrument> instrument = InstrumentFromToken(rest.substr(kInstrument.size()));
    if (!instrument) {
      return false;
    }
    *answer = snapshot->instruments.Allows(*instrument);
    return true;
  }
  return false;
}

}  // namespace rb_blitz::store
