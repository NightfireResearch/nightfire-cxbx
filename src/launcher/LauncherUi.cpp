#include "LauncherUi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>

#include <SDL3/SDL.h>

#include "Signature.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace {

const char *const kLanguages[] = {"English", "French", "German", "Spanish", "Italian", "Japanese"};
const char *const kRegions[] = {"PAL", "NTSC"};

struct Resolution {
    int width, height;
};
const Resolution kStandard[] = {{640, 480}, {960, 720}, {1280, 960}, {1440, 1080}, {1920, 1440}, {2880, 2160}};
const Resolution kWide[] = {{854, 480}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

const ImVec4 kRed(0.95f, 0.35f, 0.3f, 1.0f);
const ImVec4 kGreen(0.4f, 0.85f, 0.45f, 1.0f);
const ImVec4 kGrey(0.65f, 0.65f, 0.65f, 1.0f);

std::mutex g_chosenMutex;
std::string g_chosenFolder;

void SDLCALL SourceChosen(void *userdata, const char *const *files, int) {
    if (files == nullptr || files[0] == nullptr)
        return;
    std::lock_guard<std::mutex> lock(g_chosenMutex);
    g_chosenFolder = files[0];
    (void)userdata;
}

int IndexOf(const char *const *names, int count, const std::string &value, int fallback) {
    for (int i = 0; i < count; i++)
        if (SDL_strcasecmp(names[i], value.c_str()) == 0)
            return i;
    return fallback;
}

void Heading(const char *text) {
    ImGui::Spacing();
    ImGui::SeparatorText(text);
}

} // namespace

LauncherUi::LauncherUi(Launcher &launcher, SDL_Window *window) : m_launcher(launcher), m_window(window) {}

void LauncherUi::FolderChosen(const std::string &folder) {
    snprintf(m_discFolder, sizeof(m_discFolder), "%s", folder.c_str());
}

void LauncherUi::Draw() {
    {
        std::lock_guard<std::mutex> lock(g_chosenMutex);
        if (!g_chosenFolder.empty()) {
            FolderChosen(g_chosenFolder);
            g_chosenFolder.clear();
        }
    }
    JobStatus finished;
    if (m_launcher.TakeFinished(&finished)) {
        m_jobMessage = finished.message;
        m_jobOk = finished.ok;
        if (finished.ok && m_launcher.GetInstallation().HasGameData())
            m_changingData = false;
        m_settingsLoaded = false;   // setup or an update may have written settings.ini
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Nightfire", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::Text("007: Nightfire");
    ImGui::SameLine();
    ImGui::TextColored(kGrey, "launcher %s", m_launcher.Version().c_str());
    if (Signature_TestKeyBuild())
        ImGui::TextColored(kRed, "TEST BUILD: trusts the test signing key. Never give this launcher to players.");
    else if (!Signature_HaveKey())
        ImGui::TextColored(kRed, "Built without the release key: updates cannot be checked.");
    if (!m_notice.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(kGreen, "%s", m_notice.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();

    if (!m_launcher.GetInstallation().HasGameData() || m_changingData) {
        DrawSetup();
    } else if (ImGui::BeginTabBar("tabs")) {
        auto tab = [&](const char *name, void (LauncherUi::*draw)()) {
            ImGuiTabItemFlags flags = m_selectTab == name ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(name, nullptr, flags)) {
                (this->*draw)();
                ImGui::EndTabItem();
            }
        };
        tab("Play", &LauncherUi::DrawPlay);
        tab("Updates", &LauncherUi::DrawUpdates);
        tab("Settings", &LauncherUi::DrawSettings);
        tab("Help", &LauncherUi::DrawHelp);
        m_selectTab.clear();
        ImGui::EndTabBar();
    }
    DrawReport();
    ImGui::End();
}

void LauncherUi::DrawJob() {
    JobStatus status = m_launcher.Status();
    if (status.running) {
        ImGui::Text("%s...", status.name.c_str());
        if (status.fraction >= 0)
            ImGui::ProgressBar(float(status.fraction), ImVec2(-1, 0));
        if (!status.step.empty())
            ImGui::TextColored(kGrey, "%s", status.step.c_str());
        if (ImGui::Button("Cancel"))
            m_launcher.Cancel();
    } else if (!m_jobMessage.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(m_jobOk ? kGreen : kRed, "%s", m_jobMessage.c_str());
        ImGui::PopTextWrapPos();
    }
}

void LauncherUi::DrawSetup() {
    Installation &installation = m_launcher.GetInstallation();
    bool busy = m_launcher.Busy();
    Heading("First-time setup");
    ImGui::TextWrapped("You need your own copy of 007: Nightfire for the original Xbox: a disc image (.iso), or the "
                       "folder an image was extracted to, which holds the disc's eurocom and driving folders. About "
                       "3.2 GB is copied. Nothing is downloaded: the game's data never comes from us.");
    if (ImGui::TextLink("What you need (README)"))
        Platform_OpenUrl("https://github.com/" + installation.Config().repository + "#what-you-need");
    ImGui::Spacing();

    ImGui::BeginDisabled(busy);
    ImGui::Text("The disc image or the extracted disc's folder:");
    float browseWidth = ImGui::CalcTextSize("Disc image...").x + ImGui::CalcTextSize("Folder...").x +
                        ImGui::GetStyle().FramePadding.x * 4 + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetNextItemWidth(-browseWidth);
    ImGui::InputText("##source", m_discFolder, sizeof(m_discFolder));
    ImGui::SameLine();
    if (ImGui::Button("Disc image...")) {
        static const SDL_DialogFileFilter kFilters[] = {{"Xbox disc images", "iso;xiso"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(SourceChosen, nullptr, m_window, kFilters, SDL_arraysize(kFilters),
                               m_discFolder[0] ? m_discFolder : nullptr, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Folder..."))
        SDL_ShowOpenFolderDialog(SourceChosen, nullptr, m_window, m_discFolder[0] ? m_discFolder : nullptr, false);

    bool image = m_discFolder[0] && DiscSetup_IsImage(FromUtf8(m_discFolder));
    ImGui::BeginDisabled(image);
    int copyData = image ? 1 : m_copyData;
    ImGui::RadioButton("Copy the data into the launcher's folder", &copyData, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Use the folder where it is", &copyData, 0);
    if (!image)
        m_copyData = copyData;
    ImGui::EndDisabled();
    if (copyData)
        ImGui::TextColored(kGrey, "Copied to %s", ToUtf8(installation.DiscFolder()).c_str());

    if (ImGui::Button(image ? "Check the image" : "Check the folder") && m_discFolder[0])
        m_launcher.CheckDisc(FromUtf8(m_discFolder));
    ImGui::SameLine();
    if (ImGui::Button(copyData ? "Copy and set up" : "Set up") && m_discFolder[0])
        m_launcher.SetUpDisc(FromUtf8(m_discFolder), copyData != 0);
    if (m_changingData) {
        ImGui::SameLine();
        if (ImGui::Button("Back"))
            m_changingData = false;
    }
    ImGui::EndDisabled();

    DiscCheck check = m_launcher.LastDiscCheck();
    if (check.usable || !check.problems.empty()) {
        Heading("The data");
        if (check.usable)
            ImGui::TextColored(check.release.empty() ? kRed : kGreen, "%s",
                               check.release.empty() ? "Not a version we have tested"
                                                     : ("Recognised: " + check.release +
                                                        (check.confirmed ? "" : " (by its files' sizes; their "
                                                                                "contents are checked after the copy)"))
                                                           .c_str());
        if (check.bytes > 0)
            ImGui::Text("%.1f GB to copy", double(check.bytes) / 1e9);
        for (const std::string &problem : check.problems) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(kRed, "- %s", problem.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Spacing();
    DrawJob();
}

void LauncherUi::DrawPlay() {
    Installation &installation = m_launcher.GetInstallation();
    const LauncherConfig &config = installation.Config();
    int exitCode = 0;
    bool running = m_launcher.GameRunning(&exitCode);
    if (m_wasRunning && !running)
        m_gameMessage = exitCode == 0 ? "The game has ended."
                                      : "The game ended with code " + std::to_string(exitCode) +
                                            ". If that was not you closing it, please report a problem (Help).";
    m_wasRunning = running;

    ImGui::Spacing();
    if (installation.HasGame())
        ImGui::Text("Installed: version %s", config.installed.c_str());
    else
        ImGui::TextColored(kRed, "No version of the game is installed: check for updates (Updates).");
    ImGui::Text("Game data: %s", config.discRelease.empty() ? "a version we have not tested" : config.discRelease.c_str());
    ImGui::Spacing();

    ImGui::BeginDisabled(running || !installation.HasGame() || m_launcher.Busy());
    if (ImGui::Button(running ? "Running..." : "Play", ImVec2(200, 48))) {
        std::string error;
        m_gameMessage = m_launcher.Play(m_level, &error) ? "" : "Could not start the game: " + error;
    }
    ImGui::EndDisabled();
    if (running) {
        ImGui::SameLine();
        if (ImGui::Button("Stop the game", ImVec2(0, 48)))
            m_launcher.StopGame();
    }
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("Start in level (optional)", "-level, e.g. 0x07010008", m_level, sizeof(m_level));
    if (!m_gameMessage.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(m_gameMessage.find("Could not") == 0 || m_gameMessage.find("code") != std::string::npos
                               ? kRed
                               : kGreen,
                           "%s", m_gameMessage.c_str());
        ImGui::PopTextWrapPos();
    }

    Heading("Folders");
    if (ImGui::Button("Open the saves"))
        Platform_OpenPath(installation.UserFolder() / "saves");
    ImGui::SameLine();
    if (ImGui::Button("Open the logs"))
        Platform_OpenPath(installation.LogFolder());
    ImGui::SameLine();
    if (ImGui::Button("Open the game's folder"))
        Platform_OpenPath(installation.UserFolder());
    ImGui::Spacing();
    DrawJob();
}

void LauncherUi::DrawUpdates() {
    Installation &installation = m_launcher.GetInstallation();
    LauncherConfig &config = installation.Config();
    bool busy = m_launcher.Busy();
    ImGui::Spacing();
    ImGui::Text("Installed: %s", config.installed.empty() ? "nothing yet" : config.installed.c_str());

    ImGui::BeginDisabled(busy);
    int channel = config.channel == Channel::Development ? 1 : 0;
    bool changed = ImGui::RadioButton("Stable (releases)", &channel, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Development (pre-releases)", &channel, 1);
    if (changed) {
        config.channel = channel == 1 ? Channel::Development : Channel::Stable;
        std::string error;
        installation.Save(&error);
    }
    if (ImGui::Button("Check for updates"))
        m_launcher.CheckForUpdates();

    UpdateCheck check = m_launcher.LastCheck();
    if (check.ok) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!check.newer);
        if (ImGui::Button(check.newer ? ("Update to " + check.release.version).c_str() : "Up to date"))
            m_launcher.InstallUpdate();
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    std::error_code ec;
    bool canUndo = !config.previous.empty() && fs::exists(installation.GameExecutable(config.previous), ec);
    ImGui::BeginDisabled(!canUndo);
    if (ImGui::Button(canUndo ? ("Undo the last update (back to " + config.previous + ")").c_str()
                              : "Undo the last update")) {
        std::string error;
        m_jobOk = m_launcher.Undo(&error);
        m_jobMessage = m_jobOk ? "Back to version " + config.installed + "." : error;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::Spacing();
    DrawJob();

    if (check.ok) {
        Heading((check.release.title + (check.release.prerelease ? " (pre-release)" : "")).c_str());
        ImGui::BeginChild("notes", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(check.release.notes.empty() ? "(no release notes)" : check.release.notes.c_str());
        ImGui::PopTextWrapPos();
        if (!check.release.page.empty() && ImGui::TextLink("The release on GitHub"))
            Platform_OpenUrl(check.release.page);
        ImGui::EndChild();
    }
}

void LauncherUi::LoadSettings() {
    m_settings.Load(m_launcher.GetInstallation().SettingsFile());
    m_width = atoi(m_settings.Get("RenderWidth", "640").c_str());
    m_height = atoi(m_settings.Get("RenderHeight", "480").c_str());
    m_widescreen = atoi(m_settings.Get("Widescreen", "0").c_str()) != 0;
    m_language = IndexOf(kLanguages, IM_ARRAYSIZE(kLanguages), m_settings.Get("Language", "English"), 0);
    m_region = IndexOf(kRegions, IM_ARRAYSIZE(kRegions), m_settings.Get("Region", "PAL"), 0);
    m_settingsLoaded = true;
}

void LauncherUi::SaveSettings() {
    m_settings.Load(m_launcher.GetInstallation().SettingsFile());   // edits made in the file meanwhile are kept
    m_settings.Set("RenderWidth", std::to_string(m_width));
    m_settings.Set("RenderHeight", std::to_string(m_height));
    m_settings.Set("Widescreen", m_widescreen ? "1" : "0");
    m_settings.Set("Language", kLanguages[m_language]);
    m_settings.Set("Region", kRegions[m_region]);
    m_settingsMessage = m_settings.Save(m_launcher.GetInstallation().SettingsFile())
                            ? "Saved. The game reads them when it starts."
                            : "Could not write settings.ini.";
}

void LauncherUi::DrawSettings() {
    Installation &installation = m_launcher.GetInstallation();
    if (!m_settingsLoaded)
        LoadSettings();
    ImGui::Spacing();
    ImGui::Checkbox("Widescreen (16:9)", &m_widescreen);

    const Resolution *list = m_widescreen ? kWide : kStandard;
    int count = m_widescreen ? IM_ARRAYSIZE(kWide) : IM_ARRAYSIZE(kStandard);
    char current[32];
    snprintf(current, sizeof(current), "%d x %d", m_width, m_height);
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("Render resolution", current)) {
        for (int i = 0; i < count; i++) {
            char label[32];
            snprintf(label, sizeof(label), "%d x %d", list[i].width, list[i].height);
            if (ImGui::Selectable(label, list[i].width == m_width && list[i].height == m_height)) {
                m_width = list[i].width;
                m_height = list[i].height;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextColored(kGrey, "The menus and the HUD stay laid out for 640 x 480 at any resolution.");
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("Language", &m_language, kLanguages, IM_ARRAYSIZE(kLanguages));
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("Region", &m_region, kRegions, IM_ARRAYSIZE(kRegions));
    const std::string &release = installation.Config().discRelease;
    if ((release == "PAL" && m_region != 0) || (release.compare(0, 4, "NTSC") == 0 && m_region != 1))
        ImGui::TextColored(kRed, "Your disc is %s: the background movies need Region=%s.", release.c_str(),
                           release == "PAL" ? "PAL" : "NTSC");
    if (release.compare(0, 4, "NTSC") == 0 && m_language != 0)
        ImGui::TextColored(kRed, "Your disc has the driving missions' speech in English only.");

    if (ImGui::Button("Save"))
        SaveSettings();
    ImGui::SameLine();
    if (ImGui::Button("Open the settings file"))
        Platform_OpenPath(installation.SettingsFile());
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
        m_settingsLoaded = false;
    if (!m_settingsMessage.empty())
        ImGui::TextColored(kGreen, "%s", m_settingsMessage.c_str());

    Heading("Game data");
    ImGui::Text("%s", installation.Config().discPath.c_str());
    ImGui::BeginDisabled(m_launcher.Busy());
    if (ImGui::Button("Check the game data"))
        m_launcher.CheckDisc(FromUtf8(installation.Config().discPath));
    ImGui::SameLine();
    if (ImGui::Button("Use other game data...")) {
        m_changingData = true;
        m_discFolder[0] = '\0';
    }
    ImGui::EndDisabled();
    DiscCheck check = m_launcher.LastDiscCheck();
    if (check.usable)
        ImGui::TextColored(check.release.empty() ? kRed : kGreen, "%s",
                           check.release.empty() ? "Not a version we have tested" : ("Recognised: " + check.release).c_str());
    ImGui::Spacing();
    DrawJob();
}

void LauncherUi::DrawHelp() {
    Installation &installation = m_launcher.GetInstallation();
    ImGui::Spacing();
    if (ImGui::Button("Report a problem...")) {
        m_reportText = m_launcher.ProblemReport();
        m_reportTitle[0] = '\0';
        m_reportOpen = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open the logs"))
        Platform_OpenPath(installation.LogFolder());
    Heading("About");
    ImGui::Text("Launcher %s, for %s", m_launcher.Version().c_str(), Platform_Name());
    ImGui::Text("Folder: %s", ToUtf8(installation.Root()).c_str());
    if (ImGui::TextLink(("github.com/" + installation.Config().repository).c_str()))
        Platform_OpenUrl("https://github.com/" + installation.Config().repository);
    ImGui::TextWrapped("This program and the game reimplementation contain no game data. Built with SDL3, Dear "
                       "ImGui, miniz and Monocypher (their licences are in the release's licences folder); the game "
                       "uses FFmpeg.");
}

void LauncherUi::DrawReport() {
    if (m_reportOpen) {
        ImGui::OpenPopup("Report a problem");
        m_reportOpen = false;
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.85f));
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.05f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.075f));
    if (!ImGui::BeginPopupModal("Report a problem", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return;
    ImGui::TextWrapped("This text goes into a new issue on GitHub, which opens in your browser for you to check and "
                       "send (you need a GitHub account). Nothing is sent until you do. Edit anything you like.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##title", "A title: what went wrong, in a few words", m_reportTitle,
                             sizeof(m_reportTitle));
    ImGui::InputTextMultiline("##report", &m_reportText, ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() * 1.3f));
    if (ImGui::Button("Open in the browser")) {
        Platform_OpenUrl(m_launcher.IssueUrl(m_reportTitle[0] ? m_reportTitle : "A problem report", m_reportText));
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy the text"))
        ImGui::SetClipboardText(m_reportText.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
