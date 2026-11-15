#include "check.h"
#include "diag/session_logs.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace rb_blitz::test;
namespace fs = std::filesystem;

int main() {
  BeginCase("two sessions of logs are retained beside other diagnostics");
  const fs::path root = fs::temp_directory_path() /
      ("rb-session-logs-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path dir = root / "diagnostics";
  CHECK_TRUE(rb_blitz::diag::PrepareSessionLog(root) == dir / "game.log");
  CHECK_TRUE(rb_blitz::diag::WriteSessionInfo(root, "first build", "first compiler", "game.exe"));
  { std::ofstream(dir / "game.log") << "first";
    std::ofstream(dir / "game.1.log") << "first rotated";
    std::ofstream(dir / "unmapped-shaders.jsonl") << "discovery"; }
  CHECK_TRUE(rb_blitz::diag::PrepareSessionLog(root) == dir / "game.log");
  CHECK_TRUE(fs::exists(dir / "previous.log"));
  CHECK_TRUE(fs::exists(dir / "previous-session-info.txt"));
  CHECK_FALSE(fs::exists(dir / "session-info.txt"));
  CHECK_TRUE(rb_blitz::diag::WriteSessionInfo(root, "second build", "second compiler", "new.exe"));
  CHECK_TRUE(fs::exists(dir / "previous.1.log"));
  { std::ofstream(dir / "game.log") << "second"; }
  CHECK_TRUE(rb_blitz::diag::PrepareSessionLog(root) == dir / "game.log");
  CHECK_FALSE(fs::exists(dir / "previous.1.log"));
  std::string previous, shader;
  { std::ifstream in(dir / "previous.log"); in >> previous; }
  { std::ifstream in(dir / "unmapped-shaders.jsonl"); in >> shader; }
  CHECK_TRUE(previous == "second");
  CHECK_TRUE(shader == "discovery");
  { std::ifstream input(dir / "previous-session-info.txt");
    std::string identity((std::istreambuf_iterator<char>(input)), {});
    CHECK_TRUE(identity.find("second build") != std::string::npos);
    CHECK_TRUE(identity.find("first build") == std::string::npos); }
  fs::remove_all(root);
  return Finish();
}
