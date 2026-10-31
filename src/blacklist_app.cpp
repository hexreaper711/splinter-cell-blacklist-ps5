// Title-side runtime hooks: the disc launcher.

#include "generated/default/blacklist_init.h"
#include "blacklist_app.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform.h>
#if REX_PLATFORM_PS5
#include <rex/ps5_diag.h>
#endif
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/util/xex2_info.h>
#include <rex/system/xmemory.h>
#include <rex/ui/image_decode.h>

#include "disc_launcher.h"
#include "hd_texture_pack.h"

// (the launcher's HD Texture Pack setting)
REXCVAR_DEFINE_BOOL(hd_texture_pack, true, "Blacklist",
                    "Let the game use the installed HD Texture Pack");

namespace {
std::filesystem::path LauncherFolder() {
  return rex::filesystem::GetExecutableFolder() / "launcher";
}

// The first line of the app's VERSION.txt (build.sh writes it: "v0.1.0"), or nothing.
std::string AppVersion() {
  std::ifstream file(rex::filesystem::GetExecutableFolder() / "VERSION.txt");
  std::string line;
  std::getline(file, line);
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
    line.pop_back();
  }
  return line;
}

std::filesystem::path DiscFolder(uint32_t disc) {
  return rex::filesystem::GetExecutableFolder() / "game" / ("disc" + std::to_string(disc));
}

// A disc's identity as its default.xex states it (XEX_HEADER_EXECUTION_INFO, in the header,
// which isn't encrypted): disc 1 is media 054F5A57, disc 2 5E86F600.
struct DiscId {
  uint32_t media_id = 0;
  uint8_t disc_number = 0;
};
std::optional<DiscId> ReadDiscId(const std::filesystem::path& xex) {
  std::ifstream file(xex, std::ios::binary);
  std::vector<uint8_t> header(0x4000);
  file.read(reinterpret_cast<char*>(header.data()), header.size());
  header.resize(size_t(file.gcount()));
  auto be32 = [&](size_t at) {
    return at + 4 <= header.size() ? uint32_t(header[at]) << 24 | uint32_t(header[at + 1]) << 16 |
                                         uint32_t(header[at + 2]) << 8 | header[at + 3]
                                   : 0u;
  };
  if (be32(0) != 0x58455832 /* XEX2 */) {
    return std::nullopt;
  }
  for (uint32_t i = 0, count = be32(0x14); i < count; ++i) {
    if (be32(0x18 + 8 * i) == rex::XEX_HEADER_EXECUTION_INFO) {
      const uint32_t at = be32(0x1C + 8 * i);
      if (at + 0x18 <= header.size()) {
        return DiscId{be32(at), header[at + 0x12]};
      }
    }
  }
  return std::nullopt;
}
}  // namespace

void BlacklistApp::OnConfigureFonts(ImFontAtlas* atlas) {
  const auto font = LauncherFolder() / "Roboto-Medium.ttf";
  if (!std::filesystem::exists(font)) {
    REXLOG_WARN("Launcher: no {}, using the default font", font.string());
    return;
  }
  // Baked at the largest size each is drawn at, for a 2160-line screen; ASCII is all it shows.
  static const ImWchar ascii[] = {0x0020, 0x007E, 0};
  ImFontConfig config;
  config.OversampleH = config.OversampleV = 1;
  launcher_display_font_ = atlas->AddFontFromFileTTF(font.string().c_str(), 150.0f, &config, ascii);
  launcher_text_font_ = atlas->AddFontFromFileTTF(font.string().c_str(), 48.0f, &config, ascii);
}

void BlacklistApp::OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) {
  (void)ui_style;
  // The SDK's dialogs are laid out for its 10-pixel font: on the PS5's 3840x2160 screen the game's
  // "install the HD Texture Pack?" box was a few centimetres wide. They size themselves to their
  // text, so a 48-pixel font and three times the spacing make them readable from the sofa.
  if (launcher_text_font_) {
    ImGui::GetIO().FontDefault = launcher_text_font_;
    imgui_style.ScaleAllSizes(3.0f);
  }
}

void BlacklistApp::OnPostSetup() {
  if (!runtime() || !runtime()->kernel_state()) {
    return;
  }
  // (the launcher extracted the HD Texture Pack, if it was due: its content header now)
  if (hd_texture_pack_) {
    hd_texture_pack_->Finish(runtime()->kernel_state()->content_manager());
    hd_texture_pack_->SetEnabled(REXCVAR_GET(hd_texture_pack));
  }
  runtime()->kernel_state()->set_disc_swap_handler([this](uint32_t disc) { return SwapDisc(disc); });
#if REX_PLATFORM_PS5
  // Back to the launcher: the game's own Quit, or OPTIONS + touchpad held for 2 seconds (the app
  // restarts; a game in progress is left as at its last save).
  rex::ps5diag::title_exit_hook = [] { rex::ps5diag::RestartApp("the game quit"); };
  rex::ps5diag::WatchLeaveGame([] { rex::ps5diag::RestartApp("OPTIONS + touchpad held"); });
#endif
}

// The 360 asks for the other disc when the story crosses over (and for disc 1 when started from
// disc 2) and goes on running once it's in: the same code serves both discs, so the other disc's
// folder is mounted, game: and d: moved to it, and the disc the game reads its identity from
// (XamGetExecutionId: the executable's execution info) made that disc's.
bool BlacklistApp::SwapDisc(uint32_t disc) {
  const auto folder = DiscFolder(disc);
  const auto id = ReadDiscId(folder / "default.xex");
  auto* kernel = runtime()->kernel_state();
  if (!id || !kernel) {
    REXLOG_ERROR("Disc swap: no disc {} in {}", disc, folder.string());
    return false;
  }
  auto* vfs = kernel->file_system();
  const std::string mount = "\\Device\\Harddisk0\\Disc" + std::to_string(disc);
  if (std::find(mounted_discs_.begin(), mounted_discs_.end(), disc) == mounted_discs_.end()) {
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(mount, folder, true);
    if (!device->Initialize() || !vfs->RegisterDevice(std::move(device))) {
      REXLOG_ERROR("Disc swap: can't mount {}", folder.string());
      return false;
    }
    mounted_discs_.push_back(disc);
  }
  for (const char* link : {"game:", "d:"}) {
    vfs->UnregisterSymbolicLink(link);
    vfs->RegisterSymbolicLink(link, mount);
  }
  uint32_t info_address = 0;
  if (auto module = kernel->GetExecutableModule();
      module && module->GetOptHeader(rex::XEX_HEADER_EXECUTION_INFO, &info_address) == 0) {
    auto* info = runtime()->memory()->TranslateVirtual<rex::xex2_opt_execution_info*>(info_address);
    info->media_id = id->media_id;
    info->disc_number = id->disc_number;
  }
  REXLOG_INFO("Disc swap: disc {} in ({}, media {:08X})", disc, folder.string(), id->media_id);
  return true;
}

std::optional<rex::PathConfig> BlacklistApp::OnFinalizePaths(
    const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) {
  // SetupEnvironment snapshots game_data_root before LoadConfig, so a toml value is ignored
  // unless the cvar is read again here (as the Broken Bond port does). Set, it skips the launcher.
  auto paths = defaults;
  launcher_settings_ = std::make_unique<LauncherSettings>(
      (paths.user_data_root.empty() ? rex::filesystem::GetExecutableFolder() : paths.user_data_root) /
      "launcher-settings.toml");
  launcher_settings_->LoadAndApply();
  if (paths.game_data_root.empty()) {
    const auto from_cvar = rex::cvar::GetFlagByName("game_data_root");
    if (!from_cvar.empty()) {
      paths.game_data_root = from_cvar;
    }
  }
  if (!paths.game_data_root.empty()) {
    return paths;
  }

  const std::filesystem::path games = rex::filesystem::GetExecutableFolder() / "game";
  std::vector<DiscChoice> discs = {
      {"Disc 1", "Campaign Part 1 & Multiplayer", games / "disc1"},
      {"Disc 2", "Campaign Part 2 & HD Texture Pack", games / "disc2"},
  };
  for (auto& disc : discs) {
    disc.present = std::filesystem::exists(disc.root / "default.xex");
    REXLOG_INFO("Launcher: {} {} ({})", disc.title, disc.present ? "installed" : "missing",
                disc.root.string());
  }
  if (!imgui_drawer()) {
    // (no screen to ask on: the first disc that is there)
    for (const auto& disc : discs) {
      if (disc.present) {
        paths.game_data_root = disc.root;
        return paths;
      }
    }
    return paths;
  }
  LauncherLook look;
  look.display_font = launcher_display_font_;
  look.text_font = launcher_text_font_;
  look.version = AppVersion();
  REXLOG_INFO("Launcher: version {}", look.version.empty() ? "unknown" : look.version);
  const auto picture = LauncherFolder() / "launcher-background.png";
  std::ifstream file(picture, std::ios::binary);
  const std::vector<uint8_t> png((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
  int width = 0;
  int height = 0;
  const auto rgba = rex::ui::DecodeImageRGBA(png.data(), png.size(), width, height);
  if (!rgba.empty() && immediate_drawer()) {
    launcher_background_ = immediate_drawer()->CreateTexture(
        width, height, rex::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
    look.background = launcher_background_.get();
  } else {
    REXLOG_WARN("Launcher: no background ({})", picture.string());
  }
  LauncherTask task;
  hd_texture_pack_ = std::make_unique<HdTexturePackInstall>(paths.user_data_root);
  if (hd_texture_pack_->Pending()) {
    look.note = "The HD Texture Pack (3 GB, from Disc 2) is installed once, when you start a disc.";
    task.title = "INSTALLING THE HD TEXTURE PACK";
    task.start = [this] { hd_texture_pack_->Start(); };
    task.progress = [this] { return hd_texture_pack_->Progress(); };
  }

  // The dialog deletes itself when closed. The game starts on the next UI loop turn, not from
  // inside the dialog's own draw; the picture goes then (the drawer keeps it until the GPU is
  // done with the last frame that showed it).
  LauncherSettingsPage settings_page;
  settings_page.settings = launcher_settings_->List();
  settings_page.choose = [this](size_t setting, size_t choice) {
    launcher_settings_->Choose(setting, choice);
  };
  settings_page.restore = [this] {
    launcher_settings_->RestoreRecommended();
    return launcher_settings_->List();
  };
  new DiscLauncherDialog(imgui_drawer(), look, std::move(task), std::move(settings_page),
                         std::move(discs),
                         [this, paths, resume](const DiscChoice& disc) mutable {
                           paths.game_data_root = disc.root;
                           app_context().CallInUIThreadDeferred([this, resume, paths] {
                             launcher_background_.reset();
                             resume(paths);
                           });
                         });
  return std::nullopt;
}
