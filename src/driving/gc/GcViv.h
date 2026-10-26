#ifndef DRIVING_GC_GCVIV_H_
#define DRIVING_GC_GCVIV_H_

// The driving engine's EA BIGF archives (driving\*.viv, *.mus, *.spe) read whole-entry at a time, for the GameCube
// data's offline checker (docs/gc-driving.md). The GameCube's archives are the Xbox's container: the directory is
// big-endian on both, the header's total size little-endian on both. No game state.

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

struct GcVivEntry {
    std::string name;       // as the directory has it ("data\\track\\paris_mis01.crp")
    uint32_t offset;
    uint32_t size;
};

class GcViv {
public:
    GcViv() : file(NULL) {}
    ~GcViv() { Close(); }

    bool Open(const std::string &path);
    void Close();

    const std::vector<GcVivEntry> &Entries() const { return entries; }
    // The entry of that name (compared without case), or NULL
    const GcVivEntry *Find(const std::string &name) const;
    // The entry's bytes as they are in the archive
    bool Read(const GcVivEntry &entry, std::vector<uint8_t> &out) const;
    // The entry's bytes, unpacked when they are RefPack-packed ("10 FB", as FILE_loadpackz unpacks them)
    bool Load(const GcVivEntry &entry, std::vector<uint8_t> &out) const;

private:
    FILE *file;
    std::vector<GcVivEntry> entries;
};

// Unpacks data in place when it starts with a RefPack header; false when it does not
bool GcViv_Unpack(std::vector<uint8_t> &data);

#endif // DRIVING_GC_GCVIV_H_
