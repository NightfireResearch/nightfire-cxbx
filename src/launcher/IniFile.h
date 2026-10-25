#ifndef LAUNCHER_INIFILE_H_
#define LAUNCHER_INIFILE_H_

// settings.ini, edited in place: the keys of its [Settings] section read and set by name, every other line - the
// comments, keys the launcher does not know - kept as it was. A key that is set again keeps its line; a new one goes
// at the end of the section, after its comment.

#include <filesystem>
#include <string>
#include <vector>

class IniFile {
public:
    bool Load(const std::filesystem::path &file);   // false if it does not exist (the file is then empty)
    bool Save(const std::filesystem::path &file) const;

    bool Has(const std::string &key) const;
    std::string Get(const std::string &key, const std::string &fallback = "") const;
    void Set(const std::string &key, const std::string &value, const std::string &comment = "");

    // The game's own settings.ini (src/action/engine/XboxSettings.cpp writes it when there is none): every key it
    // writes that this file lacks, with its comment, at the game's default. Keys already set are left alone.
    void AddMissingDefaults();

private:
    int Find(const std::string &key) const;   // the line index, or -1
    int SectionEnd() const;                  // where a new key goes

    std::vector<std::string> m_lines;
};

#endif // LAUNCHER_INIFILE_H_
