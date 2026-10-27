#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// The GameCube's image containers (SHPG) as the Xbox's (SHPX), alone, in galleries and as fonts' images
// (docs/gc-driving.md, "Textures"). The directory is the same, its words big-endian; the file's size word is
// little-endian on both. A GameCube image's header is 16 bytes (type in the first byte, the 24-bit offset to the
// next attachment after it, width and height, a word, a flags word: big-endian), its texels the GX's tiles after it;
// a palette attachment (0x30 IA8, 0x31 RGB565, 0x32 RGB5A3) holds its width in 16-bit colours. Each image becomes:
// CMPR the Xbox's DXT1 (0x60: the same blocks, reordered), C4 and C8 with sides that are powers of two an 8-bit one
// (0x7b, swizzled) with a 256-colour palette attachment (0x2a), anything else 32-bit (0x7d, swizzled when its sides
// are powers of two). The GameCube's info attachment ('i') is empty; the Xbox's holds the 0x40 bytes the texture
// code keeps its shared record in (EAGL_FindSharedData), so one of those is made. The name attachment ('p') and any
// other are kept.

#include "GcConvert.h"
#include "GcBytes.h"
#include "GcUData.h"

#include "../data/UData.h"

#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace {

// The GameCube's image types (TARPrivate::InitFromShape in Agent Under Fire's GameCube driving.elf): 0x10 + the GX
// texture format, the palettised ones 0x18-0x1a, anything else CMPR
enum GcShapeType : uint32_t {
    kGcI4 = 0x10, kGcI8 = 0x11, kGcIA4 = 0x12, kGcIA8 = 0x13, kGcRGB565 = 0x14, kGcRGB5A3 = 0x15, kGcRGBA8 = 0x16,
    kGcC4 = 0x18, kGcC8 = 0x19, kGcC14X2 = 0x1a,
    kGcPaletteIA8 = 0x30, kGcPaletteRGB565 = 0x31, kGcPaletteRGB5A3 = 0x32,
    kGcInfo = 0x69,
};

constexpr uint32_t kXboxDxt1 = 0x60;
constexpr uint32_t kXboxIndexed = 0x7b;
constexpr uint32_t kXboxArgb = 0x7d;
constexpr uint32_t kXboxPalette = 0x2a;
constexpr uint32_t kXboxInfo = 0x69;
constexpr uint32_t kXboxSwizzled = 0x2000;       // EAGL::kShapeSwizzled
constexpr uint32_t kMipLevelShift = 28;          // an Xbox image's flags: mip levels - 1 in bits 28..31
constexpr uint32_t kPaletteColours = 256;
constexpr uint32_t kInfoBytes = 0x40;            // the Xbox's info attachments' data
constexpr uint32_t kGcHeaderBytes = 0x10;
constexpr uint32_t kFontShapeOffset = 0x1c;      // FNTXFont::shapeOffset

struct ShapeFileHeader {             // SHPG / SHPX
    char magic[4];                   // +0x00
    uint32_t size;                   // +0x04 little-endian on both
    uint32_t count;                  // +0x08
    char id[4];                      // +0x0c
};

struct ShapeDirectoryEntry {         // from +0x10
    char name[4];
    uint32_t offset;
};

struct XboxShapeImage {              // 0x14 (EAGL::ShapeImage)
    uint32_t typeAndNext;
    uint16_t width;
    uint16_t height;
    uint32_t unknown08;
    uint32_t flags;
    uint32_t dataOffset;
};

struct XboxAttachment {              // an attachment's header: 0x10
    uint32_t typeAndNext;
    uint16_t width;
    uint16_t height;
    uint32_t unknown08;
    uint32_t unknown0c;
};

// A GameCube image header, read
struct GcImage {
    uint32_t type, next, width, height, levels;
};

GcImage ReadImage(const uint8_t *p) {
    GcImage image;
    image.type = p[0];
    image.next = GcBe32(p) & 0xffffff;
    image.width = GcBe16(p + 4);
    image.height = GcBe16(p + 6);
    image.levels = (p[0x0e] >> 4) + 1;
    return image;
}

uint32_t Argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
    return a << 24 | r << 16 | g << 8 | b;
}

uint8_t Expand(uint32_t value, int bits) {
    switch (bits) {
    case 3: return uint8_t(value * 255 / 7);
    case 4: return uint8_t(value * 17);
    case 5: return uint8_t(value << 3 | value >> 2);
    default: return uint8_t(value << 2 | value >> 4);   // 6
    }
}

uint32_t FromRGB565(uint16_t c) {
    return Argb(0xff, Expand(c >> 11, 5), Expand(c >> 5 & 63, 6), Expand(c & 31, 5));
}

uint32_t FromRGB5A3(uint16_t c) {
    if (c & 0x8000)
        return Argb(0xff, Expand(c >> 10 & 31, 5), Expand(c >> 5 & 31, 5), Expand(c & 31, 5));
    return Argb(Expand(c >> 12 & 7, 3), Expand(c >> 8 & 15, 4), Expand(c >> 4 & 15, 4), Expand(c & 15, 4));
}

uint32_t FromIA8(uint16_t c) {
    uint32_t i = c & 0xff;
    return Argb(c >> 8, i, i, i);
}

// The GX's tiles: width and height of one, and bits a texel
void TileShape(uint32_t type, uint32_t *tileWidth, uint32_t *tileHeight, uint32_t *bits) {
    switch (type) {
    case kGcI4: case kGcC4: *tileWidth = 8; *tileHeight = 8; *bits = 4; break;
    case kGcI8: case kGcIA4: case kGcC8: *tileWidth = 8; *tileHeight = 4; *bits = 8; break;
    case kGcRGBA8: *tileWidth = 4; *tileHeight = 4; *bits = 32; break;
    case kGcIA8: case kGcRGB565: case kGcRGB5A3: case kGcC14X2: *tileWidth = 4; *tileHeight = 4; *bits = 16; break;
    default: *tileWidth = 8; *tileHeight = 8; *bits = 4; break;   // CMPR
    }
}

uint32_t LevelBytes(uint32_t type, uint32_t width, uint32_t height) {
    uint32_t tw, th, bits;
    TileShape(type, &tw, &th, &bits);
    uint32_t w = (width + tw - 1) / tw * tw, h = (height + th - 1) / th * th;
    return w * h * bits / 8;
}

bool Palettised(uint32_t type) {
    return type == kGcC4 || type == kGcC8 || type == kGcC14X2;
}

bool IsCmpr(uint32_t type) {
    return !(type >= kGcI4 && type <= kGcRGBA8) && !Palettised(type);
}

// One texel of a tiled level: an index for the palettised types, else ARGB
uint32_t Texel(const uint8_t *texels, uint32_t type, uint32_t width, uint32_t x, uint32_t y) {
    uint32_t tw, th, bits;
    TileShape(type, &tw, &th, &bits);
    uint32_t tilesX = (width + tw - 1) / tw;
    uint32_t tile = (y / th) * tilesX + x / tw;
    uint32_t at = (y % th) * tw + x % tw;
    const uint8_t *t = texels + tile * (tw * th * bits / 8);
    switch (type) {
    case kGcI4: case kGcC4: {
        uint32_t v = t[at / 2] >> ((at & 1) ? 0 : 4) & 15;
        return type == kGcC4 ? v : Argb(v * 17, v * 17, v * 17, v * 17);
    }
    case kGcI8: return Argb(t[at], t[at], t[at], t[at]);
    case kGcC8: return t[at];
    case kGcIA4: {
        uint32_t i = Expand(t[at] & 15, 4);
        return Argb(Expand(t[at] >> 4, 4), i, i, i);
    }
    case kGcIA8: return FromIA8(GcBe16(t + at * 2));
    case kGcRGB565: return FromRGB565(GcBe16(t + at * 2));
    case kGcRGB5A3: return FromRGB5A3(GcBe16(t + at * 2));
    case kGcC14X2: return GcBe16(t + at * 2) & 0x3fff;
    default:   // RGBA8: AR pairs, then GB pairs
        return Argb(t[at * 2], t[at * 2 + 1], t[32 + at * 2], t[32 + at * 2 + 1]);
    }
}

// A CMPR block (8 bytes) as DXT1's: the colours little-endian, each row's texels' two bits in the other order
void CmprBlockToDxt1(const uint8_t *cmpr, uint8_t *dxt) {
    dxt[0] = cmpr[1];
    dxt[1] = cmpr[0];
    dxt[2] = cmpr[3];
    dxt[3] = cmpr[2];
    for (int row = 0; row < 4; row++) {
        uint8_t b = cmpr[4 + row];
        dxt[4 + row] = uint8_t(b >> 6 | (b >> 2 & 0x0c) | (b << 2 & 0x30) | b << 6);
    }
}

// A CMPR level as DXT1's rows of blocks: tiles of four blocks in rows
void CmprLevelToDxt1(const uint8_t *texels, uint32_t width, uint32_t height, std::vector<uint8_t> *out) {
    uint32_t blocksX = (std::max)((width + 3) / 4, 1u), blocksY = (std::max)((height + 3) / 4, 1u);
    uint32_t tilesX = (width + 7) / 8;
    for (uint32_t by = 0; by < blocksY; by++) {
        for (uint32_t bx = 0; bx < blocksX; bx++) {
            uint32_t tile = (by / 2) * tilesX + bx / 2;
            uint32_t sub = (by & 1) * 2 + (bx & 1);
            uint8_t dxt[8];
            CmprBlockToDxt1(texels + (tile * 4 + sub) * 8, dxt);
            out->insert(out->end(), dxt, dxt + 8);
        }
    }
}

bool PowerOfTwo(uint32_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

// The NV2A's swizzled order: the bits of x and y interleaved, x first, while both have bits left
template <class T> void Swizzle(const T *linear, T *swizzled, uint32_t width, uint32_t height) {
    uint32_t maskX = 0, maskY = 0;
    for (uint32_t bit = 1, size = 1; size < width || size < height; size <<= 1) {
        if (size < width) {
            maskX |= bit;
            bit <<= 1;
        }
        if (size < height) {
            maskY |= bit;
            bit <<= 1;
        }
    }
    uint32_t yOffset = 0;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t xOffset = 0;
        for (uint32_t x = 0; x < width; x++) {
            swizzled[yOffset | xOffset] = linear[y * width + x];
            xOffset = (xOffset - maskX) & maskX;
        }
        yOffset = (yOffset - maskY) & maskY;
    }
}

void Append(std::vector<uint8_t> *out, const void *data, size_t bytes) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    out->insert(out->end(), p, p + bytes);
}

void Align16(std::vector<uint8_t> *out) {
    while (out->size() % 16 != 0)
        out->push_back(0);
}

void PutAttachmentWord(std::vector<uint8_t> *out, size_t at, uint32_t type, uint32_t next) {
    uint32_t word = type | next << 8;
    memcpy(&(*out)[at], &word, 4);
}

// One image and its attachments (from `at` to `end`) appended to `out` as the Xbox's
void ConvertImage(const uint8_t *file, uint32_t at, uint32_t end, std::vector<uint8_t> *out) {
    GcImage image = ReadImage(file + at);
    const uint8_t *pixels = file + at + kGcHeaderBytes;
    uint32_t imageEnd = image.next != 0 ? at + image.next : end;

    // The attachments: the palette is used, the info replaced, the rest kept turned round
    const uint8_t *palette = NULL;
    uint32_t paletteType = 0, colours = 0;
    std::vector<std::pair<uint32_t, uint32_t>> kept;
    for (uint32_t a = imageEnd; a + kGcHeaderBytes <= end;) {
        GcImage attachment = ReadImage(file + a);
        uint32_t next = attachment.next != 0 ? (std::min)(a + attachment.next, end) : end;
        if (attachment.type >= kGcPaletteIA8 && attachment.type <= kGcPaletteRGB5A3) {
            palette = file + a + kGcHeaderBytes;
            paletteType = attachment.type;
            colours = attachment.width;
        } else if (attachment.type != kGcInfo) {
            kept.push_back(std::make_pair(a, next));
        }
        if (attachment.next == 0)
            break;
        a = next;
    }
    auto paletteColour = [&](uint32_t index) -> uint32_t {
        if (palette == NULL || index >= colours)
            return Argb(0xff, 0, 0, 0);
        uint16_t c = GcBe16(palette + index * 2);
        return paletteType == kGcPaletteIA8 ? FromIA8(c) : paletteType == kGcPaletteRGB565 ? FromRGB565(c)
                                                                                        : FromRGB5A3(c);
    };

    uint32_t width = image.width, height = image.height;
    bool square = PowerOfTwo(width) && PowerOfTwo(height);
    bool cmpr = IsCmpr(image.type);
    bool indexed = (image.type == kGcC4 || image.type == kGcC8) && palette != NULL && square;
    // The mip levels: as many as the header says that lie inside the image's block and are a texel or more a side
    uint32_t levels = 1;
    for (uint32_t bytes = LevelBytes(image.type, width, height); levels < image.levels; levels++) {
        uint32_t w = width >> levels, h = height >> levels;
        if (w == 0 || h == 0 || !square || at + kGcHeaderBytes + bytes + LevelBytes(image.type, w, h) > imageEnd)
            break;
        bytes += LevelBytes(image.type, w, h);
    }

    size_t start = out->size();
    XboxShapeImage header = {};
    header.width = uint16_t(width);
    header.height = uint16_t(height);
    header.unknown08 = cmpr ? 1 : 0;
    header.flags = (levels - 1) << kMipLevelShift | (!cmpr && square ? kXboxSwizzled : 0);
    Append(out, &header, sizeof(header));
    for (uint32_t level = 0; level < levels; level++) {
        uint32_t w = (std::max)(width >> level, 1u), h = (std::max)(height >> level, 1u), texels = w * h;
        if (cmpr) {
            CmprLevelToDxt1(pixels, w, h, out);
        } else if (indexed) {
            std::vector<uint8_t> indices(texels), swizzledIndices(texels);
            for (uint32_t y = 0; y < h; y++)
                for (uint32_t x = 0; x < w; x++)
                    indices[y * w + x] = uint8_t(Texel(pixels, image.type, w, x, y));
            Swizzle(indices.data(), swizzledIndices.data(), w, h);
            Append(out, swizzledIndices.data(), texels);
        } else {
            std::vector<uint32_t> argb(texels), swizzledArgb(texels);
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    uint32_t t = Texel(pixels, image.type, w, x, y);
                    argb[y * w + x] = Palettised(image.type) ? paletteColour(t) : t;
                }
            }
            if (square)
                Swizzle(argb.data(), swizzledArgb.data(), w, h);
            Append(out, square ? swizzledArgb.data() : argb.data(), texels * 4);
        }
        pixels += LevelBytes(image.type, w, h);
    }
    Align16(out);

    std::vector<size_t> starts(1, start);
    std::vector<uint32_t> types(1, cmpr ? kXboxDxt1 : indexed ? kXboxIndexed : kXboxArgb);
    if (indexed) {
        starts.push_back(out->size());
        types.push_back(kXboxPalette);
        XboxAttachment clut = {};
        clut.width = kPaletteColours;
        clut.height = 1;
        clut.unknown08 = kPaletteColours;
        Append(out, &clut, sizeof(clut));
        for (uint32_t i = 0; i < kPaletteColours; i++) {
            uint32_t colour = paletteColour(i);
            Append(out, &colour, 4);
        }
    }
    starts.push_back(out->size());
    types.push_back(kXboxInfo);
    XboxAttachment info = {};
    info.width = kInfoBytes;
    info.height = 0x10;
    Append(out, &info, sizeof(info));
    out->insert(out->end(), kInfoBytes, 0);
    for (const auto &k : kept) {
        starts.push_back(out->size());
        types.push_back(file[k.first]);
        out->insert(out->end(), file + k.first, file + k.second);   // a name: text after the first word
        Align16(out);
    }
    for (size_t k = 0; k < starts.size(); k++)
        PutAttachmentWord(out, starts[k], types[k], k + 1 < starts.size() ? uint32_t(starts[k + 1] - starts[k]) : 0);
    Align16(out);
}

// Every image container among a group's records (and its subgroups'), converted and appended to `out`, the record
// pointed at the copy
void ConvertGroupShapes(std::vector<uint8_t> *out, uint32_t group, int depth) {
    UData record;
    memcpy(&record, out->data() + group, sizeof(record));
    if (!(record.flags & kDataRelative))
        return;
    uint32_t items = group + record.data * 16;
    uint32_t groups = record.flags >> 5;
    for (uint32_t i = 0; i < groups + record.count && depth < 16; i++) {
        uint32_t at = items + i * sizeof(UData);
        if (at + sizeof(UData) > out->size())
            return;
        if (i < groups) {
            ConvertGroupShapes(out, at, depth + 1);
            continue;
        }
        UData item;
        memcpy(&item, out->data() + at, sizeof(item));
        uint32_t payload = at + item.data;
        if (!(item.flags & kDataRelative) || payload + 4 > out->size() || memcmp(out->data() + payload, "SHPG", 4))
            continue;
        std::vector<uint8_t> shapes;
        std::vector<uint8_t> source(out->begin() + payload, out->begin() + payload + item.Size());
        if (!Gc_ConvertShapes(source.data(), uint32_t(source.size()), &shapes))
            continue;
        Align16(out);
        item.data = uint32_t(out->size()) - at;
        item.flags = (item.flags & 0xff) | uint32_t(shapes.size()) << 8;
        out->insert(out->end(), shapes.begin(), shapes.end());
        memcpy(out->data() + at, &item, sizeof(item));
    }
}

bool HasExtension(const char *path, const char *extension) {
    size_t length = strlen(path), n = strlen(extension);
    if (length < n)
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = path[length - n + i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != extension[i])
            return false;
    }
    return true;
}

}  // namespace

bool Gc_ConvertShapes(const uint8_t *file, uint32_t size, std::vector<uint8_t> *converted) {
    if (size < sizeof(ShapeFileHeader) || memcmp(file, "SHPG", 4) != 0)
        return false;
    uint32_t count = GcBe32(file + 8);
    if (sizeof(ShapeFileHeader) + count * sizeof(ShapeDirectoryEntry) > size)
        return false;
    std::vector<uint32_t> starts;
    for (uint32_t i = 0; i < count; i++)
        starts.push_back(GcBe32(file + 0x14 + i * 8));
    std::sort(starts.begin(), starts.end());
    uint32_t first = starts.empty() ? size : (std::min)(starts[0], size);

    std::vector<uint8_t> &out = *converted;
    out.assign(file, file + first);
    std::vector<uint32_t> offsets(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t at = GcBe32(file + 0x14 + i * 8);
        auto later = std::upper_bound(starts.begin(), starts.end(), at);
        uint32_t end = later != starts.end() ? (std::min)(*later, size) : size;
        Align16(&out);
        offsets[i] = uint32_t(out.size());
        if (at + kGcHeaderBytes <= end)
            ConvertImage(file, at, end, &out);
    }
    ShapeFileHeader *header = reinterpret_cast<ShapeFileHeader *>(out.data());
    memcpy(header->magic, "SHPX", 4);
    header->size = uint32_t(out.size());
    header->count = count;
    ShapeDirectoryEntry *directory = reinterpret_cast<ShapeDirectoryEntry *>(out.data() + sizeof(*header));
    for (uint32_t i = 0; i < count; i++)
        directory[i].offset = offsets[i];
    return true;
}

bool Gc_ConvertFont(const uint8_t *file, uint32_t size, std::vector<uint8_t> *converted) {
    constexpr uint32_t kWideGlyphs = 0x40000;
    if (size < 0x80 || memcmp(file, "FNTG", 4) != 0)
        return false;
    uint32_t image = GcBe32(file + kFontShapeOffset);
    if (image >= size)
        return false;
    converted->assign(file, file + image);
    uint8_t *p = converted->data();
    uint32_t glyphs = GcBe16(file + 0x0a), flags = GcBe32(file + 0x0c);
    uint32_t glyphOffset = GcBe32(file + 0x14), kernOffset = GcBe32(file + 0x18);
    // The header (FontHeader, eagl/Realgraph.cpp): the size word is little-endian already
    GcSwap16(p + 0x08);
    GcSwap16(p + 0x0a);
    GcSwap32(p + 0x0c);
    for (uint32_t at = 0x14; at < 0x80; at += 4)
        GcSwap32(p + at);
    // The glyphs (FontGlyph): code, two sizes, two halfwords, four bytes; the wide form two halfwords more
    uint32_t stride = flags & kWideGlyphs ? 0x10 : 0x0c;
    for (uint32_t g = 0; g < glyphs && glyphOffset + (g + 1) * stride <= image; g++) {
        uint8_t *glyph = p + glyphOffset + g * stride;
        GcSwap16(glyph);
        GcSwap16(glyph + 4);
        GcSwap16(glyph + 6);
        if (stride == 0x10) {
            GcSwap16(glyph + 0x0c);
            GcSwap16(glyph + 0x0e);
        }
    }
    // The kern table: a count, then pairs of a halfword and two bytes
    if (kernOffset != 0 && kernOffset + 4 <= image) {
        uint32_t kerns = GcBe32(file + kernOffset);
        GcSwap32(p + kernOffset);
        for (uint32_t k = 0; k < kerns && kernOffset + 4 + (k + 1) * 4 <= image; k++)
            GcSwap16(p + kernOffset + 4 + k * 4);
    }
    ConvertImage(file, image, size, converted);
    memcpy(converted->data(), "FNTX", 4);
    uint32_t total = uint32_t(converted->size());
    memcpy(converted->data() + 4, &total, 4);
    return true;
}

bool Gc_ConvertDataGroup(const char *path, const uint8_t *file, uint32_t size, std::vector<uint8_t> *converted) {
    if (size < sizeof(UData) || !(HasExtension(path, ".crp") || HasExtension(path, ".gal")))
        return false;
    converted->assign(file, file + size);
    if (!GcUData_ConvertHeaders(converted->data(), size) || !GcUData_ConvertPayloads(converted->data(), size))
        return false;
    if (HasExtension(path, ".gal"))
        ConvertGroupShapes(converted, 0, 0);
    return true;
}
