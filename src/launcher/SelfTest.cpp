// launcher --selftest --root DIR --disc DIR|ISO --api-base URL --bundle DIR [--in-place] [--play-seconds N]
//                    [--image ISO --reference DIR]... [--bad-image FILE]... [--log FILE]
//
//   --root          the test's own launcher folder: made, or emptied if an earlier self-test made it (it holds
//                   selftest-root.txt); any other existing folder is refused
//   --disc          an extracted disc (eurocom/, driving/), or a disc image of one
//   --in-place      use the disc folder where it is instead of copying its 3 GB
//   --image         a disc image to check and extract, the result compared byte for byte with the --reference
//                   folder given after it (tools/release/make_test_xiso.py makes such images)
//   --bad-image     a file the image check must refuse (not an Xbox disc, cut short, damaged)
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

#include <string.h>

#include <algorithm>
#include <atomic>
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
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec)
        return false;
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    std::vector<char> bufferX(1 << 20), bufferY(1 << 20);
    while (x && y) {
        x.read(bufferX.data(), std::streamsize(bufferX.size()));
        y.read(bufferY.data(), std::streamsize(bufferY.size()));
        if (x.gcount() != y.gcount() || memcmp(bufferX.data(), bufferY.data(), size_t(x.gcount())) != 0)
            return false;
    }
    return true;
}

// The game's files under a folder: eurocom/ and driving/, by their lowercase relative paths
std::vector<std::pair<std::string, fs::path>> GameFiles(const fs::path &folder) {
    std::vector<std::pair<std::string, fs::path>> files;
    for (const char *top : {"eurocom", "driving"}) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(folder / top, ec); it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec || !it->is_regular_file(ec))
                continue;
            std::string name = ToUtf8(fs::relative(it->path(), folder, ec).generic_wstring());
            for (char &c : name)
                c = char(tolower(static_cast<unsigned char>(c)));
            files.emplace_back(name, it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// An image checked and extracted, and every file compared with the folder it was made from
void ImageTest(const fs::path &root, const fs::path &image, const fs::path &reference) {
    std::string name = ToUtf8(image.filename());
    DiscCheck check = DiscSetup_Check(image);
    Check(check.image && check.usable && !check.release.empty(), name + ": recognised before extracting",
          check.release.empty() ? (check.problems.empty() ? "unknown" : check.problems[0]) : check.release);
    fs::path out = root / "image-test";
    std::error_code ec;
    fs::remove_all(out, ec);
    std::atomic<bool> cancel{false};
    std::string error;
    auto started = std::chrono::steady_clock::now();
    bool extracted = DiscSetup_Copy(image, out, nullptr, cancel, &error);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    Check(extracted, name + ": extracted", extracted ? std::to_string(int(seconds)) + " s" : error);
    std::vector<std::pair<std::string, fs::path>> got = GameFiles(out), want = GameFiles(reference);
    size_t differ = 0;
    std::string first;
    for (size_t i = 0; i < got.size() && i < want.size(); i++)
        if (got[i].first != want[i].first || !SameFile(got[i].second, want[i].second)) {
            if (differ++ == 0)
                first = want[i].first;
        }
    Check(got.size() == want.size() && differ == 0 && !want.empty(),
          name + ": the extracted files are the reference's, byte for byte",
          std::to_string(got.size()) + " files of " + std::to_string(want.size()) +
              (differ ? ", " + std::to_string(differ) + " differ (first " + first + ")" : ""));
    DiscCheck copied = DiscSetup_Check(out);
    Check(copied.confirmed && copied.release == check.release, name + ": the copy's hashes confirm it",
          copied.release);
    fs::remove_all(out, ec);

    // Cancelled part-way, the copy stops
    cancel = true;
    Check(!DiscSetup_Copy(image, out, nullptr, cancel, &error) && error == "cancelled", name + ": cancel stops it");
    fs::remove_all(out, ec);
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
    std::vector<std::pair<fs::path, fs::path>> images;   // image, reference folder
    std::vector<fs::path> badImages;
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
        else if (argument == "--image")
            images.emplace_back(FromUtf8(value()), fs::path());
        else if (argument == "--reference" && !images.empty())
            images.back().second = FromUtf8(value());
        else if (argument == "--bad-image")
            badImages.push_back(FromUtf8(value()));
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
    Check(status.ok && check.usable, "the disc's check", status.message);
    Check(!check.release.empty(), "the disc is a release we know", check.release.empty() ? "unknown" : check.release);
    launcher.SetUpDisc(disc, !inPlace);
    status = Wait(launcher);
    Check(status.ok && installation.HasGameData(),
          DiscSetup_IsImage(disc) ? "setup, the data extracted from the image"
                                  : inPlace ? "setup, the data used in place" : "setup, the data copied",
          status.message);
    IniFile settings;
    settings.Load(installation.SettingsFile());
    std::string discPath = settings.Get("DiscPath");
    Check(fs::path(discPath).is_absolute() && fs::exists(fs::path(discPath) / "eurocom" / "filesys.d00", ec),
          "settings.ini has an absolute DiscPath", discPath);
    Check(settings.Get("Region") == check.region && settings.Has("MouseLook"), "settings.ini's Region and defaults",
          "Region=" + settings.Get("Region"));

    // Disc images: good ones extracted exactly, bad ones refused with a reason
    for (const auto &image : images)
        ImageTest(root, image.first, image.second);
    for (const fs::path &bad : badImages) {
        DiscCheck badCheck = DiscSetup_Check(bad);
        std::atomic<bool> cancel{false};
        std::string error;
        bool copied = DiscSetup_Copy(bad, root / "bad-image-test", nullptr, cancel, &error);
        fs::remove_all(root / "bad-image-test", ec);
        Check(!badCheck.usable && !badCheck.problems.empty() && !copied,
              ToUtf8(bad.filename()) + ": refused", badCheck.problems.empty() ? "" : badCheck.problems[0]);
    }

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
