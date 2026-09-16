#include "d3dSeam.h"
#include "../../actionhelpers.h"

#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// "Thin seam" reimplementation. These are Eurocom's own small wrapper functions that sit directly on top of
// the statically-linked D3D8 library CXBX still hooks via its own OOVPA pattern matching - we're reimplementing
// THIS layer in clean, normally-compiled C++ while deliberately leaving every D3D8::/XGRAPHC:: function it
// calls completely untouched, so CXBX's own D3D8 emulation keeps working exactly as it does today. The
// eventual full DX9 switch (replacing those D3D8:: functions too) is a separate, later step - this is
// groundwork for it, done first specifically so calling-convention surprises like the one below get caught
// now, while everything is still cross-checkable against the current, still-CXBX-backed known-good baseline.
//
// Confirmed via raw disassembly (not just decompile, and not just the has_custom_variable_storage flag -
// that flag only reflects what Ghidra's own analysis or a human has already caught, not a guarantee that
// nothing else is hiding a custom calling convention) of both these functions and everything they call:
// D3DDevice_SetRenderState_Simple takes its two arguments in ECX/EDX rather than on the stack. Ghidra's own
// analysis reports it as a zero-parameter function and has_custom_variable_storage is NOT set for it - a
// real gap in that flag's coverage. Every other D3D8 function used below (D3DResource_Register,
// XGSetTextureHeader, D3DDevice_SetRenderState_CullMode) was individually re-checked via raw disassembly
// and confirmed to be a plain, normal __stdcall function - worth re-verifying the same way for anything
// added to this file later rather than trusting Ghidra's reported signature at face value.
// ---------------------------------------------------------------------------------------------------------------

#define D3DDevice_SetRenderState_Simple_ADDR   0x00100580u
#define D3DDevice_SetRenderState_CullMode_ADDR 0x001009b0u
#define D3DResource_Register_ADDR              0x00105080u
#define XGSetTextureHeader_ADDR                0x00112586u

#define Gfx_D3DLastError          U32_AT(0x002C5750) // Gfx.D3DLastError
#define Gfx_TotalTextureBytesUsed U32_AT(0x002C6FE0) // Gfx.field6075_0x1890 - running total, informational only

// D3DDevice_SetRenderState_CullMode(value) and D3DResource_Register(pTexture, data) are both plain __stdcall
// functions taking their arguments on the stack in the normal way - confirmed via raw disassembly, unlike
// D3DDevice_SetRenderState_Simple below.
typedef void(__stdcall *D3DDevice_SetRenderState_CullModeFn)(int value);
#define D3DDevice_SetRenderState_CullMode ((D3DDevice_SetRenderState_CullModeFn)D3DDevice_SetRenderState_CullMode_ADDR)

typedef void(__stdcall *D3DResource_RegisterFn)(void *pTexture, uint32_t data);
#define D3DResource_Register ((D3DResource_RegisterFn)D3DResource_Register_ADDR)

typedef void(__stdcall *XGSetTextureHeaderFn)(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage,
                                               int format, uint32_t pool, void *pTexture, uint32_t data, uint32_t pitch);
#define XGSetTextureHeader ((XGSetTextureHeaderFn)XGSetTextureHeader_ADDR)

// D3DDevice_SetRenderState_Simple(NV2A method header word in ECX, value in EDX) - the generic, runtime-method
// render-state setter. Everything else in the D3DDevice_SetRenderState_XXX family takes its single value on
// the stack like a normal __stdcall function; only this one, being the sole one whose method is a caller-
// supplied runtime value rather than baked into its own bytecode, uses registers instead.
static void D3D_SetRenderStateSimple(uint32_t method, uint32_t value) {
    void(*fn)() = (void(*)())D3DDevice_SetRenderState_Simple_ADDR;
    __asm {
        mov ecx, method
        mov edx, value
        call fn
    }
}

// ---------------------------------------------------------------------------------------------------------------
// RegisterTexture
// ---------------------------------------------------------------------------------------------------------------

#define D3D_TEXTURE_TABLE_BASE  0x002CC3ECu // Gfx + 27804 (0x6c9c) - see GraphicsSystem's D3DTexture[2048] field
#define D3D_TEXTURE_TABLE_COUNT 2048

// Matches the 36-byte on-disk layout of the Xbox D3DTexture struct as used by this table. The first 20 bytes
// are opaque Xbox D3D8 texture-header internals (written by XGSetTextureHeader below) that nothing here
// reads directly - only the trailing bookkeeping fields Eurocom's own code touches are named.
struct D3DTextureSlotRaw {
    uint8_t  opaqueHeader[20];
    void    *baseTexture;   // +0x14 - set to point at this same slot once registered (Xbox convention)
    uint16_t width;         // +0x18
    uint16_t height;        // +0x1a
    uint16_t unused1c;      // +0x1c - written 0; nothing else reads it as far as we've traced
    uint16_t nonSwizzled;   // +0x1e - the "param_6 != 0" flag from the caller
    uint32_t mipChainBytes; // +0x20 - total byte size of every mip level, written just below
};
static_assert(sizeof(D3DTextureSlotRaw) == 36, "Bad size for D3DTextureSlotRaw");

static D3DTextureSlotRaw *D3DTextureSlot(int index) {
    return (D3DTextureSlotRaw*)(D3D_TEXTURE_TABLE_BASE + (unsigned)index * sizeof(D3DTextureSlotRaw));
}

// AUTOINJECT
int RegisterTexture(unsigned int width, unsigned int height, int formatType, unsigned int levels, void *data, int param_6) {
    if (param_6 != 0 || (int)levels < 1)
        levels = 1;

    if (formatType == 9)
        param_6 = 1;

    for (int slot = 1; slot < D3D_TEXTURE_TABLE_COUNT; slot++) {
        D3DTextureSlotRaw *texSlot = D3DTextureSlot(slot);
        if (texSlot->baseTexture != NULL)
            continue;

        // Found a free slot. Format enum per formatType (mirroring the original's own switch exactly -
        // some formats have a "swizzled" and "non-swizzled" D3DFORMAT variant selected via param_6);
        // isCompressed marks the DXT-style formats, whose mip levels are measured in 4x4 blocks below.
        int format;
        bool isCompressed = false;
        switch (formatType) {
            case 1:  format = (param_6 != 0) ? 0x1d : 4;  break;
            case 2:  format = (param_6 != 0) ? 0x12 : 6;  break;
            case 3:  format = 0xc;  isCompressed = true;  break;
            case 4:
            case 5:  format = 0xe;  isCompressed = true;  break;
            case 6:
            case 7:  format = 0xf;  isCompressed = true;  break;
            case 8:  format = 0xb;  break;
            case 9:  format = 0x24; break; // D3DFMT_A16B16G16R16
            case 10: format = (param_6 != 0) ? 0x1e : 7;  break;
            default: format = (param_6 != 0) ? 0x10 : 2;  break;
        }

        // Sum the byte size of every mip level (DXT-compressed levels are measured in 4x4-pixel blocks,
        // clamped to a 4x4 minimum; uncompressed levels are measured in texels at 2 or 4 bytes each).
        uint32_t mipChainBytes = 0;
        for (unsigned int level = 0; level < levels; level++) {
            int levelWidth = (int)width >> level;
            if (levelWidth < 1) levelWidth = 1;
            int levelHeight = (int)height >> level;
            if (levelHeight < 1) levelHeight = 1;

            uint32_t levelBytes;
            if (isCompressed) {
                if (levelWidth < 4) levelWidth = 4;
                if (levelHeight < 4) levelHeight = 4;
                // Inverted ternary here previously - DXT1 (formatType==3) is 0.5 bytes/pixel (divide by 2),
                // DXT3/DXT5 (4,5,6,7) are 1 byte/pixel (divide by 1). Confirmed against d3dSeam_debug.log:
                // a 512x512 DXT1 texture was computing 262144 (the DXT3/5 answer) instead of the correct
                // 131072, doubling mipChainBytes and over-running the pitch-realignment memmove below by
                // exactly that much for every DXT1 texture - the likely cause of the mission-load crash.
                levelBytes = (uint32_t)(levelWidth * levelHeight) / (formatType == 3 ? 2u : 1u);
            } else {
                levelBytes = (formatType == 2 || formatType == 10) ? ((uint32_t)levelWidth << 2) : ((uint32_t)levelWidth * 2);
                levelBytes *= levelHeight;
            }
            mipChainBytes += levelBytes;
        }
        texSlot->mipChainBytes = mipChainBytes;

        // The GPU's texture memory needs 128-byte alignment - if the caller's data buffer isn't already
        // aligned, shift the whole mip chain up in place to the next 128-byte boundary. This assumes (as
        // the original does) that the caller left enough slack above the buffer for the shift - not
        // something we can safely change without also auditing every caller's allocation size.
        uintptr_t dataAddr = (uintptr_t)data;
        uintptr_t alignedAddr = (dataAddr + 0x7F) & ~(uintptr_t)0x7F;
        if (alignedAddr != dataAddr) {
            memmove((void*)alignedAddr, (void*)dataAddr, mipChainBytes);
            data = (void*)alignedAddr;
        }

        // Reverted the pitch experiment - didn't fix the movie regression, so back to a faithful port.
        uint32_t pitch = 0;
        if (format == 0x24) // D3DFMT_A16B16G16R16
            pitch = width * 2;

        // XGSetTextureHeader builds the Xbox-tiled texture header directly in-place inside texSlot (Xbox's
        // "texture object" pointer IS the header struct pointer - no separate allocation), then
        // D3DResource_Register associates it with the actual pixel data buffer.
        XGSetTextureHeader(width, height, levels, 0, format, 0, texSlot, 0, pitch);
        D3DResource_Register(texSlot, (uint32_t)data);

        // D3DResource_Register (real, untouched Xbox code, fully disassembled and confirmed to do nothing
        // else) computes header->Data = header->Data(0) + data, but ANDs the result with 0xFFFFFFF unless
        // header->Common has a specific flag bit set - stripping the top nibble of the pointer. On real
        // Xbox hardware every address in this path already lived inside a range where that top nibble was
        // a redundant/reconstructible tag; on PC, a plain heap/pool pointer can have any top nibble, so
        // this silently truncates it. Empirically confirmed (see d3dSeam_debug.log from this investigation):
        // Common=0x00040001 takes the masking branch, turning e.g. 0x8201A500 into 0x0201A500 - which
        // pointed nowhere valid, explaining the black background movie and mission-load hang. We know
        // exactly what Data should be, so just force it back to the real pointer afterward.
        *(uint32_t*)((char*)texSlot + 4) = (uint32_t)data;

        texSlot->width = (uint16_t)width;
        texSlot->baseTexture = texSlot;
        texSlot->height = (uint16_t)height;
        texSlot->unused1c = 0;
        texSlot->nonSwizzled = (uint16_t)(param_6 != 0);
        Gfx_TotalTextureBytesUsed += mipChainBytes;

        return slot;
    }

    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Render state wrappers
// ---------------------------------------------------------------------------------------------------------------

#define D3D_ZFuncCache          U32_AT(0x002C6FA8)
#define D3D_ZFuncLastValue      U32_AT(0x00111AB4)
#define D3D_AlphaRefCache       U32_AT(0x002C6FA0)
#define D3D_AlphaRefLastValue   U32_AT(0x00111AC4)
#define D3D_DepthMaskCache      U32_AT(0x002C6FA4)
#define D3D_DepthMaskLastValue  U32_AT(0x00111AD0)
#define D3D_CullModeLastValue   U32_AT(0x00111C4C)

// Nonzero once the device's initial vertex-shader-creation bring-up (in xboxInitGraphics) has succeeded -
// these render-state setters no-op the actual D3D8 call (but still update their cache) until then.
#define D3D_DeviceReady U32_AT(0x002C576C)

// AUTOINJECT
void d3dSetRenderState(int enableDepthTest) {
    if ((uint32_t)enableDepthTest == D3D_ZFuncCache)
        return;
    D3D_ZFuncCache = (uint32_t)enableDepthTest;

    if (D3D_DeviceReady != 0) {
        // NV097_SET_DEPTH_FUNC. 0x207/0x203 are real OpenGL-style comparison-function constants
        // (GL_ALWAYS/GL_LEQUAL) - NV2A hardware registers reuse them directly.
        uint32_t glCompareFunc = (enableDepthTest == 0) ? 0x207u : 0x203u;
        D3D_SetRenderStateSimple(0x40354, glCompareFunc);
        D3D_ZFuncLastValue = glCompareFunc;
    }
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSetRenderState1(int alphaRef) {
    if ((uint32_t)alphaRef == D3D_AlphaRefCache)
        return;
    D3D_AlphaRefCache = (uint32_t)alphaRef;

    if (D3D_DeviceReady != 0) {
        D3D_SetRenderStateSimple(0x40340, (uint32_t)alphaRef); // NV097_SET_ALPHA_REF
        D3D_AlphaRefLastValue = (uint32_t)alphaRef;
    }
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSetRenderState2(int zWriteEnable) {
    if ((uint32_t)zWriteEnable == D3D_DepthMaskCache)
        return;
    D3D_DepthMaskCache = (uint32_t)zWriteEnable;

    if (D3D_DeviceReady != 0) {
        uint32_t mask = (zWriteEnable != 0) ? 1u : 0u;
        D3D_SetRenderStateSimple(0x4035c, mask); // NV097_SET_DEPTH_MASK
        D3D_DepthMaskLastValue = mask;
    }
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSetCullMode(int cullEnabled) {
    // No dirty-flag cache here (unlike the three above) - matches the original exactly.
    uint32_t value = (cullEnabled != 0) ? 0x901u : 0u;
    D3DDevice_SetRenderState_CullMode((int)value);
    D3D_CullModeLastValue = (uint32_t)cullEnabled; // stores the raw input, not the transformed value
}
