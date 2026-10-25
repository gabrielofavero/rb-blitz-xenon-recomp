#include "graphics/shader_coverage.h"
#include "graphics/preparation_assets.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>
#include <xxhash.h>
#include "util/sha256.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rb_blitz::graphics {
namespace {
using Json = nlohmann::json;
constexpr uintmax_t kMaxBytes = 64 * 1024 * 1024;
constexpr uintmax_t kMaxStoreBytes = 12 + 32768 * 72;
struct PathSpec { const char* name; const char* file; uint32_t api, version; size_t size; };
constexpr PathSpec kPaths[] = {
    {"d3d12/rtv", "5841122D.rtv.d3d12.xpso", 0x54525844, kD3D12RecipeStorageVersion, 64},
    {"d3d12/rov", "5841122D.rov.d3d12.xpso", 0x4f525844, kD3D12RecipeStorageVersion, 64},
    {"vulkan/fbo", "5841122D.fbo.vk.xpso", 0, kVulkanRecipeStorageVersion, 60},
    {"vulkan/fsi", "5841122D.fsi.vk.xpso", 1, kVulkanRecipeStorageVersion, 60}};
uint64_t LE(std::span<const uint8_t> data, size_t at, size_t count) {
  uint64_t value = 0;
  for (size_t i = 0; i < count; ++i) value |= uint64_t(data[at+i]) << (i*8);
  return value;
}
std::string Hex(uint64_t value) {
  char text[17];
  std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
  return text;
}
std::string RawHex(std::span<const uint8_t> raw) {
  constexpr char digits[] = "0123456789abcdef";
  std::string text;
  for (auto b : raw) { text += digits[b>>4]; text += digits[b&15]; }
  return text;
}
const PathSpec* Spec(const std::string& name) {
  for (const auto& spec : kPaths) if (name == spec.name) return &spec;
  return nullptr;
}
bool ReadStore(const std::filesystem::path& file, std::span<uint8_t> bytes) {
#ifdef _WIN32
  // Preparation may atomically replace a store while this passive reader is
  // open. The CRT's default sharing omits DELETE and can block that replacement.
  HANDLE handle = CreateFileW(file.c_str(),GENERIC_READ,
      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  DWORD read = 0;
  const bool ok = ReadFile(handle,bytes.data(),DWORD(bytes.size()),&read,nullptr) &&
                  read == bytes.size();
  CloseHandle(handle);
  return ok;
#else
  std::ifstream in(file,std::ios::binary);
  return bool(in.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));
#endif
}
std::string Key(const std::string& path, uint32_t version, const std::string& hash) {
  return path + ":" + std::to_string(version) + ":" + hash;
}
// Journals are evidence, not trusted cache input. Deduplicate only entries whose
// path, generation, exact description size and XXH3 identity validate again.
bool ValidEntry(const Json& j) {
  try {
    if (j.at("schema") != 1 || j.at("title_id") != "5841122D") return false;
    const auto* spec = Spec(j.at("path").get<std::string>());
    if (!spec || j.at("version") != spec->version) return false;
    const auto raw = j.at("raw").get<std::string>();
    if (raw.size() != spec->size*2 || raw.find_first_not_of("0123456789abcdef") != std::string::npos)
      return false;
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < raw.size(); i += 2)
      bytes.push_back(uint8_t(std::stoul(raw.substr(i,2),nullptr,16)));
    return Hex(XXH3_64bits(bytes.data(),bytes.size())) == j.at("hash").get<std::string>();
  } catch (const std::exception&) { return false; }
}
}  // namespace

std::string DevelopmentDirectoryIdentity(const std::filesystem::path& directory) {
  std::error_code error;
  const auto canonical = std::filesystem::canonical(directory,error);
  if (error) return {};
  auto text = canonical.generic_string();
#ifdef _WIN32
  for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
#endif
  return util::HashHex(text);
}

bool IsDevelopmentExecutable(const std::filesystem::path& executable,
                             const std::filesystem::path& build_directory) {
  std::error_code error;
  return std::filesystem::equivalent(executable.parent_path(),build_directory,error) && !error;
}

struct ShaderCoverageJournal::State {
  std::filesystem::path cache, journal;
  std::map<std::string,std::string> covered, seen;
  std::set<std::string> sources;
  std::map<std::string,std::pair<uintmax_t,std::filesystem::file_time_type>> stamps;
  std::string catalogue_hash;
  bool needs_newline = false;
};

ShaderCoverageJournal::ShaderCoverageJournal(std::string catalogue, std::filesystem::path cache,
                                           std::filesystem::path journal)
    : state_(std::make_unique<State>()) {
  auto& s = *state_;
  s.cache = std::move(cache); s.journal = std::move(journal);
  s.catalogue_hash = Hex(XXH3_64bits(catalogue.data(),catalogue.size()));
  const auto manifest = Json::parse(catalogue);
  if (manifest.at("schema") != 1 || manifest.at("title_id") != "5841122D")
    throw std::runtime_error("unsupported coverage catalogue");
  for (const auto& [path, recipes] : manifest.at("paths").items()) {
    const auto* spec = Spec(path);
    if (!spec) continue;
    for (const auto& recipe : recipes)
      s.covered.emplace(Key(path,spec->version,recipe.at("hash")),recipe.at("raw"));
  }
  for (const auto& source : manifest.at("sources"))
    s.sources.insert(source.at("stage").get<std::string>()+":"+source.at("hash").get<std::string>());
  std::error_code error;
  if (!std::filesystem::exists(s.journal,error)) return;
  if (std::filesystem::file_size(s.journal) > kMaxBytes)
    throw std::runtime_error("coverage journal exceeds 64 MiB; archive it before collecting more");
  std::ifstream in(s.journal,std::ios::binary);
  if (!in) throw std::runtime_error("cannot read coverage journal");
  std::string line;
  while (std::getline(in,line)) {
    s.needs_newline = in.eof();
    const auto entry = Json::parse(line,nullptr,false);
    if (entry.is_discarded() || !ValidEntry(entry)) continue;
    s.seen.emplace(Key(entry.at("path"),entry.at("version"),entry.at("hash")),entry.at("raw"));
  }
}
ShaderCoverageJournal::~ShaderCoverageJournal() = default;

size_t ShaderCoverageJournal::Scan() {
  auto& s = *state_;
  size_t added = 0;
  for (const auto& spec : kPaths) {
    const auto file = s.cache/"shaders"/"shareable"/spec.file;
    std::error_code error;
    const auto size = std::filesystem::file_size(file,error);
    if (error || size < 12 || size > kMaxStoreBytes) continue;
    const auto time = std::filesystem::last_write_time(file,error);
    if (error) continue;
    const auto stamp = std::pair(size,time);
    if (s.stamps.contains(spec.name) && s.stamps.at(spec.name) == stamp) continue;
    std::vector<uint8_t> bytes(size);
    if (!ReadStore(file,bytes)) continue;
    if (LE(bytes,0,4) != 0x53504558 || LE(bytes,4,4) != spec.api) continue;
    uint32_t version = 0;
    for (size_t i = 8; i < 12; ++i) version = (version<<8)|bytes[i];
    if (version != spec.version) continue;
    bool complete = (size-12) % (spec.size+8) == 0;
    // A concurrent append may end partway through a record. Consume only the
    // verified prefix and retry an incomplete/corrupt tail on the next scan.
    for (size_t at = 12; at+8+spec.size <= bytes.size(); at += 8+spec.size) {
      const auto raw = std::span<const uint8_t>(bytes).subspan(at+8,spec.size);
      const auto hash_value = LE(bytes,at,8);
      if (XXH3_64bits(raw.data(),raw.size()) != hash_value) { complete = false; break; }
      const auto hash = Hex(hash_value), text = RawHex(raw);
      const auto key = Key(spec.name,version,hash);
      const auto covered = s.covered.find(key), seen = s.seen.find(key);
      if (covered != s.covered.end() || seen != s.seen.end()) {
        const auto& previous = covered != s.covered.end() ? covered->second : seen->second;
        if (previous != text) throw std::runtime_error("coverage recipe hash collision");
        continue;
      }
      const auto vertex = Hex(LE(raw,0,8)), pixel = Hex(LE(raw,16,8));
      Json unresolved = Json::array();
      if (!s.sources.contains("vertex:"+vertex)) unresolved.push_back("vertex:"+vertex);
      if (pixel != "0000000000000000" && !s.sources.contains("pixel:"+pixel))
        unresolved.push_back("pixel:"+pixel);
      const auto now = std::chrono::system_clock::now().time_since_epoch();
      Json entry{{"schema",1},{"title_id","5841122D"},{"path",spec.name},
                 {"version",version},{"hash",hash},{"raw",text},{"vertex",vertex},
                 {"pixel",pixel},{"unresolved_sources",unresolved},{"catalogue",s.catalogue_hash},
                 {"observed_unix_ms",std::chrono::duration_cast<std::chrono::milliseconds>(now).count()},
                 {"store",file.generic_string()}};
      std::filesystem::create_directories(s.journal.parent_path());
      std::ofstream out(s.journal,std::ios::binary|std::ios::app);
      if (!out) throw std::runtime_error("cannot append coverage journal");
      if (s.needs_newline) out << '\n';
      out << entry.dump() << '\n';
      out.flush();
      if (!out) throw std::runtime_error("cannot flush coverage journal");
      s.needs_newline = false;
      s.seen.emplace(key,text);  // Only suppress after a successful append.
      ++added;
    }
    if (complete) s.stamps[spec.name] = stamp;
  }
  return added;
}

CoverageRecorder::CoverageRecorder(std::string catalogue, std::filesystem::path cache,
                                   std::filesystem::path journal,
                                   std::function<void(const std::string&)> report)
    : worker_([catalogue=std::move(catalogue),cache=std::move(cache),journal=std::move(journal),
               report=std::move(report)](std::stop_token stop) {
#ifdef _WIN32
        // Background mode lowers I/O and memory priority as well as CPU priority.
        if (!SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN))
          SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_LOWEST);
#endif
        try {
          ShaderCoverageJournal collector(catalogue,cache,journal);
          report("collecting unprepared recipes in " + journal.string());
          std::mutex mutex;
          std::condition_variable_any changed;
          std::unique_lock lock(mutex);
          do {
            const auto count = collector.Scan();
            if (count) report("appended " + std::to_string(count) + " uncovered recipe(s)");
            if (stop.stop_requested()) break;
            changed.wait_for(lock,stop,std::chrono::seconds(5),[] { return false; });
            // One final scan on orderly shutdown; a last buffered SDK write
            // occurring later is recovered from the store on the next launch.
          } while (true);
        } catch (const std::exception& error) {
          report(std::string("collection stopped: ")+error.what());
        }
      }) {}
}  // namespace rb_blitz::graphics
