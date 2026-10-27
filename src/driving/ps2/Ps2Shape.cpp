#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The PS2's image containers (SHPS) as the Xbox's (SHPX) (docs/ps2-driving.md, "Textures"). The directory is the
// same; each image becomes a linear 32-bit one (type 0x7d, flags 0: one level, not swizzled) with its attachments
// but the palette. PS2 images: 16-byte headers, the pixels after them; type 1 four bits a pixel (the first in the
// low half), type 2 eight, each with a palette attachment (0x21: as many RGBA colours as its width, an 8-bit
// image's indices in the GS's CSM1 order, bits 3 and 4 exchanged), type 4 RGB, type 5 RGBA; alpha 0x80 is opaque.
// Only the first mip level is kept.

#include "Ps2Data.h"

#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace {

struct ShapeFileHeader {             // SHPS / SHPX
    char magic[4];                   // +0x00
    uint32_t size;                   // +0x04
    uint32_t count;                  // +0x08
    char id[4];                      // +0x0c
};

struct ShapeDirectoryEntry {         // from +0x10
    char name[4];
    uint32_t offset;
};

struct Ps2ShapeImage {               // 0x10
    uint32_t typeAndNext;            // +0x00 type in the low byte, the next attachment's offset above
    uint16_t width;                  // +0x04
    uint16_t height;                 // +0x06
    uint32_t unknown08;              // +0x08
    uint32_t flags;                  // +0x0c bits 28..31 mip levels - 1

    uint32_t Type() const { return typeAndNext & 0xff; }
    uint32_t Next() const { return typeAndNext >> 8; }
};

struct XboxShapeImage {              // 0x14 (EAGL::ShapeImage)
    uint32_t typeAndNext;
    uint16_t width;
    uint16_t height;
    uint32_t unknown08;
    uint32_t flags;
    uint32_t dataOffset;
};

enum Ps2ShapeType : uint32_t {
    kPs2Indexed4 = 1,
    kPs2Indexed8 = 2,
    kPs2Rgb = 4,
    kPs2Rgba = 5,
    kPs2Palette = 0x21,
};

constexpr uint32_t kXboxLinearArgb = 0x7d;
constexpr uint8_t kPs2Opaque = 0x80;

int PaletteSlot(int index) {
    return (index & ~0x18) | ((index & 0x08) << 1) | ((index & 0x10) >> 1);
}

void PutPixel(uint8_t *bgra, const uint8_t *rgba) {
    bgra[0] = rgba[2];
    bgra[1] = rgba[1];
    bgra[2] = rgba[0];
    bgra[3] = rgba[3] >= kPs2Opaque ? 0xff : uint8_t(rgba[3] * 2);
}

// One image and its attachments (from `at` to `end`) appended to `out` as the Xbox's
void ConvertImage(const uint8_t *file, uint32_t at, uint32_t end, std::vector<uint8_t> *out) {
    const Ps2ShapeImage *image = reinterpret_cast<const Ps2ShapeImage *>(file + at);
    uint32_t type = image->Type();
    if (type != kPs2Indexed4 && type != kPs2Indexed8 && type != kPs2Rgb && type != kPs2Rgba) {
        out->insert(out->end(), file + at, file + end);
        return;
    }
    // The attachments after the pixels: the palette is used, the rest kept
    const uint8_t *palette = NULL;
    uint32_t colours = 0;
    std::vector<std::pair<uint32_t, uint32_t>> kept;
    uint32_t a = image->Next() != 0 ? at + image->Next() : end;
    while (a < end) {
        const Ps2ShapeImage *attachment = reinterpret_cast<const Ps2ShapeImage *>(file + a);
        uint32_t next = attachment->Next() != 0 ? a + attachment->Next() : end;
        if (attachment->Type() == kPs2Palette) {
            palette = file + a + sizeof(Ps2ShapeImage);
            colours = attachment->width;
        } else {
            kept.push_back(std::make_pair(a, (std::min)(next, end)));
        }
        if (attachment->Next() == 0)
            break;
        a = next;
    }

    uint32_t width = image->width, height = image->height;
    size_t start = out->size();
    out->resize(start + sizeof(XboxShapeImage) + width * height * 4);
    XboxShapeImage header = {};
    uint32_t pixelBlock = uint32_t(sizeof(XboxShapeImage) + width * height * 4);
    header.typeAndNext = kXboxLinearArgb | (kept.empty() ? 0 : pixelBlock << 8);
    header.width = uint16_t(width);
    header.height = uint16_t(height);
    header.unknown08 = image->unknown08;
    memcpy(&(*out)[start], &header, sizeof(header));
    uint8_t *bgra = &(*out)[start + sizeof(XboxShapeImage)];
    const uint8_t *pixels = file + at + sizeof(Ps2ShapeImage);
    static const uint8_t kBlack[4] = { 0, 0, 0, kPs2Opaque };
    for (uint32_t i = 0; i < width * height; i++) {
        uint8_t rgba[4] = { 0, 0, 0, kPs2Opaque };
        const uint8_t *colour = rgba;
        if (type == kPs2Rgba) {
            colour = pixels + i * 4;
        } else if (type == kPs2Rgb) {
            memcpy(rgba, pixels + i * 3, 3);
        } else {
            uint32_t index = type == kPs2Indexed8 ? pixels[i] : pixels[i / 2] >> (i & 1) * 4 & 0xf;
            if (type == kPs2Indexed8)
                index = PaletteSlot(int(index));
            colour = palette != NULL && index < colours ? palette + index * 4 : kBlack;
        }
        PutPixel(bgra + i * 4, colour);
    }
    for (size_t k = 0; k < kept.size(); k++) {
        size_t here = out->size();
        out->insert(out->end(), file + kept[k].first, file + kept[k].second);
        uint32_t next = k + 1 < kept.size() ? kept[k].second - kept[k].first : 0;
        uint32_t word;
        memcpy(&word, &(*out)[here], 4);
        word = (word & 0xff) | next << 8;
        memcpy(&(*out)[here], &word, 4);
    }
    while (out->size() % 16 != 0)
        out->push_back(0);
}

}  // namespace

void *Ps2_ConvertFile(void *data, int *size, const char *path, int flags, FileMallocFn allocate,
                      FileFreeFn release) {
    const uint8_t *file = static_cast<const uint8_t *>(data);
    if (file == NULL || memcmp(file, "SHPS", 4) != 0)
        return data;
    const ShapeFileHeader *header = reinterpret_cast<const ShapeFileHeader *>(file);
    const ShapeDirectoryEntry *directory = reinterpret_cast<const ShapeDirectoryEntry *>(file + sizeof(*header));
    uint32_t fileSize = header->size;
    std::vector<uint32_t> starts;
    for (uint32_t i = 0; i < header->count; i++)
        starts.push_back(directory[i].offset);
    std::sort(starts.begin(), starts.end());
    uint32_t first = starts.empty() ? fileSize : starts[0];

    std::vector<uint8_t> out(file, file + first);
    std::vector<uint32_t> converted(header->count);
    for (uint32_t i = 0; i < header->count; i++) {
        uint32_t at = directory[i].offset;
        auto later = std::upper_bound(starts.begin(), starts.end(), at);
        uint32_t end = later != starts.end() ? *later : fileSize;
        converted[i] = uint32_t(out.size());
        ConvertImage(file, at, end, &out);
    }
    ShapeFileHeader *outHeader = reinterpret_cast<ShapeFileHeader *>(out.data());
    memcpy(outHeader->magic, "SHPX", 4);
    outHeader->size = uint32_t(out.size());
    ShapeDirectoryEntry *outDirectory = reinterpret_cast<ShapeDirectoryEntry *>(out.data() + sizeof(*header));
    for (uint32_t i = 0; i < header->count; i++)
        outDirectory[i].offset = converted[i];

    void *result = allocate(path, int(out.size()), flags);
    if (result == NULL)
        return data;
    memcpy(result, out.data(), out.size());
    release(data);
    if (size != NULL)
        *size = int(out.size());
    return result;
}
