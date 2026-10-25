#ifndef LAUNCHER_SEMVER_H_
#define LAUNCHER_SEMVER_H_

// Release versions, compared as Semantic Versioning 2.0 says: MAJOR.MINOR.PATCH, then a pre-release ("-dev.3")
// below the release it leads to, then build metadata ("+12.gabc1234", cmake/version.cmake's builds past a tag),
// which does not count. A leading "v" (a tag) is allowed.

#include <string>
#include <vector>

struct SemVer {
    int major = 0, minor = 0, patch = 0;
    std::vector<std::string> pre;   // the pre-release's dot-separated identifiers
    std::string build;

    static bool Parse(const std::string &text, SemVer *out);
    std::string ToString() const;
};

// <0, 0 or >0, as a is below, equal to or above b
int SemVer_Compare(const SemVer &a, const SemVer &b);

// The same on text; text that does not parse is below everything that does
int SemVer_Compare(const std::string &a, const std::string &b);

#endif // LAUNCHER_SEMVER_H_
