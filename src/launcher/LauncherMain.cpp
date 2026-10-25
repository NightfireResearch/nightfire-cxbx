// The launcher (docs/install-and-update.md): one window, SDL3 with Dear ImGui on SDL's renderer.
//
//   launcher                          the window, on the per-user folder (Platform_DataRoot)
//   launcher --root DIR               another folder (tests)
//   launcher --api-base URL           the releases API's address in place of GitHub's (tools/release/fake_releases.py)
//   launcher --repository OWNER/NAME  the repository to ask for releases
//   launcher --no-update-check        no check at start
//   launcher --finish-update          the check and the install at start: an update put this launcher in place first
//                                     (the release's minLauncher was above the one before)
//   launcher --screenshot FILE.bmp [--tab NAME]   the window drawn, saved, and closed
//   launcher --selftest ...           the scripted test (SelfTest.cpp)
//   launcher --version

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Http.h"
#include "Launcher.h"
#include "LauncherUi.h"
#include "SelfTest.h"
#include "Signature.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "../common/version.h"

namespace {

struct Options {
    fs::path root;
    std::string apiBase;
    std::string repository;
    bool updateCheck = true;
    bool finishUpdate = false;
    std::string screenshot;
    std::string tab;
    std::vector<std::string> passOn;   // what a restarted launcher is given again
};

void LoadFont(float scale) {
    ImGuiIO &io = ImGui::GetIO();
    const float size = 17.0f * scale;
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (windows != nullptr) {
        std::string font = std::string(windows) + "\\Fonts\\segoeui.ttf";
        if (SDL_GetPathInfo(font.c_str(), nullptr) && io.Fonts->AddFontFromFileTTF(font.c_str(), size) != nullptr)
            return;
    }
#endif
    ImFontConfig config;
    config.SizePixels = size;
    io.Fonts->AddFontDefault(&config);
}

void Style(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(16, 14);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.FrameRounding = 4;
    style.ChildRounding = 4;
    style.PopupRounding = 4;
    style.ScaleAllSizes(scale);
}

bool SaveScreenshot(SDL_Renderer *renderer, const std::string &file) {
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr)
        return false;
    bool ok = SDL_SaveBMP(surface, file.c_str());
    SDL_DestroySurface(surface);
    return ok;
}

int RunWindow(const Options &options) {
    fs::path root = options.root.empty() ? Platform_DataRoot() : options.root;
    if (root.empty()) {
        Platform_ShowError("Nightfire", "The per-user folder (LOCALAPPDATA) could not be found.");
        return 1;
    }
    Launcher launcher(root, NightfireVersion);
    std::string notice;
    if (!launcher.Start(&notice)) {
        Platform_ShowError("Nightfire", notice.c_str());
        return 1;
    }
    LauncherConfig &config = launcher.GetInstallation().Config();
    if (!options.apiBase.empty())
        config.apiBase = options.apiBase;
    if (!options.repository.empty())
        config.repository = options.repository;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        Platform_ShowError("Nightfire", SDL_GetError());
        return 1;
    }
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0)
        scale = 1;
    SDL_Window *window = SDL_CreateWindow("Nightfire", int(760 * scale), int(600 * scale),
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer *renderer = window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (renderer == nullptr) {
        Platform_ShowError("Nightfire", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    Style(scale);
    LoadFont(scale);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    LauncherUi ui(launcher, window);
    ui.SetNotice(notice);
    if (!options.tab.empty())
        ui.ShowTab(options.tab);
    if (options.finishUpdate)
        launcher.FinishUpdate();
    else if (options.updateCheck && launcher.GetInstallation().HasGameData() && Signature_HaveKey())
        launcher.CheckForUpdates();

    bool running = true;
    int frames = 0;
    int result = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)))
                running = false;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ui.Draw();
        ImGui::Render();
        SDL_SetRenderScale(renderer, ImGui::GetIO().DisplayFramebufferScale.x, ImGui::GetIO().DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(renderer, 0.08f, 0.08f, 0.09f, 1.0f);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        // A few frames after any job (the check at start) has ended: ImGui sizes its windows on the first ones
        if (launcher.Busy())
            frames = 0;
        if (!options.screenshot.empty() && ++frames == 10) {
            if (!SaveScreenshot(renderer, options.screenshot))
                result = 1;
            running = false;
        }
        SDL_RenderPresent(renderer);
        if (ui.QuitWanted())
            running = false;

        // A new launcher replaced this one: it starts, and this one ends
        if (launcher.RestartWanted() && !launcher.Busy()) {
            std::vector<std::string> arguments = options.passOn;
            if (launcher.RestartFinishesUpdate())
                arguments.push_back("--finish-update");
            Platform_StartDetached(launcher.GetInstallation().LauncherFile(), arguments);
            running = false;
        }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

} // namespace

int main(int argc, char **argv) {
    Http_SetUserAgent(std::string("NightfireLauncher/") + NightfireVersion);
    Options options;
    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];
        auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (argument == "--selftest") {
            Platform_AttachConsole();
            return SelfTest_Run(argc, argv);
        } else if (argument == "--version") {
            Platform_AttachConsole();
            printf("Nightfire launcher %s (%s)%s\n", NightfireVersion, Platform_Name(),
                   Signature_TestKeyBuild() ? ", TEST KEY BUILD" : "");
            return 0;
        } else if (argument == "--root") {
            options.root = FromUtf8(value());
            options.passOn.insert(options.passOn.end(), {argument, ToUtf8(options.root)});
        } else if (argument == "--api-base") {
            options.apiBase = value();
            options.passOn.insert(options.passOn.end(), {argument, options.apiBase});
        } else if (argument == "--repository") {
            options.repository = value();
            options.passOn.insert(options.passOn.end(), {argument, options.repository});
        } else if (argument == "--no-update-check") {
            options.updateCheck = false;
        } else if (argument == "--finish-update") {
            options.finishUpdate = true;
        } else if (argument == "--screenshot") {
            options.screenshot = value();
        } else if (argument == "--tab") {
            options.tab = value();
        }
    }
    return RunWindow(options);
}
