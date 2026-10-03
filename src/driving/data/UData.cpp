#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "UData.h"

#include "../engine/UMemory.hpp"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The three UData helpers the data layer owns (see UData.h): adopting new data, decoding a tag written as text,
// and UGroup's binary search for a tag.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x001178f0)
void UData::AdoptData(void *newData, uint32_t size, uint32_t newCount, bool own) {
    if ((flags & kDataOwned) && data != 0)
        UMemory::Free(reinterpret_cast<void *>(uintptr_t(data)));
    data = uint32_t(uintptr_t(newData));
    // kDataRelative and kDataOwned cleared, the size's bits above them replaced - flags & 0xf9 keeps only the
    // low byte's other bits
    flags = (flags & 0xf9) | (((own & 1) | (size << 6)) << 2);
    count = newCount;
}

// A character of the tag as the original packs it: sign-extended, so a byte above 0x7f sets the bits above it.
static uint32_t TagChar(char c) {
    return uint32_t(int32_t(int8_t(c)));
}

// FUNC_AT(0x001177b0)
uint32_t UDataGroupDecodeTag(const char *text) {
    uint32_t tag = 0;
    if (strlen(text) > 5 && text[0] == '{' && (text[5] == '}' || text[9] == '}')) {
        tag = TagChar(text[1]) << 24 | TagChar(text[2]) << 16 | TagChar(text[3]) << 8 | TagChar(text[4]);
        if (text[5] != '}') {
            // The original's number is an uninitialised local, left as it was when sscanf matches nothing; -1
            // (keep the four characters alone) stands in for it. Every caller passes well-formed tags.
            unsigned int number = 0xffffffff;
            sscanf(text + 5, "%04x", &number);
            if (number != 0xffffffff)
                tag = (tag & 0xffff0000) | number;
        }
    }
    return tag;
}

uint32_t UDataFindTag(uint32_t count, UData **records, uint32_t tag) {
    if (count == 0)
        return 0;
    UData *first = *records;
    uint32_t n = count;
    uint32_t half;
    do {
        half = n >> 1;
        UData *middle = &first[half];
        if (middle->tag > tag) {
            n = half;
        } else if (middle->tag == tag) {
            // found: back up to the first record with the tag
            while (half > 0 && first[half - 1].tag == tag)
                half--;
            *records = &first[half];
            return (*records)->tag;
        } else {
            first = middle + 1;
            n = n - half - 1;
        }
    } while (n > 0);

    uint32_t index = uint32_t(first - *records);
    if (index >= count) {
        *records = first;
        return 0;
    }
    if (first->tag < tag)
        index++;
    *records = *records + index;
    if (index >= count)
        return 0;
    return (*records)->tag;
}

// EBX = count, [esp+4] = records **, [esp+8] = tag; EBX, ESI, EDI and EBP kept, as C++ keeps them.
// AUTOLTCG
__declspec(naked) void FUN_00117840() {
    __asm {
        mov eax, [esp + 8]
        mov edx, [esp + 4]
        push eax
        push edx
        push ebx
        call UDataFindTag
        add esp, 12
        ret
    }
}
