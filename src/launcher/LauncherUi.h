#ifndef LAUNCHER_LAUNCHERUI_H_
#define LAUNCHER_LAUNCHERUI_H_

// The launcher's window, drawn with Dear ImGui each frame: first-time setup until there is game data, then the tabs
// Play, Updates, Settings and Help (docs/install-and-update.md, "The launcher").
//
// Planned (docs/install-and-update.md, "Where it stands"): a Mods tab, and a profile choice beside Play; a choice of
// game data (the PS2 and GameCube builds) as a setup source and in the Settings tab's Game data section.

#include <string>

#include "IniFile.h"
#include "Launcher.h"

struct SDL_Window;

class LauncherUi {
public:
    LauncherUi(Launcher &launcher, SDL_Window *window);

    void Draw();
    void SetNotice(const std::string &notice) { m_notice = notice; }
    bool QuitWanted() const { return m_quit; }
    void ShowTab(const std::string &tab) { m_selectTab = tab; }   // for --screenshot

    // A folder chosen in the system's dialog (SDL's callback, on the window's thread)
    void FolderChosen(const std::string &folder);

private:
    void DrawSetup();
    void DrawPlay();
    void DrawUpdates();
    void DrawSettings();
    void DrawHelp();
    void DrawJob();
    void DrawReport();
    void LoadSettings();
    void SaveSettings();

    Launcher &m_launcher;
    SDL_Window *m_window;
    std::string m_notice;
    std::string m_jobMessage;
    bool m_jobOk = true;
    bool m_quit = false;
    std::string m_selectTab;

    // Setup
    char m_discFolder[1024] = {};
    int m_copyData = 1;   // 1: copy into disc/, 0: use the folder in place
    bool m_changingData = false;

    // Play
    char m_level[64] = {};
    bool m_wasRunning = false;
    std::string m_gameMessage;

    // Settings: the settings.ini keys the window shows
    IniFile m_settings;
    bool m_settingsLoaded = false;
    int m_width = 640, m_height = 480;
    bool m_widescreen = false;
    int m_language = 0;
    int m_region = 0;
    std::string m_settingsMessage;

    // Report a problem
    bool m_reportOpen = false;
    char m_reportTitle[256] = {};
    std::string m_reportText;
};

#endif // LAUNCHER_LAUNCHERUI_H_
