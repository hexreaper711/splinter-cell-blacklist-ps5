#include "launcher_settings.h"

#include <cstdlib>
#include <fstream>
#include <string_view>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace {
// Raised when the recommended settings change: a file of another version is set back to them
// (once), so nobody keeps settings tried for a problem a newer build fixed.
constexpr int kVersion = 3;

// (the first choice of each is the recommended one)
struct Definition {
  const char* cvar;
  const char* label;
  const char* help;
  std::vector<std::pair<const char*, const char*>> choices;  // shown, value
};

const std::vector<Definition>& Definitions() {
  static const std::vector<Definition> definitions = {
      {"hd_texture_pack", "HD Texture Pack",
       "The 3 GB of sharper textures from disc 2 (installed once). Off hides them from the game.",
       {{"On", "true"}, {"Off", "false"}}},
      {"render_target_path_vulkan", "Render path",
       "Accurate emulates the Xbox's EDRAM exactly: try it if the 3D world is black. Standard is "
       "faster.",
       {{"Standard", ""}, {"Accurate (FSI)", "fsi"}}},
      {"depth_float24_convert_in_pixel_shader", "Depth conversion",
       "The Xbox kept depth as 24-bit floats. Exact converts it in every pixel (slower): try it "
       "if people and walls are missing and only lights show.",
       {{"Fast", "false"}, {"Exact", "true"}}},
      {"depth_float24_round", "Depth rounding",
       "With Exact depth: round instead of cut off. On if Exact alone doesn't bring the world back.",
       {{"Off", "false"}, {"On", "true"}}},
      {"native_2x_msaa", "2x MSAA",
       "The Xbox's own 2x anti-aliasing, done by the PS5's GPU. Off draws it another way.",
       {{"On", "true"}, {"Off", "false"}}},
      {"gpu_allow_invalid_fetch_constants", "Odd textures",
       "Draw textures the game leaves marked invalid (the story's 3D world needed it). Skip is "
       "Xenia's default.",
       {{"Draw", "true"}, {"Skip", "false"}}},
      {"resolution_scale", "Resolution",
       "The Xbox drew at 720p. Sharper means much more work for the GPU.",
       {{"720p (Xbox)", "1"}, {"1440p", "2"}, {"4K", "3"}}},
      {"guest_vblank_rate", "Frame rate",
       "The Xbox ran the game at 30. 60 is experimental: smoother, but if anything moves too fast "
       "or the game can't keep up, go back to 30.",
       {{"30 (Xbox)", "0"}, {"60 (experimental)", "120"}}},
      {"texture_replace_dir", "Lens flare halo",
       "The big rainbow ring the lens flare draws when you face a bright light. Off removes the "
       "ring; the glow stays.",
       {{"On", "/app0/launcher/textures"},
        {"Off", "/app0/launcher/textures;/app0/launcher/textures-no-halo"}}},
      {"async_shader_compilation", "New shaders",
       "Wait: a scene pauses the first time it needs a shader. Background: no pause, but things "
       "are missing until it's ready.",
       {{"Wait", "false"}, {"Background", "true"}}},
      {"readback_resolve", "GPU readback",
       "The game reads some GPU results itself (what's visible, how bright it is): without them "
       "far scenery is missing and the screen flickers black. Small: just those. Full: all, slow.",
       {{"Small", "small"}, {"Full", "full"}, {"Off", "none"}, {"Fast", "fast"},
        {"Some", "some"}}},
      {"readback_memexport_fast", "Visibility results",
       "What the GPU's shaders write to memory for the game (which objects are visible). Exact "
       "hands them over at once; Fast a frame late, so things pop in and out.",
       {{"Exact", "false"}, {"Fast", "true"}}},
      {"occlusion_query_enable", "Occlusion queries",
       "On asks the GPU how much of each light is visible, for its lens flare. Off says all of "
       "it: flares and glare everywhere, even indoors and through walls.",
       {{"On", "true"}, {"Off", "false"}}},
      {"vsync", "V-Sync", "Off only to test: the picture may tear.",
       {{"On", "true"}, {"Off", "false"}}},
  };
  return definitions;
}

std::string Trim(std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\"");
  const auto end = text.find_last_not_of(" \t\r\"");
  return begin == std::string_view::npos ? std::string()
                                         : std::string(text.substr(begin, end - begin + 1));
}

bool Set(const char* cvar, const char* value) {
  // The GPU reads these when the game starts, after the launcher; render_target_path_vulkan is
  // init-only, which the SDK otherwise refuses after its own start.
  rex::cvar::testing::ScopedLifecycleOverride before_the_gpu_starts;
  return rex::cvar::SetFlagByName(cvar, value);
}
}  // namespace

LauncherSettings::LauncherSettings(std::filesystem::path file) : file_(std::move(file)) {}

void LauncherSettings::LoadAndApply() {
  std::ifstream in(file_);
  std::vector<std::pair<std::string, std::string>> saved;
  int version = 0;
  std::string line;
  while (std::getline(in, line)) {
    const auto equals = line.find('=');
    if (line.empty() || line[0] == '#' || equals == std::string::npos) {
      continue;
    }
    std::string name = Trim(std::string_view(line).substr(0, equals));
    std::string value = Trim(std::string_view(line).substr(equals + 1));
    if (name == "settings_version") {
      version = std::atoi(value.c_str());
    } else {
      saved.emplace_back(std::move(name), std::move(value));
    }
  }
  if (version != kVersion) {
    REXLOG_INFO("Launcher settings: recommended ones ({})",
                version ? "saved by an older build" : "first start");
    RestoreRecommended();
    return;
  }
  for (const auto& [name, value] : saved) {
    for (const auto& definition : Definitions()) {
      if (name == definition.cvar) {
        const bool ok = Set(definition.cvar, value.c_str());
        REXLOG_INFO("Launcher settings: {} = \"{}\"{}", name, value, ok ? "" : " (refused)");
      }
    }
  }
}

std::vector<LauncherSetting> LauncherSettings::List() const {
  std::vector<LauncherSetting> list;
  for (const auto& definition : Definitions()) {
    LauncherSetting setting;
    setting.label = definition.label;
    setting.help = definition.help;
    const std::string current = rex::cvar::GetFlagByName(definition.cvar);
    for (size_t i = 0; i < definition.choices.size(); ++i) {
      setting.choices.push_back(definition.choices[i].first);
      if (current == definition.choices[i].second) {
        setting.chosen = i;
      }
    }
    setting.recommended = 0;
    list.push_back(std::move(setting));
  }
  return list;
}

void LauncherSettings::Choose(size_t setting, size_t choice) {
  const auto& definitions = Definitions();
  if (setting >= definitions.size() || choice >= definitions[setting].choices.size()) {
    return;
  }
  const auto& definition = definitions[setting];
  const bool ok = Set(definition.cvar, definition.choices[choice].second);
  REXLOG_INFO("Launcher settings: {} = \"{}\"{}", definition.cvar,
              definition.choices[choice].second, ok ? "" : " (refused)");
  Save();
}

void LauncherSettings::RestoreRecommended() {
  for (const auto& definition : Definitions()) {
    Set(definition.cvar, definition.choices.front().second);
  }
  Save();
}

void LauncherSettings::Save() const {
  std::error_code ec;
  std::filesystem::create_directories(file_.parent_path(), ec);
  std::ofstream out(file_);
  out << "# Chosen on the launcher's Settings page (they override blacklist.toml).\n";
  out << "settings_version = " << kVersion << "\n";
  for (const auto& definition : Definitions()) {
    out << definition.cvar << " = \"" << rex::cvar::GetFlagByName(definition.cvar) << "\"\n";
  }
}
