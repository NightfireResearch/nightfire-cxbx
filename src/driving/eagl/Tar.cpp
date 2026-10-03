#include "Tar.h"

#include "Loader.h"
#include "Realgraph.h"

#include <stdint.h>
#include <string.h>

#pragma float_control(precise, on)
#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// EAGL::TAR (docs/driving/eagl.md 2.6, 4.4): a texture binding. A TAR keeps the sampler state of one texture stage
// (address modes, filters, LOD bias, anisotropy, bump-env matrix), a D3D palette when its SHAPE has a clut, and a
// pointer to the shared image record (TARSharedData) that owns the D3D texture. TARs made from the same SHAPE share
// the record (reference counted); the record lives inside the SHAPE file when the file has room for it.
//
// Use binds a TAR: it sends only what changed against EAGL's per-stage caches, and writes D3D8's texture-stage table
// (0x00175428, 0x80 bytes a stage) and dirty flags (0x00175424) directly, as the inlined XDK code did - the backend
// reads them at draw time (5.2). Commit gives the image to D3D: see the comment above it about the in-place path.
//
// Everything runs in the original's order with the original's arguments; exception frames are left out (nothing
// throws). Calls to D3D8 go to the original entry points, which our seam replaces.
// ---------------------------------------------------------------------------------------------------------------

using EAGL::TAR;
using EAGL::TARExtension;
using EAGL::TARSharedData;

namespace {

inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}

inline uint8_t &U8(uint32_t address) {
    return *(uint8_t *)(uintptr_t)address;
}

inline float &F32(uint32_t address) {
    return *(float *)(uintptr_t)address;
}

inline uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

inline uint32_t Ptr(const void *p) {
    return (uint32_t)(uintptr_t)p;
}

// EAGL's allocator hooks
inline void *Malloc(uint32_t size, uint32_t name) {
    return (*(void *(**)(uint32_t, const char *))0x001caf68u)(size, (const char *)(uintptr_t)name);
}

inline void Free(void *p, uint32_t size) {
    (*(void (**)(void *, uint32_t))0x001caf6cu)(p, size);
}

// ---- D3D8 / XGRAPHICS / D3DX entry points (the seam's), by their original addresses

inline void D3DSetTexture(uint32_t stage, void *texture) {
    ((void (__stdcall *)(uint32_t, void *))0x00166830u)(stage, texture);
}

inline void D3DSetPalette(uint32_t stage, void *palette) {
    ((void (__stdcall *)(uint32_t, void *))0x001669e0u)(stage, palette);
}

inline void D3DSetYuvEnable(uint32_t enable) {
    ((void (__stdcall *)(uint32_t))0x00168980u)(enable);
}

inline void D3DSetBumpEnv(uint32_t stage, uint32_t type, uint32_t value) {
    ((void (__stdcall *)(uint32_t, uint32_t, uint32_t))0x00167d50u)(stage, type, value);
}

inline void *D3DCreatePalette2(uint32_t size) {
    return ((void *(__stdcall *)(uint32_t))0x0016b510u)(size);
}

inline uint32_t *D3DPaletteLock2(void *palette, uint32_t flags) {
    return ((uint32_t *(__stdcall *)(void *, uint32_t))0x0016b570u)(palette, flags);
}

inline void D3DBlockUntilNotBusy(void *resource) {
    ((void (__stdcall *)(void *))0x001693d0u)(resource);
}

inline void D3DRelease(void *resource) {
    ((uint32_t (__stdcall *)(void *))0x00169230u)(resource);
}

inline void D3DRegister(void *resource, void *base) {
    ((void (__stdcall *)(void *, void *))0x001693a0u)(resource, base);
}

inline void *D3DGetSurfaceLevel2(void *texture, uint32_t level) {
    return ((void *(__stdcall *)(void *, uint32_t))0x00167330u)(texture, level);
}

inline void D3DLockRect(void *surface, uint32_t *locked, const void *rect, uint32_t flags) {
    ((uint32_t (__stdcall *)(void *, uint32_t *, const void *, uint32_t))0x00167240u)(surface, locked, rect, flags);
}

inline void D3DGetDesc(void *surface, uint32_t *desc) {
    ((uint32_t (__stdcall *)(void *, uint32_t *))0x00167220u)(surface, desc);
}

inline void D3DGet2DSurfaceDesc(void *surface, uint32_t level, uint32_t *desc) {
    ((void (__stdcall *)(void *, uint32_t, uint32_t *))0x00167320u)(surface, level, desc);
}

inline uint32_t *D3DCreateTexture2(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, uint32_t usage,
                                   uint32_t format, uint32_t pool) {
    return ((uint32_t *(__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))
                0x00167260u)(width, height, depth, levels, usage, format, pool);
}

inline uint32_t *D3DCreateStandAloneSurface(uint32_t width, uint32_t height, uint32_t a, uint32_t format) {
    return ((uint32_t *(__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t))0x00167100u)(width, height, a, format);
}

inline void XGSetTextureHeader(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage, uint32_t format,
                               uint32_t pool, void *texture, uint32_t data, uint32_t pitch) {
    ((void (__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, void *, uint32_t, uint32_t))
         0x0017a8acu)(width, height, levels, usage, format, pool, texture, data, pitch);
}

inline int32_t XGBytesPerPixelFromFormat(uint32_t format) {
    return ((int32_t (__stdcall *)(uint32_t))0x00178fb8u)(format);
}

inline int32_t D3DXLoadSurfaceFromMemory(void *surface, const void *destPalette, const int32_t *destRect,
                                         const void *source, uint32_t format, uint32_t pitch,
                                         const void *sourcePalette, const int32_t *sourceRect, uint32_t filter,
                                         uint32_t colourKey) {
    return ((int32_t (__stdcall *)(void *, const void *, const int32_t *, const void *, uint32_t, uint32_t,
                                   const void *, const int32_t *, uint32_t, uint32_t))0x0015d3bdu)(
        surface, destPalette, destRect, source, format, pitch, sourcePalette, sourceRect, filter, colourKey);
}

inline void MmFreeContiguousMemory(void *p) {
    ((void (__stdcall *)(void *))0x0010e82cu)(p);
}

inline void *DeviceGet() {
    return ((void *(*)())0x000e8a40u)();   // EAGL::Device::Get (not ours yet)
}

#define PrintMessage ((int (*)(int level, const char *format, ...))0x000f42b0u)
#define GlobalPool   ((SymbolPool *)0x0023fb8cu)

// ---- EAGL's state

const uint32_t kStageTexture = 0x0023ff80;    // the texture bound per stage
const uint32_t kStagePalette = 0x0023ffa0;    // the palette bound per stage
const uint32_t kCacheLodBias = 0x0023ffc0;    // float per stage
const uint32_t kCacheYuv = 0x0023ffe0;        // byte
const uint32_t kCacheBumpEnv = 0x0023ffe8;    // four floats per stage
const uint32_t kLodBiasOverride = 0x0023ff0c; // float; 0 = each TAR's own
const uint32_t kFilterOverride = 0x001cb938;  // -1 = each TAR's own
const uint32_t kCacheAddressU = 0x001ccb8c, kCacheAddressV = 0x001ccbac, kCacheAddressW = 0x001ccbcc,
               kCacheFilter = 0x001ccbec, kCacheMipFilter = 0x001ccc0c, kCacheAnisotropy = 0x001ccc2c;
const uint32_t kTextureEnable = 0x001cd1b0;   // GeoPrimState::Apply's cache of the texture enable
const uint32_t kRegistered = 0x00240814;      // "a resource was registered": opcode 15 flushes the cache
const uint32_t kZero = 0x00189dec;            // 0.0f

// D3D8's own tables
const uint32_t kDirty = 0x00175424;
const uint32_t kStageState = 0x00175428;      // 0x80 bytes a stage: ADDRESSU, V, W, MAG, MIN, MIP, LODBIAS, -, ANISO

inline uint8_t *DefaultShape() {
    return (uint8_t *)(uintptr_t)(U32(0x001cbde4) + 0x001cbdd0u);   // the first image of EAGL's built-in SHPX
}

// The inlined D3DDevice_SetTextureStageState: the stage's dirty bit, then the value into D3D8's table.
inline void StageState(uint32_t stage, uint32_t slot, uint32_t value) {
    U32(kDirty) |= 1u << (stage & 31);
    U32(kStageState + (stage << 7) + slot * 4) = value;
}

// One cached stage state: sent when it differs from the cache.
inline void CachedStageState(uint32_t stage, uint32_t cache, uint32_t slot, uint32_t value) {
    if (U32(cache + stage * 4) != value) {
        U32(cache + stage * 4) = value;
        StageState(stage, slot, value);
    }
}

// What FLD then FSTP does to a float's bits: a signalling NaN comes out quiet.
inline uint32_t ThroughX87(uint32_t bits) {
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0)
        bits |= 0x00400000u;
    return bits;
}

inline int32_t MipLevels(uint32_t bits) {
    return (int32_t)(bits << 26) >> 26;
}

// A SHAPE image's pixels: at an offset when its flag 0x1000 says so, else straight after the 0x10-byte header.
inline uint8_t *ImageData(uint8_t *image) {
    if (*(uint32_t *)(image + 0xc) & 0x1000)
        return image + *(int32_t *)(image + 0x10);
    return image + 0x10;
}

// The clut's 256 colours into the palette, a byte at a time as the original reads them.
inline void CopyClut(uint32_t *palette, const uint8_t *colours) {
    for (int i = 0; i < 256; i++, colours += 4)
        palette[i] = (uint32_t)colours[0] | (uint32_t)colours[1] << 8 | (uint32_t)colours[2] << 16 |
                     (uint32_t)colours[3] << 24;
}

// The palette made if there is none yet, the clut image's colours copied into it (Create's order).
inline void LoadClut(TAR *t, uint8_t *clutImage) {
    if (t->palette == NULL)
        t->palette = D3DCreatePalette2(0);
    uint8_t *colours = ImageData(clutImage);
    t->clut = colours;
    uint32_t *p = D3DPaletteLock2(t->palette, 0);
    CopyClut(p, colours);
}

// The shared record let go when the last TAR leaves it: freed too when EAGL allocated it.
inline void ReleaseShared(TARSharedData *d) {
    if (d->bits & 0x800000) {
        if (d != NULL) {
            d->Release();
            Free(d, 0x34);
        }
    } else {
        d->Release();
    }
}

// A new record when the SHAPE has none of its own (TARSharedData::Init inlined).
inline TARSharedData *NewShared() {
    TARSharedData *d = (TARSharedData *)Malloc(0x34, 0x001ccc70u);   // "EAGL::TARPrivate::SharedData new"
    if (d != NULL)
        d->Init();
    return d;
}

// "shape_" and four characters, as the loader names a SHAPE.
inline void ShapeSymbol(char *name, uint32_t prefix, const uint8_t *four) {
    memcpy(name, (const char *)(uintptr_t)prefix, 6);
    name[6] = (char)four[0];
    name[7] = (char)four[1];
    name[8] = (char)four[2];
    name[9] = (char)four[3];
    name[10] = 0;
}

inline void *FindShape(const char *name, DynamicLoader *loader) {
    bool found;
    void *shape = GlobalPool->Search(name, &found);
    if (!found) {
        void *out = NULL;
        loader->GetAddr((const char *)0x001a09fcu, name, &out);   // "SHAPE"
        shape = out;
    }
    return shape;
}

}  // namespace

// ---- the image record

// The D3D format of a SHAPE image (by its type byte, swizzled or linear by its flag 0x2000), or 0. A palettised
// image (0x1a) whose info flags have bit 1 becomes 0x28.
// FUNC_AT(0x000eb070)
uint32_t EAGL_TextureFormatFromShape(const uint8_t *shape) {
    if (shape == NULL)
        return 0;
    int info = SHAPE_infoflags(shape);
    uint32_t type = (*(const uint32_t *)shape & 0xff) - 0x60;
    uint32_t format = 0;
    if (type <= 0x1e) {
        bool linear = (*(const uint32_t *)(shape + 0xc) & 0x2000) != 0;
        switch (type) {
            case 0x00: format = 0xc; break;
            case 0x01: format = 0xe; break;
            case 0x02: format = 0xf; break;
            case 0x04: format = linear ? 0x19 : 0x1f; break;
            case 0x05: format = linear ? 0x1a : 0x20; break;
            case 0x08: format = 0x24; break;
            case 0x0d: format = linear ? 0x04 : 0x1d; break;
            case 0x18: format = linear ? 0x05 : 0x11; break;
            case 0x1b: format = 0xb; break;
            case 0x1d: format = linear ? 0x06 : 0x12; break;
            case 0x1e: format = linear ? 0x02 : 0x10; break;
            default: format = 0; break;
        }
    }
    if ((info & 2) && format == 0x1a)
        format = 0x28;
    return format;
}

// The image's clut attachment ('*'), or NULL.
// FUNC_AT(0x000eb200)
uint8_t* EAGL_FindClut(uint8_t *shape) {
    uint8_t *p = shape;
    if (p == NULL)
        return NULL;
    for (;;) {
        int32_t d = *(int32_t *)p;
        if ((uint8_t)d == 0x2a)
            return p;
        d >>= 8;
        if (d == 0)
            return NULL;
        p += d;
        if (p == NULL)
            return NULL;
    }
}

// FUNC_AT(0x000eb840)
TARSharedData* EAGL::TARSharedData::Init() {
    bits = (bits & 0xff7fffffu) | 0x400000u;
    refCount = 0;
    width = 0;
    height = 0;
    shape = NULL;
    contiguous = NULL;
    texture = NULL;
    flags &= ~1u;
    yuv = 0;
    return this;
}

// FUNC_AT(0x000eb880)
void EAGL::TARSharedData::Release() {
    magic[0] = 'D';
    magic[1] = 'G';
    magic[2] = 'K';
    magic[3] = '!';
    pixels = NULL;
    if (texture == NULL)
        return;
    if (texture[0] & 0x1000000) {   // made by D3D (render targets, surfaces): D3D releases it
        D3DRelease(texture);
        texture = NULL;
        return;
    }
    if (contiguous != NULL) {
        MmFreeContiguousMemory(contiguous);
        contiguous = NULL;
    }
    if (texture != NULL) {
        texture[0] = 0;
        texture[1] = 0;
        texture[2] = 0;
        texture[3] = 0;
        texture[4] = 0;
        Free(texture, 0x14);
        texture = NULL;
    }
}

// The record for a SHAPE image: in its 'i' attachment ("USED", set up the first time), else in an "EAGL" block of
// its long name (at the next dword after the four letters, "EAGL" turned into "EAGl" the first time; only when at
// least 0x40 characters are left), else NULL.
// FUNC_AT(0x000ebe00)
TARSharedData* EAGL_FindSharedData(uint8_t *shape) {
    uint8_t *info = SHAPE_infodata(shape);
    if (info != NULL) {
        if (info[0] == 'U' && info[1] == 'S' && info[2] == 'E' && info[3] == 'D')
            return (TARSharedData *)info;
        info[3] = 'D';
        ((TARSharedData *)info)->Init();
        info[0] = 'U';
        info[1] = 'S';
        info[2] = 'E';
        return (TARSharedData *)info;
    }
    uint8_t *name = SHAPE_namedata(shape);
    if (name == NULL)
        return NULL;
    int32_t length = (int32_t)strlen((const char *)name);
    for (int32_t i = 0; i < length; i++, name++) {
        if (name[0] != 'E' || name[1] != 'A' || name[2] != 'G')
            continue;
        TARSharedData *d = (TARSharedData *)(((uintptr_t)name + 7) & ~(uintptr_t)3);
        if (name[3] == 'L') {
            if (length - i < 0x40)
                return NULL;
            name[3] = 'l';
            if (d != NULL)
                d->Init();
            return d;
        }
        if (name[3] == 'l')
            return d;
    }
    return NULL;
}

// ---- TAR

// FUNC_AT(0x000eb7a0)
TAR* EAGL::TAR::InitFields() {
    address0 = 1;
    addressU = 1;
    addressV = 1;
    addressW = 1;
    filter = 2;
    mipFilter = 2;
    stage = 0;
    *(uint32_t *)&lodBias = 0;
    committed = 0;
    maxAnisotropy = 4;
    palette = NULL;
    clut = NULL;
    data = NULL;
    *(uint32_t *)&bumpEnv[0] = 0x3f800000;
    *(uint32_t *)&bumpEnv[1] = 0;
    *(uint32_t *)&bumpEnv[2] = 0;
    *(uint32_t *)&bumpEnv[3] = 0x3f800000;
    return this;
}

// FUNC_AT(0x000eca20)
TAR* EAGL::TAR::Construct(uint8_t *shape) {
    InitFields();
    extension = this;
    Create(shape);
    return this;
}

// A TAR with a record of its own (render targets and surfaces).
// FUNC_AT(0x000ebee0)
TAR* EAGL::TAR::ConstructShared() {
    InitFields();
    extension = this;
    data = NewShared();
    data->bits |= 0x800000;   // a failed allocation faults here, as in the original
    data->flags &= ~1u;
    data->refCount++;
    data->shape = NULL;
    data->width = 0;
    data->height = 0;
    data->bits &= 0xffffffc0u;
    data->bits &= 0xffffc03fu;
    palette = NULL;
    clut = NULL;
    data->pixels = NULL;
    data->texture = NULL;
    return this;
}

// The other's 0x48 bytes over the defaults (this one's extension pointer kept), one more reference on the record.
// FUNC_AT(0x000ec3b0)
TAR* EAGL::TAR::ConstructCopy(const TAR *other) {
    InitFields();
    extension = this;
    memcpy(this, other, 0x48);
    data->refCount++;
    return this;
}

// FUNC_AT(0x000ecab0)
void EAGL::TAR::Destruct() {
    data->refCount--;
    if (data->refCount == 0) {
        for (uint32_t s = 0; s < 4; s++) {
            if (U32(kStageTexture + s * 4) == Ptr(data->texture)) {
                D3DSetTexture(s, NULL);
                U32(kStageTexture + s * 4) = 0;
            }
        }
        if (data->texture != NULL)
            D3DBlockUntilNotBusy(data->texture);
        if (data->flags & 1)
            Free(data->shape, 0x14);   // the render target's SHAPE header
        ReleaseShared(data);
    }
    if (palette != NULL) {
        if (U32(kStagePalette + stage * 4) == Ptr(palette)) {
            D3DSetPalette(stage, NULL);
            U32(kStagePalette + stage * 4) = 0;
        }
        D3DBlockUntilNotBusy(palette);
    }
    if (palette != NULL) {
        D3DRelease(palette);
        palette = NULL;
    }
}

// FUNC_AT(0x000eb220)
bool EAGL::TAR::SwapClut(uint8_t *clutShape) {
    uint8_t *p = clutShape;
    if (p == NULL)
        return false;
    for (;;) {
        int32_t d = *(int32_t *)p;
        if ((uint8_t)d == 0x2a)
            break;
        d >>= 8;
        if (d == 0)
            return false;
        p += d;
        if (p == NULL)
            return false;
    }
    uint8_t *colours = ImageData(p);
    clut = colours;
    if (colours == NULL)
        return false;
    if (palette == NULL)
        palette = D3DCreatePalette2(0);
    uint32_t *pal = D3DPaletteLock2(palette, 0);
    CopyClut(pal, colours);
    return true;
}

// FUNC_AT(0x000ecbc0)
bool EAGL::TAR::SwapShape(uint8_t *shape) {
    ((TARExtension *)&extension)->SwapShape(shape);
    return true;
}

// FUNC_AT(0x000eb790)
uint8_t* EAGL::TAR::GetShape() const {
    return data->shape;
}

// FUNC_AT(0x000eb390)
int32_t EAGL::TAR::GetRefCount() const {
    return data->refCount;
}

// Binds the TAR to its stage: YUV, texture (only while GeoPrimState says texturing is on), palette, then each
// sampler state that differs from EAGL's cache - written into D3D8's texture-stage table directly - and the
// bump-env matrix through D3D8. The two globals at 0x001cb938 (filter) and 0x0023ff0c (LOD bias) override every
// TAR's own when set.
// FUNC_AT(0x000eb3f0)
void EAGL::TAR::Use() {
    uint8_t yuv = data->yuv;
    if (U8(kCacheYuv) != yuv) {
        U8(kCacheYuv) = yuv;
        D3DSetYuvEnable(yuv);
    }
    if (U8(kTextureEnable) != 0) {
        if (U32(kStageTexture + stage * 4) != Ptr(data->texture)) {
            U32(kStageTexture + stage * 4) = Ptr(data->texture);
            D3DSetTexture(stage, data->texture);
        }
    }
    if (palette != NULL && U32(kStagePalette + stage * 4) != Ptr(palette)) {
        U32(kStagePalette + stage * 4) = Ptr(palette);
        D3DSetPalette(stage, palette);
    }
    CachedStageState(stage, kCacheAddressU, 0, addressU);
    CachedStageState(stage, kCacheAddressV, 1, addressV);
    CachedStageState(stage, kCacheAddressW, 2, addressW);
    uint32_t override = U32(kFilterOverride);
    uint32_t f = override == 0xffffffffu ? filter : override;
    if (U32(kCacheFilter + stage * 4) != f) {
        U32(kCacheFilter + stage * 4) = f;
        StageState(stage, 3, f);   // MAGFILTER
        StageState(stage, 4, f);   // MINFILTER
    }
    CachedStageState(stage, kCacheAnisotropy, 8, maxAnisotropy);

    // FCOMP + TEST AH,0x44: a component counts as changed unless it compares equal (NaN: changed).
    uint32_t b = kCacheBumpEnv + (stage << 4);
    if (!(F32(b) == bumpEnv[0]) || !(F32(b + 4) == bumpEnv[1]) || !(F32(b + 8) == bumpEnv[2]) ||
        !(F32(b + 12) == bumpEnv[3])) {
        U32(b) = Bits(bumpEnv[0]);
        U32(kCacheBumpEnv + (stage << 4) + 4) = Bits(bumpEnv[1]);
        U32(kCacheBumpEnv + (stage << 4) + 8) = Bits(bumpEnv[2]);
        U32(kCacheBumpEnv + (stage << 4) + 12) = Bits(bumpEnv[3]);
        D3DSetBumpEnv(stage, 0x16, Bits(bumpEnv[0]));   // BUMPENVMAT00
        D3DSetBumpEnv(stage, 0x17, Bits(bumpEnv[1]));   // BUMPENVMAT01
        D3DSetBumpEnv(stage, 0x19, Bits(bumpEnv[2]));   // BUMPENVMAT10
        D3DSetBumpEnv(stage, 0x18, Bits(bumpEnv[3]));   // BUMPENVMAT11
    }

    if (!(F32(kLodBiasOverride) == F32(kZero))) {   // an override (or NaN)
        if (!(F32(kCacheLodBias + stage * 4) == F32(kLodBiasOverride))) {
            U32(kCacheLodBias + stage * 4) = ThroughX87(U32(kLodBiasOverride));   // FLD / FSTP
            StageState(stage, 6, U32(kLodBiasOverride));                           // the raw bits (MOV)
        }
    } else if (!(F32(kCacheLodBias + stage * 4) == lodBias)) {
        U32(kCacheLodBias + stage * 4) = Bits(lodBias);
        StageState(stage, 6, Bits(lodBias));
    }
    CachedStageState(stage, kCacheMipFilter, 5, mipFilter);
}

// FUNC_AT(0x000eb770)
bool EAGL::TAR::ReturnsFalse1() {
    return false;
}

// FUNC_AT(0x000eb780)
bool EAGL::TAR::ReturnsFalse2() {
    return false;
}

// FUNC_AT(0x000eb7f0)
void EAGL::TAR::ReleasePalette() {
    if (palette != NULL) {
        if (U32(kStagePalette + stage * 4) == Ptr(palette)) {
            D3DSetPalette(stage, NULL);
            U32(kStagePalette + stage * 4) = 0;
        }
        D3DBlockUntilNotBusy(palette);
    }
    if (palette != NULL) {
        D3DRelease(palette);
        palette = NULL;
    }
}

// FUNC_AT(0x000eb900)
void EAGL::TAR::DumpState() {
}

// Copies the atlas's images into each mip level of the texture with D3DXLoadSurfaceFromMemory. No caller. Kept as
// the original has it: each record's image is only tested for pixels - the source is the TAR's own pixels, advanced
// by each level's height times the *destination's* pitch - the source rectangle is the whole level and the
// destination the record's (x, y, x + width, y + height), the surface is never unlocked, and a failed load releases
// the surface once more.
// FUNC_AT(0x000eb910)
void EAGL::TAR::LoadAtlas() {
    data->atlas = atlas;
    uint8_t *cursor = (uint8_t *)atlas + 8;
    while (*(uint8_t **)cursor != NULL) {
        uint8_t *image = *(uint8_t **)cursor;
        uint8_t *pixels = ImageData(image);
        int32_t w = *(int16_t *)(image + 4);
        int32_t h = *(int16_t *)(image + 6);
        int32_t x = *(uint16_t *)(cursor - 4);
        int32_t y = *(uint16_t *)(cursor - 2);
        uint32_t offset = 0;
        if (pixels != NULL) {
            DeviceGet();
            int32_t right = x + w;
            int32_t bottom = y + h;
            int32_t level = 0;
            do {
                void *surface = D3DGetSurfaceLevel2(data->texture, (uint32_t)level);
                uint32_t locked[2];   // pitch, bits
                D3DLockRect(surface, locked, NULL, 0x80);
                uint32_t pitch = locked[0];
                uint32_t desc[7];     // D3DSURFACE_DESC: format, type, usage, size, multisample, width, height
                D3DGetDesc(surface, desc);
                int32_t sourceRect[4] = { 0, 0, (int32_t)desc[5], (int32_t)desc[6] };
                int32_t destRect[4] = { x, y, right, bottom };
                if (D3DXLoadSurfaceFromMemory(surface, NULL, destRect, data->pixels + offset, data->format, pitch,
                                              NULL, sourceRect, 1, 0) < 0)
                    D3DRelease(surface);
                offset += desc[6] * pitch;
                D3DRelease(surface);
                level++;
            } while (level < MipLevels(data->bits));
        }
        cursor += 0xc;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The commit: the texture header made (or the bound one unbound and waited for), sampler fix-ups from the image,
// and the header set over the pixels and registered.
//
// The original has two ways of giving the pixels to D3D, chosen by whether the record's pixel-pointer field
// (record + 0x20 - the field's own address, not the pointer in it) lies in the physical-memory alias
// 0x80000000..0x8FFFFFFF: in place (the header built over the pixels where they are, the GPU reading whatever the
// CPU writes there later), or a copy (contiguous memory from XPhysicalAlloc, zeroed, the pixels copied into it once,
// registered instead; the previous copy freed first). Under this loader nothing lives at 0x80000000, so the test
// always chose the copy - and the pause menu's girl (GGirl::InitGirl 0x000d7990 makes two 128x128 textures,
// GGirl::DoGirl 0x000d7a50 decodes its run-length stream into their pixels thirty times a second and never commits
// again) showed a frozen copy of uninitialised memory. The D3D seam used to patch the two branches at
// 0x000ebbec/0x000ebbf4 to NOPs; this port does what that patched code did: the in-place path for every texture.
// That is what the console does for every texture EAGL keeps in physical memory, and the backend can read any
// memory. The copy path (and the failure return it had, when XPhysicalAlloc gave nothing) is gone and `contiguous`
// is never set; the size it would have allocated is still worked out, so the arithmetic follows the listing. The
// right answer remains a real alias (a reservation at 0x80000000, docs/driving-engine-plan.md section 0). The one
// risk: a texture whose pixels the game frees after committing would draw from freed memory; none has been seen.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000eba80)
uint32_t EAGL::TAR::Commit() {
    int32_t bpp = XGBytesPerPixelFromFormat(data->format);
    TARSharedData *d = data;
    uint32_t format = d->format;
    if (format == 0xc || format == 0xe || format == 0xf || format == 0x24) {
        switch (format) {
            case 0x0c: bpp = 8; break;          // DXT1: bytes per 4x4 block
            case 0x0e: case 0x0f: bpp = 16; break;   // DXT3, DXT5
            case 0x24: bpp = bpp / 2; break;    // YUY2
            default: break;
        }
    }
    if (format == 0x24)
        d->yuv = 1;
    if (data->texture == NULL) {
        data->texture = (uint32_t *)Malloc(0x14, 0x001ccc4cu);   // "D3DTexture"
    } else {
        if (U32(kStageTexture + stage * 4) == Ptr(data->texture)) {
            D3DSetTexture(stage, NULL);
            U32(kStageTexture + stage * 4) = 0;
        }
        D3DBlockUntilNotBusy(data->texture);
    }
    if (SHAPE_infoflags(data->shape) & 2)
        filter = 1;
    d = data;
    if ((d->bits & 0x1000000) == 0 && d->format != 0xc && d->format != 0xe && d->format != 0xf) {
        address0 = 3;   // swizzled, not compressed: clamp
        addressU = 3;
        addressV = 3;
        addressW = 3;
    }
    format = d->format;
    int32_t height = d->height;
    int32_t width = d->width;
    uint32_t headerHeight = (uint32_t)height;
    if (format == 0xc || format == 0xe || format == 0xf) {
        width /= 4;
        height /= 4;
    }
    int32_t levels = MipLevels(d->bits);
    uint32_t size = 0;   // what the copy path allocated
    int32_t level = 0;
    do {
        size += (uint32_t)height * (uint32_t)width * (uint32_t)bpp;
        width >>= 1;
        height >>= 1;
        level++;
    } while (level < levels);
    (void)size;

    // The in-place path, for every texture (see above).
    uint32_t headerWidth = (uint32_t)d->width;
    XGSetTextureHeader(headerWidth, headerHeight, (uint32_t)levels, 0, format, 0, d->texture, 0,
                       headerWidth * (uint32_t)bpp);
    D3DRegister(data->texture, data->pixels);

    if (SHAPE_infoflags(data->shape) & 4)
        data->texture[3] |= 4;
    U8(kRegistered) = 1;
    committed = 1;
    return 1;
}

// The TAR's record for a SHAPE image (EAGL's built-in image if the SHAPE has nowhere to keep one); the first TAR on
// it fills it in and commits the texture. The clut, if the image has one, goes into this TAR's palette either way.
// FUNC_AT(0x000ec000)
void EAGL::TAR::Create(uint8_t *shape) {
    data = EAGL_FindSharedData(shape);
    if (data == NULL) {
        shape = DefaultShape();
        data = EAGL_FindSharedData(shape);
    }
    data->refCount++;
    if (data->refCount == 1) {
        data->shape = shape;
        data->width = *(int16_t *)(shape + 4);
        data->height = *(int16_t *)(shape + 6);
        data->bits = (data->bits & ~0x3fu) | (((*(uint32_t *)(shape + 0xc) >> 28) + 1) & 0x3f);
        data->bits = (data->bits & ~0x1000000u) | ((*(uint32_t *)(shape + 0xc) << 11) & 0x1000000);
        int depth = SHAPE_depth(shape);
        data->bits = (data->bits & ~0x3fc0u) | (((uint32_t)depth << 6) & 0x3fc0);
        data->format = EAGL_TextureFormatFromShape(shape);
        clut = NULL;
        uint8_t *c = EAGL_FindClut(shape);
        clut = c;
        if (c != NULL)
            LoadClut(this, c);
        data->pixels = ImageData(shape);
        Commit();
        return;
    }
    clut = NULL;
    uint8_t *c = EAGL_FindClut(shape);
    clut = c;
    if (c != NULL)
        LoadClut(this, c);
}

// ---- the Xbox extension (this = the TAR's +0x48 field)

// FUNC_AT(0x000ebd40)
TARExtension* EAGL::TARExtension::Construct(TAR *owner) {
    tar = owner;
    return this;
}

// FUNC_AT(0x000ebd50)
void EAGL::TARExtension::DumpState() {
}

// FUNC_AT(0x000ebd60)
bool EAGL::TARExtension::SetStage(uint32_t s) {
    tar->stage = s;
    return true;
}

// FUNC_AT(0x000ebd70)
bool EAGL::TARExtension::GetStage(uint32_t *s) const {
    *s = tar->stage;
    return true;
}

// FUNC_AT(0x000ebd80)
bool EAGL::TARExtension::SetMaxAnisotropy(uint32_t anisotropy) {
    tar->maxAnisotropy = anisotropy;
    return true;
}

// FUNC_AT(0x000ebd90)
bool EAGL::TARExtension::GetMaxAnisotropy(uint32_t *anisotropy) const {
    *anisotropy = tar->maxAnisotropy;
    return true;
}

// FUNC_AT(0x000ebda0)
bool EAGL::TARExtension::SetBumpEnvMatrix(float m00, float m01, float m10, float m11) {
    TAR *t = tar;
    memcpy(&t->bumpEnv[0], &m00, 4);   // stored as the bits passed
    memcpy(&t->bumpEnv[1], &m01, 4);
    memcpy(&t->bumpEnv[2], &m10, 4);
    memcpy(&t->bumpEnv[3], &m11, 4);
    return true;
}

// FUNC_AT(0x000ebdd0)
bool EAGL::TARExtension::GetBumpEnvMatrix(float *matrix) const {
    memcpy(matrix, tar->bumpEnv, 16);
    return true;
}

// Walks every record's image for a clut attachment and keeps none of it (the original's loop has no effect beyond
// the reads), then stores the list.
// FUNC_AT(0x000eb3a0)
void EAGL::TARExtension::SetAtlas(void *list) {
    uint8_t **entry = (uint8_t **)((uint8_t *)list + 8);
    while (*entry != NULL) {
        uint8_t *volatile found = EAGL_FindClut(*entry);
        (void)found;
        entry = (uint8_t **)((uint8_t *)entry + 0xc);
    }
    tar->atlas = list;
}

// FUNC_AT(0x000eb3e0)
void* EAGL::TARExtension::GetAtlas() const {
    return tar->atlas;
}

// A new image for the TAR: the old record let go (and its texture unbound) when this was its last TAR, a record for
// the new SHAPE found or allocated, then Create. Quirk kept: when the SHAPE has nowhere to keep a record, the one
// allocated here is replaced (and leaked) by Create's, which falls back to EAGL's built-in image.
// FUNC_AT(0x000ec420)
void EAGL::TARExtension::SwapShape(uint8_t *shape) {
    if (shape == NULL)
        shape = DefaultShape();
    tar->data->refCount--;
    if (tar->data->refCount == 0) {
        if (U32(kStageTexture + tar->stage * 4) == Ptr(tar->data->texture)) {
            D3DSetTexture(tar->stage, NULL);
            U32(kStageTexture + tar->stage * 4) = 0;
        }
        if (tar->data->texture != NULL)
            D3DBlockUntilNotBusy(tar->data->texture);
        ReleaseShared(tar->data);
    }
    tar->data = EAGL_FindSharedData(shape);
    if (tar->data == NULL) {
        tar->data = NewShared();
        tar->data->bits |= 0x800000;
    }
    tar->Create(shape);
}

// The other TAR's record, palette and clut shared (one more reference). The old record is let go when this was its
// last TAR; the old palette is unbound and waited for but never released (leaked), as in the original.
// FUNC_AT(0x000ec530)
void EAGL::TARExtension::Share(TAR *other) {
    tar->data->refCount--;
    if (tar->data->refCount == 0) {
        if (U32(kStageTexture + tar->stage * 4) == Ptr(tar->data->texture)) {
            D3DSetTexture(tar->stage, NULL);
            U32(kStageTexture + tar->stage * 4) = 0;
        }
        if (tar->data->texture != NULL)
            D3DBlockUntilNotBusy(tar->data->texture);
        if (tar->palette != NULL) {
            if (U32(kStagePalette + tar->stage * 4) == Ptr(tar->palette)) {
                D3DSetPalette(tar->stage, NULL);
                U32(kStagePalette + tar->stage * 4) = 0;
            }
            D3DBlockUntilNotBusy(tar->palette);
        }
        ReleaseShared(tar->data);
    }
    other->data->refCount++;
    tar->data = other->data;
    tar->palette = other->palette;
    tar->clut = other->clut;
}

// ---- TARs over D3D surfaces (render contexts, offscreen buffers, shadow maps)

// FUNC_AT(0x000ec610)
TAR* EAGL_TARFromSurface(void *surface) {
    TAR *t = (TAR *)Malloc(0x4c, 0x0018a348u);   // "EAGL::TAR new"
    t = t != NULL ? t->ConstructShared() : NULL;
    uint32_t desc[7];   // D3DSURFACE_DESC
    D3DGet2DSurfaceDesc(surface, 0, desc);
    t->data->shape = NULL;
    t->clut = NULL;
    t->palette = NULL;
    t->data->width = (int32_t)desc[5];
    t->data->height = (int32_t)desc[6];
    t->data->format = desc[0];
    t->data->bits &= 0xffffffc0u;
    t->data->bits &= 0xfeffffffu;
    t->data->bits &= 0xffffc03fu;
    t->addressU = 3;
    t->addressV = 3;
    t->addressW = 3;
    t->data->texture = (uint32_t *)surface;
    return t;
}

// A render-target texture (depth 16 or 32; mode 1 linear, 0 swizzled and clamped), with a SHAPE header of its own
// ("EGLRTShape") whose data offset points at the texture's memory.
// FUNC_AT(0x000ec6e0)
TAR* EAGL_TARRenderTarget(int32_t width, int32_t height, int32_t depth, int32_t mode) {
    TAR *t = (TAR *)Malloc(0x4c, 0x0018a348u);   // "EAGL::TAR new"
    t = t != NULL ? t->ConstructShared() : NULL;
    t->data->shape = NULL;
    t->clut = NULL;
    t->palette = NULL;
    t->data->width = width;
    t->data->height = height;
    t->data->bits = (t->data->bits & 0xffffffc1u) | 1;
    t->data->bits |= 0x1000000;
    t->data->bits = (t->data->bits & ~0x3fc0u) | (((uint32_t)depth << 6) & 0x3fc0);
    if (mode == 0) {
        t->data->bits &= 0xfeffffffu;
        t->address0 = 3;
        t->addressU = 3;
        t->addressV = 3;
        t->addressW = 3;
    }
    if (depth == 0x10)
        t->data->format = mode == 1 ? 5 : 0x11;
    else if (depth == 0x20)
        t->data->format = mode == 1 ? 6 : 0x12;
    TARSharedData *d = t->data;
    d->texture = D3DCreateTexture2((uint32_t)d->width, (uint32_t)d->height, 1, 1, 1, d->format, 3);
    t->committed = 0;
    t->data->flags |= 1;
    uint8_t *s = (uint8_t *)Malloc(0x14, 0x001ccc58u);   // "EGLRTShape"
    memset(s, 0, 0x14);
    if (depth == 0x10)
        s[0] = 0x78;
    else if (depth == 0x20)
        s[0] = 0x7d;
    *(uint16_t *)(s + 4) = (uint16_t)width;
    *(uint16_t *)(s + 6) = (uint16_t)height;
    *(uint32_t *)(s + 0xc) |= 0x1000;
    *(uint32_t *)(s + 0x10) = t->data->texture[1] - Ptr(s) - 0x80000000u;
    t->data->shape = s;
    return t;
}

// A depth surface (depth 16 or 32), with its own SHAPE header ("EGLRTZShape").
// FUNC_AT(0x000ec8a0)
TAR* EAGL_TARDepthSurface(int32_t width, int32_t height, int32_t depth) {
    TAR *t = (TAR *)Malloc(0x4c, 0x0018a348u);   // "EAGL::TAR new"
    t = t != NULL ? t->ConstructShared() : NULL;
    t->data->shape = NULL;
    t->clut = NULL;
    t->palette = NULL;
    t->data->width = width;
    t->data->height = height;
    t->data->bits = (t->data->bits & 0xffffffc1u) | 1;
    t->data->bits &= 0xfeffffffu;
    t->data->bits = (t->data->bits & ~0x3fc0u) | (((uint32_t)depth << 6) & 0x3fc0);
    t->address0 = 3;
    t->addressU = 3;
    t->addressV = 3;
    t->addressW = 3;
    if (depth == 0x10)
        t->data->format = 0x30;
    else if (depth == 0x20)
        t->data->format = 0x2e;
    TARSharedData *d = t->data;
    d->texture = D3DCreateStandAloneSurface((uint32_t)d->width, (uint32_t)d->height, 2, d->format);
    t->committed = 0;
    t->data->flags |= 1;
    uint8_t *s = (uint8_t *)Malloc(0x14, 0x001ccc64u);   // "EGLRTZShape"
    memset(s, 0, 0x14);
    if (depth == 0x10)
        s[0] = 0x78;
    else if (depth == 0x20)
        s[0] = 0x7d;
    *(uint16_t *)(s + 4) = (uint16_t)width;
    *(uint16_t *)(s + 6) = (uint16_t)height;
    *(uint32_t *)(s + 0xc) |= 0x1000;
    *(uint32_t *)(s + 0x10) = t->data->texture[1] - Ptr(s) - 0x80000000u;
    t->data->shape = s;
    return t;
}

// ---- the loader's constructor and destructor for EAGL::TAR symbols

// The symbol's data is a TAR as the tools wrote it: the SHAPE's four-character name at +4, the U and V wrap modes
// at +0x1c/+0x20 (0 clamp, 1 wrap), the filter at +0x24 (0..2), a clut flag at +0x28 and the clut's name at +0x2c.
// The TAR is built over it in place; its settings are read from a copy taken first.
// FUNC_AT(0x000ed070)
void EAGL_TARConstructor(void *object, DynamicLoader *loader) {
    TAR *t = (TAR *)object;
    uint8_t record[0x4c];
    memcpy(record, object, 0x4c);
    char name[11];
    ShapeSymbol(name, 0x001cca44u, (const uint8_t *)object + 4);
    uint8_t *shape = (uint8_t *)FindShape(name, loader);
    if (shape == NULL)
        shape = DefaultShape();
    t->Construct(shape);
    if (*(uint32_t *)(record + 0x28) != 0) {
        char clutName[11];
        ShapeSymbol(clutName, 0x001cca4cu, record + 0x2c);
        uint8_t *clutShape = (uint8_t *)FindShape(clutName, loader);
        if (clutShape != NULL)
            t->SwapClut(clutShape);
    }
    uint32_t u = *(uint32_t *)(record + 0x1c);
    if (u == 0) {
        TAR *e = t->extension;
        t->address0 = 3;
        t->addressU = 3;
        t->addressV = 3;
        t->addressW = 3;
        e->addressU = 3;
    } else if (u == 1) {
        TAR *e = t->extension;
        t->address0 = 1;
        t->addressU = 1;
        t->addressV = 1;
        t->addressW = 1;
        e->addressU = 1;
    } else {
        PrintMessage(0, (const char *)0x001cca58u, u);   // "... Invalid U wrap/clamp setting '%d' ..."
    }
    uint32_t v = *(uint32_t *)(record + 0x20);
    if (v == 0)
        t->extension->addressV = 3;
    else if (v == 1)
        t->extension->addressV = 1;
    else
        PrintMessage(0, (const char *)0x001ccac0u, v);   // "... Invalid V wrap/clamp setting '%d' ..."
    uint32_t f = *(uint32_t *)(record + 0x24);
    if (f == 0)
        t->filter = 1;
    else if (f == 1)
        t->filter = 2;
    else if (f == 2)
        t->filter = 3;
    else
        PrintMessage(0, (const char *)0x001ccb28u, f);   // "... Invalid filter mode '%d'"
}

// FUNC_AT(0x000ed2b0)
void EAGL_TARDestructor(void *object) {
    ((TAR *)object)->Destruct();
}

// FUNC_AT(0x000ed2c0)
void EAGL_TARDestructor2(void *object) {
    ((TAR *)object)->Destruct();
}

// ---- EAGLInternal::Property pieces (the folded constructor is also Grenade's and Missile's)

// FUNC_AT(0x000ed310)
EAGL::TARProperty* EAGL::TARProperty::Construct() {
    name = NULL;
    count = 0;
    values = NULL;
    return this;
}

// The value array freed with size 4 whatever its length, as in the original.
// FUNC_AT(0x000ed320)
void EAGL::TARProperty::Destruct() {
    Free((void *)values, 4);
}

// The property array through the vector destructor iterator (0x000ed320 each), its block freed with size 0xc, then
// the text buffer. Only an unwind funclet reaches it.
// FUNC_AT(0x000edde0)
void EAGL::TARProperties::Destruct() {
    if (list != NULL) {
        int32_t *block = (int32_t *)list - 1;
        ((void (__stdcall *)(void *, uint32_t, int32_t, void *))0x0013332eu)(list, 0xc, block[0],
                                                                           (void *)0x000ed320u);   // ??_M
        Free(block, 0xc);
    }
    Free(buffer, (uint32_t)length + 1);
}

// ---- unreferenced helpers after it (register arguments: the original instructions)

// The image count of a SHPX file in EAX.
// FUNC_AT(0x000ede20)
__declspec(naked) void EAGL_ShapeCountEax() {
    __asm {
        mov eax, dword ptr [eax + 8]
        ret
    }
}

// Image EAX of the SHPX file in ECX.
// FUNC_AT(0x000ede30)
__declspec(naked) void EAGL_ShapeImageEax() {
    __asm {
        mov eax, dword ptr [ecx + eax * 8 + 0x14]
        add eax, ecx
        ret
    }
}

// FUNC_AT(0x000ede40)
int EAGL_AddSymbolAnswerZero(const char *name, void *value) {
    GlobalPool->AddSymbol(name, value);
    return 0;
}
