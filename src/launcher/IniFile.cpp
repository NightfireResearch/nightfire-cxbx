#include "IniFile.h"

#include <ctype.h>

#include <fstream>

namespace {

struct DefaultSetting {
    const char *key;
    const char *value;
    const char *comment;   // lines joined by \n, each written with "; " before it
};

// The keys and comments of the game's own default settings.ini (src/action/engine/XboxSettings.cpp,
// WriteDefaultSettingsFile), in its order
const DefaultSetting kDefaults[] = {
    {"Widescreen", "0", "0 = 4:3, 1 = 16:9"},
    {"FOV", "60",
     "Field of view in play, in degrees from the top of the screen to the bottom (widescreen adds width at\n"
     "the sides). The game's own is 60. Scopes show the same view whatever this is set to."},
    {"RenderWidth", "640", "Render resolution does not affect UI scale, UI is referenced to 640x480 still"},
    {"RenderHeight", "480", ""},
    {"Region", "PAL",
     "NTSC or PAL - also picks which of two English text variants the game uses (a real regional\n"
     "product tie-in: the \"Stunner\" gadget is rebranded as a Philips-brand shaver outside NTSC/US).\n"
     "If background movies stay black for you, try switching this."},
    {"Language", "English", "English, Japanese, German, French, Spanish, or Italian"},
    {"FPS", "0", "Target frame rate. 0 = use the region default (60 for NTSC, 50 for PAL)"},
    {"Reverb", "on",
     "Native audio (action.exe) only: reverb on 3D sounds. The Xbox ran this on its audio DSP; the room here is an\n"
     "approximation, since the game never sets the room parameters and nothing local can reproduce them."},
    {"MouseLook", "on",
     "Mouse look, which the game itself has no idea about. Click in the window during play to\n"
     "capture the pointer, Escape (or anything that opens a menu) to let it go again."},
    {"MouseSensitivity", "1.0",
     "Multiplier on the default mouse speed, which is about a five inch sweep per 360 degrees\n"
     "on an 800 DPI mouse. Larger is faster."},
    {"MouseInvertY", "off", "Whether moving the mouse away from you looks down instead of up."},
    {"MouseZoom", "hold",
     "The right mouse button zooms: hold = zoomed while it is held down, toggle = each click zooms\n"
     "in or back out. (With the game's own Aim option set to toggle, the button toggles either way.)"},
    {"DiscPath", "../disc",
     "Where the game's disc data lives - the folder containing eurocom\\filesys.d00 and the rest. This is\n"
     "what the Xbox's D: drive resolves to. Relative paths are relative to this executable's folder."},
    {"DumpFiles", "off",
     "Developer option: save a copy of every file the game loads, as loaded from the disc's archives,\n"
     "under dump\\ (the action engine) and dump_driving\\ (the driving engine). Slow, and uses a lot of space."},
    {"DemoRecord", "off",
     "Developer option: record a time demo of every session to demos\\<date>-<time>.nfdemo, to play back\n"
     "and check the game does the same again (docs/action-timedemo.md). F6 marks a moment in the recording."},
};

std::string Trim(const std::string &text) {
    size_t start = 0, end = text.size();
    while (start < end && isspace(static_cast<unsigned char>(text[start])))
        start++;
    while (end > start && isspace(static_cast<unsigned char>(text[end - 1])))
        end--;
    return text.substr(start, end - start);
}

bool SameKey(const std::string &a, const std::string &b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// "[Name]" lines; before any, a line is in [Settings], as the game's own reader takes it
bool SectionHeader(const std::string &line, std::string *name) {
    std::string trimmed = Trim(line);
    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']')
        return false;
    *name = trimmed.substr(1, trimmed.size() - 2);
    return true;
}

// The key of a "key = value" line, or empty for a comment or anything else
std::string LineKey(const std::string &line) {
    std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
        return std::string();
    size_t equals = trimmed.find('=');
    return equals == std::string::npos ? std::string() : Trim(trimmed.substr(0, equals));
}

} // namespace

bool IniFile::Load(const std::filesystem::path &file) {
    m_lines.clear();
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        m_lines.push_back(line);
    }
    return true;
}

bool IniFile::Save(const std::filesystem::path &file) const {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
#ifdef _WIN32
    const char *newline = "\r\n";   // as the game's own fopen(..., "w") writes it
#else
    const char *newline = "\n";
#endif
    for (const std::string &line : m_lines)
        out << line << newline;
    return bool(out);
}

int IniFile::Find(const std::string &key) const {
    bool inSettings = true;
    for (size_t i = 0; i < m_lines.size(); i++) {
        std::string section;
        if (SectionHeader(m_lines[i], &section)) {
            inSettings = SameKey(section, "Settings");
            continue;
        }
        if (inSettings && SameKey(LineKey(m_lines[i]), key))
            return int(i);
    }
    return -1;
}

int IniFile::SectionEnd() const {
    bool inSettings = true;
    int end = int(m_lines.size());
    for (size_t i = 0; i < m_lines.size(); i++) {
        std::string section;
        if (!SectionHeader(m_lines[i], &section))
            continue;
        if (inSettings && i > 0 && !SameKey(section, "Settings")) {
            end = int(i);
            break;
        }
        inSettings = SameKey(section, "Settings");
    }
    while (end > 0 && Trim(m_lines[size_t(end - 1)]).empty())   // before the blank lines that end the section
        end--;
    return end;
}

bool IniFile::Has(const std::string &key) const {
    return Find(key) >= 0;
}

std::string IniFile::Get(const std::string &key, const std::string &fallback) const {
    int index = Find(key);
    if (index < 0)
        return fallback;
    const std::string &line = m_lines[size_t(index)];
    std::string value = line.substr(line.find('=') + 1);
    size_t comment = value.find_first_of(";#");   // the game's reader cuts the line there too
    if (comment != std::string::npos)
        value.resize(comment);
    return Trim(value);
}

void IniFile::Set(const std::string &key, const std::string &value, const std::string &comment) {
    int index = Find(key);
    if (index >= 0) {
        m_lines[size_t(index)] = key + "=" + value;
        return;
    }
    if (m_lines.empty()) {
        m_lines.push_back("; 007: Nightfire settings - replaces the Xbox dashboard's EEPROM-stored options.");
        m_lines.push_back("; Edit and save, then restart the game for changes to take effect.");
        m_lines.push_back("");
        m_lines.push_back("[Settings]");
    }
    std::vector<std::string> added;
    if (!comment.empty())
        added.push_back("");
    size_t start = 0;
    while (!comment.empty() && start <= comment.size()) {
        size_t end = comment.find('\n', start);
        added.push_back("; " + comment.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    added.push_back(key + "=" + value);
    int at = SectionEnd();
    m_lines.insert(m_lines.begin() + at, added.begin(), added.end());
}

void IniFile::AddMissingDefaults() {
    for (const DefaultSetting &setting : kDefaults)
        if (!Has(setting.key))
            Set(setting.key, setting.value, setting.comment);
}
