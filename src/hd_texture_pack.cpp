#include "hd_texture_pack.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <span>
#include <vector>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/file.h>
#include <rex/logging.h>
#include <rex/string.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>

namespace {
constexpr uint32_t kTitleId = 0x555308B6;

std::filesystem::path Package() {
  return rex::filesystem::GetExecutableFolder() / "game" / "disc2" / "Content" /
         "0000000000000000" / "555308B6" / "00000002" /
         "EE6AD050002B5EEB981B7E5A0F7F7B6F90F85B8855";
}
}  // namespace

HdTexturePackInstall::HdTexturePackInstall(std::filesystem::path user_data_root)
    : user_data_root_(std::move(user_data_root)) {}

HdTexturePackInstall::~HdTexturePackInstall() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

std::filesystem::path HdTexturePackInstall::InstallFolder() const {
  // (ContentManager::ResolvePackagePath for xuid 0, marketplace content)
  return user_data_root_ / "0000000000000000" / "555308B6" / "00000002" / Package().filename();
}

std::filesystem::path HdTexturePackInstall::Marker() const {
  return user_data_root_ / "hd-texture-pack.installed";
}

bool HdTexturePackInstall::Pending() const {
  return !std::filesystem::exists(Marker()) && std::filesystem::exists(Package());
}

void HdTexturePackInstall::Start() {
  if (state_ != kIdle) {
    return;
  }
  state_ = kExtracting;
  thread_ = std::thread([this] { Run(); });
}

float HdTexturePackInstall::Progress() const {
  switch (state_.load()) {
    case kExtracted:
      return 1.0f;
    case kFailed:
      return -1.0f;
    default: {
      const uint64_t total = total_bytes_;
      return total ? std::min(0.999f, float(double(done_bytes_) / double(total))) : 0.0f;
    }
  }
}

void HdTexturePackInstall::Run() {
  using rex::X_STATUS;  // (X_STATUS_SUCCESS names it unqualified)
  const auto start = std::chrono::steady_clock::now();
  REXLOG_INFO("HD Texture Pack: installing from {}", Package().string());
  rex::filesystem::StfsContainerDevice device("", Package());
  auto* root = device.Initialize() ? device.ResolvePath("") : nullptr;
  if (!root) {
    REXLOG_ERROR("HD Texture Pack: can't open {}", Package().string());
    state_ = kFailed;
    return;
  }
  display_name_ = device.header().metadata.display_name(rex::system::XLanguage::kEnglish);
  for (const auto& license : device.header().header.licenses) {
    if (license.license_flags) {
      license_mask_ |= license.license_bits;
    }
  }

  // Every entry, sizes first for the progress.
  std::vector<rex::filesystem::Entry*> entries;
  for (size_t i = 0, n = 1; i < n; ++i) {
    auto* entry = i == 0 ? root : entries[i - 1];
    for (auto& child : entry->children()) {
      entries.push_back(child.get());
      total_bytes_ += child->size();
    }
    n = entries.size() + 1;
  }

  const auto folder = InstallFolder();
  std::vector<uint8_t> buffer(16u << 20);
  for (auto* entry : entries) {
    const auto dest =
        folder / rex::to_path(rex::string::utf8_fix_path_separators(entry->path()));
    std::error_code ec;
    if (entry->attributes() & rex::filesystem::kFileAttributeDirectory) {
      std::filesystem::create_directories(dest, ec);
      continue;
    }
    std::filesystem::create_directories(dest.parent_path(), ec);
    rex::filesystem::File* in = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kFileReadData, &in) != X_STATUS_SUCCESS) {
      REXLOG_ERROR("HD Texture Pack: can't read {}", entry->path());
      state_ = kFailed;
      return;
    }
    std::unique_ptr<FILE, int (*)(FILE*)> out(rex::filesystem::OpenFile(dest, "wb"), fclose);
    size_t offset = 0;
    bool ok = out != nullptr;
    while (ok && offset < entry->size()) {
      size_t read = 0;
      in->ReadSync(std::span<uint8_t>(buffer.data(), std::min(buffer.size(), entry->size() - offset)),
                   offset, &read);
      ok = read > 0 && fwrite(buffer.data(), 1, read, out.get()) == read;
      offset += read;
      done_bytes_ += read;
    }
    in->Destroy();
    if (!ok) {
      REXLOG_ERROR("HD Texture Pack: copying {} to {} failed", entry->path(), dest.string());
      state_ = kFailed;
      return;
    }
  }
  REXLOG_INFO("HD Texture Pack: {} files, {} MB extracted in {} s", entries.size(),
              total_bytes_ >> 20,
              std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::steady_clock::now() - start)
                  .count());
  state_ = kExtracted;
}

void HdTexturePackInstall::SetEnabled(bool enabled) {
  const auto installed = InstallFolder();
  const auto parked = user_data_root_ / "hd-texture-pack.off" / installed.filename();
  std::error_code ec;
  if (!enabled && std::filesystem::exists(installed)) {
    std::filesystem::create_directories(parked.parent_path(), ec);
    std::filesystem::rename(installed, parked, ec);
    REXLOG_INFO("HD Texture Pack: off (moved to {}){}", parked.string(), ec ? " - failed" : "");
  } else if (enabled && std::filesystem::exists(parked) && !std::filesystem::exists(installed)) {
    std::filesystem::rename(parked, installed, ec);
    REXLOG_INFO("HD Texture Pack: on again{}", ec ? " - failed" : "");
  }
}

void HdTexturePackInstall::Finish(rex::system::xam::ContentManager* content) {
  if (thread_.joinable()) {
    thread_.join();
  }
  if (state_ != kExtracted || !content) {
    return;
  }
  rex::system::xam::XCONTENT_AGGREGATE_DATA data;
  data.device_id = static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
  data.content_type = rex::system::XContentType::kMarketplaceContent;
  data.title_id = kTitleId;
  data.xuid = 0;
  data.set_file_name(rex::path_to_utf8(Package().filename()));
  data.set_display_name(display_name_.empty() ? rex::path_to_utf16(Package().filename())
                                              : display_name_);
  if (content->WriteContentHeaderFile(0, data, license_mask_) != 0) {
    REXLOG_ERROR("HD Texture Pack: writing its content header failed");
    return;
  }
  std::ofstream(Marker()) << "installed from " << Package().string() << "\n";
  REXLOG_INFO("HD Texture Pack: installed");
}
