#include "GcUData.h"
#include "GcBytes.h"

#include <string.h>

namespace {

// ---- headers

// The top `bits` bits of the GameCube's word as the Xbox's low bits: its bit 31 is the Xbox's bit 0
uint32_t FlagBits(uint32_t word, int bits) {
    uint32_t flags = 0;
    for (int i = 0; i < bits; i++)
        if (word & (0x80000000u >> i))
            flags |= 1u << i;
    return flags;
}

void ConvertRecord(uint8_t *record, uint32_t flags) {
    uint32_t tag = GcBe32(record);
    uint32_t count = GcBe32(record + 8);
    uint32_t data = GcBe32(record + 12);
    GcPutLe32(record, tag);
    GcPutLe32(record + 4, flags);
    GcPutLe32(record + 8, count);
    GcPutLe32(record + 12, data);
}

// A group's record at offset, its items after it; depth stops a tree that loops
bool ConvertGroup(uint8_t *data, size_t size, size_t offset, int depth) {
    if (offset + 16 > size || depth > 64)
        return false;
    uint8_t *record = data + offset;
    uint32_t word = GcBe32(record + 4);
    uint32_t groups = word & 0x07ffffff;
    uint32_t items = GcBe32(record + 8);
    uint32_t first = GcBe32(record + 12);
    bool relative = (word & 0x40000000) != 0;
    ConvertRecord(record, FlagBits(word, 5) | groups << 5);
    if (!relative)
        return true;
    size_t base = offset + size_t(first) * 16;
    if (base + (size_t(groups) + items) * 16 > size)
        return false;
    for (uint32_t i = 0; i < groups; i++)
        if (!ConvertGroup(data, size, base + size_t(i) * 16, depth + 1))
            return false;
    for (uint32_t i = 0; i < items; i++) {
        uint8_t *item = data + base + (size_t(groups) + i) * 16;
        uint32_t itemWord = GcBe32(item + 4);
        uint32_t field = itemWord >> 24 & 0x1f;
        ConvertRecord(item, FlagBits(itemWord, 3) | field << 3 | (itemWord & 0x00ffffff) << 8);
    }
    return true;
}

// ---- payloads

// A tag as the Xbox's header has it; an indexed one's number replaced by two spaces (UData::MatchTag)
constexpr uint32_t Tag(char a, char b, char c = ' ', char d = ' ') {
    return uint32_t(uint8_t(a)) << 24 | uint32_t(uint8_t(b)) << 16 | uint32_t(uint8_t(c)) << 8 | uint8_t(d);
}

// A float a game could hold: zero, or neither tiny nor huge
bool Plausible(uint32_t word) {
    uint32_t exponent = word >> 23 & 0xff;
    return (word & 0x7fffffff) == 0 || (exponent > 127 - 20 && exponent < 127 + 24);
}

// A float the GameCube's file has in either order (some gallery keys' are the Xbox's already): turned round unless
// only the order it is in makes a plausible value
void SwapEither(uint8_t *p) {
    if (Plausible(GcBe32(p)) || !Plausible(GcLe32(p)))
        GcSwap32(p);
}

// Turns `bytes` bytes at p round by a layout: fields in order, each a count (1 if none) and a width - 'w' a word, 'f'
// a float in either order (SwapEither), 'h' a halfword, 'b' a byte left as it is - and '*' after the last to repeat
// it to the end. The layout is applied again and again until the bytes run out; a field that would cross the end is
// left. Answers the bytes it covered.
uint32_t Swap(uint8_t *p, uint32_t bytes, const char *layout) {
    uint32_t at = 0;
    while (at < bytes) {
        uint32_t start = at;
        for (const char *l = layout; *l != 0 && at < bytes;) {
            if (*l == ' ') {
                l++;
                continue;
            }
            uint32_t count = 0;
            while (*l >= '0' && *l <= '9')
                count = count * 10 + uint32_t(*l++ - '0');
            if (count == 0)
                count = 1;
            char width = *l++;
            if (*l == '*') {
                l++;
                count = 0xffffffff;
            }
            uint32_t step = width == 'w' || width == 'f' ? 4 : width == 'h' ? 2 : 1;
            for (uint32_t i = 0; i < count && at + step <= bytes; i++, at += step) {
                if (width == 'w')
                    GcSwap32(p + at);
                else if (width == 'f')
                    SwapEither(p + at);
                else if (width == 'h')
                    GcSwap16(p + at);
            }
            if (count == 0xffffffff || at + step > bytes)
                return at;
        }
        if (at == start)
            break;
    }
    return at;
}

// A word already turned round
uint32_t Word(const uint8_t *p) {
    return GcLe32(p);
}

uint16_t Half(const uint8_t *p) {
    return GcLe16(p);
}

// CARP::AIElement (data/Carp.h): a character's or a vehicle's part at +0x54
void SwapAIElements(uint8_t *p, uint32_t size) {
    for (uint32_t at = 0; at + 0x100 <= size; at += 0x100) {
        uint8_t *e = p + at;
        Swap(e, 0x54, "16w 2h 4w");
        if (Word(e + 0x44) == 0)
            Swap(e + 0x54, 0x24, "9w");     // its body, AIElementCharacterInfo
        Swap(e + 0x78, 0x88, "4b w 24w 6w w w");
    }
}

// CARP::Instance x count: the packed dimensions a bit-field from the top bit down on the GameCube (x in bits 31-22,
// y 21-12, z 11-2, then the large step and "given separately"), from bit 0 up on the Xbox
void SwapInstances(uint8_t *p, uint32_t size) {
    for (uint32_t at = 0; at + 0x40 <= size; at += 0x40) {
        uint8_t *instance = p + at;
        Swap(instance, 0x40, "3w 2b h 3w w 3w 2h 3w w");
        uint32_t packed = Word(instance + 0x3c);
        uint32_t x = packed >> 22, y = packed >> 12 & 0x3ff, z = packed >> 2 & 0x3ff;
        GcPutLe32(instance + 0x3c, x | y << 10 | z << 20 | (packed & 2) << 29 | (packed & 1) << 31);
    }
}

// MissionRuleRecord (game/MissionRules.h): a property rule's last eight bytes are a name
void SwapRules(uint8_t *p, uint32_t size) {
    constexpr uint32_t kRuleProperty = 10;
    for (uint32_t at = 0; at + 0x20 <= size; at += 0x20) {
        Swap(p + at, 0x10, "4w");
        Swap(p + at + 0x10, 0x10, Word(p + at) == kRuleProperty ? "2w 8b" : "4w");
    }
}

// WMapNode x 4 (world/Tree.h), then the nodes' index lists
void SwapMapNodes(uint8_t *p, uint32_t size) {
    uint32_t nodes = size < 0x40 ? size : 0x40;
    Swap(p, nodes, "2w 2h 4b");
    Swap(p + nodes, size - nodes, "h*");
}

// A collision article (CollisionArticle, world/CollisionTypes.h): its header, strips, the strips' vertices, barriers
// and window groups, each found from the header's offsets
void SwapCollisionArticle(uint8_t *p, uint32_t size) {
    if (size < 0x20)
        return;
    Swap(p, 0x20, "5h 22b");
    uint32_t strips = Half(p), barrierOffset = Half(p + 2), barriers = Half(p + 4);
    uint32_t windowOffset = Half(p + 6), groups = Half(p + 8);
    uint8_t *body = p + 0x20;
    uint32_t bodySize = size - 0x20;
    for (uint32_t s = 0; s < strips && s * 16 + 16 <= bodySize; s++) {
        uint8_t *strip = body + s * 16;
        Swap(strip, 16, "3w 2h");
        uint32_t vertexOffset = Half(strip + 14);
        if (vertexOffset + 32 > bodySize)
            continue;
        // The first vertex's fourth word is the count, the second's flags, the rest's a FaceTag
        uint8_t *v = body + vertexOffset;
        Swap(v, 32, "8w");
        uint32_t count = Word(v + 12);
        for (uint32_t i = 2; i < count && vertexOffset + i * 16 + 16 <= bodySize; i++)
            Swap(v + i * 16, 16, "3w 2b h");
    }
    if (barrierOffset + barriers * 32 <= bodySize)
        Swap(body + barrierOffset, barriers * 32, "3w 2b h 3w w");
    uint32_t group = barrierOffset + windowOffset;
    for (uint32_t g = 0; g < groups && group + 12 <= bodySize; g++) {
        Swap(body + group, 12, "3w");
        uint32_t sets = Word(body + group), groupSize = Word(body + group + 4);
        uint32_t set = group + 12;
        for (uint32_t s = 0; s < sets && set + 8 <= bodySize; s++) {
            Swap(body + set, 8, "2w");
            uint32_t panes = Word(body + set);
            if (set + 8 + panes * 8 > bodySize)
                break;
            Swap(body + set + 8, panes * 8, "4h");
            set += (1 + panes) * 8;
        }
        if (groupSize == 0)
            break;
        group += groupSize;
    }
}

// SkeletonBone x count (render/RSceneObj.cpp), then their names
void SwapSkeleton(uint8_t *p, uint32_t size, uint32_t count) {
    uint32_t bones = count * 0x20 < size ? count * 0x20 : size;
    Swap(p, bones, "7w 2h");
}

// ArticleEffect x count (world/World.h): the part from +0x18 by its type
void SwapEffects(uint8_t *p, uint32_t size) {
    for (uint32_t at = 0; at < size; at += 0x40) {
        uint8_t *e = p + at;
        uint32_t left = size - at;
        Swap(e, left < 0x18 ? left : 0x18, "5w 4b");
        if (left <= 0x18)
            break;
        const char *layout;
        switch (e[0x14]) {
        case 5: layout = "w 4b w 4w 3w"; break;    // a light: its colour in bytes
        case 7: layout = "20b w 4w"; break;        // a locator: a name
        default: layout = "10w"; break;            // glare, gfx, spring: words
        }
        Swap(e + 0x18, left - 0x18 < 0x28 ? left - 0x18 : 0x28, layout);
    }
}

// The gallery's keys (GalleryKey, game/Gallery.h): a header, then the data of the kind the key's group holds, then
// words to the next key. The header's orientation and the two words after its bytes are in either order on the
// GameCube's disc (the first key's big-endian, later keys' often the Xbox's already).
void SwapGalleryKeys(uint8_t *p, uint32_t size, uint32_t count, uint32_t parent) {
    const char *data;
    switch (parent & 0xffff0000) {
    case Tag('g', 'b', 0, 0): data = "12w 27b 4b b w*"; break;   // GallerySprite
    case Tag('g', 't', 0, 0): data = "4w 16b w*"; break;         // GalleryText
    case Tag('g', 's', 0, 0): data = "3w 32b w*"; break;         // GallerySound: the patch's name
    case Tag('g', 'd', 0, 0): data = "4w 4b w*"; break;          // GalleryDebris: its colour in bytes
    default: data = "w*"; break;                                 // quads, glares, particles: words
    }
    uint32_t at = 0;
    for (uint32_t k = 0; k < count && at + 0x30 <= size; k++) {
        uint8_t *key = p + at;
        Swap(key, 0x30, "4w 4f 4b 2f w");
        uint32_t extra = key[0x21];
        uint32_t stride = extra + 0x30 > 0x40 ? extra + 0x30 : 0x40;
        Swap(key + 0x30, (at + stride <= size ? stride : size - at) - 0x30, data);
        at += stride;
    }
}

// The record types with a fixed layout, each the structure the game reads it as
struct Layout {
    uint32_t tag;
    const char *layout;
};
const Layout kLayouts[] = {
    { Tag('A', 'I', 'C', 'o'), "16w 3w 3w 8w 8b w 4b 2w 16b" },   // AICommand (game/AIVehicle.h)
    { Tag('A', 'I', 'S', 'p'), "16w w 40b 4b" },                  // CARP::AISpline: a name at +0x44
    { Tag('A', 'u', 'd', 'i'), "3w w 48b 8w" },                   // WorldSoundRecord: "bank: sound"
    { Tag('B', 'a', 's', 'e'), "w*" },                            // CARP::BaseDesc and its levers
    { Tag('p', 'D'), "w*" },
    { Tag('C', 'G', 'r', 'd'), "w*" },                            // WGridRecord
    { Tag('c', 'i'), "3w 3w 2b h w 3w w 3w w" },                  // WCollisionInstance
    { Tag('c', 'o'), "8w 4b h 2b 8b" },                           // WCollisionObject
    { Tag('c', 'n'), "w 4b 4h h*" },                              // WGridNode, then its index lists
    { Tag('d', 'e'), "2h w" },                                    // WGridElementRecord
    { Tag('E', 'n', 'v', 'i'), "2w 12b 2w" },                     // EnviroDesc: the ambience's name
    { Tag('M', 'S', 'e', 't'), "36b 2w 32b w*" },                 // MissionSet: a name, the weapons' flags
    { Tag('T', 'r', 'g', 'r'), "3w w 2b h 3w 3w w 3w w" },        // WTrigger
    { Tag('V', 'i', 's', 'C'), "w*" },                            // WVisCurtain
    { Tag('S', 'h', 'a', 'd'), "w*" },                            // ShadowMapInfo
    { Tag('W', 'm', 'a', 'p'), "w*" },                            // WMapHeader
    { Tag('S', 'e', 'c', 't'), "w*" },                            // CarpSections
    { Tag('p', 's'), "2b h b h b 5w 4b" },                        // ProcAnimState: +0x05 a halfword on the GameCube
    { Tag('a', 'n'), "w 3h 6b" },                                 // CARP::AnimInfo, the list's end included
    { Tag('a', 'q'), "w*" },                                      // CARP::AnimKey
    { Tag('a', 's'), "6w h 6b w*" },                              // WorldArticle, then its levels of detail
    { Tag('e', 'l'), "w*" },                                      // TriggerEvents: every event's record is words
    { Tag('p', 't'), "w*" },                                      // CARP::PathInfo: channels, times and keys
    { Tag('d', 'i'), "w*" },                                      // DissolveInfo
    { Tag('v', 'd'), "h*" },                                      // the view's draw list
    { Tag('R', 'N', 'h', 'd'), "h*" },                            // WRoadNetworkHeader
    { Tag('r', 'n'), "3w h 2b 8h" },                              // WRoadNode
    { Tag('r', 's'), "3w 2h 3w 2h 3w w 3w 2h 3w w h 2b 2h 4b 2w" }, // WRoadSegment
    { Tag('r', 'i'), "3w h 2b 4h 2b 11h" },                       // WRoadIntersection
    { Tag('r', 'j'), "h*" },                                      // WRoadJunction
    { Tag('r', 'r'), "4h w 32b 4b" },                             // WRoad: a name at +0x0c
    { Tag('c', 'd'), "8b 2w" },
    { Tag('c', 's'), "w*" },
    { Tag('m', 'p'), "h*" },
    { Tag('u', '1'), "w*" },
    { Tag('F', 'l', 'g', 's'), "w" },                             // the effects gallery's words
    { Tag('N', 'U', 'M', 'P'), "w" },
    { Tag('L', 'I', 'F', 'E'), "w" },
};

// Text, names and byte streams: nothing to turn round
const uint32_t kBytes[] = {
    Tag('N', 'a', 'm', 'e'), Tag('D', 'B', 'N'), Tag('M', 'a', 'p', 'N'), Tag('s', 'n'), Tag('s', 'r'),
    Tag('i', 'N'), Tag('f', 'i'), Tag('M', 'i', 'E', 'd'), Tag('t', 'x'), Tag('E', 'L', 'F', 'd'),
    Tag('E', 'L', 'F', 'r'),
};

bool ConvertPayloads(uint8_t *data, size_t size, size_t offset, uint32_t parent, int depth) {
    if (offset + 16 > size || depth > 64)
        return false;
    const uint8_t *record = data + offset;
    uint32_t tag = Word(record), flags = Word(record + 4), items = Word(record + 8), first = Word(record + 12);
    if (!(flags & 2))
        return true;
    uint32_t groups = flags >> 5;
    size_t base = offset + size_t(first) * 16;
    if (base + (size_t(groups) + items) * 16 > size)
        return false;
    for (uint32_t i = 0; i < groups; i++)
        if (!ConvertPayloads(data, size, base + size_t(i) * 16, tag, depth + 1))
            return false;
    for (uint32_t i = 0; i < items; i++) {
        size_t at = base + (size_t(groups) + i) * 16;
        const uint8_t *item = data + at;
        uint32_t itemFlags = Word(item + 4);
        if (!(itemFlags & 2))
            continue;
        uint32_t itemTag = Word(item);
        if (itemFlags & 1)
            itemTag = (itemTag & 0xffff0000) | 0x2020;
        size_t payload = at + Word(item + 12);
        uint32_t bytes = itemFlags >> 8;
        if (payload + bytes > size)
            return false;
        GcUData_ConvertPayload(itemTag, tag, data + payload, bytes, Word(item + 8));
    }
    return true;
}

}  // namespace

bool GcUData_ConvertHeaders(uint8_t *data, size_t size) {
    return ConvertGroup(data, size, 0, 0);
}

bool GcUData_ConvertPayloads(uint8_t *data, size_t size) {
    return ConvertPayloads(data, size, 0, 0, 0);
}

bool GcUData_ConvertPayload(uint32_t tag, uint32_t parent, uint8_t *payload, uint32_t size, uint32_t count) {
    for (uint32_t t : kBytes)
        if (t == tag)
            return true;
    for (const Layout &l : kLayouts) {
        if (l.tag == tag) {
            Swap(payload, size, l.layout);
            return true;
        }
    }
    switch (tag) {
    case Tag('A', 'I', 'E', 'l'): SwapAIElements(payload, size); return true;
    case Tag('R', 'u', 'l', 'e'): SwapRules(payload, size); return true;
    case Tag('i', 'n'):
    case Tag('C', 'a', 'm', 's'): SwapInstances(payload, size); return true;
    case Tag('w', 'n'): SwapMapNodes(payload, size); return true;
    case Tag('c', 'a'): SwapCollisionArticle(payload, size); return true;
    case Tag('S', 'k', 'e', 'l'): SwapSkeleton(payload, size, count); return true;
    case Tag('e', 'f'): SwapEffects(payload, size); return true;
    case Tag('g', 'k'): SwapGalleryKeys(payload, size, count, parent); return true;
    default: return false;
    }
}
