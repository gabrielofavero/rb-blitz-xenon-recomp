// SPDX-License-Identifier: GPL-2.0-only
#include "fs/payload_overlay.h"

#include <array>
#include <atomic>
#include <optional>
#include <rex/kernel/types/status.h>
#include <rex/logging/log_category.h>

namespace rb_blitz::fs {
namespace {
std::atomic<StorePathObserver> observer{nullptr};

void Observe(std::string_view path, bool open, rex::filesystem::Access access,
              rex::filesystem::FileSystemError error) {
  const auto callback=observer.load(std::memory_order_relaxed);
  if (!callback) return;
  using E=rex::filesystem::FileSystemError;
  using namespace rex::kernel;
  X_STATUS status=X_STATUS_UNSUCCESSFUL;
  switch(error) {
    case E::kNone:status=X_STATUS_SUCCESS;break;
    case E::kNotFound:case E::kNoMount:status=X_STATUS_NO_SUCH_FILE;break;
    case E::kPathNotFound:status=X_STATUS_OBJECT_PATH_NOT_FOUND;break;
    case E::kAccessDenied:status=X_STATUS_ACCESS_DENIED;break;
    default:break;
  }
  callback(path,open,static_cast<uint32_t>(access),static_cast<int32_t>(status));
}
}

void SetStorePathObserver(StorePathObserver callback) {
  observer.store(callback,std::memory_order_relaxed);
}

bool MountPayloadOverlay(rex::filesystem::IFileSystem& filesystem, std::string_view mount,
    const std::filesystem::path& payload_root, std::string_view base_path,
    std::vector<std::string> hidden_paths, const std::filesystem::path& settings_root,
    std::vector<std::string> redirects, std::string* error) {
  auto fail=[&](std::string message) {
    if(error)*error=message;
    REXLOG_ERROR("payload: {}",message);
    return false;
  };
  auto policy=std::make_shared<rex::filesystem::OverlayMountOptions>();
  policy->base_path=base_path;
  policy->excluded_source_paths=std::move(hidden_paths);
  policy->redirect_root=settings_root;
  policy->redirected_paths=std::move(redirects);
  policy->observe=&Observe;
  rex::filesystem::MountEntry entry{std::string(mount),
      rex::filesystem::ProviderType::kOverlay,payload_root,
      rex::filesystem::MountFlags::kNone,{},policy};
  auto mounted=filesystem.Mount(entry);
  if(!mounted && !policy->redirected_paths.empty()) {
    REXLOG_WARN("payload: cannot mount player storage {}: {}; retaining read-only records",
        settings_root.string(),mounted.error().message);
    policy->redirected_paths.clear();
    policy->redirect_root.clear();
    mounted=filesystem.Mount(entry);
  }
  if(!mounted)return fail("cannot mount payload "+payload_root.string()+" over "+
      std::string(base_path)+": "+mounted.error().message);
  constexpr std::array<std::string_view,2> aliases{"game:","d:"};
  std::array<std::optional<std::string>,2> previous;
  for(size_t i=0;i<aliases.size();++i)previous[i]=filesystem.FindLink(aliases[i]);
  for(size_t i=0;i<aliases.size();++i) {
    if(previous[i]) (void)filesystem.Unlink(aliases[i]);
    auto linked=filesystem.Link(aliases[i],mount);
    if(linked)continue;
    for(size_t j=0;j<=i;++j) {
      (void)filesystem.Unlink(aliases[j]);
      if(previous[j]) (void)filesystem.Link(aliases[j],*previous[j]);
    }
    (void)filesystem.Unmount(mount);
    return fail("cannot route payload alias "+std::string(aliases[i])+": "+linked.error().message);
  }
  REXLOG_INFO("payload: mounted {} over {} at {}; {} hidden paths, {} player redirects",
      payload_root.string(),base_path,mount,policy->excluded_source_paths.size(),
      policy->redirected_paths.size());
  return true;
}
}  // namespace rb_blitz::fs
