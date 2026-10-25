#include "DiscSetup.h"

#include <fstream>

#include "Platform.h"
#include "Sha256.h"

namespace {

struct KnownFile {
    const char *path;
    uint64_t size;
    const char *sha256;   // null: the size alone (a file too large to read on every check)
};

struct KnownRelease {
    const char *name;
    const char *region;
    KnownFile files[5];
};

// The releases we have seen, each by a few files that tell it from the others. The two discs' eurocom/filesys.d00 to
// .d10 and their driving/ *.mus, *.viv and English *.spe files are the same bytes; what differs:
//   - the background movies: eurocom/25_fps/ (25 files) on the PAL disc, eurocom/30_fps/ (22) on the US one. The game
//     opens 30_fps when it runs at 60 Hz, which is Region=NTSC (src/action/engine/Fmv.cpp): the US data needs
//     Region=NTSC, or the movies are not found;
//   - eurocom/config.txt, US only, "AM": the text language, American (src/action/engine/XboxSystem.cpp);
//   - driving/misc.viv: 644,839,568 bytes PAL, 187,968,400 US; and the French, German and Spanish speech
//     (driving/mis*fr.spe, ge, sp), PAL only;
//   - default.xbe and Driving.xbe (not copied: the standalone executables do not read them).
// The PAL disc is also the one in Q:\nightfire-re\iso_extract\xbox_eu (checked 25 October 2026).
const KnownRelease kReleases[] = {
    {"PAL", "PAL",
     {{"eurocom/filesys.d00", 133801984, "c6733abcb893e794972ac3fc104ce8fc02563e225543b590666bf545b77c3437"},
      {"driving/bond.dat", 204488, "0f4d3391e4e10dd5fd5abe80d56794c2fbb03c964c6ae6079ae5c5e116b39c88"},
      {"eurocom/25_fps/07100001.xmv", 19148800, "1a8c49c683d54e6dd8ab921cc5821888fdeec6ab8ba2ce3559f9b446bfe8c131"},
      {"driving/misc.viv", 644839568, nullptr}}},
    {"NTSC (US)", "NTSC",
     {{"eurocom/filesys.d00", 133801984, "c6733abcb893e794972ac3fc104ce8fc02563e225543b590666bf545b77c3437"},
      {"driving/bond.dat", 204488, "0f4d3391e4e10dd5fd5abe80d56794c2fbb03c964c6ae6079ae5c5e116b39c88"},
      {"eurocom/30_fps/07100001.xmv", 14721024, "0b88a5e026f4bdc083e4511ad8b6d081d3b22ccc327ed38405a854d1fbf9b8f4"},
      {"eurocom/config.txt", 2, "c8f48a68649d1cae147fb8afd54801e3e82622465c98ff1ee29075da2c201d78"},
      {"driving/misc.viv", 187968400, nullptr}}},
};

bool HashFile(const fs::path &file, std::string *hex, const std::function<void(uint64_t)> &read) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    Sha256 sha;
    std::vector<char> buffer(1 << 20);
    while (in) {
        in.read(buffer.data(), std::streamsize(buffer.size()));
        std::streamsize got = in.gcount();
        if (got <= 0)
            break;
        sha.Update(buffer.data(), size_t(got));
        if (read)
            read(uint64_t(got));
    }
    *hex = sha.FinalHex();
    return true;
}

uint64_t FolderBytes(const fs::path &folder) {
    uint64_t bytes = 0;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(folder, error); it != fs::recursive_directory_iterator();
         it.increment(error)) {
        if (error)
            break;
        if (it->is_regular_file(error))
            bytes += it->file_size(error);
    }
    return bytes;
}

} // namespace

DiscCheck DiscSetup_Check(const fs::path &folder, const DiscProgress &progress) {
    DiscCheck check;
    std::error_code error;
    if (!fs::is_directory(folder, error)) {
        check.problems.push_back("there is no folder " + ToUtf8(folder));
        return check;
    }
    fs::path eurocom = folder / "eurocom", driving = folder / "driving";
    if (!fs::is_directory(eurocom, error) || !fs::is_directory(driving, error)) {
        check.problems.push_back("the folder should hold the disc's eurocom and driving folders");
        return check;
    }
    int archives = 0;
    while (fs::is_regular_file(eurocom / ("filesys.d" + std::string(archives < 10 ? "0" : "") +
                                          std::to_string(archives)),
                               error))
        archives++;
    if (archives == 0)
        check.problems.push_back("eurocom/filesys.d00 is missing");
    else if (archives < 11)
        check.problems.push_back("eurocom/ has filesys.d00 to .d" + std::to_string(archives - 1) +
                                 "; the discs we know have eleven, to .d10");
    bool movies25 = fs::is_directory(eurocom / "25_fps", error), movies30 = fs::is_directory(eurocom / "30_fps", error);
    if (!movies25 && !movies30)
        check.problems.push_back("eurocom/25_fps (or 30_fps), the background movies, is missing");
    if (!fs::is_regular_file(driving / "misc.viv", error))
        check.problems.push_back("driving/misc.viv is missing");

    uint64_t eurocomBytes = FolderBytes(eurocom), drivingBytes = FolderBytes(driving);
    check.bytes = eurocomBytes + drivingBytes;
    if (eurocomBytes < 1000000000ull || drivingBytes < 150000000ull)
        check.problems.push_back("the folders are smaller than the game's data (about 1.5 GB in eurocom, more than "
                                 "150 MB in driving): is the copy complete?");
    check.usable = archives > 0 && (movies25 || movies30) && fs::is_regular_file(driving / "misc.viv", error);
    if (!check.usable)
        return check;

    // The releases: the files of each read once, whichever release names them
    uint64_t toHash = 0, hashed = 0;
    std::vector<std::pair<std::string, std::string>> hashes;   // path, sha256
    for (const KnownRelease &release : kReleases)
        for (const KnownFile &file : release.files)
            if (file.path != nullptr && file.sha256 != nullptr && fs::file_size(folder / file.path, error) == file.size)
                toHash += file.size;
    for (const KnownRelease &release : kReleases) {
        bool matches = true;
        for (const KnownFile &file : release.files) {
            if (file.path == nullptr)
                break;
            fs::path path = folder / file.path;
            if (fs::file_size(path, error) != file.size || error) {
                matches = false;
                break;
            }
            if (file.sha256 == nullptr)
                continue;
            std::string hex;
            for (const auto &known : hashes)
                if (known.first == file.path)
                    hex = known.second;
            if (hex.empty()) {
                HashFile(path, &hex, [&](uint64_t read) {
                    hashed += read;
                    if (progress)
                        progress(hashed, toHash, file.path);
                });
                hashes.emplace_back(file.path, hex);
            }
            if (hex != file.sha256) {
                matches = false;
                break;
            }
        }
        if (matches) {
            check.release = release.name;
            check.region = release.region;
            break;
        }
    }
    if (check.release.empty()) {
        check.region = movies25 ? "PAL" : "NTSC";
        check.problems.push_back("not a version we have tested (we know the PAL and US Xbox discs): it may well work; "
                                 "please report how it goes");
    }
    return check;
}

bool DiscSetup_Copy(const fs::path &from, const fs::path &to, const DiscProgress &progress,
                    const std::atomic<bool> &cancel, std::string *error) {
    uint64_t total = FolderBytes(from / "eurocom") + FolderBytes(from / "driving"), done = 0;
    std::vector<char> buffer(4 << 20);
    for (const char *top : {"eurocom", "driving"}) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(from / top, ec); it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) {
                *error = "cannot read " + ToUtf8(from / top) + ": " + ec.message();
                return false;
            }
            fs::path relative = fs::relative(it->path(), from, ec);
            fs::path target = to / relative;
            if (it->is_directory(ec)) {
                fs::create_directories(target, ec);
                continue;
            }
            fs::create_directories(target.parent_path(), ec);
            std::ifstream in(it->path(), std::ios::binary);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!in || !out) {
                *error = "cannot copy " + ToUtf8(relative) + " to " + ToUtf8(target);
                return false;
            }
            while (in) {
                if (cancel) {
                    *error = "cancelled";
                    return false;
                }
                in.read(buffer.data(), std::streamsize(buffer.size()));
                std::streamsize got = in.gcount();
                if (got <= 0)
                    break;
                out.write(buffer.data(), got);
                if (!out) {
                    *error = "cannot write " + ToUtf8(target) + " (is the disk full?)";
                    return false;
                }
                done += uint64_t(got);
                if (progress)
                    progress(done, total, ToUtf8(relative));
            }
        }
    }
    return true;
}
