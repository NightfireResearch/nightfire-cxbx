// The GameCube data's conversions run offline over the extracted GameCube disc (docs/gc-data.md): no game, no window.
// The Xbox's twins come from an extracted Xbox disc's filesys.dNN archives, read here as FS.cpp reads them.
//
//   gcdata_check <gc disc> files                            every bundle on the disc: compressed or not, its
//                                                           directory's entries by type
//   gcdata_check <gc disc> bundle <hash>                    a bundle's directory and its packs' blocks
//   gcdata_check <gc disc> decompress <hash> <out>          a bundle as the loader gets it from the disc
//   gcdata_check <gc disc> textures <hash> [<out>]          every texture converted to a KXT; TGAs of the GameCube's
//                                                           level 0 and of the KXT's, decoded back, into <out>
//   gcdata_check <gc disc> convert <hash> <xbox disc>       the level made the Xbox's (GcBundle.h) and checked against
//                                                           the Xbox's bundle, entry by entry and block by block
//   gcdata_check <gc disc> all <xbox disc>                  convert, for every level bundle, totals only
//   gcdata_check <gc disc> xbox <xbox disc> <name> <out>    a file out of the Xbox's archives
//   gcdata_check <gc disc> names <xbox disc>                every name the Xbox build asks for, found on both discs
//   gcdata_check <gc disc> dt <hash>                        the "DT" texture headers' indices against their names
//   gcdata_check <gc disc> meshes <xbox disc> [<hash>...]   every rigid mesh to a KXE, against the Xbox's twins
//                                                           (every level when no hash is given)
//   gcdata_check <gc disc> skins <xbox disc> [<hash>...]    the same for the skinned meshes: bones, weights, morphs
//   gcdata_check <gc disc> sound <xbox disc> [<wav dir>]    the banks decoded, the sounds and music against the
//                                                           Xbox's; WAVs of the samples and the first track
//
// <gc disc> is the folder with Nightfire.elf, common\ and gc\. <hash> is a bundle's name without .bin (0700001b).

#include "../gc/GcBundle.h"
#include "../gc/GcBytes.h"
#include "../gc/GcDisc.h"
#include "../gc/GcMap.h"
#include "../gc/GcMesh.h"
#include "../gc/GcSound.h"
#include "../sound/PcmBanks.h"
#include "../gc/GcTexture.h"
#include "../engine/EDL.h"
#include "../engine/psiGraphics.h"
#include "../util/crc.h"
#include "../../common/sound/xadpcm.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

typedef std::vector<uint8_t> Bytes;

bool ReadWhole(const std::string &path, Bytes &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (f == NULL)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)size);
    bool ok = size == 0 || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

bool WriteWhole(const std::string &path, const uint8_t *data, size_t bytes) {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == NULL)
        return false;
    bool ok = fwrite(data, 1, bytes, f) == bytes;
    fclose(f);
    return ok;
}

std::string Upper(std::string s) {
    for (char &c : s)
        c = (char)toupper((unsigned char)c);
    return s;
}

// ---- The Xbox's archives (FS.cpp): a header of 0x20 bytes, then 0x26-byte entries sorted by the CRC-32 of the
// lower-cased, backslashed name; a file is in filesys.d<idx>, EDL sections when flag bit 1 is set

#pragma pack(push, 1)
struct XboxFileEntry {
    uint32_t nameCrc;
    uint32_t dataCrc;
    uint32_t unknown1;
    uint32_t offsetLow;
    uint32_t offsetHigh;
    uint32_t compressedLen;
    uint32_t uncompressedLen;
    uint8_t flags;
    uint8_t unknown2;
    uint32_t filesysIdx;
    uint32_t offsetOfFileWithinArchive;
};
#pragma pack(pop)
static_assert(sizeof(XboxFileEntry) == 0x26, "XboxFileEntry is 0x26 bytes");

struct XboxDisc {
    std::string root;
    std::vector<XboxFileEntry> entries;

    bool Open(const std::string &folder) {
        root = folder + "/eurocom";
        FILE *f = fopen((root + "/filesys.d00").c_str(), "rb");
        if (f == NULL) {
            printf("no %s/filesys.d00\n", root.c_str());
            return false;
        }
        uint8_t header[0x20];
        bool ok = fread(header, 1, sizeof(header), f) == sizeof(header);
        uint32_t count = GcLe32(header + 0x18);
        entries.resize(count);
        ok = ok && fread(entries.data(), sizeof(XboxFileEntry), count, f) == count;
        fclose(f);
        return ok;
    }

    bool Read(const char *name, Bytes &out) {
        std::string n = name;
        for (char &c : n)
            c = c == '/' ? '\\' : (char)tolower((unsigned char)c);
        uint32_t crc = crc32buf((const uint8_t *)n.data(), n.size());
        auto it = std::lower_bound(entries.begin(), entries.end(), crc,
                                   [](const XboxFileEntry &e, uint32_t c) { return e.nameCrc < c; });
        if (it == entries.end() || it->nameCrc != crc)
            return false;
        char path[512];
        snprintf(path, sizeof(path), "%s/filesys.d%02u", root.c_str(), it->filesysIdx);
        FILE *f = fopen(path, "rb");
        if (f == NULL)
            return false;
        Bytes packed(it->compressedLen);
        fseek(f, (long)it->offsetOfFileWithinArchive, SEEK_SET);
        bool ok = fread(packed.data(), 1, packed.size(), f) == packed.size();
        fclose(f);
        if (!ok)
            return false;
        if (!(it->flags & 2)) {
            out = packed;
            return true;
        }
        // EDL sections: an identifier and a block count, then the blocks one after another (EDL.h's size names are
        // each other's, as EDL_DecompressSection steps them)
        out.assign(it->uncompressedLen + 0x800, 0);
        uint32_t blocks = GcLe32(packed.data() + 4);
        char *src = (char *)packed.data() + 8, *dst = (char *)out.data();
        for (uint32_t b = 0; b < blocks; b++) {
            uint32_t in = EDL_GetDecompressedSize(src), made = EDL_GetCompressedSize(src);
            if (!EDL_DecompressBlock(dst, src))
                return false;
            src += in;
            dst += made;
        }
        out.resize(it->uncompressedLen);
        return true;
    }
};

// ---- The GameCube's disc: loose files

struct GcDisc {
    std::string root;

    bool Open(const std::string &folder) {
        root = folder;
        Bytes probe;
        if (!ReadWhole(root + "/gc/common.bin", probe)) {
            printf("no %s/gc/common.bin\n", root.c_str());
            return false;
        }
        return true;
    }

    bool Bundle(const std::string &hash, Bytes &out) {
        Bytes file;
        if (!ReadWhole(root + "/gc/" + hash + ".bin", file) || file.empty())
            return false;
        return GcBundle_Decompress(file, out);
    }

    // A file by its path on the disc (GcDisc_Path's)
    static bool Read(void *context, const char *path, Bytes &out) {
        std::string p = ((GcDisc *)context)->root + "/" + path;
        for (char &c : p)
            if (c == '\\')
                c = '/';
        return ReadWhole(p, out);
    }

    // A bundle as the Xbox's, as GcFs.cpp serves it
    bool Convert(const std::string &hash, Bytes &out, GcBundleStats *stats) {
        return GcBundle_Load((uint32_t)strtoul(hash.c_str(), NULL, 16), Read, this, out, stats);
    }
};

// ---- Blocks of a map pack

struct Block {
    int id;
    const uint8_t *data;    // after the header word
    uint32_t bytes;
};

std::vector<Block> Blocks(const uint8_t *pack, uint32_t bytes) {
    std::vector<Block> out;
    for (uint32_t o = 4; o + 4 <= bytes;) {
        uint32_t h = GcLe32(pack + o), size = h & 0xffffff;
        if (size < 4 || o + size > bytes)
            break;
        out.push_back({ (int)(h >> 24), pack + o + 4, size - 4 });
        if ((h >> 24) == GC_BLOCK_END)
            break;
        o += size;
    }
    return out;
}

// ---- Textures, decoded for the comparisons: the KXT's level 0, frame 0, to linear BGRA

void UnswizzleTo(const uint32_t *swizzled, uint32_t *linear, int width, int height) {
    uint32_t maskX = 0, maskY = 0, bit = 1, size = 1;
    for (;;) {
        bool any = false;
        if (size < (uint32_t)width)  { maskX |= bit; bit <<= 1; any = true; }
        if (size < (uint32_t)height) { maskY |= bit; bit <<= 1; any = true; }
        size <<= 1;
        if (!any)
            break;
    }
    uint32_t yOff = 0;
    for (int y = 0; y < height; y++) {
        uint32_t xOff = 0;
        for (int x = 0; x < width; x++) {
            linear[y * width + x] = swizzled[yOff | xOff];
            xOff = (xOff - maskX) & maskX;
        }
        yOff = (yOff - maskY) & maskY;
    }
}

void DecodeColourBlock(const uint8_t *b, bool threeColour, uint8_t out[16][4]) {
    uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    int col[4][4];
    for (int k = 0; k < 2; k++) {
        uint16_t c = k == 0 ? c0 : c1;
        col[k][2] = ((c >> 11) & 31) * 255 / 31;
        col[k][1] = ((c >> 5) & 63) * 255 / 63;
        col[k][0] = (c & 31) * 255 / 31;
        col[k][3] = 255;
    }
    bool four = !threeColour || c0 > c1;
    for (int i = 0; i < 3; i++) {
        col[2][i] = four ? (2 * col[0][i] + col[1][i]) / 3 : (col[0][i] + col[1][i]) / 2;
        col[3][i] = four ? (col[0][i] + 2 * col[1][i]) / 3 : 0;
    }
    col[2][3] = 255;
    col[3][3] = four ? 255 : 0;
    for (int i = 0; i < 16; i++) {
        int index = (b[4 + i / 4] >> ((i % 4) * 2)) & 3;
        for (int c = 0; c < 4; c++)
            out[i][c] = (uint8_t)col[index][c];
    }
}

// The texture header's frame 0, level 0 as linear BGRA (DXT2 and DXT4 read as DXT3 and DXT5, their colours left
// premultiplied); false for a format this does not decode
bool DecodeKxt(const uint8_t *kxt, uint32_t bytes, std::vector<uint8_t> &bgra, int *w, int *h) {
    if (bytes < sizeof(TextureInfo))
        return false;
    TextureInfo info;
    memcpy(&info, kxt, offsetof(TextureInfo, baseIdx));
    if (info.magic != TEXINFO_MAGIC || info.width == 0 || info.height == 0 || info.width > 4096 || info.height > 4096)
        return false;
    *w = (int)info.width;
    *h = (int)info.height;
    const uint8_t *pixels = kxt + offsetof(TextureInfo, baseIdx) + info.numFrames * 4;
    bgra.assign((size_t)*w * *h * 4, 0);
    uint32_t level0;
    int format = info.formatType;
    if (format == 0 && info.bitsPerPixel == 8)
        level0 = (uint32_t)*w * *h * 4;
    else if (format >= 1 && format <= 5)
        level0 = (uint32_t)std::max(*w, 4) * std::max(*h, 4) / (format == 1 ? 2 : 1);
    else
        return false;
    if (pixels + level0 > kxt + bytes)
        return false;
    if (format == 0) {
        UnswizzleTo((const uint32_t *)pixels, (uint32_t *)bgra.data(), *w, *h);
        return true;
    }
    int bx = std::max(*w, 4) / 4, by = std::max(*h, 4) / 4;
    for (int y = 0; y < by; y++) {
        for (int x = 0; x < bx; x++) {
            const uint8_t *b = pixels + (y * bx + x) * (format == 1 ? 8 : 16);
            uint8_t block[16][4];
            DecodeColourBlock(format == 1 ? b : b + 8, format == 1, block);
            if (format == 4 || format == 5) {
                int a0 = b[0], a1 = b[1], lv[8] = { a0, a1 };
                for (int k = 1; k < 7; k++)
                    lv[k + 1] = a0 > a1 ? ((7 - k) * a0 + k * a1) / 7
                                        : (k < 5 ? ((5 - k) * a0 + k * a1) / 5 : k == 5 ? 0 : 255);
                uint64_t bits = 0;
                for (int k = 0; k < 6; k++)
                    bits |= (uint64_t)b[2 + k] << (8 * k);
                for (int i = 0; i < 16; i++)
                    block[i][3] = (uint8_t)lv[(bits >> (3 * i)) & 7];
            } else if (format == 2 || format == 3) {
                for (int i = 0; i < 16; i++)
                    block[i][3] = (uint8_t)(((b[i / 2] >> ((i & 1) * 4)) & 15) * 17);
            }
            for (int i = 0; i < 16; i++) {
                int px = x * 4 + i % 4, py = y * 4 + i / 4;
                if (px < *w && py < *h)
                    memcpy(&bgra[((size_t)py * *w + px) * 4], block[i], 4);
            }
        }
    }
    return true;
}

// Peak signal to noise over RGB (and alpha) of two images of one size; 99 for the same
double Psnr(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    double sum = 0;
    for (size_t i = 0; i < a.size(); i++) {
        double d = (double)a[i] - b[i];
        sum += d * d;
    }
    if (sum == 0)
        return 99.0;
    return 10.0 * log10(255.0 * 255.0 / (sum / a.size()));
}

// Halved (box filter) until it is w x h, for an Xbox twin at twice the GameCube's size
std::vector<uint8_t> Shrink(const std::vector<uint8_t> &src, int sw, int sh, int dw, int dh) {
    std::vector<uint8_t> out((size_t)dw * dh * 4);
    int fx = sw / dw, fy = sh / dh;
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++)
            for (int c = 0; c < 4; c++) {
                int sum = 0;
                for (int j = 0; j < fy; j++)
                    for (int i = 0; i < fx; i++)
                        sum += src[(((size_t)y * fy + j) * sw + x * fx + i) * 4 + c];
                out[((size_t)y * dw + x) * 4 + c] = (uint8_t)(sum / (fx * fy));
            }
    return out;
}

void WriteTga(const std::string &path, const std::vector<uint8_t> &bgra, int w, int h) {
    uint8_t header[18] = { 0, 0, 2 };
    header[12] = (uint8_t)w; header[13] = (uint8_t)(w >> 8);
    header[14] = (uint8_t)h; header[15] = (uint8_t)(h >> 8);
    header[16] = 32; header[17] = 0x28;     // top-left origin, 8 bits of alpha
    Bytes out(header, header + 18);
    out.insert(out.end(), bgra.begin(), bgra.end());
    WriteWhole(path, out.data(), out.size());
}

// ---- Commands

int Files(GcDisc &disc) {
    static const char *const kBundles[] = { "07000001", "07000002", "07000003", "07000004", "07000005", "07000006",
        "07000007", "07000008", "07000009", "0700000a", "0700000b", "0700000c", "0700000d", "07000011", "07000012",
        "07000013", "07000014", "07000015", "07000016", "0700001b", "07000023", "07000024", "07000025", "07000026",
        "07000027", "07000028", "07000029", "07000048", "0700004a", "0700004b", "07000500", "07000999" };
    for (const char *hash : kBundles) {
        Bytes file, bundle;
        if (!ReadWhole(disc.root + "/gc/" + hash + ".bin", file)) {
            printf("%s missing\n", hash);
            continue;
        }
        if (file.empty()) {
            printf("%s empty on this disc\n", hash);
            continue;
        }
        bool edl = memcmp(file.data(), "EDL", 3) == 0;
        if (!GcBundle_Decompress(file, bundle)) {
            printf("%s: EDL does not decompress\n", hash);
            continue;
        }
        std::vector<GcBundleEntry> entries;
        bool ok = GcBundle_Directory(bundle.data(), (uint32_t)bundle.size(), entries);
        std::map<int, int> types;
        uint32_t end = 0;
        for (const GcBundleEntry &e : entries) {
            types[e.type]++;
            end = std::max(end, e.offset + e.length);
        }
        printf("%s %s %9zu -> %9zu bytes, %s, %zu entries, data to %u:", hash, edl ? "EDL" : "   ", file.size(),
               bundle.size(), ok ? "directory read" : "DIRECTORY BAD", entries.size(), end);
        for (auto &t : types)
            printf(" type%d=%d", t.first, t.second);
        printf("\n");
    }
    return 0;
}

int BundleInfo(GcDisc &disc, const std::string &hash) {
    Bytes bundle;
    if (!disc.Bundle(hash, bundle)) {
        printf("no bundle %s\n", hash.c_str());
        return 1;
    }
    std::vector<GcBundleEntry> entries;
    if (!GcBundle_Directory(bundle.data(), (uint32_t)bundle.size(), entries)) {
        printf("%s: the directory does not read\n", hash.c_str());
        return 1;
    }
    std::map<int, int> blockCounts;
    for (const GcBundleEntry &e : entries) {
        printf("%-20s type %2d %8x bytes at %8x\n", e.name.c_str(), e.type, e.length, e.offset);
        if (e.type == 0 || e.type == 1 || e.type == 2 || e.type == 11)
            for (const Block &b : Blocks(bundle.data() + e.offset, e.length))
                blockCounts[b.id]++;
    }
    printf("blocks over the packs:");
    for (auto &b : blockCounts)
        printf(" %02x=%d", b.first, b.second);
    printf("\n");
    return 0;
}

// ---- Meshes: our KXEs against the Xbox's

#pragma pack(push, 1)
struct KxeVertexData {      // the rigid vertex (a skinned one has a bone word after it)
    float position[3];
    uint32_t normal;
    uint8_t colour[4];      // B, G, R, A
    float uv[2];
};
struct KxePrimitiveData {
    uint16_t tex0, tex1;
    uint32_t count;
    uint16_t states;
    uint8_t changes, palette;
};
#pragma pack(pop)

struct Kxe {
    std::vector<KxeVertexData> vertices;
    std::vector<uint16_t> indices;
    std::vector<KxePrimitiveData> primitives;
    bool skinned = false;
    std::vector<uint32_t> boneWords;                // a skinned mesh's, one a vertex
    std::vector<std::vector<uint8_t>> palettes;     // its bone palettes, skeleton bones
    std::vector<int> vertexPalette;                 // the palette each vertex is drawn with, -1 for none
    int morphTargets = 0, morphVertices = 0;
    std::vector<int16_t> morphDeltas;               // target by target, morphVertices of x, y, z each
};

// The palette slot a skinned vertex's register byte names (the inverse of the Xbox's SkinRegTable)
int SlotOfRegister(int reg) {
    if (reg < 57)
        return reg / 3;
    if (reg < 126)
        return 19 + (reg - 60) / 3;
    return 31 + (reg - 126) / 3;
}

// A skinned vertex's bones (skeleton bones) and bone A's weight, from its word and its palette; false when unknown
bool VertexBones(const Kxe &k, int v, int *boneA, int *boneB, float *weightA) {
    if (!k.skinned || k.vertexPalette[v] < 0)
        return false;
    const std::vector<uint8_t> &palette = k.palettes[k.vertexPalette[v]];
    uint32_t word = k.boneWords[v];
    int a = SlotOfRegister(word & 0xff), b = SlotOfRegister((word >> 8) & 0xff);
    if (a >= (int)palette.size() || b >= (int)palette.size())
        return false;
    *boneA = palette[a];
    *boneB = palette[b];
    *weightA = (word >> 16) / 65535.0f;
    return true;
}

bool ReadKxe(const uint8_t *p, uint32_t bytes, Kxe &k) {
    if (bytes < 0x60 || GcLe32(p) != 0x0645584b)
        return false;
    uint32_t vtxCnt = GcLe32(p + 8), idxCnt = GcLe32(p + 0xc), primCnt = GcLe32(p + 0x10);
    k.skinned = GcLe32(p + 0x1c) != 0;
    uint32_t stride = k.skinned ? 32 : 28;
    uint64_t need = 0x60 + (uint64_t)vtxCnt * stride + 3 + idxCnt * 2 + 3 + primCnt * 12;
    if (need > bytes)
        return false;
    const uint8_t *at = p + 0x60;
    k.vertices.resize(vtxCnt);
    for (uint32_t i = 0; i < vtxCnt; i++)
        memcpy(&k.vertices[i], at + i * stride, sizeof(KxeVertexData));
    at += vtxCnt * stride + 3;
    k.indices.resize(idxCnt);
    memcpy(k.indices.data(), at, idxCnt * 2);
    at += idxCnt * 2 + 3;
    k.primitives.resize(primCnt);
    memcpy(k.primitives.data(), at, primCnt * 12);
    if (k.skinned) {
        k.boneWords.resize(vtxCnt);
        for (uint32_t i = 0; i < vtxCnt; i++)
            k.boneWords[i] = GcLe32(p + 0x60 + i * stride + 28);
        uint32_t groups = GcLe32(p + 0x1c);
        const uint8_t *pal = at + primCnt * 12;
        if (pal + groups * 0x36 > p + bytes)
            return false;
        for (uint32_t g = 0; g < groups; g++) {
            std::vector<uint8_t> palette;
            for (int slot = 0; slot < 0x36 && pal[g * 0x36 + slot] != 0xff; slot++)
                palette.push_back(pal[g * 0x36 + slot]);
            k.palettes.push_back(palette);
        }
        k.vertexPalette.assign(vtxCnt, -1);
        size_t start = 0;
        int palette = -1;
        for (const KxePrimitiveData &prim : k.primitives) {
            if (prim.palette != 0)
                palette = prim.palette - 1;
            for (size_t i = start; i < start + prim.count + 2 && i < k.indices.size(); i++)
                if (k.indices[i] < vtxCnt)
                    k.vertexPalette[k.indices[i]] = palette;
            start += prim.count + 2;
        }
        k.morphTargets = (int)GcLe32(p + 0x24);
        k.morphVertices = (int)GcLe32(p + 0x28);
        const uint8_t *morph = pal + groups * 0x36;
        size_t n = (size_t)k.morphTargets * k.morphVertices * 3;
        if (n != 0 && morph + n * 2 <= p + bytes) {
            k.morphDeltas.resize(n);
            memcpy(k.morphDeltas.data(), morph, n * 2);
        } else {
            k.morphTargets = k.morphVertices = 0;
        }
    }
    return true;
}

// A pack's entities in order, each with its mesh block (id meshId, before its parameters block paramsId), or none
std::vector<Block> EntityMeshes(const uint8_t *pack, uint32_t bytes, int meshId, int paramsId) {
    std::vector<Block> out;
    Block mesh = { 0, NULL, 0 };
    for (const Block &b : Blocks(pack, bytes)) {
        if (b.id == meshId)
            mesh = b;
        if (b.id == paramsId) {
            out.push_back(mesh);
            mesh.data = NULL;
        }
    }
    return out;
}

// Positions within a few millimetres taken as one
struct PositionSnap {
    static constexpr float kTolerance = 0.002f;
    std::vector<std::array<float, 3>> points;
    std::map<std::tuple<int, int, int>, std::vector<int>> grid;

    std::tuple<int, int, int> Cell(const float *p) const {
        return std::make_tuple((int)floorf(p[0] / kTolerance), (int)floorf(p[1] / kTolerance),
                               (int)floorf(p[2] / kTolerance));
    }
    int Find(const float *p) const {
        auto c = Cell(p);
        int best = -1;
        float bestD = kTolerance;
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
                for (int dz = -1; dz <= 1; dz++) {
                    auto it = grid.find(std::make_tuple(std::get<0>(c) + dx, std::get<1>(c) + dy, std::get<2>(c) + dz));
                    if (it == grid.end())
                        continue;
                    for (int i : it->second) {
                        const float *q = points[i].data();
                        float d = sqrtf((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) +
                                        (p[2] - q[2]) * (p[2] - q[2]));
                        if (d <= bestD) {
                            bestD = d;
                            best = i;
                        }
                    }
                }
        return best;
    }
    int Add(const float *p) {
        int i = Find(p);
        if (i >= 0)
            return i;
        points.push_back({ p[0], p[1], p[2] });
        grid[Cell(p)].push_back((int)points.size() - 1);
        return (int)points.size() - 1;
    }
};

void UnpackNormal(uint32_t n, float *out) {
    out[0] = (float)((int32_t)(n << 21) >> 21) / 1023.0f;
    out[1] = (float)((int32_t)(n << 10) >> 21) / 1023.0f;
    out[2] = (float)((int32_t)n >> 22) / 511.0f;
}

// A KXE's triangles as (three position ids, the primitive), wound as drawn; degenerate ones left out
struct Triangle {
    int v[3];
    int vertex[3];          // the KXE's own vertex indices
    int primitive;
};
std::vector<Triangle> KxeTriangles(const Kxe &k, const std::vector<int> &ids) {
    std::vector<Triangle> out;
    size_t start = 0;
    for (size_t p = 0; p < k.primitives.size(); p++) {
        const KxePrimitiveData &prim = k.primitives[p];
        for (uint32_t i = 0; i < prim.count && start + i + 2 < k.indices.size(); i++) {
            int a = k.indices[start + i], b = k.indices[start + i + 1], c = k.indices[start + i + 2];
            if (i & 1)
                std::swap(a, b);
            if (a >= (int)ids.size() || b >= (int)ids.size() || c >= (int)ids.size())
                continue;
            Triangle t = { { ids[a], ids[b], ids[c] }, { a, b, c }, (int)p };
            if (t.v[0] < 0 || t.v[1] < 0 || t.v[2] < 0 || t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[0] == t.v[2])
                continue;
            out.push_back(t);
        }
        start += prim.count + 2;
    }
    return out;
}

// The triangle's corners turned to start at the smallest id, keeping the winding
std::tuple<int, int, int> Canonical(int a, int b, int c) {
    if (b < a && b < c)
        return std::make_tuple(b, c, a);
    if (c < a && c < b)
        return std::make_tuple(c, a, b);
    return std::make_tuple(a, b, c);
}

struct MeshTotals {
    std::map<std::string, long> counts;
    void Add(const std::string &what, long n = 1) { counts[what] += n; }
};

void CompareMesh(const Kxe &ours, const Kxe &xbox, MeshTotals &t) {
    PositionSnap snap;
    std::vector<int> ourIds(ours.vertices.size()), xboxIds(xbox.vertices.size());
    for (size_t i = 0; i < ours.vertices.size(); i++)
        ourIds[i] = snap.Add(ours.vertices[i].position);
    std::map<int, std::vector<int>> xboxAt;     // our position id -> the Xbox's vertices there
    for (size_t i = 0; i < xbox.vertices.size(); i++) {
        xboxIds[i] = snap.Find(xbox.vertices[i].position);
        if (xboxIds[i] >= 0)
            xboxAt[xboxIds[i]].push_back((int)i);
    }
    t.Add("xbox vertices", (long)xbox.vertices.size());
    long at = 0;
    for (int id : xboxIds)
        at += id >= 0;
    t.Add("xbox vertices at one of ours", at);
    t.Add("our vertices", (long)ours.vertices.size());
    for (size_t i = 0; i < ours.vertices.size(); i++) {
        auto it = xboxAt.find(ourIds[i]);
        if (it == xboxAt.end())
            continue;
        t.Add("our vertices at an xbox vertex");
        const KxeVertexData &v = ours.vertices[i];
        // The Xbox's vertex there with the nearest coordinates
        const KxeVertexData *best = &xbox.vertices[it->second[0]];
        float bestD = 1e9f;
        for (int j : it->second) {
            const KxeVertexData &x = xbox.vertices[j];
            float d = fabsf(x.uv[0] - v.uv[0]) + fabsf(x.uv[1] - v.uv[1]);
            if (d < bestD) {
                bestD = d;
                best = &x;
            }
        }
        t.Add(bestD < 0.002f ? "  uv the same" : "  uv differs");
        float a[3], b[3];
        UnpackNormal(v.normal, a);
        UnpackNormal(best->normal, b);
        float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        t.Add(dot > 0.9f ? "  normal the same (cos > 0.9)" : dot < -0.9f ? "  normal opposite" : "  normal differs");
        bool colour = memcmp(v.colour, best->colour, 4) == 0;
        t.Add(colour ? "  colour the same" : memcmp(v.colour, best->colour, 3) == 0 ? "  colour, alpha differs"
                                                                                       : "  colour differs");
        int oa, ob, xa, xb;
        float ow, xw;
        int bestIndex = (int)(best - xbox.vertices.data());
        if (VertexBones(ours, (int)i, &oa, &ob, &ow) && VertexBones(xbox, bestIndex, &xa, &xb, &xw)) {
            // One bone is the same bone twice at weight 1; a pair may come in either order
            if (oa == ob || ow > 0.999f) { ob = oa; ow = 1.0f; }
            if (xa == xb || xw > 0.999f) { xb = xa; xw = 1.0f; }
            bool same = (oa == xa && ob == xb) || (oa == xb && ob == xa);
            bool dropped = !same && oa == ob && xa == oa && xw >= 0.95f;     // the Xbox's second bone under 5%
            t.Add(same ? "  bones the same" : dropped ? "  bones the same but for the xbox's second under 5%"
                                                      : "  bones differ");
            if (!same && !dropped && getenv("GCDATA_SHOW") != NULL)
                printf("  bones: ours %d %d %.3f, the xbox's %d %d %.3f\n", oa, ob, ow, xa, xb, xw);
            if (same) {
                float w = oa == xa ? xw : 1.0f - xw;
                t.Add(fabsf(w - ow) <= 0.01f ? "  bone A's weight the same (0.01)" : "  bone A's weight differs");
            }
        }
        if (i < (size_t)ours.morphVertices && bestIndex < xbox.morphVertices &&
            ours.morphTargets == xbox.morphTargets) {
            int exact = 0, close = 0, near = 0, moving = 0;
            for (int target = 0; target < ours.morphTargets; target++)
                for (int a = 0; a < 3; a++) {
                    int d1 = ours.morphDeltas[((size_t)target * ours.morphVertices + i) * 3 + a];
                    int d2 = xbox.morphDeltas[((size_t)target * xbox.morphVertices + bestIndex) * 3 + a];
                    exact += d1 == d2;
                    close += abs(d1 - d2) <= 4;
                    near += abs(d1 - d2) <= 32;
                    moving += d1 != 0 || d2 != 0;
                }
            int all = ours.morphTargets * 3;
            if (moving != 0 && getenv("GCDATA_SHOW") != NULL && (close != all || getenv("GCDATA_SHOW")[0] == '2')) {
                printf("  morph vertex %zu:", i);
                for (int a = 0; a < 6; a++)
                    printf(" %d/%d", ours.morphDeltas[((size_t)(a / 3) * ours.morphVertices + i) * 3 + a % 3],
                           xbox.morphDeltas[((size_t)(a / 3) * xbox.morphVertices + bestIndex) * 3 + a % 3]);
                printf("\n");
            }
            if (moving != 0) {
                t.Add("  morph vertices compared");
                t.Add(exact == all ? "    every delta the same" : close == all ? "    every delta within 4"
                                      : near == all ? "    every delta within 32" : "    deltas differ");
            }
        } else if (i < (size_t)ours.morphVertices) {
            t.Add("  our morph vertex with no xbox morph vertex there");
        }
    }

    std::map<std::tuple<int, int, int>, std::vector<int>> xboxTris;    // -> the Xbox primitives drawing it
    std::set<std::pair<int, int>> xboxEdges;
    for (const Triangle &x : KxeTriangles(xbox, xboxIds)) {
        xboxTris[Canonical(x.v[0], x.v[1], x.v[2])].push_back(x.primitive);
        for (int e = 0; e < 3; e++)
            xboxEdges.insert(std::minmax(x.v[e], x.v[(e + 1) % 3]));
    }
    std::set<std::pair<std::tuple<int, int, int>, int>> seen;
    for (const Triangle &o : KxeTriangles(ours, ourIds)) {
        auto key = Canonical(o.v[0], o.v[1], o.v[2]);
        const KxePrimitiveData &op = ours.primitives[o.primitive];
        if (!seen.insert(std::make_pair(key, (int)op.tex0)).second)
            continue;
        t.Add("our triangles");
        auto it = xboxTris.find(key);
        if (it == xboxTris.end()) {
            if (xboxTris.count(Canonical(o.v[0], o.v[2], o.v[1]))) {
                t.Add("  wound the other way");
                continue;
            }
            int edges = 0;
            for (int e = 0; e < 3; e++)
                edges += xboxEdges.count(std::minmax(o.v[e], o.v[(e + 1) % 3])) != 0;
            t.Add(edges == 2 ? "  the quad's other diagonal on the xbox" : "  not on the xbox");
            continue;
        }
        t.Add("  the xbox's, wound the same");
        // Against the Xbox primitive with the same texture, else any
        const KxePrimitiveData *xp = &xbox.primitives[it->second[0]];
        for (int p : it->second)
            if (xbox.primitives[p].tex0 == op.tex0)
                xp = &xbox.primitives[p];
        t.Add(xp->tex0 == op.tex0 ? "    texture the same" : "    texture differs");
        if (xp->tex0 != op.tex0)
            continue;
        auto state = [&](const char *name, int mask) {
            bool a = (op.states & mask) != 0, b = (xp->states & mask) != 0;
            t.Add(std::string("    ") + name + (a == b ? " the same" : a ? " ours only" : " xbox only"));
        };
        state("double-sided", 0x08);
        if (op.states & 0x06)
            t.Add(std::string("    blended: double-sided on the xbox ") + ((xp->states & 0x08) ? "yes" : "no") +
                  ", ours " + ((op.states & 0x08) ? "yes" : "no"));
        state("blend additive", 0x02);
        state("blend subtractive", 0x04);
        state("z-write", 0x20);
        state("alpha reference 180", 0x10);
        state("wrap u", 0x80);
        state("wrap v", 0x100);
        t.Add(op.tex1 == xp->tex1 ? "    second texture the same" : "    second texture differs");
    }
}

// Rigid meshes, or with skins the skinned ones
int Meshes(GcDisc &disc, XboxDisc &xboxDisc, const std::vector<std::string> &hashes, bool skins) {
    MeshTotals t;
    for (const std::string &hash : hashes) {
        Bytes bundle, xbox;
        if (!disc.Bundle(hash, bundle) || !xboxDisc.Read((hash + ".bin").c_str(), xbox)) {
            printf("%s: not on both discs\n", hash.c_str());
            continue;
        }
        std::vector<GcBundleEntry> ge, xe;
        GcBundle_Directory(bundle.data(), (uint32_t)bundle.size(), ge);
        GcBundle_Directory(xbox.data(), (uint32_t)xbox.size(), xe);
        std::map<std::string, const GcBundleEntry *> xbyName;
        for (const GcBundleEntry &e : xe)
            xbyName[Upper(e.name)] = &e;
        for (const GcBundleEntry &e : ge) {
            if (!(e.type == 0 || e.type == 1 || e.type == 2 || e.type == 11))
                continue;
            auto xi = xbyName.find(Upper(e.name));
            if (xi == xbyName.end())
                continue;
            std::vector<Block> ours = EntityMeshes(bundle.data() + e.offset, e.length, GC_BLOCK_ENTITY_GEOMETRY,
                                                   GC_BLOCK_ENTITY_PARAMS);
            std::vector<Block> theirs = EntityMeshes(xbox.data() + xi->second->offset, xi->second->length, 0x0d, 0x04);
            if (ours.size() != theirs.size()) {
                t.Add("packs with another entity count");
                continue;
            }
            for (size_t i = 0; i < ours.size(); i++) {
                if (ours[i].data == NULL)
                    continue;
                t.Add("meshes");
                Kxe x;
                bool xboxOk = theirs[i].data != NULL && ReadKxe(theirs[i].data, theirs[i].bytes, x);
                std::vector<uint8_t> kxe;
                GcMeshStats stats;
                bool converted = GcMesh_ConvertToKxe(ours[i].data, bundle.data(), (uint32_t)bundle.size(), kxe,
                                                      &stats);
                if (stats.skinned != skins) {
                    t.Add(skins ? "  rigid (see meshes)" : "  skinned (see skins)");
                    continue;
                }
                if (!converted) {
                    t.Add(stats.outOfRange ? "  NOT CONVERTED: past the bundle" : "  nothing to draw");
                    continue;
                }
                t.Add("  converted");
                if (skins) {
                    t.Add("  bone palettes", stats.palettes);
                    if (stats.morphTargets != 0)
                        t.Add("  with morph targets");
                }
                t.Add("  double-sided triangles", stats.doubleSidedTriangles);
                if (stats.layers != 0)
                    t.Add("  with layers after the first");
                Kxe k;
                if (!ReadKxe(kxe.data(), (uint32_t)kxe.size(), k)) {
                    t.Add("  OUR KXE DOES NOT READ");
                    continue;
                }
                if (!xboxOk || x.vertices.empty()) {
                    t.Add("  the xbox's draws nothing");
                    continue;
                }
                if (x.skinned != skins) {
                    t.Add(skins ? "  the xbox's rigid" : "  the xbox's skinned");
                    continue;
                }
                t.Add("  compared");
                MeshTotals one;
                CompareMesh(k, x, one);
                for (auto &c : one.counts)
                    t.Add(c.first, c.second);
                long covered = one.counts["xbox vertices at one of ours"];
                if (getenv("GCDATA_SHOW") != NULL && covered * 10 < (long)x.vertices.size() * 9)
                    printf("  %s %s mesh %zu (%.20s): %ld of %zu xbox vertices at ours, %zu of ours\n", hash.c_str(),
                           e.name.c_str(), i, (const char *)ours[i].data + 4, covered, x.vertices.size(),
                           k.vertices.size());
            }
        }
    }
    for (auto &c : t.counts)
        printf("%-50s %ld\n", c.first.c_str(), c.second);
    return 0;
}

// ---- Sound: the banks decoded, their sounds and the music's markers against the Xbox's

void WriteWav(const std::string &path, const int16_t *pcm, size_t samples, int channels, int rate) {
    Bytes out(44 + samples * 2);
    uint32_t data = (uint32_t)(samples * 2);
    memcpy(&out[0], "RIFF", 4);
    GcPutLe32(&out[4], 36 + data);
    memcpy(&out[8], "WAVEfmt ", 8);
    GcPutLe32(&out[16], 16);
    GcPutLe32(&out[20], 1 | (uint32_t)channels << 16);
    GcPutLe32(&out[24], (uint32_t)rate);
    GcPutLe32(&out[28], (uint32_t)(rate * channels * 2));
    GcPutLe32(&out[32], (uint32_t)(channels * 2) | 16u << 16);
    memcpy(&out[36], "data", 4);
    GcPutLe32(&out[40], data);
    memcpy(&out[44], pcm, data);
    WriteWhole(path, out.data(), out.size());
}

int Sound(GcDisc &disc, XboxDisc &xbox, const char *wavDir) {
    std::map<std::string, long> t;
    uint32_t pcmFingerprint = 2166136261u;      // FNV-1a over every sample's PCM, bank by bank
    for (int bank = 0; bank < 32; bank++) {
        char name[512], path[512];
        Bytes shf, shg, sbf, sfx, x;
        snprintf(path, sizeof(path), "%s/gc/audio/SFX/English/SB_%d/SB_%d.", disc.root.c_str(), bank / 8, bank);
        std::string p = path;
        if (!ReadWhole(p + "shf", shf) || !ReadWhole(p + "shg", shg) || !ReadWhole(p + "sbf", sbf) ||
            !ReadWhole(p + "sfx", sfx))
            continue;
        std::vector<GcSoundSample> samples;
        if (!GcSound_DecodeBank(shf.data(), (uint32_t)shf.size(), shg.data(), (uint32_t)shg.size(), sbf.data(),
                                (uint32_t)sbf.size(), samples)) {
            t["banks not decoded"]++;
            continue;
        }
        t["banks"]++;
        t["samples"] += (long)samples.size();
        double seconds = 0;
        for (size_t i = 0; i < samples.size(); i++) {
            const GcSoundSample &s = samples[i];
            seconds += (double)s.pcm.size() / (s.frequency > 0 ? s.frequency : 1);
            if (s.pcm.size() <= 1)
                t["samples empty"]++;
            if (s.loops)
                t["samples that loop"]++;
            if (wavDir != NULL) {
                snprintf(name, sizeof(name), "%s/SB_%d_%03zu.wav", wavDir, bank, i);
                WriteWav(name, s.pcm.data(), s.pcm.size(), 1, s.frequency);
            }
        }
        t["seconds of sound"] += (long)seconds;
        for (size_t i = 0; i < samples.size(); i++) {
            const GcSoundSample &s = samples[i];
            for (int16_t v : s.pcm)
                for (int b = 0; b < 2; b++) {
                    pcmFingerprint ^= (uint8_t)((uint16_t)v >> (8 * b));
                    pcmFingerprint *= 16777619u;
                }
            // What the backends get (sound/PcmBanks.cpp prints the same for the first samples played)
            if (getenv("GCDATA_SHOW") != NULL) {
                std::vector<int16_t> padded = s.pcm;
                PcmBanks_Pad(padded, s.loops ? s.loopStart : -1);
                printf("bank %d's sample %zu: %zu samples at %d Hz, fingerprint %08x\n", bank, i, padded.size(),
                       s.frequency, PcmBanks_Fingerprint(padded.data(), padded.size()));
            }
        }
        // The sounds against the Xbox's, word for word
        GcSound_ConvertSfx(sfx.data(), (uint32_t)sfx.size());
        snprintf(name, sizeof(name), "ENGLISH/SB_%d/SB_%d.SFX", bank / 8, bank);
        if (xbox.Read(name, x)) {
            size_t n = std::min(sfx.size(), x.size()), same = 0;
            for (size_t i = 0; i < n; i++)
                same += sfx[i] == x[i];
            t["sfx bytes"] += (long)sfx.size();
            t["sfx bytes the same as the xbox's"] += (long)same;
            // The Xbox's archive pads its files to 2 KB
            bool sameSize = sfx.size() == x.size() || (x.size() > sfx.size() && x.size() - sfx.size() < 2048);
            t[sameSize ? "sfx files the same size" : "sfx files of another size"]++;
        }
    }
    for (int track = 1; track < 32; track++) {
        char path[512], name[64];
        Bytes ssd, smf, xssd, xsmf;
        snprintf(path, sizeof(path), "%s/gc/audio/SFX/music/MFX_%d/MFX_%d.", disc.root.c_str(), track >> 4, track);
        std::string p = path;
        if (!ReadWhole(p + "ssd", ssd) || !ReadWhole(p + "smf", smf))
            continue;
        t["music tracks"]++;
        std::vector<uint8_t> music;
        GcSound_XboxMusic(ssd.data(), (uint32_t)ssd.size(), music);
        GcSound_ConvertMarkers(smf.data(), (uint32_t)smf.size());
        snprintf(name, sizeof(name), "music/MFX_%d/MFX_%d.SSD", track >> 4, track);
        bool haveX = xbox.Read(name, xssd);
        snprintf(name, sizeof(name), "music/MFX_%d/MFX_%d.SMF", track >> 4, track);
        haveX = haveX && xbox.Read(name, xsmf);
        double gcSeconds = (double)ssd.size() / GC_MUSIC_FREQUENCY;   // a byte a sample of both channels
        t["music seconds"] += (long)gcSeconds;
        if (!haveX)
            continue;
        double xSeconds = (double)xssd.size() / 72 * 64 / 44100;
        t[fabs(gcSeconds - xSeconds) < 0.5 ? "music tracks the xbox's length (0.5 s)"
                                           : "music tracks of another length"]++;
        // Marker positions against the Xbox's, in seconds
        if (xsmf.size() >= 20 && smf.size() >= 20) {
            uint32_t mc = std::min(GcLe32(&smf[4]), GcLe32(&xsmf[4]));
            uint32_t mo = GcLe32(&smf[12]), xmo = GcLe32(&xsmf[12]);
            for (uint32_t i = 0; i < mc; i++) {
                if (mo + i * 32 + 8 > smf.size() || xmo + i * 32 + 8 > xsmf.size())
                    break;
                double a = (double)(int32_t)GcLe32(&smf[mo + i * 32 + 4]) / 72 * 64 / GC_MUSIC_FREQUENCY;
                double b = (double)(int32_t)GcLe32(&xsmf[xmo + i * 32 + 4]) / 72 * 64 / 44100;
                t[fabs(a - b) < 0.01 ? "markers at the xbox's time (10 ms)" : "markers at another time"]++;
            }
        }
        if (wavDir != NULL && track == 1) {
            std::vector<int16_t> pcm(XAdpcm_DecodedValueCount(music.size(), 2));
            size_t got = XAdpcm_Decode(music.data(), music.size(), 2, pcm.data(), pcm.size());
            std::string w = std::string(wavDir) + "/MFX_1.wav";
            WriteWav(w, pcm.data(), got, 2, GC_MUSIC_FREQUENCY);
        }
    }
    for (auto &c : t)
        printf("%-40s %ld\n", c.first.c_str(), c.second);
    printf("the samples' PCM, fingerprint %08x\n", pcmFingerprint);
    return 0;
}

// The names the Xbox build asks its archives for, each found on the GameCube's disc as GcFs.cpp finds it (GcDisc_Path)
// and set beside the Xbox's file: whether both discs have it and whether the bytes are the same
int Names(GcDisc &disc, XboxDisc &xbox) {
    std::vector<std::string> names;
    static const char *const kTextBanks[] = { "UKTxt.Dat", "FRTxt.Dat", "GRTxt.Dat", "SPTxt.Dat", "ITTxt.Dat",
                                              "DUTxt.Dat", "USATxt.Dat", "JAPTxt.Dat", "SWTxt.Dat" };
    for (const char *t : kTextBanks)
        names.push_back(t);
    names.push_back("woman.rle");
    names.push_back("SBINFO.SBI");
    names.push_back("MFXINFO.MXI");
    char name[64];
    for (int bank = 0; bank < 32; bank++) {
        static const char *const kExtensions[] = { "SFX", "SHF", "SBF", "SHG" };
        for (const char *ext : kExtensions) {
            snprintf(name, sizeof(name), "ENGLISH/SB_%d/SB_%d.%s", bank / 8, bank, ext);
            names.push_back(name);
        }
    }
    for (int track = 1; track <= 31; track++) {
        snprintf(name, sizeof(name), "music/MFX_%d/MFX_%d.SSD", track >> 4, track);
        names.push_back(name);
        snprintf(name, sizeof(name), "music/MFX_%d/MFX_%d.SMF", track >> 4, track);
        names.push_back(name);
    }
    names.push_back("ENGLISH/STREAMS/STREAMS.BIN");
    names.push_back("ENGLISH/STREAMS/STREAMS.LUT");
    for (uint32_t level = 0x07000000; level < 0x07000100; level++) {
        snprintf(name, sizeof(name), "%08x.bin", level);
        names.push_back(name);
    }

    static const char *const kKinds[] = { "none", "as is", "bundle", "sound", "movie" };
    std::map<std::string, int> totals;
    for (const std::string &n : names) {
        Bytes x, g;
        bool onXbox = xbox.Read(n.c_str(), x);
        char path[256];
        GcFileKind kind = GcDisc_Path(n.c_str(), path, sizeof(path));
        bool onGc = kind != GC_FILE_NONE && GcDisc::Read(&disc, path, g) && !g.empty();
        if (!onXbox && !onGc)
            continue;
        const char *what;
        if (!onGc)
            what = "on the Xbox only";
        else if (!onXbox)
            what = "on the GameCube only";
        else if (kind == GC_FILE_BUNDLE)
            what = "a bundle on both (convert)";
        else {
            // The Xbox's files are padded to 2 KB in its archives
            bool same = x.size() >= g.size() && memcmp(x.data(), g.data(), g.size()) == 0;
            for (size_t i = g.size(); same && i < x.size(); i++)
                same = x[i] == 0;
            what = same ? "the same bytes" : "differs";
        }
        totals[std::string(kKinds[kind]) + ": " + what]++;
        printf("%-30s %-6s %-26s %9zu %9zu  %s\n", n.c_str(), kKinds[kind], what, x.size(), g.size(), path);
    }
    for (auto &t : totals)
        printf("  %s: %d\n", t.first.c_str(), t.second);
    return 0;
}

// The "DT" headers' indices: each names the texture whose name it carries, counting every texture header of the
// bundle's packs in the order the loader meets them from 0 (a DT header takes a slot too)
int Duplicates(GcDisc &disc, const std::string &hash) {
    Bytes bundle;
    if (!disc.Bundle(hash, bundle)) {
        printf("no bundle %s\n", hash.c_str());
        return 1;
    }
    std::vector<GcBundleEntry> entries;
    GcBundle_Directory(bundle.data(), (uint32_t)bundle.size(), entries);
    std::vector<std::string> names;
    std::vector<std::pair<uint32_t, std::string>> duplicates;
    for (const GcBundleEntry &e : entries) {
        if (!(e.type == 0 || e.type == 1 || e.type == 2 || e.type == 11))
            continue;
        for (const Block &b : Blocks(bundle.data() + e.offset, e.length)) {
            if (b.id != GC_BLOCK_TEXTURE_DATA)
                continue;
            std::string name((const char *)b.data + 4, strnlen((const char *)b.data + 4, 16));
            uint32_t magic = GcBe32(b.data);
            if ((magic >> 16) == GC_TEX_DUPLICATE)
                duplicates.push_back(std::make_pair(magic & 0xffff, name));
            names.push_back(name);
        }
    }
    int same = 0;
    for (auto &d : duplicates) {
        if (d.first < names.size() && names[d.first] == d.second)
            same++;
        else if (getenv("GCDATA_SHOW") != NULL)
            printf("  DT %u %s names %s\n", d.first, d.second.c_str(),
                   d.first < names.size() ? names[d.first].c_str() : "(past the end)");
    }
    printf("%s: %zu texture headers, %zu DT, %d naming the texture of their own name\n", hash.c_str(), names.size(),
           duplicates.size(), same);
    return same != (int)duplicates.size();
}

int Textures(GcDisc &disc, const std::string &hash, const char *outDir) {
    Bytes bundle;
    if (!disc.Bundle(hash, bundle)) {
        printf("no bundle %s\n", hash.c_str());
        return 1;
    }
    std::vector<GcBundleEntry> entries;
    GcBundle_Directory(bundle.data(), (uint32_t)bundle.size(), entries);
    std::map<int, int> formats;
    int converted = 0, failed = 0, duplicates = 0, lossless = 0, rateMatches = 0, animated = 0;
    long rgb5a3Opaque = 0, rgb5a3Texels = 0;
    std::vector<uint8_t> kxt;
    for (const GcBundleEntry &e : entries) {
        if (!(e.type == 0 || e.type == 1 || e.type == 2 || e.type == 11))
            continue;
        std::vector<Block> blocks = Blocks(bundle.data() + e.offset, e.length);
        std::vector<uint8_t> headers;
        int texture = 0;
        for (const Block &b : blocks) {
            if (b.id == GC_BLOCK_TEXTURE_HEADER)
                headers.assign(b.data, b.data + b.bytes);
            if (b.id != GC_BLOCK_TEXTURE_DATA)
                continue;
            int frames = 1, rate = 0;
            if ((size_t)(texture + 1) * 12 <= headers.size()) {
                frames = headers[texture * 12 + 6];
                rate = headers[texture * 12 + 7];
            }
            texture++;
            if (frames > 1)
                animated++;
            if ((GcBe32(b.data) >> 16) == GC_TEX_DUPLICATE) {
                duplicates++;
                continue;
            }
            int format = (int)GcBe32(b.data + 0x20), w = GcBe16(b.data + 0x1c), h = GcBe16(b.data + 0x1e);
            formats[format]++;
            if (frames > 1 && (int)GcBe32(b.data + 0x2c) == rate)
                rateMatches++;
            if (!GcTexture_ToKxt(b.data, b.bytes, frames, rate, false, kxt)) {
                failed++;
                printf("  %s texture %d: not converted (format %d, %dx%d, %d frames, %u bytes)\n", e.name.c_str(),
                       texture - 1, format, w, h, frames, GcBe32(b.data + 0x18));
                continue;
            }
            converted++;
            // Decode the GameCube's level 0 and the KXT's back: the same texels, for CMPR's blocks reordered
            std::vector<uint8_t> back;
            int kw = w, kh = h;
            bool decoded = DecodeKxt(kxt.data(), (uint32_t)kxt.size(), back, &kw, &kh);
            w = kw;     // the size the GameCube draws it at
            h = kh;
            std::vector<uint8_t> gc((size_t)w * h * 4);
            GcTexture_DecodeLevel(b.data + sizeof(GcTextureHeader), format, w, h, gc.data());
            if (decoded && back == gc)
                lossless++;
            if (format == GC_TEX_RGB5A3) {
                const uint8_t *t = b.data + sizeof(GcTextureHeader);
                for (int i = 0; i < w * h; i++, rgb5a3Texels++)
                    rgb5a3Opaque += (t[i * 2] & 0x80) != 0;
            }
            if (outDir != NULL) {
                char name[512];
                char texName[17] = {};
                memcpy(texName, b.data + 4, 16);
                for (char &c : texName)
                    if (c == '/' || c == '\\' || c == ' ')
                        c = '_';
                snprintf(name, sizeof(name), "%s/%s_%03d_%s.tga", outDir, e.name.c_str(), texture - 1, texName);
                WriteTga(name, gc, w, h);
            }
        }
    }
    printf("%s: %d textures converted, %d failed, %d duplicates (DT), %d animated; formats:", hash.c_str(), converted,
           failed, duplicates, animated);
    for (auto &f : formats)
        printf(" %d=%d", f.first, f.second);
    printf("\n  the KXT decodes to the GameCube's texels exactly in %d of %d\n", lossless, converted);
    printf("  animated textures whose header rate (0x2c) is the 0x10 header's: %d of %d\n", rateMatches, animated);
    if (rgb5a3Texels != 0)
        printf("  format 6 texels with the top bit set (RGB5A3's opaque form): %.1f%%\n",
               100.0 * rgb5a3Opaque / rgb5a3Texels);
    return failed != 0;
}

// Block-by-block comparison of a converted pack with its Xbox twin
struct PackCompare {
    std::map<std::string, std::map<std::string, int>> counts;   // block id -> result -> count
    double texturePsnrSum = 0;
    int texturePsnrCount = 0;
    int textureExact = 0;
    double texturePsnrWorst = 99;
    std::string worstTexture;

    void Note(int id, const char *what) {
        char key[8];
        snprintf(key, sizeof(key), "%02x", id);
        counts[key][what]++;
    }
};

// An entity's name as the GameCube's exporter wrote it: upper case, spaces and dashes as underscores
std::string EntityName(std::string s) {
    for (char &c : s)
        c = c == ' ' || c == '-' ? '_' : (char)toupper((unsigned char)c);
    return s;
}

// Placements equal but for their padding (bytes 2-3, 0x46-0x47)
bool SamePlacements(const Block &a, const Block &b) {
    if (a.bytes != b.bytes)
        return false;
    uint32_t o = 0;
    while (o + 0x4c <= a.bytes) {
        uint32_t keys = GcLe32(b.data + o + 0x48);
        uint32_t size = 0x4c + keys * 8;
        if (o + size > a.bytes)
            return false;
        for (uint32_t i = 0; i < size; i++) {
            if (i == 2 || i == 3 || i == 0x46 || i == 0x47)
                continue;
            if (a.data[o + i] != b.data[o + i])
                return false;
        }
        o += size;
    }
    return o == a.bytes;
}

void ComparePack(const uint8_t *ours, uint32_t ourBytes, const uint8_t *xbox, uint32_t xboxBytes, PackCompare &pc,
                 const std::string &pack) {
    std::map<int, std::vector<Block>> a, b;
    for (const Block &k : Blocks(ours, ourBytes))
        a[k.id == GC_BLOCK_ENTITY_PARAMS ? 0x04 : k.id == GC_BLOCK_COLLISION ? 0x2e : k.id].push_back(k);
    for (const Block &k : Blocks(xbox, xboxBytes))
        b[k.id == GC_BLOCK_ENTITY_PARAMS ? 0x04 : k.id == GC_BLOCK_COLLISION ? 0x2e : k.id].push_back(k);
    std::set<int> ids;
    for (auto &k : a) ids.insert(k.first);
    for (auto &k : b) ids.insert(k.first);
    for (int id : ids) {
        std::vector<Block> &x = a[id], &y = b[id];
        if (id == 0x0d) {           // the meshes: the meshes command compares them
            pc.Note(id, x.size() == y.size() ? "as many meshes" : x.size() < y.size() ? "fewer meshes than the Xbox's"
                                                                                     : "more meshes than the Xbox's");
            continue;
        }
        if (x.size() != y.size()) {
            pc.Note(id, "pack with another count");
            continue;
        }
        for (size_t i = 0; i < x.size(); i++) {
            const Block &u = x[i], &v = y[i];
            bool same;
            switch (id) {
            case 0x0f:      // the palette headers: the second word (the hashcode); the first is each disc's own
                same = u.bytes == v.bytes;
                for (uint32_t o = 0; same && o + 8 <= u.bytes; o += 8)
                    same = memcmp(u.data + o + 4, v.data + o + 4, 4) == 0;
                break;
            case 0x10:      // the texture headers: all but the first two bytes, each disc's own; a size is the disc's
                same = u.bytes == v.bytes;
                for (uint32_t o = 0; same && o + 12 <= u.bytes; o += 12) {
                    if (memcmp(u.data + o + 8, v.data + o + 8, 4) != 0 || u.data[o + 6] != v.data[o + 6])
                        same = false;
                    else if (memcmp(u.data + o + 2, v.data + o + 2, 4) != 0)
                        pc.Note(id, GcLe32(u.data + o + 2) < GcLe32(v.data + o + 2) ? "entries smaller than the Xbox's"
                                                                                    : "entries larger than the Xbox's");
                    else if (u.data[o + 7] != v.data[o + 7])
                        pc.Note(id, "texture of another rate (entries)");
                }
                break;
            case 0x1a:
                same = SamePlacements(u, v);
                break;
            case 0x04: {    // the entity parameters, then the name in another case
                if (u.bytes < 0x30 || v.bytes < 0x30) {
                    pc.Note(id, "too short");
                    continue;
                }
                int differ = 0;
                for (int o = 0; o < 0x30; o++)
                    differ += u.data[o] != v.data[o];
                std::string n1((const char *)u.data + 0x30, strnlen((const char *)u.data + 0x30, u.bytes - 0x30));
                std::string n2((const char *)v.data + 0x30, strnlen((const char *)v.data + 0x30, v.bytes - 0x30));
                pc.Note(id, differ == 0 ? "parameters same" : differ <= 2 ? "parameters 1-2 bytes apart (float noise)"
                                                                          : "parameters differ");
                pc.Note(id, EntityName(n1) == EntityName(n2) ? "name same (exporter's case)" : "name differs");
                continue;
            }
            case 0x18: {
                int w1, h1, w2, h2;
                std::vector<uint8_t> i1, i2;
                bool d1 = DecodeKxt(u.data, u.bytes, i1, &w1, &h1), d2 = DecodeKxt(v.data, v.bytes, i2, &w2, &h2);
                uint32_t m1 = GcLe32(u.data), m2 = GcLe32(v.data);
                if ((m1 >> 16) == 0x4454 || (m2 >> 16) == 0x4454) {
                    if (((m1 >> 16) == 0x4454) != ((m2 >> 16) == 0x4454))
                        pc.Note(id, "DT on one only");
                    else
                        pc.Note(id, m1 == m2 ? "both DT, the same index" : "both DT, another index");
                    continue;
                }
                if (!d1 || !d2) {
                    char what[64];
                    if (!d1)
                        snprintf(what, sizeof(what), "ours not decoded");
                    else
                        snprintf(what, sizeof(what), "xbox's not decoded (format %u, %u bits)", GcLe32(v.data + 0x20),
                                 GcLe32(v.data + 0x14));
                    pc.Note(id, what);
                    continue;
                }
                if (w1 == w2 && h1 == h2) {
                    pc.Note(id, "same size");
                } else if (w2 >= w1 && h2 >= h1 && w2 % w1 == 0 && h2 % h1 == 0 && w2 / w1 == h2 / h1) {
                    pc.Note(id, "xbox larger");
                    i2 = Shrink(i2, w2, h2, w1, h1);
                } else {
                    pc.Note(id, "other size");
                    continue;
                }
                double psnr = Psnr(i1, i2);
                if (psnr == 99.0)
                    pc.textureExact++;
                pc.texturePsnrSum += psnr;
                pc.texturePsnrCount++;
                if (psnr < pc.texturePsnrWorst) {
                    pc.texturePsnrWorst = psnr;
                    char texName[17] = {};
                    memcpy(texName, (const char *)v.data + 0x34, 16);
                    pc.worstTexture = pack + " " + texName;
                }
                continue;
            }
            default:
                same = u.bytes == v.bytes && memcmp(u.data, v.data, u.bytes) == 0;
                break;
            }
            if (!same && getenv("GCDATA_SHOW") != NULL) {
                uint32_t o = 0;
                while (o < u.bytes && o < v.bytes && u.data[o] == v.data[o])
                    o++;
                uint32_t at = o & ~15u;
                printf("  block %02x in %s: %u and %u bytes, first difference at 0x%x\n    ours", id, pack.c_str(),
                       u.bytes, v.bytes, o);
                for (uint32_t i = at; i < at + 32 && i < u.bytes; i++)
                    printf(" %02x", u.data[i]);
                printf("\n    xbox");
                for (uint32_t i = at; i < at + 32 && i < v.bytes; i++)
                    printf(" %02x", v.data[i]);
                printf("\n");
            }
            if (same) {
                pc.Note(id, "same");
            } else if (u.bytes == v.bytes) {
                // How far apart: bytes that differ (float noise in the exports shows as a few)
                uint32_t d = 0;
                for (uint32_t o = 0; o < u.bytes; o++)
                    d += u.data[o] != v.data[o];
                pc.Note(id, d * 1000 <= u.bytes ? "differs in under 0.1% of bytes" : "differs");
            } else {
                pc.Note(id, "another size");
            }
        }
    }
}

struct ConvertTotals {
    int levels = 0;
    std::map<int, std::map<std::string, int>> entries;     // type -> result -> count
    PackCompare packs;
};

int Convert(GcDisc &disc, XboxDisc &xboxDisc, const std::string &hash, ConvertTotals &totals, bool verbose) {
    Bytes ours, xbox;
    GcBundleStats stats;
    if (!disc.Convert(hash, ours, &stats)) {
        printf("%s: no GameCube bundle to convert\n", hash.c_str());
        return 1;
    }
    if (!xboxDisc.Read((hash + ".bin").c_str(), xbox)) {
        printf("%s: no Xbox twin\n", hash.c_str());
        return 1;
    }
    totals.levels++;
    std::vector<GcBundleEntry> a, b;
    bool okA = GcBundle_Directory(ours.data(), (uint32_t)ours.size(), a);
    bool okB = GcBundle_Directory(xbox.data(), (uint32_t)xbox.size(), b);
    printf("%s: %zu bytes as the Xbox's (%s), %d entries (%d sequences added, %d left out), %d packs: %d textures, "
           "%d DT, %d failed, %d meshes (%d skinned; %d with nothing to draw left out), %d unknown blocks; the "
           "Xbox's: %zu bytes, %zu entries\n",
           hash.c_str(), ours.size(), okA ? "directory reads" : "DIRECTORY BAD", stats.entries, stats.sequencesAdded,
           stats.dropped, stats.packs, stats.texturesConverted, stats.duplicates, stats.texturesFailed,
           stats.meshesConverted, stats.meshesSkinned, stats.meshesSkipped, stats.unknownBlocks, xbox.size(),
           okB ? b.size() : 0);
    std::map<std::string, const GcBundleEntry *> xb;
    for (const GcBundleEntry &e : b)
        xb[Upper(e.name)] = &e;
    std::set<std::string> seen;
    for (const GcBundleEntry &e : a) {
        auto it = xb.find(Upper(e.name));
        seen.insert(Upper(e.name));
        if (it == xb.end()) {
            totals.entries[e.type]["gc only"]++;
            continue;
        }
        const GcBundleEntry &f = *it->second;
        const uint8_t *u = ours.data() + e.offset, *v = xbox.data() + f.offset;
        if (e.type == 0 || e.type == 1 || e.type == 2 || e.type == 11) {
            totals.entries[e.type]["pack compared"]++;
            ComparePack(u, e.length, v, f.length, totals.packs, e.name);
            continue;
        }
        // Lengths are padded to 32 on both: compare the shorter
        uint32_t n = std::min(e.length, f.length);
        bool same = memcmp(u, v, n) == 0;
        for (uint32_t i = n; same && i < e.length; i++) same = u[i] == 0;
        for (uint32_t i = n; same && i < f.length; i++) same = v[i] == 0;
        totals.entries[e.type][same ? "identical" : "differs"]++;
        if (!same && verbose)
            printf("  %s (type %d) differs from the Xbox's\n", e.name.c_str(), e.type);
    }
    for (const GcBundleEntry &f : b)
        if (!seen.count(Upper(f.name))) {
            totals.entries[f.type]["xbox only"]++;
            if (verbose && f.type != 4)
                printf("  %s (type %d) only on the Xbox\n", f.name.c_str(), f.type);
        }
    return 0;
}

void PrintTotals(const ConvertTotals &t) {
    printf("over %d level(s): entries by type\n", t.levels);
    for (auto &e : t.entries) {
        printf("  type %2d:", e.first);
        for (auto &r : e.second)
            printf("  %s %d", r.first.c_str(), r.second);
        printf("\n");
    }
    printf("map blocks against the Xbox's (block id: result count)\n");
    for (auto &c : t.packs.counts) {
        printf("  %s:", c.first.c_str());
        for (auto &r : c.second)
            printf("  %s %d", r.first.c_str(), r.second);
        printf("\n");
    }
    if (t.packs.texturePsnrCount != 0)
        printf("textures against the Xbox's (level 0, the Xbox's shrunk where larger): mean PSNR %.1f dB over %d, "
               "%d decoding to the same texels, worst %.1f (%s)\n", t.packs.texturePsnrSum / t.packs.texturePsnrCount,
               t.packs.texturePsnrCount, t.packs.textureExact, t.packs.texturePsnrWorst, t.packs.worstTexture.c_str());
}

// The level bundles on both discs
const char *const kLevels[] = { "07000002", "07000003", "07000004", "07000005", "07000006", "07000007", "07000008",
    "07000009", "0700000a", "0700000b", "0700000c", "0700000d", "07000011", "07000012", "07000013", "07000014",
    "07000015", "07000016", "0700001b", "07000023", "07000024", "07000025", "07000026", "07000027", "07000028",
    "07000029", "07000048", "0700004a", "0700004b" };

} // namespace

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) {
        printf("usage: gcdata_check <gc disc> files | bundle <hash> | textures <hash> [<out>] | "
               "convert <hash> <xbox disc> | all <xbox disc> | xbox <xbox disc> <name> <out> | names <xbox disc> | "
               "dt <hash> | meshes <xbox disc> [<hash>...]\n");
        return 2;
    }
    GcDisc disc;
    if (!disc.Open(argv[1]))
        return 1;
    std::string cmd = argv[2];
    if (cmd == "files")
        return Files(disc);
    if (cmd == "bundle" && argc >= 4)
        return BundleInfo(disc, argv[3]);
    if (cmd == "decompress" && argc >= 5) {
        Bytes bundle;
        return disc.Bundle(argv[3], bundle) && WriteWhole(argv[4], bundle.data(), bundle.size()) ? 0 : 1;
    }
    if (cmd == "names" && argc >= 4) {
        XboxDisc xbox;
        return xbox.Open(argv[3]) ? Names(disc, xbox) : 1;
    }
    if (cmd == "sound" && argc >= 4) {
        XboxDisc xbox;
        return xbox.Open(argv[3]) ? Sound(disc, xbox, argc >= 5 ? argv[4] : NULL) : 1;
    }
    if ((cmd == "meshes" || cmd == "skins") && argc >= 4) {
        XboxDisc xbox;
        if (!xbox.Open(argv[3]))
            return 1;
        std::vector<std::string> hashes(argv + 4, argv + argc);
        if (hashes.empty())
            hashes.assign(std::begin(kLevels), std::end(kLevels));
        return Meshes(disc, xbox, hashes, cmd == "skins");
    }
    if (cmd == "dt" && argc >= 4)
        return Duplicates(disc, argv[3]);
    if (cmd == "textures" && argc >= 4)
        return Textures(disc, argv[3], argc >= 5 ? argv[4] : NULL);
    if (cmd == "xbox" && argc >= 6) {
        XboxDisc xbox;
        Bytes data;
        if (!xbox.Open(argv[3]) || !xbox.Read(argv[4], data)) {
            printf("no %s in the Xbox's archives\n", argv[4]);
            return 1;
        }
        return WriteWhole(argv[5], data.data(), data.size()) ? 0 : 1;
    }
    if ((cmd == "convert" && argc >= 5) || (cmd == "all" && argc >= 4)) {
        XboxDisc xbox;
        if (!xbox.Open(cmd == "all" ? argv[3] : argv[4]))
            return 1;
        ConvertTotals totals;
        if (cmd == "convert") {
            Convert(disc, xbox, argv[3], totals, true);
        } else {
            for (const char *level : kLevels)
                Convert(disc, xbox, level, totals, false);
        }
        PrintTotals(totals);
        return 0;
    }
    printf("unknown command %s\n", cmd.c_str());
    return 2;
}
