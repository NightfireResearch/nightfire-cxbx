#include "GcUData.h"

static uint32_t Be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void PutLe32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

// The top `bits` bits of the GameCube's word as the Xbox's low bits: its bit 31 is the Xbox's bit 0
static uint32_t FlagBits(uint32_t word, int bits) {
    uint32_t flags = 0;
    for (int i = 0; i < bits; i++)
        if (word & (0x80000000u >> i))
            flags |= 1u << i;
    return flags;
}

static void ConvertRecord(uint8_t *record, uint32_t flags) {
    uint32_t tag = Be32(record);
    uint32_t count = Be32(record + 8);
    uint32_t data = Be32(record + 12);
    PutLe32(record, tag);
    PutLe32(record + 4, flags);
    PutLe32(record + 8, count);
    PutLe32(record + 12, data);
}

// A group's record at offset, its items after it; depth stops a tree that loops
static bool ConvertGroup(uint8_t *data, size_t size, size_t offset, int depth) {
    if (offset + 16 > size || depth > 64)
        return false;
    uint8_t *record = data + offset;
    uint32_t word = Be32(record + 4);
    uint32_t groups = word & 0x07ffffff;
    uint32_t items = Be32(record + 8);
    uint32_t first = Be32(record + 12);
    bool relative = (word & 0x40000000) != 0;
    ConvertRecord(record, FlagBits(word, 5) | groups << 5);
    if (!relative)
        return true;
    size_t base = offset + (size_t)first * 16;
    if (base + ((size_t)groups + items) * 16 > size)
        return false;
    for (uint32_t i = 0; i < groups; i++)
        if (!ConvertGroup(data, size, base + (size_t)i * 16, depth + 1))
            return false;
    for (uint32_t i = 0; i < items; i++) {
        uint8_t *item = data + base + ((size_t)groups + i) * 16;
        uint32_t itemWord = Be32(item + 4);
        uint32_t field = itemWord >> 24 & 0x1f;
        ConvertRecord(item, FlagBits(itemWord, 3) | field << 3 | (itemWord & 0x00ffffff) << 8);
    }
    return true;
}

bool GcUData_ConvertHeaders(uint8_t *data, size_t size) {
    return ConvertGroup(data, size, 0, 0);
}
