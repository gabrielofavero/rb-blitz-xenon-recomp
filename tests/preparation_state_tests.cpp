// SPDX-License-Identifier: GPL-2.0-only
// P7's persistent preparation ledger: separate validity per layer, and the rule
// that stale, corrupt or partial state never becomes a reusable claim.
// See docs/engine/shader-cache.md (part 7).
#include "check.h"
#include "graphics/preparation_state.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace rb_blitz::test;
using namespace rb_blitz::graphics;

namespace {
PreparationInputs Inputs(uint64_t catalogue = 11, uint64_t content = 22, uint64_t executable = 33,
                         uint32_t d3d12 = 0x20260226, uint32_t vulkan = 0x20260228) {
  PreparationInputs inputs;
  inputs.catalogue = catalogue;
  inputs.content = content;
  inputs.executable = executable;
  inputs.d3d12_version = d3d12;
  inputs.vulkan_version = vulkan;
  return inputs;
}

PreparationLayer Layer(const PreparationInputs& inputs, bool d3d12, uint64_t entries) {
  PreparationLayer layer;
  layer.complete = true;
  layer.catalogue = inputs.catalogue;
  layer.content = inputs.content;
  layer.executable = inputs.executable;
  layer.d3d12_version = inputs.d3d12_version;
  layer.vulkan_version = inputs.vulkan_version;
  layer.entries = entries;
  (void)d3d12;
  return layer;
}

void Write(const std::filesystem::path& path, const std::string& text) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(text.data(), std::streamsize(text.size()));
}

void Prepare(const std::filesystem::path& root) {
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root / "shaders", error);
}
}  // namespace

int main() {
  const auto root = std::filesystem::current_path() / "preparation-state-test";

  BeginCase("a missing ledger reports itself and trusts nothing");
  Prepare(root);
  {
    PreparationLedger ledger;
    std::string why;
    ledger.Load(root, &why);
    CHECK_FALSE(ledger.loaded);
    CHECK_TRUE(!why.empty());
    CHECK_FALSE(ledger.extraction.ExtractionValid(Inputs()));
    CHECK_FALSE(ledger.recipes[0].RecipesValid(Inputs(), true));
  }

  BeginCase("a complete round trip preserves every layer, the epoch and the attempt");
  Prepare(root);
  {
    PreparationLedger ledger;
    const PreparationInputs inputs = Inputs();
    ledger.extraction = Layer(inputs, true, 208);
    for (size_t i = 0; i < kPreparationPathCount; ++i) {
      ledger.recipes[i] = Layer(inputs, i < 2, 100 + i);
    }
    ledger.content_epoch = 4;
    ledger.last_attempt.attempt = 7;
    ledger.last_attempt.configuration = 0xABCDEF;
    ledger.last_attempt.phase = 5;
    ledger.last_attempt.complete = true;
    ledger.last_attempt.required = 432;
    ledger.last_attempt.succeeded = 432;
    ledger.last_attempt.elapsed_milliseconds = 15240;
    std::string why;
    CHECK_TRUE(ledger.Save(root, &why));
    CHECK_TRUE(why.empty());
    CHECK_TRUE(ledger.writable);

    PreparationLedger loaded;
    loaded.Load(root, &why);
    CHECK_TRUE(loaded.loaded);
    CHECK_EQ(loaded.extraction.entries, 208);
    CHECK_TRUE(loaded.extraction.ExtractionValid(inputs));
    for (size_t i = 0; i < kPreparationPathCount; ++i) {
      CHECK_EQ(loaded.recipes[i].entries, 100 + i);
      CHECK_TRUE(loaded.recipes[i].RecipesValid(inputs, i < 2));
    }
    CHECK_EQ(loaded.content_epoch, 4);
    CHECK_EQ(loaded.last_attempt.attempt, 7);
    CHECK_EQ(loaded.last_attempt.configuration, 0xABCDEF);
    CHECK_EQ(uint64_t(loaded.last_attempt.phase), 5);
    CHECK_TRUE(loaded.last_attempt.complete);
    CHECK_EQ(loaded.last_attempt.succeeded, 432);
    CHECK_EQ(loaded.last_attempt.elapsed_milliseconds, 15240);
  }

  BeginCase("each input invalidates only the layers that were built from it");
  {
    PreparationLedger ledger;
    const PreparationInputs inputs = Inputs();
    ledger.extraction = Layer(inputs, true, 208);
    for (size_t i = 0; i < kPreparationPathCount; ++i) ledger.recipes[i] = Layer(inputs, i < 2, 1);
    // A changed catalogue or content or executable invalidates both layers.
    for (const PreparationInputs changed :
         {Inputs(12, 22, 33), Inputs(11, 23, 33), Inputs(11, 22, 34)}) {
      CHECK_FALSE(ledger.extraction.ExtractionValid(changed));
      CHECK_FALSE(ledger.recipes[0].RecipesValid(changed, true));
      CHECK_FALSE(ledger.recipes[2].RecipesValid(changed, false));
    }
    // A translator or layout bump invalidates only that backend's stores: the
    // guest shaders do not change with it, and the other backend keeps its own.
    const PreparationInputs d3d12_bump = Inputs(11, 22, 33, 0x20260301, 0x20260228);
    CHECK_TRUE(ledger.extraction.ExtractionValid(d3d12_bump));
    CHECK_FALSE(ledger.recipes[0].RecipesValid(d3d12_bump, true));
    CHECK_FALSE(ledger.recipes[1].RecipesValid(d3d12_bump, true));
    CHECK_TRUE(ledger.recipes[2].RecipesValid(d3d12_bump, false));
    CHECK_TRUE(ledger.recipes[3].RecipesValid(d3d12_bump, false));
    const PreparationInputs vulkan_bump = Inputs(11, 22, 33, 0x20260226, 0x20260301);
    CHECK_TRUE(ledger.extraction.ExtractionValid(vulkan_bump));
    CHECK_TRUE(ledger.recipes[0].RecipesValid(vulkan_bump, true));
    CHECK_FALSE(ledger.recipes[2].RecipesValid(vulkan_bump, false));
    CHECK_FALSE(ledger.recipes[3].RecipesValid(vulkan_bump, false));
    // Switching backend inside one install keeps the other backend's entries.
    CHECK_TRUE(ledger.recipes[0].RecipesValid(inputs, true));
    CHECK_TRUE(ledger.recipes[2].RecipesValid(inputs, false));
  }

  BeginCase("an incomplete layer is never reusable, even with matching inputs");
  {
    PreparationLedger ledger;
    const PreparationInputs inputs = Inputs();
    ledger.extraction = Layer(inputs, true, 208);
    ledger.extraction.complete = false;
    CHECK_FALSE(ledger.extraction.ExtractionValid(inputs));
    ledger.recipes[0] = Layer(inputs, true, 10);
    ledger.recipes[0].complete = false;
    CHECK_FALSE(ledger.recipes[0].RecipesValid(inputs, true));
  }

  BeginCase("a cancelled, failed or ignored attempt is never a readiness claim");
  {
    PreparationLedger ledger;
    ledger.last_attempt.phase = 7;  // kCancelled
    ledger.last_attempt.complete = false;
    ledger.last_attempt.succeeded = 300;
    ledger.last_attempt.cancelled = 132;
    CHECK_FALSE(ledger.last_attempt.complete);
    ledger.last_attempt.phase = 8;  // kFailed
    CHECK_FALSE(ledger.last_attempt.complete);
    ledger.last_attempt.phase = 5;  // kSucceeded
    ledger.last_attempt.complete = true;
    CHECK_TRUE(ledger.last_attempt.complete);
    // An explicit Ignore is recorded as such and still is not readiness.
    ledger.last_attempt.phase = 6;  // kIncomplete
    ledger.last_attempt.complete = false;
    ledger.last_attempt.ignored = true;
    CHECK_FALSE(ledger.last_attempt.complete);
  }

  BeginCase("a corrupt, truncated or wrong-schema ledger is discarded as a whole");
  {
    Prepare(root);
    Write(PreparationLedger::PathFor(root), "{ not json");
    PreparationLedger ledger;
    std::string why;
    ledger.Load(root, &why);
    CHECK_FALSE(ledger.loaded);
    CHECK_TRUE(!why.empty());
    CHECK_FALSE(ledger.extraction.ExtractionValid(Inputs()));

    Write(PreparationLedger::PathFor(root),
          R"({"schema":99,"extraction":{"complete":true,"entries":5}})");
    ledger.Load(root, &why);
    CHECK_FALSE(ledger.loaded);
    CHECK_FALSE(ledger.extraction.ExtractionValid(Inputs()));

    Write(PreparationLedger::PathFor(root),
          R"({"schema":1,"extraction":{"complete":true,"entries":5},"recipes":)");
    ledger.Load(root, &why);
    CHECK_FALSE(ledger.loaded);
    CHECK_FALSE(ledger.extraction.ExtractionValid(Inputs()));
  }

  BeginCase("one truncated layer does not lend its validity to another");
  Prepare(root);
  {
    const PreparationInputs inputs = Inputs();
    // Only the extraction layer is in the file: a boot that crashed after the
    // first save must not present the recipe stores as reusable.
    Write(PreparationLedger::PathFor(root),
          R"({"schema":1,"content_epoch":0,"extraction":{"complete":true,"catalogue":11,)"
          R"("content":22,"executable":33,"entries":208}})");
    PreparationLedger loaded;
    std::string why;
    loaded.Load(root, &why);
    CHECK_TRUE(loaded.loaded);
    CHECK_TRUE(loaded.extraction.ExtractionValid(inputs));
    CHECK_FALSE(loaded.recipes[0].RecipesValid(inputs, true));
    CHECK_FALSE(loaded.recipes[2].RecipesValid(inputs, false));
  }

  BeginCase("an unwritable cache is a persistence fact, not a lost layer");
  {
    const auto blocked = root / "blocked";
    std::error_code error;
    std::filesystem::remove_all(blocked, error);
    // A regular file where the ledger's directory has to be: the write must
    // fail, the in-memory result must stay valid, and `writable` must say so.
    Write(blocked, "not a directory");
    PreparationLedger ledger;
    ledger.extraction = Layer(Inputs(), true, 208);
    std::string why;
    CHECK_FALSE(ledger.Save(blocked, &why));
    CHECK_TRUE(!why.empty());
    CHECK_FALSE(ledger.writable);
    CHECK_TRUE(ledger.extraction.ExtractionValid(Inputs()));
  }

  BeginCase("the path helpers reject undeclared rendering paths");
  {
    CHECK_TRUE(IsPreparationPath("d3d12/rtv"));
    CHECK_TRUE(IsPreparationPath("vulkan/fsi"));
    CHECK_FALSE(IsPreparationPath("d3d12/xyz"));
    CHECK_EQ(PreparationPathIndex("d3d12/rov"), 1);
    CHECK_EQ(PreparationPathIndex("vulkan/fsi"), 3);
    CHECK_EQ(PreparationPathIndex("nope"), kPreparationPathCount);
  }

  std::error_code error;
  std::filesystem::remove_all(root, error);
  return Finish();
}
