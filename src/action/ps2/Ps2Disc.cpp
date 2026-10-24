#include "Ps2Disc.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>

// ELF32, little endian (the EE's)
#pragma pack(push, 1)
struct Elf32Header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Elf32Section {
    uint32_t name, type, flags, addr, offset, size, link, info, addralign, entsize;
};
struct Elf32Symbol {
    uint32_t name, value, size;
    uint8_t info, other;
    uint16_t shndx;
};
#pragma pack(pop)
static_assert(sizeof(Elf32Header) == 0x34, "Elf32Header is 0x34 bytes");
static_assert(sizeof(Elf32Section) == 0x28, "Elf32Section is 0x28 bytes");
static_assert(sizeof(Elf32Symbol) == 0x10, "Elf32Symbol is 0x10 bytes");

#define SHT_PROGBITS 1
#define SHT_SYMTAB 2

static const Elf32Section *ElfSection(const uint8_t *elf, size_t elfSize, uint32_t index) {
    const Elf32Header *header = (const Elf32Header *)elf;
    size_t at = header->shoff + (size_t)index * sizeof(Elf32Section);
    if (index >= header->shnum || at + sizeof(Elf32Section) > elfSize)
        return NULL;
    return (const Elf32Section *)(elf + at);
}

bool Ps2Elf_FindSymbol(const uint8_t *elf, size_t elfSize, const char *name, uint32_t *fileOffset, uint32_t *size) {
    if (elfSize < sizeof(Elf32Header) || memcmp(elf, "\x7f" "ELF", 4) != 0 || elf[4] != 1 || elf[5] != 1)
        return false;
    const Elf32Header *header = (const Elf32Header *)elf;
    for (uint32_t s = 0; s < header->shnum; s++) {
        const Elf32Section *symtab = ElfSection(elf, elfSize, s);
        if (symtab == NULL || symtab->type != SHT_SYMTAB)
            continue;
        const Elf32Section *strtab = ElfSection(elf, elfSize, symtab->link);
        if (strtab == NULL || symtab->offset + symtab->size > elfSize || strtab->offset + strtab->size > elfSize)
            return false;
        const Elf32Symbol *symbols = (const Elf32Symbol *)(elf + symtab->offset);
        const char *strings = (const char *)(elf + strtab->offset);
        for (uint32_t i = 0; i < symtab->size / sizeof(Elf32Symbol); i++) {
            if (symbols[i].name >= strtab->size || strcmp(strings + symbols[i].name, name) != 0)
                continue;
            // The section holding the address, for its place in the file
            for (uint32_t d = 0; d < header->shnum; d++) {
                const Elf32Section *data = ElfSection(elf, elfSize, d);
                if (data == NULL || data->type != SHT_PROGBITS)
                    continue;
                uint32_t value = symbols[i].value;
                if (value >= data->addr && value < data->addr + data->size) {
                    *fileOffset = data->offset + (value - data->addr);
                    *size = symbols[i].size;
                    return *fileOffset + *size <= elfSize;
                }
            }
            return false;
        }
    }
    return false;
}

int Ps2Disc_ReadFileList(const uint8_t *elf, size_t elfSize, Ps2FileListEntry *entries, int max) {
    uint32_t at, size;
    if (!Ps2Elf_FindSymbol(elf, elfSize, "FileList", &at, &size))
        return 0;
    int count = (int)(size / sizeof(Ps2FileListEntry));
    if (count > max)
        count = max;
    memcpy(entries, elf + at, count * sizeof(Ps2FileListEntry));
    // The table's tail is empty entries
    while (count > 0 && entries[count - 1].name[0] == '\0')
        count--;
    return count;
}

// psiFileOpen's comparison: both names upper-cased (strupr), then strcmp
static bool SameName(const char *a, const char *b, size_t limit) {
    for (size_t i = 0; i < limit; i++) {
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i]))
            return false;
        if (a[i] == '\0')
            return true;
    }
    return b[limit] == '\0';
}

int Ps2Disc_FindFile(const Ps2FileListEntry *entries, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (SameName(entries[i].name, name, sizeof(entries[i].name)))
            return i;
    }
    return -1;
}

bool Ps2Disc_FindBundleEntry(const uint8_t *header, const uint8_t *directory, uint32_t directoryBytes, int type,
                             const char *name, uint32_t *offset, uint32_t *size) {
    uint32_t dirSize, count;
    memcpy(&dirSize, header, 4);
    memcpy(&count, header + 4, 4);
    uint32_t data = PS2_SECTOR_SIZE + dirSize;   // the files follow the directory, in its order
    uint32_t at = 0;
    for (uint32_t i = 0; i < count && at + 5 < directoryBytes; i++) {
        uint32_t len;
        memcpy(&len, directory + at, 4);
        int entryType = directory[at + 4];
        const char *entryName = (const char *)directory + at + 5;
        size_t nameLength = strnlen(entryName, directoryBytes - at - 5);
        if (entryType == type && strcmp(entryName, name) == 0) {
            *offset = data;
            *size = len;
            return true;
        }
        at += 5 + (uint32_t)nameLength + 1;
        data += len;
    }
    return false;
}

void Ps2Disc_LoosePath(const char *name, char *out, size_t outSize) {
    size_t n = (size_t)snprintf(out, outSize, "PS2\\");
    for (; *name != '\0' && n + 1 < outSize; name++)
        out[n++] = *name == '/' ? '\\' : (char)toupper((unsigned char)*name);
    out[n] = '\0';
}
