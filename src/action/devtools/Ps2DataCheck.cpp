// The PS2 data's conversions run offline over an extracted PS2 disc (docs/ps2-data.md): no game, no window.
//
//   ps2data_check <disc> files                          FileList and each file's size
//   ps2data_check <disc> bundle <hash>                  a level bundle's directory and its packs' blocks
//   ps2data_check <disc> textures <hash> [<out>]        every texture converted to a KXT; PNGs of frame 0 into <out>
//   ps2data_check <disc> meshes <hash> [<xbox bundle>] [--obj <out>]
//                                                       every mesh converted to a KXE and checked: strips against the
//                                                       stored normals, and against the Xbox's twin of each named
//                                                       entity when the Xbox's bundle (decompressed) is given
//   ps2data_check <disc> skins <hash> <xbox bundle>     every skinned mesh converted and checked against the Xbox's
//                                                       twin (the same hashcode): vertices, bones, weights, normals,
//                                                       STs, triangles and their winding
//   ps2data_check <disc> all                            textures and meshes of every level bundle, totals only
//   ps2data_check <disc> woman [<xbox woman.rle>]       the pause screen's film, as FS_ps2.cpp finds it
//   ps2data_check <disc> sound <bank> [<out>]            a sound bank's samples decoded and made Xbox ADPCM (WAVs)
//   ps2data_check <disc> music <track> [<out.wav>]       a music track read in pieces as the music code reads it
//
// <disc> is the folder with FILES.BIN, ACTION.ELF and PS2\. <hash> is a bundle's name without .bin (07000001).

#include "../ps2/Ps2Disc.h"
#include "../ps2/Ps2Mesh.h"
#include "../ps2/Ps2Texture.h"
#include "../ps2/Ps2Sound.h"
#include "../../common/sound/xadpcm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
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
    bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

struct Disc {
    std::string root;
    Ps2FileListEntry files[PS2_FILELIST_COUNT];
    int count = 0;

    bool Open(const std::string &folder) {
        root = folder;
        Bytes elf;
        if (!ReadWhole(root + "/ACTION.ELF", elf)) {
            printf("no %s/ACTION.ELF\n", root.c_str());
            return false;
        }
        count = Ps2Disc_ReadFileList(elf.data(), elf.size(), files, PS2_FILELIST_COUNT);
        if (count == 0)
            printf("ACTION.ELF has no FileList\n");
        return count != 0;
    }

    bool Read(const char *name, Bytes &out) {
        int i = Ps2Disc_FindFile(files, count, name);
        if (i < 0)
            return false;
        FILE *f = fopen((root + "/FILES.BIN").c_str(), "rb");
        if (f == NULL)
            return false;
        out.resize(files[i].size);
        fseek(f, (long)files[i].offset, SEEK_SET);
        bool ok = fread(out.data(), 1, out.size(), f) == out.size();
        fclose(f);
        return ok;
    }
};

// A bundle's entries: LoaderLoad's directory walk, with the header the given size (0x800 PS2, 0x20 Xbox)
struct Entry {
    std::string name;
    int type;
    const uint8_t *data;
    uint32_t size;
};

std::vector<Entry> ReadBundle(const Bytes &bundle, uint32_t headerBytes) {
    std::vector<Entry> entries;
    uint32_t dirSize, count;
    memcpy(&dirSize, bundle.data(), 4);
    memcpy(&count, bundle.data() + 4, 4);
    const uint8_t *p = bundle.data() + headerBytes;
    const uint8_t *data = p + dirSize;
    for (uint32_t i = 0; i < count; i++) {
        Entry e;
        memcpy(&e.size, p, 4);
        e.type = p[4];
        e.name = (const char *)(p + 5);
        p += 5 + e.name.size() + 1;
        e.data = data;
        data += e.size;
        if (data > bundle.data() + bundle.size()) {
            printf("bundle entry %s runs past the end\n", e.name.c_str());
            break;
        }
        entries.push_back(e);
    }
    return entries;
}

// A pack's blocks, as parsemap_parsenextblock walks them: by the offset table (version 1, the PS2's) or one after
// another (version 0, the Xbox's)
struct Block {
    int id;
    const uint8_t *data;    // after the header word
    uint32_t size;          // of the data
};

std::vector<Block> ReadBlocks(const uint8_t *pack, uint32_t packSize) {
    std::vector<Block> blocks;
    uint32_t version;
    memcpy(&version, pack, 4);
    const uint8_t *table = pack + 4;
    const uint8_t *next = pack + 4;
    for (;;) {
        if (version == 1) {
            uint32_t offset;
            memcpy(&offset, table, 4);
            table += 4;
            next = pack + offset + 4;
        }
        if (next + 4 > pack + packSize)
            break;
        uint32_t header;
        memcpy(&header, next, 4);
        Block b = { (int)(header >> 24), next + 4, (header & 0xffffff) - 4 };
        blocks.push_back(b);
        if (b.id == 0x1d || (header & 0xffffff) < 4)
            break;
        if (version != 1)
            next += header & 0xffffff;
    }
    return blocks;
}

bool IsPack(int type) {
    return type == 0 || type == 1 || type == 2 || type == 11;
}

// PNG with stored (uncompressed) deflate blocks: enough for a viewer
uint32_t Crc32(const uint8_t *p, size_t n, uint32_t crc = 0) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

void PutBE(Bytes &b, uint32_t v) {
    for (int i = 3; i >= 0; i--)
        b.push_back((uint8_t)(v >> (i * 8)));
}

void Chunk(Bytes &png, const char *type, const Bytes &data) {
    PutBE(png, (uint32_t)data.size());
    Bytes body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    PutBE(png, Crc32(body.data(), body.size()));
}

bool WritePng(const std::string &path, int width, int height, const uint8_t *bgra) {
    Bytes raw;
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        for (int x = 0; x < width; x++) {
            const uint8_t *p = bgra + (y * width + x) * 4;
            raw.push_back(p[2]); raw.push_back(p[1]); raw.push_back(p[0]); raw.push_back(p[3]);
        }
    }
    Bytes z = { 0x78, 0x01 };
    for (size_t at = 0; at < raw.size() || at == 0; at += 65535) {
        size_t n = raw.size() - at < 65535 ? raw.size() - at : 65535;
        z.push_back(at + n >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)n); z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)~n); z.push_back((uint8_t)(~n >> 8));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
        if (n == 0)
            break;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    PutBE(z, (b << 16) | a);
    Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    Bytes ihdr;
    PutBE(ihdr, width); PutBE(ihdr, height);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", Bytes());
    FILE *f = fopen(path.c_str(), "wb");
    if (f == NULL)
        return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}

// The swizzled frame back to linear, as the backends do
void Unswizzle(const uint32_t *src, uint32_t *dst, int width, int height) {
    std::vector<uint32_t> index((size_t)width * height);
    for (int i = 0; i < width * height; i++)
        index[i] = i;
    std::vector<uint32_t> swizzledIndex((size_t)width * height);
    Ps2Texture_Swizzle(index.data(), swizzledIndex.data(), width, height);
    for (int i = 0; i < width * height; i++)
        dst[swizzledIndex[i]] = src[i];
}

// ---- files, bundle ----

int CmdFiles(Disc &disc) {
    uint32_t total = 0;
    for (int i = 0; i < disc.count; i++) {
        printf("%3d  %-16.16s  sector %6u  %9u bytes\n", i, disc.files[i].name, disc.files[i].offset / PS2_SECTOR_SIZE,
               disc.files[i].size);
        total += disc.files[i].size;
    }
    printf("%d files, %u bytes\n", disc.count, total);
    return 0;
}

bool ReadPs2Bundle(Disc &disc, const std::string &hash, Bytes &bundle) {
    if (!disc.Read((hash + ".bin").c_str(), bundle)) {
        printf("%s.bin is not in FileList\n", hash.c_str());
        return false;
    }
    return true;
}

int CmdBundle(Disc &disc, const std::string &hash) {
    Bytes bundle;
    if (!ReadPs2Bundle(disc, hash, bundle))
        return 1;
    std::map<int, int> types;
    std::map<int, int> blockIds;
    for (const Entry &e : ReadBundle(bundle, PS2_SECTOR_SIZE)) {
        types[e.type]++;
        if (IsPack(e.type))
            for (const Block &b : ReadBlocks(e.data, e.size))
                blockIds[b.id]++;
    }
    printf("%s.bin: %zu bytes\nentries by type:", hash.c_str(), bundle.size());
    for (auto &t : types)
        printf(" %d:%d", t.first, t.second);
    printf("\nblocks by id:");
    for (auto &b : blockIds)
        printf(" 0x%02x:%d", b.first, b.second);
    printf("\n");
    return 0;
}

// ---- textures ----

struct TextureTotals {
    int textures = 0, frames = 0, failed = 0, resized = 0, colours16 = 0, colours256 = 0;
    int formats[4] = {};                // by Ps2TextureFormat
    uint64_t kxtBytes = 0;
    double psnrSum = 0, psnrWorst = 99;
    std::string worstName;
};

// DXT1 or DXT5 blocks back to texels (bytes B, G, R, A), as the backends upload them
void DecodeDxt(const uint8_t *blocks, int width, int height, bool dxt5, uint8_t *bgra) {
    for (int by = 0; by < height; by += 4) {
        for (int bx = 0; bx < width; bx += 4) {
            int alpha[16];
            for (int i = 0; i < 16; i++)
                alpha[i] = 255;
            if (dxt5) {
                int a0 = blocks[0], a1 = blocks[1], levels[8] = { a0, a1 };
                for (int k = 1; k < 7; k++)
                    levels[k + 1] = a0 > a1 ? ((7 - k) * a0 + k * a1) / 7 : (k < 5 ? ((5 - k) * a0 + k * a1) / 5 : 0);
                if (a0 <= a1) { levels[6] = 0; levels[7] = 255; }
                uint64_t bits = 0;
                for (int b = 0; b < 6; b++)
                    bits |= (uint64_t)blocks[2 + b] << (b * 8);
                for (int i = 0; i < 16; i++)
                    alpha[i] = levels[(bits >> (i * 3)) & 7];
                blocks += 8;
            }
            uint16_t c0, c1;
            uint32_t indices;
            memcpy(&c0, blocks, 2);
            memcpy(&c1, blocks + 2, 2);
            memcpy(&indices, blocks + 4, 4);
            blocks += 8;
            int p[4][4];
            for (int k = 0; k < 2; k++) {
                uint16_t c = k == 0 ? c0 : c1;
                p[k][0] = ((c >> 11) & 31) * 255 / 31; p[k][1] = ((c >> 5) & 63) * 255 / 63; p[k][2] = (c & 31) * 255 / 31;
                p[k][3] = 255;
            }
            bool four = dxt5 || c0 > c1;
            for (int c = 0; c < 3; c++) {
                p[2][c] = four ? (2 * p[0][c] + p[1][c]) / 3 : (p[0][c] + p[1][c]) / 2;
                p[3][c] = four ? (p[0][c] + 2 * p[1][c]) / 3 : 0;
            }
            p[2][3] = 255;
            p[3][3] = four ? 255 : 0;
            for (int i = 0; i < 16; i++) {
                const int *q = p[(indices >> (i * 2)) & 3];
                uint8_t *t = bgra + ((size_t)(by + i / 4) * width + bx + i % 4) * 4;
                t[0] = (uint8_t)q[2]; t[1] = (uint8_t)q[1]; t[2] = (uint8_t)q[0];
                t[3] = (uint8_t)(dxt5 ? alpha[i] : q[3]);
            }
        }
    }
}

// As parsemap: block 0x10 the headers, then per texture 0x16 (texels) and 0x12 (palette), which converts it
void ConvertPackTextures(const std::vector<Block> &blocks, const std::string &pack, const char *outDir,
                         TextureTotals &totals) {
    const bmpHeaderEntry *headers = NULL;
    const uint8_t *paletteHeaders = NULL;
    int count = 0, next = 0;
    const uint8_t *texels = NULL;
    uint32_t texelBytes = 0;
    for (const Block &b : blocks) {
        if (b.id == 0x10) {
            headers = (const bmpHeaderEntry *)b.data;
            count = (int)(b.size / sizeof(bmpHeaderEntry));
        } else if (b.id == 0x0f) {
            paletteHeaders = b.data;
        } else if (b.id == 0x16) {
            texels = b.data;
            texelBytes = b.size;
        } else if (b.id == 0x12 && headers != NULL && next < count) {
            const bmpHeaderEntry &h = headers[next];
            uint32_t hashcode;
            memcpy(&hashcode, paletteHeaders + next * 8 + 4, 4);
            int width = h.widthMinusOne + 1, height = h.heightMinusOne + 1;
            int frames = h.animFrames != 0 ? h.animFrames : 1;
            int colours = Ps2Texture_PaletteColours(b.size);
            bool flag8 = (h.unknown0 & 1) != 0;
            int headerWidth = width;
            bool fits = colours != 0 && Ps2Texture_FitSize(&width, &height, frames, colours, texelBytes);
            if (fits && width != headerWidth) {
                printf("  %s texture %d (0x%08x): %dx%d in its header, texels for %dx%d\n", pack.c_str(), next,
                       hashcode, headerWidth, h.heightMinusOne + 1, width, height);
                totals.resized++;
            }
            if (!fits || texels == NULL || (colours == 256) != flag8) {
                printf("  %s texture %d (0x%08x): %dx%d x%d, palette %u bytes, texels %u: does not fit\n",
                       pack.c_str(), next, hashcode, width, height, frames, b.size, texelBytes);
                totals.failed++;
            } else {
                Ps2TextureFormat format = Ps2Texture_ChooseFormat(b.data, colours, width, height);
                Bytes kxt(Ps2Texture_KxtBytes(width, height, frames, format));
                TextureInfo *tex = (TextureInfo *)kxt.data();
                Ps2Texture_BuildKxt(tex, hashcode, width, height, frames, h.someDivisor, texels, b.data, colours,
                                    format);
                totals.textures++;
                totals.frames += frames;
                totals.kxtBytes += kxt.size();
                totals.formats[format]++;
                (colours == 16 ? totals.colours16 : totals.colours256)++;
                // Frame 0 as the backend will see it, against the PS2's texels decoded straight
                const uint8_t *pixels = (const uint8_t *)(&tex->baseIdx + frames);
                std::vector<uint32_t> linear((size_t)width * height), direct((size_t)width * height);
                if (format == PS2TEX_A8R8G8B8)
                    Unswizzle((const uint32_t *)pixels, linear.data(), width, height);
                else
                    DecodeDxt(pixels, width, height, format == PS2TEX_DXT5, (uint8_t *)linear.data());
                Ps2Texture_DecodeFrame(texels, b.data, colours, width, height, (uint8_t *)direct.data());
                // (a clear texel's colour does not count)
                double error = 0;
                size_t counted = 0;
                for (size_t k = 0; k < direct.size(); k++) {
                    const uint8_t *x = (const uint8_t *)&linear[k], *y = (const uint8_t *)&direct[k];
                    for (int c = 0; c < 4; c++) {
                        if (c < 3 && y[3] == 0)
                            continue;
                        error += (double)(x[c] - y[c]) * (x[c] - y[c]);
                        counted++;
                    }
                }
                double psnr = error > 0 ? 10 * log10(255.0 * 255.0 * counted / error) : 99;
                totals.psnrSum += psnr;
                if (psnr < totals.psnrWorst) {
                    totals.psnrWorst = psnr;
                    totals.worstName = pack + "_" + std::to_string(next);
                }
                if (outDir != NULL) {
                    char name[512];
                    if (hashcode != 0xffffffff)
                        snprintf(name, sizeof(name), "%s/%s_%03d_%08x.png", outDir, pack.c_str(), next, hashcode);
                    else
                        snprintf(name, sizeof(name), "%s/%s_%03d.png", outDir, pack.c_str(), next);
                    WritePng(name, width, height, (const uint8_t *)linear.data());
                }
            }
            texels = NULL;
            next++;
        }
    }
    if (headers != NULL && next != count) {
        printf("  %s: %d texture headers, %d converted\n", pack.c_str(), count, next);
        totals.failed += count - next;
    }
}

int CmdTextures(Disc &disc, const std::string &hash, const char *outDir, bool quiet) {
    Bytes bundle;
    if (!ReadPs2Bundle(disc, hash, bundle))
        return 1;
    TextureTotals totals;
    for (const Entry &e : ReadBundle(bundle, PS2_SECTOR_SIZE))
        if (IsPack(e.type))
            ConvertPackTextures(ReadBlocks(e.data, e.size), e.name, outDir, totals);
    if (!quiet || totals.failed != 0)
        printf("[ps2tex] %s: %d textures (%d of 16 colours, %d of 256; %d A8R8G8B8, %d DXT1, %d DXT5), %d frames, %.1f MB "
               "as KXT, PSNR mean %.1f dB worst %.1f (%s), %d resized, %d failed\n", hash.c_str(), totals.textures,
               totals.colours16, totals.colours256, totals.formats[PS2TEX_A8R8G8B8], totals.formats[PS2TEX_DXT1],
               totals.formats[PS2TEX_DXT5], totals.frames, totals.kxtBytes / 1048576.0,
               totals.textures ? totals.psnrSum / totals.textures : 0.0, totals.psnrWorst, totals.worstName.c_str(),
               totals.resized, totals.failed);
    return totals.failed != 0;
}

// ---- meshes ----

#pragma pack(push, 1)
struct Vertex28 {
    float p[3];
    uint32_t normal;
    uint8_t colour[4];
    float uv[2];
};
struct Prim12 {
    uint16_t tex0, tex1;
    uint32_t count;
    uint16_t stateFlags;
    uint8_t changeMask, bonePalette;
};
#pragma pack(pop)

// A KXE read back as psiCreateEntityGfx and RecurseAndDrawBoxes read it
struct Kxe {
    const ModelData *header = NULL;
    const Vertex28 *vertices = NULL;
    const uint16_t *indices = NULL;
    const Prim12 *prims = NULL;
    int stride = 28;

    bool Parse(const uint8_t *data, size_t size) {
        header = (const ModelData *)data;
        if (size < offsetof(ModelData, data) || header->magicTag != MODELDATA_MAGIC)
            return false;
        stride = header->skinGroupCnt != 0 ? 32 : 28;
        const uint8_t *at = data + offsetof(ModelData, data);
        vertices = (const Vertex28 *)at;
        at += header->vtxCnt * stride + 3;
        indices = (const uint16_t *)at;     // unaligned on the disc: d3dCreateIndexBuffer moves it
        at += header->idxCnt * 2 + 3;
        prims = (const Prim12 *)at;
        at += header->primitiveCnt * 12;
        return at <= data + size;
    }

    const float *Position(int i) const { return (const float *)((const uint8_t *)vertices + i * stride); }
    uint32_t Normal(int i) const { uint32_t n; memcpy(&n, (const uint8_t *)vertices + i * stride + 12, 4); return n; }
    uint16_t Index(int i) const { uint16_t v; memcpy(&v, (const uint8_t *)indices + i * 2, 2); return v; }
};

void UnpackNormal(uint32_t n, float out[3]) {
    int x = (int)(n << 21) >> 21, y = (int)(n << 10) >> 21, z = (int)n >> 22;
    out[0] = x / 1023.0f; out[1] = y / 1023.0f; out[2] = z / 511.0f;
}

struct MeshCheck {
    int triangles = 0, agree = 0, disagree = 0, outward = 0, inward = 0;
    float boundMin[3] = { 1e30f, 1e30f, 1e30f }, boundMax[3] = { -1e30f, -1e30f, -1e30f };
};

// Each drawn triangle in strip order against its vertices' stored normals; and whether the normals point away from
// the mesh's centre
MeshCheck CheckKxe(const Kxe &k) {
    MeshCheck c;
    for (int i = 0; i < k.header->vtxCnt; i++)
        for (int a = 0; a < 3; a++) {
            c.boundMin[a] = fminf(c.boundMin[a], k.Position(i)[a]);
            c.boundMax[a] = fmaxf(c.boundMax[a], k.Position(i)[a]);
        }
    float centre[3];
    for (int a = 0; a < 3; a++)
        centre[a] = (c.boundMin[a] + c.boundMax[a]) / 2;
    for (int i = 0; i < k.header->vtxCnt; i++) {
        float n[3], d = 0;
        UnpackNormal(k.Normal(i), n);
        for (int a = 0; a < 3; a++)
            d += n[a] * (k.Position(i)[a] - centre[a]);
        (d > 0 ? c.outward : c.inward)++;
    }
    int start = 0;
    for (int p = 0; p < k.header->primitiveCnt; p++) {
        for (uint32_t t = 0; t < k.prims[p].count; t++) {
            int i0 = k.Index(start + t), i1 = k.Index(start + t + 1), i2 = k.Index(start + t + 2);
            if (t & 1) { int s = i0; i0 = i1; i1 = s; }
            if (i0 == i1 || i1 == i2 || i0 == i2)
                continue;
            const float *a = k.Position(i0), *b = k.Position(i1), *cc = k.Position(i2);
            float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { cc[0] - a[0], cc[1] - a[1], cc[2] - a[2] };
            float f[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
            float n0[3], n1[3], n2[3];
            UnpackNormal(k.Normal(i0), n0); UnpackNormal(k.Normal(i1), n1); UnpackNormal(k.Normal(i2), n2);
            float d = 0;
            for (int x = 0; x < 3; x++)
                d += f[x] * (n0[x] + n1[x] + n2[x]);
            c.triangles++;
            (d >= 0 ? c.agree : c.disagree)++;
        }
        start += k.prims[p].count + 2;
    }
    return c;
}

void WriteObj(const std::string &path, const Kxe &k) {
    FILE *f = fopen(path.c_str(), "w");
    if (f == NULL)
        return;
    for (int i = 0; i < k.header->vtxCnt; i++) {
        const Vertex28 *v = (const Vertex28 *)((const uint8_t *)k.vertices + i * k.stride);
        fprintf(f, "v %g %g %g\nvt %g %g\n", v->p[0], v->p[1], v->p[2], v->uv[0], 1.0f - v->uv[1]);
    }
    int start = 0;
    for (int p = 0; p < k.header->primitiveCnt; p++) {
        fprintf(f, "g prim%d_tex%d\n", p, k.prims[p].tex0);
        for (uint32_t t = 0; t < k.prims[p].count; t++) {
            int i0 = k.Index(start + t), i1 = k.Index(start + t + 1), i2 = k.Index(start + t + 2);
            if (t & 1) { int s = i0; i0 = i1; i1 = s; }
            if (i0 == i1 || i1 == i2 || i0 == i2)
                continue;
            fprintf(f, "f %d/%d %d/%d %d/%d\n", i0 + 1, i0 + 1, i1 + 1, i1 + 1, i2 + 1, i2 + 1);
        }
        start += k.prims[p].count + 2;
    }
    fclose(f);
}

// The entities of a bundle's packs by name: block 0x04's name, and the mesh block before it
struct NamedMesh {
    const uint8_t *data;
    uint32_t size;
    int id;
};

std::map<std::string, NamedMesh> Entities(const std::vector<Entry> &entries, int meshId) {
    std::map<std::string, NamedMesh> out;
    for (const Entry &e : entries) {
        if (!IsPack(e.type))
            continue;
        NamedMesh last = { NULL, 0, 0 };
        for (const Block &b : ReadBlocks(e.data, e.size)) {
            if (b.id == meshId)
                last = { b.data, b.size, b.id };
            if (b.id == 0x04 && last.data != NULL) {
                std::string name = (const char *)(b.data + 0x30);
                out.emplace(name, last);
                last.data = NULL;
            }
        }
    }
    return out;
}

struct MeshTotals {
    int meshes = 0, converted = 0, skinned = 0, empty = 0, emptyButXboxDraws = 0, unknownVif = 0, badBatches = 0;
    int otherPrims = 0;
    int batches = 0, strips = 0, fans = 0, triangles = 0, skipped = 0, primitives = 0, vertices = 0;
    MeshCheck check;
    int twins = 0, twinsBoundsMatch = 0;
    MeshCheck xboxCheck;
    int normalsSame = 0, normalsOpposite = 0, normalsUnmatched = 0;
    int statesCompared = 0, statesAgree[4] = {};
    int leafFlagDoubleSided[4] = {};    // [leaf flag 1 * 2 + the Xbox twin double-sided anywhere]
};

// Which states a mesh's primitives use anywhere: bit 0 additive, 1 subtractive, 2 alpha reference 180, 3 no z-write
uint32_t StatesUsed(const Kxe &k) {
    uint32_t used = 0;
    for (int p = 0; p < k.header->primitiveCnt; p++) {
        uint16_t s = k.prims[p].stateFlags;
        used |= (((s >> 1) & 3) == 1) | ((((s >> 1) & 3) == 2) << 1) | (((s >> 4) & 1) << 2) | ((((s >> 5) & 1) ^ 1) << 3);
    }
    return used;
}

// Each PS2 vertex's normal against the Xbox twin's vertex at the same place (within 1 mm)
void CompareNormals(const Kxe &ps2, const Kxe &xbox, MeshTotals &totals) {
    for (int i = 0; i < ps2.header->vtxCnt; i++) {
        const float *p = ps2.Position(i);
        int best = -1;
        float bestDistance = 1e-6f;
        for (int j = 0; j < xbox.header->vtxCnt; j++) {
            const float *q = xbox.Position(j);
            float d = (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
            if (d < bestDistance) {
                bestDistance = d;
                best = j;
            }
        }
        if (best < 0) {
            totals.normalsUnmatched++;
            continue;
        }
        float a[3], b[3];
        UnpackNormal(ps2.Normal(i), a);
        UnpackNormal(xbox.Normal(best), b);
        (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] >= 0 ? totals.normalsSame : totals.normalsOpposite)++;
    }
}

void AddCheck(MeshCheck &to, const MeshCheck &c) {
    to.triangles += c.triangles; to.agree += c.agree; to.disagree += c.disagree;
    to.outward += c.outward; to.inward += c.inward;
}

int CmdMeshes(Disc &disc, const std::string &hash, const char *xboxBundlePath, const char *objDir, bool quiet) {
    Bytes bundle;
    if (!ReadPs2Bundle(disc, hash, bundle))
        return 1;
    std::vector<Entry> entries = ReadBundle(bundle, PS2_SECTOR_SIZE);
    Bytes xboxBundle;
    std::map<std::string, NamedMesh> xboxEntities;
    if (xboxBundlePath != NULL) {
        if (!ReadWhole(xboxBundlePath, xboxBundle)) {
            printf("no %s\n", xboxBundlePath);
            return 1;
        }
        xboxEntities = Entities(ReadBundle(xboxBundle, 0x20), 0x0d);
    }

    MeshTotals totals;
    for (auto &named : Entities(entries, 0x2d)) {
        totals.meshes++;
        Bytes kxe;
        Ps2MeshStats s;
        bool ok = Ps2Mesh_ConvertToKxe(named.second.data, named.second.size, kxe, &s);
        totals.skinned += s.skinned;
        totals.unknownVif += s.unknownVif;
        totals.badBatches += s.badBatches;
        totals.otherPrims += s.otherPrims;
        if (!ok) {
            totals.empty += !s.skinned;
            auto twin = xboxEntities.find(named.first);
            Kxe x;
            if (!s.skinned && twin != xboxEntities.end() && x.Parse(twin->second.data, twin->second.size) &&
                x.header->vtxCnt != 0) {
                totals.emptyButXboxDraws++;
                if (!quiet)
                    printf("  %s: nothing to draw on the PS2 (%d boxes), %d vertices on the Xbox\n", named.first.c_str(),
                           s.boxes, x.header->vtxCnt);
            }
            continue;
        }
        totals.converted++;
        totals.batches += s.batches; totals.strips += s.strips; totals.fans += s.fans;
        totals.triangles += s.triangles; totals.skipped += s.skippedTriangles;
        totals.primitives += s.primitives; totals.vertices += s.vertices;
        Kxe k;
        if (!k.Parse(kxe.data(), kxe.size())) {
            printf("  %s: the KXE does not read back\n", named.first.c_str());
            totals.badBatches++;
            continue;
        }
        MeshCheck c = CheckKxe(k);
        AddCheck(totals.check, c);
        if (objDir != NULL) {
            std::string file = named.first;
            for (char &ch : file)
                if (ch == '/' || ch == '\\' || ch == ':' || ch == ' ')
                    ch = '_';
            WriteObj(std::string(objDir) + "/" + file + ".obj", k);
        }
        auto twin = xboxEntities.find(named.first);
        if (twin != xboxEntities.end()) {
            Kxe x;
            if (x.Parse(twin->second.data, twin->second.size) && x.header->vtxCnt != 0 && x.header->skinGroupCnt == 0) {
                MeshCheck xc = CheckKxe(x);
                totals.twins++;
                AddCheck(totals.xboxCheck, xc);
                bool match = true;
                for (int a = 0; a < 3; a++)
                    match &= fabsf(xc.boundMin[a] - c.boundMin[a]) < 0.05f && fabsf(xc.boundMax[a] - c.boundMax[a]) < 0.05f;
                totals.twinsBoundsMatch += match;
                if (match) {
                    uint32_t a = StatesUsed(k), b = StatesUsed(x);
                    for (int s = 0; s < 4; s++)
                        totals.statesAgree[s] += ((a >> s) & 1) == ((b >> s) & 1);
                    totals.statesCompared++;
                    bool xboxDoubleSided = false;
                    for (int p = 0; p < x.header->primitiveCnt; p++)
                        xboxDoubleSided |= (x.prims[p].stateFlags & 8) != 0;
                    totals.leafFlagDoubleSided[(s.leafFlags & 1) * 2 + xboxDoubleSided]++;
                }
                if (match && k.header->vtxCnt * x.header->vtxCnt < 4000000) {
                    int same = totals.normalsSame, opposite = totals.normalsOpposite;
                    CompareNormals(k, x, totals);
                    same = totals.normalsSame - same;
                    opposite = totals.normalsOpposite - opposite;
                    if (opposite > same && !quiet)
                        printf("  %s: normals against the Xbox's: %d the same way, %d opposite\n", named.first.c_str(),
                               same, opposite);
                }
                if (!match && !quiet)
                    printf("  %s: bounds PS2 (%g %g %g)-(%g %g %g), Xbox (%g %g %g)-(%g %g %g)\n", named.first.c_str(),
                           c.boundMin[0], c.boundMin[1], c.boundMin[2], c.boundMax[0], c.boundMax[1], c.boundMax[2],
                           xc.boundMin[0], xc.boundMin[1], xc.boundMin[2], xc.boundMax[0], xc.boundMax[1], xc.boundMax[2]);
            }
        }
    }
    printf("[ps2mesh] %s: %d meshes, %d converted, %d skinned (not converted), %d with nothing to draw; %d batches "
           "(%d strips, %d fans), %d triangles drawn, %d skipped, %d primitives, %d vertices; %d unread VIF chains, "
           "%d bad batches, %d other primitives\n",
           hash.c_str(), totals.meshes, totals.converted, totals.skinned, totals.empty, totals.batches, totals.strips,
           totals.fans, totals.triangles, totals.skipped, totals.primitives, totals.vertices, totals.unknownVif,
           totals.badBatches, totals.otherPrims);
    printf("[ps2mesh] %s: strip order against the stored normals: %d of %d agree; normals away from the centre: %d of %d\n",
           hash.c_str(), totals.check.agree, totals.check.triangles, totals.check.outward,
           totals.check.outward + totals.check.inward);
    if (xboxBundlePath != NULL)
        printf("[ps2mesh] %s: %d Xbox twins, %d with the same bounds (5 cm); the twins on the Xbox: %d of %d agree, "
               "%d of %d normals away from the centre\n",
               hash.c_str(), totals.twins, totals.twinsBoundsMatch, totals.xboxCheck.agree, totals.xboxCheck.triangles,
               totals.xboxCheck.outward, totals.xboxCheck.outward + totals.xboxCheck.inward);
    if (xboxBundlePath != NULL)
        printf("[ps2mesh] %s: PS2 normals against the twin's at the same place: %d the same way, %d opposite, %d with "
               "no vertex there; %d meshes with nothing to draw whose twin draws\n", hash.c_str(), totals.normalsSame,
               totals.normalsOpposite, totals.normalsUnmatched, totals.emptyButXboxDraws);
    if (xboxBundlePath != NULL)
        printf("[ps2mesh] %s: states used, PS2 against the twin, of %d twins: additive %d agree, subtractive %d, alpha "
               "reference 180 %d, no z-write %d\n", hash.c_str(), totals.statesCompared, totals.statesAgree[0],
               totals.statesAgree[1], totals.statesAgree[2], totals.statesAgree[3]);
    if (xboxBundlePath != NULL)
        printf("[ps2mesh] %s: leaf flag 1 against the twin double-sided: neither %d, Xbox only %d, PS2 only %d, both %d\n",
               hash.c_str(), totals.leafFlagDoubleSided[0], totals.leafFlagDoubleSided[1],
               totals.leafFlagDoubleSided[2], totals.leafFlagDoubleSided[3]);
    return totals.unknownVif != 0 || totals.badBatches != 0;
}

// ---- skinned meshes ----

// The entities of a bundle's packs by hashcode (block 0x04's first word), the mesh block before each
std::map<uint32_t, NamedMesh> EntitiesByHash(const std::vector<Entry> &entries, int meshId) {
    std::map<uint32_t, NamedMesh> out;
    for (const Entry &e : entries) {
        if (!IsPack(e.type))
            continue;
        NamedMesh last = { NULL, 0, 0 };
        for (const Block &b : ReadBlocks(e.data, e.size)) {
            if (b.id == meshId)
                last = { b.data, b.size, b.id };
            if (b.id == 0x04 && last.data != NULL) {
                uint32_t hashcode;
                memcpy(&hashcode, b.data, 4);
                out.emplace(hashcode, last);
                last.data = NULL;
            }
        }
    }
    return out;
}

// A skinned KXE vertex's bones and bone A's weight, through the mesh's first palette (spec-mesh.md 7.1)
struct SkinWeights {
    int boneA, boneB;
    float weightA;
};

// Two STs the same on a wrapped texture
bool WrappedNear(float a, float b) {
    float d = fabsf(a - b);
    return fabsf(d - roundf(d)) < 0.01f;
}

int SlotOfRegister(int r) {
    return r < 57 ? r / 3 : r <= 93 ? (r - 3) / 3 : (r - 33) / 3;
}

// Each vertex's palette: the one loaded when a primitive that draws it is drawn (bonePalette 0 keeps the last)
std::vector<int> VertexPalettes(const Kxe &k) {
    std::vector<int> out(k.header->vtxCnt, 0);
    int start = 0, palette = 0;
    for (int p = 0; p < k.header->primitiveCnt; p++) {
        if (k.prims[p].bonePalette != 0)
            palette = k.prims[p].bonePalette - 1;
        for (uint32_t i = 0; i < k.prims[p].count + 2; i++)
            out[k.Index(start + i)] = palette;
        start += k.prims[p].count + 2;
    }
    return out;
}

SkinWeights Weights(const Kxe &k, const std::vector<int> &palettes, int i) {
    uint32_t word;
    memcpy(&word, (const uint8_t *)k.vertices + i * k.stride + 28, 4);
    const uint8_t *palette = (const uint8_t *)k.prims + k.header->primitiveCnt * 12 + palettes[i] * 0x36;
    SkinWeights w;
    w.boneA = palette[SlotOfRegister(word & 0xff)];
    w.boneB = palette[SlotOfRegister((word >> 8) & 0xff)];
    w.weightA = (word >> 16) / 65535.0f;
    return w;
}

// A drawn triangle's corners (positions to the millimetre), in the order drawn
typedef std::array<std::array<int, 3>, 3> Corners;

std::array<int, 3> Mm(const float *p) {
    return { (int)lroundf(p[0] * 1000), (int)lroundf(p[1] * 1000), (int)lroundf(p[2] * 1000) };
}

std::vector<Corners> Triangles(const Kxe &k) {
    std::vector<Corners> out;
    int start = 0;
    for (int p = 0; p < k.header->primitiveCnt; p++) {
        for (uint32_t t = 0; t < k.prims[p].count; t++) {
            int i0 = k.Index(start + t), i1 = k.Index(start + t + 1), i2 = k.Index(start + t + 2);
            if (t & 1)
                std::swap(i0, i1);
            if (i0 == i1 || i1 == i2 || i0 == i2)
                continue;
            out.push_back({ Mm(k.Position(i0)), Mm(k.Position(i1)), Mm(k.Position(i2)) });
        }
        start += k.prims[p].count + 2;
    }
    return out;
}

// The same corners in either winding: sorted, and whether that took an odd permutation
Corners Sorted(const Corners &c, bool *odd) {
    Corners s = c;
    int swaps = 0;
    for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++)
            if (s[b] < s[a]) {
                std::swap(s[a], s[b]);
                swaps++;
            }
    *odd = (swaps & 1) != 0;
    return s;
}

struct SkinTotals {
    int meshes = 0, converted = 0, twins = 0, ps2Vertices = 0, matched = 0, bones = 0, weights = 0, normals = 0,
        sts = 0, ps2Triangles = 0, foundOnXbox = 0, sameWinding = 0, xboxTriangles = 0, foundOnPs2 = 0,
        badBatches = 0, unknownVif = 0, tooManyBones = 0;
};

int CmdSkins(Disc &disc, const std::string &hash, const char *xboxBundlePath) {
    Bytes bundle, xboxBundle;
    if (!ReadPs2Bundle(disc, hash, bundle))
        return 1;
    if (!ReadWhole(xboxBundlePath, xboxBundle)) {
        printf("no %s\n", xboxBundlePath);
        return 1;
    }
    std::map<uint32_t, NamedMesh> xbox = EntitiesByHash(ReadBundle(xboxBundle, 0x20), 0x0d);
    SkinTotals totals;
    for (auto &entity : EntitiesByHash(ReadBundle(bundle, PS2_SECTOR_SIZE), 0x2d)) {
        Bytes kxe;
        Ps2MeshStats st;
        bool ok = Ps2Mesh_ConvertToKxe(entity.second.data, entity.second.size, kxe, &st);
        if (!st.skinned)
            continue;
        totals.meshes++;
        totals.badBatches += st.badBatches;
        totals.unknownVif += st.unknownVif;
        if (!ok) {
            totals.tooManyBones += st.bones > 53;
            printf("  %08x: not converted (%d bones, %d bad batches%s)\n", entity.first, st.bones, st.badBatches,
                   st.unknownVif ? ", an unread chain" : "");
            continue;
        }
        totals.converted++;
        Kxe k;
        k.Parse(kxe.data(), kxe.size());
        auto twin = xbox.find(entity.first);
        Kxe x;
        if (twin == xbox.end() || !x.Parse(twin->second.data, twin->second.size) || x.header->skinGroupCnt == 0) {
            printf("  %08x: no skinned Xbox twin\n", entity.first);
            continue;
        }
        totals.twins++;
        // Vertices: each PS2 one against the Xbox's at the same place (to the millimetre); the best agreeing of those
        std::vector<int> ps2Palettes = VertexPalettes(k), xboxPalettes = VertexPalettes(x);
        std::multimap<std::array<int, 3>, int> xboxAt;
        for (int j = 0; j < x.header->vtxCnt; j++)
            xboxAt.emplace(Mm(x.Position(j)), j);
        int matched = 0, bones = 0, weights = 0, normals = 0, sts = 0;
        for (int i = 0; i < k.header->vtxCnt; i++) {
            auto range = xboxAt.equal_range(Mm(k.Position(i)));
            if (range.first == range.second)
                continue;
            matched++;
            SkinWeights a = Weights(k, ps2Palettes, i);
            float na[3];
            UnpackNormal(k.Normal(i), na);
            const float *sta = (const float *)((const uint8_t *)k.vertices + i * k.stride + 20);
            int best = -1;
            bool bestFlags[4] = {};
            for (auto it = range.first; it != range.second; ++it) {
                int j = it->second;
                SkinWeights b = Weights(x, xboxPalettes, j);
                float nb[3];
                UnpackNormal(x.Normal(j), nb);
                const float *stb = (const float *)((const uint8_t *)x.vertices + j * x.stride + 20);
                bool flags[4] = {
                    a.boneA == b.boneA && (b.weightA > 0.999f || a.boneB == b.boneB),
                    fabsf(a.weightA - b.weightA) < 0.01f,
                    na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2] > 0.9f,
                    WrappedNear(sta[0], stb[0]) && WrappedNear(sta[1], stb[1]),
                };
                int score = flags[0] + flags[1] + flags[2] + flags[3];
                if (score > best) {
                    best = score;
                    memcpy(bestFlags, flags, sizeof(flags));
                }
            }
            bones += bestFlags[0];
            weights += bestFlags[1];
            normals += bestFlags[2];
            sts += bestFlags[3];
        }
        // Triangles, and the winding of those both have
        std::map<Corners, bool> xboxTriangles;
        for (const Corners &c : Triangles(x)) {
            bool odd;
            xboxTriangles.emplace(Sorted(c, &odd), odd);
        }
        std::set<Corners> ps2Seen;
        int ps2Triangles = 0, found = 0, sameWinding = 0;
        for (const Corners &c : Triangles(k)) {
            bool odd;
            Corners key = Sorted(c, &odd);
            if (!ps2Seen.insert(key).second)
                continue;
            ps2Triangles++;
            auto t = xboxTriangles.find(key);
            if (t == xboxTriangles.end())
                continue;
            found++;
            sameWinding += t->second == odd;
        }
        int foundOnPs2 = 0;
        for (auto &t : xboxTriangles)
            foundOnPs2 += ps2Seen.count(t.first) != 0;
        printf("  %08x: %4d vertices (Xbox %4d), %4d at an Xbox vertex: bones %4d, weight %4d, normal %4d, ST %4d; "
               "%4d triangles (Xbox %4d): %4d on the Xbox (%4d wound the same), %4d of the Xbox's here\n", entity.first,
               k.header->vtxCnt, x.header->vtxCnt, matched, bones, weights, normals, sts, ps2Triangles,
               (int)xboxTriangles.size(), found, sameWinding, foundOnPs2);
        totals.ps2Vertices += k.header->vtxCnt;
        totals.matched += matched;
        totals.bones += bones;
        totals.weights += weights;
        totals.normals += normals;
        totals.sts += sts;
        totals.ps2Triangles += ps2Triangles;
        totals.foundOnXbox += found;
        totals.sameWinding += sameWinding;
        totals.xboxTriangles += (int)xboxTriangles.size();
        totals.foundOnPs2 += foundOnPs2;
    }
    printf("[ps2skin] %s: %d skinned meshes, %d converted (%d with too many bones), %d bad batches, %d unread chains; "
           "%d with an Xbox twin\n", hash.c_str(), totals.meshes, totals.converted, totals.tooManyBones,
           totals.badBatches, totals.unknownVif, totals.twins);
    printf("[ps2skin] %s: %d vertices, %d at an Xbox vertex: the same bones %d, weight %d, normal %d, ST %d\n",
           hash.c_str(), totals.ps2Vertices, totals.matched, totals.bones, totals.weights, totals.normals, totals.sts);
    printf("[ps2skin] %s: %d distinct triangles, %d on the Xbox (%d wound the same way); %d of the Xbox's %d here\n",
           hash.c_str(), totals.ps2Triangles, totals.foundOnXbox, totals.sameWinding, totals.foundOnPs2,
           totals.xboxTriangles);
    return totals.badBatches != 0 || totals.unknownVif != 0;
}

// The pause screen's film as FS_ps2.cpp finds it (the first level bundle with a "woman" entry), against the Xbox's
// woman.rle when given
int CmdWoman(Disc &disc, const char *xboxPath) {
    for (int i = 0; i < disc.count; i++) {
        if (disc.files[i].name[0] != '0' || disc.files[i].name[1] != '7')
            continue;
        Bytes bundle;
        if (!disc.Read(disc.files[i].name, bundle) || bundle.size() <= PS2_SECTOR_SIZE)
            continue;
        uint32_t dirSize, at, size;
        memcpy(&dirSize, bundle.data(), 4);
        if (!Ps2Disc_FindBundleEntry(bundle.data(), bundle.data() + PS2_SECTOR_SIZE, dirSize, PS2_BUNDLE_WOMAN_TYPE,
                                     PS2_BUNDLE_WOMAN_NAME, &at, &size))
            continue;
        printf("woman: %s, %u bytes at %u\n", disc.files[i].name, size, at);
        if (xboxPath == NULL)
            return 0;
        Bytes xbox;
        if (!ReadWhole(xboxPath, xbox)) {
            printf("no %s\n", xboxPath);
            return 1;
        }
        bool same = xbox.size() <= size && memcmp(xbox.data(), bundle.data() + at, xbox.size()) == 0;
        for (uint32_t k = (uint32_t)xbox.size(); same && k < size; k++)
            same = bundle[at + k] == 0;
        printf("against the Xbox's %zu bytes: %s\n", xbox.size(), same ? "the same, then zeros" : "DIFFERENT");
        return same ? 0 : 1;
    }
    printf("no level bundle has a woman entry\n");
    return 1;
}

void WriteWav(const std::string &path, const int16_t *pcm, size_t samples, uint32_t rate) {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == NULL)
        return;
    uint32_t dataBytes = (uint32_t)(samples * 2);
    struct {
        char riff[4]; uint32_t size; char wave[4]; char fmt[4]; uint32_t fmtSize; uint16_t format, channels;
        uint32_t rate, byteRate; uint16_t align, bits; char data[4]; uint32_t dataSize;
    } h = { {'R','I','F','F'}, 36 + dataBytes, {'W','A','V','E'}, {'f','m','t',' '}, 16, 1, 1, rate, rate * 2, 2, 16,
            {'d','a','t','a'}, dataBytes };
    fwrite(&h, sizeof(h), 1, f);
    fwrite(pcm, 2, samples, f);
    fclose(f);
}

// A sound bank's samples as Ps2_ConvertSoundBank converts them: the SPU decode (to WAV, with <out>), then the Xbox
// ADPCM made of it decoded again, against it
int CmdSound(Disc &disc, int bank, const char *outDir) {
    char base[300];
    snprintf(base, sizeof(base), "%s/PS2/ENGLISH/SB_%d/SB_%d", disc.root.c_str(), bank / 8, bank);
    Bytes shf, sbf;
    if (!ReadWhole(std::string(base) + ".SHF", shf) || !ReadWhole(std::string(base) + ".SBF", sbf)) {
        printf("no %s.SHF / .SBF\n", base);
        return 1;
    }
    uint32_t count;
    memcpy(&count, shf.data(), 4);
    double worstSnr = 1e9;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t h[9];
        memcpy(h, shf.data() + 4 + i * 36, sizeof(h));
        uint32_t offset = h[1], size = h[2], pitch = h[3];
        std::vector<int16_t> pcm(size / SPU_BLOCK_BYTES * SPU_BLOCK_SAMPLES + 1);
        long loopStart;
        size_t n = Ps2Spu_Decode(sbf.data() + offset, size, pcm.data(), &loopStart);
        Bytes xadpcm(Ps2Spu_XAdpcmBytes(n));
        Ps2Spu_EncodeXAdpcm(pcm.data(), n, xadpcm.data());
        std::vector<int16_t> back(XAdpcm_DecodedValueCount(xadpcm.size(), 1));
        XAdpcm_Decode(xadpcm.data(), xadpcm.size(), 1, back.data(), back.size());
        double signal = 0, noise = 0;
        for (size_t k = 0; k < n; k++) {
            signal += (double)pcm[k] * pcm[k];
            noise += ((double)pcm[k] - back[k]) * ((double)pcm[k] - back[k]);
        }
        double snr = noise > 0 ? 10 * log10(signal / noise) : 99;
        if (signal > 0 && snr < worstSnr)
            worstSnr = snr;
        printf("  sample %u: %u bytes, %zu samples, pitch %u = %u Hz, loop flag %u, loop start %ld, %zu bytes of Xbox "
               "ADPCM, %.1f dB\n", i, size, n, pitch, Ps2Spu_PitchToHz(pitch), h[0], loopStart, xadpcm.size(), snr);
        if (outDir != NULL) {
            char name[400];
            snprintf(name, sizeof(name), "%s/sb%d_%u.wav", outDir, bank, i);
            WriteWav(name, pcm.data(), n, Ps2Spu_PitchToHz(pitch));
            snprintf(name, sizeof(name), "%s/sb%d_%u_xbox.wav", outDir, bank, i);
            WriteWav(name, back.data(), n, Ps2Spu_PitchToHz(pitch));
        }
    }
    printf("[ps2sound] bank %d: %u samples, the Xbox ADPCM's worst signal to noise %.1f dB\n", bank, count, worstSnr);
    return 0;
}

bool ReadFromBytes(void *context, uint32_t offset, uint32_t bytes, uint8_t *out) {
    const Bytes *data = (const Bytes *)context;
    if (offset >= data->size())
        return false;
    if (bytes > data->size() - offset)
        bytes = (uint32_t)(data->size() - offset);
    memcpy(out, data->data() + offset, bytes);
    return true;
}

// A music track read as FS_ps2.cpp reads it for the music code (the Xbox's 0x4800-byte pieces), against the same
// seconds made in one piece: the pieces must join without a seam. Writes the first seconds as a stereo WAV.
int CmdMusic(Disc &disc, int track, const char *outWav) {
    char path[300];
    snprintf(path, sizeof(path), "%s/PS2/MUSIC/MFX_%d/MFX_%d.SSD", disc.root.c_str(), track / 16, track);
    Bytes ssd;
    if (!ReadWhole(path, ssd)) {
        printf("no %s\n", path);
        return 1;
    }
    uint32_t xboxBytes = Ps2Music_ToXboxBytes((uint32_t)ssd.size());
    const uint32_t piece = 0x4800, blockBytes = 72, span = piece * 16;    // the first 16 pieces, about 6 s
    Bytes pieces(span), whole(span);
    for (uint32_t at = 0; at < span; at += piece)
        Ps2Music_XboxBlocks(at / blockBytes, piece / blockBytes, (uint32_t)ssd.size(), ReadFromBytes, &ssd,
                            pieces.data() + at);
    Ps2Music_XboxBlocks(0, span / blockBytes, (uint32_t)ssd.size(), ReadFromBytes, &ssd, whole.data());
    std::vector<int16_t> a(XAdpcm_DecodedValueCount(span, 2)), b(a.size());
    XAdpcm_Decode(pieces.data(), span, 2, a.data(), a.size());
    XAdpcm_Decode(whole.data(), span, 2, b.data(), b.size());
    int worst = 0;
    double energy = 0, difference = 0;
    for (size_t k = 0; k < a.size(); k++) {
        worst = std::max(worst, abs(a[k] - b[k]));
        energy += (double)a[k] * a[k];
        difference += (double)(a[k] - b[k]) * (a[k] - b[k]);
    }
    printf("[ps2music] track %d: %zu PS2 bytes, %u as the Xbox's; the first %u bytes in pieces against in one: the "
           "music's RMS %.0f, the difference's %.1f (worst %d)\n", track, ssd.size(), xboxBytes, span,
           sqrt(energy / a.size()), sqrt(difference / a.size()), worst);
    if (outWav != NULL) {
        FILE *f = fopen(outWav, "wb");
        if (f != NULL) {
            uint32_t dataBytes = (uint32_t)(a.size() * 2);
            uint32_t h[11] = { 0x46464952, 36 + dataBytes, 0x45564157, 0x20746d66, 16, 0x00020001, 44100, 44100 * 4,
                               0x00100004, 0x61746164, dataBytes };
            fwrite(h, 4, 11, f);
            fwrite(a.data(), 2, a.size(), f);
            fclose(f);
        }
    }
    return 0;
}

int CmdAll(Disc &disc) {
    int failed = 0;
    for (int i = 0; i < disc.count; i++) {
        std::string name = disc.files[i].name;
        if (name.size() != 12 || name.compare(8, 4, ".bin") != 0 || name[0] != '0' || name[1] != '7')
            continue;
        std::string hash = name.substr(0, 8);
        failed |= CmdTextures(disc, hash, NULL, false);
        failed |= CmdMeshes(disc, hash, NULL, NULL, true);
    }
    return failed;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: ps2data_check <disc> files | bundle <hash> | textures <hash> [<out>] | "
               "meshes <hash> [<xbox bundle>] [--obj <out>] | skins <hash> <xbox bundle> | all | woman [<xbox woman.rle>] | sound <bank> [<out>] | "
               "music <track> [<wav>]\n");
        return 2;
    }
    Disc disc;
    if (!disc.Open(argv[1]))
        return 1;
    std::string cmd = argv[2];
    if (cmd == "files")
        return CmdFiles(disc);
    if (cmd == "bundle" && argc > 3)
        return CmdBundle(disc, argv[3]);
    if (cmd == "textures" && argc > 3)
        return CmdTextures(disc, argv[3], argc > 4 ? argv[4] : NULL, false);
    if (cmd == "meshes" && argc > 3) {
        const char *xbox = NULL, *obj = NULL;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--obj") == 0 && i + 1 < argc)
                obj = argv[++i];
            else
                xbox = argv[i];
        }
        return CmdMeshes(disc, argv[3], xbox, obj, false);
    }
    if (cmd == "skins" && argc > 4)
        return CmdSkins(disc, argv[3], argv[4]);
    if (cmd == "all")
        return CmdAll(disc);
    if (cmd == "sound" && argc > 3)
        return CmdSound(disc, atoi(argv[3]), argc > 4 ? argv[4] : NULL);
    if (cmd == "music" && argc > 3)
        return CmdMusic(disc, atoi(argv[3]), argc > 4 ? argv[4] : NULL);
    if (cmd == "woman")
        return CmdWoman(disc, argc > 3 ? argv[3] : NULL);
    printf("unknown command %s\n", cmd.c_str());
    return 2;
}
