// SPDX-License-Identifier: GPL-2.0-only
// Production mesh capture and projection against isolated guest memory; no game UI.
#include "check.h"
#include "input/native_mesh.h"
#include "input/touch_controls.h"

#include <bit>
#include <cstring>
#include <limits>
#include <rex/ppc/hook.h>
#include <rex/memory/endian.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

REX_EXTERN(sub_823F0118);
namespace {
constexpr uint32_t kHeap = 0x40000000, kMesh = kHeap + 0x1000;
constexpr uint32_t kObject = kMesh + 0x300, kDir = kHeap + 0x2000;
constexpr uint32_t kTable = 0x82010000, kCamera = kHeap + 0x3000;
constexpr uint32_t kVertices = kHeap + 0x4000, kStack = kHeap + 0x10000;
uint8_t* memory = nullptr;
bool active = true, ready = true, discard_vertices = true;
unsigned publications = 0, finds = 0, uploads = 0, resolves = 0;
rb_blitz::input::touch::Rect published;

uint32_t Word(uint32_t a) { return rex::memory::load_and_swap<uint32_t>(memory + a); }
void Word(uint32_t a, uint32_t v) { rex::memory::store_and_swap<uint32_t>(memory + a, v); }
float Float(uint32_t a) { return std::bit_cast<float>(Word(a)); }
void Float(uint32_t a, float v) { Word(a, std::bit_cast<uint32_t>(v)); }
void Text(uint32_t a, const char* text) { std::memcpy(memory + a, text, std::strlen(text) + 1); }
PPCContext Context() {
  PPCContext ctx{};
  ctx.r1.u32 = kStack; ctx.r3.u32 = kMesh; ctx.r4.u32 = 0x1F;
  ctx.r31.u32 = 0xDEADBEEF;
  return ctx;
}
void Fixture(bool packed) {
  std::memset(memory + kHeap, 0, 0x10000);
  Word(kMesh + 4, kTable);
  Word(kTable + 4, kObject - kMesh - 4);
  Word(kObject + 32, kHeap + 0x5000); Text(kHeap + 0x5000, "powerup_icon.mesh");
  Word(kObject + 36, kDir);
  Word(kDir + 4, kTable + 16); Word(kTable + 20, 0);
  Word(kDir + 4 + 32, kHeap + 0x5100); Text(kHeap + 0x5100, "powerup_icon1");
  Word(kMesh + 324, kMesh);
  Word(kMesh + (packed ? 380 : 256), kVertices);
  Word(kMesh + (packed ? 384 : 260), 4);
  for (unsigned i = 0; i < 4; ++i) {
    const auto vertex = kVertices + i * (packed ? 36 : 96);
    Float(vertex, (i & 1) ? .08f : -.08f);
    Float(vertex + 4, 0);
    Float(vertex + 8, (i & 2) ? .04f : -.04f);
  }
  Float(kMesh + 136 + 48, .5f); Float(kMesh + 136 + 56, .86f);
  Word(0x8284A108, kCamera);
  active = ready = discard_vertices = true;
  publications = finds = uploads = resolves = 0;
  published = {};
}
void Upload() {
  auto ctx = Context();
  sub_823F0118(ctx, memory);
  CHECK_EQ(ctx.r3.u32, uint32_t(0x12345678));
  CHECK_EQ(ctx.r31.u32, uint32_t(0xDEADBEEF));
  CHECK_EQ(ctx.r1.u32, kStack);
}
void Capture() {
  auto ctx = Context();
  rb_blitz::input::CaptureLanePowerup(ctx, memory, kDir);
  CHECK_EQ(ctx.r1.u32, kStack);
  CHECK_EQ(ctx.r3.u32, kMesh);
  CHECK_EQ(ctx.r4.u32, uint32_t(0x1F));
  CHECK_EQ(ctx.r31.u32, uint32_t(0xDEADBEEF));
}
void Close(float value, float expected) { CHECK_TRUE(std::abs(value - expected) < .0001f); }
}

namespace rb_blitz::input {
bool GameScreenActive() { return active; }
}
namespace rb_blitz::input::touch {
bool PowerupReady() { return ready; }
void PublishLanePowerup(Rect rect) { published = rect; ++publications; }
}
extern "C" REX_FUNC(__imp__sub_823F0118) {
  ++uploads;
  Word(ctx.r3.u32 + 412, 0x1234 + uploads);
  if (discard_vertices) {
    Word(ctx.r3.u32 + 256, 0); Word(ctx.r3.u32 + 260, 0);
    Word(ctx.r3.u32 + 380, 0); Word(ctx.r3.u32 + 384, 0);
    std::memset(base + kVertices, 0, 4 * 96);
  }
  ctx.r3.u32 = 0x12345678;
}
extern "C" REX_FUNC(sub_82402DE8) {
  ++finds;
  CHECK_EQ(ctx.r3.u32, kDir);
  CHECK_TRUE(std::strcmp(reinterpret_cast<char*>(base + ctx.r4.u32), "powerup_icon.mesh") == 0);
  ctx.r3.u32 = kObject;
}
extern "C" REX_FUNC(sub_821F13D0) {
  ++resolves;
  CHECK_EQ(ctx.r3.u32, kMesh + 64);
  memory[kMesh + 253] = 0;
}
extern "C" REX_FUNC(sub_821E14A8) {
  for (uint32_t axis = 0; axis < 3; ++axis)
    Float(ctx.r5.u32 + axis * 4, Float(ctx.r3.u32 + axis * 4) + Float(ctx.r4.u32 + 48 + axis * 4));
}
extern "C" REX_FUNC(sub_821FF4A0) {
  CHECK_EQ(ctx.r3.u32, kCamera);
  Float(ctx.r5.u32, Float(ctx.r4.u32));
  Float(ctx.r5.u32 + 4, Float(ctx.r4.u32 + 8));
}

int main() {
  using namespace rb_blitz::test;
  using namespace rb_blitz::input;
  using namespace rb_blitz::input::touch;
  constexpr size_t size = 0x83000000;
#if defined(_WIN32)
  memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS));
#else
  memory = static_cast<uint8_t*>(mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
  if (memory == MAP_FAILED) memory = nullptr;
#endif
  CHECK_TRUE(memory != nullptr);
  if (!memory) return Finish();
  for (uint32_t region : {kHeap, kTable, uint32_t(0x82840000)}) {
#if defined(_WIN32)
    CHECK_TRUE(VirtualAlloc(memory + region, 0x10000, MEM_COMMIT, PAGE_READWRITE) != nullptr);
#else
    CHECK_EQ(mprotect(memory + region, 0x10000, PROT_READ | PROT_WRITE), 0);
#endif
  }
  BeginCase("nested native icon publishes a click target after static vertices are discarded");
  for (bool packed : {false, true}) {
    Fixture(packed);
    Upload();
    CHECK_EQ(Word(kMesh + 256), uint32_t(0));
    CHECK_EQ(Word(kMesh + 380), uint32_t(0));
    Capture();
    CHECK_EQ(publications, 1u);
    Close(published.x, .42f); Close(published.y, .82f);
    Close(published.width, .16f); Close(published.height, .08f);
    // Route a click through the captured production bounds and native action queue.
    PowerupTarget target;
    target.SetTarget(published, true);
    CHECK_TRUE(target.Observe({UINT64_MAX, 0, Phase::kDown, .5f, .86f}).consumed);
    const auto release = target.Observe({UINT64_MAX, 0, Phase::kUp, .5f, .86f});
    CHECK_TRUE(release.consumed && release.deploy);
    Gestures gestures;
    gestures.SetActive(true); gestures.SetPowerupAvailable(true);
    gestures.DrainGameplay(); // Discard the activation boundary before the click.
    if (release.deploy) gestures.DeployPowerup();
    const auto events = gestures.DrainGameplay();
    CHECK_EQ(events.size(), size_t(2));
    CHECK_TRUE(events[0].kind == GameplayKind::kPress && events[0].action == Action::kPowerup);
    CHECK_TRUE(events[1].kind == GameplayKind::kRelease && events[1].action == Action::kPowerup);
  }

  BeginCase("native bounds follow the current transform and clip at the guest frame");
  Fixture(true); Upload();
  memory[kMesh + 253] = 1;
  Float(kMesh + 136 + 48, .98f);
  Capture();
  CHECK_EQ(resolves, 1u); CHECK_EQ(publications, 1u);
  Close(published.x, .9f); Close(published.width, .1f);
  Float(kMesh + 136 + 48, 2.f);
  Capture(); CHECK_EQ(publications, 1u);
  Float(kMesh + 136 + 48, std::numeric_limits<float>::quiet_NaN());
  Capture(); CHECK_EQ(publications, 1u);

  BeginCase("inactive gameplay and unavailable powerups never publish targets");
  Fixture(true); Upload();
  active = false; Capture(); CHECK_EQ(publications, 0u); CHECK_EQ(finds, 0u);
  active = true; ready = false; Capture(); CHECK_EQ(publications, 0u); CHECK_EQ(finds, 0u);

  BeginCase("reused GPU resources and unrelated meshes cannot reuse an icon target");
  Fixture(true); Upload();
  Word(kMesh + 412, 0x5678);
  Capture(); CHECK_EQ(publications, 0u);
  Fixture(true); Upload();
  Text(kHeap + 0x5000, "other.mesh");
  Capture(); CHECK_EQ(publications, 0u);
#if defined(_WIN32)
  VirtualFree(memory, 0, MEM_RELEASE);
#else
  munmap(memory, size);
#endif
  return Finish();
}
