#include "Ps2ExecutableData.h"
#include "Ps2Disc.h"
#include "../gfx/Font.h"

#include <stdio.h>
#include <string.h>

// The PS2's records are laid out as ours (letter, texture, its size, the box in it, the size drawn)
static void ReadSpecialChars(const uint8_t *elf, size_t elfSize) {
    uint32_t at, size;
    if (!Ps2Elf_FindSymbol(elf, elfSize, "specialchar", &at, &size)) {
        printf("[ps2] ACTION.ELF has no specialchar: the Xbox's button symbols stay\n");
        return;
    }
    size_t count = size / sizeof(SPECIALCHAR_tag);
    if (count > sizeof(SpecialChars) / sizeof(SpecialChars[0]))
        count = sizeof(SpecialChars) / sizeof(SpecialChars[0]);
    memset(SpecialChars, 0, sizeof(SpecialChars));
    memcpy(SpecialChars, elf + at, count * sizeof(SPECIALCHAR_tag));
    printf("[ps2] %zu button symbols from ACTION.ELF\n", count);
}

void Ps2_ReadExecutableData(const uint8_t *elf, size_t elfSize) {
    ReadSpecialChars(elf, elfSize);
}
