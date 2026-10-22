// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/album_art_service.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "assets/texture_codec.h"
#include "store/album_art.h"
#include "store/store_http.h"
#include "store/store_probe.h"
#include "store/store_serve.h"
#include "store/store_settings.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <wincodec.h>

REXCVAR_DEFINE_BOOL(store_art_off, false, "Diagnostics",
                    "Serve no album art: the offer records carry no `preview_art`, so the retail "
                    "panel keeps its own placeholder art. Also stops the art worker from fetching "
                    "cover images. On by default because the art is what P3's exit asks for")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(store_art_dump_dir, "", "Diagnostics",
                      "The art worker's scratch directory. Every encoded cover is written there as "
                      "`<key>.png_xbox`, and a file already present for a key is **served instead** "
                      "of being fetched - so the exact bytes the panel receives can be decoded "
                      "offline (`python scripts/hmx_tex.py export <key>.png_xbox --out cover.png`), "
                      "and a shipped `.png_xbox` copied in is a measurement of the serving path "
                      "against a texture retail accepts. Empty does neither")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::store {
namespace {

// The image the API serves is a cover thumbnail; a 2 MiB ceiling is generous for a PNG and stops an
// error page (or a hostile redirect target that passed the URL rule) from becoming a memory
// problem. The decoder's own bound is separate: a 4096x4096 image is refused rather than resized.
constexpr std::size_t kMaxArtBytes = 2 * 1024 * 1024;
constexpr UINT kMaxArtDimension = 4096;

// A page is 24 offers; the queue holds several pages' worth so a burst of "Download Again" rows
// cannot grow it without bound. Beyond that the extra keys are simply dropped - the next visit to
// the page enqueues them again.
constexpr std::size_t kMaxQueuedArt = 64;

// Why the last fetch or decode failed, for the boot log and the diagnostics. Guarded by the same
// mutex as the queue.
std::mutex g_mutex;
std::condition_variable g_wake;
// `store_art_dump_dir`, read once at install and guarded by `g_mutex` (the worker reads it too).
std::string g_dump_dir;
std::deque<std::string> g_queue;
std::set<std::string> g_pending;  // queued or in flight, so one key is fetched once
bool g_quit = false;
bool g_started = false;
bool g_off = false;
std::string g_last_error;

enum class State { kOff, kIdle, kQueued, kFetching, kFailed };
std::atomic<State> g_state{State::kOff};
std::atomic<std::size_t> g_encoded{0};

// A COM pointer without pulling in WRL: the worker owns a handful of these for one call each.
template <class T>
class ComPtr {
 public:
  ComPtr() = default;
  ~ComPtr() { Reset(); }
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;

  T** Put() {
    Reset();
    return &pointer_;
  }
  T* Get() const { return pointer_; }
  T* operator->() const { return pointer_; }
  explicit operator bool() const { return pointer_ != nullptr; }

  void Reset() {
    if (pointer_ != nullptr) {
      pointer_->Release();
      pointer_ = nullptr;
    }
  }

 private:
  T* pointer_ = nullptr;
};

// WIC -> straight RGBA8, top-down. `why` names the step that refused the body, because "the art did
// not decode" is not actionable on its own: an HTML error page and a JPEG the decoder cannot read
// are the same sentence otherwise.
bool DecodeImageToRgba(const std::string& bytes, std::vector<std::uint8_t>* rgba,
                       std::uint32_t* width, std::uint32_t* height, std::string* why) {
  ComPtr<IStream> stream;
  if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.Put()))) {
    *why = "no memory stream";
    return false;
  }
  ULONG written = 0;
  if (FAILED(stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written)) ||
      written != bytes.size()) {
    *why = "the body could not be buffered";
    return false;
  }
  LARGE_INTEGER origin{};
  origin.QuadPart = 0;
  stream->Seek(origin, STREAM_SEEK_SET, nullptr);

  ComPtr<IWICImagingFactory> factory;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(factory.Put())))) {
    *why = "Windows Imaging Component is unavailable";
    return false;
  }
  ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                              WICDecodeMetadataCacheOnDemand, decoder.Put()))) {
    *why = "the body is not an image this system can decode";
    return false;
  }
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, frame.Put()))) {
    *why = "the image has no frame";
    return false;
  }
  UINT frame_width = 0;
  UINT frame_height = 0;
  if (FAILED(frame->GetSize(&frame_width, &frame_height)) || frame_width == 0 ||
      frame_height == 0) {
    *why = "the image reports no size";
    return false;
  }
  if (frame_width > kMaxArtDimension || frame_height > kMaxArtDimension) {
    *why = "the image is larger than " + std::to_string(kMaxArtDimension) + " pixels a side";
    return false;
  }
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(factory->CreateFormatConverter(converter.Put())) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
    *why = "the image could not be converted to RGBA";
    return false;
  }
  rgba->assign(std::size_t(frame_width) * frame_height * 4, 0);
  if (FAILED(converter->CopyPixels(nullptr, frame_width * 4,
                                   static_cast<UINT>(rgba->size()), rgba->data()))) {
    *why = "the image pixels could not be read";
    return false;
  }
  *width = frame_width;
  *height = frame_height;
  return true;
}

// The per-file id the envelope carries. Opaque to the engine (it identifies the asset), so any
// stable value works; FNV-1a 32 over the key keeps a boot's textures distinguishable in a capture
// without hashing the pixels.
std::uint32_t TextureIdForKey(const std::string& key) {
  std::uint32_t hash = 2166136261u;
  for (const char c : key) {
    hash ^= static_cast<std::uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}

void RememberError(const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last_error = message;
  }
  g_state.store(State::kFailed, std::memory_order_relaxed);
}

// One fetch, decode, encode. Runs on the worker thread with COM initialized for it.
void ProcessKey(const std::string& key) {
  if (HasArtTexture(key)) {
    return;
  }
  const SettingsSnapshot settings = Settings();
  if (settings && !settings->enabled) {
    return;  // the master switch went off while this key waited
  }
  // A file already in the dump directory is taken as this key's texture: a cover encoded in an
  // earlier boot, a hand-made one, or a **shipped** `.png_xbox` copied in to measure the serving
  // path against a texture the retail loader is known to accept (`store_art_dump_dir`).
  std::string dump_dir;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    dump_dir = g_dump_dir;
  }
  if (!dump_dir.empty()) {
    std::ifstream override_file(std::filesystem::path(dump_dir) / (key + ".png_xbox"),
                                std::ios::binary);
    if (override_file) {
      const std::string bytes((std::istreambuf_iterator<char>(override_file)),
                              std::istreambuf_iterator<char>());
      if (!bytes.empty()) {
        PublishArtTexture(key, bytes);
        g_encoded.fetch_add(1, std::memory_order_relaxed);
        g_state.store(State::kIdle, std::memory_order_relaxed);
        REXLOG_INFO("album_art: {} -> {} byte(s) read from {}\\{}.png_xbox", key, bytes.size(),
                    dump_dir, key);
        return;
      }
    }
  }
  const std::string url = ArtUrlForKey(key);
  if (url.empty()) {
    RememberError(key + ": no art URL was published");
    return;
  }
  g_state.store(State::kFetching, std::memory_order_relaxed);
  HttpOptions options;
  options.receive_timeout_ms = 15000;
  options.max_body_bytes = kMaxArtBytes;
  const HttpResponse response = Get(url, {}, options);
  if (!response.ok) {
    RememberError(key + ": " + (response.error.empty() ? "the request failed" : response.error));
    REXLOG_WARN("album_art: {} could not be fetched: {}", key,
                response.error.empty() ? "the request failed" : response.error);
    return;
  }
  if (response.status != 200) {
    RememberError(key + ": the artwork server answered HTTP " + std::to_string(response.status));
    REXLOG_WARN("album_art: {} answered HTTP {}", key, response.status);
    return;
  }
  if (response.body.empty()) {
    RememberError(key + ": the artwork answer was empty");
    return;
  }
  std::vector<std::uint8_t> rgba;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::string why;
  if (!DecodeImageToRgba(response.body, &rgba, &width, &height, &why)) {
    RememberError(key + ": " + why);
    REXLOG_WARN("album_art: {} could not be decoded: {}", key, why);
    return;
  }
  std::vector<std::uint8_t> scaled(std::size_t(kAlbumArtTextureSize) * kAlbumArtTextureSize * 4);
  assets::ResizeRgba(rgba.data(), width, height, scaled.data(), kAlbumArtTextureSize,
                     kAlbumArtTextureSize);
  // DXT1, not DXT5: a cover is opaque, and measured 2026-10-21 the engine draws a shipped DXT1
  // texture through this very path correctly while our DXT5 is drawn as garbage even though the
  // same bytes decode to the exact source image offline (`store_art_dump_dir`, Pillow BC3). Half the
  // bytes for the same picture, and the format retail's own `default_album_art` uses.
  const std::string texture =
      assets::BuildPlatformTexture(scaled.data(), kAlbumArtTextureSize, kAlbumArtTextureSize,
                                   assets::TextureFormat::kDxt1, TextureIdForKey(key),
                                   /*swap_words=*/true);
  if (texture.empty()) {
    RememberError(key + ": the texture could not be encoded");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last_error.clear();
  }
  PublishArtTexture(key, texture);
  g_encoded.fetch_add(1, std::memory_order_relaxed);
  g_state.store(State::kIdle, std::memory_order_relaxed);
  {  // The bytes the panel is handed, for `store_art_dump_dir`.
    std::string dump_dir;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      dump_dir = g_dump_dir;
    }
    if (!dump_dir.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(dump_dir, ec);
      std::ofstream file(std::filesystem::path(dump_dir) / (key + ".png_xbox"),
                         std::ios::binary | std::ios::trunc);
      if (file) {
        file.write(texture.data(), static_cast<std::streamsize>(texture.size()));
      }
    }
  }
  REXLOG_INFO("album_art: {} -> {}x{} from {}x{} ({} byte(s))", key, kAlbumArtTextureSize,
              kAlbumArtTextureSize, width, height, texture.size());
}

void WorkerMain() {
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  for (;;) {
    std::string key;
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_wake.wait(lock, [] { return g_quit || !g_queue.empty(); });
      if (g_queue.empty()) {
        if (g_quit) break;
        continue;
      }
      key = std::move(g_queue.front());
      g_queue.pop_front();
    }
    ProcessKey(key);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_pending.erase(key);
      if (!g_quit && g_state.load(std::memory_order_relaxed) != State::kFailed) {
        g_state.store(g_queue.empty() ? State::kIdle : State::kQueued,
                      std::memory_order_relaxed);
      }
    }
  }
  if (SUCCEEDED(com)) {
    CoUninitialize();
  }
}

void Enqueue(std::string key) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_off || g_quit || !g_started) return;
    if (g_pending.size() >= kMaxQueuedArt) return;
    if (!g_pending.insert(key).second) return;
    g_queue.push_back(std::move(key));
  }
  g_state.store(State::kQueued, std::memory_order_relaxed);
  g_wake.notify_one();
}

// The worker's lifetime, the same shape the other store services use: a function-local static so
// the destructor runs at exit and joins, instead of a global `std::thread` whose destructor would
// terminate the process. A fetch in flight delays that exit by at most the transport's receive
// timeout (15 s), the same bound P5's download service documents.
struct WorkerThread {
  WorkerThread() : thread(WorkerMain) {}
  ~WorkerThread() {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_quit = true;
    }
    g_wake.notify_all();
    if (thread.joinable()) {
      thread.join();
    }
  }
  std::thread thread;
};

}  // namespace

void InstallAlbumArt() {
  // The serving policy tells this module which offers a page carries, so a page the player opens
  // right after it is produced still starts its covers (store_serve.h's `SetArtPrefetch`); the
  // guest's own art request asks for a single key through the other seam (album_art.h).
  SetArtPrefetch([](const std::vector<Offer>& offers) { RequestAlbumArt(offers); });
  SetArtTextureRequest([](std::string_view key) { RequestAlbumArtTexture(key); });
  const bool off = REXCVAR_GET(store_art_off);
  SetAlbumArtEnabled(!off);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dump_dir = REXCVAR_GET(store_art_dump_dir);
  }
  if (off) {
    g_off = true;
    g_state.store(State::kOff, std::memory_order_relaxed);
    REXLOG_INFO("album_art: off (store_art_off); offers carry no preview art");
    return;
  }
  {
    static WorkerThread worker;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_started = true;
  }
  g_state.store(State::kIdle, std::memory_order_relaxed);
  REXLOG_INFO("album_art: the cover worker is up; textures are {}x{} DXT5 (.png_xbox)",
              kAlbumArtTextureSize, kAlbumArtTextureSize);
}

void RequestAlbumArt(const std::vector<Offer>& offers) {
  if (g_off) return;
  for (const Offer& offer : offers) {
    if (offer.preview_art.empty() || offer.art_url.empty()) continue;
    const std::string key = ArtKeyFromPath(offer.preview_art);
    if (key.empty()) continue;
    const ResolvedUrl url = ResolveAlbumArtUrl(offer.art_url);
    if (!url.ok) continue;  // the field is not a RhythmVerse album-art path (plan §3.1)
    PublishArtUrl(key, url.url);
    if (TraceEnabled()) {
      // The pair, because a cover that does not match its row is otherwise indistinguishable from a
      // codec fault: this line says which URL the key was given (boot `rb_blitz_177.log`).
      REXLOG_INFO("album_art: {} <- {} ({})", key, url.url, offer.name);
    }
    Enqueue(key);
  }
}

void RequestAlbumArtTexture(std::string_view key) {
  if (g_off || key.empty()) return;
  if (HasArtTexture(key)) return;
  if (ArtUrlForKey(key).empty()) return;
  Enqueue(std::string(key));
}

std::string_view AlbumArtStateToken() {
  switch (g_state.load(std::memory_order_relaxed)) {
    case State::kOff:
      return "off";
    case State::kQueued:
      return "queued";
    case State::kFetching:
      return "fetching";
    case State::kFailed:
      return "failed";
    case State::kIdle:
    default:
      return "idle";
  }
}

std::size_t AlbumArtEncodedCount() { return g_encoded.load(std::memory_order_relaxed); }

std::string AlbumArtLastError() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_last_error;
}

}  // namespace rb_blitz::store
