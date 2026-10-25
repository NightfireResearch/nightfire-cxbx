#ifndef LAUNCHER_PLATFORM_H_
#define LAUNCHER_PLATFORM_H_

// What the launcher asks of the system: its folders, the system's file and web handlers, the game's process.
// PlatformWin32.cpp answers on Windows. A macOS or Linux launcher adds its own file (PlatformPosix.cpp) with the same
// functions: ~/Library/Application Support/Nightfire/ or $XDG_DATA_HOME/nightfire/, open / xdg-open, posix_spawn
// with a process group in place of the job object.

#include <stdint.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Paths and UTF-8 text, both ways: the window and the JSON files are UTF-8
std::string ToUtf8(const fs::path &path);
fs::path FromUtf8(const std::string &text);

// The per-user folder everything lives in (docs/install-and-update.md, "Folder layout"): %LOCALAPPDATA%\Nightfire
fs::path Platform_DataRoot();

// This program's own file
fs::path Platform_ExecutablePath();

// The release's platform name, as manifest.json and the zips name it: "windows-x86"
const char *Platform_Name();

// The executables' suffix: ".exe" on Windows
const char *Platform_ExecutableSuffix();

// A short description of the system for a problem report: "Windows 10.0.26200"
std::string Platform_Description();

// A folder or file in the system's handler (Explorer, the .ini editor), a URL in the browser
bool Platform_OpenPath(const fs::path &path);
bool Platform_OpenUrl(const std::string &url);

// A path the game's narrow-character file calls can open: on Windows a path outside the ANSI code page is given as
// its short (8.3) form
std::string Platform_GamePath(const fs::path &path);

// The game's process, and any it starts (the other engine, through src/common/launchInfo.cpp): a job object on
// Windows, so that "running" lasts across the switch between engines
struct GameProcess {
    void *job = nullptr;
    void *process = nullptr;
    uint32_t id = 0;
};

// Starts executable with its arguments in workingDirectory, its standard output and error appended to logFile
bool Platform_StartGame(const fs::path &executable, const std::vector<std::string> &arguments,
                        const fs::path &workingDirectory, const fs::path &logFile, GameProcess *game,
                        std::string *error);
// Whether any of its processes is still running; the first process's exit code once none is
bool Platform_GameRunning(const GameProcess &game, int *exitCode);
// Its windows asked to close, then, after a few seconds, the processes ended
void Platform_StopGame(const GameProcess &game);
void Platform_CloseGame(GameProcess *game);

// Starts another copy of a program (the launcher after it replaced itself), not waiting for it
bool Platform_StartDetached(const fs::path &executable, const std::vector<std::string> &arguments);

// Shows a message box without a window of the launcher's own (a failure before the window opens)
void Platform_ShowError(const char *title, const char *message);

// For --selftest and --version from a terminal: this program has no console of its own on Windows
void Platform_AttachConsole();

#endif // LAUNCHER_PLATFORM_H_
