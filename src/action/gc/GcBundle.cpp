#include "GcBundle.h"
#include "GcBytes.h"
#include "GcMap.h"
#include "../engine/EDL.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <set>

#define BUNDLE_HEADER_BYTES 0x20
#define BUNDLE_ALIGN 32

// A level's bundle is 0x0700xxxx; bits 20-23 pick a variant, 0x07d0xxxx holding the level's own sequences
#define LEVEL_BUNDLE 0x07000000
#define VARIANT_MASK 0x00f00000
#define LEVEL_SEQUENCES_VARIANT 0x00d00000

// A directory entry's type byte (Loader.cpp's DirFileType_tag), and the GameCube's own
enum {
    ENTRY_MAP = 0,
    ENTRY_MAP_SECOND_PASS = 1,
    ENTRY_MAP_2 = 2,
    ENTRY_SEQUENCE = 4,
    ENTRY_MAP_B = 0xb,
    ENTRY_LOADABLE = 0xc,           // its name is a 4-byte hashcode
    ENTRY_GC_EXTRA_STATIC = 0xd,
    ENTRY_GC_EXTRA_DISCARD = 0xe,
};

bool GcBundle_Directory(const uint8_t *bundle, uint32_t bytes, std::vector<GcBundleEntry> &entries) {
    entries.clear();
    if (bytes < BUNDLE_HEADER_BYTES)
        return false;
    // A little-endian count from a big-endian header is in the millions
    bool bigEndian = GcLe32(bundle + 4) > 0x100000;
    uint32_t dirSize = bigEndian ? GcBe32(bundle) : GcLe32(bundle);
    uint32_t count = bigEndian ? GcBe32(bundle + 4) : GcLe32(bundle + 4);
    if (BUNDLE_HEADER_BYTES + (uint64_t)dirSize > bytes)
        return false;
    const uint8_t *p = bundle + BUNDLE_HEADER_BYTES, *end = p + dirSize;
    uint32_t offset = BUNDLE_HEADER_BYTES + dirSize;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 5 > end)
            return false;
        GcBundleEntry e;
        e.length = GcLe32(p);
        e.type = p[4];
        p += 5;
        if (e.type == ENTRY_LOADABLE) {
            if (p + 4 > end)
                return false;
            char hex[9];
            snprintf(hex, sizeof(hex), "%08x", GcLe32(p));
            e.name = hex;
            p += 4;
        } else {
            const uint8_t *nul = (const uint8_t *)memchr(p, 0, (size_t)(end - p));
            if (nul == NULL)
                return false;
            e.name.assign((const char *)p, (size_t)(nul - p));
            p = nul + 1;
        }
        e.offset = offset;
        if ((uint64_t)offset + e.length > bytes)
            return false;
        offset += e.length;
        entries.push_back(e);
    }
    return true;
}

bool GcBundle_Decompress(const GcBytesVec &file, GcBytesVec &out) {
    if (file.size() < sizeof(EDLHeader) || memcmp(file.data(), "EDL", 3) != 0) {
        out = file;
        return true;
    }
    // A big-endian EDL (the header's top bit) is the same bit stream read from words stored the other way round. The
    // Xbox's Inflate reads its words as they lie, so the words, and the header's sizes, are turned round first.
    GcBytesVec packed(file);
    packed.resize((packed.size() + 3) / 4 * 4 + 4, 0);
    EDLHeader *header = (EDLHeader *)packed.data();
    if (header->algorithmAndEndianness & 0x80) {
        header->algorithmAndEndianness &= 0x7f;
        GcSwap32((uint8_t *)&header->decompressedSize);
        GcSwap32((uint8_t *)&header->compressedSize);
        for (size_t o = sizeof(EDLHeader); o + 4 <= packed.size(); o += 4)
            GcSwap32(&packed[o]);
    }
    // EDL.h's names for the header's sizes are each other's: its "compressed" size is what the block decompresses to
    // (EDL_DecompressSection steps through the blocks by its "decompressed" size)
    uint32_t size = EDL_GetCompressedSize((char *)packed.data());
    if (size == 0 || EDL_GetDecompressedSize((char *)packed.data()) > file.size())
        return false;
    out.assign(size, 0);
    return EDL_DecompressBlock((char *)out.data(), (char *)packed.data());
}

uint32_t GcBundle_SequenceGroup(const uint8_t *commonBin, uint32_t bytes, uint32_t level) {
    for (uint32_t o = 0; o + 8 <= bytes; o += 8) {
        uint32_t key = GcBe32(commonBin + o);
        if (key == 0)
            break;
        if (key == level)
            return GcBe32(commonBin + o + 4);
    }
    return 0;
}

static std::string Upper(const std::string &s) {
    std::string u = s;
    for (char &c : u)
        c = (char)toupper((unsigned char)c);
    return u;
}

// An entry of the Xbox's bundle: its directory record and its data
struct XboxEntry {
    std::string name;
    int type;
    GcBytesVec data;
};

static void Pad(GcBytesVec &v) {
    while (v.size() % BUNDLE_ALIGN != 0)
        v.push_back(0);
}

void GcBundle_Convert(const uint8_t *bundle, uint32_t bytes, const std::vector<GcBytesVec> &sequenceBundles,
                      GcBytesVec &out, GcBundleStats *stats) {
    memset(stats, 0, sizeof(*stats));
    out.clear();
    std::vector<GcBundleEntry> entries;
    if (!GcBundle_Directory(bundle, bytes, entries))
        return;

    std::vector<XboxEntry> xbox;
    std::set<std::string> names;
    for (const GcBundleEntry &e : entries) {
        if (e.type == ENTRY_LOADABLE || e.type == ENTRY_GC_EXTRA_STATIC || e.type == ENTRY_GC_EXTRA_DISCARD) {
            stats->dropped++;
            continue;
        }
        XboxEntry x;
        x.name = e.name;
        x.type = e.type;
        const uint8_t *data = bundle + e.offset;
        if (e.type == ENTRY_MAP || e.type == ENTRY_MAP_SECOND_PASS || e.type == ENTRY_MAP_2 || e.type == ENTRY_MAP_B) {
            GcMapStats map;
            GcMap_Convert(data, e.length, x.data, &map);
            stats->packs++;
            stats->texturesConverted += map.texturesConverted;
            stats->texturesFailed += map.texturesFailed;
            stats->duplicates += map.duplicates;
            stats->meshesSkipped += map.meshesSkipped;
            stats->unknownBlocks += map.unknownBlocks;
        } else {
            x.data.assign(data, data + e.length);
        }
        names.insert(Upper(x.name));
        xbox.push_back(std::move(x));
    }

    // The sequences go first, as in the Xbox's bundles: the anim scripts after them name them
    std::vector<XboxEntry> sequences;
    for (const GcBytesVec &seq : sequenceBundles) {
        std::vector<GcBundleEntry> seqEntries;
        if (!GcBundle_Directory(seq.data(), (uint32_t)seq.size(), seqEntries))
            continue;
        for (const GcBundleEntry &e : seqEntries) {
            if (e.type != ENTRY_SEQUENCE || !names.insert(Upper(e.name)).second)
                continue;
            XboxEntry x;
            x.name = e.name;
            x.type = e.type;
            x.data.assign(seq.data() + e.offset, seq.data() + e.offset + e.length);
            sequences.push_back(std::move(x));
            stats->sequencesAdded++;
        }
    }
    xbox.insert(xbox.begin(), std::make_move_iterator(sequences.begin()), std::make_move_iterator(sequences.end()));

    GcBytesVec directory;
    for (XboxEntry &x : xbox) {
        Pad(x.data);
        uint8_t record[5];
        GcPutLe32(record, (uint32_t)x.data.size());
        record[4] = (uint8_t)x.type;
        directory.insert(directory.end(), record, record + 5);
        directory.insert(directory.end(), x.name.begin(), x.name.end());
        directory.push_back(0);
    }
    Pad(directory);

    out.assign(BUNDLE_HEADER_BYTES, 0);
    GcPutLe32(&out[0], (uint32_t)directory.size());
    GcPutLe32(&out[4], (uint32_t)xbox.size());
    memcpy(&out[8], bundle + 8, BUNDLE_HEADER_BYTES - 8);   // not read by the loader
    out.insert(out.end(), directory.begin(), directory.end());
    for (const XboxEntry &x : xbox)
        out.insert(out.end(), x.data.begin(), x.data.end());
    stats->entries = (int)xbox.size();
}

// A bundle file read and decompressed: false when it is missing or empty
static bool ReadBundle(uint32_t hash, GcReadFile read, void *context, GcBytesVec &out) {
    char path[32];
    snprintf(path, sizeof(path), "gc\\%08x.bin", hash);
    GcBytesVec file;
    return read(context, path, file) && !file.empty() && GcBundle_Decompress(file, out);
}

bool GcBundle_Load(uint32_t hash, GcReadFile read, void *context, GcBytesVec &out, GcBundleStats *stats) {
    out.clear();
    GcBytesVec bundle;
    if (!ReadBundle(hash, read, context, bundle))
        return false;
    std::vector<GcBytesVec> sequences;
    GcBytesVec common, seq;
    if ((hash & 0xfff00000) == LEVEL_BUNDLE && read(context, "gc\\common.bin", common)) {
        uint32_t group = GcBundle_SequenceGroup(common.data(), (uint32_t)common.size(), hash);
        if (group != 0 && ReadBundle(group, read, context, seq))
            sequences.push_back(seq);
        if (ReadBundle((hash & ~VARIANT_MASK) | LEVEL_SEQUENCES_VARIANT, read, context, seq))
            sequences.push_back(seq);
    }
    GcBundle_Convert(bundle.data(), (uint32_t)bundle.size(), sequences, out, stats);
    return !out.empty();
}
