// The HD Texture Pack: a 3 GB marketplace content package on disc 2 (Content/0000000000000000/
// 555308B6/00000002/). On the 360 the game offers to copy it to the hard drive and asks at every
// start until it's there. Here it is installed once from game/disc2, the way the SDK's
// ContentManager::InstallContent does it (extracted into <user data>/0000000000000000/555308B6/
// 00000002/<package>/, a .header for the game's content enumeration), but on a thread of its own
// while the launcher shows the progress: InstallContent ran on the UI thread and froze the screen
// for the whole copy.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

namespace rex::system::xam {
class ContentManager;
}

class HdTexturePackInstall {
 public:
  explicit HdTexturePackInstall(std::filesystem::path user_data_root);
  ~HdTexturePackInstall();

  // Not installed yet, and disc 2's package is there.
  bool Pending() const;

  // Extracts the package (on its own thread).
  void Start();
  // 0..1 while extracting, 1 once done, -1 if it failed.
  float Progress() const;

  // Once the runtime is set up: the .header the game's content enumeration reads, then the
  // marker that the install is complete (written last: an install cut short is done again).
  void Finish(rex::system::xam::ContentManager* content);

  // The installed pack where the game finds it, or parked beside (the launcher's setting): the
  // SDK lists every folder of the content directory, so off it moves out (a rename, no copy).
  void SetEnabled(bool enabled);

 private:
  void Run();
  std::filesystem::path InstallFolder() const;
  std::filesystem::path Marker() const;

  std::filesystem::path user_data_root_;
  std::thread thread_;
  std::atomic<uint64_t> done_bytes_{0};
  std::atomic<uint64_t> total_bytes_{0};
  enum State { kIdle, kExtracting, kExtracted, kFailed };
  std::atomic<State> state_{kIdle};
  // (from the package header, for the .header)
  std::u16string display_name_;
  uint32_t license_mask_ = 0;
};
