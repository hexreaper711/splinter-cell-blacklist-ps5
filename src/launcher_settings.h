// The launcher's Settings page: the emulation options a player may need to change for this game
// (the render path, odd textures, resolution...), changed with the controller instead of a new
// build. Saved in <user data>/launcher-settings.toml and set before the game starts, over
// blacklist.toml: the GPU reads them when the game starts, after the launcher.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct LauncherSetting {
  std::string label;                 // "Render path"
  std::string help;                  // a line about it, under the list
  std::vector<std::string> choices;  // what each choice is called
  size_t chosen = 0;
  size_t recommended = 0;            // the choice to use unless something needs another
};

class LauncherSettings {
 public:
  explicit LauncherSettings(std::filesystem::path file);

  // The saved choices onto the settings (cvars). Call before the game starts.
  void LoadAndApply();
  // The settings and their current choices, for the launcher.
  std::vector<LauncherSetting> List() const;
  // A choice made on the launcher: set and saved.
  void Choose(size_t setting, size_t choice);
  // Every setting back to its recommended choice (saved). Also what a new install starts with.
  void RestoreRecommended();

 private:
  void Save() const;

  std::filesystem::path file_;
};
