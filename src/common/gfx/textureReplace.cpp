#include "textureReplace.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// Texture replacement.
//
// Every texture the game uploads is identified by a hash of what the game gave for it - its format, its size
// and the bytes of its top level, exactly as they sit in the game's memory - which is the same from run to
// run, level to level and engine build to engine build. A file textures/<hash>.dds beside the executable is
// used in its place: any size (the game addresses textures in normalised coordinates, and linear textures are
// scaled by the size in the game's own header, so a larger replacement simply has more detail), any mip chain
// the file carries, uncompressed 32-bit or DXT1/3/5.
//
// DumpTextures=1 writes each texture the game uses to textures_dump/<hash>.dds, the name its replacement
// would need, and a font's glyph boxes to <hash>.font.json beside it. tools/texture_replacement.py turns
// dumps into PNGs and PNGs into replacements, and renders a TrueType font into a font's boxes.
//
// With neither a textures folder with anything in it nor dumping, nothing is hashed: the upload path is as it
// was.
// ---------------------------------------------------------------------------------------------------------------

#define REPLACE_DIR "textures"
#define DUMP_DIR    "textures_dump"

static TextureReplaceLogFn g_log = NULL;
static bool g_replacing = false, g_dumping = false;

static bool DirectoryHasDds(const char *dir) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*.dds", dir);
    WIN32_FIND_DATAA found;
    HANDLE h = FindFirstFileA(pattern, &found);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    FindClose(h);
    return true;
}

void TextureReplace_Init(TextureReplaceLogFn log) {
    g_log = log;
    bool replacement = GetPrivateProfileIntA("Settings", "TextureReplacement", 1, ".\\settings.ini") != 0;
    g_replacing = replacement && DirectoryHasDds(REPLACE_DIR);
    g_dumping = GetPrivateProfileIntA("Settings", "DumpTextures", 0, ".\\settings.ini") != 0;
    if (g_dumping)
        CreateDirectoryA(DUMP_DIR, NULL);
    if (g_log != NULL && (g_replacing || g_dumping))
        g_log("[d3d9] textures: %s%s%s\n", g_replacing ? "replacing from " REPLACE_DIR "\\" : "",
              g_replacing && g_dumping ? ", " : "", g_dumping ? "dumping to " DUMP_DIR "\\ (DumpTextures=1)" : "");
}

bool TextureReplace_Active(void) { return g_replacing || g_dumping; }
bool TextureReplace_Dumping(void) { return g_dumping; }

uint64_t TextureReplace_Hash(uint32_t xboxFormat, uint32_t width, uint32_t height, const uint8_t *data, size_t bytes) {
    uint64_t h = 0xcbf29ce484222325ull;   // FNV-1a
    const uint32_t words[3] = { xboxFormat, width, height };
    const uint8_t *w = (const uint8_t *)words;
    for (size_t i = 0; i < sizeof(words); i++) { h ^= w[i]; h *= 0x100000001b3ull; }
    for (size_t i = 0; i < bytes; i++) { h ^= data[i]; h *= 0x100000001b3ull; }
    return h;
}

// ---------------------------------------------------------------------------------------------------------------
// DDS
// ---------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
struct DdsPixelFormat { uint32_t size, flags, fourCC, rgbBits, rMask, gMask, bMask, aMask; };
struct DdsHeader {
    uint32_t magic, size, flags, height, width, pitch, depth, mipCount, reserved[11];
    DdsPixelFormat format;
    uint32_t caps, caps2, caps3, caps4, reserved2;
};
#pragma pack(pop)
static_assert(sizeof(DdsHeader) == 128, "DDS header size");

#define DDS_MAGIC       0x20534444u   // "DDS "
#define DDPF_ALPHAPIXELS 0x1u
#define DDPF_FOURCC     0x4u
#define DDPF_RGB        0x40u

static void PathFor(char *out, size_t size, const char *dir, uint64_t hash, const char *ext) {
    snprintf(out, size, "%s\\%016llx%s", dir, (unsigned long long)hash, ext);
}

IDirect3DTexture9 *TextureReplace_Load(IDirect3DDevice9 *device, uint64_t hash) {
    if (!g_replacing || device == NULL)
        return NULL;
    char path[MAX_PATH];
    PathFor(path, sizeof(path), REPLACE_DIR, hash, ".dds");
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = fileSize > (long)sizeof(DdsHeader) ? (uint8_t *)malloc((size_t)fileSize) : NULL;
    bool read = file != NULL && fread(file, 1, (size_t)fileSize, f) == (size_t)fileSize;
    fclose(f);
    if (!read) { free(file); if (g_log) g_log("[d3d9] textures: could not read %s\n", path); return NULL; }

    const DdsHeader *hd = (const DdsHeader *)file;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    uint32_t blockBytes = 0;   // DXT: bytes per 4x4 block; otherwise 0
    if (hd->magic == DDS_MAGIC && hd->size == 124) {
        if (hd->format.flags & DDPF_FOURCC) {
            if (hd->format.fourCC == MAKEFOURCC('D', 'X', 'T', '1')) { format = D3DFMT_DXT1; blockBytes = 8; }
            if (hd->format.fourCC == MAKEFOURCC('D', 'X', 'T', '3')) { format = D3DFMT_DXT3; blockBytes = 16; }
            if (hd->format.fourCC == MAKEFOURCC('D', 'X', 'T', '5')) { format = D3DFMT_DXT5; blockBytes = 16; }
        } else if ((hd->format.flags & DDPF_RGB) && hd->format.rgbBits == 32 && hd->format.rMask == 0x00ff0000u &&
                   hd->format.gMask == 0x0000ff00u && hd->format.bMask == 0x000000ffu) {
            format = (hd->format.flags & DDPF_ALPHAPIXELS) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
        }
    }
    if (format == D3DFMT_UNKNOWN || hd->width == 0 || hd->height == 0) {
        if (g_log) g_log("[d3d9] textures: %s is not a DDS this reads (32-bit ARGB, DXT1/3/5)\n", path);
        free(file);
        return NULL;
    }

    uint32_t levels = hd->mipCount != 0 ? hd->mipCount : 1;
    IDirect3DTexture9 *texture = NULL;
    if (FAILED(device->CreateTexture(hd->width, hd->height, levels, 0, format, D3DPOOL_MANAGED, &texture, NULL))) {
        if (g_log) g_log("[d3d9] textures: could not create %ux%u for %s\n", hd->width, hd->height, path);
        free(file);
        return NULL;
    }
    const uint8_t *src = file + sizeof(DdsHeader), *end = file + fileSize;
    for (uint32_t level = 0; level < levels; level++) {
        uint32_t lw = hd->width >> level, lh = hd->height >> level;
        if (lw == 0) lw = 1;
        if (lh == 0) lh = 1;
        uint32_t rows = blockBytes ? (lh + 3) / 4 : lh;
        uint32_t rowBytes = blockBytes ? ((lw + 3) / 4) * blockBytes : lw * 4;
        if (src + (size_t)rows * rowBytes > end) {
            if (g_log) g_log("[d3d9] textures: %s is shorter than its header says\n", path);
            texture->Release();
            free(file);
            return NULL;
        }
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(texture->LockRect(level, &lr, NULL, 0))) {
            for (uint32_t r = 0; r < rows; r++)
                memcpy((uint8_t *)lr.pBits + (size_t)r * lr.Pitch, src + (size_t)r * rowBytes, rowBytes);
            texture->UnlockRect(level);
        }
        src += (size_t)rows * rowBytes;
    }
    if (g_log) g_log("[d3d9] textures: replaced %016llx with %s (%ux%u, %u levels)\n",
                     (unsigned long long)hash, path, hd->width, hd->height, levels);
    free(file);   // hd points into it
    return texture;
}

// Once per hash: a small open-addressed set, which stops dumping when it fills.
#define DUMPED_CAPACITY 8192
static uint64_t g_dumped[DUMPED_CAPACITY], g_fontsDumped[DUMPED_CAPACITY];

static bool FirstTime(uint64_t *set, uint64_t hash) {
    if (hash == 0) hash = 1;
    for (uint32_t i = 0, slot = (uint32_t)hash % DUMPED_CAPACITY; i < DUMPED_CAPACITY; i++, slot = (slot + 1) % DUMPED_CAPACITY) {
        if (set[slot] == hash) return false;
        if (set[slot] == 0) { set[slot] = hash; return true; }
    }
    return false;
}

void TextureReplace_Dump(uint64_t hash, uint32_t width, uint32_t height, const uint8_t *bgra) {
    if (!g_dumping || bgra == NULL || !FirstTime(g_dumped, hash))
        return;
    char path[MAX_PATH];
    PathFor(path, sizeof(path), DUMP_DIR, hash, ".dds");
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return;
    DdsHeader hd;
    memset(&hd, 0, sizeof(hd));
    hd.magic = DDS_MAGIC;
    hd.size = 124;
    hd.flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8;   // caps, height, width, pixel format, pitch
    hd.height = height;
    hd.width = width;
    hd.pitch = width * 4;
    hd.mipCount = 1;
    hd.format.size = 32;
    hd.format.flags = DDPF_RGB | DDPF_ALPHAPIXELS;
    hd.format.rgbBits = 32;
    hd.format.rMask = 0x00ff0000u; hd.format.gMask = 0x0000ff00u; hd.format.bMask = 0x000000ffu; hd.format.aMask = 0xff000000u;
    hd.caps = 0x1000;   // texture
    fwrite(&hd, sizeof(hd), 1, f);
    fwrite(bgra, 4, (size_t)width * height, f);
    fclose(f);
}

void TextureReplace_DumpFont(uint64_t hash, uint32_t width, uint32_t height, const D3D9FontGlyph *glyphs, int count) {
    if (!g_dumping || glyphs == NULL || count <= 0 || !FirstTime(g_fontsDumped, hash))
        return;
    char path[MAX_PATH];
    PathFor(path, sizeof(path), DUMP_DIR, hash, ".font.json");
    FILE *f = fopen(path, "w");
    if (f == NULL)
        return;
    fprintf(f, "{\n  \"texture\": \"%016llx\",\n  \"width\": %u,\n  \"height\": %u,\n  \"encoding\": \"nightfire\",\n  \"glyphs\": [\n",
            (unsigned long long)hash, width, height);
    for (int i = 0; i < count; i++)
        fprintf(f, "    {\"code\": %u, \"u\": %u, \"v\": %u, \"w\": %u, \"h\": %u, \"yOffset\": %d, \"advance\": %d}%s\n",
                glyphs[i].code, glyphs[i].u, glyphs[i].v, glyphs[i].w, glyphs[i].h, glyphs[i].yOffset, glyphs[i].advance,
                i + 1 < count ? "," : "");
    fprintf(f, "  ]\n}\n");
    fclose(f);
    if (g_log) g_log("[d3d9] textures: font metrics for %016llx dumped (%d glyphs)\n", (unsigned long long)hash, count);
}
