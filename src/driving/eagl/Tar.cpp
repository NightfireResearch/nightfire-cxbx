#include "Tar.h"
#include "../platform/XboxXapi.h"

#include "D3D8State.h"
#include "EaglGlobals.h"
#include "EaglOriginals.h"
#include "Loader.h"
#include "Profiler.h"
#include "Realgraph.h"
#include "View.h"
#include "../platform/X87.h"

#include <bit>
#include <stddef.h>
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
// (0x00175428, 32 states a stage) and dirty flags (0x00175424) directly, as the inlined XDK code did - the backend
// reads them at draw time (5.2). Commit gives the image to D3D: see the comment above it about the in-place path.
//
// Everything runs in the original's order with the original's arguments; exception frames are left out (nothing
// throws). Calls to D3D8 go to the seam (../gfx/D3D8.h).
// ---------------------------------------------------------------------------------------------------------------

using EAGL::ShapeImage;
using EAGL::SurfaceTexture;
using EAGL::TAR;
using EAGL::TARAtlas;
using EAGL::TARExtension;
using EAGL::TARSharedData;

namespace {

// The allocations' names, passed as the original's strings (and NameTARNew, EaglGlobals.h)
#define NameSharedDataNew ((const char *)0x001ccc70)        // "EAGL::TARPrivate::SharedData new"
#define NameD3DTexture ((const char *)0x001ccc4c)           // "D3DTexture"
#define NameRenderTargetShape ((const char *)0x001ccc58)    // "EGLRTShape"
#define NameDepthSurfaceShape ((const char *)0x001ccc64)    // "EGLRTZShape"

// TARProperty::Destruct's original entry, which jumps to ours: the C runtime's vector destructor iterator calls it
// with the element in ECX, which no C++ function pointer of ours can be passed as.
#define TARPropertyDestructEntry ((void *)0x000ed320)

// ---- EAGL's state (the overrides, the YUV shadow and the built-in SHPX are EaglGlobals.h's)
#define StagePalette ((D3DResource **)0x0023ffa0)           // the palette bound per stage
#define CacheLodBias ((float *)0x0023ffc0)                  // per stage
#define CacheBumpEnv ((float (*)[4])0x0023ffe8)             // four per stage

// What Use last sent per stage, at 0x001ccb8c: a word per stage in each array (eight slots, four stages used)
struct StageStateCache {
    uint32_t addressU[8];            // +0x00
    uint32_t addressV[8];            // +0x20
    uint32_t addressW[8];            // +0x40
    uint32_t filter[8];              // +0x60
    uint32_t mipFilter[8];           // +0x80
    uint32_t maxAnisotropy[8];       // +0xa0
};
static_assert(sizeof(StageStateCache) == 0xc0, "the stage caches run to 0x001ccc4c");

#define StageCache (*(StageStateCache *)0x001ccb8c)

// The texture-stage states Use writes into D3D8's table (D3D8State.h), as the Xbox's D3D8 numbers them
enum D3DTextureStateIndex {
    kTssAddressU = 0, kTssAddressV, kTssAddressW, kTssMagFilter, kTssMinFilter, kTssMipFilter, kTssMipMapLodBias,
    kTssMaxMipLevel, kTssMaxAnisotropy,
    kTssBumpEnvMat00 = 22, kTssBumpEnvMat01, kTssBumpEnvMat11, kTssBumpEnvMat10,
};

enum TextureAddress : uint32_t { kAddressWrap = 1, kAddressClamp = 3 };

inline ShapeImage *Image(uint8_t *shape) {
    return reinterpret_cast<ShapeImage *>(shape);
}

// The first image of EAGL's built-in SHPX
inline uint8_t *DefaultShape() {
    return reinterpret_cast<uint8_t *>(&BuiltInShapes) + BuiltInShapes.entries[0].offset;
}

// The inlined D3DDevice_SetTextureStageState: the stage's dirty bit, then the value into D3D8's table.
void SetStageState(uint32_t stage, D3DTextureStateIndex state, uint32_t value) {
    D3DDirtyFlags |= 1u << (stage & 31);   // SHL by CL
    D3DTextureState[stage][state] = value;
}

// One cached stage state: sent when it differs from the cache.
void CachedStageState(uint32_t stage, uint32_t *cache, D3DTextureStateIndex state, uint32_t value) {
    if (cache[stage] != value) {
        cache[stage] = value;
        SetStageState(stage, state, value);
    }
}

// A SHAPE image's pixels: at an offset when its flag says so, else straight after the 0x10-byte header - where the
// offset word would be.
uint8_t *ImageData(uint8_t *image) {
    if (Image(image)->flags & EAGL::kShapeDataAtOffset)
        return image + Image(image)->dataOffset;
    return (uint8_t *)&Image(image)->dataOffset;
}

// The image's clut attachment ('*'), or NULL.
uint8_t *ClutAttachment(uint8_t *shape) {
    uint8_t *p = shape;
    if (p == NULL)
        return NULL;
    for (;;) {
        if (Image(p)->type == 0x2a)
            return p;
        int32_t next = Image(p)->next;
        if (next == 0)
            return NULL;
        p += next;
        if (p == NULL)
            return NULL;
    }
}

// The clut's 256 colours into the palette, a byte at a time as the original reads them.
void CopyClut(uint32_t *palette, const uint8_t *colours) {
    for (int i = 0; i < 256; i++, colours += 4)
        palette[i] = colours[0] | colours[1] << 8 | colours[2] << 16 | (uint32_t)colours[3] << 24;
}

// The palette made if there is none yet, the clut image's colours copied into it (Create's order).
void LoadClut(TAR *t, uint8_t *clutImage) {
    if (t->palette == NULL)
        t->palette = D3DDevice_CreatePalette2(0);
    uint8_t *colours = ImageData(clutImage);
    t->clut = colours;
    uint32_t *p = D3DPalette_Lock2(t->palette, 0);
    CopyClut(p, colours);
}

// The clut of the SHAPE, if it has one, into the TAR (clut written twice, as the original does).
void FindAndLoadClut(TAR *t, uint8_t *shape) {
    t->clut = NULL;
    uint8_t *c = EAGL_FindClut(shape);
    t->clut = c;
    if (c != NULL)
        LoadClut(t, c);
}

// The shared record let go when the last TAR leaves it: freed too when EAGL allocated it.
void ReleaseShared(TARSharedData *d) {
    if (d->allocated) {
        if (d != NULL) {
            d->Release();
            EaglFree(d, sizeof(TARSharedData));
        }
    } else {
        d->Release();
    }
}

// A new record when the SHAPE has none of its own (TARSharedData::Init inlined).
TARSharedData *NewShared() {
    TARSharedData *d = (TARSharedData *)EaglMalloc(sizeof(TARSharedData), NameSharedDataNew);
    if (d != NULL)
        d->Init();
    return d;
}

// The record's texture unbound from the TAR's stage if it is bound there.
void UnbindTexture(TAR *t) {
    if (StageTexture[t->stage] == t->data->texture) {
        D3DDevice_SetTexture(t->stage, NULL);
        StageTexture[t->stage] = NULL;
    }
}

// The palette unbound from the TAR's stage if it is bound there, and waited for.
void UnbindPalette(TAR *t) {
    if (StagePalette[t->stage] == t->palette) {
        D3DDevice_SetPalette(t->stage, NULL);
        StagePalette[t->stage] = NULL;
    }
    D3DResource_BlockUntilNotBusy(t->palette);
}

// "shape_" and four characters, as the loader names a SHAPE.
void ShapeSymbol(char *name, const uint8_t *four) {
    memcpy(name, "shape_", 6);
    name[6] = four[0];
    name[7] = four[1];
    name[8] = four[2];
    name[9] = four[3];
    name[10] = 0;
}

uint8_t *FindShape(const char *name, DynamicLoader *loader) {
    bool found;
    void *shape = GlobalPool.Search(name, &found);
    if (!found) {
        void *out = NULL;
        loader->GetAddr("SHAPE", name, &out);
        shape = out;
    }
    return (uint8_t *)shape;
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
    const ShapeImage *image = reinterpret_cast<const ShapeImage *>(shape);
    uint32_t type = image->type - 0x60;
    uint32_t format = 0;
    if (type <= 0x1e) {
        bool linear = (image->flags & EAGL::kShapeLinear) != 0;
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

// FUNC_AT(0x000eb200)
uint8_t* EAGL_FindClut(uint8_t *shape) {
    return ClutAttachment(shape);
}

// FUNC_AT(0x000eb840)
TARSharedData* EAGL::TARSharedData::Init() {
    allocated = 0;
    initialised = 1;
    refCount = 0;
    width = 0;
    height = 0;
    shape = NULL;
    contiguous = NULL;
    texture = NULL;
    flags &= ~kSharedOwnsShape;
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
    if (texture->common & 0x1000000) {   // made by D3D (render targets, surfaces): D3D releases it
        D3DResource_Release(texture);
        texture = NULL;
        return;
    }
    if (contiguous != NULL) {
        Xbox_MmFreeContiguousMemory(contiguous);
        contiguous = NULL;
    }
    if (texture != NULL) {
        texture->common = 0;
        texture->data = 0;
        texture->lock = 0;
        texture->format = 0;
        texture->size = 0;
        EaglFree(texture, sizeof(SurfaceTexture));
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
        TARSharedData *d = (TARSharedData *)info;
        if (info[0] == 'U' && info[1] == 'S' && info[2] == 'E' && info[3] == 'D')
            return d;
        info[3] = 'D';
        d->Init();
        info[0] = 'U';
        info[1] = 'S';
        info[2] = 'E';
        return d;
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
    address0 = kAddressWrap;
    addressU = kAddressWrap;
    addressV = kAddressWrap;
    addressW = kAddressWrap;
    filter = 2;
    mipFilter = 2;
    stage = 0;
    lodBias = 0.0f;
    committed = 0;
    maxAnisotropy = 4;
    palette = NULL;
    clut = NULL;
    data = NULL;
    bumpEnv[0] = 1.0f;
    bumpEnv[1] = 0.0f;
    bumpEnv[2] = 0.0f;
    bumpEnv[3] = 1.0f;
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
    data->allocated = 1;   // a failed allocation faults here, as in the original
    data->flags &= ~kSharedOwnsShape;
    data->refCount++;
    data->shape = NULL;
    data->width = 0;
    data->height = 0;
    data->mipLevels = 0;
    data->depth = 0;
    palette = NULL;
    clut = NULL;
    data->pixels = NULL;
    data->texture = NULL;
    return this;
}

// The other's fields over the defaults (this one's extension pointer kept), one more reference on the record.
// FUNC_AT(0x000ec3b0)
TAR* EAGL::TAR::ConstructCopy(const TAR *other) {
    InitFields();
    extension = this;
    memcpy(this, other, offsetof(TAR, extension));
    data->refCount++;
    return this;
}

// FUNC_AT(0x000ecab0)
void EAGL::TAR::Destruct() {
    data->refCount--;
    if (data->refCount == 0) {
        for (uint32_t s = 0; s < 4; s++) {
            if (StageTexture[s] == data->texture) {
                D3DDevice_SetTexture(s, NULL);
                StageTexture[s] = NULL;
            }
        }
        if (data->texture != NULL)
            D3DResource_BlockUntilNotBusy(data->texture);
        if (data->flags & kSharedOwnsShape)
            EaglFree(data->shape, sizeof(ShapeImage));   // the render target's SHAPE header
        ReleaseShared(data);
    }
    if (palette != NULL)
        UnbindPalette(this);
    if (palette != NULL) {
        D3DResource_Release(palette);
        palette = NULL;
    }
}

// FUNC_AT(0x000eb220)
bool EAGL::TAR::SwapClut(uint8_t *clutShape) {
    uint8_t *p = ClutAttachment(clutShape);
    if (p == NULL)
        return false;
    uint8_t *colours = ImageData(p);
    clut = colours;
    if (colours == NULL)
        return false;
    if (palette == NULL)
        palette = D3DDevice_CreatePalette2(0);
    uint32_t *entries = D3DPalette_Lock2(palette, 0);
    CopyClut(entries, colours);
    return true;
}

// FUNC_AT(0x000ecbc0)
bool EAGL::TAR::SwapShape(uint8_t *shape) {
    reinterpret_cast<TARExtension *>(&extension)->SwapShape(shape);
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
// bump-env matrix through D3D8. The two globals FilterOverride and LodBiasOverride override every TAR's own when
// set.
// FUNC_AT(0x000eb3f0)
void EAGL::TAR::Use() {
    uint8_t yuv = data->yuv;
    if (YuvEnable != yuv) {
        YuvEnable = yuv;
        D3DDevice_SetRenderState_YuvEnable(yuv);
    }
    if (ApplyCache.textureEnable != 0) {
        if (StageTexture[stage] != data->texture) {
            StageTexture[stage] = data->texture;
            D3DDevice_SetTexture(stage, data->texture);
        }
    }
    if (palette != NULL && StagePalette[stage] != palette) {
        StagePalette[stage] = palette;
        D3DDevice_SetPalette(stage, palette);
    }
    CachedStageState(stage, StageCache.addressU, kTssAddressU, addressU);
    CachedStageState(stage, StageCache.addressV, kTssAddressV, addressV);
    CachedStageState(stage, StageCache.addressW, kTssAddressW, addressW);
    uint32_t override = FilterOverride;
    uint32_t f = override == 0xffffffff ? filter : override;
    if (StageCache.filter[stage] != f) {
        StageCache.filter[stage] = f;
        SetStageState(stage, kTssMagFilter, f);
        SetStageState(stage, kTssMinFilter, f);
    }
    CachedStageState(stage, StageCache.maxAnisotropy, kTssMaxAnisotropy, maxAnisotropy);

    // FCOMP + TEST AH,0x44: a component counts as changed unless it compares equal (NaN: changed).
    float *cache = CacheBumpEnv[stage];
    if (!(cache[0] == bumpEnv[0]) || !(cache[1] == bumpEnv[1]) || !(cache[2] == bumpEnv[2]) ||
        !(cache[3] == bumpEnv[3])) {
        cache[0] = bumpEnv[0];
        cache[1] = bumpEnv[1];
        cache[2] = bumpEnv[2];
        cache[3] = bumpEnv[3];
        D3DDevice_SetTextureState_BumpEnv(stage, kTssBumpEnvMat00, std::bit_cast<uint32_t>(bumpEnv[0]));
        D3DDevice_SetTextureState_BumpEnv(stage, kTssBumpEnvMat01, std::bit_cast<uint32_t>(bumpEnv[1]));
        D3DDevice_SetTextureState_BumpEnv(stage, kTssBumpEnvMat10, std::bit_cast<uint32_t>(bumpEnv[2]));
        D3DDevice_SetTextureState_BumpEnv(stage, kTssBumpEnvMat11, std::bit_cast<uint32_t>(bumpEnv[3]));
    }

    if (!(LodBiasOverride == 0.0f)) {   // an override (or NaN)
        if (!(CacheLodBias[stage] == LodBiasOverride)) {
            CacheLodBias[stage] = QuietNaN(LodBiasOverride);                                      // FLD / FSTP
            SetStageState(stage, kTssMipMapLodBias, std::bit_cast<uint32_t>(LodBiasOverride));   // the raw bits (MOV)
        }
    } else if (!(CacheLodBias[stage] == lodBias)) {
        CacheLodBias[stage] = lodBias;
        SetStageState(stage, kTssMipMapLodBias, std::bit_cast<uint32_t>(lodBias));
    }
    CachedStageState(stage, StageCache.mipFilter, kTssMipFilter, mipFilter);
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
    if (palette != NULL)
        UnbindPalette(this);
    if (palette != NULL) {
        D3DResource_Release(palette);
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
    for (TARAtlasEntry *entry = atlas->entries; entry->image != NULL; entry++) {
        uint8_t *image = entry->image;
        uint8_t *pixels = ImageData(image);
        int32_t w = Image(image)->width;
        int32_t h = Image(image)->height;
        int32_t x = entry->x;
        int32_t y = entry->y;
        uint32_t offset = 0;
        if (pixels == NULL)
            continue;
        Device::Get();
        int32_t right = x + w;
        int32_t bottom = y + h;
        int32_t level = 0;
        do {
            D3DPixelContainer *surface = D3DTexture_GetSurfaceLevel2(data->texture, level);
            D3DLockedRect locked;
            D3DSurface_LockRect(surface, &locked, NULL, 0x80);
            uint32_t pitch = locked.pitch;
            D3DSurfaceDesc desc;
            D3DSurface_GetDesc(surface, &desc);
            D3DRect sourceRect = { 0, 0, (int32_t)desc.width, (int32_t)desc.height };
            D3DRect destRect = { x, y, right, bottom };
            if (D3DXLoadSurfaceFromMemory(surface, NULL, &destRect, data->pixels + offset, data->format, pitch, NULL,
                                          &sourceRect, 1, 0) < 0)
                D3DResource_Release(surface);
            offset += desc.height * pitch;
            D3DResource_Release(surface);
            level++;
        } while (level < data->mipLevels);
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
    enum { kDxt1 = 0x0c, kDxt3 = 0x0e, kDxt5 = 0x0f, kYuy2 = 0x24 };
    int32_t bpp = XGBytesPerPixelFromFormat(data->format);
    TARSharedData *d = data;
    uint32_t format = d->format;
    if (format == kDxt1 || format == kDxt3 || format == kDxt5 || format == kYuy2) {
        switch (format) {
            case kDxt1: bpp = 8; break;                 // bytes per 4x4 block
            case kDxt3: case kDxt5: bpp = 16; break;
            case kYuy2: bpp = bpp / 2; break;
            default: break;
        }
    }
    if (format == kYuy2)
        d->yuv = 1;
    if (data->texture == NULL) {
        data->texture = (SurfaceTexture *)EaglMalloc(sizeof(SurfaceTexture), NameD3DTexture);
    } else {
        UnbindTexture(this);
        D3DResource_BlockUntilNotBusy(data->texture);
    }
    if (SHAPE_infoflags(data->shape) & 2)
        filter = 1;
    d = data;
    if (!d->linear && d->format != kDxt1 && d->format != kDxt3 && d->format != kDxt5) {
        address0 = kAddressClamp;   // swizzled, not compressed: clamp
        addressU = kAddressClamp;
        addressV = kAddressClamp;
        addressW = kAddressClamp;
    }
    format = d->format;
    int32_t height = d->height;
    int32_t width = d->width;
    uint32_t headerHeight = height;
    if (format == kDxt1 || format == kDxt3 || format == kDxt5) {
        width /= 4;
        height /= 4;
    }
    int32_t levels = d->mipLevels;
    uint32_t size = 0;   // what the copy path allocated
    int32_t level = 0;
    do {
        size += (uint32_t)height * width * bpp;
        width >>= 1;
        height >>= 1;
        level++;
    } while (level < levels);
    (void)size;

    // The in-place path, for every texture (see above).
    uint32_t headerWidth = d->width;
    XGSetTextureHeader(headerWidth, headerHeight, levels, 0, format, 0, d->texture, 0, headerWidth * bpp);
    D3DResource_Register(data->texture, data->pixels);

    if (SHAPE_infoflags(data->shape) & 4)
        data->texture->format |= 4;
    ResourceRegistered = 1;
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
    if (data->refCount != 1) {
        FindAndLoadClut(this, shape);
        return;
    }
    const ShapeImage *image = Image(shape);
    data->shape = shape;
    data->width = image->width;
    data->height = image->height;
    data->mipLevels = (image->flags >> 28) + 1;
    data->linear = (image->flags & kShapeLinear) != 0;
    data->depth = SHAPE_depth(shape);
    data->format = EAGL_TextureFormatFromShape(shape);
    FindAndLoadClut(this, shape);
    data->pixels = ImageData(shape);
    Commit();
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
    t->bumpEnv[0] = m00;   // stored as the bits passed
    t->bumpEnv[1] = m01;
    t->bumpEnv[2] = m10;
    t->bumpEnv[3] = m11;
    return true;
}

// FUNC_AT(0x000ebdd0)
bool EAGL::TARExtension::GetBumpEnvMatrix(float *matrix) const {
    matrix[0] = tar->bumpEnv[0];
    matrix[1] = tar->bumpEnv[1];
    matrix[2] = tar->bumpEnv[2];
    matrix[3] = tar->bumpEnv[3];
    return true;
}

// Walks every record's image for a clut attachment and keeps none of it (the original's loop has no effect beyond
// the reads), then stores the list.
// FUNC_AT(0x000eb3a0)
void EAGL::TARExtension::SetAtlas(TARAtlas *list) {
    for (TARAtlasEntry *entry = list->entries; entry->image != NULL; entry++) {
        uint8_t *volatile found = EAGL_FindClut(entry->image);
        (void)found;
    }
    tar->atlas = list;
}

// FUNC_AT(0x000eb3e0)
EAGL::TARAtlas* EAGL::TARExtension::GetAtlas() const {
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
        UnbindTexture(tar);
        if (tar->data->texture != NULL)
            D3DResource_BlockUntilNotBusy(tar->data->texture);
        ReleaseShared(tar->data);
    }
    tar->data = EAGL_FindSharedData(shape);
    if (tar->data == NULL) {
        tar->data = NewShared();
        tar->data->allocated = 1;
    }
    tar->Create(shape);
}

// The other TAR's record, palette and clut shared (one more reference). The old record is let go when this was its
// last TAR; the old palette is unbound and waited for but never released (leaked), as in the original.
// FUNC_AT(0x000ec530)
void EAGL::TARExtension::Share(TAR *other) {
    tar->data->refCount--;
    if (tar->data->refCount == 0) {
        UnbindTexture(tar);
        if (tar->data->texture != NULL)
            D3DResource_BlockUntilNotBusy(tar->data->texture);
        if (tar->palette != NULL)
            UnbindPalette(tar);
        ReleaseShared(tar->data);
    }
    other->data->refCount++;
    tar->data = other->data;
    tar->palette = other->palette;
    tar->clut = other->clut;
}

// ---- TARs over D3D surfaces (render contexts, offscreen buffers, shadow maps)

// FUNC_AT(0x000ec610)
TAR* EAGL_TARFromSurface(D3DPixelContainer *surface) {
    TAR *t = (TAR *)EaglMalloc(sizeof(TAR), NameTARNew);
    t = t != NULL ? t->ConstructShared() : NULL;
    D3DSurfaceDesc desc;
    D3D_Get2DSurfaceDesc(surface, 0, &desc);
    t->data->shape = NULL;
    t->clut = NULL;
    t->palette = NULL;
    t->data->width = desc.width;
    t->data->height = desc.height;
    t->data->format = desc.format;
    t->data->mipLevels = 0;
    t->data->linear = 0;
    t->data->depth = 0;
    t->addressU = kAddressClamp;
    t->addressV = kAddressClamp;
    t->addressW = kAddressClamp;
    t->data->texture = static_cast<SurfaceTexture *>(surface);
    return t;
}

// The SHAPE header a render target or depth surface gets ("EGLRTShape", "EGLRTZShape"): its data offset points at
// the texture's memory.
static uint8_t *SurfaceShape(TARSharedData *d, int32_t width, int32_t height, int32_t depth, const char *name) {
    uint8_t *s = (uint8_t *)EaglMalloc(sizeof(ShapeImage), name);
    memset(s, 0, sizeof(ShapeImage));
    ShapeImage *image = Image(s);
    if (depth == 0x10)
        image->type = 0x78;
    else if (depth == 0x20)
        image->type = 0x7d;
    image->width = width;
    image->height = height;
    image->flags |= EAGL::kShapeDataAtOffset;
    image->dataOffset = d->texture->data - (uintptr_t)s - 0x80000000;
    return s;
}

// A render-target texture (depth 16 or 32; mode 1 linear, 0 swizzled and clamped), with a SHAPE header of its own.
// FUNC_AT(0x000ec6e0)
TAR* EAGL_TARRenderTarget(int32_t width, int32_t height, int32_t depth, int32_t mode) {
    TAR *t = (TAR *)EaglMalloc(sizeof(TAR), NameTARNew);
    t = t != NULL ? t->ConstructShared() : NULL;
    t->data->shape = NULL;
    t->clut = NULL;
    t->palette = NULL;
    t->data->width = width;
    t->data->height = height;
    t->data->mipLevels = 1;
    t->data->linear = 1;
    t->data->depth = depth;
    if (mode == 0) {
        t->data->linear = 0;
        t->address0 = kAddressClamp;
        t->addressU = kAddressClamp;
        t->addressV = kAddressClamp;
        t->addressW = kAddressClamp;
    }
    if (depth == 0x10)
        t->data->format = mode == 1 ? 5 : 0x11;
    else if (depth == 0x20)
        t->data->format = mode == 1 ? 6 : 0x12;
    TARSharedData *d = t->data;
    d->texture = static_cast<SurfaceTexture *>(D3DDevice_CreateTexture2(d->width, d->height, 1, 1, 1, d->format, 3));
    t->committed = 0;
    t->data->flags |= EAGL::kSharedOwnsShape;
    t->data->shape = SurfaceShape(t->data, width, height, depth, NameRenderTargetShape);
    return t;
}

// A depth surface (depth 16 or 32), with its own SHAPE header.
// FUNC_AT(0x000ec8a0)
TAR* EAGL_TARDepthSurface(int32_t width, int32_t height, int32_t depth) {
    TAR *t = (TAR *)EaglMalloc(sizeof(TAR), NameTARNew);
    t = t != NULL ? t->ConstructShared() : NULL;
    t->data->shape = NULL;
    t->clut = NULL;
    t->palette = NULL;
    t->data->width = width;
    t->data->height = height;
    t->data->mipLevels = 1;
    t->data->linear = 0;
    t->data->depth = depth;
    t->address0 = kAddressClamp;
    t->addressU = kAddressClamp;
    t->addressV = kAddressClamp;
    t->addressW = kAddressClamp;
    if (depth == 0x10)
        t->data->format = 0x30;   // D3DFMT_LIN_D16
    else if (depth == 0x20)
        t->data->format = 0x2e;   // D3DFMT_LIN_D24S8
    TARSharedData *d = t->data;
    d->texture = static_cast<SurfaceTexture *>(D3D_CreateStandAloneSurface(d->width, d->height, 2, d->format));
    t->committed = 0;
    t->data->flags |= EAGL::kSharedOwnsShape;
    t->data->shape = SurfaceShape(t->data, width, height, depth, NameDepthSurfaceShape);
    return t;
}

// ---- the loader's constructor and destructor for EAGL::TAR symbols

namespace {

// The symbol's data: a TAR as the tools wrote it.
struct TARSymbolData {               // 0x4c
    uint32_t unknown00;              // +0x00
    uint8_t shapeName[4];            // +0x04 the SHAPE's four-character name
    uint8_t unknown08[0x14];         // +0x08
    uint32_t wrapU;                  // +0x1c 0 clamp, 1 wrap
    uint32_t wrapV;                  // +0x20
    uint32_t filter;                 // +0x24 0..2
    uint32_t hasClut;                // +0x28
    uint8_t clutName[4];             // +0x2c
    uint8_t unknown30[0x1c];         // +0x30
};
static_assert(sizeof(TARSymbolData) == sizeof(TAR), "the symbol's data is a TAR's size");

}  // namespace

// The TAR is built over the symbol's data in place; its settings are read from a copy taken first.
// FUNC_AT(0x000ed070)
void EAGL_TARConstructor(void *object, DynamicLoader *loader) {
    TAR *t = (TAR *)object;
    TARSymbolData record;
    memcpy(&record, object, sizeof(record));
    char name[11];
    ShapeSymbol(name, record.shapeName);
    uint8_t *shape = FindShape(name, loader);
    if (shape == NULL)
        shape = DefaultShape();
    t->Construct(shape);
    if (record.hasClut != 0) {
        char clutName[11];
        ShapeSymbol(clutName, record.clutName);
        uint8_t *clutShape = FindShape(clutName, loader);
        if (clutShape != NULL)
            t->SwapClut(clutShape);
    }
    // The U setting is written a second time through the extension (the TAR itself), as the original does.
    if (record.wrapU == 0) {
        TAR *e = t->extension;
        t->address0 = kAddressClamp;
        t->addressU = kAddressClamp;
        t->addressV = kAddressClamp;
        t->addressW = kAddressClamp;
        e->addressU = kAddressClamp;
    } else if (record.wrapU == 1) {
        TAR *e = t->extension;
        t->address0 = kAddressWrap;
        t->addressU = kAddressWrap;
        t->addressV = kAddressWrap;
        t->addressW = kAddressWrap;
        e->addressU = kAddressWrap;
    } else {
        EAGL::PrintMessage(0, "Constructors::TARConstructor() -- ERROR: Invalid U wrap/clamp setting '%d'. "
                              "See EAGL::FilterMode.\n", record.wrapU);
    }
    if (record.wrapV == 0)
        t->extension->addressV = kAddressClamp;
    else if (record.wrapV == 1)
        t->extension->addressV = kAddressWrap;
    else
        EAGL::PrintMessage(0, "Constructors::TARConstructor() -- ERROR: Invalid V wrap/clamp setting '%d'. "
                              "See EAGL::FilterMode.\n", record.wrapV);
    if (record.filter == 0)
        t->filter = 1;
    else if (record.filter == 1)
        t->filter = 2;
    else if (record.filter == 2)
        t->filter = 3;
    else
        EAGL::PrintMessage(0, "Constructors::TARConstructor() -- ERROR: Invalid filter mode '%d'\n",
                           record.filter);
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
    EaglFree(values, 4);
}

// The property array through the vector destructor iterator (TARProperty::Destruct each), its block - the count
// in the word before it - freed with size 0xc, then the text buffer. Only an unwind funclet reaches it.
// FUNC_AT(0x000edde0)
void EAGL::TARProperties::Destruct() {
    if (list != NULL) {
        int32_t *block = (int32_t *)list - 1;
        VectorDestructorIterator(list, sizeof(TARProperty), block[0], TARPropertyDestructEntry);
        EaglFree(block, sizeof(TARProperty));
    }
    EaglFree(buffer, length + 1);
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
    GlobalPool.AddSymbol(name, value);
    return 0;
}
