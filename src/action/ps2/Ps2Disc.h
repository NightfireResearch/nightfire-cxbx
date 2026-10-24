#ifndef PS2_PS2DISC_H_
#define PS2_PS2DISC_H_

// The PS2 disc's layout (docs/ps2-data.md): the action engine's files are all in FILES.BIN, at the places its
// table FileList (in ACTION.ELF) gives; the sound, music and streams are loose files under PS2\, which the IOP's
// sound driver reads. Used by FS_ps2.cpp and by the offline checker, so it touches no game state.

#include <stddef.h>
#include <stdint.h>

#define PS2_SECTOR_SIZE 0x800
#define PS2_FILELIST_COUNT 118            // psiFileOpen searches 0x76 entries

// A FileList entry (ACTION.ELF 0x002453a0 in SLES-51258)
#pragma pack(push, 1)
struct Ps2FileListEntry {
    char name[16];          // "07000001.bin", "UKTxt.dat"; psiFileOpen compares upper-cased
    uint32_t offset;        // 0x10 - bytes into FILES.BIN, a whole number of sectors
    uint32_t size;          // 0x14
    uint8_t pad[8];
};
#pragma pack(pop)
static_assert(sizeof(Ps2FileListEntry) == 0x20, "Ps2FileListEntry is 0x20 bytes");

// A symbol's data in an ELF file read whole: its offset in the file and its size, from the symbol table. False when
// the file has no such symbol or it lies in no section with file data.
bool Ps2Elf_FindSymbol(const uint8_t *elf, size_t elfSize, const char *name, uint32_t *fileOffset, uint32_t *size);

// The FileList of ACTION.ELF read whole, copied to entries (up to max); the entries found, 0 if none
int Ps2Disc_ReadFileList(const uint8_t *elf, size_t elfSize, Ps2FileListEntry *entries, int max);

// The FileList entry named, compared as psiFileOpen does (both upper-cased); -1 for none
int Ps2Disc_FindFile(const Ps2FileListEntry *entries, int count, const char *name);

// The PS2's level bundles carry files the Xbox keeps on their own: the pause screen's film, woman.rle on the Xbox, is
// each level bundle's entry of type 16 named "woman" (byte for byte the Xbox's file, padded).
#define PS2_BUNDLE_WOMAN_TYPE 16
#define PS2_BUNDLE_WOMAN_NAME "woman"

// An entry of a bundle from its first sector and directory (as LoaderLoad reads them: the header sector, then
// dirSize bytes): its offset from the bundle's start and its size. False when the bundle has no such entry.
bool Ps2Disc_FindBundleEntry(const uint8_t *header, const uint8_t *directory, uint32_t directoryBytes, int type,
                             const char *name, uint32_t *offset, uint32_t *size);

// A loose file's path on the disc from the name the game asks for: "PS2\" and the name upper-cased, with '\' for '/'
// ("ENGLISH/SB_0/SB_0.SBF" is "PS2\ENGLISH\SB_0\SB_0.SBF")
void Ps2Disc_LoosePath(const char *name, char *out, size_t outSize);

#endif // PS2_PS2DISC_H_
