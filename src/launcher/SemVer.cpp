#include "SemVer.h"

#include <stdlib.h>

namespace {

bool AllDigits(const std::string &text) {
    if (text.empty())
        return false;
    for (char c : text)
        if (c < '0' || c > '9')
            return false;
    return true;
}

bool ValidIdentifier(const std::string &text) {
    if (text.empty())
        return false;
    for (char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-'))
            return false;
    return true;
}

std::vector<std::string> Split(const std::string &text, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            return parts;
        start = end + 1;
    }
}

} // namespace

bool SemVer::Parse(const std::string &text, SemVer *out) {
    std::string rest = text;
    if (!rest.empty() && (rest[0] == 'v' || rest[0] == 'V'))
        rest.erase(0, 1);

    SemVer version;
    size_t plus = rest.find('+');
    if (plus != std::string::npos) {
        version.build = rest.substr(plus + 1);
        rest.resize(plus);
    }
    size_t dash = rest.find('-');
    if (dash != std::string::npos) {
        version.pre = Split(rest.substr(dash + 1), '.');
        for (const std::string &identifier : version.pre)
            if (!ValidIdentifier(identifier))
                return false;
        rest.resize(dash);
    }
    std::vector<std::string> numbers = Split(rest, '.');
    if (numbers.size() != 3)
        return false;
    for (const std::string &number : numbers)
        if (!AllDigits(number) || number.size() > 9)
            return false;
    version.major = atoi(numbers[0].c_str());
    version.minor = atoi(numbers[1].c_str());
    version.patch = atoi(numbers[2].c_str());
    *out = version;
    return true;
}

std::string SemVer::ToString() const {
    std::string text = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    for (size_t i = 0; i < pre.size(); i++)
        text += (i == 0 ? "-" : ".") + pre[i];
    if (!build.empty())
        text += "+" + build;
    return text;
}

int SemVer_Compare(const SemVer &a, const SemVer &b) {
    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;
    if (a.pre.empty() || b.pre.empty())
        return a.pre.empty() == b.pre.empty() ? 0 : (a.pre.empty() ? 1 : -1);
    for (size_t i = 0; i < a.pre.size() && i < b.pre.size(); i++) {
        const std::string &x = a.pre[i], &y = b.pre[i];
        bool xNumeric = AllDigits(x), yNumeric = AllDigits(y);
        if (xNumeric && yNumeric) {
            if (x.size() != y.size())
                return x.size() < y.size() ? -1 : 1;
            if (x != y)
                return x < y ? -1 : 1;
        } else if (xNumeric != yNumeric) {
            return xNumeric ? -1 : 1;   // numeric identifiers sort below alphanumeric ones
        } else if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    if (a.pre.size() != b.pre.size())
        return a.pre.size() < b.pre.size() ? -1 : 1;
    return 0;
}

int SemVer_Compare(const std::string &a, const std::string &b) {
    SemVer x, y;
    bool xOk = SemVer::Parse(a, &x), yOk = SemVer::Parse(b, &y);
    if (!xOk || !yOk)
        return xOk == yOk ? 0 : (xOk ? 1 : -1);
    return SemVer_Compare(x, y);
}
