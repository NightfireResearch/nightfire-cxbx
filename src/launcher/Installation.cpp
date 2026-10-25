#include "Installation.h"

#include <fstream>
#include <sstream>

#include "IniFile.h"
#include "Json.h"
#include "Platform.h"
#include "SemVer.h"

Installation::Installation(fs::path root) : m_root(std::move(root)) {}

fs::path Installation::VersionFolder(const std::string &version) const {
    return AppFolder() / FromUtf8(version);
}

fs::path Installation::LauncherFile() const {
    return m_root / (std::string("launcher") + Platform_ExecutableSuffix());
}

fs::path Installation::GameExecutable(const std::string &version) const {
    return VersionFolder(version) / (std::string("action_standalone") + Platform_ExecutableSuffix());
}

bool Installation::HasGame() const {
    std::error_code error;
    return !m_config.installed.empty() && fs::is_regular_file(GameExecutable(m_config.installed), error);
}

bool Installation::Load(std::string *error) {
    std::ifstream in(ConfigFile(), std::ios::binary);
    if (!in)
        return true;
    std::stringstream text;
    text << in.rdbuf();
    Json json;
    std::string why;
    if (!Json::Parse(text.str(), &json, &why) || !json.IsObject()) {
        *error = ToUtf8(ConfigFile()) + " is damaged (" + why + ")";
        return false;
    }
    m_config.installed = json["installed"].AsString();
    m_config.previous = json["previous"].AsString();
    m_config.channel = json["channel"].AsString() == "development" ? Channel::Development : Channel::Stable;
    m_config.repository = json["repository"].AsString(kDefaultRepository);
    m_config.apiBase = json["apiBase"].AsString(kDefaultApiBase);
    m_config.discPath = json["discPath"].AsString();
    m_config.discRelease = json["discRelease"].AsString();
    return true;
}

bool Installation::Save(std::string *error) const {
    Json json = Json::MakeObject();
    json.Set("format", 1);
    json.Set("installed", m_config.installed);
    json.Set("previous", m_config.previous);
    json.Set("channel", m_config.channel == Channel::Development ? "development" : "stable");
    json.Set("repository", m_config.repository);
    if (m_config.apiBase != kDefaultApiBase)
        json.Set("apiBase", m_config.apiBase);
    json.Set("discPath", m_config.discPath);
    json.Set("discRelease", m_config.discRelease);

    // Written beside, then moved over, so that a crash mid-write leaves the old record
    std::error_code ec;
    fs::create_directories(m_root, ec);
    fs::path temporary = ConfigFile();
    temporary += ".new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << json.Dump();
        if (!out) {
            *error = "cannot write " + ToUtf8(temporary);
            return false;
        }
    }
    fs::rename(temporary, ConfigFile(), ec);
    if (ec) {
        *error = "cannot replace " + ToUtf8(ConfigFile()) + ": " + ec.message();
        return false;
    }
    return true;
}

bool Installation::WriteSettings(const std::string &region, std::string *error) const {
    std::error_code ec;
    fs::create_directories(UserFolder(), ec);
    fs::create_directories(LogFolder(), ec);
    IniFile ini;
    ini.Load(SettingsFile());
    bool regionSet = ini.Has("Region");
    ini.AddMissingDefaults();
    ini.Set("DiscPath", Platform_GamePath(FromUtf8(m_config.discPath)));
    if (!region.empty() && !regionSet)
        ini.Set("Region", region);
    if (!ini.Save(SettingsFile())) {
        *error = "cannot write " + ToUtf8(SettingsFile());
        return false;
    }
    return true;
}

void Installation::Prune() const {
    std::error_code ec;
    for (auto it = fs::directory_iterator(AppFolder(), ec); it != fs::directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        std::string name = ToUtf8(it->path().filename());
        if (name == m_config.installed || name == m_config.previous)
            continue;
        SemVer version;
        bool release = SemVer::Parse(name, &version);
        bool leftover = name.size() > 8 && name.compare(name.size() - 8, 8, ".partial") == 0;
        if (release || leftover || name == ".download") {
            std::error_code removeError;
            fs::remove_all(it->path(), removeError);
        }
    }
}
