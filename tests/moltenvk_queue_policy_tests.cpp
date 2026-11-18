// SPDX-License-Identifier: GPL-2.0-only
// No Vulkan instance or game: exercise the production loader configuration.
#include "check.h"

#include <cstdlib>
#include <optional>
#include <string>

#include "vulkan_moltenvk.h"

int main() {
  using namespace rb_blitz::test;
  constexpr const char* key = "MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS";
  const char* original = std::getenv(key);
  const std::optional<std::string> saved = original ? std::optional<std::string>(original) : std::nullopt;
  const auto configure = [] {
    rex::gpu::vulkan::ConfigureMacOSVulkanEnvironment({});
  };
  const auto matches = [](const char* expected) {
    const char* value = std::getenv("MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS");
    return value && std::string(value) == expected;
  };

  BeginCase("Mac loader defaults to ordered asynchronous Metal encoding");
  CHECK_EQ(unsetenv(key), 0);
  configure();
  CHECK_TRUE(matches("0"));
  configure();
  CHECK_TRUE(matches("0"));

  BeginCase("explicit synchronous and asynchronous preferences remain authoritative");
  for (const char* value : {"1", "0", ""}) {
    CHECK_EQ(setenv(key, value, 1), 0);
    configure();
    CHECK_TRUE(matches(value));
  }

  if (saved) setenv(key, saved->c_str(), 1);
  else unsetenv(key);
  return Finish();
}
