#include "Platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <stdio.h>

namespace {

std::wstring Widen(const std::string &text) {
    if (text.empty())
        return std::wstring();
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring wide(size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), length);
    return wide;
}

std::string Narrow(const std::wstring &wide, UINT codePage = CP_UTF8) {
    if (wide.empty())
        return std::string();
    int length = WideCharToMultiByte(codePage, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string text(size_t(length), '\0');
    WideCharToMultiByte(codePage, 0, wide.data(), int(wide.size()), text.data(), length, nullptr, nullptr);
    return text;
}

std::string LastErrorText(DWORD error) {
    wchar_t *message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    std::string text = message != nullptr ? Narrow(message) : "error " + std::to_string(error);
    LocalFree(message);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

// One argument as CommandLineToArgvW reads it back
void AppendQuoted(std::wstring &line, const std::wstring &argument) {
    if (!line.empty())
        line += L' ';
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += argument;
        return;
    }
    line += L'"';
    for (size_t i = 0;; i++) {
        size_t backslashes = 0;
        while (i < argument.size() && argument[i] == L'\\') {
            i++;
            backslashes++;
        }
        if (i == argument.size()) {
            line.append(backslashes * 2, L'\\');
            break;
        }
        if (argument[i] == L'"') {
            line.append(backslashes * 2 + 1, L'\\');
        } else {
            line.append(backslashes, L'\\');
        }
        line += argument[i];
    }
    line += L'"';
}

std::wstring CommandLine(const fs::path &executable, const std::vector<std::string> &arguments) {
    std::wstring line;
    AppendQuoted(line, executable.wstring());
    for (const std::string &argument : arguments)
        AppendQuoted(line, Widen(argument));
    return line;
}

} // namespace

std::string ToUtf8(const fs::path &path) {
    return Narrow(path.wstring());
}

fs::path FromUtf8(const std::string &text) {
    return fs::path(Widen(text));
}

fs::path Platform_DataRoot() {
    wchar_t *folder = nullptr;
    fs::path root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
        root = fs::path(folder) / L"Nightfire";
    CoTaskMemFree(folder);
    return root;
}

fs::path Platform_ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (length < path.size()) {
            path.resize(length);
            return fs::path(path);
        }
        path.resize(path.size() * 2);
    }
}

const char *Platform_Name() {
#if defined(_M_X64) || defined(__x86_64__)
    return "windows-x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "windows-arm64";
#else
    return "windows-x86";
#endif
}

const char *Platform_ExecutableSuffix() {
    return ".exe";
}

std::string Platform_Description() {
    // GetVersionEx answers what the manifest claims; ntdll's RtlGetVersion answers the truth
    typedef LONG(WINAPI * RtlGetVersionFunction)(OSVERSIONINFOW *);
    OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    auto rtlGetVersion =
        reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (rtlGetVersion == nullptr || rtlGetVersion(&info) != 0)
        return "Windows";
    char text[64];
    snprintf(text, sizeof(text), "Windows %lu.%lu.%lu", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
    return text;
}

bool Platform_OpenPath(const fs::path &path) {
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool Platform_OpenUrl(const std::string &url) {
    std::wstring wide = Widen(url);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

std::string Platform_GamePath(const fs::path &path) {
    std::wstring wide = path.wstring();
    BOOL lost = FALSE;
    std::string ansi;
    int length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), int(wide.size()), nullptr, 0, nullptr,
                                     &lost);
    if (!lost) {
        ansi.resize(size_t(length));
        WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), int(wide.size()), ansi.data(), length, nullptr,
                            nullptr);
        return ansi;
    }
    std::wstring shortPath(MAX_PATH, L'\0');
    DWORD shortLength = GetShortPathNameW(wide.c_str(), shortPath.data(), DWORD(shortPath.size()));
    if (shortLength == 0 || shortLength >= shortPath.size())
        return Narrow(wide, CP_ACP);
    shortPath.resize(shortLength);
    return Narrow(shortPath, CP_ACP);
}

bool Platform_StartGame(const fs::path &executable, const std::vector<std::string> &arguments,
                        const fs::path &workingDirectory, const fs::path &logFile, GameProcess *game,
                        std::string *error) {
    SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
    HANDLE log = CreateFileW(logFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        *error = "cannot write the log " + ToUtf8(logFile) + ": " + LastErrorText(GetLastError());
        return false;
    }
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                               nullptr);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = log;
    startup.hStdError = log;
    PROCESS_INFORMATION process = {};
    std::wstring line = CommandLine(executable, arguments);

    // Suspended until it is in the job, so that nothing it starts can escape it
    BOOL started = CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, workingDirectory.c_str(), &startup,
                                  &process);
    DWORD startError = GetLastError();
    CloseHandle(log);
    if (input != INVALID_HANDLE_VALUE)
        CloseHandle(input);
    if (!started) {
        *error = "cannot start " + ToUtf8(executable) + ": " + LastErrorText(startError);
        return false;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr && !AssignProcessToJobObject(job, process.hProcess)) {
        CloseHandle(job);
        job = nullptr;
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    game->job = job;
    game->process = process.hProcess;
    game->id = process.dwProcessId;
    return true;
}

bool Platform_GameRunning(const GameProcess &game, int *exitCode) {
    if (game.process == nullptr)
        return false;
    if (game.job != nullptr) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting = {};
        if (QueryInformationJobObject(game.job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting),
                                      nullptr) &&
            accounting.ActiveProcesses > 0)
            return true;
    } else if (WaitForSingleObject(game.process, 0) == WAIT_TIMEOUT) {
        return true;
    }
    DWORD code = 0;
    GetExitCodeProcess(game.process, &code);
    if (exitCode != nullptr)
        *exitCode = int(code);
    return false;
}

void Platform_StopGame(const GameProcess &game) {
    if (game.job != nullptr)
        TerminateJobObject(game.job, 1);
    else if (game.process != nullptr)
        TerminateProcess(game.process, 1);
}

void Platform_CloseGame(GameProcess *game) {
    if (game->job != nullptr)
        CloseHandle(game->job);
    if (game->process != nullptr)
        CloseHandle(game->process);
    *game = GameProcess();
}

bool Platform_StartDetached(const fs::path &executable, const std::vector<std::string> &arguments) {
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    std::wstring line = CommandLine(executable, arguments);
    std::wstring folder = executable.parent_path().wstring();
    if (!CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup,
                        &process))
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void Platform_ShowError(const char *title, const char *message) {
    MessageBoxW(nullptr, Widen(message).c_str(), Widen(title).c_str(), MB_OK | MB_ICONERROR);
}

void Platform_AttachConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;   // started from Explorer: output goes nowhere, and --selftest's log file still has it
    FILE *reopened = nullptr;
    // Only where the standard handles are not already a file or pipe the caller redirected
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN)
        freopen_s(&reopened, "CONOUT$", "w", stdout);
    if (GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_UNKNOWN)
        freopen_s(&reopened, "CONOUT$", "w", stderr);
}
