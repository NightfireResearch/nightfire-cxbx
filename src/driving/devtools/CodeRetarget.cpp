#include "CodeRetarget.h"

#include <string.h>

#include <windows.h>

namespace {

RetargetTarget *Find(RetargetTarget *targets, int count, uintptr_t address) {
    for (int i = 0; i < count; i++)
        if (targets[i].from == address)
            return &targets[i];
    return NULL;
}

void Write32(uint8_t *at, uint32_t value) {
    DWORD old;
    VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &old);
    memcpy(at, &value, 4);
    VirtualProtect(at, 4, old, &old);
}

int MoveCallSites(uint8_t *base, RetargetTarget *targets, int count) {
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    int moved = 0;
    for (int s = 0; s < nt->FileHeader.NumberOfSections; s++, section++) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
            continue;
        uint8_t *code = base + section->VirtualAddress;
        size_t size = section->Misc.VirtualSize;
        DWORD old;
        if (!VirtualProtect(code, size, PAGE_EXECUTE_READWRITE, &old))
            continue;
        for (size_t i = 0; i + 6 <= size; i++) {
            size_t length;
            if (code[i] == 0xe8 || code[i] == 0xe9)
                length = 5;
            else if (code[i] == 0x0f && (code[i + 1] & 0xf0) == 0x80)
                length = 6;
            else
                continue;
            uint8_t *next = code + i + length;
            int32_t displacement;
            memcpy(&displacement, next - 4, 4);
            RetargetTarget *t = Find(targets, count, (uintptr_t)next + displacement);
            if (t == NULL)
                continue;
            displacement = (int32_t)(t->to - (uintptr_t)next);
            memcpy(next - 4, &displacement, 4);
            t->sites++;
            moved++;
            i += length - 1;
        }
        VirtualProtect(code, size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), code, size);
    }
    return moved;
}

int MoveAddresses(uint8_t *base, RetargetTarget *targets, int count, const void *keep, size_t keepBytes) {
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint8_t *block = base + directory.VirtualAddress;
    uint8_t *end = block + directory.Size;
    const uint8_t *keepStart = (const uint8_t *)keep, *keepEnd = keepStart + keepBytes;
    int moved = 0;
    while (directory.Size != 0 && block < end) {
        IMAGE_BASE_RELOCATION *page = (IMAGE_BASE_RELOCATION *)block;
        if (page->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION))
            break;
        int entries = (int)((page->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD));
        const WORD *entry = (const WORD *)(page + 1);
        for (int i = 0; i < entries; i++) {
            if ((entry[i] >> 12) != IMAGE_REL_BASED_HIGHLOW)
                continue;
            uint8_t *at = base + page->VirtualAddress + (entry[i] & 0xfff);
            if (at >= keepStart && at < keepEnd)
                continue;
            uint32_t value;
            memcpy(&value, at, 4);
            RetargetTarget *t = Find(targets, count, value);
            if (t == NULL)
                continue;
            Write32(at, (uint32_t)t->to);
            t->sites++;
            moved++;
        }
        block += page->SizeOfBlock;
    }
    return moved;
}

}  // namespace

int Retarget(RetargetTarget *targets, int count, const void *keep, size_t keepBytes) {
    HMODULE module = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&Retarget, &module))
        return 0;
    // Folded targets: the first one's wrapper serves them all
    for (int i = 0; i < count; i++) {
        targets[i].sites = 0;
        RetargetTarget *first = Find(targets, count, targets[i].from);
        if (first != &targets[i])
            targets[i].to = first->to;
    }
    uint8_t *base = (uint8_t *)module;
    return MoveCallSites(base, targets, count) + MoveAddresses(base, targets, count, keep, keepBytes);
}
