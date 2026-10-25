#include "Xdvdfs.h"

#include <string.h>

#include <set>

#include "Platform.h"

namespace {

const uint64_t kSector = 2048;
const char kMagic[] = "MICROSOFT*XBOX*MEDIA";
const size_t kMagicLength = 20;
const uint8_t kDirectory = 0x10;

// Where the game partition starts: a trimmed xiso, then the full images of the three disc generations (XGD1, the
// original Xbox's, is Nightfire's; XGD2 and XGD3 are the Xbox 360's, whose backwards-compatible discs use the same
// file system)
const uint64_t kPartitions[] = {0, 0x18300000, 0xFD90000, 0x2080000};

const uint32_t kMaxTable = 16u << 20;   // a directory table larger than this is damage, not a directory
const size_t kMaxEntries = 200000;
const int kMaxDepth = 32;

uint16_t Read16(const uint8_t *p) {
    return uint16_t(p[0] | p[1] << 8);
}

uint32_t Read32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// A name the extraction can write as one path component
bool PlainName(const std::string &name) {
    if (name.empty() || name == "." || name == "..")
        return false;
    for (unsigned char c : name)
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|')
            return false;
    return true;
}

} // namespace

bool XdvdfsImage::Read(uint64_t offset, void *buffer, size_t size) {
    if (offset > m_imageSize || size > m_imageSize - offset)
        return false;
    m_file.clear();
    m_file.seekg(std::streamoff(offset));
    m_file.read(static_cast<char *>(buffer), std::streamsize(size));
    return size_t(m_file.gcount()) == size;
}

bool XdvdfsImage::Open(const std::filesystem::path &image, std::string *error) {
    m_file.open(image, std::ios::binary);
    std::error_code ec;
    m_imageSize = std::filesystem::file_size(image, ec);
    if (!m_file || ec) {
        *error = "cannot read " + ToUtf8(image);
        return false;
    }
    uint8_t descriptor[kSector];
    for (uint64_t partition : kPartitions) {
        if (!Read(partition + 32 * kSector, descriptor, sizeof(descriptor)))
            continue;
        if (memcmp(descriptor, kMagic, kMagicLength) != 0 || memcmp(descriptor + 2028, kMagic, kMagicLength) != 0)
            continue;
        m_partition = partition;
        m_rootSector = Read32(descriptor + 20);
        m_rootSize = Read32(descriptor + 24);
        return true;
    }
    *error = "not an Xbox disc image (no Xbox volume descriptor found)";
    return false;
}

bool XdvdfsImage::List(std::vector<XdvdfsEntry> *entries, std::string *error) {
    struct Directory {
        uint32_t sector, size;
        std::string prefix;
        int depth;
    };
    std::vector<Directory> directories = {{m_rootSector, m_rootSize, "", 0}};
    std::set<uint32_t> tablesSeen;
    entries->clear();
    uint64_t partitionSize = m_imageSize - m_partition;

    while (!directories.empty()) {
        Directory directory = directories.back();
        directories.pop_back();
        std::string where = directory.prefix.empty() ? "the root directory" : directory.prefix;
        if (directory.size == 0)
            continue;   // an empty directory
        if (directory.depth > kMaxDepth || !tablesSeen.insert(directory.sector).second) {
            *error = "the image is damaged: " + where + " loops back on itself";
            return false;
        }
        if (directory.size > kMaxTable || directory.sector * kSector + directory.size > partitionSize) {
            *error = "the image is damaged or cut short: " + where + "'s table lies past the end of the file";
            return false;
        }
        std::vector<uint8_t> table(directory.size);
        if (!Read(m_partition + directory.sector * kSector, table.data(), table.size())) {
            *error = "cannot read " + where;
            return false;
        }
        if (Read16(table.data()) == 0xFFFF)
            continue;   // a directory with no entries: its sector is padding

        std::vector<uint32_t> pending = {0};
        std::set<uint32_t> visited;
        while (!pending.empty()) {
            uint32_t at = pending.back() * 4;
            pending.pop_back();
            if (!visited.insert(at).second || at + 14 > table.size()) {
                *error = "the image is damaged: a bad entry in " + where;
                return false;
            }
            const uint8_t *entry = table.data() + at;
            uint16_t left = Read16(entry), right = Read16(entry + 2);
            uint32_t sector = Read32(entry + 4), size = Read32(entry + 8);
            uint8_t attributes = entry[12], nameLength = entry[13];
            if (nameLength == 0 || at + 14 + nameLength > table.size()) {
                *error = "the image is damaged: a bad entry in " + where;
                return false;
            }
            std::string name(reinterpret_cast<const char *>(entry + 14), nameLength);
            if (!PlainName(name)) {
                *error = "the image is damaged: an entry in " + where + " has an unusable name";
                return false;
            }
            std::string path = directory.prefix.empty() ? name : directory.prefix + "/" + name;
            if (uint64_t(sector) * kSector + size > partitionSize) {
                *error = "the image is damaged or cut short: " + path + " lies past the end of the file";
                return false;
            }
            bool isDirectory = (attributes & kDirectory) != 0;
            entries->push_back({path, m_partition + uint64_t(sector) * kSector, size, isDirectory});
            if (entries->size() > kMaxEntries) {
                *error = "the image is damaged: too many entries";
                return false;
            }
            if (isDirectory)
                directories.push_back({sector, size, path, directory.depth + 1});
            if (right != 0)
                pending.push_back(right);
            if (left != 0)
                pending.push_back(left);
        }
    }
    return true;
}
