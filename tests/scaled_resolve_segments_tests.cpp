// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "gpu/xenos/host/vulkan/scaled_resolve_segments.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <random>
#include <vector>

using namespace rb_blitz::test;
using rex::gpu::vulkan::ScaledResolveSegments;
using Binding = ScaledResolveSegments::Binding;
constexpr uint64_t kBlock = ScaledResolveSegments::kAlignment;

// Executes recorded work later, independently of the interval map. Expected
// reads come from a flat logical byte array updated in command-recording order.
class QueuedGPU {
 public:
  explicit QueuedGPU(size_t logical_size) : expected_(logical_size) {}

  bool Acquire(ScaledResolveSegments& segments, uint64_t start, uint64_t size,
               Binding& binding) {
    return segments.Acquire(start, size,
        [&](uint64_t allocation_size) -> uint64_t {
          ++allocation_attempts;
          uint64_t retained = 0;
          for (const auto& entry : buffers) retained += entry.second.size();
          if (fail_allocation || allocation_size > max_allocation ||
              retained > budget || allocation_size > budget - retained) return 0;
          const auto handle = ++next_handle_;
          buffers.emplace(handle, std::vector<uint8_t>(size_t(allocation_size)));
          return handle;
        },
        [&](uint64_t source, uint64_t destination, uint64_t source_offset,
            uint64_t destination_offset, uint64_t size) {
          commands_.push_back({Kind::kCopy, source, destination, source_offset,
                               destination_offset, size, {}});
          ++copies;
        },
        [&](uint64_t handle) { retired.push_back({handle, submission}); }, binding);
  }

  void Write(const Binding& binding, uint64_t logical_start, uint8_t value) {
    std::vector<uint8_t> bytes(size_t(binding.length), value);
    commands_.push_back({Kind::kWrite, 0, binding.handle, 0, binding.offset,
                         binding.length, bytes});
    std::copy(bytes.begin(), bytes.end(), expected_.begin() + logical_start);
  }

  void Read(const Binding& binding, uint64_t logical_start) {
    std::vector<uint8_t> bytes(expected_.begin() + logical_start,
                               expected_.begin() + logical_start + binding.length);
    commands_.push_back({Kind::kRead, binding.handle, 0, binding.offset, 0,
                         binding.length, std::move(bytes)});
  }

  void Execute() {
    for (const auto& command : commands_) {
      const uint64_t used_handle = command.kind == Kind::kWrite
          ? command.destination : command.source;
      CHECK_TRUE(buffers.contains(used_handle));
      if (!buffers.contains(used_handle)) continue;
      if (command.kind == Kind::kCopy) {
        CHECK_TRUE(buffers.contains(command.destination));
        if (!buffers.contains(command.destination)) continue;
        const auto& source = buffers.at(command.source);
        auto& destination = buffers.at(command.destination);
        CHECK_TRUE(command.source_offset + command.size <= source.size());
        CHECK_TRUE(command.destination_offset + command.size <= destination.size());
        if (command.source_offset + command.size > source.size() ||
            command.destination_offset + command.size > destination.size()) continue;
        std::copy_n(source.begin() + command.source_offset, size_t(command.size),
                    destination.begin() + command.destination_offset);
      } else if (command.kind == Kind::kWrite) {
        auto& destination = buffers.at(command.destination);
        CHECK_TRUE(command.destination_offset + command.size <= destination.size());
        if (command.destination_offset + command.size <= destination.size())
          std::copy(command.bytes.begin(), command.bytes.end(),
                    destination.begin() + command.destination_offset);
      } else {
        const auto& source = buffers.at(command.source);
        CHECK_TRUE(command.source_offset + command.size <= source.size());
        if (command.source_offset + command.size <= source.size())
          CHECK_MEM_EQ(source.data() + command.source_offset, command.bytes.data(),
                       command.bytes.size());
      }
    }
    commands_.clear();
  }

  void Complete(uint64_t completed) {
    // Completion is meaningful only after queued work has run.
    CHECK_TRUE(commands_.empty());
    for (auto it = retired.begin(); it != retired.end();) {
      if (it->second <= completed) {
        buffers.erase(it->first);
        it = retired.erase(it);
      } else {
        ++it;
      }
    }
  }

  std::map<uint64_t, std::vector<uint8_t>> buffers;
  std::vector<std::pair<uint64_t, uint64_t>> retired;
  uint64_t submission = 1;
  uint64_t max_allocation = UINT64_MAX;
  uint64_t budget = UINT64_MAX;
  bool fail_allocation = false;
  unsigned allocation_attempts = 0;
  unsigned copies = 0;

 private:
  enum class Kind { kCopy, kWrite, kRead };
  struct Command {
    Kind kind;
    uint64_t source, destination, source_offset, destination_offset, size;
    std::vector<uint8_t> bytes;
  };
  std::vector<Command> commands_;
  std::vector<uint8_t> expected_;
  uint64_t next_handle_ = 0;
};

int main() {
  BeginCase("distant addresses allocate compact backing with relative descriptors");
  ScaledResolveSegments segments(128 * kBlock);
  QueuedGPU gpu(128 * kBlock);
  Binding first, distant, alias;
  CHECK_TRUE(gpu.Acquire(segments, 5 * kBlock + 256, 128, first));
  CHECK_TRUE(gpu.Acquire(segments, 100 * kBlock + 512, 256, distant));
  CHECK_EQ(segments.spans().size(), 2);
  CHECK_EQ(gpu.buffers.at(first.handle).size(), kBlock);
  CHECK_EQ(gpu.buffers.at(distant.handle).size(), kBlock);
  CHECK_EQ(first.offset, 256);
  CHECK_EQ(distant.offset, 512);
  CHECK_EQ(distant.length, 256);
  CHECK_TRUE(first.handle != distant.handle);
  gpu.Write(first, 5 * kBlock + 256, 0x31);
  gpu.Write(distant, 100 * kBlock + 512, 0x72);
  CHECK_TRUE(segments.Find(5 * kBlock + 272, 64, alias));
  CHECK_EQ(alias.handle, first.handle);
  CHECK_EQ(alias.offset, 272);
  gpu.Read(alias, 5 * kBlock + 272);
  const auto attempts = gpu.allocation_attempts;
  CHECK_TRUE(gpu.Acquire(segments, 5 * kBlock + 280, 32, alias));
  CHECK_EQ(gpu.allocation_attempts, attempts);
  gpu.Execute();

  BeginCase("bridging ranges preserve queued writes and old descriptors");
  ScaledResolveSegments bridge(8 * kBlock);
  QueuedGPU bridge_gpu(8 * kBlock);
  Binding left, right, middle;
  CHECK_TRUE(bridge_gpu.Acquire(bridge, kBlock + 256, 256, left));
  CHECK_TRUE(bridge_gpu.Acquire(bridge, 3 * kBlock + 256, 256, right));
  bridge_gpu.Write(left, kBlock + 256, 0x43);
  bridge_gpu.Write(right, 3 * kBlock + 256, 0xA7);
  bridge_gpu.Read(left, kBlock + 256);
  CHECK_TRUE(bridge_gpu.Acquire(bridge, kBlock + 512, 2 * kBlock, middle));
  CHECK_EQ(bridge.spans().size(), 1);
  CHECK_EQ(bridge.spans().front().start, kBlock);
  CHECK_EQ(bridge.spans().front().size, 3 * kBlock);
  CHECK_EQ(bridge_gpu.copies, 2);
  CHECK_EQ(bridge_gpu.retired.size(), 2);
  // Descriptors captured before replacement still read live old resources.
  bridge_gpu.Read(left, kBlock + 256);
  bridge_gpu.Read(right, 3 * kBlock + 256);
  CHECK_TRUE(bridge.Find(kBlock + 256, 256, alias));
  bridge_gpu.Read(alias, kBlock + 256);
  CHECK_TRUE(bridge.Find(3 * kBlock + 256, 256, alias));
  bridge_gpu.Read(alias, 3 * kBlock + 256);
  bridge_gpu.Write(alias, 3 * kBlock + 256, 0xBB);
  CHECK_TRUE(bridge.Find(3 * kBlock + 272, 64, alias));
  bridge_gpu.Read(alias, 3 * kBlock + 272);
  bridge_gpu.Execute();
  bridge_gpu.Complete(0);
  CHECK_TRUE(bridge_gpu.buffers.contains(left.handle));
  CHECK_TRUE(bridge_gpu.buffers.contains(right.handle));
  bridge_gpu.Complete(1);
  CHECK_FALSE(bridge_gpu.buffers.contains(left.handle));
  CHECK_FALSE(bridge_gpu.buffers.contains(right.handle));
  CHECK_EQ(bridge_gpu.buffers.size(), 1);

  BeginCase("allocation and peak-budget failures leave the old map intact");
  const auto old_span = bridge.spans().front();
  const auto old_copies = bridge_gpu.copies;
  Binding unchanged{999, 333, 444};
  bridge_gpu.fail_allocation = true;
  CHECK_FALSE(bridge_gpu.Acquire(bridge, 0, 2 * kBlock, unchanged));
  CHECK_EQ(unchanged.handle, 999);
  CHECK_EQ(unchanged.offset, 333);
  CHECK_EQ(unchanged.length, 444);
  CHECK_EQ(bridge.spans().size(), 1);
  CHECK_EQ(bridge.spans().front().handle, old_span.handle);
  CHECK_EQ(bridge.spans().front().start, old_span.start);
  CHECK_EQ(bridge.spans().front().size, old_span.size);
  CHECK_EQ(bridge_gpu.copies, old_copies);
  bridge_gpu.fail_allocation = false;
  bridge_gpu.max_allocation = 3 * kBlock;
  CHECK_FALSE(bridge_gpu.Acquire(bridge, 0, 2 * kBlock, unchanged));
  bridge_gpu.max_allocation = UINT64_MAX;
  bridge_gpu.budget = 6 * kBlock;  // Existing 3 plus replacement 4 exceeds peak.
  CHECK_FALSE(bridge_gpu.Acquire(bridge, 0, 2 * kBlock, unchanged));
  CHECK_EQ(bridge.spans().front().handle, old_span.handle);
  bridge_gpu.budget = 7 * kBlock;
  CHECK_TRUE(bridge_gpu.Acquire(bridge, 0, 2 * kBlock, middle));
  CHECK_EQ(bridge_gpu.retired.size(), 1);
  CHECK_TRUE(bridge.Find(3 * kBlock + 272, 64, alias));
  bridge_gpu.Read(alias, 3 * kBlock + 272);
  bridge_gpu.Execute();
  bridge_gpu.Complete(1);

  BeginCase("end bounds, overflow, empty requests and configuration are explicit");
  const auto before_attempts = gpu.allocation_attempts;
  CHECK_FALSE(gpu.Acquire(segments, 128 * kBlock - 1, 2, unchanged));
  CHECK_FALSE(gpu.Acquire(segments, UINT64_MAX - 10, 20, unchanged));
  CHECK_EQ(gpu.allocation_attempts, before_attempts);
  CHECK_TRUE(gpu.Acquire(segments, 128 * kBlock, 0, unchanged));
  CHECK_EQ(unchanged.handle, 0);
  CHECK_EQ(gpu.allocation_attempts, before_attempts);
  CHECK_FALSE(segments.Configure(4 * kBlock));
  ScaledResolveSegments partial(kBlock + 17);
  QueuedGPU partial_gpu(kBlock + 17);
  CHECK_TRUE(partial_gpu.Acquire(partial, kBlock + 16, 1, alias));
  CHECK_EQ(partial.spans().front().size, 17);
  partial_gpu.Write(alias, kBlock + 16, 17);
  partial_gpu.Read(alias, kBlock + 16);
  partial_gpu.Execute();
  ScaledResolveSegments near_max(UINT64_MAX);
  uint64_t requested_size = 0;
  CHECK_TRUE(near_max.Acquire(UINT64_MAX - 16, 16,
      [&](uint64_t size) { requested_size = size; return uint64_t(1); },
      [](auto...) {}, [](auto...) {}, alias));
  CHECK_EQ(requested_size, kBlock - 1);
  CHECK_EQ(alias.offset, kBlock - 17);
  CHECK_EQ(alias.length, 16);

  BeginCase("mixed queued aliases agree with a flat logical-memory oracle");
  ScaledResolveSegments random_segments(32 * kBlock);
  QueuedGPU random_gpu(32 * kBlock);
  std::mt19937 random(0x5841122D);
  for (unsigned iteration = 0; iteration < 1200; ++iteration) {
    const uint64_t length = 1 + random() % (3 * kBlock);
    const uint64_t start = random() % (32 * kBlock - length);
    Binding write;
    CHECK_TRUE(random_gpu.Acquire(random_segments, start, length, write));
    random_gpu.Write(write, start, uint8_t(random()));
    const uint64_t inner = random() % length;
    Binding read;
    CHECK_TRUE(random_segments.Find(start + inner, length - inner, read));
    random_gpu.Read(read, start + inner);
    // Spans never overlap; their offsets retain the original logical order.
    const auto& spans = random_segments.spans();
    for (size_t index = 1; index < spans.size(); ++index)
      CHECK_TRUE(spans[index - 1].start + spans[index - 1].size <= spans[index].start);
    if (iteration % 37 == 36) {
      random_gpu.Execute();
      random_gpu.Complete(random_gpu.submission);
      ++random_gpu.submission;
    }
  }
  random_gpu.Execute();
  random_gpu.Complete(random_gpu.submission);
  random_segments.Clear([&](uint64_t handle) { random_gpu.buffers.erase(handle); });
  CHECK_TRUE(random_segments.spans().empty());
  CHECK_TRUE(random_gpu.buffers.empty());
  CHECK_TRUE(random_segments.Configure(2 * kBlock));
  CHECK_FALSE(random_segments.Find(3 * kBlock, 1, alias));
  return Finish();
}
