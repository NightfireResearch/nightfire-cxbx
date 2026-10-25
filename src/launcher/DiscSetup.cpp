#include "DiscSetup.h"

#include <algorithm>
#include <fstream>
#include <map>

#include "Platform.h"
#include "Sha256.h"
#include "Xdvdfs.h"

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

std::string Lower(std::string text) {
    for (char &c : text)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return text;
}

// What the game reads: eurocom/ and driving/
bool GameFile(const std::string &lowerPath) {
    return lowerPath.compare(0, 8, "eurocom/") == 0 || lowerPath.compare(0, 8, "driving/") == 0;
}

// The game's files in a folder or an image, by their lowercase paths
struct Listing {
    struct File {
        std::string path;   // as the source spells it
        uint64_t size;
        uint64_t offset;    // in an image
    };
    std::map<std::string, File> files;

    const File *Find(const std::string &path) const {
        auto it = files.find(path);
        return it == files.end() ? nullptr : &it->second;
    }
    bool HasFolder(const std::string &path) const {
        auto it = files.lower_bound(path + "/");
        return it != files.end() && it->first.compare(0, path.size() + 1, path + "/") == 0;
    }
    uint64_t Bytes(const std::string &folder) const {
        uint64_t bytes = 0;
        for (const auto &file : files)
            if (file.first.compare(0, folder.size() + 1, folder + "/") == 0)
                bytes += file.second.size;
        return bytes;
    }
};

void ListFolder(const fs::path &folder, Listing *listing) {
    for (const char *top : {"eurocom", "driving"}) {
        std::error_code error;
        for (auto it = fs::recursive_directory_iterator(folder / top, error); it != fs::recursive_directory_iterator();
             it.increment(error)) {
            if (error)
                break;
            if (!it->is_regular_file(error))
                continue;
            std::string path = ToUtf8(fs::relative(it->path(), folder, error).generic_wstring());
            listing->files[Lower(path)] = {path, it->file_size(error), 0};
        }
    }
}

bool ListImage(XdvdfsImage &image, Listing *listing, std::string *error) {
    std::vector<XdvdfsEntry> entries;
    if (!image.List(&entries, error))
        return false;
    for (const XdvdfsEntry &entry : entries) {
        std::string lower = Lower(entry.path);
        if (!entry.directory && GameFile(lower))
            listing->files[lower] = {entry.path, entry.size, entry.offset};
    }
    return true;
}

// The folders and files the game needs, and the release whose key files have the sizes found
void CheckListing(const Listing &listing, DiscCheck *check) {
    if (!listing.HasFolder("eurocom") || !listing.HasFolder("driving")) {
        check->problems.push_back(check->image ? "the image holds no eurocom and driving folders: is it 007: Nightfire?"
                                               : "the folder should hold the disc's eurocom and driving folders");
        return;
    }
    int archives = 0;
    while (listing.Find("eurocom/filesys.d" + std::string(archives < 10 ? "0" : "") + std::to_string(archives)))
        archives++;
    if (archives == 0)
        check->problems.push_back("eurocom/filesys.d00 is missing");
    else if (archives < 11)
        check->problems.push_back("eurocom/ has filesys.d00 to .d" + std::to_string(archives - 1) +
                                  "; the discs we know have eleven, to .d10");
    bool movies25 = listing.HasFolder("eurocom/25_fps"), movies30 = listing.HasFolder("eurocom/30_fps");
    if (!movies25 && !movies30)
        check->problems.push_back("eurocom/25_fps (or 30_fps), the background movies, is missing");
    if (!listing.Find("driving/misc.viv"))
        check->problems.push_back("driving/misc.viv is missing");

    uint64_t eurocomBytes = listing.Bytes("eurocom"), drivingBytes = listing.Bytes("driving");
    check->bytes = eurocomBytes + drivingBytes;
    if (eurocomBytes < 1000000000ull || drivingBytes < 150000000ull)
        check->problems.push_back("the folders are smaller than the game's data (about 1.5 GB in eurocom, more than "
                                  "150 MB in driving): is the copy complete?");
    check->usable = archives > 0 && (movies25 || movies30) && listing.Find("driving/misc.viv") != nullptr;
    check->region = movies25 ? "PAL" : "NTSC";
    for (const KnownRelease &release : kReleases) {
        bool sizes = true;
        for (const KnownFile &file : release.files) {
            if (file.path == nullptr)
                break;
            const Listing::File *found = listing.Find(file.path);
            sizes = sizes && found != nullptr && found->size == file.size;
        }
        if (sizes) {
            check->release = release.name;
            check->region = release.region;
            break;
        }
    }
}

void NotTested(DiscCheck *check) {
    check->problems.push_back("not a version we have tested (we know the PAL and US Xbox discs): it may well work; "
                              "please report how it goes");
}

} // namespace

bool DiscSetup_IsImage(const fs::path &source) {
    std::error_code error;
    return fs::is_regular_file(source, error);
}

DiscCheck DiscSetup_Check(const fs::path &source, const DiscProgress &progress) {
    DiscCheck check;
    std::error_code error;
    Listing listing;
    check.image = DiscSetup_IsImage(source);
    if (check.image) {
        // An image: its file system read, and the release told by its files' sizes; the hashes come after the copy
        XdvdfsImage image;
        std::string why;
        if (!image.Open(source, &why) || !ListImage(image, &listing, &why)) {
            check.problems.push_back(why);
            return check;
        }
        CheckListing(listing, &check);
        if (check.usable && check.release.empty())
            NotTested(&check);
        return check;
    }
    if (!fs::is_directory(source, error)) {
        check.problems.push_back("there is no folder or disc image " + ToUtf8(source));
        return check;
    }
    ListFolder(source, &listing);
    CheckListing(listing, &check);
    if (!check.usable)
        return check;

    // A folder: the release its sizes name, confirmed by the key files' hashes
    std::string sized = check.release;
    check.release.clear();
    for (const KnownRelease &release : kReleases) {
        if (sized != release.name)
            continue;
        uint64_t toHash = 0, hashed = 0;
        for (const KnownFile &file : release.files)
            if (file.path != nullptr && file.sha256 != nullptr)
                toHash += file.size;
        bool matches = true;
        for (const KnownFile &file : release.files) {
            if (file.path == nullptr || !matches)
                break;
            if (file.sha256 == nullptr)
                continue;
            std::string hex;
            HashFile(source / FromUtf8(listing.Find(file.path)->path), &hex, [&](uint64_t read) {
                hashed += read;
                if (progress)
                    progress(hashed, toHash, file.path);
            });
            matches = hex == file.sha256;
        }
        if (matches) {
            check.release = release.name;
            check.confirmed = true;
        }
    }
    if (check.release.empty())
        NotTested(&check);
    return check;
}

bool DiscSetup_Copy(const fs::path &from, const fs::path &to, const DiscProgress &progress,
                    const std::atomic<bool> &cancel, std::string *error) {
    Listing listing;
    XdvdfsImage image;
    bool fromImage = DiscSetup_IsImage(from);
    if (fromImage) {
        if (!image.Open(from, error) || !ListImage(image, &listing, error))
            return false;
    } else {
        ListFolder(from, &listing);
    }
    uint64_t total = listing.Bytes("eurocom") + listing.Bytes("driving"), done = 0;

    std::error_code ec;
    fs::create_directories(to, ec);
    fs::space_info space = fs::space(to, ec);
    if (!ec && space.available < total + (100u << 20)) {
        *error = "not enough free space in " + ToUtf8(to) + ": the data needs " + std::to_string(total / 1000000) +
                 " MB, and " + std::to_string(space.available / 1000000) + " MB are free";
        return false;
    }

    std::vector<char> buffer(4 << 20);
    for (const auto &item : listing.files) {
        const Listing::File &file = item.second;
        fs::path target = to / FromUtf8(file.path);
        fs::create_directories(target.parent_path(), ec);
        std::ifstream in;
        if (!fromImage) {
            in.open(from / FromUtf8(file.path), std::ios::binary);
            if (!in) {
                *error = "cannot read " + file.path;
                return false;
            }
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            *error = "cannot write " + ToUtf8(target);
            return false;
        }
        for (uint64_t at = 0; at < file.size;) {
            if (cancel) {
                *error = "cancelled";
                return false;
            }
            size_t chunk = size_t(std::min<uint64_t>(buffer.size(), file.size - at));
            bool read = fromImage ? image.Read(file.offset + at, buffer.data(), chunk)
                                  : bool(in.read(buffer.data(), std::streamsize(chunk)));
            if (!read) {
                *error = "cannot read " + file.path + (fromImage ? " from the image (is it damaged?)" : "");
                return false;
            }
            out.write(buffer.data(), std::streamsize(chunk));
            if (!out) {
                *error = "cannot write " + ToUtf8(target) + " (is the disk full?)";
                return false;
            }
            at += chunk;
            done += chunk;
            if (progress)
                progress(done, total, file.path);
        }
    }
    return true;
}
