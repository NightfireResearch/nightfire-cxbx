#include "Launcher.h"

#include <time.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include "Json.h"
#include "SemVer.h"

namespace {

const size_t kKeptLogs = 20;
const size_t kIssueUrlLimit = 7500;   // GitHub refuses a new-issue address much past 8 KB

std::string Timestamp() {
    time_t now = time(nullptr);
    struct tm local;
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
    return text;
}

std::string UrlEncode(const std::string &text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += char(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

std::vector<std::string> LastLines(const fs::path &file, size_t count) {
    std::ifstream in(file, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
        if (lines.size() > count)
            lines.erase(lines.begin());
    }
    return lines;
}

// The newest log in the folder, by name (they are time-stamped)
fs::path NewestLog(const fs::path &folder) {
    fs::path newest;
    std::error_code ec;
    for (auto it = fs::directory_iterator(folder, ec); it != fs::directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        if (it->path().extension() == ".log" && (newest.empty() || it->path().filename() > newest.filename()))
            newest = it->path();
    }
    return newest;
}

void PruneLogs(const fs::path &folder) {
    std::vector<fs::path> logs;
    std::error_code ec;
    for (auto it = fs::directory_iterator(folder, ec); it != fs::directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        if (it->path().extension() == ".log")
            logs.push_back(it->path());
    }
    std::sort(logs.begin(), logs.end());
    for (size_t i = 0; i + kKeptLogs < logs.size(); i++)
        fs::remove(logs[i], ec);
}

} // namespace

Launcher::Launcher(fs::path root, std::string version) : m_installation(std::move(root)), m_version(std::move(version)) {}

Launcher::~Launcher() {
    m_cancel = true;
    if (m_worker.joinable())
        m_worker.join();
    Platform_CloseGame(&m_game);
}

bool Launcher::RunningFromRoot() const {
    std::error_code ec;
    return fs::equivalent(Platform_ExecutablePath(), m_installation.LauncherFile(), ec);
}

bool Launcher::Start(std::string *notice) {
    std::string error;
    if (!m_installation.Load(&error)) {
        *notice = error;
        return false;
    }
    // launcher.old is the launcher that started this one after an update, which may take a moment to end
    for (int wait = 0; wait < 40 && fs::exists(m_installation.Root() / ("launcher.old" +
                                                                      std::string(Platform_ExecutableSuffix())));
         wait++) {
        Updater_CleanUp(m_installation);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    m_lastLog = NewestLog(m_installation.LogFolder());

    // A first run from an extracted release: its files go into app/, and this launcher into the root. Only a release
    // newer than the installed one, or the first.
    if (!RunningFromRoot()) {
        fs::path here = Platform_ExecutablePath().parent_path();
        std::ifstream in(here / "nightfire-release.json", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        Json release;
        if (in && Json::Parse(text.str(), &release, nullptr)) {
            const LauncherConfig &config = m_installation.Config();
            bool newer = config.installed.empty() || !m_installation.HasGame() ||
                         SemVer_Compare(release["version"].AsString(), config.installed) > 0;
            InstallResult result;
            if (newer && Updater_InstallFolder(m_installation, here, m_version, &result)) {
                *notice = "Installed version " + result.version + " into " + ToUtf8(m_installation.Root()) +
                          ". From now on, start the launcher from there: " + ToUtf8(m_installation.LauncherFile());
            } else if (newer && !result.error.empty()) {
                *notice = "Could not install the release beside the launcher: " + result.error;
            }
        }
    }
    return true;
}

bool Launcher::Busy() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status.running;
}

JobStatus Launcher::Status() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

bool Launcher::TakeFinished(JobStatus *status) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_status.finished)
        return false;
    m_status.finished = false;
    *status = m_status;
    if (m_worker.joinable())
        m_worker.join();
    if (m_pendingInstallation) {
        m_installation = *m_pendingInstallation;
        m_pendingInstallation.reset();
    }
    return true;
}

void Launcher::Report(const std::string &step, double fraction) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.step = step;
    m_status.fraction = fraction;
}

void Launcher::RunJob(const std::string &name, std::function<bool(std::string *message)> job) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_status.running)
            return;
        if (m_worker.joinable())
            m_worker.join();
        m_status = JobStatus();
        m_status.running = true;
        m_status.name = name;
    }
    m_cancel = false;
    m_worker = std::thread([this, job]() {
        std::string message;
        bool ok = job(&message);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.running = false;
        m_status.finished = true;
        m_status.ok = ok;
        m_status.message = message;
        m_status.fraction = -1;
    });
}

UpdateCheck Launcher::LastCheck() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastCheck;
}

DiscCheck Launcher::LastDiscCheck() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastDisc;
}

void Launcher::CheckForUpdates() {
    Installation installation = m_installation;
    RunJob("Checking for updates", [this, installation](std::string *message) {
        UpdateCheck check = Updater_Check(installation);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastCheck = check;
        }
        if (!check.ok) {
            *message = check.offline ? "No connection: you can still play the installed version. (" + check.error + ")"
                                     : check.error;
            return false;
        }
        *message = check.newer ? "Version " + check.release.version + " is available."
                               : "You have the newest version (" + check.release.version + ").";
        return true;
    });
}

bool Launcher::Install(Installation &installation, const ReleaseInfo &release, bool afterRestart,
                       std::string *message) {
    UpdateProgress progress;
    progress.report = [this](const std::string &step, double fraction) { Report(step, fraction); };
    progress.cancel = &m_cancel;
    InstallResult result = Updater_Install(installation, release, m_version, progress, afterRestart);
    if (!result.ok) {
        *message = "The update stopped, and nothing was changed: " + result.error;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingInstallation = installation;
        if (!result.restartToFinish)
            m_lastCheck.newer = false;
    }
    if (result.restartToFinish) {
        m_restartFinishes = true;
        m_restartWanted = true;
        *message = "Version " + result.version + " needs a newer launcher: the launcher was updated first, and "
                   "restarts now to finish.";
        return true;
    }
    *message = "Updated to " + result.version + ".";
    if (result.launcherReplaced && RunningFromRoot()) {
        m_restartWanted = true;
        *message += " The launcher was updated too and restarts now.";
    }
    return true;
}

void Launcher::InstallUpdate() {
    Installation installation = m_installation;
    ReleaseInfo release = LastCheck().release;
    RunJob("Updating to " + release.version, [this, installation, release](std::string *message) mutable {
        return Install(installation, release, false, message);
    });
}

void Launcher::FinishUpdate() {
    Installation installation = m_installation;
    RunJob("Finishing the update", [this, installation](std::string *message) mutable {
        UpdateCheck check = Updater_Check(installation);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastCheck = check;
        }
        if (!check.ok) {
            *message = "The update could not be finished: " + check.error;
            return false;
        }
        if (!check.newer) {
            *message = "You have the newest version (" + check.release.version + ").";
            return true;
        }
        return Install(installation, check.release, true, message);
    });
}

void Launcher::CheckDisc(const fs::path &folder) {
    RunJob("Checking the game data", [this, folder](std::string *message) {
        DiscCheck check = DiscSetup_Check(folder, [this](uint64_t done, uint64_t total, const std::string &file) {
            Report("Reading " + file, total > 0 ? double(done) / double(total) : -1);
        });
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastDisc = check;
        }
        if (!check.usable) {
            *message = "This folder does not hold the game's data.";
            return false;
        }
        return true;   // the window shows the check itself
    });
}

void Launcher::SetUpDisc(const fs::path &folder, bool copy) {
    Installation installation = m_installation;
    RunJob(copy ? "Copying the game data" : "Setting up the game data",
           [this, installation, folder, copy](std::string *message) mutable {
               DiscCheck check = DiscSetup_Check(folder, [this](uint64_t done, uint64_t total, const std::string &file) {
                   Report("Checking " + file, total > 0 ? double(done) / double(total) : -1);
               });
               {
                   std::lock_guard<std::mutex> lock(m_mutex);
                   m_lastDisc = check;
               }
               if (!check.usable) {
                   *message = "This folder does not hold the game's data.";
                   return false;
               }
               fs::path data = fs::absolute(folder);
               if (copy) {
                   std::string error;
                   if (!DiscSetup_Copy(folder, installation.DiscFolder(),
                                       [this](uint64_t done, uint64_t total, const std::string &file) {
                                           Report("Copying " + file, total > 0 ? double(done) / double(total) : -1);
                                       },
                                       m_cancel, &error)) {
                       *message = "The copy stopped: " + error;
                       return false;
                   }
                   // The copies, checked as the source was
                   DiscCheck copied = DiscSetup_Check(installation.DiscFolder(),
                                                      [this](uint64_t done, uint64_t total, const std::string &file) {
                                                          Report("Checking the copy of " + file,
                                                                 total > 0 ? double(done) / double(total) : -1);
                                                      });
                   if (!copied.usable || copied.release != check.release) {
                       *message = "The copy does not match the source: is the disk full or failing?";
                       return false;
                   }
                   data = installation.DiscFolder();
               }
               installation.Config().discPath = ToUtf8(data);
               installation.Config().discRelease = check.release;
               std::string error;
               if (!installation.WriteSettings(check.region, &error) || !installation.Save(&error)) {
                   *message = error;
                   return false;
               }
               {
                   std::lock_guard<std::mutex> lock(m_mutex);
                   m_pendingInstallation = installation;
               }
               *message = check.release.empty() ? "The game data is set up (a version we have not tested)."
                                                : "The game data is set up: " + check.release + ".";
               return true;
           });
}

bool Launcher::Undo(std::string *error) {
    if (Busy()) {
        *error = "wait for the current job to finish";
        return false;
    }
    if (!Updater_Undo(m_installation, error))
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_lastCheck.ok)
        m_lastCheck.newer = SemVer_Compare(m_lastCheck.release.version, m_installation.Config().installed) > 0;
    return true;
}

bool Launcher::Play(const std::string &level, std::string *error) {
    int exitCode;
    if (GameRunning(&exitCode)) {
        *error = "the game is already running";
        return false;
    }
    if (!m_installation.HasGame()) {
        *error = "no version of the game is installed";
        return false;
    }
    std::error_code ec;
    fs::create_directories(m_installation.LogFolder(), ec);
    // A settings.ini the player deleted is written again, or the game would write one with its own DiscPath
    std::string settingsError;
    if (m_installation.HasGameData() && !fs::exists(m_installation.SettingsFile(), ec) &&
        !m_installation.WriteSettings(m_installation.Config().discRelease.compare(0, 4, "NTSC") == 0 ? "NTSC" : "",
                                      &settingsError)) {
        *error = settingsError;
        return false;
    }
    PruneLogs(m_installation.LogFolder());
    m_lastLog = m_installation.LogFolder() / ("game-" + Timestamp() + ".log");
    std::vector<std::string> arguments;
    if (!level.empty()) {
        arguments.push_back("-level");
        arguments.push_back(level);
    }
    Platform_CloseGame(&m_game);
    return Platform_StartGame(m_installation.GameExecutable(m_installation.Config().installed), arguments,
                              m_installation.UserFolder(), m_lastLog, &m_game, error);
}

bool Launcher::GameRunning(int *exitCode) {
    return Platform_GameRunning(m_game, exitCode);
}

void Launcher::StopGame() {
    Platform_StopGame(m_game);
}

std::string Launcher::ProblemReport() const {
    const LauncherConfig &config = m_installation.Config();
    std::string text;
    text += "**What happened**\n\n(Please say what you were doing and what went wrong.)\n\n";
    text += "**Details**\n\n";
    text += "- Launcher: " + m_version + "\n";
    text += "- Game: " + (config.installed.empty() ? std::string("not installed") : config.installed) + "\n";
    text += std::string("- Platform: ") + Platform_Name() + ", " + Platform_Description() + "\n";
    text += "- Game data: " +
            (config.discPath.empty() ? std::string("not set up")
                                     : (config.discRelease.empty() ? std::string("a version we have not tested")
                                                                   : config.discRelease)) +
            "\n";
    if (!m_lastLog.empty()) {
        text += "\n**The end of the game's log** (" + ToUtf8(m_lastLog.filename()) + ")\n\n```\n";
        for (const std::string &line : LastLines(m_lastLog, 40))
            text += line + "\n";
        text += "```\n";
    }
    std::error_code ec;
    if (fs::exists(m_installation.UserFolder() / "crash.log", ec))
        text += "\n(There is a crash.log in the game's folder: please attach it.)\n";
    return text;
}

std::string Launcher::IssueUrl(const std::string &title, const std::string &report) const {
    std::string base = "https://github.com/" + m_installation.Config().repository + "/issues/new?title=" +
                       UrlEncode(title) + "&body=";
    std::string body = report;
    // Too long for an address: the log's oldest lines go first, then the end of the text
    while (base.size() + UrlEncode(body).size() > kIssueUrlLimit) {
        size_t fence = body.find("```\n");
        size_t firstLine = fence == std::string::npos ? std::string::npos : body.find('\n', fence + 4);
        if (firstLine != std::string::npos && body.compare(firstLine + 1, 3, "```") != 0)
            body.erase(fence + 4, firstLine - fence - 3);
        else
            body.resize(body.size() * 9 / 10);
    }
    return base + UrlEncode(body);
}
