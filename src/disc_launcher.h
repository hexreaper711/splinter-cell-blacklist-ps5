// The start screen: Splinter Cell: Blacklist came on two discs (Campaign Part 1 & Multiplayer,
// Campaign Part 2 & HD Texture Pack), installed side by side in game/disc1 and game/disc2. Two
// pages, L1/R1 between them: Play lists the discs found and starts the one picked (D-pad or left
// stick, Cross), Settings the emulation options (launcher_settings.h), changed with left/right.
// On the PS5 from any logged-in user's DualSense. Its picture and font are the app's launcher/
// folder (launcher-background.png, Roboto-Medium.ttf); without them it draws on plain dark.

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>

#include "launcher_settings.h"

struct ImFont;

struct DiscChoice {
  std::string title;        // "Disc 1"
  std::string description;  // what the disc holds
  std::filesystem::path root;
  bool present = false;     // its default.xex is there
};

// What the screen is drawn with (owned by the app: the dialog deletes itself before its last
// frame is drawn). Any of it may be missing.
struct LauncherLook {
  ImFont* display_font = nullptr;                    // the titles
  ImFont* text_font = nullptr;                       // the rest
  rex::ui::ImmediateTexture* background = nullptr;  // full screen, cropped to fill
  std::string note;                                  // a line above the buttons, if any
  std::string version;                               // bottom right ("v0.1.0"), if any
};

// Work done after a disc is picked, before its game starts, shown with a progress bar (the HD
// Texture Pack's install). None if start is empty.
struct LauncherTask {
  std::string title;
  std::function<void()> start;
  std::function<float()> progress;  // 0..1, 1 = done, < 0 = failed (the game starts either way)
};

// The Settings page: the settings, and what to do when one is changed (setting, choice).
struct LauncherSettingsPage {
  std::vector<LauncherSetting> settings;
  std::function<void(size_t, size_t)> choose;
  // All back to the recommended choices (Square): the settings as they are then.
  std::function<std::vector<LauncherSetting>()> restore;
};

class DiscLauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  DiscLauncherDialog(rex::ui::ImGuiDrawer* imgui_drawer, LauncherLook look, LauncherTask task,
                     LauncherSettingsPage settings, std::vector<DiscChoice> discs,
                     std::function<void(const DiscChoice&)> on_pick);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  // The buttons pressed since the previous frame (PS5 pad bits), from every open pad.
  uint32_t PollPressed();

  LauncherLook look_;
  LauncherTask task_;
  bool task_running_ = false;
  LauncherSettingsPage settings_;
  bool settings_page_ = false;
  size_t setting_row_ = 0;
  std::vector<DiscChoice> discs_;
  std::function<void(const DiscChoice&)> on_pick_;
  size_t selected_ = 0;
  bool picked_ = false;
  uint32_t held_ = 0;
  double shown_at_ = -1.0;  // ImGui time of the first frame (the fade in)
};
