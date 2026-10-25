#ifndef LAUNCHER_DISCSETUP_H_
#define LAUNCHER_DISCSETUP_H_

// First-time setup's data (docs/install-and-update.md, "First-time setup"): a folder holding the extracted Xbox disc
// checked, recognised against the releases we know, and copied into the launcher's disc/ folder.
//
// TODO: a disc image (.iso) as the source - the XDVDFS reader of the design's step 6: the volume descriptor at sector
// 32 (offset 0x18300000 into a full "redump" image, 0 in an xiso), then the directory entries' binary trees, copying
// out eurocom/ and driving/ only.

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
    std::string region;                  // settings.ini's Region for it: "PAL" or "NTSC"
    uint64_t bytes = 0;                  // what a copy of eurocom/ and driving/ takes
};

// Progress through a long step: bytes done of total, and what it is on
using DiscProgress = std::function<void(uint64_t done, uint64_t total, const std::string &file)>;

// The checks, then the known-release hashes (a few hundred MB read)
DiscCheck DiscSetup_Check(const std::filesystem::path &folder, const DiscProgress &progress = nullptr);

// eurocom/ and driving/ copied into to (made if need be); cancel stops it between blocks
bool DiscSetup_Copy(const std::filesystem::path &from, const std::filesystem::path &to, const DiscProgress &progress,
                    const std::atomic<bool> &cancel, std::string *error);

#endif // LAUNCHER_DISCSETUP_H_
