#ifndef LAUNCHER_UPDATER_H_
#define LAUNCHER_UPDATER_H_

// Updates (docs/install-and-update.md, "Releases and updates"):
//
//   check     the GitHub releases API: releases/latest (Stable) or the releases list (Development), the newest
//             release above the installed version
//   install   its manifest.json and manifest.json.sig, the signature checked; the platform's zip, its size and SHA-256
//             checked against the manifest; unpacked into app/<version>/; the launcher replaced if the release's is
//             newer; then launcher.json switched, and every version but this and the previous one deleted. When the
//             manifest's minLauncher is above this launcher, only the launcher is replaced: the new one restarts and
//             installs the release itself, so that an old launcher never lays out a release it does not know
//   undo      back to the previous version
//
// Any failed check stops the update and leaves the installed version as it was. Nothing from a download runs before
// its checks pass. The manifest's format only gains fields; a launcher ignores those it does not know.

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "Installation.h"

struct ReleaseAsset {
    std::string name;
    std::string url;
};

struct ReleaseInfo {
    std::string tag;       // "v0.4.0"
    std::string version;   // "0.4.0"
    std::string title;
    std::string notes;     // the release's text (Markdown)
    std::string page;      // its web page
    bool prerelease = false;
    std::vector<ReleaseAsset> assets;

    const ReleaseAsset *Asset(const std::string &name) const;
};

struct UpdateCheck {
    bool ok = false;
    bool offline = false;    // no connection: play the installed version
    bool newer = false;      // release is above the installed version
    ReleaseInfo release;     // the newest on the channel
    std::string error;
};

struct UpdateProgress {
    std::function<void(const std::string &step, double fraction)> report;
    const std::atomic<bool> *cancel = nullptr;
};

struct InstallResult {
    bool ok = false;
    std::string error;
    std::string version;
    bool launcherReplaced = false;   // a new launcher is in place: restart it
    bool launcherRequired = false;   // the release's minLauncher was above this launcher
    bool restartToFinish = false;    // only the launcher was replaced: the new one installs the release
};

UpdateCheck Updater_Check(const Installation &installation);

// Installs release (as Updater_Check found it) and switches to it; launcherVersion is this launcher's own.
// afterRestart: this launcher was put in place by the same update (restartToFinish), so if it is still older than
// minLauncher the release is refused rather than replacing the launcher again.
InstallResult Updater_Install(Installation &installation, const ReleaseInfo &release,
                              const std::string &launcherVersion, const UpdateProgress &progress,
                              bool afterRestart = false);

// The release in a folder beside the launcher (a downloaded zip, extracted by hand: the first run), described by
// its nightfire-release.json, installed as an update would be but with no download. False with no error if the
// folder holds no release.
bool Updater_InstallFolder(Installation &installation, const fs::path &folder, const std::string &launcherVersion,
                           InstallResult *result);

// Back to the previous version; the undone one becomes the previous
bool Updater_Undo(Installation &installation, std::string *error);

// launcher.new beside target, then target to launcher.old and launcher.new to target: a running program cannot be
// overwritten on Windows, but it can be renamed. launcher.old goes at the next start (Updater_CleanUp).
bool Updater_ReplaceLauncher(const fs::path &newLauncher, const fs::path &target, std::string *error);
void Updater_CleanUp(const Installation &installation);

#endif // LAUNCHER_UPDATER_H_
