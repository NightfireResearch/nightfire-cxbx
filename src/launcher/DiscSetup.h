#ifndef LAUNCHER_DISCSETUP_H_
#define LAUNCHER_DISCSETUP_H_

// First-time setup's data (docs/install-and-update.md, "First-time setup"): the Xbox disc, as a disc image (Xdvdfs.h)
// or a folder it was extracted to, checked, recognised against the releases we know, and its eurocom/ and driving/
// copied into the launcher's disc/ folder.

#include <stdint.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct DiscCheck {
    bool usable = false;                 // the folders and files the game needs are there
    std::vector<std::string> problems;   // why not, or warnings
    std::string release;                 // "PAL", "NTSC (US)"; empty for a version we have not seen
    bool confirmed = false;              // the release told by the key files' hashes, not only their sizes
    bool image = false;                  // the source is a disc image
    std::string region;                  // settings.ini's Region for it: "PAL" or "NTSC"
    uint64_t bytes = 0;                  // what a copy of eurocom/ and driving/ takes
};

// Progress through a long step: bytes done of total, and what it is on
using DiscProgress = std::function<void(uint64_t done, uint64_t total, const std::string &file)>;

// Whether source is a disc image rather than a folder
bool DiscSetup_IsImage(const std::filesystem::path &source);

// The checks, and the release its key files' sizes name; for a folder, then their hashes (a few hundred MB read). An
// image's hashes are checked on the copy.
DiscCheck DiscSetup_Check(const std::filesystem::path &source, const DiscProgress &progress = nullptr);

// eurocom/ and driving/ copied, or extracted from an image, into to (made if need be), after a check of the free
// space; cancel stops it between blocks
bool DiscSetup_Copy(const std::filesystem::path &from, const std::filesystem::path &to, const DiscProgress &progress,
                    const std::atomic<bool> &cancel, std::string *error);

#endif // LAUNCHER_DISCSETUP_H_
