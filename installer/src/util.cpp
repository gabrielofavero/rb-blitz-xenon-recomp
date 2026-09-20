// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "util.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>

namespace rb_blitz::installer {
namespace {

// Device names are case-insensitive on Windows, so the list is compared against
// a lowercased component.
constexpr char kReservedNames[] =
    "con prn aux nul com1 com2 com3 com4 com5 com6 com7 com8 com9 "
    "lpt1 lpt2 lpt3 lpt4 lpt5 lpt6 lpt7 lpt8 lpt9";

bool IsReservedName(const std::string& component) {
  const std::size_t dot = component.find('.');
  const std::string base = Lower(dot == std::string::npos ? component : component.substr(0, dot));
  std::istringstream stream(kReservedNames);
  std::string name;
  while (stream >> name) {
    if (base == name) {
      return true;
    }
  }
  return false;
}

class LogState {
 public:
  std::mutex mutex;
  std::filesystem::path path;
  std::string contents;
  bool opened = false;
};

LogState& State() {
  static LogState state;
  return state;
}

std::string Timestamp() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  return S('[', now.wHour < 10 ? "0" : "", now.wHour, ':', now.wMinute < 10 ? "0" : "",
           now.wMinute, ':', now.wSecond < 10 ? "0" : "", now.wSecond, '.',
           now.wMilliseconds < 100 ? (now.wMilliseconds < 10 ? "00" : "0") : "", now.wMilliseconds,
           ']');
}

void Emit(std::string_view level, std::string_view message) {
  const std::string line = S(Timestamp(), ' ', level, ' ', message);
  std::fflush(stdout);
  std::fputs(line.c_str(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);

  LogState& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.contents += line;
  state.contents += '\n';
  if (!state.opened) {
    return;
  }
  std::FILE* file = _wfopen(state.path.c_str(), L"ab");
  if (file == nullptr) {
    return;
  }
  std::fwrite(line.data(), 1, line.size(), file);
  std::fputc('\n', file);
  std::fclose(file);
}

}  // namespace

// --- formatting -----------------------------------------------------------

std::string HumanBytes(std::uint64_t bytes) {
  constexpr std::uint64_t kKiB = 1024ull;
  constexpr std::uint64_t kMiB = 1024ull * 1024;
  constexpr std::uint64_t kGiB = 1024ull * 1024 * 1024;
  if (bytes < kKiB) {
    return S(bytes, bytes == 1 ? " byte" : " bytes");
  }
  const auto scaled = [](double value) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(value < 10.0 ? 2 : 1);
    stream << value;
    return stream.str();
  };
  if (bytes < kMiB) {
    return S(scaled(static_cast<double>(bytes) / static_cast<double>(kKiB)), " KiB");
  }
  if (bytes < kGiB) {
    return S(scaled(static_cast<double>(bytes) / static_cast<double>(kMiB)), " MB");
  }
  return S(scaled(static_cast<double>(bytes) / static_cast<double>(kGiB)), " GB");
}

std::string ToHex(const std::uint8_t* data, std::size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (std::size_t index = 0; index < size; ++index) {
    out.push_back(kHex[data[index] >> 4]);
    out.push_back(kHex[data[index] & 0x0F]);
  }
  return out;
}

std::string HexU32(std::uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
  return buffer;
}

std::string Lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  return out;
}

bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    const auto a = static_cast<unsigned char>(left[index]);
    const auto b = static_cast<unsigned char>(right[index]);
    if (std::tolower(a) != std::tolower(b)) {
      return false;
    }
  }
  return true;
}

bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}

bool ContainsIgnoreCase(std::string_view text, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  if (text.size() < needle.size()) {
    return false;
  }
  const std::string lowered_text = Lower(text);
  const std::string lowered_needle = Lower(needle);
  return lowered_text.find(lowered_needle) != std::string::npos;
}

std::string Trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  const auto is_space = [](char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' ||
           value == '\v';
  };
  while (begin < end && is_space(text[begin])) {
    ++begin;
  }
  while (end > begin && is_space(text[end - 1])) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string line = Trim(text.substr(start, end - start));
    lines.push_back(std::move(line));
    if (end == text.size()) {
      break;
    }
    start = end + 1;
  }
  return lines;
}

std::string Join(const std::vector<std::string>& parts, std::string_view separator) {
  std::string out;
  for (std::size_t index = 0; index < parts.size(); ++index) {
    if (index != 0) {
      out += separator;
    }
    out += parts[index];
  }
  return out;
}

std::string FormatWin32Error(unsigned long code) {
  LPWSTR buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0,
      nullptr);
  std::string message;
  if (length != 0 && buffer != nullptr) {
    message = Trim(Narrow(std::wstring_view(buffer, length)));
    LocalFree(buffer);
  }
  if (message.empty()) {
    message = S("error ", code);
  } else {
    message = S(message, " (", code, ')');
  }
  return message;
}

std::string LastWin32Error() { return FormatWin32Error(GetLastError()); }

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

// --- Windows helpers ------------------------------------------------------

std::wstring Widen(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                         nullptr, 0);
  if (length <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
  return wide;
}

std::string Narrow(std::wstring_view wide) {
  if (wide.empty()) {
    return {};
  }
  const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (length <= 0) {
    return {};
  }
  std::string utf8(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length,
                      nullptr, nullptr);
  return utf8;
}

void EnableUtf8Console() { SetConsoleOutputCP(CP_UTF8); }

std::optional<std::filesystem::path> EnvironmentPath(std::wstring_view name) {
  const std::wstring key(name);
  const DWORD needed = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
  if (needed == 0) {
    return std::nullopt;
  }
  std::wstring value(needed, L'\0');
  const DWORD written = GetEnvironmentVariableW(key.c_str(), value.data(), needed);
  if (written == 0) {
    return std::nullopt;
  }
  value.resize(written);
  if (value.empty()) {
    return std::nullopt;
  }
  return std::filesystem::path(value);
}

std::uint32_t WindowsBuildNumber() {
  // RTL_OSVERSIONINFOW, declared here so this file does not need winternl.h and
  // whatever the local SDK version spells it as.
  struct OsVersionInfo {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platform;
    WCHAR service_pack[128];
  };
  using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);

  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll == nullptr) {
    return 0;
  }
  const auto get_version = reinterpret_cast<RtlGetVersionFn>(
      reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
  if (get_version == nullptr) {
    return 0;
  }
  OsVersionInfo info{};
  info.size = sizeof(info);
  if (get_version(&info) != 0) {
    return 0;
  }
  return info.build;
}

std::uint64_t FreeBytesAvailable(const std::filesystem::path& path) {
  ULARGE_INTEGER available{};
  ULARGE_INTEGER total{};
  ULARGE_INTEGER free_total{};
  if (!GetDiskFreeSpaceExW(path.c_str(), &available, &total, &free_total)) {
    return 0;
  }
  return available.QuadPart;
}

std::uint64_t TotalBytes(const std::filesystem::path& path) {
  ULARGE_INTEGER available{};
  ULARGE_INTEGER total{};
  ULARGE_INTEGER free_total{};
  if (!GetDiskFreeSpaceExW(path.c_str(), &available, &total, &free_total)) {
    return 0;
  }
  return total.QuadPart;
}

std::string MachineArchitecture() {
  SYSTEM_INFO info{};
  GetNativeSystemInfo(&info);
  switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
      return "x64";
    case PROCESSOR_ARCHITECTURE_INTEL:
      return "x86";
    case PROCESSOR_ARCHITECTURE_ARM64:
      return "arm64";
    default:
      return S("unknown (", info.wProcessorArchitecture, ')');
  }
}

// --- file primitives ------------------------------------------------------

bool ReadFileBytes(const std::filesystem::path& path, Bytes* out, std::string* error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    SetError(error, S("cannot open ", Narrow(path.native()), ": ", LastWin32Error()));
    return false;
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  stream.seekg(0, std::ios::beg);
  out->assign(static_cast<std::size_t>(size > 0 ? size : 0), 0);
  if (size > 0 && !stream.read(reinterpret_cast<char*>(out->data()), size)) {
    SetError(error, S("cannot read ", Narrow(path.native()), " (expected ", size, " bytes)"));
    return false;
  }
  return true;
}

bool ReadFileText(const std::filesystem::path& path, std::string* out, std::string* error) {
  Bytes bytes;
  if (!ReadFileBytes(path, &bytes, error)) {
    return false;
  }
  out->assign(bytes.begin(), bytes.end());
  return true;
}

bool WriteFileBytes(const std::filesystem::path& path, const std::uint8_t* data, std::size_t size,
                    bool append, std::string* error) {
  if (!EnsureParentDirectory(path, error)) {
    return false;
  }
  std::ofstream stream(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
  if (!stream) {
    SetError(error, S("cannot open ", Narrow(path.native()), " for writing: ", LastWin32Error()));
    return false;
  }
  if (size != 0) {
    stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  }
  if (!stream) {
    SetError(error, S("cannot write ", Narrow(path.native())));
    return false;
  }
  return true;
}

bool WriteFileText(const std::filesystem::path& path, std::string_view text, bool append,
                   std::string* error) {
  return WriteFileBytes(path, reinterpret_cast<const std::uint8_t*>(text.data()), text.size(),
                        append, error);
}

bool EnsureDirectory(const std::filesystem::path& path, std::string* error) {
  if (path.empty()) {
    return true;
  }
  std::error_code code;
  if (std::filesystem::exists(path, code)) {
    if (std::filesystem::is_directory(path, code)) {
      return true;
    }
    SetError(error, S(Narrow(path.native()), " exists and is not a directory"));
    return false;
  }
  std::filesystem::create_directories(path, code);
  if (code) {
    SetError(error, S("cannot create ", Narrow(path.native()), ": ", code.message()));
    return false;
  }
  return true;
}

bool EnsureParentDirectory(const std::filesystem::path& path, std::string* error) {
  const std::filesystem::path parent = path.parent_path();
  if (parent.empty()) {
    return true;
  }
  return EnsureDirectory(parent, error);
}

bool RemoveFile(const std::filesystem::path& path, std::string* error) {
  std::error_code code;
  if (!std::filesystem::exists(path, code)) {
    return true;
  }
  std::filesystem::remove(path, code);
  if (code) {
    SetError(error, S("cannot delete ", Narrow(path.native()), ": ", code.message()));
    return false;
  }
  return true;
}

bool RemoveTree(const std::filesystem::path& path, std::string* error) {
  std::error_code code;
  if (!std::filesystem::exists(path, code)) {
    return true;
  }
  // A file copied from a dump can be read-only, and remove_all() cannot delete
  // read-only files on Windows. Clearing attributes first keeps "uninstall"
  // from failing on someone else's content.
  std::error_code walk_code;
  for (std::filesystem::recursive_directory_iterator iterator(
           path, std::filesystem::directory_options::skip_permission_denied, walk_code),
       end;
       iterator != end; iterator.increment(walk_code)) {
    std::error_code ignored;
    std::filesystem::permissions(iterator->path(), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, ignored);
  }
  std::filesystem::remove_all(path, code);
  if (code || std::filesystem::exists(path, walk_code)) {
    SetError(error, S("cannot delete ", Narrow(path.native()), ": ",
                      code ? code.message() : std::string("still present after delete")));
    return false;
  }
  return true;
}

bool FileExists(const std::filesystem::path& path) {
  std::error_code code;
  return std::filesystem::is_regular_file(path, code);
}

bool DirectoryExists(const std::filesystem::path& path) {
  std::error_code code;
  return std::filesystem::is_directory(path, code);
}

std::uint64_t FileSizeOrZero(const std::filesystem::path& path) {
  std::error_code code;
  const auto size = std::filesystem::file_size(path, code);
  return code ? 0 : static_cast<std::uint64_t>(size);
}

void WriteProgressFile(const std::filesystem::path& path, int percent) {
  WriteProgressFile(path, percent, {});
}

void WriteProgressFile(const std::filesystem::path& path, int percent, std::string_view detail) {
  if (path.empty()) {
    return;
  }
  std::string text = S(percent < 0 ? -1 : (percent > 100 ? 100 : percent));
  if (!detail.empty()) {
    text += S('\n', detail);
  }
  std::string error;
  WriteFileText(path, text, false, &error);
}

void WriteSummaryFile(const std::filesystem::path& path,
                      const std::vector<std::pair<std::string, std::string>>& pairs) {
  if (path.empty()) {
    return;
  }
  std::vector<std::string> parts;
  parts.reserve(pairs.size());
  for (const auto& [key, value] : pairs) {
    parts.push_back(S(key, '=', value));
  }
  std::string error;
  WriteFileText(path, Join(parts, ";"), false, &error);
}

std::optional<std::string> ReadSummaryValue(const std::string& text, std::string_view key) {
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find(';', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string_view pair(text.data() + start, end - start);
    const std::size_t equals = pair.find('=');
    if (equals != std::string_view::npos && pair.substr(0, equals) == key) {
      return std::string(pair.substr(equals + 1));
    }
    if (end == text.size()) {
      break;
    }
    start = end + 1;
  }
  return std::nullopt;
}

// --- path safety ----------------------------------------------------------

std::filesystem::path SafeRelativePath(std::string_view posix_relative, std::string* error) {
  const auto fail = [&](std::string_view reason) {
    SetError(error, S("unsafe path '", posix_relative, "': ", reason));
    return std::filesystem::path{};
  };
  if (posix_relative.empty()) {
    return fail("empty");
  }
  if (posix_relative.size() > 1024) {
    return fail("too long");
  }
  std::string normalised(posix_relative);
  std::replace(normalised.begin(), normalised.end(), '\\', '/');
  if (normalised.front() == '/' || normalised.find("//") != std::string::npos) {
    return fail("absolute");
  }
  if (normalised.size() >= 2 && normalised[1] == ':') {
    return fail("drive-qualified");
  }

  std::filesystem::path out;
  std::size_t start = 0;
  while (start <= normalised.size()) {
    std::size_t end = normalised.find('/', start);
    if (end == std::string::npos) {
      end = normalised.size();
    }
    const std::string component = normalised.substr(start, end - start);
    if (component.empty() || component == ".") {
      return fail("empty component");
    }
    if (component == "..") {
      return fail("parent traversal");
    }
    if (component.find(':') != std::string::npos) {
      return fail("alternate data stream");
    }
    if (component.back() == '.' || component.back() == ' ') {
      return fail("component ends with a dot or a space");
    }
    if (component.size() > 255) {
      return fail("component too long");
    }
    for (const char character : component) {
      const auto value = static_cast<unsigned char>(character);
      if (value < 0x20 || character == '<' || character == '>' || character == '"' ||
          character == '|' || character == '?' || character == '*') {
        return fail("forbidden character");
      }
    }
    if (IsReservedName(component)) {
      return fail("reserved device name");
    }
    out /= Widen(component);
    if (end == normalised.size()) {
      break;
    }
    start = end + 1;
  }
  // Also covers the wizard's 260-character MAX_PATH limit without long-path
  // opt-in, both for the staging path and for the final install location.
  if (out.native().size() > 240) {
    return fail("path too long");
  }
  return out;
}

bool PathIsWithin(const std::filesystem::path& root, const std::filesystem::path& candidate) {
  std::error_code code;
  const std::filesystem::path resolved_root = std::filesystem::weakly_canonical(root, code);
  if (code) {
    return false;
  }
  const std::filesystem::path resolved_candidate = std::filesystem::weakly_canonical(candidate, code);
  if (code) {
    return false;
  }
  auto root_part = resolved_root.begin();
  auto candidate_part = resolved_candidate.begin();
  for (; root_part != resolved_root.end(); ++root_part, ++candidate_part) {
    if (candidate_part == resolved_candidate.end()) {
      return false;
    }
    if (!EqualsIgnoreCase(Narrow(root_part->native()), Narrow(candidate_part->native()))) {
      return false;
    }
  }
  return true;
}

std::string PosixPath(const std::filesystem::path& path) {
  std::string text = Narrow(path.generic_wstring());
  return text;
}

// --- logging --------------------------------------------------------------

namespace log {

void Open(const std::filesystem::path& path) {
  LogState& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.path = path;
  state.opened = !path.empty();
  if (!state.opened) {
    return;
  }
  std::string error;
  EnsureParentDirectory(path, &error);
}

void Info(std::string_view message) { Emit("info:  ", message); }
void Detail(std::string_view message) { Emit("detail:", message); }
void Warn(std::string_view message) { Emit("warn:  ", message); }
void Error(std::string_view message) { Emit("error: ", message); }

std::string Contents() {
  LogState& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.contents;
}

}  // namespace log

}  // namespace rb_blitz::installer
