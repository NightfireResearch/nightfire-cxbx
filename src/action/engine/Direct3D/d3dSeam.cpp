#include "d3dSeam.h"
#include "d3dhelpers.h"
#include "../../actionhelpers.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

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
#define D3DDevice_SetGammaRamp_ADDR             0x001038f0u
#define D3DDevice_SetViewport_ADDR              0x00103d50u
#define D3DDevice_Clear_ADDR                    0x001043e0u
#define D3DResource_Release_ADDR                0x00104fa0u
#define D3DTexture_GetSurfaceLevel2_ADDR        0x00105130u
#define D3DDevice_SetRenderState_FogColor_ADDR  0x00100960u
#define D3DDevice_SetRenderState_YuvEnable_ADDR 0x00101c20u
#define D3DDevice_Swap_ADDR                     0x00103730u
#define D3DDevice_SetTexture_ADDR               0x00103eb0u
#define D3DDevice_SetVertexShaderConstant1_ADDR 0x00102570u
#define D3DDevice_SetDepthClipPlanes_ADDR       0x001019f0u
#define D3DDevice_SetStreamSource_ADDR          0x001027a0u

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

// D3DDevice_SetGammaRamp(flags, pRamp) and D3DResource_Release(pResource) - both confirmed plain __stdcall
// via raw disassembly (RET 0x8 / RET 0x4 respectively, matching their param counts exactly).
typedef void(__stdcall *D3DDevice_SetGammaRampFn)(uint32_t flags, void *pRamp);
#define D3DDevice_SetGammaRamp ((D3DDevice_SetGammaRampFn)D3DDevice_SetGammaRamp_ADDR)

typedef void(__stdcall *D3DResource_ReleaseFn)(void *pResource);
#define D3DResource_Release ((D3DResource_ReleaseFn)D3DResource_Release_ADDR)

// D3DDevice_SetViewport(pViewport) - confirmed plain __stdcall (RET 0x4). D3DVIEWPORT here is the standard
// D3D8 viewport struct (X, Y, Width, Height, MinZ, MaxZ), not something Eurocom-specific.
struct D3DVIEWPORT {
    uint32_t X, Y, Width, Height;
    float MinZ, MaxZ;
};
typedef void(__stdcall *D3DDevice_SetViewportFn)(D3DVIEWPORT *pViewport);
#define D3DDevice_SetViewport ((D3DDevice_SetViewportFn)D3DDevice_SetViewport_ADDR)

// D3DDevice_Clear(rectCount, pRects, flags, colour, z, stencil) - Ghidra's own prototype only reports 5
// params (20 bytes), but the function's own RET 0x18 cleans up 24 bytes, and the d3dClear call site (see
// below) pushes exactly 6 dwords. It's a completely standard D3D8 Clear() signature - Ghidra just missed the
// trailing stencil parameter (always 0 at this call site, which is likely why its analysis folded it away).
typedef void(__stdcall *D3DDevice_ClearFn)(uint32_t rectCount, void *pRects, uint32_t flags, uint32_t colour, float z, uint32_t stencil);
#define D3DDevice_Clear ((D3DDevice_ClearFn)D3DDevice_Clear_ADDR)

// All four below confirmed plain __stdcall via raw disassembly (RET immediate matches param count exactly).
typedef void(__stdcall *D3DTexture_GetSurfaceLevel2Fn)(void *pTexture, uint32_t level);
#define D3DTexture_GetSurfaceLevel2 ((D3DTexture_GetSurfaceLevel2Fn)D3DTexture_GetSurfaceLevel2_ADDR)

typedef void(__stdcall *D3DDevice_SetRenderState_FogColorFn)(uint32_t colour);
#define D3DDevice_SetRenderState_FogColor ((D3DDevice_SetRenderState_FogColorFn)D3DDevice_SetRenderState_FogColor_ADDR)

typedef void(__stdcall *D3DDevice_SetRenderState_YuvEnableFn)(uint32_t enable);
#define D3DDevice_SetRenderState_YuvEnable ((D3DDevice_SetRenderState_YuvEnableFn)D3DDevice_SetRenderState_YuvEnable_ADDR)

typedef void(__stdcall *D3DDevice_SwapFn)(uint32_t type);
#define D3DDevice_Swap ((D3DDevice_SwapFn)D3DDevice_Swap_ADDR)

typedef void(__stdcall *D3DDevice_SetTextureFn)(uint32_t stage, void *pTexture);
#define D3DDevice_SetTexture ((D3DDevice_SetTextureFn)D3DDevice_SetTexture_ADDR)

// Eurocom's own frame-timing helper (Global namespace, not D3D8::) - already AUTOGEN-declared (and its stub
// body generated) via game.cpp; just a plain forward declaration here so d3dSwap can call it too, without
// asking preprocess.py to generate a second, colliding body for it.
double timestamp(void);

// D3DDevice_SetVertexShaderConstant1(constant index in ECX, pointer to 4 floats in EDX) - confirmed via raw
// disassembly to take exactly two register arguments and no stack arguments (plain RET, no immediate). Unlike
// D3D_SetRenderStateSimple above, this exactly matches MSVC's own __fastcall ABI for a 2-argument function
// (first two register-sized args in ECX/EDX, callee-cleans-up-nothing since nothing was pushed), so a plain
// __fastcall function pointer works here without needing a hand-written asm trampoline.
typedef void(__fastcall *D3DDevice_SetVertexShaderConstant1Fn)(uint32_t constantIndex, float *pConstants);
#define D3DDevice_SetVertexShaderConstant1 ((D3DDevice_SetVertexShaderConstant1Fn)D3DDevice_SetVertexShaderConstant1_ADDR)

// D3DDevice_SetVertexShaderConstant4(constant index in ECX, pointer to one D3DMATRIX - 16 floats - in EDX) -
// confirmed via raw disassembly: reads exactly one MMX-copied D3DMATRIX through EDX, no stack args, plain RET.
// Same genuine __fastcall match as SetVertexShaderConstant1 above.
typedef void(__fastcall *D3DDevice_SetVertexShaderConstant4Fn)(uint32_t constantIndex, void *pMatrix);
#define D3DDevice_SetVertexShaderConstant4 ((D3DDevice_SetVertexShaderConstant4Fn)0x001025d0u)

// D3DDevice_SetVertexShaderConstantNotInline(constant index in ECX, pointer in EDX, count-in-dwords on the
// stack) - confirmed via raw disassembly: exactly matches MSVC's own __fastcall ABI for a 3-argument function
// (first two register args, third stacked, callee cleans up RET 0x4) - another case needing no asm trampoline.
typedef void(__fastcall *D3DDevice_SetVertexShaderConstantNotInlineFn)(uint32_t constantIndex, void *pData, uint32_t countDwords);
#define D3DDevice_SetVertexShaderConstantNotInline ((D3DDevice_SetVertexShaderConstantNotInlineFn)0x00102760u)

// D3DDevice_SetDepthClipPlanes(uint, uint, uint) and D3DDevice_SetStreamSource(int streamNumber, void*
// vertexBuffer, int stride) - both confirmed plain __stdcall via raw disassembly (RET 0xc, matching 3 params).
typedef void(__stdcall *D3DDevice_SetDepthClipPlanesFn)(uint32_t param1, uint32_t param2, uint32_t param3);
#define D3DDevice_SetDepthClipPlanes ((D3DDevice_SetDepthClipPlanesFn)D3DDevice_SetDepthClipPlanes_ADDR)

typedef void(__stdcall *D3DDevice_SetStreamSourceFn)(int streamNumber, void *vertexBuffer, int stride);
#define D3DDevice_SetStreamSource ((D3DDevice_SetStreamSourceFn)D3DDevice_SetStreamSource_ADDR)

// ---------------------------------------------------------------------------------------------------------------
// Pure-math Eurocom matrix helpers (Global namespace, not D3D8::) - never previously declared/called from any
// reimplemented code in this project, so first-time AUTOGEN forward declarations rather than plain ones (see
// timestamp() above for the contrast - that one already had a body generated elsewhere). All confirmed via raw
// disassembly to be plain __cdecl, stack-only arguments, no register-convention surprises. We call these but
// deliberately don't reimplement or need to understand their internals - they remain completely untouched.
// ---------------------------------------------------------------------------------------------------------------
// AUTOGEN
void maybeD3DMATRIXcopy(undefined4 *dest, undefined4 *src);
// AUTOGEN
void d3dMatrixIdentity(D3DMATRIX *mtx);
// AUTOGEN
void maybeMultiplyMatrixChain(D3DMATRIX *mtxOut, D3DMATRIX *base, MatrixChainNode *mtxChain);

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

// Gfx.D3DDevice (confirmed via get_struct_layout: GraphicsSystem+28) - the actual D3D8 device handle/pointer,
// null until xboxInitGraphics has created it. Every function below that touches D3D8 guards on this exactly
// like the original did, and no-ops (while still updating any cache) if it's not ready yet.
#define D3D_DeviceReady U32_AT(0x002C576C)

#define Gfx_ViewportX U32_AT(0x002C6F74) // Gfx.viewportX
#define Gfx_ViewportY U32_AT(0x002C6F78) // Gfx.viewportY
#define Gfx_ViewportWidth U32_AT(0x002C6F7C) // Gfx.viewportWidth
#define Gfx_ViewportHeight U32_AT(0x002C6F80) // Gfx.viewportHeight

// D3D8's own internal pushbuffer-dirty-flags word, and its own cached fog-enable flag - both plain globals
// living inside the D3D8 library's static data (not the Gfx struct), poked directly by the original rather
// than through an API call. Confirmed via raw disassembly to be ordinary memory, safe to preserve verbatim.
#define D3D8_PushBufferDirtyFlags U32_AT(0x001117CC)
#define D3D8_RS_FogEnable         U32_AT(0x00111B40)

#define Gfx_CurrentlyLoadedTexture U32_AT(0x002C6F84) // Gfx.currentlyLoadedTexture (stage 0)
#define Gfx_TexStage1SlotCache     U32_AT(0x002C6F94) // Gfx.field6053_0x1844
#define Gfx_TexStage1Param2Cache   U32_AT(0x002C6F98) // Gfx.field6054_0x1848
#define Gfx_TexStage1ModeFlags     U32_AT(0x002FF3A4) // not in the Gfx struct - a separate global

// Opaque D3D8-internal texture-stage-1 configuration registers, poked directly with fixed constants by the
// original depending on whether a real texture or NULL is being bound to stage 1 - untraced meaning, ported
// verbatim rather than guessed at.
#define D3D8_TexStage1_0x00 U32_AT(0x00111800)
#define D3D8_TexStage1_0x08 U32_AT(0x00111808)
#define D3D8_TexStage1_0x10 U32_AT(0x00111810)
#define D3D8_TexStage1_0x80 U32_AT(0x00111880)
#define D3D8_TexStage1_0x88 U32_AT(0x00111888)
#define D3D8_TexStage1_0x8c U32_AT(0x0011188C)
#define D3D8_TexStage1_0x90 U32_AT(0x00111890)
#define D3D8_TexStage1_0x98 U32_AT(0x00111898)
#define D3D8_TexStage1_0x9c U32_AT(0x0011189C)

#define Gfx_FogEnabledCache U32_AT(0x002C6FBC) // Gfx.field6063_0x186c
#define Gfx_FogModeFlag     U32_AT(0x002C6F90) // Gfx.field6052_0x1840 - untraced meaning; see d3dSetFogEnable/Color
#define Gfx_FogColorCache   U32_AT(0x002FF450) // not in the Gfx struct - a separate global
#define Gfx_FogColorMasked  U32_AT(0x002C6FC0) // Gfx._6256_4_ - the colour D3D8 actually gets told about

#define Gfx_SwapPending           U8_AT(0x002C5754)     // Gfx.field1_0x4
#define Gfx_LastSwapTimestamp     DOUBLE_AT(0x002FF438) // not in the Gfx struct - a separate global
#define Gfx_AccumulatedSwapTime   DOUBLE_AT(0x002FF440) // not in the Gfx struct - a separate global
#define Gfx_AccumulatedSwapTimeReport DOUBLE_AT(0x002FF448) // not in the Gfx struct - the accumulator's value just before each reset, for reporting

// Shader constant 0x67: a packed 0xAARRGGBB colour, unpacked byte-by-byte through Gfx.u8tofloat01 (a 256-entry
// byte-to-[0,1]-float lookup table) into 4 shader-constant floats. Exact use (tint/ambient colour) untraced.
#define Gfx_ColorConstant67Cache U32_AT(0x002C6FD4) // Gfx.field6072_0x1884
#define D3D8_ForceColorConstant67Update U8_AT(0x001B52DC) // not in the Gfx struct - forces a re-send even if the cache matches (e.g. after a device reset)
#define Gfx_U8ToFloat01(byteValue) (((float*)0x002FECEC)[(uint8_t)(byteValue)]) // Gfx.u8tofloat01[256]

// Shader constant 0x66: a fog {1/(far-near), near/(far-near)} pair, shared between the near and far setters -
// whichever is called, both cached scaled values get re-read and the pair recomputed the same way.
#define Gfx_FogScale       FLOAT_AT(0x002FF26C) // not in the Gfx struct - a separate global, set elsewhere
#define Gfx_FogScaledNear  FLOAT_AT(0x002FF34C)
#define Gfx_FogScaledFar   FLOAT_AT(0x002FF350)
#define Gfx_FogNearFarDelta FLOAT_AT(0x002FF354)
#define Gfx_FogConstant66  ((float*)0x002FF33C) // [0] = 1/delta (or a sentinel if delta is ~0), [1] = [0]*scaledNear

// Shader constant 0x75: {0, 0, characterLightIntensity, 1-characterLightIntensity}. The first two floats are
// zeroed once per frame by d3dBeginFrame; gfxSetCharacterLightIntensity only ever touches the last two.
#define Gfx_ShaderConstant75 ((float*)0x002FF378)
#define Gfx_MiscModeFlags U32_AT(0x002FF3A4) // not in the Gfx struct - shared small state-flags word (also used by d3dSetTextureStage1's 0x20 bit; this function uses bit 0x10)

// d3dBeginFrame-only globals - none of these are really part of the Gfx struct despite how Ghidra's own
// decompile of this particular function mis-labelled a couple of them (it even flags the mismatch itself:
// "WARNING: Globals starting with '_' overlap smaller symbols at the same address"). Verified via raw
// disassembly instead of trusting that decompile's variable-to-field mapping here.
#define Gfx_FrameCounter U32_AT(0x002C5758)          // Gfx.field5_0x8
#define Gfx_MiscResetFlag U32_AT(0x002FF3A8)         // not in the Gfx struct - untraced meaning, always set to -1 here
#define Gfx_CurrentStreamBuffer U32_AT(0x002C6F88)   // Gfx.currentStreamBuffer
#define Gfx_CurrentIndexBuffer U32_AT(0x002C6F8C)    // Gfx.currentIndexBuffer

// Projection matrix. Cache A is write-only from here (presumably read by a not-yet-ported function); cache B
// is scaled in place by Gfx_FogScale and feeds the depth-clip-plane calculation - both confirmed via raw
// disassembly, none of this is really "in" the Gfx struct despite how it reads in decompile.
#define Gfx_ProjMatrixCacheA ((D3DMATRIX*)0x002FF0EC)
#define Gfx_ProjMatrixCacheB ((D3DMATRIX*)0x002FF12C)

#define Gfx_d3dActiveMatrix ((D3DMATRIX*)0x002FF22C) // Gfx.d3dActiveMatrix
#define Gfx_MatrixGenFlag1 U32_AT(0x002FF270)        // untraced meaning - always set to -1 by d3dSetMatrix
#define Gfx_MatrixGenFlag2 U32_AT(0x002FF278)        // ditto
#define Gfx_ViewMatrixCache ((D3DMATRIX*)0x002FF1EC) // Gfx.field158568_0x39a9c - the "base" d3dSetMatrix combines the new matrix with
#define Gfx_SecondaryBasisMatrix ((D3DMATRIX*)0x002FF16C) // Gfx.field158566_0x39a1c - feeds constant register 100's half-scaled 2x3 basis

#define Gfx_StreamStrideConstants ((float*)0x002FF358)  // Gfx.field_0x39c08 - 8 floats, shader constant 0x73
#define Gfx_d3dstreamDataPtr ((void**)0x002DECF8)        // Gfx.d3dstreamDataPtr - array of stream-data pointers, stride 9 dwords per slot

// Bit pattern for the fog constant's "avoid divide-by-zero" sentinel value - written as a raw uint32_t by the
// original rather than a float literal, so reproduced bit-for-bit rather than approximated with a decimal one.
static inline float BitsToFloat(uint32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static inline uint32_t FloatToBits(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// Standard row-major 4x4 matrix product (out = base * chain), verified term-by-term against
// maybeMultiplyMatrixChain's own decompiled single-link arithmetic. See d3dSetMatrix's comment for why this is
// implemented directly rather than calling that function.
static void Multiply4x4RowMajor(const D3DMATRIX *base, const D3DMATRIX *chain, D3DMATRIX *out) {
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            out->f[row * 4 + col] = base->f[row * 4 + 0] * chain->f[0 * 4 + col]
                                   + base->f[row * 4 + 1] * chain->f[1 * 4 + col]
                                   + base->f[row * 4 + 2] * chain->f[2 * 4 + col]
                                   + base->f[row * 4 + 3] * chain->f[3 * 4 + col];
        }
    }
}

static void RecomputeFogConstant66() {
    float delta = Gfx_FogNearFarDelta;
    float invDelta = (delta <= -0.001f || delta >= 0.001f) ? (1.0f / delta) : BitsToFloat(0x4479ffff);
    Gfx_FogConstant66[0] = invDelta;
    Gfx_FogConstant66[1] = invDelta * Gfx_FogScaledNear;
}

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

// ---------------------------------------------------------------------------------------------------------------
// Gamma ramp, viewport, clear, resource release
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void ConfigureGammaRamp(float gamma, float brightness, float contrast) {
    // Builds a 256-entry-per-channel gamma ramp from a gamma/brightness/contrast-style triplet. Xbox's
    // D3DDevice_SetGammaRamp here expects a packed 768-byte buffer (256 bytes red, then 256 green, then 256
    // blue, one byte per entry) - confirmed by the original's byte-sized writes into three stack buffers that
    // sit contiguously in that exact order; NOT the 16-bit-per-entry D3DGAMMARAMP PC D3D8/D3D9 normally uses.
    // All three channels get the identical value per index (r/g/b here are single global adjustment knobs,
    // not per-channel curves), so building one array and copying it three times is simplest and byte-for-byte
    // equivalent to relying on stack layout the way the original did.
    uint8_t gammaRamp[768];
    for (int i = 0; i < 256; i++) {
        double gammaCorrected = pow((double)gamma, (double)i * (1.0 / 255.0));
        double value = (gammaCorrected * 255.0 - 128.0) * brightness + 128.0 + (contrast - 1.0);
        if (value < 0.0) value = 0.0;
        else if (value > 255.0) value = 255.0;
        uint8_t byteValue = (uint8_t)(value + 0.5); // round to nearest, matching the original's __ftol2(x+0.5)

        gammaRamp[i] = byteValue;
        gammaRamp[256 + i] = byteValue;
        gammaRamp[512 + i] = byteValue;
    }

    if (D3D_DeviceReady != 0)
        D3DDevice_SetGammaRamp(0, gammaRamp);
}

// AUTOINJECT
void D3DResourceRelease(void *resource) {
    if (resource != NULL)
        D3DResource_Release(resource);
}

// AUTOINJECT
void d3dSetupViewportDimensions(unsigned int viewportX, unsigned int viewportY, unsigned int viewportWidth, unsigned int viewportHeight) {
    if ((int)viewportWidth < 2) viewportWidth = 2;
    if ((int)viewportHeight < 2) viewportHeight = 2;

    Gfx_ViewportX = viewportX;
    Gfx_ViewportY = viewportY;
    Gfx_ViewportWidth = viewportWidth;
    Gfx_ViewportHeight = viewportHeight;

    if (D3D_DeviceReady != 0) {
        D3DVIEWPORT viewport;
        viewport.X = viewportX;
        viewport.Y = viewportY;
        viewport.Width = viewportWidth;
        viewport.Height = viewportHeight;
        viewport.MinZ = 0.0f;
        viewport.MaxZ = 1.0f;
        D3DDevice_SetViewport(&viewport);
    }
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dClear(unsigned int colour, bool clearTarget, bool clearZStencil) {
    uint32_t flags = 0;
    if (clearTarget) flags = 0xf0;
    if (clearZStencil) flags |= 3;

    if (D3D_DeviceReady != 0)
        D3DDevice_Clear(0, NULL, flags, colour & 0xffffffu, 1.0f, 0);
    Gfx_D3DLastError = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Texture binding, fog, swap
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void d3dGetTextureSurfaceLevel0(int textureSlot) {
    void *baseTexture = D3DTextureSlot(textureSlot)->baseTexture;
    if (baseTexture != NULL)
        D3DTexture_GetSurfaceLevel2(baseTexture, 0);
}

// AUTOINJECT
void d3dSetTextureStage0(int textureSlot) {
    if (Gfx_CurrentlyLoadedTexture == (uint32_t)textureSlot)
        return;
    Gfx_CurrentlyLoadedTexture = (uint32_t)textureSlot;

    if (D3D_DeviceReady != 0)
        D3DDevice_SetTexture(0, D3DTextureSlot(textureSlot)->baseTexture);
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSetTextureStage1(int textureSlot, int param2) {
    if (Gfx_TexStage1SlotCache == (uint32_t)textureSlot && Gfx_TexStage1Param2Cache == (uint32_t)param2)
        return;
    Gfx_TexStage1Param2Cache = (uint32_t)param2;
    Gfx_TexStage1SlotCache = (uint32_t)textureSlot;

    bool deviceReady = (D3D_DeviceReady != 0);
    void *baseTexture = NULL;

    if (textureSlot != 0) {
        Gfx_TexStage1ModeFlags |= 0x20;
        if (deviceReady) {
            D3D8_PushBufferDirtyFlags |= 0x800;
            baseTexture = D3DTextureSlot(textureSlot)->baseTexture;
            D3D8_TexStage1_0x00 = 3;
            D3D8_TexStage1_0x88 = 2;
            D3D8_TexStage1_0x80 = 5;
            D3D8_TexStage1_0x8c = 1;
            D3D8_TexStage1_0x98 = 2;
            D3D8_TexStage1_0x90 = 4;
            D3D8_TexStage1_0x9c = 1;
        }
    } else {
        Gfx_TexStage1ModeFlags &= ~0x20u;
        if (deviceReady) {
            D3D8_PushBufferDirtyFlags |= 0x800;
            D3D8_TexStage1_0x00 = 5;
            D3D8_TexStage1_0x10 = 4;
            D3D8_TexStage1_0x80 = 1;
            D3D8_TexStage1_0x90 = 1;
        }
    }

    if (deviceReady) {
        D3D8_TexStage1_0x08 = 2;
        D3DDevice_SetTexture(1, baseTexture); // always stage 1, in both branches - matches the original exactly
    }
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSetFogEnable(int enable) {
    bool enabled = (enable != 0);
    bool deviceReady = (D3D_DeviceReady != 0);
    Gfx_FogEnabledCache = enabled;

    if (deviceReady) {
        D3D8_PushBufferDirtyFlags |= 0x2000;
        D3D8_RS_FogEnable = enabled;
    }

    if (enabled) {
        // Gfx_FogModeFlag's exact meaning hasn't been traced - when set, it forces the fog colour actually
        // sent to D3D8 to 0 rather than the real cached colour, matching the original's mask logic exactly.
        uint32_t mask = (Gfx_FogModeFlag != 0) ? 0u : 0xFFFFFFFFu;
        uint32_t maskedColor = Gfx_FogColorCache & mask;
        Gfx_FogColorMasked = maskedColor;
        if (deviceReady)
            D3DDevice_SetRenderState_FogColor(maskedColor);
    }
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSetFogColor(unsigned int colour) {
    uint32_t maskedInput = colour & 0xffffffu;
    Gfx_FogColorCache = maskedInput;

    if (Gfx_FogEnabledCache == 1) {
        uint32_t mask = (Gfx_FogModeFlag != 0) ? 0u : 0xFFFFFFFFu;
        uint32_t maskedColor = maskedInput & mask;
        Gfx_FogColorMasked = maskedColor;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetRenderState_FogColor(maskedColor);
        Gfx_D3DLastError = 0;
    }
}

// AUTOINJECT
void d3dSetYuvEnable(int enable) {
    if (D3D_DeviceReady != 0)
        D3DDevice_SetRenderState_YuvEnable(enable != 0);
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSwap(void) {
    if (Gfx_SwapPending == 0)
        return;
    Gfx_SwapPending = 0;

    double now = timestamp();
    Gfx_D3DLastError = 0;
    Gfx_AccumulatedSwapTime += (now - Gfx_LastSwapTimestamp);
    Gfx_LastSwapTimestamp = now;

    if (D3D_DeviceReady != 0)
        D3DDevice_Swap(0);
    Gfx_D3DLastError = 0;

    Gfx_LastSwapTimestamp = timestamp(); // called again, unconditionally, matching the original exactly
}

// ---------------------------------------------------------------------------------------------------------------
// Shader constants (all via the __fastcall D3DDevice_SetVertexShaderConstant1 above)
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void d3dSetColorConstant67(unsigned int packedColour) {
    if (D3D8_ForceColorConstant67Update == 0 && Gfx_ColorConstant67Cache == packedColour)
        return;
    Gfx_ColorConstant67Cache = packedColour;
    D3D8_ForceColorConstant67Update = 0;

    float constants[4];
    constants[0] = Gfx_U8ToFloat01((packedColour >> 16) & 0xFF); // R
    constants[1] = Gfx_U8ToFloat01((packedColour >> 8) & 0xFF);  // G
    constants[2] = Gfx_U8ToFloat01(packedColour & 0xFF);         // B
    constants[3] = Gfx_U8ToFloat01((packedColour >> 24) & 0xFF); // A

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstant1(0x67, constants);
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSetFogNear(float near_) {
    Gfx_FogScaledNear = Gfx_FogScale * near_;
    Gfx_FogNearFarDelta = Gfx_FogScaledFar - Gfx_FogScaledNear;
    RecomputeFogConstant66();

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstant1(0x66, Gfx_FogConstant66);
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSetFogFar(float far_) {
    Gfx_FogScaledFar = Gfx_FogScale * far_;
    Gfx_FogNearFarDelta = Gfx_FogScaledFar - Gfx_FogScaledNear;
    RecomputeFogConstant66();

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstant1(0x66, Gfx_FogConstant66);
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void gfxSetCharacterLightIntensity(float intensity) {
    // Translated literally from the original's own comparison (matches its plate comment: sets the flag for
    // positive values or NaN, clears/clamps-to-zero for negative values or exact zero).
    if ((intensity < 0.0f) == (intensity == 0.0f)) {
        Gfx_MiscModeFlags |= 0x10;
    } else {
        intensity = 0.0f;
        Gfx_MiscModeFlags &= ~0x10u;
    }
    Gfx_ShaderConstant75[2] = intensity;
    Gfx_ShaderConstant75[3] = 1.0f - intensity;

    if ((Gfx_MiscModeFlags & 0x10) != 0) {
        if (D3D_DeviceReady != 0)
            D3DDevice_SetVertexShaderConstant1(0x75, Gfx_ShaderConstant75);
        Gfx_D3DLastError = 0;
    }
}

// Per-frame reset, paired with d3dSwap (which clears Gfx_SwapPending; this sets it, and no-ops if a frame is
// already pending). Resets stream/index-buffer caches, the frame counter, and the first two floats of shader
// constant 0x75 (gfxSetCharacterLightIntensity owns the other two).
//
// AUTOINJECT
void d3dBeginFrame(void) {
    if (Gfx_SwapPending != 0)
        return;
    Gfx_SwapPending = 1;

    double now = timestamp();
    Gfx_D3DLastError = 0;
    Gfx_AccumulatedSwapTimeReport = (now - Gfx_LastSwapTimestamp) + Gfx_AccumulatedSwapTime;
    Gfx_AccumulatedSwapTime = 0.0;
    Gfx_LastSwapTimestamp = now;

    Gfx_LastSwapTimestamp = timestamp(); // called again, unconditionally, matching d3dSwap's own pattern

    Gfx_MiscResetFlag = 0xFFFFFFFFu;
    Gfx_CurrentStreamBuffer = 0xFFFFFFFFu;
    Gfx_CurrentIndexBuffer = 0xFFFFFFFFu;
    Gfx_FrameCounter = Gfx_FrameCounter + 1;
    Gfx_ShaderConstant75[0] = 0.0f;
    Gfx_ShaderConstant75[1] = 0.0f;

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstant1(0x75, Gfx_ShaderConstant75);
    Gfx_D3DLastError = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Matrices and stream sources
// ---------------------------------------------------------------------------------------------------------------

// Sets the projection matrix, caches two copies of it, and derives Gfx_FogScale (also used by d3dSetFogNear/
// Far) plus the near/far depth-clip planes from it. Not yet named/traced beyond that - untraced why two
// separate cached copies exist (cache A is never read again within this function).
//
// AUTOINJECT
void d3dSetProjectionMatrix(D3DMATRIX *projMtx) {
    memcpy(Gfx_ProjMatrixCacheA, projMtx, sizeof(D3DMATRIX));
    memcpy(Gfx_ProjMatrixCacheB, projMtx, sizeof(D3DMATRIX));

    D3DMATRIX *cacheB = Gfx_ProjMatrixCacheB;
    float scaleBasis = (cacheB->f[14] + cacheB->f[15]) * ((cacheB->f[15] - cacheB->f[11]) / (cacheB->f[10] - cacheB->f[14]));
    if (scaleBasis == 0.0f)
        scaleBasis = 1.0f;
    Gfx_FogScale = 16777215.0f / scaleBasis;

    for (int i = 0; i < 16; i++)
        cacheB->f[i] *= Gfx_FogScale;

    // Ghidra's decompile shows this arg with a "(uint)" cast, but the actual instruction storing it (FSTP, not
    // FISTP) confirms it's a raw float bit-pattern reinterpreted as a uint for the call - not a value-
    // converting int cast. Confirmed this was wrong before the fix (real, observed bug: frustum culling was
    // rejecting fully-visible objects and admitting partially-offscreen ones - a numerically-converted value
    // here is wildly different from the real bit pattern the NV2A register actually wants).
    float depthClipNear = (-(projMtx->f[11] / projMtx->f[10]) * 16777215.0f) / -(projMtx->f[11] / (projMtx->f[10] - 1.0f));
    D3DDevice_SetDepthClipPlanes(FloatToBits(depthClipNear), 0x4b7fffffu, 1);
}

// Sets the active matrix (shader constant 0x60, combined with the cached "view" matrix), plus a secondary
// half-scaled 2x3 basis constant (register 100) derived from a separately-cached matrix.
//
// The combine step deliberately does NOT call maybeMultiplyMatrixChain, despite that being what the original
// decompile shows. That function casts its third argument to MatrixChainNode* and, for anything beyond a
// single link, walks a "next chain link" pointer read from a local scratch/ping-pong buffer table whose exact
// construction we were never able to fully verify from disassembly alone - and empirically, feeding it a
// properly NULL-terminated single-link node (confirmed via Ghidra's emulator not to fault) still produced
// wrong, non-crashing results here: a real, confirmed bug (off-origin/rotating objects transforming
// incorrectly, wrong frustum culling) that survived two separate "pad the chain node" fixes at different call
// sites (d3dSetWorldMatrix, and the pre-existing psiDrawObjectMatrix in psiGraphics.cpp). Rather than keep
// guessing at that function's internals, the single-link multiply itself is plain, unambiguous arithmetic we
// DID fully verify against the decompile (output = base * chain, standard row-major 4x4 product) - so it's
// implemented directly below instead, sidestepping the chain-walk risk entirely.
//
// AUTOINJECT
void d3dSetMatrix(D3DMATRIX *d3dMtx) {
    memcpy(Gfx_d3dActiveMatrix, d3dMtx, sizeof(D3DMATRIX));
    Gfx_MatrixGenFlag1 = 0xFFFFFFFFu;
    Gfx_MatrixGenFlag2 = 0xFFFFFFFFu;

    D3DMATRIX combined;
    Multiply4x4RowMajor(Gfx_ViewMatrixCache, d3dMtx, &combined);

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstant4(0x60, &combined);
    Gfx_D3DLastError = 0;

    D3DMATRIX secondaryBasis;
    maybeD3DMATRIXcopy((undefined4 *)&secondaryBasis, (undefined4 *)Gfx_SecondaryBasisMatrix);

    float constants[8];
    constants[0] = secondaryBasis.f[0] * 0.5f;
    constants[1] = secondaryBasis.f[4] * 0.5f;
    constants[2] = secondaryBasis.f[8] * 0.5f;
    constants[3] = 0.5f;
    constants[4] = secondaryBasis.f[1] * 0.5f;
    constants[5] = secondaryBasis.f[5] * 0.5f;
    constants[6] = secondaryBasis.f[9] * 0.5f;
    constants[7] = 0.5f;

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstantNotInline(100, constants, 8);
    Gfx_D3DLastError = 0;
}

// Splits a world matrix into a translation-only matrix (forwarded to d3dSetMatrix) and a rotation-only 3x4
// block (translation column zeroed, sent directly as shader constant 0 - likely for transforming normals
// without translation affecting them).
//
// AUTOINJECT
void d3dSetWorldMatrix(D3DMATRIX *worldMtx) {
    // No MatrixChainNode padding needed here - d3dSetMatrix computes its combine step directly rather than
    // going through maybeMultiplyMatrixChain (see its own comment for why), so a plain D3DMATRIX is safe.
    D3DMATRIX translationOnly;
    d3dMatrixIdentity(&translationOnly);
    translationOnly.f[3] = worldMtx->f[3];
    translationOnly.f[7] = worldMtx->f[7];
    translationOnly.f[11] = worldMtx->f[11];
    d3dSetMatrix(&translationOnly);

    float constants[12];
    memcpy(constants, worldMtx, sizeof(constants));
    constants[3] = 0.0f;
    constants[7] = 0.0f;
    constants[11] = 0.0f;

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstantNotInline(0, constants, 0xc);
    Gfx_D3DLastError = 0;
}

// AUTOINJECT
void d3dSetStreamSources(int baseIndex, int stream1Offset, float stream1Stride, int stream2Offset, float stream2Stride,
                          int stream3Offset, float stream3Stride, int stream4Offset, float stream4Stride,
                          int stream5Offset, float stream5Stride, int stream6Offset, float stream6Stride,
                          int stream7Offset, float stream7Stride, int stream8Offset, float stream8Stride) {
    if (baseIndex == 0) {
        Gfx_MiscModeFlags &= ~0x1u;
        for (int stream = 1; stream <= 8 && D3D_DeviceReady != 0; stream++) {
            D3DDevice_SetStreamSource(stream, NULL, 6);
            Gfx_D3DLastError = 0;
        }
        Gfx_D3DLastError = 0;
        return;
    }

    // Strides are stored pre-scaled by a fixed 1/32768-ish constant, matching the original exactly.
    Gfx_StreamStrideConstants[0] = stream1Stride * 3.051851e-05f;
    Gfx_StreamStrideConstants[1] = stream2Stride * 3.051851e-05f;
    Gfx_StreamStrideConstants[2] = stream3Stride * 3.051851e-05f;
    Gfx_StreamStrideConstants[3] = stream4Stride * 3.051851e-05f;
    Gfx_StreamStrideConstants[4] = stream5Stride * 3.051851e-05f;
    Gfx_StreamStrideConstants[5] = stream6Stride * 3.051851e-05f;
    Gfx_StreamStrideConstants[6] = stream7Stride * 3.051851e-05f;
    Gfx_StreamStrideConstants[7] = stream8Stride * 3.051851e-05f;

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstantNotInline(0x73, Gfx_StreamStrideConstants, 8);

    Gfx_MiscModeFlags |= 0x1;
    Gfx_D3DLastError = 0;

    int streamOffsets[8] = { stream1Offset, stream2Offset, stream3Offset, stream4Offset,
                              stream5Offset, stream6Offset, stream7Offset, stream8Offset };
    for (int i = 0; i < 8 && D3D_DeviceReady != 0; i++) {
        void *dataPtr = Gfx_d3dstreamDataPtr[(streamOffsets[i] + baseIndex) * 9];
        D3DDevice_SetStreamSource(i + 1, dataPtr, 6);
        Gfx_D3DLastError = 0;
    }
    Gfx_D3DLastError = 0;
}
