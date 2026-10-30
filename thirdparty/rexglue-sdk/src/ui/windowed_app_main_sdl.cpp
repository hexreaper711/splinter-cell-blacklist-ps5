/**
 * @file        ui/windowed_app_main_sdl.cpp
 * @brief       Entry point for windowed applications (SDL3 windowing)
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_sdl.h>

#if REX_PLATFORM_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

#if REX_PLATFORM_PS5
#include <cstdio>
#include <cstring>
#include <exception>
#include <typeinfo>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <unistd.h>

#include <rex/ps5_diag.h>

extern "C" int sceKernelSendNotificationRequest(int, void*, size_t, int);
#endif

namespace {

#if REX_PLATFORM_PS5
// [ps5 port] (from gta4-ps5) A system notification: on the PS5 nobody sees stderr.
void Ps5Notify(const char* text) {
  struct {
    char unused[45];
    char message[3075];
  } request;
  std::memset(&request, 0, sizeof(request));
  std::strncpy(request.message, text, sizeof(request.message) - 1);
  sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

// Before the log exists: the reason on screen and in logs/startup_error.log.
void Ps5EarlyFailure(const char* what) {
  char text[600];
  std::snprintf(text, sizeof(text), "%s: %s", what, SDL_GetError());
  Ps5Notify(text);
  mkdir("/app0/logs", 0777);
  if (std::FILE* f = std::fopen("/app0/logs/startup_error.log", "w")) {
    std::fprintf(f, "%s\n", text);
    std::fclose(f);
  }
}

// An uncaught C++ exception ends in abort() with its message on stderr: put it in the log and on
// screen first.
void Ps5Terminate() {
  char text[600] = "Uncaught exception";
  if (auto current = std::current_exception()) {
    try {
      std::rethrow_exception(current);
    } catch (const std::exception& e) {
      std::snprintf(text, sizeof(text), "Uncaught %s: %s", typeid(e).name(), e.what());
    } catch (...) {
    }
  }
  REXLOG_ERROR("{}", text);
  Ps5Notify(text);
  sleep(2);  // (let the notification show)
  std::abort();
}
#endif

int RunWindowedApp(int argc, char** argv) {
  auto remaining = rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  int result;
  {
#if REX_PLATFORM_PS5
    // [ps5 port] SDL has no PS5 video driver: its dummy driver keeps windows and events working;
    // the picture goes to the TV through VK_KHR_display.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
#endif
    rex::ui::SDLWindowedAppContext app_context;
    if (!app_context.Initialize()) {
#if REX_PLATFORM_PS5
      Ps5EarlyFailure("SDL didn't start");
#endif
      return EXIT_FAILURE;
    }

#if REX_PLATFORM_WIN32
    // Apartment-threaded COM for shell dialogs.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
      return EXIT_FAILURE;
    }
#endif

    std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);

    // Match remaining positional args to the app's expected options.
    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    result = app->OnInitialize() ? app_context.RunMainMessageLoop() : EXIT_FAILURE;
#if REX_PLATFORM_PS5
    // [ps5 port] Skip the teardown: guest threads can't be stopped reliably.
    rex::FlushLogging();
    std::_Exit(result);
#endif

    app->InvokeOnDestroy();
  }

#if REX_PLATFORM_WIN32
  CoUninitialize();
#endif

  return result;
}

#if REX_PLATFORM_WIN32
// Convert wide argv from CommandLineToArgvW to UTF-8 for cvar::Init.
std::vector<std::string> WideArgsToUtf8(int argc, wchar_t** wargv) {
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    std::wstring wide(wargv[i]);
    if (wide.empty()) {
      args.emplace_back();
      continue;
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr,
                                   0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), size,
                        nullptr, nullptr);
    args.push_back(std::move(utf8));
  }
  return args;
}
#endif

}  // namespace

#if REX_PLATFORM_WIN32

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hinstance_prev, LPWSTR command_line,
                    int show_cmd) {
  (void)hinstance;
  (void)hinstance_prev;
  (void)command_line;
  (void)show_cmd;

  int wargc = 0;
  wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  auto utf8_args = WideArgsToUtf8(wargc, wargv);
  LocalFree(wargv);

  std::vector<char*> argv_ptrs;
  argv_ptrs.reserve(utf8_args.size());
  for (auto& s : utf8_args) {
    argv_ptrs.push_back(s.data());
  }
  return RunWindowedApp(static_cast<int>(argv_ptrs.size()), argv_ptrs.data());
}

#elif REX_PLATFORM_PS5

// [ps5 port] (from gta4-ps5, without its hub and GTA IV monitors) Work in the app's folder (logs/
// and the user data go there; the process starts elsewhere, often read-only), and close the game
// with a notification when a thread crashes instead of leaving it hanging.
int main(int argc, char* argv[]) {
  chdir("/app0");
  rex::ps5diag::RegisterThread("main");
  // (It runs in the fault handler, maybe inside the allocator: no logging, which allocates. The
  // crash report is already at the end of the log file.)
  rex::ps5diag::crash_hook = [](const char*) {
    Ps5Notify("The game crashed - send /app0/logs to the developer");
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& l) { l->flush(); });
    sleep(2);
    _exit(1);
  };
  std::set_terminate(Ps5Terminate);
  rex::ps5diag::StartMonitor();
  // The game quit to the dashboard: the PS5's home screen is the nearest thing.
  rex::ps5diag::title_exit_hook = [] {
    rex::FlushLogging();
    _exit(0);
  };
  int result = RunWindowedApp(argc, argv);
  if (result != 0) {
    Ps5Notify("Stopped with an error - see /app0/logs");
    sleep(2);  // (let the notification show)
  }
  // Leave without the C runtime's exit handlers (with the game's threads still around, they end
  // in a crash report).
  _exit(result);
}

#else

int main(int argc, char* argv[]) {
  return RunWindowedApp(argc, argv);
}

#endif
