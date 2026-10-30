#include "disc_launcher.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <imgui.h>

#include <rex/logging.h>
#include <rex/platform.h>

#if REX_PLATFORM_PS5
extern "C" {
int sceUserServiceInitialize(void* params);
struct SceUserServiceLoginUserIdList {
  int32_t user_id[4];
};
int sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList* list);
int scePadInit(void);
int scePadOpen(int32_t user_id, int32_t type, int32_t index, const void* param);
int scePadGetHandle(int32_t user_id, int32_t type, int32_t index);
int scePadReadState(int32_t handle, void* data);
}
#endif

namespace {
// ScePadData's buttons and left stick (the rest is unused here).
struct PadData {
  uint32_t buttons;
  uint8_t left_stick_x;
  uint8_t left_stick_y;
  uint8_t rest[246];
};
constexpr uint32_t kPadUp = 0x00000010u;
constexpr uint32_t kPadRight = 0x00000020u;
constexpr uint32_t kPadDown = 0x00000040u;
constexpr uint32_t kPadLeft = 0x00000080u;
constexpr uint32_t kPadL1 = 0x00000400u;
constexpr uint32_t kPadR1 = 0x00000800u;
constexpr uint32_t kPadCross = 0x00004000u;
constexpr uint32_t kPadSquare = 0x00008000u;
// (stick past these counts as a D-pad press)
constexpr uint32_t kStickUp = 0x10000000u;
constexpr uint32_t kStickDown = 0x20000000u;
constexpr uint32_t kStickLeft = 0x40000000u;
constexpr uint32_t kStickRight = 0x80000000u;

// The goggles' green, and the PlayStation cross's blue.
constexpr ImU32 kGreen = IM_COL32(118, 232, 72, 255);
constexpr ImU32 kCrossBlue = IM_COL32(124, 178, 232, 255);

ImU32 WithAlpha(ImU32 color, float alpha) {
  const auto a = static_cast<uint32_t>(((color >> IM_COL32_A_SHIFT) & 0xFF) *
                                       std::clamp(alpha, 0.0f, 1.0f));
  return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

float SpacedWidth(ImFont* font, float size, const std::string& text, float spacing) {
  float width = 0.0f;
  for (size_t i = 0; i < text.size(); ++i) {
    width += font->CalcTextSizeA(size, FLT_MAX, 0.0f, &text[i], &text[i] + 1).x;
  }
  return width + spacing * static_cast<float>(text.empty() ? 0 : text.size() - 1);
}

// Text with extra room between the letters (ImGui has no tracking of its own).
void AddSpacedText(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color,
                   const std::string& text, float spacing) {
  for (size_t i = 0; i < text.size(); ++i) {
    draw->AddText(font, size, pos, color, &text[i], &text[i] + 1);
    pos.x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, &text[i], &text[i] + 1).x + spacing;
  }
}

std::string Upper(std::string text) {
  for (char& c : text) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return text;
}
}  // namespace

DiscLauncherDialog::DiscLauncherDialog(rex::ui::ImGuiDrawer* imgui_drawer, LauncherLook look,
                                       LauncherTask task, LauncherSettingsPage settings,
                                       std::vector<DiscChoice> discs,
                                       std::function<void(const DiscChoice&)> on_pick)
    : rex::ui::ImGuiDialog(imgui_drawer),
      look_(look),
      task_(std::move(task)),
      settings_(std::move(settings)),
      discs_(std::move(discs)),
      on_pick_(std::move(on_pick)) {
  // Start on the first disc that is there.
  for (size_t i = 0; i < discs_.size(); ++i) {
    if (discs_[i].present) {
      selected_ = i;
      break;
    }
  }
}

uint32_t DiscLauncherDialog::PollPressed() {
  uint32_t now = 0;
#if REX_PLATFORM_PS5
  // The pads are opened here first; the game's input driver gets the same handles later
  // (scePadGetHandle when scePadOpen says the pad is open already).
  static bool initialized = false;
  if (!initialized) {
    sceUserServiceInitialize(nullptr);
    scePadInit();
    initialized = true;
  }
  SceUserServiceLoginUserIdList users;
  for (int32_t& user : users.user_id) user = -1;
  if (sceUserServiceGetLoginUserIdList(&users) >= 0) {
    for (int32_t user : users.user_id) {
      if (user == -1 || user == 0xFF) continue;
      int32_t handle = scePadGetHandle(user, 0, 0);
      if (handle < 0) handle = scePadOpen(user, 0, 0, nullptr);
      PadData data;
      std::memset(&data, 0, sizeof(data));
      if (handle < 0 || scePadReadState(handle, &data) != 0) continue;
      now |= data.buttons;
      if (data.left_stick_y < 48) now |= kStickUp;
      if (data.left_stick_y > 208) now |= kStickDown;
      if (data.left_stick_x < 48) now |= kStickLeft;
      if (data.left_stick_x > 208) now |= kStickRight;
    }
  }
#endif
  const uint32_t pressed = now & ~held_;
  held_ = now;
  return pressed;
}

void DiscLauncherDialog::OnDraw(ImGuiIO& io) {
  if (picked_) {
    return;
  }
  const uint32_t pressed = PollPressed();
  const bool any_present =
      std::any_of(discs_.begin(), discs_.end(), [](const DiscChoice& d) { return d.present; });
  auto step = [&](int direction) {
    for (size_t n = 0; n < discs_.size(); ++n) {
      selected_ = (selected_ + discs_.size() + direction) % discs_.size();
      if (discs_[selected_].present) {
        break;
      }
    }
  };
  bool start = false;
  float task_progress = 0.0f;
  if (task_running_) {
    // (the disc is chosen: it starts once the task is done, or has failed)
    task_progress = task_.progress();
    start = task_progress >= 1.0f || task_progress < 0.0f;
  } else if ((pressed & (kPadL1 | kPadR1)) && !settings_.settings.empty()) {
    settings_page_ = !settings_page_;
  } else if (settings_page_ && (pressed & kPadSquare) && settings_.restore) {
    settings_.settings = settings_.restore();
  } else if (settings_page_) {
    const size_t rows = settings_.settings.size();
    if (pressed & (kPadUp | kStickUp)) {
      setting_row_ = (setting_row_ + rows - 1) % rows;
    }
    if (pressed & (kPadDown | kStickDown)) {
      setting_row_ = (setting_row_ + 1) % rows;
    }
    auto& setting = settings_.settings[setting_row_];
    const size_t count = setting.choices.size();
    size_t chosen = setting.chosen;
    if (pressed & (kPadLeft | kStickLeft)) {
      chosen = (chosen + count - 1) % count;
    }
    if (pressed & (kPadRight | kStickRight | kPadCross)) {
      chosen = (chosen + 1) % count;
    }
    if (chosen != setting.chosen) {
      setting.chosen = chosen;
      settings_.choose(setting_row_, chosen);
    }
  } else {
    if (pressed & (kPadUp | kStickUp)) {
      step(-1);
    }
    if (pressed & (kPadDown | kStickDown)) {
      step(1);
    }
    start = (pressed & kPadCross) && any_present && discs_[selected_].present;
    if (start && task_.start) {
      REXLOG_INFO("Launcher: {} picked; {} first", discs_[selected_].title, task_.title);
      task_.start();
      task_running_ = true;
      start = false;
    }
  }

  const double now = ImGui::GetTime();
  if (shown_at_ < 0.0) {
    shown_at_ = now;
  }
  const auto t = static_cast<float>(now - shown_at_);

  // Laid out for 3840x2160 (the PS5's window) and scaled to the screen.
  const ImVec2 screen = io.DisplaySize;
  const float s = screen.y / 2160.0f;
  ImFont* display = look_.display_font ? look_.display_font : ImGui::GetFont();
  ImFont* text = look_.text_font ? look_.text_font : ImGui::GetFont();

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(screen);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.025f, 0.03f, 1.0f));
  ImGui::Begin("##disc_launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
  ImDrawList* draw = ImGui::GetWindowDrawList();

  // The picture, filling the screen (cropped, not stretched), settling in from a slight zoom.
  if (look_.background) {
    const float iw = static_cast<float>(look_.background->width);
    const float ih = static_cast<float>(look_.background->height);
    const float settle = 1.0f - std::min(t / 1.6f, 1.0f);
    const float zoom = std::max(screen.x / iw, screen.y / ih) * (1.0f + 0.04f * settle * settle);
    const ImVec2 size(iw * zoom, ih * zoom);
    const ImVec2 p0((screen.x - size.x) * 0.5f, (screen.y - size.y) * 0.5f);
    draw->AddImage(reinterpret_cast<ImTextureID>(look_.background), p0,
                   ImVec2(p0.x + size.x, p0.y + size.y));
  }
  // Shade on the left for the menu and along the bottom for the hints; Sam stays in the light.
  draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(screen.x * 0.62f, screen.y),
                                IM_COL32(0, 0, 0, 225), IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 225));
  draw->AddRectFilledMultiColor(ImVec2(0, screen.y * 0.70f), screen, IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 200),
                                IM_COL32(0, 0, 0, 200));

  const float left = 300.0f * s;

  // The title.
  float y = 300.0f * s;
  AddSpacedText(draw, text, 46.0f * s, ImVec2(left + 6.0f * s, y), IM_COL32(200, 205, 210, 220),
                "TOM CLANCY'S", 14.0f * s);
  y += 62.0f * s;
  AddSpacedText(draw, display, 150.0f * s, ImVec2(left, y), IM_COL32(245, 247, 250, 255),
                "SPLINTER CELL", 8.0f * s);
  y += 160.0f * s;
  AddSpacedText(draw, display, 100.0f * s, ImVec2(left + 4.0f * s, y), kGreen, "BLACKLIST",
                44.0f * s);
  y += 136.0f * s;
  const float rule = SpacedWidth(display, 150.0f * s, "SPLINTER CELL", 8.0f * s);
  draw->AddRectFilledMultiColor(ImVec2(left, y), ImVec2(left + rule, y + 5.0f * s), kGreen,
                                WithAlpha(kGreen, 0.0f), WithAlpha(kGreen, 0.0f), kGreen);

  const float card_w = 1180.0f * s;
  const float card_h = 200.0f * s;
  const float round = 14.0f * s;
  const float pulse = 0.65f + 0.35f * std::sin(t * 3.0f);

  // A shoulder button's badge ("L1"), its width.
  auto badge = [&](float bx, float by, const char* label) {
    const float size = 34.0f * s;
    const float w = text->CalcTextSizeA(size, FLT_MAX, 0.0f, label).x + 28.0f * s;
    draw->AddRectFilled(ImVec2(bx, by), ImVec2(bx + w, by + 52.0f * s), IM_COL32(20, 22, 26, 230),
                        10.0f * s);
    draw->AddRect(ImVec2(bx, by), ImVec2(bx + w, by + 52.0f * s), IM_COL32(215, 220, 225, 255),
                  10.0f * s, 0, 2.5f * s);
    draw->AddText(text, size, ImVec2(bx + 14.0f * s, by + 8.0f * s), IM_COL32(215, 220, 225, 255),
                  label);
    return w;
  };

  // The pages' tabs.
  y = 960.0f * s;
  {
    float tx = left;
    if (!settings_.settings.empty()) {
      tx += badge(tx, y - 2.0f * s, "L1") + 30.0f * s;
    }
    const char* tabs[] = {"PLAY", "SETTINGS"};
    for (int i = 0; i < (settings_.settings.empty() ? 1 : 2); ++i) {
      const bool active = (i == 1) == settings_page_;
      const float w = SpacedWidth(text, 46.0f * s, tabs[i], 12.0f * s);
      AddSpacedText(draw, text, 46.0f * s, ImVec2(tx, y), active ? IM_COL32(255, 255, 255, 255)
                                                                 : IM_COL32(150, 156, 162, 255),
                    tabs[i], 12.0f * s);
      if (active) {
        draw->AddRectFilled(ImVec2(tx, y + 62.0f * s), ImVec2(tx + w, y + 67.0f * s), kGreen);
      }
      tx += w + 56.0f * s;
    }
    if (!settings_.settings.empty()) {
      badge(tx - 26.0f * s, y - 2.0f * s, "R1");
    }
  }
  y = 1080.0f * s;

  // Settings: a row each, the choice on the right between arrows.
  if (settings_page_) {
    const float row_h = 56.0f * s;
    for (size_t i = 0; i < settings_.settings.size(); ++i) {
      const auto& setting = settings_.settings[i];
      const bool selected = i == setting_row_;
      const ImVec2 p0(left, y);
      const ImVec2 p1(left + card_w, y + row_h - 8.0f * s);
      if (selected) {
        draw->AddRectFilled(p0, p1, IM_COL32(10, 26, 12, 215), round);
        draw->AddRect(p0, p1, WithAlpha(kGreen, 0.55f + 0.45f * pulse), round, 0, 3.0f * s);
        draw->AddRectFilled(p0, ImVec2(p0.x + 10.0f * s, p1.y), kGreen, round,
                            ImDrawFlags_RoundCornersLeft);
      } else {
        draw->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 140), round);
      }
      const float ty = p0.y + 6.0f * s;
      draw->AddText(text, 40.0f * s, ImVec2(p0.x + 44.0f * s, ty),
                    selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 195, 200, 255),
                    setting.label.c_str());
      const std::string& value = setting.choices[setting.chosen];
      const float vw = text->CalcTextSizeA(40.0f * s, FLT_MAX, 0.0f, value.c_str()).x;
      const float vx = p1.x - 70.0f * s - vw;
      // (green: the recommended choice; amber: changed)
      const bool recommended = setting.chosen == setting.recommended;
      const ImU32 value_color = recommended ? (selected ? kGreen : WithAlpha(kGreen, 0.7f))
                                            : IM_COL32(240, 176, 64, 255);
      draw->AddText(text, 40.0f * s, ImVec2(vx, ty), value_color, value.c_str());
      if (selected) {
        const float cy = (p0.y + p1.y) * 0.5f;
        draw->AddTriangleFilled(ImVec2(vx - 44.0f * s, cy), ImVec2(vx - 24.0f * s, cy - 14.0f * s),
                                ImVec2(vx - 24.0f * s, cy + 14.0f * s), kGreen);
        draw->AddTriangleFilled(ImVec2(p1.x - 24.0f * s, cy), ImVec2(p1.x - 44.0f * s, cy - 14.0f * s),
                                ImVec2(p1.x - 44.0f * s, cy + 14.0f * s), kGreen);
      }
      y += row_h;
    }
    if (setting_row_ < settings_.settings.size()) {
      draw->AddText(text, 36.0f * s, ImVec2(left, y + 14.0f * s), IM_COL32(200, 205, 210, 230),
                    settings_.settings[setting_row_].help.c_str(), nullptr, card_w);
    }
  }

  // The discs.
  for (size_t i = 0; !settings_page_ && i < discs_.size(); ++i) {
    const DiscChoice& disc = discs_[i];
    const bool selected = i == selected_ && disc.present;
    const ImVec2 p0(left, y);
    const ImVec2 p1(left + card_w, y + card_h);
    if (selected) {
      for (int ring = 1; ring <= 3; ++ring) {
        const float grow = 7.0f * s * ring;
        draw->AddRect(ImVec2(p0.x - grow, p0.y - grow), ImVec2(p1.x + grow, p1.y + grow),
                      WithAlpha(kGreen, 0.16f * pulse / ring), round + grow, 0, 4.0f * s);
      }
      draw->AddRectFilled(p0, p1, IM_COL32(10, 26, 12, 215), round);
      draw->AddRect(p0, p1, WithAlpha(kGreen, 0.55f + 0.45f * pulse), round, 0, 3.5f * s);
      draw->AddRectFilled(p0, ImVec2(p0.x + 12.0f * s, p1.y), kGreen, round,
                          ImDrawFlags_RoundCornersLeft);
      const float cx = p1.x - 70.0f * s;
      const float cy = (p0.y + p1.y) * 0.5f;
      draw->AddTriangleFilled(ImVec2(cx - 14.0f * s, cy - 24.0f * s),
                              ImVec2(cx + 18.0f * s, cy), ImVec2(cx - 14.0f * s, cy + 24.0f * s),
                              kGreen);
    } else {
      draw->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, disc.present ? 150 : 110), round);
      draw->AddRect(p0, p1, IM_COL32(255, 255, 255, 28), round, 0, 2.0f * s);
    }
    const ImU32 title_color = !disc.present ? IM_COL32(120, 124, 128, 255)
                              : selected    ? IM_COL32(255, 255, 255, 255)
                                            : IM_COL32(190, 195, 200, 255);
    AddSpacedText(draw, display, 76.0f * s, ImVec2(p0.x + 64.0f * s, p0.y + 30.0f * s),
                  title_color, Upper(disc.title), 6.0f * s);
    const ImVec2 line2(p0.x + 66.0f * s, p0.y + 120.0f * s);
    if (disc.present) {
      draw->AddText(text, 48.0f * s, line2,
                    selected ? kGreen : IM_COL32(160, 166, 172, 255), disc.description.c_str());
    } else {
      const std::string missing = "Not installed - copy it to " + disc.root.string();
      draw->AddText(text, 40.0f * s, line2, IM_COL32(230, 110, 90, 255), missing.c_str());
    }
    y += card_h + 44.0f * s;
  }

  if (task_running_) {
    // The task's progress, where the note was.
    const float bar_y = 1730.0f * s;
    const float shown = std::clamp(task_progress, 0.0f, 1.0f);
    const std::string label = task_.title + "   " + std::to_string(int(shown * 100.0f)) + "%";
    AddSpacedText(draw, text, 42.0f * s, ImVec2(left, bar_y - 70.0f * s), kGreen, label, 6.0f * s);
    draw->AddRectFilled(ImVec2(left, bar_y), ImVec2(left + card_w, bar_y + 16.0f * s),
                        IM_COL32(255, 255, 255, 40), 8.0f * s);
    draw->AddRectFilled(ImVec2(left, bar_y),
                        ImVec2(left + std::max(card_w * shown, 16.0f * s), bar_y + 16.0f * s),
                        WithAlpha(kGreen, 0.75f + 0.25f * pulse), 8.0f * s);
  } else if (!look_.note.empty() && !settings_page_) {
    draw->AddText(text, 40.0f * s, ImVec2(left, 1660.0f * s), IM_COL32(200, 205, 210, 230),
                  look_.note.c_str(), nullptr, card_w);
  }

  if (!task_running_ && !settings_page_) {
    draw->AddText(text, 36.0f * s, ImVec2(left, 1810.0f * s), IM_COL32(150, 156, 162, 255),
                  "In the game, hold OPTIONS + TOUCHPAD for 2 seconds to come back here.");
  }

  // The buttons.
  const float hint_size = 44.0f * s;
  const float hint_y = 1890.0f * s;
  const ImU32 hint_color = IM_COL32(215, 220, 225, 255);
  float x = left;
  if (task_running_) {
    draw->AddText(text, hint_size, ImVec2(x, hint_y), hint_color,
                  "Once only - the game starts when it's done");
  } else if (any_present) {
    // D-pad: a plus.
    const float c = hint_y + hint_size * 0.55f;
    const float arm = 26.0f * s;
    const float half = 9.0f * s;
    draw->AddRectFilled(ImVec2(x, c - half), ImVec2(x + 2 * arm, c + half), hint_color,
                        3.0f * s);
    draw->AddRectFilled(ImVec2(x + arm - half, c - arm), ImVec2(x + arm + half, c + arm),
                        hint_color, 3.0f * s);
    x += 2 * arm + 22.0f * s;
    const char* move = settings_page_ ? "Choose / Change" : "Choose";
    draw->AddText(text, hint_size, ImVec2(x, hint_y), hint_color, move);
    x += text->CalcTextSizeA(hint_size, FLT_MAX, 0.0f, move).x + 80.0f * s;
    // Cross: the PlayStation button.
    const float r = 30.0f * s;
    const ImVec2 o(x + r, c);
    draw->AddCircleFilled(o, r, IM_COL32(20, 22, 26, 230));
    draw->AddCircle(o, r, hint_color, 0, 3.0f * s);
    const float k = r * 0.45f;
    draw->AddLine(ImVec2(o.x - k, o.y - k), ImVec2(o.x + k, o.y + k), kCrossBlue, 5.0f * s);
    draw->AddLine(ImVec2(o.x - k, o.y + k), ImVec2(o.x + k, o.y - k), kCrossBlue, 5.0f * s);
    x += 2 * r + 22.0f * s;
    const char* act = settings_page_ ? "Change" : "Play";
    draw->AddText(text, hint_size, ImVec2(x, hint_y), hint_color, act);
    x += text->CalcTextSizeA(hint_size, FLT_MAX, 0.0f, act).x + 80.0f * s;
    if (settings_page_ && settings_.restore) {
      // Square: the PlayStation button.
      const float sq = 23.0f * s;
      const ImVec2 so(x + r, c);
      draw->AddCircleFilled(so, r, IM_COL32(20, 22, 26, 230));
      draw->AddCircle(so, r, hint_color, 0, 3.0f * s);
      draw->AddRect(ImVec2(so.x - sq * 0.5f, so.y - sq * 0.5f), ImVec2(so.x + sq * 0.5f, so.y + sq * 0.5f),
                    IM_COL32(230, 140, 200, 255), 2.0f * s, 0, 4.0f * s);
      x += 2 * r + 22.0f * s;
      draw->AddText(text, hint_size, ImVec2(x, hint_y), hint_color, "Recommended");
      x += text->CalcTextSizeA(hint_size, FLT_MAX, 0.0f, "Recommended").x + 80.0f * s;
    }
    if (!settings_.settings.empty()) {
      x += badge(x, hint_y - 4.0f * s, "L1") + 12.0f * s;
      x += badge(x, hint_y - 4.0f * s, "R1") + 22.0f * s;
      draw->AddText(text, hint_size, ImVec2(x, hint_y), hint_color,
                    settings_page_ ? "Discs" : "Settings");
    }
  } else {
    draw->AddText(text, hint_size, ImVec2(x, hint_y), IM_COL32(230, 110, 90, 255),
                  "No disc installed: see game/README.txt in the app folder");
  }

  // Fading in from black.
  const float fade = 1.0f - std::min(t / 0.8f, 1.0f);
  if (fade > 0.0f) {
    draw->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(0, 0, 0, static_cast<int>(255 * fade)));
  }

  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);

  if (start) {
    picked_ = true;
    REXLOG_INFO("Launcher: starting {} ({})", discs_[selected_].title,
                discs_[selected_].root.string());
    on_pick_(discs_[selected_]);
    Close();
  }
}
