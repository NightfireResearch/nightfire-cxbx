#ifndef LAUNCHER_XDVDFS_H_
#define LAUNCHER_XDVDFS_H_

// The Xbox disc's file system (XDVDFS), read from a disc image: a full ("redump") image, whose game partition starts
// some way in, or a trimmed xiso, whose partition starts at 0. Sectors are 2048 bytes.
//
//   sector 32 of the partition   the volume descriptor: "MICROSOFT*XBOX*MEDIA", the root directory's sector and size
//                                (uint32 each), a time stamp, and the same 20 bytes again at byte 2028
//   a directory                  a table of entries forming a binary tree, sorted by name:
//                                  uint16 left, right    the subtrees' entries, in dwords from the table's start
//                                                        (0: none)
//                                  uint32 sector, size   the file's data, or a subdirectory's table
//                                  uint8 attributes      0x10: a directory
//                                  uint8 nameLength, then the name, the entry padded to 4 bytes
//                                An entry does not cross a sector: the rest of one is filled with 0xFF.
//
// Every offset and size is checked against the table and the image, so a damaged or hostile image fails with a
// reason rather than reading out of bounds or looping.

#include <stdint.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

struct XdvdfsEntry {
    std::string path;   // relative, '/' between names, as the disc spells them
    uint64_t offset;    // the data's position in the image file
    uint64_t size;
    bool directory;
};

class XdvdfsImage {
public:
    // The image opened and its volume descriptor found; false and why if it is not an Xbox disc image
    bool Open(const std::filesystem::path &image, std::string *error);

    uint64_t ImageSize() const { return m_imageSize; }
    uint64_t PartitionOffset() const { return m_partition; }

    // Every file and directory, depth first; false and why if a directory table is damaged
    bool List(std::vector<XdvdfsEntry> *entries, std::string *error);

    bool Read(uint64_t offset, void *buffer, size_t size);

private:
    std::ifstream m_file;
    uint64_t m_imageSize = 0;
    uint64_t m_partition = 0;
    uint32_t m_rootSector = 0;
    uint32_t m_rootSize = 0;
};

#endif // LAUNCHER_XDVDFS_H_
