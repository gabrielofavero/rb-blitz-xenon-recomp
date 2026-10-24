// SPDX-License-Identifier: GPL-2.0-only
// Fence sequencing regression and headless GPU readback. No game/UI is opened.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <rex/graphics/d3d12/fence_sync.h>
#include "check.h"
#include <array>
#include <vector>

using namespace rb_blitz::test;
using namespace rex::graphics::d3d12;
using Microsoft::WRL::ComPtr;

void TestSequence() {
  BeginCase("S_OK must signal, arm, then wait; a successful zero HRESULT is not false");
  for (HRESULT success : {S_OK, S_FALSE}) {
    std::vector<int> order;
    bool armed = false;
    const HRESULT result = SignalAndWaitForFence(
        [&] { order.push_back(1); return success; },
        [&] { order.push_back(2); armed = true; return S_OK; },
        [&] { order.push_back(3); CHECK_TRUE(armed); return S_OK; });
    CHECK_EQ(result, S_OK);
    CHECK_TRUE(order == std::vector<int>({1, 2, 3}));
  }
  BeginCase("signal/arming failures never enter a wait and preserve the HRESULT");
  uint32_t arms = 0, waits = 0;
  auto arm = [&] { ++arms; return S_OK; };
  auto wait = [&] { ++waits; return S_OK; };
  CHECK_EQ(SignalAndWaitForFence([] { return E_FAIL; }, arm, wait), E_FAIL);
  CHECK_EQ(arms, 0u);
  CHECK_EQ(waits, 0u);
  CHECK_EQ(SignalAndWaitForFence([] { return S_OK; }, [] { return E_OUTOFMEMORY; }, wait),
           E_OUTOFMEMORY);
  CHECK_EQ(waits, 0u);
  CHECK_EQ(ArmAndWaitForFence([] { return E_FAIL; }, wait), E_FAIL);
  CHECK_EQ(waits, 0u);
  CHECK_EQ(ArmAndWaitForFence(arm, [] { return HRESULT_FROM_WIN32(WAIT_TIMEOUT); }),
           HRESULT_FROM_WIN32(WAIT_TIMEOUT));
}

void TestReadiness() {
  BeginCase("mapped/allocated readback slots require a completed write of sufficient size");
  CHECK_FALSE(ReadbackCopyReady(0, 100, 1024, 1024));
  CHECK_FALSE(ReadbackCopyReady(6, 5, 1024, 1024));
  CHECK_FALSE(ReadbackCopyReady(6, 6, 512, 1024));
  CHECK_FALSE(ReadbackCopyReady(6, UINT64_MAX, 1024, 1024));
  CHECK_FALSE(ReadbackCopyReady(6, 6, 1024, 0));
  CHECK_TRUE(ReadbackCopyReady(6, 6, 1024, 1024));
  CHECK_TRUE(ReadbackCopyReady(6, 7, 1024, 512));
}

bool TestGpu() {
  BeginCase("headless D3D12 queue signal/event and GPU readback");
  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    std::puts("SKIP: no usable D3D12 device for headless validation");
    return false;
  }
  auto ok = [](HRESULT result) { CHECK_TRUE(SUCCEEDED(result)); return SUCCEEDED(result); };
  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!ok(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))) return true;
  ComPtr<ID3D12Fence> fence, gate;
  if (!ok(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
      !ok(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)))) return true;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> commands;
  if (!ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
      !ok(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&commands)))) return true;
  constexpr uint32_t bytes = 1024;
  D3D12_RESOURCE_DESC buffer{};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = bytes; buffer.Height = 1; buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD; heap.CreationNodeMask = 1; heap.VisibleNodeMask = 1;
  ComPtr<ID3D12Resource> upload, readback;
  if (!ok(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) return true;
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  if (!ok(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return true;
  std::array<uint32_t, bytes / sizeof(uint32_t)> expected{};
  for (uint32_t i = 0; i < expected.size(); ++i) expected[i] = 0xa5a50000u ^ (i * 7919);
  void* data = nullptr;
  D3D12_RANGE no_reads{0, 0};
  if (!ok(upload->Map(0, &no_reads, &data))) return true;
  std::memcpy(data, expected.data(), bytes);
  upload->Unmap(0, nullptr);
  commands->CopyBufferRegion(readback.Get(), 0, upload.Get(), 0, bytes);
  if (!ok(commands->Close())) return true;
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  CHECK_TRUE(event != nullptr);
  if (!event) return true;
  // Delay actual GPU completion deterministically, rather than assume the GPU
  // will be slower than this test's CPU thread.
  if (!ok(queue->Wait(gate.Get(), 1))) { CloseHandle(event); return true; }
  ID3D12CommandList* lists[] = {commands.Get()};
  queue->ExecuteCommandLists(1, lists);
  const HRESULT queued = queue->Signal(fence.Get(), 1);
  ok(queued);
  CHECK_FALSE(ReadbackCopyReady(1, fence->GetCompletedValue(), bytes, bytes));
  ok(gate->Signal(1));
  bool armed = false;
  const HRESULT waited = SignalAndWaitForFence(
      [&] { return queue->Signal(fence.Get(), 2); },
      [&] { armed = true; return fence->SetEventOnCompletion(2, event); },
      [&] {
        CHECK_TRUE(armed);
        const DWORD result = WaitForSingleObject(event, 5000);
        return result == WAIT_OBJECT_0 ? S_OK : HRESULT_FROM_WIN32(
            result == WAIT_FAILED ? GetLastError() : WAIT_TIMEOUT);
      });
  CloseHandle(event);
  if (!ok(waited)) return true;
  CHECK_TRUE(ReadbackCopyReady(1, fence->GetCompletedValue(), bytes, bytes));
  D3D12_RANGE reads{0, bytes};
  if (!ok(readback->Map(0, &reads, &data))) return true;
  CHECK_MEM_EQ(data, static_cast<const void*>(expected.data()), bytes);
  D3D12_RANGE no_writes{0, 0};
  readback->Unmap(0, &no_writes);
  return true;
}

int main() {
  TestSequence();
  TestReadiness();
  const bool gpu = TestGpu();
  const int result = Finish();
  return result != 0 ? result : (gpu ? 0 : 77);
}
