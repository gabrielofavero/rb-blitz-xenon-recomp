// SPDX-License-Identifier: GPL-2.0-only
// The retail stream assumes frequent main-loop polls. On slow Metal frames,
// its 2048-sample forwarding cap and exact half-ring refill test let the output
// DMA replay old PCM. Its audio-derived gameplay clock then wraps backwards.
// Keep the guest decoder, crypto, copy completion, pause/seek and clock routes.
// Catch up through those same routes, with bounded work on the owning thread.
#include "hooks/audio_stream_policy.h"

#include <chrono>
#include "settings/runtime_settings.h"
#include <rex/ppc/hook.h>
#include <rex/memory/endian.h>



REX_EXTERN(__imp__sub_827E08F8);  // ogg_sync_buffer
REX_EXTERN(__imp__sub_82384C88);  // ArkFile::ReadAsync
REX_EXTERN(__imp__sub_823E61E0);  // StandardStream::PollStream
REX_EXTERN(__imp__sub_823E9828);  // StreamReceiver::Poll

namespace {
using namespace rb_blitz::audio_stream;
thread_local PendingRead pending_read;
uint32_t Load(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}
}  // namespace

REX_HOOK_RAW(sub_827E08F8) {
  bool larger = false;
  if (rb_blitz::settings::title::audio_stream_catchup.Get() && uint32_t(ctx.lr) == kReadBufferCaller &&
      ctx.r4.u32 == kBlockBytes) {
    // At this verified call site r31 is the VorbisReader, whose +48 owns
    // the file. Other file classes keep their existing read size.
    pending_read.buffer = 0;
    const uint32_t file = Load(base, ctx.r31.u32 + 48);
    larger = file && Load(base, Load(base, file) + 12) == kArkReadAsync;
    if (larger) ctx.r4.u64 = kReadBytes;
  }
  __imp__sub_827E08F8(ctx, base);
  if (larger) pending_read.buffer = ctx.r3.u32;
}

REX_HOOK_RAW(sub_82384C88) {
  if (rb_blitz::settings::title::audio_stream_catchup.Get()) {
    const uint32_t bytes = pending_read.Consume(uint32_t(ctx.lr), ctx.r4.u32, ctx.r5.u32);
    if (bytes != ctx.r5.u32) ctx.r5.u64 = bytes;
  }
  __imp__sub_82384C88(ctx, base);
}

REX_HOOK_RAW(sub_823E61E0) {
  const uint32_t stream = ctx.r3.u32;
  __imp__sub_823E61E0(ctx, base);
  if (!rb_blitz::settings::title::audio_stream_catchup.Get() || Load(base, stream + 20) != 3 ||
      Load(base, stream + 32) == Load(base, stream + 36))
    return;
  const uint32_t channel = Load(base, Load(base, stream + 32));
  const uint32_t blocks = Load(base, channel + 8), rate = Load(base, stream + 44);
  const auto start = std::chrono::steady_clock::now();
  const uint32_t saved_fp = ctx.fpscr.csr;
  // Each poll forwards at most 2048 samples. Four additional polls cover a
  // 100 ms frame at 44.1/48 kHz. Stop immediately when no PCM/copy progresses;
  // waiting reads and the decoder worker cannot benefit from busy polling.
  for (unsigned extra = 0; extra < 4 && Load(base, stream + 20) == 3; ++extra) {
    const uint32_t written = Load(base, channel + 32796);
    if (!NeedsDecodedAudio(blocks, written, Load(base, channel + 32808), rate)) break;
    const uint32_t samples = Load(base, stream + 160);
    PPCContext call = ctx;
    call.r3.u64 = stream;
    __imp__sub_823E61E0(call, base);
    ctx.fpscr.RestoreGuestBits(saved_fp);
    if (samples == Load(base, stream + 160) && written == Load(base, channel + 32796)) break;
    if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(4)) break;
  }
}

REX_HOOK_RAW(sub_823E9828) {
  const uint32_t receiver = ctx.r3.u32;
  __imp__sub_823E9828(ctx, base);
  if (!rb_blitz::settings::title::audio_stream_catchup.Get() || Load(base, receiver + 32784) != 2) return;
  const uint32_t blocks = Load(base, receiver + 8);
  const uint32_t saved_fp = ctx.fpscr.csr;
  for (uint32_t retry = 0; retry < blocks; ++retry) {
    if (!MissedRefill(blocks, Load(base, receiver + 32788), Load(base, receiver + 32808))) break;
    // The original poll still checks queued PCM and outstanding asynchronous
    // copies before writing; this only repairs its missed threshold crossing.
    *(base + receiver + 32792) = 1;
    const uint32_t written = Load(base, receiver + 32796), queued = Load(base, receiver + 32780);
    PPCContext call = ctx;
    call.r3.u64 = receiver;
    __imp__sub_823E9828(call, base);
    ctx.fpscr.RestoreGuestBits(saved_fp);
    if (written == Load(base, receiver + 32796) && queued == Load(base, receiver + 32780)) break;
  }
}
