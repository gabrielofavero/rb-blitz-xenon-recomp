// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Minimal assertion helpers for host-side unit tests. Deliberately
// dependency-free: the SDK vendors catch2 as a submodule we do not initialise,
// and these tests must be buildable from a bare checkout of src/ plus tests/.
// docs/rb3-references.md §7.3 asks for exactly this - host code tested without
// booting the game.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace rb_blitz::test {

inline int g_checks = 0;
inline int g_failures = 0;
inline const char* g_case = "<none>";

inline void BeginCase(const char* name) {
  g_case = name;
  std::printf("[ RUN  ] %s\n", name);
}

inline void Fail(const char* file, int line, const std::string& message) {
  ++g_failures;
  std::printf("[ FAIL ] %s\n         %s:%d\n         %s\n", g_case, file, line, message.c_str());
}

inline std::string HexDump(const uint8_t* data, size_t size) {
  std::string out;
  char pair[4];
  for (size_t i = 0; i < size; ++i) {
    std::snprintf(pair, sizeof(pair), "%02X", data[i]);
    if (i != 0) {
      out += ' ';
    }
    out += pair;
  }
  return out;
}

inline void CheckTrue(const char* file, int line, bool ok, const char* expr) {
  ++g_checks;
  if (!ok) {
    Fail(file, line, std::string("expected true, got false: ") + expr);
  }
}

inline void CheckFalse(const char* file, int line, bool ok, const char* expr) {
  ++g_checks;
  if (ok) {
    Fail(file, line, std::string("expected false, got true: ") + expr);
  }
}

inline void CheckEq(const char* file, int line, uint64_t actual, uint64_t expected,
                    const char* actual_expr, const char* expected_expr) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  char message[256];
  std::snprintf(message, sizeof(message), "%s == %s failed: 0x%llX vs 0x%llX (%llu vs %llu)",
                actual_expr, expected_expr, static_cast<unsigned long long>(actual),
                static_cast<unsigned long long>(expected), static_cast<unsigned long long>(actual),
                static_cast<unsigned long long>(expected));
  Fail(file, line, message);
}

inline void CheckMemEq(const char* file, int line, const uint8_t* actual, const uint8_t* expected,
                       size_t size, const char* actual_expr, const char* expected_expr) {
  ++g_checks;
  if (std::memcmp(actual, expected, size) == 0) {
    return;
  }
  Fail(file, line, std::string(actual_expr) + " != " + expected_expr + "\n         actual   " +
                        HexDump(actual, size) + "\n         expected " + HexDump(expected, size));
}

// 0 on success, 1 on any failed check, so CTest reports a failing test.
inline int Finish() {
  if (g_failures == 0) {
    std::printf("[ DONE ] %d checks passed\n", g_checks);
    return 0;
  }
  std::printf("[ DONE ] %d of %d checks failed\n", g_failures, g_checks);
  return 1;
}

}  // namespace rb_blitz::test

#define CHECK_TRUE(expr) \
  ::rb_blitz::test::CheckTrue(__FILE__, __LINE__, static_cast<bool>(expr), #expr)

#define CHECK_FALSE(expr) \
  ::rb_blitz::test::CheckFalse(__FILE__, __LINE__, static_cast<bool>(expr), #expr)

#define CHECK_EQ(actual, expected)                                                          \
  ::rb_blitz::test::CheckEq(__FILE__, __LINE__, static_cast<uint64_t>(actual),              \
                            static_cast<uint64_t>(expected), #actual, #expected)

#define CHECK_MEM_EQ(actual, expected, size)                                                 \
  ::rb_blitz::test::CheckMemEq(__FILE__, __LINE__, static_cast<const uint8_t*>(actual),      \
                               static_cast<const uint8_t*>(expected), static_cast<size_t>(size), \
                               #actual, #expected)
