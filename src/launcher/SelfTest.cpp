// launcher --selftest --root DIR --disc DIR --api-base URL --bundle DIR [--in-place] [--play-seconds N] [--log FILE]
//
//   --root          the test's own launcher folder: made, or emptied if an earlier self-test made it (it holds
//                   selftest-root.txt); any other existing folder is refused
//   --disc          an extracted disc (eurocom/, driving/)
//   --in-place      use the disc where it is instead of copying its 3 GB
//   --api-base      tools/release/fake_releases.py's folder, as a file:// or http:// URL
//   --bundle        its bundle/: an extracted release, the first install
//   --play-seconds  how long the game runs before it is stopped (default 20; 0 skips it)
//   --log           a copy of the output
//
// Every step goes through the window's own code (Launcher, Updater), on its worker thread where the window uses it.

#include "SelfTest.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "IniFile.h"
#include "Launcher.h"
#include "SemVer.h"
#include "Signature.h"
#include "../common/version.h"

namespace {

FILE *g_log = nullptr;
int g_failures = 0;

void Print(const char *format, ...) {
    char text[2048];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    fputs(text, stdout);
    fflush(stdout);
    if (g_log != nullptr) {
        fputs(text, g_log);
        fflush(g_log);
    }
}

bool Check(bool ok, const std::string &step, const std::string &detail = "") {
    Print("%s  %s%s%s\n", ok ? "PASS" : "FAIL", step.c_str(), detail.empty() ? "" : ": ", detail.c_str());
    if (!ok)
        g_failures++;
    return ok;
}

JobStatus Wait(Launcher &launcher) {
    JobStatus status;
    while (!launcher.TakeFinished(&status))
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return status;
}

std::string ReadText(const fs::path &file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool SameFile(const fs::path &a, const fs::path &b) {
    std::error_code ec;
    return fs::file_size(a, ec) == fs::file_size(b, ec) && !ec && ReadText(a) == ReadText(b);
}

// A check, then an install, of the given scenario's release
JobStatus Update(Launcher &launcher, const std::string &repository, UpdateCheck *check) {
    launcher.GetInstallation().Config().repository = repository;
    launcher.CheckForUpdates();
    JobStatus status = Wait(launcher);
    *check = launcher.LastCheck();
    if (!status.ok || !check->newer)
        return status;
    launcher.InstallUpdate();
    return Wait(launcher);
}

} // namespace

int SelfTest_Run(int argc, char **argv) {
    fs::path root, disc, bundle;
    std::string apiBase, logFile;
    bool inPlace = false;
    int playSeconds = 20;
    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];
        auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (argument == "--root")
            root = FromUtf8(value());
        else if (argument == "--disc")
            disc = FromUtf8(value());
        else if (argument == "--bundle")
            bundle = FromUtf8(value());
        else if (argument == "--api-base")
            apiBase = value();
        else if (argument == "--in-place")
            inPlace = true;
        else if (argument == "--play-seconds")
            playSeconds = atoi(value().c_str());
        else if (argument == "--log")
            logFile = value();
    }
    if (!logFile.empty())
        g_log = fopen(logFile.c_str(), "w");
    if (root.empty() || disc.empty() || bundle.empty() || apiBase.empty()) {
        Print("launcher --selftest --root DIR --disc DIR --api-base URL --bundle DIR [--in-place] [--play-seconds N]\n");
        return 2;
    }
    Print("Nightfire launcher %s self-test, %s\n", NightfireVersion, Platform_Name());
    if (!Check(Signature_TestKeyBuild(), "the launcher trusts the test key", "build with NIGHTFIRE_LAUNCHER_TEST_KEY=ON"))
        return 1;

    std::error_code ec;
    root = fs::absolute(root);
    if (fs::exists(root, ec) && !fs::is_empty(root, ec)) {
        if (!fs::exists(root / "selftest-root.txt", ec)) {
            Print("%s exists and was not made by a self-test: not touching it\n", ToUtf8(root).c_str());
            return 2;
        }
        fs::remove_all(root, ec);
    }
    fs::create_directories(root, ec);
    std::ofstream(root / "selftest-root.txt") << "A launcher self-test's folder (launcher --selftest).\n";

    Launcher launcher(root, NightfireVersion);
    std::string notice;
    Check(launcher.Start(&notice), "start on an empty folder", notice);
    Installation &installation = launcher.GetInstallation();
    installation.Config().apiBase = apiBase;

    // First-time setup
    launcher.CheckDisc(disc);
    JobStatus status = Wait(launcher);
    DiscCheck check = launcher.LastDiscCheck();
    Check(status.ok && check.usable, "the disc folder's check", status.message);
    Check(!check.release.empty(), "the disc is a release we know", check.release.empty() ? "unknown" : check.release);
    launcher.SetUpDisc(disc, !inPlace);
    status = Wait(launcher);
    Check(status.ok && installation.HasGameData(), inPlace ? "setup, the data used in place" : "setup, the data copied",
          status.message);
    IniFile settings;
    settings.Load(installation.SettingsFile());
    std::string discPath = settings.Get("DiscPath");
    Check(fs::path(discPath).is_absolute() && fs::exists(fs::path(discPath) / "eurocom" / "filesys.d00", ec),
          "settings.ini has an absolute DiscPath", discPath);
    Check(settings.Get("Region") == check.region && settings.Has("MouseLook"), "settings.ini's Region and defaults",
          "Region=" + settings.Get("Region"));

    // The first install, from an extracted release
    InstallResult first;
    bool installed = Updater_InstallFolder(installation, bundle, NightfireVersion, &first);
    Check(installed && installation.HasGame(), "the first install, from the extracted release",
          installed ? first.version : first.error);
    const std::string firstVersion = installation.Config().installed;
    Check(SameFile(installation.LauncherFile(), bundle / installation.LauncherFile().filename()),
          "the launcher copied into the root");

    // No network: nothing listens on port 9 (discard)
    installation.Config().apiBase = "http://127.0.0.1:9";
    launcher.CheckForUpdates();
    status = Wait(launcher);
    Check(!status.ok && launcher.LastCheck().offline, "no network: reported as offline", status.message);
    installation.Config().apiBase = apiBase;

    // A bad signature, then a bad hash: refused, nothing changed
    UpdateCheck update;
    for (const char *scenario : {"badsig", "badhash"}) {
        status = Update(launcher, std::string("selftest/") + scenario, &update);
        bool unchanged = installation.Config().installed == firstVersion &&
                         !fs::exists(installation.VersionFolder(update.release.version), ec);
        Check(update.ok && update.newer && !status.ok && unchanged,
              scenario == std::string("badsig") ? "a bad signature is refused, nothing changed"
                                                : "a bad hash is refused, nothing changed",
              status.message);
    }

    // A good update: switched, the previous kept, the launcher replaced (90.0.0 is newer than this one)
    status = Update(launcher, "selftest/good", &update);
    const std::string goodVersion = update.release.version;
    Check(status.ok && installation.Config().installed == goodVersion && installation.Config().previous == firstVersion,
          "a good update", status.message);
    Check(SameFile(installation.LauncherFile(), installation.VersionFolder(goodVersion) / "launcher.exe") &&
              fs::exists(installation.Root() / "launcher.old.exe", ec),
          "the launcher replaced by renaming (launcher.old kept until the next start)");
    Updater_CleanUp(installation);
    Check(!fs::exists(installation.Root() / "launcher.old.exe", ec), "launcher.old removed at the next start");

    // minLauncher above this launcher: the launcher alone replaced, nothing switched; then the restarted launcher
    // installs it. Found on the Development channel, through the releases list (which a web server reaches by a
    // redirect). This build stands in for both launchers, so the restarted one is given the release's version.
    installation.Config().channel = Channel::Development;
    status = Update(launcher, "selftest/minlauncher", &update);
    installation.Config().channel = Channel::Stable;
    const std::string minVersion = update.release.version;
    Check(update.ok && minVersion == "90.1.0", "the Development channel's releases list",
          update.ok ? minVersion : update.error);
    Check(status.ok && launcher.RestartFinishesUpdate() && installation.Config().installed == goodVersion &&
              !fs::exists(installation.VersionFolder(minVersion), ec) && fs::exists(installation.LauncherFile(), ec),
          "minLauncher: the launcher replaced first, the release not yet installed", status.message);
    InstallResult again = Updater_Install(installation, update.release, NightfireVersion, UpdateProgress(), true);
    Check(!again.ok && installation.Config().installed == goodVersion,
          "minLauncher: a restarted launcher still too old refuses it", again.error);
    InstallResult finished = Updater_Install(installation, update.release, minVersion, UpdateProgress(), true);
    Check(finished.ok && installation.Config().installed == minVersion &&
              installation.Config().previous == goodVersion && !fs::exists(installation.VersionFolder(firstVersion), ec),
          "minLauncher: the restarted launcher installs it, and only the previous version is kept", finished.error);

    // Undo
    std::string error;
    Check(launcher.Undo(&error) && installation.Config().installed == goodVersion &&
              installation.Config().previous == minVersion,
          "undo the last update", error);
    Installation reread(root);
    Check(reread.Load(&error) && reread.Config().installed == goodVersion, "launcher.json records the undo");

    // The game, for a while
    if (playSeconds > 0) {
        bool started = launcher.Play("", &error);
        Check(started, "the game starts", error);
        int exitCode = 0;
        bool stillRunning = started;
        for (int tenth = 0; started && tenth < playSeconds * 10 && stillRunning; tenth++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stillRunning = launcher.GameRunning(&exitCode);
        }
        Check(stillRunning, "the game is still running after " + std::to_string(playSeconds) + " s",
              stillRunning ? "" : "it ended with code " + std::to_string(exitCode));
        launcher.StopGame();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::string log = ReadText(launcher.LastLog());
        Check(log.find(std::string("[standalone] Nightfire ") + NightfireVersion) != std::string::npos,
              "the game's log starts with its version", ToUtf8(launcher.LastLog()));
        Print("note  the log %s 'Playing background movie' (the front end's movie)\n",
              log.find("Playing background movie") != std::string::npos ? "has" : "does not have");
    }

    Print("%s: %d failure%s\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures, g_failures == 1 ? "" : "s");
    if (g_log != nullptr)
        fclose(g_log);
    return g_failures == 0 ? 0 : 1;
}
