#ifndef LAUNCHER_LAUNCHER_H_
#define LAUNCHER_LAUNCHER_H_

// What the launcher's window does, apart from drawing it (LauncherUi.cpp): the installation, the long jobs on a
// worker thread (an update check, an install, the disc's check and copy), the game's process, and a problem report.

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "DiscSetup.h"
#include "Installation.h"
#include "Platform.h"
#include "Updater.h"

// A job's state, as the window shows it
struct JobStatus {
    bool running = false;
    bool finished = false;   // set when it ends, until the window takes the result (Launcher::TakeFinished)
    bool ok = false;
    std::string name;        // "Checking for updates"
    std::string step;        // what it is on now
    double fraction = -1;    // progress, or below 0 for none
    std::string message;     // the outcome, or why it failed
};

class Launcher {
public:
    Launcher(fs::path root, std::string version);
    ~Launcher();

    // launcher.json read, launcher.old removed, and a release in this program's own folder installed (a first run
    // from an extracted zip). Messages for the window in notice.
    bool Start(std::string *notice);

    Installation &GetInstallation() { return m_installation; }
    const std::string &Version() const { return m_version; }

    // ---- jobs: one at a time
    bool Busy() const;
    JobStatus Status() const;
    bool TakeFinished(JobStatus *status);   // the job that ended, once
    void Cancel() { m_cancel = true; }

    void CheckForUpdates();
    void InstallUpdate();   // the release the last check found
    void FinishUpdate();    // a check and its install, by a launcher an update put in place (--finish-update)
    void CheckDisc(const fs::path &folder);
    void SetUpDisc(const fs::path &folder, bool copy);   // the check, the copy (or use in place), settings.ini

    // The last results, copied out for the window
    UpdateCheck LastCheck() const;
    DiscCheck LastDiscCheck() const;
    bool RestartWanted() const { return m_restartWanted; }   // a new launcher replaced this one
    bool RestartFinishesUpdate() const { return m_restartFinishes; }   // and is to install the release itself

    // ---- immediate
    bool Undo(std::string *error);
    bool Play(const std::string &level, std::string *error);
    bool GameRunning(int *exitCode);   // polled by the window; false (with the exit code) once it has ended
    void StopGame();
    fs::path LastLog() const { return m_lastLog; }

    // The text of a problem report (version, platform, the data, the end of the last log), and the GitHub new-issue
    // address with it filled in
    std::string ProblemReport() const;
    std::string IssueUrl(const std::string &title, const std::string &report) const;

private:
    void RunJob(const std::string &name, std::function<bool(std::string *message)> job);
    bool Install(Installation &installation, const ReleaseInfo &release, bool afterRestart, std::string *message);
    void Report(const std::string &step, double fraction);
    bool RunningFromRoot() const;

    Installation m_installation;
    std::string m_version;

    mutable std::mutex m_mutex;
    JobStatus m_status;
    std::thread m_worker;
    std::atomic<bool> m_cancel{false};
    UpdateCheck m_lastCheck;
    DiscCheck m_lastDisc;
    std::atomic<bool> m_restartWanted{false};
    std::atomic<bool> m_restartFinishes{false};
    // The installation a job changed, taken over by the window's thread with the job's result (TakeFinished): the
    // window reads the installation without a lock, so a job works on a copy
    std::optional<Installation> m_pendingInstallation;

    GameProcess m_game;
    fs::path m_lastLog;
};

#endif // LAUNCHER_LAUNCHER_H_
