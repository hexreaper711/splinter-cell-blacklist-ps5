// blacklist - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

#include "hd_texture_pack.h"
#include "launcher_settings.h"

class BlacklistApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BlacklistApp>(new BlacklistApp(ctx, "blacklist",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

  // The game folder: game_data_root from blacklist.toml when set, else the disc picked on the
  // launcher (game/disc1 or game/disc2, see disc_launcher.h).
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override;

  // The launcher's font, at the two sizes it draws.
  void OnConfigureFonts(ImFontAtlas* atlas) override;
  // The game's message boxes and keyboard (SDK dialogs) in that font, sized for a TV.
  void OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) override;

  // Finishes the HD Texture Pack's install (hd_texture_pack.h); answers the game's disc swaps.
  void OnPostSetup() override;

 private:
  // The game asking for another disc (XamSwapDisc): game/discN mounted as game: and d:.
  bool SwapDisc(uint32_t disc);

  ImFont* launcher_display_font_ = nullptr;
  ImFont* launcher_text_font_ = nullptr;
  // The launcher's picture, until the game starts.
  std::unique_ptr<rex::ui::ImmediateTexture> launcher_background_;
  // Installed from game/disc2 while the launcher shows it, the first time.
  std::unique_ptr<HdTexturePackInstall> hd_texture_pack_;
  // The launcher's Settings page.
  std::unique_ptr<LauncherSettings> launcher_settings_;
  // The discs swapped to so far (each mounted once, open files stay on theirs).
  std::vector<uint32_t> mounted_discs_;
};
