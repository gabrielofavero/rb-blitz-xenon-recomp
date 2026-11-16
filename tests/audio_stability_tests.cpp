// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_audio_policy.h"
#include "hooks/audio_stream_policy.h"
#include <rex/audio/frame_queue.h>
#include <rex/graphics/preparation_budget.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

int main() {
  using namespace rb_blitz::test;
  using rb_blitz::shell_music::StreamBufferSeconds;
  BeginCase("menu music gets decoded reserve without changing gameplay stream capacity");
  CHECK_TRUE(StreamBufferSeconds(true,0)==6);
  CHECK_TRUE(StreamBufferSeconds(true,1.2)==6);
  CHECK_TRUE(StreamBufferSeconds(true,10)==10);
  CHECK_TRUE(StreamBufferSeconds(false,0)==0);
  CHECK_TRUE(StreamBufferSeconds(false,1.2)==1.2);
  CHECK_TRUE(StreamBufferSeconds(true,-1)==-1);
  CHECK_TRUE(std::isnan(StreamBufferSeconds(true,std::numeric_limits<double>::quiet_NaN())));

  BeginCase("stream refill catches skipped blocks in either direction around the ring");
  using namespace rb_blitz::audio_stream;
  CHECK_FALSE(MissedRefill(8, 0, 3 * kBlockBytes));
  CHECK_FALSE(MissedRefill(8, 0, 4 * kBlockBytes)); // The retail check handles equality.
  CHECK_TRUE(MissedRefill(8, 0, 5 * kBlockBytes));
  CHECK_TRUE(MissedRefill(8, 3, 0)); // Cursor wrapped; the write cursor has not.
  CHECK_FALSE(MissedRefill(8, 7, 0));
  CHECK_FALSE(MissedRefill(0, 0, 0));
  CHECK_FALSE(MissedRefill(7, 0, 0));
  CHECK_FALSE(MissedRefill(8, 8, 0));
  CHECK_FALSE(MissedRefill(8, 0, 8 * kBlockBytes));
  CHECK_TRUE(NeedsDecodedAudio(8, 12, 3 * kBlockBytes, 44100));
  CHECK_FALSE(NeedsDecodedAudio(8, 12, 0, 44100));
  CHECK_FALSE(NeedsDecodedAudio(0, 0, 0, 44100));
  CHECK_FALSE(NeedsDecodedAudio(8, 0, 0, 0));

  BeginCase("audio read expansion requires the paired allocation and exact call site");
  PendingRead pending;
  CHECK_EQ(pending.Consume(kReadFileCaller, 0x40010000, kBlockBytes), kBlockBytes);
  pending.buffer = 0x40010000;
  CHECK_EQ(pending.Consume(0x1234, pending.buffer, kBlockBytes), kBlockBytes);
  CHECK_EQ(pending.Consume(kReadFileCaller, 0x40010000, kBlockBytes), kReadBytes);
  CHECK_EQ(pending.Consume(kReadFileCaller, 0x40010000, kBlockBytes), kBlockBytes);
  pending.buffer = 0x40010000;
  CHECK_EQ(pending.Consume(kReadFileCaller, 0x40020000, kBlockBytes), kBlockBytes);
  CHECK_EQ(pending.buffer, 0u);
  pending.buffer = 0x40010000;
  CHECK_EQ(pending.Consume(kReadFileCaller, pending.buffer, 4096), 4096u);
  CHECK_EQ(pending.buffer, 0u);

  BeginCase("preparation reserves CPU headroom and has no workers for empty input");
  using rex::graphics::PreparationWorkerCount;
  CHECK_EQ(PreparationWorkerCount(0,16),0);
  CHECK_EQ(PreparationWorkerCount(400,1),1);
  CHECK_EQ(PreparationWorkerCount(400,2),1);
  CHECK_EQ(PreparationWorkerCount(400,4),2);
  CHECK_EQ(PreparationWorkerCount(400,8),4);
  CHECK_EQ(PreparationWorkerCount(400,64),4);
  CHECK_EQ(PreparationWorkerCount(1,64),1);

  BeginCase("PCM ownership, full queue, wraparound and clear retain exact samples");
  rex::audio::AudioFrameQueue<8,8> queue;
  std::array<float,8> input{}, output{};
  CHECK_FALSE(queue.Pop(output.data()));
  for (unsigned n=0;n<8;++n) {
    input.fill(float(n));
    CHECK_TRUE(queue.Push(input.data()));
  }
  input.fill(99);
  CHECK_FALSE(queue.Push(input.data()));
  for (unsigned n=0;n<4;++n) {
    CHECK_TRUE(queue.Pop(output.data()));
    for (auto sample : output) CHECK_TRUE(sample==float(n));
  }
  for (unsigned n=8;n<12;++n) { input.fill(float(n)); CHECK_TRUE(queue.Push(input.data())); }
  for (unsigned n=4;n<12;++n) {
    CHECK_TRUE(queue.Pop(output.data()));
    for (auto sample : output) CHECK_TRUE(sample==float(n));
  }
  CHECK_FALSE(queue.Pop(output.data()));
  CHECK_TRUE(queue.Push(input.data()));
  queue.Clear();
  CHECK_FALSE(queue.Pop(output.data()));

  BeginCase("a delayed producer and consumer never replay, reorder or tear PCM frames");
  constexpr unsigned count=20000;
  std::atomic<unsigned> mismatches{0};
  std::thread producer([&] {
    std::array<float,8> frame;
    for (unsigned n=0;n<count;++n) {
      for (unsigned c=0;c<8;++c) frame[c]=float(n*8+c);
      while (!queue.Push(frame.data())) std::this_thread::yield();
      if (n%1024==0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  for (unsigned n=0;n<count;++n) {
    while (!queue.Pop(output.data())) std::this_thread::yield();
    for (unsigned c=0;c<8;++c) if (output[c]!=float(n*8+c)) ++mismatches;
    if (n%997==0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  producer.join();
  CHECK_EQ(mismatches.load(),0);
  CHECK_FALSE(queue.Pop(output.data()));
  return Finish();
}
