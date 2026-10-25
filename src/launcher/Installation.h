#ifndef LAUNCHER_INSTALLATION_H_
#define LAUNCHER_INSTALLATION_H_

// The launcher's folder and its record, launcher.json (docs/install-and-update.md, "Folder layout"):
//
//   <root>/launcher.exe   launcher.json
//          app/<version>/ one release's executables; the installed one and the previous one are kept
//          disc/          the player's game data (or elsewhere: discPath)
//          user/          the game's working folder: settings.ini, saves/, tdata/, cache/, logs/
//
// A release never writes to disc/ or user/.

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// The repository releases come from. Each manifest names its repository too, and the launcher keeps that, so that
// a renamed repository moves every launcher with one release.
static const char kDefaultRepository[] = "NightfireResearch/nightfire-cxbx";
static const char kDefaultApiBase[] = "https://api.github.com";

enum class Channel { Stable, Development };

struct LauncherConfig {
    std::string installed;   // the version in app/ the game runs from
    std::string previous;    // the one kept for "Undo the last update"
    Channel channel = Channel::Stable;
    std::string repository = kDefaultRepository;
    std::string apiBase = kDefaultApiBase;   // only changed for tests (--api-base)
    std::string discPath;                    // the data folder, absolute; empty until setup is done
    std::string discRelease;                 // what setup recognised: "PAL", "NTSC (US)", or empty
};

class Installation {
public:
    explicit Installation(fs::path root);

    const fs::path &Root() const { return m_root; }
    fs::path AppFolder() const { return m_root / "app"; }
    fs::path VersionFolder(const std::string &version) const;
    fs::path DiscFolder() const { return m_root / "disc"; }
    fs::path UserFolder() const { return m_root / "user"; }
    fs::path LogFolder() const { return UserFolder() / "logs"; }
    fs::path SettingsFile() const { return UserFolder() / "settings.ini"; }
    fs::path LauncherFile() const;   // <root>/launcher.exe
    fs::path ConfigFile() const { return m_root / "launcher.json"; }

    LauncherConfig &Config() { return m_config; }
    const LauncherConfig &Config() const { return m_config; }
    bool Load(std::string *error);   // a missing launcher.json is a fresh installation, not an error
    bool Save(std::string *error) const;

    bool HasGameData() const { return !m_config.discPath.empty(); }
    bool HasGame() const;   // the installed version's folder holds the game

    // The game's executable in a version's folder
    fs::path GameExecutable(const std::string &version) const;

    // settings.ini with DiscPath set to the data folder, Region to the disc's, and the game's other defaults added
    // where missing; keys the player has set are left as they are
    bool WriteSettings(const std::string &region, std::string *error) const;

    // app/ less everything but the installed and previous versions (and unfinished downloads)
    void Prune() const;

private:
    fs::path m_root;
    LauncherConfig m_config;
};

#endif // LAUNCHER_INSTALLATION_H_
