#include "d3dSeam.h"
#include "d3dhelpers.h"
#include "../../actionhelpers.h"
#include "../XboxSettings.h" // GetVideoMode/XboxGetAVRegion, for xboxInitGraphics
#include "d3d9Backend.h"      // the native D3D9 backend the entry-point wrappers can dispatch to

#include <stdint.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // VirtualQuery, for the uncached-alias probe in D3D_UncachedAliasOf
#include <math.h>
#include <stdio.h>
#include <type_traits>

// ---------------------------------------------------------------------------------------------------------------
// TEMPORARY tracing of every D3D8 entry point this seam calls and every D3D8-internal state slot it pokes -
// the inventory pass for the eventual non-CXBX graphics backend. Set D3DSEAM_TRACE to 0 to compile it all out
// (every D3D8_*/D3DDevice_* macro then collapses back to the plain pointer / U32_AT it was before).
//
// Two things are recorded, both to d3dSeam_trace.log in the working directory:
//  - a running table per entry point / state slot: call count plus the set of distinct "key" values seen
//    (first argument for calls - register index, stage, method...; the written value for state pokes), dumped
//    every D3DSEAM_TRACE_SUMMARY_EVERY frames and on every level reset;
//  - a full per-call listing (all arguments) for the first D3DSEAM_TRACE_DETAIL_FRAMES frames after boot and
//    after every level reset, so the log stays bounded no matter how long the game runs.
// ---------------------------------------------------------------------------------------------------------------
#ifndef D3DSEAM_TRACE
#define D3DSEAM_TRACE 0
#endif
#define D3DSEAM_TRACE_DETAIL_FRAMES 4
#define D3DSEAM_TRACE_DETAIL_DELAY  600  // frames after a level reset before the detailed window opens (past the loading screen)
#define D3DSEAM_TRACE_SUMMARY_EVERY 1000

#if D3DSEAM_TRACE

struct D3DSeamTraceEntry {
    const char *name;
    uint32_t calls;
    uint32_t distinctCount;
    uint32_t distinct[24];
    bool overflow;
};
static D3DSeamTraceEntry g_d3dTraceEntries[200];
static int g_d3dTraceEntryCount = 0;
static uint32_t g_d3dTraceFrame = 0;
static int g_d3dTraceDetailFramesLeft = D3DSEAM_TRACE_DETAIL_FRAMES;
static int g_d3dTraceDetailDelay = 0; // frames to wait before the next detailed window opens (see D3DSeamTraceLevelReset)
static FILE *g_d3dTraceFile = NULL;

static FILE *D3DSeamTraceFile(void) {
    if (g_d3dTraceFile == NULL)
        g_d3dTraceFile = fopen("d3dSeam_trace.log", "w");
    return g_d3dTraceFile;
}

static D3DSeamTraceEntry *D3DSeamTraceFind(const char *name) {
    for (int i = 0; i < g_d3dTraceEntryCount; i++) {
        if (g_d3dTraceEntries[i].name == name || strcmp(g_d3dTraceEntries[i].name, name) == 0)
            return &g_d3dTraceEntries[i];
    }
    if (g_d3dTraceEntryCount >= (int)(sizeof(g_d3dTraceEntries) / sizeof(g_d3dTraceEntries[0])))
        return NULL;
    D3DSeamTraceEntry *e = &g_d3dTraceEntries[g_d3dTraceEntryCount++];
    memset(e, 0, sizeof(*e));
    e->name = name;
    return e;
}

static void D3DSeamTraceRecord(const char *name, uint32_t key, const char *detail) {
    D3DSeamTraceEntry *e = D3DSeamTraceFind(name);
    if (e != NULL) {
        e->calls++;
        bool seen = false;
        for (uint32_t i = 0; i < e->distinctCount; i++) {
            if (e->distinct[i] == key) { seen = true; break; }
        }
        if (!seen) {
            if (e->distinctCount < sizeof(e->distinct) / sizeof(e->distinct[0]))
                e->distinct[e->distinctCount++] = key;
            else
                e->overflow = true;
        }
    }
    if (detail != NULL) {
        FILE *f = D3DSeamTraceFile();
        if (f != NULL)
            fprintf(f, "F%u %s%s\n", g_d3dTraceFrame, name, detail);
    }
}

static void D3DSeamTraceDumpSummary(const char *reason) {
    FILE *f = D3DSeamTraceFile();
    if (f == NULL)
        return;
    fprintf(f, "\n==== summary at frame %u (%s): %d entry points / state slots seen ====\n", g_d3dTraceFrame, reason, g_d3dTraceEntryCount);
    for (int i = 0; i < g_d3dTraceEntryCount; i++) {
        const D3DSeamTraceEntry *e = &g_d3dTraceEntries[i];
        fprintf(f, "  %-48s calls=%-9u keys={", e->name, e->calls);
        for (uint32_t k = 0; k < e->distinctCount; k++)
            fprintf(f, "%s0x%x", k ? ", " : "", e->distinct[k]);
        fprintf(f, "%s}\n", e->overflow ? ", ..." : "");
    }
    fprintf(f, "====\n\n");
    fflush(f);
}

static bool D3DSeamTraceWantDetail(void) {
    return g_d3dTraceDetailFramesLeft > 0;
}

// Called from d3dSwap (frame boundary) and maybeD3dShutdown (level reset).
static void D3DSeamTraceEndFrame(void) {
    g_d3dTraceFrame++;
    if (g_d3dTraceDetailDelay > 0) {
        if (--g_d3dTraceDetailDelay == 0) {
            g_d3dTraceDetailFramesLeft = D3DSEAM_TRACE_DETAIL_FRAMES;
            if (g_d3dTraceFile != NULL)
                fprintf(g_d3dTraceFile, "==== detailed listing window opens at frame %u ====\n", g_d3dTraceFrame);
        }
    }
    if (g_d3dTraceDetailFramesLeft > 0) {
        g_d3dTraceDetailFramesLeft--;
        if (g_d3dTraceFile != NULL)
            fflush(g_d3dTraceFile);
    }
    if (g_d3dTraceFrame % D3DSEAM_TRACE_SUMMARY_EVERY == 0)
        D3DSeamTraceDumpSummary("periodic");
}
static void D3DSeamTraceLevelReset(void) {
    D3DSeamTraceDumpSummary("level reset");
    FILE *f = D3DSeamTraceFile();
    if (f != NULL)
        fprintf(f, "==== level reset at frame %u - detailed listing window scheduled %d frames from now ====\n",
                g_d3dTraceFrame, D3DSEAM_TRACE_DETAIL_DELAY);
    // Not immediately: the frames right after a reset are the loading screen. Wait until the level is up.
    g_d3dTraceDetailDelay = D3DSEAM_TRACE_DETAIL_DELAY;
}

// One argument, formatted by type: floats as floats, everything else (pointers, handles, enums, packed
// colours) as hex.
template<typename T>
static void D3DSeamTraceAppendArg(char *buf, size_t cap, size_t *pos, T value) {
    if (*pos + 24 >= cap)
        return;
    int n;
    if constexpr (std::is_floating_point_v<T>)
        n = snprintf(buf + *pos, cap - *pos, "%g, ", (double)value);
    else if constexpr (std::is_pointer_v<T>)
        n = snprintf(buf + *pos, cap - *pos, "0x%x, ", (unsigned)(uintptr_t)value);
    else
        n = snprintf(buf + *pos, cap - *pos, "0x%x, ", (unsigned)value);
    if (n > 0)
        *pos += (size_t)n;
}

static inline uint32_t D3DSeamTraceKey(void) { return 0; }
template<typename T, typename... Rest>
static inline uint32_t D3DSeamTraceKey(T first, Rest...) {
    if constexpr (std::is_floating_point_v<T>) return 0;
    else if constexpr (std::is_pointer_v<T>) return (uint32_t)(uintptr_t)first;
    else return (uint32_t)first;
}

template<typename... A>
static void D3DSeamTraceCall(const char *name, A... args) {
    uint32_t key = D3DSeamTraceKey(args...);
    if (!D3DSeamTraceWantDetail()) {
        D3DSeamTraceRecord(name, key, NULL);
        return;
    }
    char detail[256];
    size_t pos = 0;
    detail[pos++] = '(';
    (D3DSeamTraceAppendArg(detail, sizeof(detail), &pos, args), ...);
    if (pos >= 3 && detail[pos - 2] == ',')
        pos -= 2;
    detail[pos++] = ')';
    detail[pos] = '\0';
    D3DSeamTraceRecord(name, key, detail);
}

// Wraps a D3D8-internal state slot (the deferred texture-stage arrays, dirty flags, last-value caches) so
// every write through the macro is recorded. Reads are untouched.
struct D3DSeamTracedU32 {
    const char *name;
    uint32_t *ptr;
    operator uint32_t() const { return *ptr; }
    uint32_t operator=(uint32_t v) const {
        if (D3DSeamTraceWantDetail()) {
            char d[32];
            snprintf(d, sizeof(d), " = 0x%x", v);
            D3DSeamTraceRecord(name, v, d);
        } else {
            D3DSeamTraceRecord(name, v, NULL);
        }
        *ptr = v;
        return v;
    }
    uint32_t operator|=(uint32_t v) const { return operator=(*ptr | v); }
    uint32_t operator&=(uint32_t v) const { return operator=(*ptr & v); }
};
#define D3D8_TRACED(name, addr) (D3DSeamTracedU32{ name, (uint32_t*)(addr) })

#else // !D3DSEAM_TRACE

#define D3D8_TRACED(name, addr) U32_AT(addr)
static inline void D3DSeamTraceEndFrame(void) {}
static inline void D3DSeamTraceLevelReset(void) {}
template<typename... A> static inline void D3DSeamTraceCall(const char *, A...) {}

#endif // D3DSEAM_TRACE

// ---------------------------------------------------------------------------------------------------------------
// Entry-point dispatch. Every D3D8 entry point the seam calls goes through one of these wrappers (via the
// D3DDevice_*/D3DResource_* macros below): it records the call when tracing is on, then either forwards to
// the D3D8 library (CXBX mode) or to the attached D3D9 backend function (d3d9 mode - see d3d9Backend.h). An
// entry point with no backend function attached is counted as missing in d3d9 mode and does nothing. One
// struct per calling convention, since x86 MSVC makes the convention part of the pointer type.
// ---------------------------------------------------------------------------------------------------------------
#define D3DSEAM_TRACED_CALL_TYPE(CONV, SUFFIX)                                                              \
    template<typename R, typename... A> struct D3DSeamTracedCall##SUFFIX {                                  \
        const char *name;                                                                                   \
        R(CONV *fn)(A...);                                                                                  \
        R(*backend)(A...);                                                                                  \
        R operator()(A... args) const {                                                                     \
            D3DSeamTraceCall(name, args...);                                                                \
            if (g_gfxBackend == GFX_BACKEND_D3D9) {                                                         \
                if (backend != NULL)                                                                        \
                    return backend(args...);                                                                \
                D3D9_BackendMissing(name);                                                                  \
                return R();                                                                                 \
            }                                                                                               \
            return fn(args...);                                                                             \
        }                                                                                                   \
    };                                                                                                      \
    template<typename R, typename... A>                                                                     \
    static inline D3DSeamTracedCall##SUFFIX<R, A...> D3DSeamTraced(const char *name, R(CONV *fn)(A...)) {   \
        return { name, fn, NULL };                                                                          \
    }                                                                                                       \
    template<typename R, typename... A>                                                                     \
    static inline D3DSeamTracedCall##SUFFIX<R, A...> D3DSeamTraced(const char *name, R(CONV *fn)(A...),     \
                                                                   R(*backend)(A...)) {                     \
        return { name, fn, backend };                                                                       \
    }
D3DSEAM_TRACED_CALL_TYPE(__stdcall, Std)
D3DSEAM_TRACED_CALL_TYPE(__fastcall, Fast)

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
#define D3DDevice_SetIndices_ADDR               0x00104060u
#define D3DDevice_SetVertexShader_ADDR          0x00102b50u
#define D3DDevice_DrawVerticesUP_ADDR            0x00104860u
#define D3DResource_BlockUntilNotBusy_ADDR      0x001050b0u
#define D3DDevice_GetRenderTarget2_ADDR          0x00103d10u
#define D3DDevice_GetDepthStencilSurface2_ADDR   0x00103d30u
#define D3DDevice_SetRenderTarget_ADDR            0x001038d0u
#define D3DDevice_DrawVertices_ADDR                0x001049c0u
#define D3DDevice_SetRenderState_ZBias_ADDR         0x00100bf0u
#define D3DDevice_SetTextureState_BorderColor_ADDR  0x00100fc0u
#define D3DDevice_SetRenderState_ZEnable_ADDR              0x00101a90u
#define D3DDevice_SetRenderState_NormalizeNormals_ADDR     0x00100a60u
#define D3DDevice_SetRenderState_VertexBlend_ADDR          0x00100df0u
#define D3DDevice_SetShaderConstantMode_ADDR               0x00102910u
#define D3DDevice_SetVertexShaderConstantNotInline_ADDR    0x00102760u
#define D3DDevice_GetBackBuffer2_ADDR                       0x00103c50u

#define Gfx_D3DLastError          U32_AT(0x002C5750) // Gfx.D3DLastError
#define Gfx_TotalTextureBytesUsed U32_AT(0x002C6FE0) // Gfx.field6075_0x1890 - running total, informational only

// D3DDevice_SetRenderState_CullMode(value) and D3DResource_Register(pTexture, data) are both plain __stdcall
// functions taking their arguments on the stack in the normal way - confirmed via raw disassembly, unlike
// D3DDevice_SetRenderState_Simple below.
typedef void(__stdcall *D3DDevice_SetRenderState_CullModeFn)(int value);
#define D3DDevice_SetRenderState_CullMode (D3DSeamTraced("D3DDevice_SetRenderState_CullMode", (D3DDevice_SetRenderState_CullModeFn)D3DDevice_SetRenderState_CullMode_ADDR, D3D9_SetCullMode))

typedef void(__stdcall *D3DResource_RegisterFn)(void *pTexture, uint32_t data);
#define D3DResource_Register (D3DSeamTraced("D3DResource_Register", (D3DResource_RegisterFn)D3DResource_Register_ADDR, D3D9_ResourceRegister))

typedef void(__stdcall *XGSetTextureHeaderFn)(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage,
                                               int format, uint32_t pool, void *pTexture, uint32_t data, uint32_t pitch);
#define XGSetTextureHeader (D3DSeamTraced("XGSetTextureHeader", (XGSetTextureHeaderFn)XGSetTextureHeader_ADDR, D3D9_XGSetTextureHeader))

// D3DDevice_SetGammaRamp(flags, pRamp) and D3DResource_Release(pResource) - both confirmed plain __stdcall
// via raw disassembly (RET 0x8 / RET 0x4 respectively, matching their param counts exactly).
typedef void(__stdcall *D3DDevice_SetGammaRampFn)(uint32_t flags, void *pRamp);
#define D3DDevice_SetGammaRamp (D3DSeamTraced("D3DDevice_SetGammaRamp", (D3DDevice_SetGammaRampFn)D3DDevice_SetGammaRamp_ADDR, D3D9_SetGammaRamp))

// Genuinely returns a value in EAX (a refcount-style result: either the resource's decremented reference
// count, or 0 once it's fully destroyed) - confirmed via raw disassembly. Nothing called this before now, so
// updating the typedef from void is safe (no existing callers relied on the wrong signature).
typedef uint32_t(__stdcall *D3DResource_ReleaseFn)(void *pResource);
#define D3DResource_Release (D3DSeamTraced("D3DResource_Release", (D3DResource_ReleaseFn)D3DResource_Release_ADDR, D3D9_ResourceRelease))

// D3DDevice_SetViewport(pViewport) - confirmed plain __stdcall (RET 0x4). D3DVIEWPORT here is the standard
// D3D8 viewport struct (X, Y, Width, Height, MinZ, MaxZ), not something Eurocom-specific.
struct D3DVIEWPORT {
    uint32_t X, Y, Width, Height;
    float MinZ, MaxZ;
};
typedef void(__stdcall *D3DDevice_SetViewportFn)(D3DVIEWPORT *pViewport);
static void D3D9Adapter_SetViewport(D3DVIEWPORT *pViewport) { // the backend takes plain fields, not this struct
    D3D9_SetViewport(pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height, pViewport->MinZ, pViewport->MaxZ);
}
#define D3DDevice_SetViewport (D3DSeamTraced("D3DDevice_SetViewport", (D3DDevice_SetViewportFn)D3DDevice_SetViewport_ADDR, D3D9Adapter_SetViewport))

// D3DDevice_Clear(rectCount, pRects, flags, colour, z, stencil) - Ghidra's own prototype only reports 5
// params (20 bytes), but the function's own RET 0x18 cleans up 24 bytes, and the d3dClear call site (see
// below) pushes exactly 6 dwords. It's a completely standard D3D8 Clear() signature - Ghidra just missed the
// trailing stencil parameter (always 0 at this call site, which is likely why its analysis folded it away).
typedef void(__stdcall *D3DDevice_ClearFn)(uint32_t rectCount, void *pRects, uint32_t flags, uint32_t colour, float z, uint32_t stencil);
#define D3DDevice_Clear (D3DSeamTraced("D3DDevice_Clear", (D3DDevice_ClearFn)D3DDevice_Clear_ADDR, D3D9_Clear))

// All four below confirmed plain __stdcall via raw disassembly (RET immediate matches param count exactly).
// Returns a surface pointer in EAX - the original d3dGetTextureSurfaceLevel0 caller discards it (called for
// side effect only), but d3dRenderTargetSetup needs it, so the typedef reflects the real return value.
typedef void *(__stdcall *D3DTexture_GetSurfaceLevel2Fn)(void *pTexture, uint32_t level);
#define D3DTexture_GetSurfaceLevel2 (D3DSeamTraced("D3DTexture_GetSurfaceLevel2", (D3DTexture_GetSurfaceLevel2Fn)D3DTexture_GetSurfaceLevel2_ADDR, D3D9_GetSurfaceLevel2))

typedef void(__stdcall *D3DDevice_SetRenderState_FogColorFn)(uint32_t colour);
#define D3DDevice_SetRenderState_FogColor (D3DSeamTraced("D3DDevice_SetRenderState_FogColor", (D3DDevice_SetRenderState_FogColorFn)D3DDevice_SetRenderState_FogColor_ADDR, D3D9_SetFogColor))

typedef void(__stdcall *D3DDevice_SetRenderState_YuvEnableFn)(uint32_t enable);
#define D3DDevice_SetRenderState_YuvEnable (D3DSeamTraced("D3DDevice_SetRenderState_YuvEnable", (D3DDevice_SetRenderState_YuvEnableFn)D3DDevice_SetRenderState_YuvEnable_ADDR, D3D9_SetYuvEnable))

typedef void(__stdcall *D3DDevice_SwapFn)(uint32_t type);
#define D3DDevice_Swap (D3DSeamTraced("D3DDevice_Swap", (D3DDevice_SwapFn)D3DDevice_Swap_ADDR, D3D9_Swap))

typedef void(__stdcall *D3DDevice_SetTextureFn)(uint32_t stage, void *pTexture);
#define D3DDevice_SetTexture (D3DSeamTraced("D3DDevice_SetTexture", (D3DDevice_SetTextureFn)D3DDevice_SetTexture_ADDR, D3D9_SetTexture))

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
#define D3DDevice_SetVertexShaderConstant1 (D3DSeamTraced("D3DDevice_SetVertexShaderConstant1", (D3DDevice_SetVertexShaderConstant1Fn)D3DDevice_SetVertexShaderConstant1_ADDR, D3D9_SetVertexShaderConstant1))

// D3DDevice_SetVertexShaderConstant4(constant index in ECX, pointer to one D3DMATRIX - 16 floats - in EDX) -
// confirmed via raw disassembly: reads exactly one MMX-copied D3DMATRIX through EDX, no stack args, plain RET.
// Same genuine __fastcall match as SetVertexShaderConstant1 above.
typedef void(__fastcall *D3DDevice_SetVertexShaderConstant4Fn)(uint32_t constantIndex, void *pMatrix);
#define D3DDevice_SetVertexShaderConstant4 (D3DSeamTraced("D3DDevice_SetVertexShaderConstant4", (D3DDevice_SetVertexShaderConstant4Fn)0x001025d0u, D3D9_SetVertexShaderConstant4))

// D3DDevice_SetTextureState_BorderColor(stage, colour) - confirmed plain __stdcall via raw disassembly (RET 0x8).
typedef void(__stdcall *D3DDevice_SetTextureState_BorderColorFn)(uint32_t stage, uint32_t colour);
#define D3DDevice_SetTextureState_BorderColor (D3DSeamTraced("D3DDevice_SetTextureState_BorderColor", (D3DDevice_SetTextureState_BorderColorFn)D3DDevice_SetTextureState_BorderColor_ADDR, D3D9_SetTextureBorderColor))

// Four more plain __stdcall D3D8 render-state setters, only ever called from d3dSetup - confirmed via
// functions_action.json (has_custom_variable_storage false, RET immediate matches param count).
typedef void(__stdcall *D3DDevice_SetRenderState_ZEnableFn)(uint32_t value);
#define D3DDevice_SetRenderState_ZEnable (D3DSeamTraced("D3DDevice_SetRenderState_ZEnable", (D3DDevice_SetRenderState_ZEnableFn)D3DDevice_SetRenderState_ZEnable_ADDR, D3D9_SetZEnable))
typedef void(__stdcall *D3DDevice_SetRenderState_NormalizeNormalsFn)(uint32_t value);
#define D3DDevice_SetRenderState_NormalizeNormals (D3DSeamTraced("D3DDevice_SetRenderState_NormalizeNormals", (D3DDevice_SetRenderState_NormalizeNormalsFn)D3DDevice_SetRenderState_NormalizeNormals_ADDR, D3D9_SetNormalizeNormals))
typedef void(__stdcall *D3DDevice_SetRenderState_VertexBlendFn)(uint32_t value);
#define D3DDevice_SetRenderState_VertexBlend (D3DSeamTraced("D3DDevice_SetRenderState_VertexBlend", (D3DDevice_SetRenderState_VertexBlendFn)D3DDevice_SetRenderState_VertexBlend_ADDR, D3D9_SetVertexBlend))
typedef void(__stdcall *D3DDevice_SetShaderConstantModeFn)(uint32_t value);
#define D3DDevice_SetShaderConstantMode (D3DSeamTraced("D3DDevice_SetShaderConstantMode", (D3DDevice_SetShaderConstantModeFn)D3DDevice_SetShaderConstantMode_ADDR, D3D9_SetShaderConstantMode))

// D3DDevice_SetVertexShaderConstantNotInline(constant index in ECX, pointer in EDX, count-in-dwords on the
// stack) - confirmed via raw disassembly: exactly matches MSVC's own __fastcall ABI for a 3-argument function
// (first two register args, third stacked, callee cleans up RET 0x4) - another case needing no asm trampoline.
typedef void(__fastcall *D3DDevice_SetVertexShaderConstantNotInlineFn)(uint32_t constantIndex, void *pData, uint32_t countDwords);
#define D3DDevice_SetVertexShaderConstantNotInline (D3DSeamTraced("D3DDevice_SetVertexShaderConstantNotInline", (D3DDevice_SetVertexShaderConstantNotInlineFn)0x00102760u, D3D9_SetVertexShaderConstantNotInline))

// D3DDevice_GetBackBuffer2(backBufferIndex) - confirmed plain __stdcall via functions_action.json (RET 0x4).
// Returns a pointer to the backbuffer's own surface-header struct (not pixel data directly) - only its
// second dword (an Xbox physical-memory byte offset, per psiBlurScreen's own use of it) is read here.
typedef uint32_t*(__stdcall *D3DDevice_GetBackBuffer2Fn)(int32_t backBufferIndex);
#define D3DDevice_GetBackBuffer2 (D3DSeamTraced("D3DDevice_GetBackBuffer2", (D3DDevice_GetBackBuffer2Fn)D3DDevice_GetBackBuffer2_ADDR, D3D9_GetBackBuffer2))

// D3DDevice_SetDepthClipPlanes(uint, uint, uint) and D3DDevice_SetStreamSource(int streamNumber, void*
// vertexBuffer, int stride) - both confirmed plain __stdcall via raw disassembly (RET 0xc, matching 3 params).
typedef void(__stdcall *D3DDevice_SetDepthClipPlanesFn)(uint32_t param1, uint32_t param2, uint32_t param3);
#define D3DDevice_SetDepthClipPlanes (D3DSeamTraced("D3DDevice_SetDepthClipPlanes", (D3DDevice_SetDepthClipPlanesFn)D3DDevice_SetDepthClipPlanes_ADDR, D3D9_SetDepthClipPlanes))

typedef void(__stdcall *D3DDevice_SetStreamSourceFn)(int streamNumber, void *vertexBuffer, int stride);
#define D3DDevice_SetStreamSource (D3DSeamTraced("D3DDevice_SetStreamSource", (D3DDevice_SetStreamSourceFn)D3DDevice_SetStreamSource_ADDR, D3D9_SetStreamSource))

// D3DDevice_SetIndices(pIndexBuffer, baseVertexIndex), D3DDevice_SetVertexShader(handle), and
// D3DDevice_DrawVerticesUP(primitiveType, vertexCount, pVertexData, stride) - all confirmed plain __stdcall
// via raw disassembly (RET 0x8 / RET 0x4 / RET 0x10 respectively, matching their param counts exactly).
typedef void(__stdcall *D3DDevice_SetIndicesFn)(void *pIndexBuffer, uint32_t baseVertexIndex);
#define D3DDevice_SetIndices (D3DSeamTraced("D3DDevice_SetIndices", (D3DDevice_SetIndicesFn)D3DDevice_SetIndices_ADDR, D3D9_SetIndices))

typedef void(__stdcall *D3DDevice_SetVertexShaderFn)(void *handle);
#define D3DDevice_SetVertexShader (D3DSeamTraced("D3DDevice_SetVertexShader", (D3DDevice_SetVertexShaderFn)D3DDevice_SetVertexShader_ADDR, D3D9_SetVertexShader))

typedef void(__stdcall *D3DDevice_DrawVerticesUPFn)(uint32_t primitiveType, uint32_t vertexCount, void *pVertexData, uint32_t stride);
#define D3DDevice_DrawVerticesUP (D3DSeamTraced("D3DDevice_DrawVerticesUP", (D3DDevice_DrawVerticesUPFn)D3DDevice_DrawVerticesUP_ADDR, D3D9_DrawVerticesUP))

// A thunk (plain JMP) to the real implementation - confirmed RET 0x4, plain __stdcall, 1 param.
typedef void(__stdcall *D3DResource_BlockUntilNotBusyFn)(void *pResource);
#define D3DResource_BlockUntilNotBusy (D3DSeamTraced("D3DResource_BlockUntilNotBusy", (D3DResource_BlockUntilNotBusyFn)D3DResource_BlockUntilNotBusy_ADDR, D3D9_BlockUntilNotBusy))

// D3DDevice_GetRenderTarget2()/GetDepthStencilSurface2() - niladic getters (plain RET, no immediate, no stack
// args), confirmed via raw disassembly. D3DDevice_SetRenderTarget(pRenderTarget, pDepthStencil) - confirmed
// plain __stdcall (RET 0x8).
typedef void *(__stdcall *D3DDevice_GetRenderTarget2Fn)(void);
#define D3DDevice_GetRenderTarget2 (D3DSeamTraced("D3DDevice_GetRenderTarget2", (D3DDevice_GetRenderTarget2Fn)D3DDevice_GetRenderTarget2_ADDR, D3D9_GetRenderTarget2))

typedef void *(__stdcall *D3DDevice_GetDepthStencilSurface2Fn)(void);
#define D3DDevice_GetDepthStencilSurface2 (D3DSeamTraced("D3DDevice_GetDepthStencilSurface2", (D3DDevice_GetDepthStencilSurface2Fn)D3DDevice_GetDepthStencilSurface2_ADDR, D3D9_GetDepthStencilSurface2))

typedef void(__stdcall *D3DDevice_SetRenderTargetFn)(void *pRenderTarget, void *pDepthStencil);
#define D3DDevice_SetRenderTarget (D3DSeamTraced("D3DDevice_SetRenderTarget", (D3DDevice_SetRenderTargetFn)D3DDevice_SetRenderTarget_ADDR, D3D9_SetRenderTarget))

// D3DDevice_DrawVertices(primitiveType, startVertex, vertexCount) - confirmed plain __stdcall (RET 0xc).
typedef void(__stdcall *D3DDevice_DrawVerticesFn)(uint32_t primitiveType, uint32_t startVertex, uint32_t vertexCount);
#define D3DDevice_DrawVertices (D3DSeamTraced("D3DDevice_DrawVertices", (D3DDevice_DrawVerticesFn)D3DDevice_DrawVertices_ADDR, D3D9_DrawVertices))

// A normal D3D8 library function we call INTO - confirmed plain __stdcall (RET 0x4), 1 param. Its own
// internals are irrelevant to us (it calls into further D3D8-internal functions, same as many others).
typedef void(__stdcall *D3DDevice_SetRenderState_ZBiasFn)(int zBias);
#define D3DDevice_SetRenderState_ZBias (D3DSeamTraced("D3DDevice_SetRenderState_ZBias", (D3DDevice_SetRenderState_ZBiasFn)D3DDevice_SetRenderState_ZBias_ADDR, D3D9_SetZBias))

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
// AUTOGEN
void maybeTransposeRotationPart(D3DMATRIX *mtx);
// AUTOGEN
void maybeInvertRigidTransform(D3DMATRIX *mtx);
// AUTOGEN
void maybeMtxApplyTransform(D3DMATRIX *mtx, float dx, float dy, float dz);
// AUTOGEN
void maybeMtxInverse(D3DMATRIX *mtx);
// Already AUTOGEN-declared (and its stub body generated) elsewhere - plain forward declaration here, same
// pattern as timestamp() above, so this file can call it too without a colliding second body.
void* allocateAligned0x1000(int numBytes);

// D3DDevice_SetRenderState_Simple(NV2A method header word in ECX, value in EDX) - the generic, runtime-method
// render-state setter. Everything else in the D3DDevice_SetRenderState_XXX family takes its single value on
// the stack like a normal __stdcall function; only this one, being the sole one whose method is a caller-
// supplied runtime value rather than baked into its own bytecode, uses registers instead.
static void D3D_SetRenderStateSimple(uint32_t method, uint32_t value) {
    if (g_gfxBackend == GFX_BACKEND_D3D9) {
        D3D9_SetRenderStateSimple(method, value); // hand-dispatched: the ECX/EDX convention keeps it off the wrapper
        return;
    }
#if D3DSEAM_TRACE
    // Traced by hand (the ECX/EDX convention keeps it off the D3DSeamTraced path): once under the shared entry
    // point name keyed by method, and once under a per-method name keyed by value, so the summary shows both
    // "which NV2A methods" and "which values per method".
    D3DSeamTraceCall("D3DDevice_SetRenderState_Simple", method, value);
    {
        static char methodNames[32][40];
        static uint32_t methodIds[32];
        static int methodCount = 0;
        int slot = -1;
        for (int i = 0; i < methodCount; i++) if (methodIds[i] == method) { slot = i; break; }
        if (slot < 0 && methodCount < 32) {
            slot = methodCount++;
            methodIds[slot] = method;
            snprintf(methodNames[slot], sizeof(methodNames[slot]), "  RS_Simple[method 0x%x]", method);
        }
        if (slot >= 0)
            D3DSeamTraceRecord(methodNames[slot], value, NULL);
    }
#endif
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

// Diagnostic helper for a full slot table - defined later in this file (near BitsToFloat/FloatToBits), forward
// declared here so RegisterTexture's own table-full path (which had no diagnostic at all until now, unlike the
// vertex/index/overlay buffer tables) can use it too.
void D3DSeamTableExhaustedWarning(const char *tableName, const char *extraContext);

// The ONE place the seam relies on Xbox memory-map semantics rather than a D3D8 entry point: on the Xbox,
// 0x80000000 | physicalAddress is the uncached alias of physical RAM. Every D3D resource's Data word is such a
// physical address with the top nibble stripped (D3DResource_Register and D3DTexture_GetSurfaceLevel2 both mask
// it), so ANY CPU access to resource pixel data has to go through this - psiBlurScreen reading the live
// backbuffer, Texture_GetRawDataPtr handing psiDecompressWoman a texture's pixels, and d3dLockSurface handing
// the video decoder its frame buffer. Under CXBX the alias is mapped to the same host memory as the contiguous
// allocations the data lives in (which themselves sit at 0x8xxxxxxx addresses), so it simply works. A
// non-CXBX backend replaces these uses (a GPU readback for the backbuffer, the plain pointer plus a "contents
// changed" notification for the textures) - which is why they're all funnelled through this helper.
// Standalone under nfloader there is no alias to take. Resource memory comes from
// MmAllocateContiguousMemoryEx, which is a plain VirtualAlloc (see src/loader/kernel.cpp), so a resource's
// Data word is already the address the CPU should use - Nightfire's land around 0x09000000-0x0c000000, well
// inside the low half, so the top nibble the Xbox strips was never set. OR'ing 0x80000000 into one of those
// produces an address belonging to nothing, and the first thing to find out was the XMV decoder writing a
// frame to 0x8b042700 when the surface it locked was at 0x0b042700.
//
// Which host we are on is worked out once, from the first address that comes through here, by asking whether
// its alias is actually mapped. That is better than a build-time switch or a "is CXBX loaded" test, because
// it checks the thing that actually matters rather than a proxy for it.
static inline void *D3D_UncachedAliasOf(uint32_t address) {
    static int aliasIsMapped = -1;
    void *alias = (void*)(uintptr_t)(address | 0x80000000u);

    if (aliasIsMapped < 0) {
        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof(mbi));
        aliasIsMapped = (VirtualQuery(alias, &mbi, sizeof(mbi)) == sizeof(mbi) && mbi.State == MEM_COMMIT);
        printf("[d3dSeam] uncached alias of resource memory: %s\n",
               aliasIsMapped ? "mapped, using 0x8xxxxxxx as on the Xbox"
                             : "not mapped, using resource addresses directly");
    }
    return aliasIsMapped ? alias : (void*)(uintptr_t)address;
}

// The extra-context line every table-full diagnostic prints (defined up here since RegisterTexture is the
// first user). See D3DSeamTableExhaustedWarning's own comment for the history.
#define D3DSEAM_TABLE_LEAK_CONTEXT \
    "Level-scoped slots are normally freed on every level change by d3dReleaseLevelResources " \
    "(maybeD3dShutdown), so this means something is allocating outside a level's lifetime, or a new " \
    "allocator caller has appeared - see D3DSeamTableExhaustedWarning's comment in d3dSeam.cpp."
#define D3DSEAM_VTX_IDX_LEAK_CONTEXT D3DSEAM_TABLE_LEAK_CONTEXT

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
    uint16_t refCount;      // +0x1c - written 0 by RegisterTexture; FUN_000e4f00 (the slot-release function) tests it
                             // == 0 before freeing, confirming it really is a refcount despite RegisterTexture
                             // never incrementing it - nothing traced so far increments it either
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
        texSlot->refCount = 0;
        texSlot->nonSwizzled = (uint16_t)(param_6 != 0);
        Gfx_TotalTextureBytesUsed += mipChainBytes;

        return slot;
    }

    // Should no longer be reachable in normal play: the original game never released level textures (every
    // load registers ~500 of them, so the table filled on the 4th/5th load), but d3dReleaseLevelResources
    // (called from maybeD3dShutdown on every level change) now frees all non-permanent slots. If this does
    // fire, the occupied count below tells a real accumulation (near D3D_TEXTURE_TABLE_COUNT - something
    // registering textures outside a level's lifetime, or a new allocator caller) apart from a corrupted
    // free-slot scan (far lower).
    {
        int occupiedCount = 0;
        for (int i = 1; i < D3D_TEXTURE_TABLE_COUNT; i++) {
            if (D3DTextureSlot(i)->baseTexture != NULL)
                occupiedCount++;
        }
        printf("[d3dSeam] texture table full - %d/%d slots actually occupied (requested %ux%u).\n",
               occupiedCount, D3D_TEXTURE_TABLE_COUNT - 1, width, height);
    }
    D3DSeamTableExhaustedWarning("texture", D3DSEAM_TABLE_LEAK_CONTEXT);
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
#define Gfx_CurrentCullMode     U32_AT(0x002C6F9C) // Gfx.currentCullMode - a separate "current mode" cache from D3D_CullModeLastValue above

// Deferred-texture-state cache pair used by maybeResetRenderState/d3dSetup (matching field names) - and the
// underlying D3D8-internal globals they drive once changed.
#define Gfx_DeferredTexStateA U32_AT(0x002C6FAC) // Gfx.field6059_0x185c
#define Gfx_DeferredTexStateB U32_AT(0x002C6FB0) // Gfx.field6060_0x1860
#define D3D8_Stage0_AddressU D3D8_TRACED("D3D8_Stage0_AddressU", 0x001117D0) // D3D8::D3D_g_DeferredTextureState
#define D3D8_Stage0_AddressV D3D8_TRACED("D3D8_Stage0_AddressV", 0x001117D4) // not in the Gfx struct - a separate D3D8-internal global

// A trio of cache fields (matching field names in d3dSetup too) all set together, driving one opaque D3D8
// register write (method 0x40358) - untraced meaning beyond that.
#define Gfx_ExtraBlendA U32_AT(0x002C6FC4) // Gfx.field6068_0x1874
#define Gfx_ExtraBlendB U32_AT(0x002C6FC8) // Gfx.field6069_0x1878
#define Gfx_ExtraBlendC U32_AT(0x002C6FCC) // Gfx.field6070_0x187c

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
#define D3D8_PushBufferDirtyFlags D3D8_TRACED("D3D8_PushBufferDirtyFlags", 0x001117CC)
#define D3D8_RS_FogEnable D3D8_TRACED("D3D8_RS_FogEnable", 0x00111B40)

#define Gfx_CurrentlyLoadedTexture U32_AT(0x002C6F84) // Gfx.currentlyLoadedTexture (stage 0)
#define Gfx_TexStage1SlotCache     U32_AT(0x002C6F94) // Gfx.field6053_0x1844
#define Gfx_TexStage1Param2Cache   U32_AT(0x002C6F98) // Gfx.field6054_0x1848
#define Gfx_TexStage1ModeFlags     U32_AT(0x002FF3A4) // not in the Gfx struct - a separate global

// Opaque D3D8-internal texture-stage-1 configuration registers, poked directly with fixed constants by the
// original depending on whether a real texture or NULL is being bound to stage 1 - untraced meaning, ported
// verbatim rather than guessed at.
#define D3D8_TexStage1_0x00 D3D8_TRACED("D3D8_TexStage1_0x00", 0x00111800)
#define D3D8_TexStage1_0x08 D3D8_TRACED("D3D8_TexStage1_0x08", 0x00111808)
#define D3D8_TexStage1_0x10 D3D8_TRACED("D3D8_TexStage1_0x10", 0x00111810)
#define D3D8_TexStage1_0x80 D3D8_TRACED("D3D8_TexStage1_0x80", 0x00111880)
#define D3D8_TexStage1_0x88 D3D8_TRACED("D3D8_TexStage1_0x88", 0x00111888)
#define D3D8_TexStage1_0x8c D3D8_TRACED("D3D8_TexStage1_0x8c", 0x0011188C)
#define D3D8_TexStage1_0x90 D3D8_TRACED("D3D8_TexStage1_0x90", 0x00111890)
#define D3D8_TexStage1_0x98 D3D8_TRACED("D3D8_TexStage1_0x98", 0x00111898)
#define D3D8_TexStage1_0x9c D3D8_TRACED("D3D8_TexStage1_0x9c", 0x0011189C)

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
#define Gfx_LevelDirectionVector ((float*)0x002FF398u) // Gfx.field158786-788_0x39c48/4c/50 - {x,y,z}; untraced consumer (no reader found anywhere in the binary), written by both d3dSetup's own hardcoded (1,0,0) default and d3dSetLevelDirectionVector's mission-specific overrides

#define Gfx_StreamStrideConstants ((float*)0x002FF358)  // Gfx.field_0x39c08 - 8 floats, shader constant 0x73
#define Gfx_d3dstreamDataPtr ((void**)0x002DECF8)        // Gfx.d3dstreamDataPtr - array of stream-data pointers, stride 9 dwords per slot
#define Gfx_StreamStrideRelated ((uint8_t*)0x002DED04)   // Gfx.d3dstreamStrideRelated - a byte flag inside the same 36-byte-per-slot struct as Gfx_d3dstreamDataPtr (offset +12), indexed in raw bytes
#define Gfx_d3dIndexBuffers ((void**)0x002F0CF8)         // Gfx.d3dIndexBuffers - array of index-buffer pointers, stride 7 dwords per slot
#define Gfx_ShardUseAltShader U32_AT(0x002C6FB4)         // Gfx.field6061_0x1864
#define VtxShaderHandles ((void**)0x002C5548)            // not in the Gfx struct - a fixed array of created vertex-shader handles

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

// Printed when one of the 2048-entry texture/vertex-buffer/index-buffer tables (or the 256-entry overlay quad
// table) has no free slot. The original binary never released the first three at all - every level load
// registered ~500 textures and one vertex + one index buffer per entity model into them and nothing ever
// marked a slot free again, so they filled up after 4-5 level loads (grey/missing textures, missing geometry,
// then a crash downstream of a 0 handle). d3dReleaseLevelResources (see its comment, near psiBlurScreen) now
// frees every level-scoped slot on each level change, so this should no longer fire in normal play. It prints
// clearly (rather than just returning 0 and letting some unrelated caller crash on the failure later) so any
// remaining or new leak is immediately attributable.
//
// (The earlier theory recorded here - that Break.cpp's unfinished Break_Create/Break_Kill leaked vertex/index
// buffer slots - was wrong: psiCreateEntityGfx is the only caller of either allocator. parsemap.cpp's
// Place_Breakable case still routes to the original Break_Create because of that theory; it's harmless but
// no longer needed for this issue.)
void D3DSeamTableExhaustedWarning(const char *tableName, const char *extraContext) {
    printf("[d3dSeam] %s table is full - allocation failed.\n", tableName);
    if (extraContext != NULL)
        printf("          %s\n", extraContext);
}

// Standard row-major 4x4 matrix product (out = base * chain), verified term-by-term against
// maybeMultiplyMatrixChain's own decompiled single-link arithmetic. See d3dSetMatrix's comment for why this is
// implemented directly rather than calling that function.
// Alias-safe: computes fully into a local temporary before copying to *out, since some callers pass the same
// D3DMATRIX for out and base and/or out and chain (the original's own maybeMultiplyMatrixChain call sequence
// in maybeBuildAndSetModelViewProjectionMtx does exactly this) - writing into *out mid-computation would read
// back partially-overwritten data for later rows/columns.
static void Multiply4x4RowMajor(const D3DMATRIX *base, const D3DMATRIX *chain, D3DMATRIX *out) {
    D3DMATRIX result;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            result.f[row * 4 + col] = base->f[row * 4 + 0] * chain->f[0 * 4 + col]
                                     + base->f[row * 4 + 1] * chain->f[1 * 4 + col]
                                     + base->f[row * 4 + 2] * chain->f[2 * 4 + col]
                                     + base->f[row * 4 + 3] * chain->f[3 * 4 + col];
        }
    }
    memcpy(out, &result, sizeof(D3DMATRIX));
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

// A render-target push/pop "stack" (single level, not truly nested): a nonzero textureSlot pushes that
// texture's surface as the new render target (saving the current viewport/render-target/depth-stencil the
// FIRST time this is pushed, not on every call); textureSlot==0 pops back to whatever was saved.
#define Gfx_RenderTargetPushed U8_AT(0x002FF494)
#define Gfx_SavedViewportX     U32_AT(0x002FF490)
#define Gfx_SavedViewportY     U32_AT(0x002FF48C)
#define Gfx_SavedViewportWidth U32_AT(0x002FF488)
#define Gfx_SavedViewportHeight U32_AT(0x002FF484)
#define Gfx_SavedRenderTarget  PTR_AT(0x002FF480)
#define Gfx_SavedDepthStencil  PTR_AT(0x002FF47C)
#define Gfx_CustomRenderTargetSurface PTR_AT(0x002FF478)

// Releases *slot if non-NULL (preserving D3DResource_Release's actual refcount-style return value in
// Gfx_D3DLastError, matching the original exactly) or just zeroes Gfx_D3DLastError if it was already NULL,
// then clears *slot.
static void ReleaseSavedSurface(void **slot) {
    if (*slot != NULL)
        Gfx_D3DLastError = D3DResource_Release(*slot);
    else
        Gfx_D3DLastError = 0;
    *slot = NULL;
}

static void _d3dRenderTargetSetup(int textureSlot) {
    if (textureSlot != 0) {
        if (Gfx_RenderTargetPushed == 0) {
            Gfx_ShaderConstant75[0] = 0.0f;
            Gfx_ShaderConstant75[1] = 0.0f;
            if (D3D_DeviceReady != 0)
                D3DDevice_SetVertexShaderConstant1(0x75, Gfx_ShaderConstant75);

            Gfx_SavedViewportX = Gfx_ViewportX;
            Gfx_D3DLastError = 0;
            Gfx_SavedViewportY = Gfx_ViewportY;
            Gfx_SavedViewportWidth = Gfx_ViewportWidth;
            Gfx_SavedViewportHeight = Gfx_ViewportHeight;
            Gfx_SavedRenderTarget = D3DDevice_GetRenderTarget2();
            Gfx_SavedDepthStencil = D3DDevice_GetDepthStencilSurface2();
        }

        ReleaseSavedSurface(&Gfx_CustomRenderTargetSurface);

        void *surface = D3DTexture_GetSurfaceLevel2(D3DTextureSlot(textureSlot)->baseTexture, 0);
        Gfx_CustomRenderTargetSurface = surface;
        D3DDevice_SetRenderTarget(surface, NULL);

        Gfx_RenderTargetPushed = 1;
        return;
    }

    if (Gfx_RenderTargetPushed == 0)
        return;

    Gfx_ShaderConstant75[0] = 0.0f;
    Gfx_ShaderConstant75[1] = 0.0f;
    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstant1(0x75, Gfx_ShaderConstant75);
    Gfx_D3DLastError = 0;

    D3DDevice_SetRenderTarget(Gfx_SavedRenderTarget, Gfx_SavedDepthStencil);
    d3dSetupViewportDimensions(Gfx_SavedViewportX, Gfx_SavedViewportY, Gfx_SavedViewportWidth, Gfx_SavedViewportHeight);

    ReleaseSavedSurface(&Gfx_CustomRenderTargetSurface);
    ReleaseSavedSurface(&Gfx_SavedRenderTarget);
    ReleaseSavedSurface(&Gfx_SavedDepthStencil);
    Gfx_RenderTargetPushed = 0;
}

// The original takes its single parameter (a texture-slot index) in ESI, not on the stack - confirmed via raw
// disassembly (its very first instruction reads ESI with no prologue setting it from the stack; Ghidra's own
// decompile flags this exact gap with "unaff_ESI"/"extraout_EAX" warnings - the same class of hidden custom
// calling convention as D3DDevice_SetRenderState_Simple at the top of this file, but on a function we're
// REPLACING rather than one we call into, so it needs a callee-side entry trampoline instead of a caller-side
// one. Matches the established View_CaptureScene/View_AddCels pattern (see view.cpp) for this exact situation.
//
// AUTOLTCG
void __declspec(naked) d3dRenderTargetSetup(void) {
    _asm {
        push esi
        call _d3dRenderTargetSetup
        add esp, 4
        ret
    }
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

// Frees a texture slot registered by RegisterTexture, no-opping if refCount is still nonzero. Also unbinds
// stage 0 first if this was the currently-loaded texture there (rebinding it to slot 0's own baseTexture,
// matching the original exactly), and stage 1 unconditionally via d3dSetTextureStage1 just above.
//
// AUTOINJECT
void ReleaseTexture(int textureSlot) {
    D3DTextureSlotRaw *texSlot = D3DTextureSlot(textureSlot);
    if (texSlot->refCount != 0)
        return;

    if (Gfx_CurrentlyLoadedTexture != 0) {
        Gfx_CurrentlyLoadedTexture = 0;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetTexture(0, D3DTextureSlot(0)->baseTexture);
        Gfx_D3DLastError = 0;
    }

    d3dSetTextureStage1(0, 0);

    if (texSlot->baseTexture != NULL)
        D3DResource_BlockUntilNotBusy(texSlot->baseTexture);

    Gfx_TotalTextureBytesUsed -= texSlot->mipChainBytes;
    texSlot->baseTexture = NULL;
    texSlot->mipChainBytes = 0;
}

// Forces refCount to 1 on an already-registered texture slot (no-op if the slot is empty), which makes
// ReleaseTexture's own gate permanently refuse to free it. Used by the engine's own critical/fallback
// textures (error-screen font, boot-time init) that must never disappear.
//
// AUTOINJECT
void d3dMarkTexturePermanent(int textureSlot) {
    D3DTextureSlotRaw *texSlot = D3DTextureSlot(textureSlot);
    if (texSlot->baseTexture != NULL)
        texSlot->refCount = 1;
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
    D3DSeamTraceEndFrame();
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

// Builds an inverse-model-view-projection-style matrix (shader constant 0x77) from a rigid-transform matrix
// and a base matrix. Like d3dSetMatrix, this deliberately does NOT call maybeMultiplyMatrixChain - the
// original's own call sequence here aliases dest with base and/or chain in every one of its three multiply
// steps (dest==chain twice, dest==base once), which is exactly the kind of construction that made verifying
// maybeMultiplyMatrixChain's internals so unreliable in the first place. Multiply4x4RowMajor is alias-safe
// (computes into a temporary before writing *out), so it sidesteps that risk entirely rather than needing to
// reconstruct a real MatrixChainNode - this is the same fix that unblocked d3dSetMatrix, applied to the other
// deferred caller.
//
// AUTOINJECT
void maybeBuildAndSetModelViewProjectionMtx(D3DMATRIX *rigidTransform, D3DMATRIX *base) {
    D3DMATRIX workingMatrix;
    D3DMATRIX tempMatrix;

    maybeD3DMATRIXcopy((undefined4 *)&workingMatrix, (undefined4 *)rigidTransform);
    maybeTransposeRotationPart(&workingMatrix);
    maybeInvertRigidTransform(&workingMatrix);
    Multiply4x4RowMajor(base, &workingMatrix, &workingMatrix);

    d3dMatrixIdentity(&tempMatrix);
    maybeMtxApplyTransform(&tempMatrix, 0.5f, 0.5f, 0.0f);
    Multiply4x4RowMajor(&tempMatrix, &workingMatrix, &workingMatrix);

    maybeD3DMATRIXcopy((undefined4 *)&tempMatrix, (undefined4 *)Gfx_ViewMatrixCache);
    maybeMtxInverse(&tempMatrix);
    Multiply4x4RowMajor(&workingMatrix, &tempMatrix, &workingMatrix);

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstant4(0x77, &workingMatrix);
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

// ---------------------------------------------------------------------------------------------------------------
// Buffer binding, shards
// ---------------------------------------------------------------------------------------------------------------

// Binds a stream buffer (slot 0) and index buffer by handle, no-opping if both already match the cache.
//
// The bound index buffer's index data, kept by the seam itself. D3D8's SetIndices stores the same pointer
// (the index-buffer header's Data word) in its own global at 0x001117c8, which is what the original
// d3dDrawIndexedVertices reads back; keeping our own copy here means the draw path doesn't have to peek into
// D3D8 internals for it.
static const uint16_t *g_d3dBoundIndexData = NULL;

// AUTOINJECT
void d3dBindBuffers(int streamBufferHandle, int indexBufferHandle) {
    if (Gfx_CurrentStreamBuffer == (uint32_t)streamBufferHandle && Gfx_CurrentIndexBuffer == (uint32_t)indexBufferHandle)
        return;

    Gfx_CurrentStreamBuffer = (uint32_t)streamBufferHandle;
    Gfx_MiscResetFlag = 0xFFFFFFFFu;
    Gfx_CurrentIndexBuffer = (uint32_t)indexBufferHandle;

    {
        const uint32_t *indexBufferHeader = (const uint32_t*)Gfx_d3dIndexBuffers[(size_t)indexBufferHandle * 7];
        g_d3dBoundIndexData = (indexBufferHeader != NULL) ? (const uint16_t*)(uintptr_t)indexBufferHeader[1] : NULL;
    }

    uint8_t strideFlagByte = Gfx_StreamStrideRelated[(size_t)streamBufferHandle * 36];
    if (strideFlagByte != 0)
        Gfx_MiscModeFlags |= 0x2u;
    else
        Gfx_MiscModeFlags &= ~0x2u;

    if (D3D_DeviceReady != 0) {
        void *dataPtr = Gfx_d3dstreamDataPtr[(size_t)streamBufferHandle * 9];
        uint32_t stride = (strideFlagByte != 0) ? 0x20u : 0x1Cu;
        D3DDevice_SetStreamSource(0, dataPtr, stride);
        Gfx_D3DLastError = 0;

        if (D3D_DeviceReady != 0) {
            void *indexBuffer = Gfx_d3dIndexBuffers[(size_t)indexBufferHandle * 7];
            D3DDevice_SetIndices(indexBuffer, 0);
        }
    }
    Gfx_D3DLastError = 0;
}

// Draws a "shard" - an ad-hoc, non-indexed triangle list built directly from a caller-supplied vertex buffer
// (glass/debris fragments, going by the name) - resetting the stream/index-buffer cache since it bypasses the
// normal binding path above.
//
// AUTOINJECT
void drawShard(void *data, int countTris) {
    if (countTris <= 0)
        return;

    if (D3D_DeviceReady != 0) {
        void *shaderHandle = VtxShaderHandles[(Gfx_ShardUseAltShader != 0) ? 64 : 0];
        D3DDevice_SetVertexShader(shaderHandle);
    }
    Gfx_MiscResetFlag = 0xFFFFFFFFu;
    Gfx_CurrentStreamBuffer = 0xFFFFFFFFu;
    Gfx_CurrentIndexBuffer = 0xFFFFFFFFu;
    Gfx_D3DLastError = 0;

    if (D3D_DeviceReady != 0)
        D3DDevice_DrawVerticesUP(5, (uint32_t)countTris * 3, data, 0x1c);
    Gfx_D3DLastError = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// d3dCreateIndexBuffer
// ---------------------------------------------------------------------------------------------------------------

// No D3D8 calls at all - purely a linear-scan slot allocator and bookkeeping struct, matching RegisterTexture's
// own free-slot-scan/alignment-shift pattern. "selfPtr" (+0xc) is what Gfx_d3dIndexBuffers[slot] resolves to -
// they're literally the same struct field, confirmed by both this function's own write and d3dBindBuffers'
// read landing on the identical address (0x2F0CEC + slot*28 + 0xc), not two separate tables.
#define D3D_INDEX_BUFFER_TABLE_BASE  0x002F0CECu
#define D3D_INDEX_BUFFER_TABLE_COUNT 2048

struct D3DIndexBufferSlotRaw {
    uint32_t header;     // +0x00 - always 0x10001 once allocated; 0 marks the slot free
    void    *dataPtr;    // +0x04
    uint32_t reserved08; // +0x08 - always 0; nothing else reads it as far as we've traced
    void    *selfPtr;    // +0x0c - points back to this same slot's own header (Xbox convention, same as RegisterTexture's baseTexture)
    uint32_t indexCount; // +0x10
    uint32_t byteSize;   // +0x14 - indexCount * 2 (16-bit indices)
    void    *dataPtr2;   // +0x18 - same value as dataPtr
};
static_assert(sizeof(D3DIndexBufferSlotRaw) == 28, "Bad size for D3DIndexBufferSlotRaw");

static D3DIndexBufferSlotRaw *D3DIndexBufferSlot(int index) {
    return (D3DIndexBufferSlotRaw*)(D3D_INDEX_BUFFER_TABLE_BASE + (unsigned)index * sizeof(D3DIndexBufferSlotRaw));
}

#define Gfx_IndexBufferBytesUsed U32_AT(0x002C6FD8) // Gfx.field6073_0x1888 - running total, informational only

// AUTOINJECT
int d3dCreateIndexBuffer(int indexCount, unsigned int data) {
    if (indexCount < 1)
        return 0;

    for (int slot = 1; slot < D3D_INDEX_BUFFER_TABLE_COUNT; slot++) {
        D3DIndexBufferSlotRaw *slotPtr = D3DIndexBufferSlot(slot);
        if (slotPtr->header != 0)
            continue;

        uint32_t byteSize = (uint32_t)indexCount * 2;

        // 4-byte alignment shift, same reverse-safe-overlap idea as RegisterTexture's 128-byte one - memmove
        // handles the direction correctly regardless of which way the shift goes.
        uintptr_t dataAddr = (uintptr_t)data;
        uintptr_t alignedAddr = (dataAddr + 3) & ~(uintptr_t)3;
        if (alignedAddr != dataAddr) {
            memmove((void*)alignedAddr, (void*)dataAddr, byteSize);
            data = (unsigned int)alignedAddr;
        }

        slotPtr->header = 0x10001;
        slotPtr->dataPtr = (void*)(uintptr_t)data;
        slotPtr->reserved08 = 0;
        slotPtr->selfPtr = slotPtr;
        slotPtr->indexCount = (uint32_t)indexCount;
        slotPtr->byteSize = byteSize;
        slotPtr->dataPtr2 = (void*)(uintptr_t)data;

        Gfx_IndexBufferBytesUsed += byteSize;
        return slot;
    }

    D3DSeamTableExhaustedWarning("index buffer", D3DSEAM_VTX_IDX_LEAK_CONTEXT);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// d3dCreateVertexBuffers
// ---------------------------------------------------------------------------------------------------------------

// Shares the SAME 2048-slot, 36-byte-per-slot table as d3dSetStreamSources/d3dBindBuffers's
// Gfx_d3dstreamDataPtr/Gfx_StreamStrideRelated (those are themselves individual fields within this same
// struct - its base is 0xc bytes before Gfx_d3dstreamDataPtr's own base address, and the "is this slot free"
// test both allocation paths use below is exactly the Gfx_d3dstreamDataPtr[slot] field being NULL).
//
// Allocates either ONE slot (single buffer, streamCount <= 0) or a run of `streamCount` CONSECUTIVE free
// slots (a multi-stream vertex-buffer set). Confirmed via raw disassembly that the two paths populate
// meaningfully DIFFERENT subsets of the struct with different values at some of the SAME offsets (notably
// +0x10, +0x18, +0x1c) - they are deliberately kept as two separate blocks below rather than unified into one
// parameterised helper, to avoid blurring that real difference.
#define D3D_VERTEX_BUFFER_TABLE_BASE  0x002DECECu
#define D3D_VERTEX_BUFFER_TABLE_COUNT 2048

struct D3DVertexBufferSlotRaw {
    uint32_t header;     // +0x00 - Common; 1 once allocated. header&0x70000 never equals 0x20000 for this value,
                          // so D3DResource_Register below never takes its pointer-masking branch (unlike
                          // RegisterTexture's texture headers) - no "force the pointer back" workaround needed.
    void    *dataPtr;    // +0x04 - Data; set BY D3DResource_Register itself (we zero it first, it adds data to that)
    uint32_t reserved08; // +0x08 - always 0; nothing else reads it as far as we've traced
    void    *selfPtr;    // +0x0c - Gfx_d3dstreamDataPtr[slot] IS this field; 0 marks the slot free
    uint32_t field10;     // +0x10 - single-path: the ORIGINAL, unaligned data pointer; multi-path: vtxCnt
    uint32_t byteSize;    // +0x14 - total byte size (both paths, same role, different formula)
    uint8_t  field18;      // +0x18 - single-path: (nonSwizzled != 0); multi-path: always 0. Aliases Gfx_StreamStrideRelated[slot]
    uint8_t  pad19, pad1a, pad1b;
    uint32_t field1c;     // +0x1c - single-path: always 0; multi-path: streamCount (same value in every slot of the run)
    void    *alignedData; // +0x20 - single-path ONLY; never written by the multi-path
};
static_assert(sizeof(D3DVertexBufferSlotRaw) == 36, "Bad size for D3DVertexBufferSlotRaw");

static D3DVertexBufferSlotRaw *D3DVertexBufferSlot(int index) {
    return (D3DVertexBufferSlotRaw*)(D3D_VERTEX_BUFFER_TABLE_BASE + (unsigned)index * sizeof(D3DVertexBufferSlotRaw));
}

#define Gfx_VertexBufferBytesUsed U32_AT(0x002C6FDC) // Gfx.field6074_0x188c - running total, informational only

// AUTOINJECT
int d3dCreateVertexBuffers(unsigned int vtxCnt, unsigned int data, int nonSwizzled, unsigned int streamCount) {
    if ((int)vtxCnt < 1)
        return 0;

    if ((int)streamCount > 0) {
        // Multi-stream path: find the first run of `streamCount` CONSECUTIVE free slots, starting the search
        // over from scratch (not resuming mid-run) whenever a run is broken by an occupied slot - matches the
        // original's own re-scan behaviour exactly.
        int runStart = 1;
        for (;;) {
            int freeCount = 0;
            for (int slot = runStart; slot < D3D_VERTEX_BUFFER_TABLE_COUNT && freeCount < (int)streamCount; slot++) {
                if (D3DVertexBufferSlot(slot)->selfPtr != NULL)
                    break;
                freeCount++;
            }
            if (freeCount >= (int)streamCount)
                break;
            runStart++;
            if (runStart >= D3D_VERTEX_BUFFER_TABLE_COUNT) {
                D3DSeamTableExhaustedWarning("vertex buffer (multi-stream)", D3DSEAM_VTX_IDX_LEAK_CONTEXT);
                return 0;
            }
        }

        uint32_t perStreamBytes = vtxCnt * 6;
        uint32_t totalBytes = perStreamBytes * streamCount;

        uintptr_t dataAddr = (uintptr_t)data;
        uintptr_t alignedAddr = (dataAddr + 3) & ~(uintptr_t)3;
        if (alignedAddr != dataAddr) {
            memmove((void*)alignedAddr, (void*)dataAddr, totalBytes);
            data = (unsigned int)alignedAddr;
        }

        for (unsigned int i = 0; i < streamCount; i++) {
            D3DVertexBufferSlotRaw *slotPtr = D3DVertexBufferSlot(runStart + i);
            slotPtr->header = 1;
            slotPtr->dataPtr = NULL;
            slotPtr->reserved08 = 0;
            D3DResource_Register(slotPtr, data);
            slotPtr->field10 = vtxCnt;
            slotPtr->selfPtr = slotPtr;
            slotPtr->byteSize = perStreamBytes;
            slotPtr->field18 = 0;
            slotPtr->field1c = streamCount;

            Gfx_VertexBufferBytesUsed += perStreamBytes;
            data += perStreamBytes;
        }

        return runStart;
    }

    // Single-buffer path: one slot.
    int slot = 1;
    for (; slot < D3D_VERTEX_BUFFER_TABLE_COUNT; slot++) {
        if (D3DVertexBufferSlot(slot)->selfPtr == NULL)
            break;
    }
    if (slot >= D3D_VERTEX_BUFFER_TABLE_COUNT) {
        D3DSeamTableExhaustedWarning("vertex buffer (single)", D3DSEAM_VTX_IDX_LEAK_CONTEXT);
        return 0;
    }

    uint32_t stride = (nonSwizzled != 0) ? 0x20u : 0x1Cu;
    uint32_t totalBytes = vtxCnt * stride;

    unsigned int originalData = data;
    uintptr_t dataAddr = (uintptr_t)data;
    uintptr_t alignedAddr = (dataAddr + 3) & ~(uintptr_t)3;
    if (alignedAddr != dataAddr) {
        memmove((void*)alignedAddr, (void*)dataAddr, totalBytes);
        data = (unsigned int)alignedAddr;
    }

    D3DVertexBufferSlotRaw *slotPtr = D3DVertexBufferSlot(slot);
    slotPtr->header = 1;
    slotPtr->dataPtr = NULL;
    slotPtr->reserved08 = 0;
    D3DResource_Register(slotPtr, data);
    slotPtr->field10 = originalData;
    slotPtr->selfPtr = slotPtr;
    slotPtr->byteSize = totalBytes;
    slotPtr->field18 = (nonSwizzled != 0) ? 1 : 0;
    slotPtr->field1c = 0;
    slotPtr->alignedData = (void*)(uintptr_t)data;

    Gfx_VertexBufferBytesUsed += totalBytes;
    return slot;
}

// ---------------------------------------------------------------------------------------------------------------
// d3dRegisterOverlayBuffer
// ---------------------------------------------------------------------------------------------------------------

// A separate, smaller (256-slot, 20-byte-per-slot) table from the texture/vertex/index-buffer ones above -
// used by d3dDrawOverlayQuad (a reticle/crosshair-style textured-quad draw) to look up a small vertex buffer
// by slot and draw it. The "is this slot free" test is at +0xc, not +0x00, matching the
// same "check the self/data-pointer field, not the header" pattern already seen on the other tables - +0xc
// here holds a copy of the caller's own data pointer (confirmed via raw disassembly: written directly from
// the same value passed to D3DResource_Register as its data argument, not computed).
#define D3D_OVERLAY_QUAD_TABLE_BASE  0x002CAFE8u
#define D3D_OVERLAY_QUAD_TABLE_COUNT 256

struct D3DOverlayQuadSlotRaw {
    uint32_t header;     // +0x00 - 1 once allocated. header&0x70000 never equals 0x20000 for this value, so
                          // D3DResource_Register never takes its pointer-masking branch here either.
    void    *dataPtr;    // +0x04 - Data; set BY D3DResource_Register itself (we zero it first, it adds data to that)
    uint32_t reserved08; // +0x08 - always 0; nothing else reads it as far as we've traced
    void    *dataPtrCopy; // +0x0c - a copy of the caller's own data pointer; doubles as the "is this slot free" test
    uint32_t vertexCount; // +0x10 - consumed by d3dDrawOverlayQuad's own D3DDevice_DrawVertices call
};
static_assert(sizeof(D3DOverlayQuadSlotRaw) == 20, "Bad size for D3DOverlayQuadSlotRaw");

static D3DOverlayQuadSlotRaw *D3DOverlayQuadSlot(int index) {
    return (D3DOverlayQuadSlotRaw*)(D3D_OVERLAY_QUAD_TABLE_BASE + (unsigned)index * sizeof(D3DOverlayQuadSlotRaw));
}

// AUTOINJECT
int d3dRegisterOverlayBuffer(void *data, unsigned int vertexCount) {
    for (int slot = 1; slot < D3D_OVERLAY_QUAD_TABLE_COUNT; slot++) {
        D3DOverlayQuadSlotRaw *slotPtr = D3DOverlayQuadSlot(slot);
        if (slotPtr->dataPtrCopy != NULL)
            continue;

        slotPtr->dataPtrCopy = data;
        slotPtr->vertexCount = vertexCount;

        slotPtr->header = 1;
        slotPtr->dataPtr = NULL;
        slotPtr->reserved08 = 0;
        D3DResource_Register(slotPtr, (uint32_t)(uintptr_t)data);

        return slot;
    }

    D3DSeamTableExhaustedWarning("overlay quad buffer", D3DSEAM_VTX_IDX_LEAK_CONTEXT);
    return 0;
}

// Frees a slot registered by d3dRegisterOverlayBuffer (no-op for slot 0) - just zeroes dataPtrCopy, the same
// field the registration side tests for "is this slot free".
//
// AUTOINJECT
void d3dReleaseOverlayBuffer(int overlaySlot) {
    if (overlaySlot != 0)
        D3DOverlayQuadSlot(overlaySlot)->dataPtrCopy = NULL;
}

// Draws a small textured quad (reticle/crosshair-style overlay, per the earlier audit's read of this
// function) previously registered via d3dRegisterOverlayBuffer, bound to texture stage 3. sizeParam gets
// scaled by the viewport's aspect-ratio-correction factor and clamped to [10,500]; the other two float/int
// params feed shader constant 0x68 directly (their exact visual role wasn't traced further). Opaque
// texture-stage-3 configuration registers below mirror d3dSetTextureStage1's own pattern, just for stage 3.
#define Gfx_FallbackOverlayTexture U32_AT(0x002CC3E8) // Gfx.field27554_0x6c98 - used when textureSlot==0
#define Gfx_OverlayVertexShaderHandle U32_AT(0x002C5748) // not in the Gfx struct - a dedicated single handle, not part of VtxShaderHandles[]

#define D3D8_TexStage3_0x80 D3D8_TRACED("D3D8_TexStage3_0x80", 0x00111980)
#define D3D8_TexStage3_0x88 D3D8_TRACED("D3D8_TexStage3_0x88", 0x00111988)
#define D3D8_TexStage3_0x8c D3D8_TRACED("D3D8_TexStage3_0x8c", 0x0011198C)
#define D3D8_TexStage3_0x90 D3D8_TRACED("D3D8_TexStage3_0x90", 0x00111990)
#define D3D8_TexStage3_0x98 D3D8_TRACED("D3D8_TexStage3_0x98", 0x00111998)
#define D3D8_TexStage3_0x9c D3D8_TRACED("D3D8_TexStage3_0x9c", 0x0011199C)
#define D3D8_TexStage3_0xba8 D3D8_TRACED("D3D8_TexStage3_0xba8", 0x00111BA8)
#define D3D8_TexStage3_0xbac D3D8_TRACED("D3D8_TexStage3_0xbac", 0x00111BAC)

// AUTOINJECT
void d3dDrawOverlayQuad(int overlaySlot, float sizeParam, int textureSlot, float param4, int param5) {
    if (overlaySlot == 0)
        return;

    float aspectScale = 640.0f / (float)Gfx_ViewportWidth;
    if (aspectScale <= 480.0f / (float)Gfx_ViewportHeight)
        aspectScale = 480.0f / (float)Gfx_ViewportHeight;
    sizeParam = (sizeParam / aspectScale) * 5.0f;
    if (sizeParam < 10.0f) sizeParam = 10.0f;
    else if (sizeParam > 500.0f) sizeParam = 500.0f;

    if (textureSlot == 0)
        textureSlot = (int)Gfx_FallbackOverlayTexture;

    if (D3D_DeviceReady != 0) {
        D3DDevice_SetTexture(3, D3DTextureSlot(textureSlot)->baseTexture);
        if (D3D_DeviceReady != 0) {
            D3D8_PushBufferDirtyFlags |= 0x900;
            D3D8_TexStage3_0x88 = 0;
            D3D8_TexStage3_0x80 = 5;
            D3D8_TexStage3_0x8c = 2;
            D3D8_TexStage3_0x98 = 0;
            D3D8_TexStage3_0x90 = 4;
            D3D8_TexStage3_0x9c = 2;
            D3D8_TexStage3_0xba8 = 1;
            D3D8_TexStage3_0xbac = 1;
        }
    }

    float constants[4];
    constants[0] = Gfx_FogScale * sizeParam;
    constants[1] = param4 * 0.5f;
    constants[2] = (float)param5;
    constants[3] = (float)(param5 * param5) * param4 * 0.5f;
    Gfx_D3DLastError = 0;
    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstant1(0x68, constants);
    Gfx_D3DLastError = 0;

    // These two share their cache fields with d3dSetRenderState/d3dSetRenderState2 (same addresses, confirmed
    // via raw disassembly) - calling them directly reuses the already-verified cache-check-and-call logic
    // rather than re-deriving the SetRenderState_Simple trampoline calls a second time.
    d3dSetRenderState(1);   // D3D_ZFuncCache -> LEQUAL
    d3dSetRenderState2(0);  // D3D_DepthMaskCache -> mask off

    if (D3D_DeviceReady != 0) {
        D3DDevice_SetStreamSource(0, D3DOverlayQuadSlot(overlaySlot), 0x24);
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetVertexShader((void *)(uintptr_t)Gfx_OverlayVertexShaderHandle);
    }
    Gfx_D3DLastError = 0;

    Gfx_MiscResetFlag = 0xFFFFFFFFu;
    Gfx_CurrentStreamBuffer = 0xFFFFFFFFu;
    Gfx_CurrentIndexBuffer = 0xFFFFFFFFu;

    if (D3D_DeviceReady != 0) {
        D3DDevice_DrawVertices(1, 0, D3DOverlayQuadSlot(overlaySlot)->vertexCount);
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady != 0) {
            D3DDevice_SetTexture(3, NULL);
            if (D3D_DeviceReady != 0) {
                D3D8_PushBufferDirtyFlags |= 0x900;
                D3D8_TexStage3_0x80 = 1;
                D3D8_TexStage3_0xba8 = 0;
                D3D8_TexStage3_0xbac = 0;
            }
        }
    }
    Gfx_D3DLastError = 0;
}

// Resets the render target/texture-stage-1/current-texture/stream-buffer bindings to a clean default state -
// purely a composition of already-implemented functions, no new D3D8 calls. Has ZERO xrefs anywhere in the
// binary (confirmed via get_xrefs_to) - either genuinely dead code (an unshipped debug path) or reached only
// via an indirect/function-pointer call Ghidra hasn't resolved. Implemented anyway since it's simple and safe
// (every call it makes is to functions we've already verified), even though its current runtime relevance is
// unclear.
//
// Calls _d3dRenderTargetSetup(0) directly (the real implementation, same translation unit) rather than the
// public d3dRenderTargetSetup() - that's a __declspec(naked) entry trampoline expecting its parameter in ESI
// from the original's own callers, which a normal C++ call site can't set correctly.
//
// AUTOINJECT
void d3dResetRenderTargetAndBuffers(void) {
    _d3dRenderTargetSetup(0);
    d3dSetTextureStage1(0, 0);

    if (Gfx_CurrentlyLoadedTexture != 0) {
        Gfx_CurrentlyLoadedTexture = 0;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetTexture(0, D3DTextureSlot(0)->baseTexture);
        Gfx_D3DLastError = 0;
    }

    d3dBindBuffers(0, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// maybeImmediateModePushItem
// ---------------------------------------------------------------------------------------------------------------

#define Gfx_ImmediateModeItemCount U32_AT(0x002C6F70) // Gfx.maybeImmediateModeItemCount
#define Gfx_ImmediateModeBuffer ((float*)0x002DE3ECu)  // Gfx.maybeImmediateModeBuffer[64][9], stride 9 floats
#define Gfx_ImmediateModeVertexBuffer ((float*)0x002C5770u) // Gfx.field20_0x20 - the actual vertex-buffer base passed to DrawVerticesUP; the transform loop below writes starting 2 floats in (Gfx.field22_0x28)
#define Gfx_ImmediateModeVertexShader PTR_AT(0x002C574Cu)  // untraced - a global holding a vertex-shader HANDLE value (must be dereferenced, not used as the handle itself), distinct from the VtxShaderHandles array drawShard/d3dDrawOverlayQuad use
#define Gfx_ZBiasActive U32_AT(0x002C6FD0)  // Gfx.field6071_0x1880 - defined again, identically, down by maybeResetRenderState; needed here too since this function comes first in the file

// Carries Ghidra's own "type propagation algorithm not settling" decompiler-confidence warning, tied to a
// loop-invariant (the texture-height reciprocal) that the original keeps resident on the x87 FPU register
// stack for the whole loop rather than spilling to memory - confirmed via raw disassembly that despite the
// warning, decompile's actual data movements (which value goes to which output offset) are correct; every
// offset below was cross-checked against the raw byte offsets in the compiled function, not just trusted from
// decompile. Transforms up to 64 pending "immediate mode" quads (position rect + UV rect + packed colour, see
// maybeImmediateModePushItem) into an axis-aligned textured-quad vertex buffer (4 vertices per item, 6 floats
// per vertex - the 6th is left untouched/stale, matching the original exactly) and draws them in one
// D3DDevice_DrawVerticesUP call. UVs are normalized by the currently-loaded texture's own width/height unless
// its nonSwizzled flag is set, in which case they're used as-is (fWidth/fHeight forced to 1.0).
//
// AUTOINJECT
void maybeImmediateModeFlush(void) {
    if ((int32_t)Gfx_ImmediateModeItemCount < 1)
        return;

    D3DTextureSlotRaw *tex = D3DTextureSlot((int)Gfx_CurrentlyLoadedTexture);
    float fWidth = (float)(int16_t)tex->width;
    if (fWidth != 0.0f)
        fWidth = 1.0f / fWidth;
    float fHeight = (float)(int16_t)tex->height;
    if (fHeight != 0.0f)
        fHeight = 1.0f / fHeight;
    if (tex->nonSwizzled != 0) {
        fWidth = 1.0f;
        fHeight = 1.0f;
    }

    float *src = Gfx_ImmediateModeBuffer + 1;       // matches the original's pfVar14 = maybeImmediateModeBuffer[0]+1
    float *dst = Gfx_ImmediateModeVertexBuffer + 2;  // matches the original's pfVar13 = &Gfx.field22_0x28

    for (uint32_t i = 0; i < Gfx_ImmediateModeItemCount; i++) {
        float x0 = *(src - 1);
        float y0 = src[0];
        float xWidth = src[1];
        float yHeight = src[2];
        float u0 = src[3];
        float v0 = src[4];
        float uWidth = src[5];
        float vHeight = src[6];
        uint32_t colourBits = *(uint32_t*)&src[7];

        float xLeft = x0 - 0.03125f;
        float yTop = y0 - 0.03125f;
        float xRight = (xWidth + x0) - 0.03125f;
        float yBottom = (yHeight + y0) - 0.03125f;
        float uLeft = u0 * fWidth;
        float uRight = (uWidth + u0) * fWidth;
        float vTop = v0 * fHeight;
        float vBottom = (vHeight + v0) * fHeight;

        dst[-2] = xLeft;
        dst[-1] = yTop;
        *(uint32_t*)&dst[0]    = colourBits;
        *(uint32_t*)&dst[6]    = colourBits;
        *(uint32_t*)&dst[0xc]  = colourBits;
        *(uint32_t*)&dst[0x12] = colourBits;
        dst[1] = uLeft;
        dst[0x13] = uLeft;
        dst[2] = vTop;
        dst[4] = xRight;
        dst[10] = xRight;
        dst[5] = yTop;
        dst[7] = uRight;
        dst[0xd] = uRight;
        dst[8] = vTop;
        dst[0xb] = yBottom;
        dst[0xe] = vBottom;
        dst[0x14] = vBottom;
        dst[0x10] = xLeft;
        dst[0x11] = yBottom;

        src += 9;
        dst += 0x18;
    }

    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShader(Gfx_ImmediateModeVertexShader);

    Gfx_MiscResetFlag = 0xFFFFFFFFu;
    Gfx_CurrentStreamBuffer = 0xFFFFFFFFu;
    Gfx_CurrentIndexBuffer = 0xFFFFFFFFu;
    Gfx_D3DLastError = 0;

    if (Gfx_ZBiasActive != 0) {
        Gfx_ZBiasActive = 0;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetRenderState_ZBias(0);
        Gfx_D3DLastError = 0;
    }

    if (D3D_DeviceReady != 0)
        D3DDevice_DrawVerticesUP(8, Gfx_ImmediateModeItemCount * 4, Gfx_ImmediateModeVertexBuffer, 0x18);

    Gfx_D3DLastError = 0;
    Gfx_ImmediateModeItemCount = 0;
}

// AUTOINJECT
void maybeImmediateModePushItem(float param1, float param2, float param3, float param4, float param5,
                                 float param6, float param7, float param8, float param9) {
    if (Gfx_ImmediateModeItemCount > 63)
        maybeImmediateModeFlush();

    float *item = Gfx_ImmediateModeBuffer + (size_t)Gfx_ImmediateModeItemCount * 9;
    item[0] = param1;
    item[1] = param2;
    item[2] = param3;
    item[3] = param4;
    item[4] = param5;
    item[5] = param6;
    item[6] = param7;
    item[7] = param8;
    item[8] = param9;

    Gfx_ImmediateModeItemCount = Gfx_ImmediateModeItemCount + 1;
}

// ---------------------------------------------------------------------------------------------------------------
// createProjectionMatrix
// ---------------------------------------------------------------------------------------------------------------

// Standard D3D-style perspective projection matrix builder - pure arithmetic, no D3D8 calls, no aliasing risk.
// 0.008726646 is exactly half the degrees-to-radians factor (pi/180/2), matching the standard
// 1/tan(halfFovRadians) projection-scale formula. Ported directly from decompile (trusted for pure arithmetic
// per this project's established policy - the raw disassembly is a long, branchy FPU comparison sequence that
// would be far more error-prone to hand-transcribe than to verify decompile's already-correctly-reconstructed
// branch structure against all 4 (fov==0?, param4==0?) combinations, which was done here).
//
// AUTOINJECT
void createProjectionMatrix(D3DMATRIX *mtxOut, float aspect, float fov, float param4, float nearDist, float farDist) {
    if (aspect == 0.0f) aspect = 1.0f;
    if (nearDist == 0.0f) nearDist = 1.0f;
    if (farDist == 0.0f) farDist = 1000.0f;

    double fVar4 = (double)param4;

    if (fov == 0.0f && param4 == 0.0f)
        fov = 75.0f;

    if (fov != 0.0f && param4 != 0.0f) {
        // both FOV and the secondary angle explicitly given
        if (fov < 1.0f) fov = 1.0f;
        else if (fov > 179.0f) fov = 179.0f;
        if (fVar4 < 1.0) fVar4 = 1.0;
        else if (fVar4 > 179.0) fVar4 = 179.0;
        fov = (float)(1.0 / tan(fov * 0.008726646));
        fVar4 = 1.0 / tan(fVar4 * 0.008726646);
    } else if (fov != 0.0f) {
        // FOV only - aspect derives the secondary scale
        fVar4 = (double)fov;
        if (fVar4 < 1.0) fVar4 = 1.0;
        else if (fVar4 > 179.0) fVar4 = 179.0;
        double invTan = 1.0 / tan(fVar4 * 0.008726646);
        fov = (float)invTan;
        fVar4 = invTan * (double)aspect;
    } else {
        // secondary angle only - aspect derives FOV
        if (fVar4 < 1.0) fVar4 = 1.0;
        else if (fVar4 > 179.0) fVar4 = 179.0;
        fVar4 = 1.0 / tan(fVar4 * 0.008726646);
        fov = (float)(fVar4 / (double)aspect);
    }

    float zScale = farDist / (farDist - nearDist);
    memset(mtxOut, 0, sizeof(D3DMATRIX));
    mtxOut->f[0] = fov;
    mtxOut->f[0xe] = 1.0f;
    mtxOut->f[5] = (float)fVar4;
    mtxOut->f[10] = zScale;
    mtxOut->f[0xb] = -(zScale * nearDist);
}

// ---------------------------------------------------------------------------------------------------------------
// d3dResetTransformCaches (FUN_000e6260)
// ---------------------------------------------------------------------------------------------------------------

// A simple, fixed 4-iteration loop (confirmed via raw disassembly - initially looked more complex from the
// decompile alone, but it's genuinely straightforward): for each of 4 "slots", clears bit N in one bitmask and
// sets bit N in another, zeros a 3-float entry, and once the first bitmask reaches zero, clears bits 2-3 of
// Gfx_MiscModeFlags. Untraced overall purpose beyond "resets some per-slot transform-related cache state."
#define Gfx_TransformCacheBitmaskA U32_AT(0x002FF274)
#define Gfx_TransformCacheBitmaskB U32_AT(0x002FF270)
#define Gfx_TransformCacheFloats ((float*)0x002FF2C0u) // 4 entries, stride 4 floats (16 bytes); only offsets -1,0,+1 (relative to each entry's base) are touched

// AUTOINJECT
void d3dResetTransformCaches(void) {
    gfxSetCharacterLightIntensity(0.0f);

    for (int i = 0; i < 4; i++) {
        uint32_t bit = 1u << i;
        Gfx_TransformCacheBitmaskA &= ~bit;
        Gfx_TransformCacheBitmaskB |= bit;

        float *entry = Gfx_TransformCacheFloats + (size_t)i * 4;
        entry[-1] = 0.0f;
        entry[0] = 0.0f;
        entry[1] = 0.0f;

        if (Gfx_TransformCacheBitmaskA == 0)
            Gfx_MiscModeFlags &= ~0xCu;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// d3dSetupRenderStatesAndFog
// ---------------------------------------------------------------------------------------------------------------

// A fog-mode state machine (param 0/1/2, meaning untraced beyond the method/value pairs below) that also
// re-sends the current fog colour (shared logic with d3dSetFogEnable/d3dSetFogColor's own masking formula -
// confirmed identical via raw disassembly). All SetRenderState_Simple method/value pairs confirmed via raw
// disassembly (decompile can't show them at all, since it doesn't understand that function's ECX/EDX
// convention) - param_1==1 and param_1==2 share a tail (constant 0x40348 method gets value 1); any other
// param_1 value takes a separate, early-returning path (same method gets value 0x303 instead).
#define D3D8_RS_BlendOp D3D8_TRACED("D3D8_RS_BlendOp", 0x00111AF8)

// AUTOINJECT
void d3dSetupRenderStatesAndFog(int param1) {
    if (Gfx_FogModeFlag == (uint32_t)param1) {
        if (Gfx_FogEnabledCache != 1)
            return;
        uint32_t maskedColor = ((Gfx_FogModeFlag != 0) ? 0u : 0xFFFFFFFFu) & Gfx_FogColorCache;
        Gfx_FogColorMasked = maskedColor;
        if (D3D_DeviceReady == 0) {
            Gfx_D3DLastError = 0;
            return;
        }
        D3DDevice_SetRenderState_FogColor(maskedColor);
        Gfx_D3DLastError = 0;
        return;
    }

    Gfx_FogModeFlag = (uint32_t)param1;
    if (Gfx_FogEnabledCache == 1) {
        uint32_t maskedColor = ((param1 != 0) ? 0u : 0xFFFFFFFFu) & Gfx_FogColorCache;
        Gfx_FogColorMasked = maskedColor;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetRenderState_FogColor(maskedColor);
        Gfx_D3DLastError = 0;
    }

    if (param1 == 1) {
        if (D3D_DeviceReady == 0) {
            Gfx_D3DLastError = 0;
            return;
        }
        D3D_SetRenderStateSimple(0x40350, 0x8006);
        D3D8_RS_BlendOp = 0x8006;
    } else if (param1 != 2) {
        if (D3D_DeviceReady == 0) {
            Gfx_D3DLastError = 0;
            return;
        }
        D3D_SetRenderStateSimple(0x40350, 0x8006);
        D3D8_RS_BlendOp = 0x8006;
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady == 0) {
            Gfx_D3DLastError = 0;
            return;
        }
        D3D_SetRenderStateSimple(0x40344, 0x302);
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady == 0) {
            Gfx_D3DLastError = 0;
            return;
        }
        D3D_SetRenderStateSimple(0x40348, 0x303);
        Gfx_D3DLastError = 0;
        return;
    } else {
        // param1 == 2
        if (D3D_DeviceReady == 0) {
            Gfx_D3DLastError = 0;
            return;
        }
        D3D_SetRenderStateSimple(0x40350, 0x800b);
        D3D8_RS_BlendOp = 0x800b;
    }

    // Shared tail: only reached for param1==1 or param1==2.
    Gfx_D3DLastError = 0;
    if (D3D_DeviceReady != 0) {
        D3D_SetRenderStateSimple(0x40344, 0x302);
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady != 0)
            D3D_SetRenderStateSimple(0x40348, 1);
    }
    Gfx_D3DLastError = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// maybeResetRenderState, maybeD3dShutdown
// ---------------------------------------------------------------------------------------------------------------

#define Gfx_ZBiasActive U32_AT(0x002C6FD0)  // Gfx.field6071_0x1880
#define D3D8_State0x40358_LastValue U32_AT(0x002C575C) // Gfx+0xc (an untraced early-offset field) - shared with d3dSetup's own use of the same opaque method

// Resets a batch of cached render state to known defaults - texture stage 0, the transform caches
// (d3dResetTransformCaches), colour constant 0x67, fog, depth-test/depth-write (reusing our own
// d3dSetRenderState/d3dSetRenderState2 directly - confirmed via raw disassembly that this function's own
// field6057/field6058 cache checks and value formulas are IDENTICAL to those two functions', not just similar),
// z-bias, fog render states (d3dSetupRenderStatesAndFog), texture stage 1, cull mode, alpha ref (reusing
// d3dSetRenderState1 directly, same reasoning), and one more opaque D3D8 register (method 0x40358, shared with
// d3dSetup's own use of it) before finally rebinding buffers.
//
// AUTOINJECT
void maybeResetRenderState(char param1) {
    if (Gfx_CurrentlyLoadedTexture != 0) {
        Gfx_CurrentlyLoadedTexture = 0;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetTexture(0, D3DTextureSlot(0)->baseTexture);
        Gfx_D3DLastError = 0;
    }

    d3dResetTransformCaches();
    d3dSetColorConstant67(0xFFFFFFFFu);
    Gfx_FogEnabledCache = 0;
    Gfx_D3DLastError = 0;
    if (D3D_DeviceReady != 0) {
        D3D8_PushBufferDirtyFlags |= 0x2000;
        D3D8_RS_FogEnable = 0;
    }

    int resetValue = (param1 == 0) ? 1 : 0;
    d3dSetRenderState2(resetValue);
    d3dSetRenderState(resetValue);

    if (Gfx_ZBiasActive != 0) {
        Gfx_ZBiasActive = 0;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetRenderState_ZBias(0);
    }
    Gfx_D3DLastError = 0;

    d3dSetupRenderStatesAndFog(0);

    if (Gfx_DeferredTexStateA != 1 || Gfx_DeferredTexStateB != 1) {
        Gfx_DeferredTexStateA = 1;
        Gfx_DeferredTexStateB = 1;
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady != 0) {
            D3D8_PushBufferDirtyFlags |= 1;
            D3D8_Stage0_AddressU = 1;
            D3D8_Stage0_AddressV = 1;
        }
    }

    d3dSetTextureStage1(0, 0);

    if (Gfx_CurrentCullMode != 1) {
        Gfx_CurrentCullMode = 1;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetRenderState_CullMode(0x901);
        Gfx_D3DLastError = 0;
    }

    d3dSetRenderState1(1);

    if (Gfx_ExtraBlendA != 1 || Gfx_ExtraBlendB != 1 || Gfx_ExtraBlendC != 1) {
        Gfx_ExtraBlendA = 1;
        Gfx_ExtraBlendB = 1;
        Gfx_ExtraBlendC = 1;
        D3D8_State0x40358_LastValue = 0x1010101;
        if (D3D_DeviceReady != 0)
            D3D_SetRenderStateSimple(0x40358, 0x1010101);
        Gfx_D3DLastError = 0;
    }

    d3dBindBuffers(0, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// d3dSetDeferredTextureState, d3dSetTextureWithBorderColor
// ---------------------------------------------------------------------------------------------------------------

#define Gfx_DeferredTexBorderColorCache U32_AT(0x002C6FB8) // Gfx.field6062_0x1868 - shares its "changed?" pair check with Gfx_ShardUseAltShader (field6061), which d3dSetTextureWithBorderColor also writes as a texture-slot cache

// Caches a pair of "deferred texture state" values and, once changed, pushes them into the two D3D8-internal
// globals as a {1 if nonzero, 3 if zero} encoding - untraced overall meaning, ported verbatim. Shares its cache
// fields with maybeResetRenderState's own use of the same pair (which resets both to 1).
//
// AUTOINJECT
void d3dSetDeferredTextureState(int param1, int param2) {
    if (Gfx_DeferredTexStateA != (uint32_t)param1 || Gfx_DeferredTexStateB != (uint32_t)param2) {
        Gfx_DeferredTexStateA = (uint32_t)param1;
        Gfx_DeferredTexStateB = (uint32_t)param2;
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady != 0) {
            D3D8_PushBufferDirtyFlags |= 1;
            D3D8_Stage0_AddressU = (param1 == 0) ? 3 : 1;
            D3D8_Stage0_AddressV = (param2 == 0) ? 3 : 1;
        }
    }
}

// Binds a texture (by slot, to stage 0) together with a border colour used for clamp-to-border wrapping - or,
// with textureSlot 0, unbinds and restores the plain deferred-texture-state pair saved beforehand. Reuses
// Gfx_ShardUseAltShader/Gfx_DeferredTexBorderColorCache as its own "did this change?" cache pair (same fields,
// different call site - see their own comments).
//
// AUTOINJECT
void d3dSetTextureWithBorderColor(int textureSlot, int borderColour) {
    if (Gfx_ShardUseAltShader == (uint32_t)textureSlot && Gfx_DeferredTexBorderColorCache == (uint32_t)borderColour)
        return;

    Gfx_ShardUseAltShader = (uint32_t)textureSlot;
    Gfx_DeferredTexBorderColorCache = (uint32_t)borderColour;

    if (textureSlot == 0) {
        Gfx_MiscModeFlags &= 0xFFFFFFBFu;

        if (Gfx_CurrentlyLoadedTexture != 0) {
            Gfx_CurrentlyLoadedTexture = 0;
            if (D3D_DeviceReady != 0)
                D3DDevice_SetTexture(0, D3DTextureSlot(0)->baseTexture);
            Gfx_D3DLastError = 0;
        }

        uint32_t savedA = Gfx_DeferredTexStateA;
        uint32_t savedB = Gfx_DeferredTexStateB;
        Gfx_DeferredTexStateA = 0xFFFFFFFFu;
        Gfx_DeferredTexStateB = 0xFFFFFFFFu;
        d3dSetDeferredTextureState((int)savedA, (int)savedB);
        return;
    }

    Gfx_MiscModeFlags |= 0x40;
    Gfx_D3DLastError = 0;
    if (D3D_DeviceReady != 0) {
        D3D8_PushBufferDirtyFlags |= 1;
        D3D8_Stage0_AddressU = 4;
        D3D8_Stage0_AddressV = 4;
        D3DDevice_SetTextureState_BorderColor(0, (uint32_t)borderColour);
        Gfx_D3DLastError = 0; // confirmed via raw disasm: only cleared here if the device was actually ready
    }

    if (Gfx_CurrentlyLoadedTexture != (uint32_t)textureSlot) {
        Gfx_CurrentlyLoadedTexture = (uint32_t)textureSlot;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetTexture(0, D3DTextureSlot(textureSlot)->baseTexture);
        Gfx_D3DLastError = 0; // ditto - only cleared when this inner block actually ran (confirmed via raw disasm)
    }
}

// ---------------------------------------------------------------------------------------------------------------
// d3dSetViewMatrixFromRigidTransform, d3dBeginEndAuxRenderPass
// ---------------------------------------------------------------------------------------------------------------

// Rebuilds Gfx_ViewMatrixCache from a rigid transform: caches the transform itself (Gfx_SecondaryBasisMatrix),
// inverts a copy of it, then combines that inverse with Gfx_ProjMatrixCacheB (the "not yet ported" reader this
// cache's own comment anticipated - this is that reader) via Multiply4x4RowMajor rather than
// maybeMultiplyMatrixChain (same alias-safety reasoning as d3dSetMatrix/maybeBuildAndSetModelViewProjectionMtx).
//
// AUTOINJECT
void d3dSetViewMatrixFromRigidTransform(D3DMATRIX *rigidTransform) {
    Gfx_MatrixGenFlag1 = 0xFFFFFFFFu;
    Gfx_MatrixGenFlag2 = 0xFFFFFFFFu;

    memcpy(Gfx_SecondaryBasisMatrix, rigidTransform, sizeof(D3DMATRIX));

    D3DMATRIX inverted;
    memcpy(&inverted, Gfx_SecondaryBasisMatrix, sizeof(D3DMATRIX));
    maybeInvertRigidTransform(&inverted);

    Multiply4x4RowMajor(Gfx_ProjMatrixCacheB, &inverted, Gfx_ViewMatrixCache);
}

#define Gfx_AuxSavedViewMatrix   ((D3DMATRIX*)0x002FF3B4) // not in the Gfx struct - saves Gfx_SecondaryBasisMatrix across a d3dBeginEndAuxRenderPass(1, ...)/(0, ...) pair
#define Gfx_AuxSavedProjMatrix   ((D3DMATRIX*)0x002FF3F4) // not in the Gfx struct - saves Gfx_ProjMatrixCacheA across the same pair
#define Gfx_AuxRenderPassResult  U32_AT(0x002FF3AC)       // not in the Gfx struct - written by d3dInitShadowBlurTextures (once, at boot), only read/returned here
#define Gfx_AuxRenderPassActive  U8_AT(0x002FF495)        // not in the Gfx struct - a static "is a pass currently pushed?" latch, one byte past Gfx_RenderTargetPushed but a separate flag

// A single-level "auxiliary render pass" push/pop, used for rendering something (character shadows are the
// likely case, going by the call pattern) into whatever d3dRenderTargetSetup's own single-level stack has
// pushed. begin != 0 pushes: saves the current view/projection matrices, zeroes shader constant 0x75's first
// two floats (same fields d3dBeginFrame zeroes each frame), pushes the render target, clears it, then rebuilds
// the view matrix from rigidTransform (rotation-only, via maybeTransposeRotationPart) combined with projMtx.
// begin == 0 pops: restores the saved matrices and render target/viewport, but only if a pass is actually active
// (matching the original's own latch check exactly). Returns Gfx_AuxRenderPassResult either way.
//
// AUTOINJECT
uint32_t d3dBeginEndAuxRenderPass(char begin, D3DMATRIX *rigidTransform, D3DMATRIX *projMtx) {
    if (begin != 0) {
        Gfx_AuxRenderPassActive = 1;
        Gfx_ShaderConstant75[0] = 0.0f;
        Gfx_ShaderConstant75[1] = 0.0f;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetVertexShaderConstant1(0x75, Gfx_ShaderConstant75);
        Gfx_D3DLastError = 0;

        // Confirmed via raw disassembly: the original loads ESI from Gfx_AuxRenderPassResult (not a fixed 0/nonzero
        // constant) before this particular push - calling the real implementation directly with that same value,
        // per the naked-trampoline warning in d3dSeam.h (never call the public d3dRenderTargetSetup() from new code).
        _d3dRenderTargetSetup((int)Gfx_AuxRenderPassResult);

        if (D3D_DeviceReady != 0)
            D3DDevice_Clear(0, NULL, 0xf3, 0, 1.0f, 0);
        Gfx_D3DLastError = 0;

        memcpy(Gfx_AuxSavedViewMatrix, Gfx_SecondaryBasisMatrix, sizeof(D3DMATRIX));
        memcpy(Gfx_AuxSavedProjMatrix, Gfx_ProjMatrixCacheA, sizeof(D3DMATRIX));

        d3dSetProjectionMatrix(projMtx);

        D3DMATRIX rotationOnly;
        memcpy(&rotationOnly, rigidTransform, sizeof(D3DMATRIX));
        maybeTransposeRotationPart(&rotationOnly);
        d3dSetViewMatrixFromRigidTransform(&rotationOnly);

        d3dSetMatrix(Gfx_d3dActiveMatrix);
        return Gfx_AuxRenderPassResult;
    }

    if (Gfx_AuxRenderPassActive != 0) {
        Gfx_AuxRenderPassActive = 0;
        Gfx_ShaderConstant75[0] = 0.0f;
        Gfx_ShaderConstant75[1] = 0.0f;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetVertexShaderConstant1(0x75, Gfx_ShaderConstant75);
        Gfx_D3DLastError = 0;

        d3dSetProjectionMatrix(Gfx_AuxSavedProjMatrix);
        d3dSetViewMatrixFromRigidTransform(Gfx_AuxSavedViewMatrix);

        _d3dRenderTargetSetup(0); // pop back to the saved render target
        d3dSetupViewportDimensions(Gfx_ViewportX, Gfx_ViewportY, Gfx_ViewportWidth, Gfx_ViewportHeight);
    }
    return Gfx_AuxRenderPassResult;
}

// Defined below psiBlurScreen (it needs that function's history-slot globals). Not an original function - see
// its own comment for why it exists.
static void d3dReleaseLevelResources(void);

// The original is exactly the first three steps below. The trailing d3dReleaseLevelResources call is OUR
// addition (see that function's comment): this function is only ever reached on a level change
// (ResetMap_Load -> maybePsiResetResources -> maybeCleanupSystem -> here) or on the two XLaunchNewImage
// relaunch paths (SetLaunchInfoAndLaunch / WriteStateFileAndLaunch), i.e. exactly the points where every
// level-scoped D3D resource is dead, so it's the natural home for the bulk release the original never had.
// The release runs last, after maybeResetRenderState/d3dBindBuffers have already unbound texture stages 0/1
// and the stream/index buffers, so nothing freed here is still bound.
//
// AUTOINJECT
void maybeD3dShutdown(void) {
    maybeResetRenderState(1);

    if (Gfx_CurrentlyLoadedTexture != 0) {
        Gfx_CurrentlyLoadedTexture = 0;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetTexture(0, D3DTextureSlot(0)->baseTexture);
        Gfx_D3DLastError = 0;
    }

    d3dBindBuffers(0, 0);

    d3dReleaseLevelResources();
    D3DSeamTraceLevelReset();
}

// ---------------------------------------------------------------------------------------------------------------
// d3dSetup
// ---------------------------------------------------------------------------------------------------------------

#define D3D8_RS_0x4033c_LastValue D3D8_TRACED("D3D8_RS_0x4033c_LastValue", 0x00111AB8) // opaque, untraced; only ever written here
#define D3D8_RS_AlphaBlendEnable D3D8_TRACED("D3D8_RS_AlphaBlendEnable", 0x00111ABC) // X_D3DRS_ALPHABLENDENABLE (render-state index 59); only ever written here
#define D3D8_RS_0x40300_LastValue D3D8_TRACED("D3D8_RS_0x40300_LastValue", 0x00111AC0) // opaque, untraced; only ever written here
#define D3D8_RS_DitherEnable D3D8_TRACED("D3D8_RS_DitherEnable", 0x00111AD4) // X_D3DRS_DITHERENABLE (render-state index 65), poked directly via D3D_SetRenderStateSimple
#define D3D8_ShaderConstantSubIndexTable ((int32_t*)0x001B5208) // untraced - ~53 ints, each added to 0x60 to form a shader-constant register index

// Per-texture-stage-like opaque D3D8-internal registers, four groups spaced 0x80 apart, written unconditionally
// (given device ready) every time d3dSetup runs. Untraced overall meaning - ported verbatim from decompile
// (cross-checked against raw disassembly's own store addresses/values one by one, all confirmed to match).
#define D3D8_Stage0_0x00 D3D8_TRACED("D3D8_Stage0_0x00", 0x00111800)
#define D3D8_Stage0_0x08 D3D8_TRACED("D3D8_Stage0_0x08", 0x00111808)
#define D3D8_Stage0_0x0c D3D8_TRACED("D3D8_Stage0_0x0c", 0x0011180C)
#define D3D8_Stage0_0x10 D3D8_TRACED("D3D8_Stage0_0x10", 0x00111810)
#define D3D8_Stage0_0x18 D3D8_TRACED("D3D8_Stage0_0x18", 0x00111818)
#define D3D8_Stage0_0x1c D3D8_TRACED("D3D8_Stage0_0x1c", 0x0011181C)
#define D3D8_PreStage_0x00 D3D8_TRACED("D3D8_PreStage_0x00", 0x001117DC)
#define D3D8_PreStage_0x04 D3D8_TRACED("D3D8_PreStage_0x04", 0x001117E0)
#define D3D8_PreStage_0x08 D3D8_TRACED("D3D8_PreStage_0x08", 0x001117E4)
#define D3D8_Stage1_0x5c D3D8_TRACED("D3D8_Stage1_0x5c", 0x0011185C)
#define D3D8_Stage1_0x60 D3D8_TRACED("D3D8_Stage1_0x60", 0x00111860)
#define D3D8_Stage1_0x64 D3D8_TRACED("D3D8_Stage1_0x64", 0x00111864)
#define D3D8_Stage2_0x00 D3D8_TRACED("D3D8_Stage2_0x00", 0x00111900)
#define D3D8_Stage2_0x10 D3D8_TRACED("D3D8_Stage2_0x10", 0x00111910)
#define D3D8_Stage2_0xdc D3D8_TRACED("D3D8_Stage2_0xdc", 0x001118DC)
#define D3D8_Stage2_0xe0 D3D8_TRACED("D3D8_Stage2_0xe0", 0x001118E0)
#define D3D8_Stage2_0xe4 D3D8_TRACED("D3D8_Stage2_0xe4", 0x001118E4)
#define D3D8_Stage2_0xe8 D3D8_TRACED("D3D8_Stage2_0xe8", 0x001118E8)
#define D3D8_Stage3_0x00 D3D8_TRACED("D3D8_Stage3_0x00", 0x00111980)
#define D3D8_Stage3_0x10 D3D8_TRACED("D3D8_Stage3_0x10", 0x00111990)
#define D3D8_Stage3_0x5c D3D8_TRACED("D3D8_Stage3_0x5c", 0x0011195C)
#define D3D8_Stage3_0x60 D3D8_TRACED("D3D8_Stage3_0x60", 0x00111960)
#define D3D8_Stage3_0x64 D3D8_TRACED("D3D8_Stage3_0x64", 0x00111964)
#define D3D8_Stage3_0x68 D3D8_TRACED("D3D8_Stage3_0x68", 0x00111968)

#define Gfx_BasisScaleDiag ((float*)0x002FF288) // Gfx.field158589_0x39b38 - untraced; 4 floats, stride 16 bytes, all reset to 1.0 by d3dSetup

// One-time-per-call D3D8 device setup: resets the whole texture/stream/buffer cache block to -1, lazily enables
// a handful of always-on render states (alpha-test/z-bias/misc/fog-mode/yuv-mode registers, Z-enable/normalize-
// normals/vertex-blend), writes a large block of opaque per-texture-stage D3D8-internal registers to fixed
// baseline values (untraced meaning, ported verbatim), sets up the projection matrix (75 degree FOV, 4:3,
// 1.0-1000.0), shader constant 0x76 (a fixed {-96, 1, 256, 1/256} vector), an identity matrix broadcast across
// a lookup-table-driven set of shader constant registers (DAT_001b5208), a basis-scale reset, default character
// light intensity/fog/cull-mode/viewport, then finally reuses d3dResetTransformCaches/d3dSetupRenderStatesAndFog/
// d3dSetRenderState[1/2]/d3dSetFogEnable/d3dSetColorConstant67/d3dSetTextureWithBorderColor/d3dSetTextureStage1/
// d3dSetStreamSources exactly as the original calls them (not reimplemented separately here - see each of those
// for what they do). The three d3dSetRenderState/1/2 calls reuse the EXACT SAME cache fields as this function's
// own field6056/6057/6058 checks (D3D_AlphaRefCache/D3D_DepthMaskCache/D3D_ZFuncCache - confirmed via raw
// disassembly, address-for-address), so calling them directly instead of hand-duplicating that logic is safe.
//
// AUTOINJECT
void d3dSetup(void) {
    for (int i = 0; i < 0x15; i++)
        ((uint32_t*)0x002C6F84)[i] = 0xFFFFFFFFu; // Gfx.currentlyLoadedTexture and 20 more consecutive fields

    d3dSetRenderState1(1); // field6056_0x1850 / D3D_AlphaRefCache - method 0x40340

    if (D3D_DeviceReady != 0) {
        D3D_SetRenderStateSimple(0x4033c, 0x206);
        D3D8_RS_0x4033c_LastValue = 0x206;
        Gfx_D3DLastError = 0;

        D3D_SetRenderStateSimple(0x40304, 1);
        D3D8_RS_AlphaBlendEnable = 1;
        Gfx_D3DLastError = 0;

        D3D_SetRenderStateSimple(0x40300, 1);
        D3D8_RS_0x40300_LastValue = 1;
        Gfx_D3DLastError = 0;

        D3D_SetRenderStateSimple(0x40350, 0x8006);
        D3D8_RS_BlendOp = 0x8006;
        Gfx_D3DLastError = 0;

        D3D_SetRenderStateSimple(0x40310, 1);
        D3D8_RS_DitherEnable = 1;
        Gfx_D3DLastError = 0;

        D3DDevice_SetRenderState_ZEnable(2);
        Gfx_D3DLastError = 0;

        D3DDevice_SetRenderState_NormalizeNormals(1);
        Gfx_D3DLastError = 0;

        D3DDevice_SetRenderState_VertexBlend(0);
        Gfx_D3DLastError = 0;

        // Opaque per-stage D3D8-internal register baseline - untraced meaning, ported verbatim.
        D3D8_Stage0_0x08 = 2;
        D3D8_Stage0_0x00 = 5;
        D3D8_Stage0_0x0c = 0;
        D3D8_Stage0_0x18 = 2;
        D3D8_Stage0_0x10 = 4;
        D3D8_Stage0_0x1c = 0;
        D3D8_PreStage_0x00 = 2;
        D3D8_PreStage_0x04 = 2;
        D3D8_PreStage_0x08 = 2;
        D3D8_TexStage1_0x80 = 1;
        D3D8_TexStage1_0x90 = 1;
        D3D8_Stage1_0x5c = 2;
        D3D8_Stage1_0x60 = 2;
        D3D8_Stage1_0x64 = 2;
        D3D8_Stage2_0x00 = 1;
        D3D8_Stage2_0x10 = 1;
        D3D8_Stage2_0xdc = 2;
        D3D8_Stage2_0xe0 = 2;
        D3D8_Stage2_0xe4 = 2;
        D3D8_Stage2_0xe8 = 0xbf800000u; // -1.0f
        D3D8_Stage3_0x00 = 1;
        D3D8_Stage3_0x10 = 1;
        D3D8_Stage3_0x5c = 2;
        D3D8_Stage3_0x60 = 2;
        D3D8_Stage3_0x64 = 2;
        D3D8_Stage3_0x68 = 0xbf800000u; // -1.0f
        D3D8_PushBufferDirtyFlags |= 0x280f;
        U32_AT(0x00111B48) = 0;
        U32_AT(0x00111B4C) = 0x3f800000u; // 1.0f
        U32_AT(0x00111B50) = 0x3f800000u; // 1.0f
        U32_AT(0x001117E8) = 0xbf800000u; // -1.0f
        U32_AT(0x00111868) = 0xbf800000u; // -1.0f
        U32_AT(0x00111B44) = 1;
        U32_AT(0x00111B54) = 0;
    }
    Gfx_D3DLastError = 0;

    if (D3D_DeviceReady != 0)
        D3DDevice_SetShaderConstantMode(1);
    Gfx_D3DLastError = 0;

    D3DMATRIX projMtx;
    createProjectionMatrix(&projMtx, 1.3333334f, 75.0f, 0.0f, 1.0f, 1000.0f);
    d3dSetProjectionMatrix(&projMtx);

    float shaderConstant76[4] = { -96.0f, 1.0f, 256.0f, 1.0f / 256.0f };
    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstant1(0x76, shaderConstant76);
    Gfx_D3DLastError = 0;

    D3DMATRIX identity;
    d3dMatrixIdentity(&identity);
    for (int byteOffset = 0; byteOffset < 0xd4; byteOffset += 4) {
        if (D3D_DeviceReady != 0) {
            int constantIndex = *(int32_t*)((char*)D3D8_ShaderConstantSubIndexTable + byteOffset) + 0x60;
            D3DDevice_SetVertexShaderConstantNotInline(constantIndex, &identity, 0xc);
        }
        Gfx_D3DLastError = 0;
    }

    for (float *p = Gfx_BasisScaleDiag; p < (float*)0x002FF2C8; p += 4)
        *p = 1.0f;

    gfxSetCharacterLightIntensity(0.5f);
    Gfx_MatrixGenFlag2 = 1;
    Gfx_LevelDirectionVector[0] = 1.0f;
    Gfx_LevelDirectionVector[1] = 0.0f;
    Gfx_LevelDirectionVector[2] = 0.0f;
    U32_AT(0x002FF344) = 0x4b7fffffu; // not in the Gfx struct - untraced, a very large float sentinel
    d3dSetFogNear(10.0f);
    d3dSetFogFar(100.0f);

    if (Gfx_CurrentCullMode != 1) {
        Gfx_CurrentCullMode = 1;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetRenderState_CullMode(0x901);
        Gfx_D3DLastError = 0;
    }

    // The original hardcodes 480/640 here (the immediates Inject() used to patch at 0x000e6cd7/0x000e6ceb);
    // now taken from SCREEN_HEIGHT/SCREEN_WIDTH directly, same as xboxInitGraphics' backbuffer size.
    Gfx_ViewportHeight = SCREEN_HEIGHT;
    Gfx_ViewportX = 0;
    Gfx_ViewportY = 0;
    Gfx_ViewportWidth = SCREEN_WIDTH;
    if (D3D_DeviceReady != 0) {
        D3DVIEWPORT viewport;
        viewport.X = Gfx_ViewportX;
        viewport.Y = Gfx_ViewportY;
        viewport.Width = Gfx_ViewportWidth;
        viewport.Height = Gfx_ViewportHeight;
        viewport.MinZ = 0.0f;
        viewport.MaxZ = 1.0f;
        D3DDevice_SetViewport(&viewport);
    }
    Gfx_D3DLastError = 0;

    d3dResetTransformCaches();

    if (Gfx_DeferredTexStateA != 1 || Gfx_DeferredTexStateB != 1) {
        Gfx_DeferredTexStateA = 1;
        Gfx_DeferredTexStateB = 1;
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady != 0) {
            D3D8_PushBufferDirtyFlags |= 1;
            D3D8_Stage0_AddressU = 1;
            D3D8_Stage0_AddressV = 1;
        }
    }

    d3dSetupRenderStatesAndFog(0);
    d3dSetRenderState2(1); // field6057_0x1854 / D3D_DepthMaskCache - method 0x4035c
    d3dSetRenderState(1);  // field6058_0x1858 / D3D_ZFuncCache - method 0x40354

    d3dSetFogEnable(0);
    d3dSetColorConstant67(0xFFFFFFFFu);

    if (Gfx_ExtraBlendA != 1 || Gfx_ExtraBlendB != 1 || Gfx_ExtraBlendC != 1) {
        Gfx_ExtraBlendA = 1;
        Gfx_ExtraBlendB = 1;
        Gfx_ExtraBlendC = 1;
        D3D8_State0x40358_LastValue = 0x1010101;
        if (D3D_DeviceReady != 0)
            D3D_SetRenderStateSimple(0x40358, 0x1010101);
        Gfx_D3DLastError = 0;
    }

    d3dSetTextureWithBorderColor(0, 0);
    d3dSetTextureStage1(0, 0);
    d3dSetStreamSources(0, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// psiBlurCharacterShadow
// ---------------------------------------------------------------------------------------------------------------

#define Gfx_ShadowBlurTargetB U32_AT(0x002FF3B0) // Gfx.field158792_0x39c60 - written by d3dInitShadowBlurTextures (once, at boot), only read here - the second of a texture-slot pair with Gfx_AuxRenderPassResult (the first)

// Called (only ever from maybe_psiDrawShadow, not yet reimplemented) right after a shadow has been rendered
// into the aux render target via d3dBeginEndAuxRenderPass(1, ...). First closes that render pass (begin=0,
// NULL/NULL - restores the normal view/projection/viewport), then runs a two-pass box blur that ping-pongs
// between two externally-chosen texture slots (Gfx_AuxRenderPassResult/Gfx_ShadowBlurTargetB, both maintained
// by d3dInitShadowBlurTextures): pass 1 renders a half-size (128x128 from a 256x256 UV rect) downsample from
// Gfx_AuxRenderPassResult into Gfx_ShadowBlurTargetB; pass 2 renders a double-size (256x256 from a 128x128 UV
// rect) upsample from Gfx_ShadowBlurTargetB back into Gfx_AuxRenderPassResult - the combination softens the
// shadow texture in place. Finally pops the render target back to the backbuffer and unbinds stage 0.
//
// AUTOINJECT
void psiBlurCharacterShadow(void) {
    d3dBeginEndAuxRenderPass(0, NULL, NULL);
    d3dSetRenderState1(1); // field6056_0x1850 / D3D_AlphaRefCache - method 0x40340, same reuse as d3dSetup
    d3dSetColorConstant67(0xFFFFFFFFu);

    // Pass 1: downsample Gfx_AuxRenderPassResult -> Gfx_ShadowBlurTargetB.
    _d3dRenderTargetSetup((int)Gfx_ShadowBlurTargetB);
    if (Gfx_CurrentlyLoadedTexture != Gfx_AuxRenderPassResult) {
        Gfx_CurrentlyLoadedTexture = Gfx_AuxRenderPassResult;
        if (D3D_DeviceReady != 0) {
            D3DDevice_SetTexture(0, D3DTextureSlot((int)Gfx_AuxRenderPassResult)->baseTexture);
            Gfx_D3DLastError = 0;
        }
    }
    if (D3D_DeviceReady != 0)
        D3DDevice_Clear(0, NULL, 0xf3, 0, 1.0f, 0);
    Gfx_D3DLastError = 0;
    maybeImmediateModePushItem(0.0f, 0.0f, 128.0f, 128.0f, 0.0f, 0.0f, 256.0f, 256.0f, BitsToFloat(0xff808080u));
    maybeImmediateModeFlush();

    // Pass 2: upsample Gfx_ShadowBlurTargetB -> Gfx_AuxRenderPassResult.
    _d3dRenderTargetSetup((int)Gfx_AuxRenderPassResult);
    if (Gfx_CurrentlyLoadedTexture != Gfx_ShadowBlurTargetB) {
        Gfx_CurrentlyLoadedTexture = Gfx_ShadowBlurTargetB;
        if (D3D_DeviceReady != 0) {
            D3DDevice_SetTexture(0, D3DTextureSlot((int)Gfx_ShadowBlurTargetB)->baseTexture);
            Gfx_D3DLastError = 0;
        }
    }
    if (D3D_DeviceReady != 0)
        D3DDevice_Clear(0, NULL, 0xf3, 0, 1.0f, 0);
    Gfx_D3DLastError = 0;
    maybeImmediateModePushItem(0.0f, 0.0f, 256.0f, 256.0f, 0.0f, 0.0f, 128.0f, 128.0f, BitsToFloat(0xff808080u));
    maybeImmediateModeFlush();

    // Pop back to the backbuffer and unbind stage 0.
    _d3dRenderTargetSetup(0);
    if (Gfx_CurrentlyLoadedTexture != 0) {
        Gfx_CurrentlyLoadedTexture = 0;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetTexture(0, D3DTextureSlot(0)->baseTexture);
        Gfx_D3DLastError = 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// psiBlurScreen
// ---------------------------------------------------------------------------------------------------------------

#define Gfx_BlurHistoryTextureSlots ((int32_t*)0x002FF468u) // not in the Gfx struct - a 3-entry round-robin of texture slots holding recent screen captures
#define Gfx_BlurHistoryIndex U32_AT(0x002FF474u)             // current round-robin index into the above (0, 1 or 2)

// Grabs the current backbuffer's pixel data (via its own known, fixed uncached-alias address - see below) into
// a freshly (re)registered texture, replacing the oldest of 3 round-robin "history" slots, then draws it back
// as a big blurIntensity-tinted screen-covering quad - the classic double-buffered "motion blur/afterimage"
// trick. Ported verbatim from raw disassembly (not just decompile - this one genuinely warranted the extra
// care): D3DDevice_GetBackBuffer2's second returned dword, OR'd with 0x80000000, is an Xbox physical-memory
// byte offset converted to its uncached-alias VIRTUAL address (0x80000000 is the fixed base of the Xbox's
// uncached view of physical RAM - a standard, documented Xbox systems-programming pattern, not something
// invented here) - i.e. this reads real backbuffer pixels directly out of physical memory rather than doing a
// GPU-side copy, which Xbox's unified memory architecture makes valid. The subsequent 128-byte-alignment shift
// (needed before the data can be registered as a texture) always moves exactly 0x12C000 bytes (640*480*4 - a
// fixed, generously-sized constant covering the largest supported backbuffer format, not a per-call computed
// size) - confirmed by tracing the raw loop-counter register, which is set from a hardcoded constant rather
// than from the pitch value itself. Uses memmove for that shift (same established idiom as RegisterTexture's
// own near-identical alignment fixup) rather than hand-rolling the original's manual backward byte loop -
// safe for the same reason: memmove handles the forward-overlapping-shift direction correctly on its own.
//
// AUTOINJECT
void psiBlurScreen(int blurIntensity) {
    if (blurIntensity < 1)
        return;

    Gfx_BlurHistoryIndex = (Gfx_BlurHistoryIndex + 1) % 3;
    int32_t textureSlot = Gfx_BlurHistoryTextureSlots[Gfx_BlurHistoryIndex];
    if (textureSlot != 0) {
        ReleaseTexture(textureSlot);
        Gfx_BlurHistoryTextureSlots[Gfx_BlurHistoryIndex] = 0;
    }

    uint32_t *backBuffer = D3DDevice_GetBackBuffer2(-1);
    uint32_t backBufferAddr = (uint32_t)(uintptr_t)D3D_UncachedAliasOf(backBuffer[1]); // the live backbuffer pixel data - see D3D_UncachedAliasOf
    D3DResource_Release(backBuffer);

    // Find a free texture slot (address-range-bounded scan, matching the original exactly - not just a plain
    // 0..2047 loop, though D3D_TEXTURE_TABLE_COUNT ends up being the same bound either way).
    textureSlot = 1;
    bool slotFound = false;
    for (;;) {
        if (D3DTextureSlot((int)textureSlot)->baseTexture == NULL) { slotFound = true; break; }
        textureSlot++;
        if ((uintptr_t)&D3DTextureSlot((int)textureSlot)->baseTexture >= 0x002DE400u)
            break;
    }
    if (!slotFound || textureSlot >= D3D_TEXTURE_TABLE_COUNT)
        textureSlot = 0;

    if (textureSlot != 0) {
        D3DTextureSlotRaw *texSlot = D3DTextureSlot((int)textureSlot);
        texSlot->mipChainBytes = 0x0012C000u; // scratch pointer value passed through to XGSetTextureHeader/D3DResource_Register below, not a byte count here

        uintptr_t alignedAddr = ((uintptr_t)backBufferAddr + 0x7Fu) & ~(uintptr_t)0x7Fu;
        if (alignedAddr != (uintptr_t)backBufferAddr) {
            memmove((void*)alignedAddr, (void*)(uintptr_t)backBufferAddr, 0x0012C000u);
            backBufferAddr = (uint32_t)alignedAddr;
        }

        XGSetTextureHeader(640, 480, 1, 0, 0x1e /* D3DFMT_X4R4G4B4 */, 0, texSlot, 0, 0);
        D3DResource_Register(texSlot, backBufferAddr);
        // Same D3DResource_Register top-nibble-masking workaround as RegisterTexture - see its own comment.
        *(uint32_t*)((char*)texSlot + 4) = backBufferAddr;

        uint32_t registeredBytes = texSlot->mipChainBytes; // re-read - D3DResource_Register overwrites this field internally
        texSlot->baseTexture = texSlot;
        texSlot->width = 640;
        texSlot->height = 480;
        texSlot->refCount = 0;
        texSlot->nonSwizzled = 1;
        Gfx_TotalTextureBytesUsed += registeredBytes;
    }

    Gfx_BlurHistoryTextureSlots[Gfx_BlurHistoryIndex] = textureSlot;

    if (Gfx_DeferredTexStateA != 0 || Gfx_DeferredTexStateB != 0) {
        Gfx_DeferredTexStateA = 0;
        Gfx_DeferredTexStateB = 0;
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady != 0) {
            D3D8_PushBufferDirtyFlags |= 1;
            D3D8_Stage0_AddressU = 3;
            D3D8_Stage0_AddressV = 3;
        }
    }

    if (Gfx_CurrentlyLoadedTexture != (uint32_t)textureSlot) {
        Gfx_CurrentlyLoadedTexture = (uint32_t)textureSlot;
        if (D3D_DeviceReady != 0)
            D3DDevice_SetTexture(0, D3DTextureSlot((int)textureSlot)->baseTexture);
        Gfx_D3DLastError = 0;
    }

    uint32_t blurColour = ((uint32_t)blurIntensity << 24) | 0x808080u;
    maybeImmediateModePushItem(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 0.0f, 640.0f, 480.0f, BitsToFloat(blurColour));
    maybeImmediateModeFlush();

    if (Gfx_DeferredTexStateA != 1 || Gfx_DeferredTexStateB != 1) {
        Gfx_DeferredTexStateA = 1;
        Gfx_DeferredTexStateB = 1;
        Gfx_D3DLastError = 0;
        if (D3D_DeviceReady != 0) {
            D3D8_PushBufferDirtyFlags |= 1;
            D3D8_Stage0_AddressU = 1;
            D3D8_Stage0_AddressV = 1;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// d3dReleaseLevelResources (NOT an original function - our own addition, called from maybeD3dShutdown)
// ---------------------------------------------------------------------------------------------------------------

// Frees every level-scoped slot in the three 2048-entry D3D resource tables - textures whose refCount is 0
// (i.e. everything except the handful d3dMarkTexturePermanent has pinned: the two fonts, the overlay fallback
// texture and the two shadow/aux render targets), and ALL vertex and index buffers - plus psiBlurScreen's
// three cached history-slot indices, which would otherwise be handed back to ReleaseTexture later and free a
// slot the next level had since reused.
//
// WHY THIS EXISTS: the original binary has no release path for these tables at all. Exhaustively checked -
// every function referencing any of the three table base addresses (by Ghidra xref, by immediate-operand
// instruction search AND by raw byte-pattern search of the whole image), every caller of ReleaseTexture,
// d3dCreateVertexBuffers and d3dCreateIndexBuffer, plus the whole level-change path (ResetMap_Load ->
// maybePsiResetResources -> maybeCleanupSystem -> maybeD3dShutdown -> maybeResetRenderState). The only thing
// that ever zeroes these tables is xboxInitGraphics' whole-Gfx memset at boot. maybePsiResetResources
// resets every OTHER level-scoped mirror of them (the 2048-entry Tex[] and d3dGeometryObjs[] arrays,
// NumXboxTexLoaded, NumXboxEntityGfxsCreated, ...) but not the D3D slot tables themselves.
//
// Meanwhile every level load registers ~500 textures (psiCreateMapTextures, one slot per sub-texture/animation
// frame, no dedup across loads) and one vertex + one index buffer per entity model (psiCreateEntityGfx - the
// ONLY caller of both allocators, so nothing in these two tables is anything but level data). Against 2047
// slots that is exhaustion on the 4th or 5th load - exactly the observed "grey/missing textures after a few
// reloads, then a crash downstream of a 0 handle" - and it reproduces identically with the original,
// unpatched allocators (the registration sequence is byte-for-byte the same with and without this seam),
// because the leak is in the game's own bookkeeping, not in the seam.
//
// Safe to do here because: maybeD3dShutdown has just unbound stages 0/1 and the stream/index buffers; the
// heap holding all the freed resources' pixel/vertex data is about to be wiped by Mem_Init anyway; the
// loading screen that stays up through the following level load draws only with DAT_002adf58 (permanent -
// see ShowLoadProgressScreen), and the background-movie textures were already released by
// maybeBackgroundMovieCleanup one call earlier. CXBX keys its host texture cache on (type, data address,
// format, size) and periodically re-hashes contents, and it converts vertex/index buffers from Xbox memory
// with content hashing at draw time - so reusing slot addresses can't hand back stale host resources.
//
// Per-slot bookkeeping mirrors ReleaseTexture exactly (BlockUntilNotBusy, running byte totals, pointer/size
// zeroed) for textures; vertex/index slots are returned to their post-boot all-zero state, since the free-slot
// test both allocators use is a NULL selfPtr/header.
static void d3dReleaseLevelResources(void) {
    int texturesFreed = 0;
    for (int slot = 1; slot < D3D_TEXTURE_TABLE_COUNT; slot++) {
        D3DTextureSlotRaw *texSlot = D3DTextureSlot(slot);
        if (texSlot->baseTexture == NULL || texSlot->refCount != 0)
            continue;
        D3DResource_BlockUntilNotBusy(texSlot->baseTexture);
        Gfx_TotalTextureBytesUsed -= texSlot->mipChainBytes;
        texSlot->baseTexture = NULL;
        texSlot->mipChainBytes = 0;
        texturesFreed++;
    }
    for (int i = 0; i < 3; i++)
        Gfx_BlurHistoryTextureSlots[i] = 0;

    int vertexBuffersFreed = 0;
    for (int slot = 1; slot < D3D_VERTEX_BUFFER_TABLE_COUNT; slot++) {
        D3DVertexBufferSlotRaw *slotPtr = D3DVertexBufferSlot(slot);
        if (slotPtr->selfPtr == NULL)
            continue;
        Gfx_VertexBufferBytesUsed -= slotPtr->byteSize;
        memset(slotPtr, 0, sizeof(*slotPtr));
        vertexBuffersFreed++;
    }

    int indexBuffersFreed = 0;
    for (int slot = 1; slot < D3D_INDEX_BUFFER_TABLE_COUNT; slot++) {
        D3DIndexBufferSlotRaw *slotPtr = D3DIndexBufferSlot(slot);
        if (slotPtr->header == 0)
            continue;
        Gfx_IndexBufferBytesUsed -= slotPtr->byteSize;
        memset(slotPtr, 0, sizeof(*slotPtr));
        indexBuffersFreed++;
    }

    printf("[d3dSeam] level reset: released %d texture, %d vertex buffer and %d index buffer slots.\n",
           texturesFreed, vertexBuffersFreed, indexBuffersFreed);
}

// ---------------------------------------------------------------------------------------------------------------
// d3dInitShadowBlurTextures
// ---------------------------------------------------------------------------------------------------------------

// One-time (called only from xboxInitGraphics, alongside d3dSetup/d3dInitFallbackOverlayTexture) setup of the two
// permanent, never-released (refCount forced to 1 below) render-target textures psiBlurCharacterShadow and
// d3dBeginEndAuxRenderPass consume as Gfx_AuxRenderPassResult (256x256) and Gfx_ShadowBlurTargetB (128x128).
//
// AUTOINJECT
void d3dInitShadowBlurTextures(void) {
    void *data = allocateAligned0x1000(0x40000);
    Gfx_AuxRenderPassResult = (uint32_t)RegisterTexture(0x100, 0x100, 2, 1, data, 0);
    d3dMarkTexturePermanent((int)Gfx_AuxRenderPassResult);

    data = allocateAligned0x1000(0x10000);
    Gfx_ShadowBlurTargetB = (uint32_t)RegisterTexture(0x80, 0x80, 2, 1, data, 0);
    d3dMarkTexturePermanent((int)Gfx_ShadowBlurTargetB);
}

// ---------------------------------------------------------------------------------------------------------------
// d3dSetLevelDirectionVector
// ---------------------------------------------------------------------------------------------------------------

// Sets Gfx_LevelDirectionVector plus the same "matrix needs regen" flag d3dSetMatrix/d3dSetViewMatrixFromRigid
// Transform use (Gfx_MatrixGenFlag2, though those two always set it to -1, not 1 - a different value for a
// different purpose reusing the same flag). Only ever called from maybePsiResetResources (not yet
// reimplemented) with one of 4 hardcoded axis vectors selected by GameState.NextLevelHashcode - looks like a
// per-mission override of some directional value (lighting? wind/particle direction?), but no reader of
// Gfx_LevelDirectionVector was found anywhere in the binary to confirm which.
//
// AUTOINJECT
void d3dSetLevelDirectionVector(float x, float y, float z) {
    Gfx_MatrixGenFlag2 = 1;
    Gfx_LevelDirectionVector[0] = x;
    Gfx_LevelDirectionVector[1] = y;
    Gfx_LevelDirectionVector[2] = z;
}

// ---------------------------------------------------------------------------------------------------------------
// Slot-table accessors: Texture_GetRawDataPtr, d3dGetVertexDataSize, d3dGetIndexDataSize, d3dGetStreamBuffer,
// d3dGetIndexBufferData
// ---------------------------------------------------------------------------------------------------------------

// Returns the pixel-data pointer of a registered texture (its D3D header's Data field, +4) with 0x80000000
// OR'd in - the Xbox's uncached-alias view of physical RAM (same trick psiBlurScreen relies on, see its
// comment). Only caller is psiDecompressWoman, which writes decompressed frames straight into the texture.
// NULL for an empty slot.
//
// AUTOINJECT
void * Texture_GetRawDataPtr(int textureSlot) {
    D3DTextureSlotRaw *texSlot = D3DTextureSlot(textureSlot);
    if (texSlot->baseTexture == NULL)
        return NULL;
    uint32_t data = *(uint32_t*)((char*)texSlot->baseTexture + 4);
    if (g_gfxBackend == GFX_BACKEND_D3D9)
        D3D9_NotifyTextureModified(texSlot->baseTexture); // the caller is about to write pixels through this pointer
    return D3D_UncachedAliasOf(data);
}

// Byte size the level loader reserves for a vertex-buffer's data: stride * count, plus 3 bytes of slack for
// d3dCreateVertexBuffers' 4-byte alignment shift. Stride is 0x1c (0x20 if skinned) for a single buffer and 6
// for a multi-stream (streamCount > 0) set, matching the allocator's own two formulas exactly.
//
// AUTOINJECT
int d3dGetVertexDataSize(int vtxCnt, char maybeSkinned, int streamCount) {
    int stride = (maybeSkinned != 0) ? 0x20 : 0x1c;
    if (streamCount > 0)
        stride = 6;
    return stride * vtxCnt + 3;
}

// Ditto for an index buffer: 16-bit indices plus the same 3 bytes of alignment slack.
//
// AUTOINJECT
int d3dGetIndexDataSize(int indexCount) {
    return indexCount * 2 + 3;
}

// Reads one vertex back out of a (single-buffer path) vertex-buffer slot: position (3 dwords at +0), the
// dword at +0x10 and the UV pair (+0x14, +0x18), using the slot's own stride flag (0x20 skinned / 0x1c not)
// and the aligned data pointer d3dCreateVertexBuffers stored at +0x20. Only caller is psiGetTriList (glass
// shatter geometry). Outputs are copied as raw dwords, exactly as the original does.
//
// AUTOINJECT
void d3dGetStreamBuffer(int streamSlot, int vtxNum, uint32_t *posOut, uint32_t *uvOut, uint32_t *dword10Out) {
    D3DVertexBufferSlotRaw *slotPtr = D3DVertexBufferSlot(streamSlot);
    uint32_t stride = (slotPtr->field18 != 0) ? 0x20u : 0x1Cu;
    const uint32_t *vertex = (const uint32_t*)((const char*)slotPtr->alignedData + (uint32_t)vtxNum * stride);
    posOut[0] = vertex[0];
    posOut[1] = vertex[1];
    posOut[2] = vertex[2];
    uvOut[0] = vertex[5];
    uvOut[1] = vertex[6];
    *dword10Out = vertex[4];
}

// Returns an index-buffer slot's (aligned) index data pointer (+0x18). Only caller is psiGetTriList.
//
// AUTOINJECT
void * d3dGetIndexBufferData(int indexSlot) {
    return D3DIndexBufferSlot(indexSlot)->dataPtr2;
}

// ---------------------------------------------------------------------------------------------------------------
// d3dSetLight, d3dDisableLight
// ---------------------------------------------------------------------------------------------------------------

// Four directional-light slots, kept as cached shader-constant inputs. These are the same fields
// d3dResetTransformCaches (which is really "disable all four lights", going by this) resets:
// Gfx_TransformCacheBitmaskA/B are the enabled/dirty masks and Gfx_TransformCacheFloats the colours. Only
// caller of both is psiLight_SetLights (not yet reimplemented). Whoever uploads these to the GPU hasn't been
// traced - it isn't any function in this file.
#define Gfx_LightEnabledMask Gfx_TransformCacheBitmaskA               // 0x002FF274 - bit i = light i enabled
#define Gfx_LightDirtyMask   Gfx_TransformCacheBitmaskB               // 0x002FF270 - bit i = light i changed
#define Gfx_LightColour(i)   (Gfx_TransformCacheFloats - 1 + (i) * 4) // 0x002FF2BC + 16*i: r, g, b (each /256)
#define Gfx_LightInvRange    ((float*)0x002FF2FCu)                    // 4 floats: 1/range (1.0 if range <= 0)
#define Gfx_LightDirection   ((uint32_t*)0x002FF30Cu)                 // 3 dwords per light, copied verbatim

// AUTOINJECT
void d3dDisableLight(int lightIndex) {
    if (lightIndex >= 4)
        return;
    uint32_t bit = 1u << lightIndex;
    Gfx_LightEnabledMask &= ~bit;
    Gfx_LightDirtyMask |= bit;
    float *colour = Gfx_LightColour(lightIndex);
    colour[2] = 0.0f;
    colour[1] = 0.0f;
    colour[0] = 0.0f;
    if (Gfx_LightEnabledMask == 0)
        Gfx_MiscModeFlags &= ~0xCu;
}

// dirX/Y/Z are copied as raw dwords (floats, going by the consumers, but the original never interprets
// them here). Sets Gfx_MiscModeFlags bit 0x4 for lights 0-1 and bit 0x8 for lights 2-3.
//
// AUTOINJECT
void d3dSetLight(int lightIndex, uint32_t dirX, uint32_t dirY, uint32_t dirZ, float range, float r, float g, float b) {
    if (lightIndex >= 4)
        return;
    uint32_t bit = 1u << lightIndex;
    Gfx_LightEnabledMask |= bit;
    Gfx_LightDirtyMask |= bit;

    uint32_t *dir = Gfx_LightDirection + lightIndex * 3;
    dir[0] = dirX;
    dir[1] = dirY;
    dir[2] = dirZ;

    // FCOMP against 0.0 then TEST AH,0x41: the divide only happens when range is strictly greater than 0
    // (a NaN range keeps 1.0 too).
    float invRange = 1.0f;
    if (range > 0.0f)
        invRange = 1.0f / range;
    Gfx_LightInvRange[lightIndex] = invRange;

    float *colour = Gfx_LightColour(lightIndex);
    colour[0] = r * 0.00390625f; // 1/256
    colour[1] = g * 0.00390625f;
    colour[2] = b * 0.00390625f;

    Gfx_MiscModeFlags |= (lightIndex >= 2) ? 0x8u : 0x4u;
}

// ---------------------------------------------------------------------------------------------------------------
// d3dInitFallbackOverlayTexture
// ---------------------------------------------------------------------------------------------------------------

// The original's FUN_000e3a20 (now d3dComputeSwizzleMasks in Ghidra) - a __thiscall helper (ECX = the 9-dword
// output block) with no other callers, so it's a plain static function here rather than an injection: builds
// the per-axis bit masks for Xbox texture swizzling (Morton order) of a w x h x d texture - masks[3..5] are
// the X/Y/Z masks, masks[6..8] the (zero) starting offsets. The classic "increment a swizzled coordinate"
// idiom the caller uses is then off = (off - mask) & mask.
static void d3dComputeSwizzleMasks(uint32_t *masks, uint32_t width, uint32_t height, uint32_t depth) {
    masks[0] = width;
    masks[1] = height;
    masks[2] = depth;
    for (int i = 3; i < 9; i++)
        masks[i] = 0;

    uint32_t bit = 1, size = 1, lastBit;
    do {
        lastBit = 0;
        if (size < width)  { masks[3] |= bit; bit <<= 1; lastBit = bit; }
        if (size < height) { masks[4] |= bit; bit <<= 1; lastBit = bit; }
        if (size < depth)  { masks[5] |= bit; bit <<= 1; lastBit = bit; }
        size <<= 1;
    } while (lastBit != 0);
}

// One-time (xboxInitGraphics only) creation of the 8x8 swizzled A4R4G4B4 "soft dot" texture d3dDrawOverlayQuad
// falls back to when given texture slot 0 (Gfx_FallbackOverlayTexture): white, with alpha = (1 - dist/sqrt(32))^3
// from the centre, clamped to [0,1] - i.e. a round radial fade. Marked permanent so level resets never free it.
// Ported from raw disassembly: the decompiler shows the pow() arguments the wrong way round (it's pow(v, 3.0),
// not pow(3.0, v) - the exponent is pushed last), the constant is 1/sqrt(32.0), the alpha byte is a plain
// __ftol truncation, and the clamp's parity-flag test treats a NaN like a negative (-> 0).
//
// AUTOINJECT
void d3dInitFallbackOverlayTexture(void) {
    uint16_t *pixels = (uint16_t*)allocateAligned0x1000(0x80);

    uint32_t masks[9];
    d3dComputeSwizzleMasks(masks, 8, 8, 1);
    const float invSqrt32 = 1.0f / (float)sqrt(32.0);

    uint32_t yOffset = 0;
    for (int y = 0; y < 8; y++) {
        float dy = (float)y - 4.0f;
        float dy2 = dy * dy;
        uint32_t xOffset = 0;
        for (int x = 0; x < 8; x++) {
            float dx = (float)x - 4.0f;
            float v = 1.0f - (float)sqrt(dx * dx + dy2) * invSqrt32;
            if (!(v >= 0.0f))
                v = 0.0f;
            else if (v > 1.0f)
                v = 1.0f;
            uint8_t alpha = (uint8_t)(int)(pow((double)v, 3.0) * 255.0f);
            pixels[yOffset | xOffset] = (uint16_t)(((uint16_t)alpha << 8) | 0x0FFF);
            xOffset = (xOffset - masks[3]) & masks[3];
        }
        yOffset = (yOffset - masks[4]) & masks[4];
    }

    Gfx_FallbackOverlayTexture = (uint32_t)RegisterTexture(8, 8, 1, 1, pixels, 0);
    d3dMarkTexturePermanent((int)Gfx_FallbackOverlayTexture);
}

// ---------------------------------------------------------------------------------------------------------------
// xboxInitGraphics
// ---------------------------------------------------------------------------------------------------------------

// The last game-side function that talked to D3D8 directly. Called once from main(). All four D3D8 entry
// points confirmed plain __stdcall via raw disassembly (RET 0x18 / 0x8 / 0x10 / 0x4; the "push buffers supported"
// query ignores its one argument and just returns 1).
#define Direct3D_CreateDevice_ADDR         0x001004a0u
#define D3D_SetPushBufferSize_ADDR         0x00100480u
#define D3DDevice_CreateVertexShader_ADDR  0x00102430u
#define D3D_ArePushBuffersSupported_ADDR   0x00105180u // Ghidra: FUN_00105180, a D3D8-internal "return 1"

typedef uint32_t(__stdcall *Direct3D_CreateDeviceFn)(uint32_t adapter, uint32_t deviceType, void *hFocusWindow,
                                                     uint32_t behaviorFlags, void *pPresentationParameters, void **ppDevice);
#define Direct3D_CreateDevice (D3DSeamTraced("Direct3D_CreateDevice", (Direct3D_CreateDeviceFn)Direct3D_CreateDevice_ADDR, D3D9_CreateDevice))

typedef void(__stdcall *D3D_SetPushBufferSizeFn)(uint32_t pushBufferSize, uint32_t kickOffSize);
#define D3D_SetPushBufferSize (D3DSeamTraced("D3D_SetPushBufferSize", (D3D_SetPushBufferSizeFn)D3D_SetPushBufferSize_ADDR, D3D9_SetPushBufferSize))

typedef uint32_t(__stdcall *D3DDevice_CreateVertexShaderFn)(const void *pDeclaration, const void *pFunction, void **pHandle, uint32_t usage);
#define D3DDevice_CreateVertexShader (D3DSeamTraced("D3DDevice_CreateVertexShader", (D3DDevice_CreateVertexShaderFn)D3DDevice_CreateVertexShader_ADDR, D3D9_CreateVertexShader))

// RET 0x4 - takes one (ignored) dword argument, which the original passes as 0. Declaring it with the argument
// matters: as __stdcall the callee pops those 4 bytes, so calling it with none would unbalance the stack.
typedef uint32_t(__stdcall *D3D_ArePushBuffersSupportedFn)(uint32_t unused);
#define D3D_ArePushBuffersSupported (D3DSeamTraced("D3D_ArePushBuffersSupported", (D3D_ArePushBuffersSupportedFn)D3D_ArePushBuffersSupported_ADDR, D3D9_ArePushBuffersSupported))

// Standard Xbox D3DPRESENT_PARAMETERS (17 dwords), zeroed and then only partially filled in by the original.
struct D3DPRESENT_PARAMETERS_Xbox {
    uint32_t BackBufferWidth, BackBufferHeight, BackBufferFormat, BackBufferCount;
    uint32_t MultiSampleType, SwapEffect, hDeviceWindow, Windowed;
    uint32_t EnableAutoDepthStencil, AutoDepthStencilFormat, Flags;
    uint32_t FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
    uint32_t BufferSurfaces[3], DepthStencilSurface;
};
static_assert(sizeof(D3DPRESENT_PARAMETERS_Xbox) == 17 * 4, "Bad size for D3DPRESENT_PARAMETERS_Xbox");

#define GFX_STRUCT_BASE   0x002C5750u
#define GFX_STRUCT_DWORDS 59193u // the original's REP STOSD count: 0x39CE4 bytes, up to (not including) 0x002FF434

#define Gfx_PushBuffersEnabled U32_AT(0x002C5768) // Gfx.pushBuffersEnabled
#define Gfx_IsPalI             U8_AT(0x002C5760)  // Gfx.IsPalI - misnamed: it's a copy of IsNotPalI (see below)
#define Gfx_IsNotPalI          U8_AT(0x002C5761)  // Gfx.IsNotPalI - AV region != 3 (PAL-I)
#define Gfx_IsSomeGfxRegion    U8_AT(0x002C5762)  // Gfx.IsSomeGfxRegion - AV region == 1 (NTSC-M)
#define Gfx_IsWidescreen       U8_AT(0x002C5763)  // Gfx.IsWidescreen - video mode bit 0
#define Gfx_VideoModeBit3      U8_AT(0x002C5764)  // Gfx.field14_0x14 - video mode bit 3, forced to 0 for PAL-I

// Vertex shader source data (ROM, in the XBE's data section) - the 128 "main" shaders share one of four
// declarations (selected by the shader index's low two bits) and each have their own function token array
// (a 128-entry pointer table), plus the immediate-mode and overlay-quad shaders with dedicated handles.
#define VtxShaderDeclTokens_Plain    ((const void*)0x001B50E4u)
#define VtxShaderDeclTokens_Bit0     ((const void*)0x001B5100u)
#define VtxShaderDeclTokens_Bit1     ((const void*)0x001B5158u)
#define VtxShaderDeclTokens_Bit0And1 ((const void*)0x001B5178u)
#define VtxShaderFunctionTokenTable  ((const void* const*)0x001B4D78u) // 128 pointers
#define ImmediateModeVtxShaderDecl   ((const void*)0x001B51D4u)
#define ImmediateModeVtxShaderFunc   ((const void*)0x001B4F78u)
#define OverlayVtxShaderDecl         ((const void*)0x001B51E8u)
#define OverlayVtxShaderFunc         ((const void*)0x001B4FD0u)
#define ImmediateModeVtxShaderHandleAddr ((void**)0x002C574Cu) // = &Gfx_ImmediateModeVertexShader
#define OverlayVtxShaderHandleAddr       ((void**)0x002C5748u) // = &Gfx_OverlayVertexShaderHandle
#define Gfx_D3DDeviceAddr                ((void**)0x002C576Cu) // = &D3D_DeviceReady (Gfx.D3DDevice)

// What xboxInitGraphics asked the device for - read back by d3dGetDisplayMode below.
static uint32_t g_d3dDisplayRefreshRate = 60;
static uint32_t g_d3dDisplayFlags = 0;
static uint32_t g_d3dDisplayFormat = 7;

// Boot-time D3D setup: zeroes the whole Gfx struct (the only thing that ever did before
// d3dReleaseLevelResources existed), builds the 0..255 -> 0..1 float table, creates the device, the fallback
// overlay texture, the baseline render state (d3dSetup), the shadow/aux render targets, all 130 vertex
// shaders, a neutral gamma ramp, then clears and presents two frames so every buffer starts black.
//
// The backbuffer size used to be patched into this function's immediates by Inject() (0x000e6efc/0x000e6f04);
// it now takes SCREEN_WIDTH/SCREEN_HEIGHT directly. Region/video-mode inputs come from GetVideoMode/
// XboxGetAVRegion, both already reimplemented on top of settings.ini in XboxSettings.cpp. The refresh rate
// picked for the device is 60Hz unless the AV region is PAL-I (3), in which case 50Hz - through the
// confusingly-named Gfx.IsPalI, which is actually set to IsNotPalI (ported as-is).
//
// AUTOINJECT
void xboxInitGraphics(void) {
    g_gfxBackend = Settings_GetGraphicsBackend(); // decided once, before the first D3D8 entry point is touched
    printf("[d3dSeam] graphics backend: %s\n", g_gfxBackend == GFX_BACKEND_D3D9 ? "d3d9 (native)" : "cxbx (D3D8 HLE)");

    memset((void*)GFX_STRUCT_BASE, 0, GFX_STRUCT_DWORDS * 4);

    for (int i = 0; i < 256; i++)
        Gfx_U8ToFloat01(i) = (float)i * (1.0f / 255.0f);

    Gfx_PushBuffersEnabled = D3D_ArePushBuffersSupported(0);
    if (Gfx_PushBuffersEnabled != 0)
        D3D_SetPushBufferSize(0x200000, 0x80000);

    D3DPRESENT_PARAMETERS_Xbox presentParams;
    memset(&presentParams, 0, sizeof(presentParams));
    Gfx_D3DLastError = 0;
    presentParams.BackBufferCount = 1;
    presentParams.BackBufferWidth = SCREEN_WIDTH;
    presentParams.BackBufferHeight = SCREEN_HEIGHT;
    presentParams.BackBufferFormat = 7;           // X_D3DFMT_X8R8G8B8
    presentParams.SwapEffect = 1;                 // D3DSWAPEFFECT_DISCARD
    presentParams.EnableAutoDepthStencil = 1;
    presentParams.AutoDepthStencilFormat = 0x2A;  // X_D3DFMT_LIN_D24S8
    presentParams.FullScreen_RefreshRateInHz = 0;
    presentParams.FullScreen_PresentationInterval = 0;

    uint32_t videoMode = GetVideoMode();
    Gfx_IsNotPalI = (XboxGetAVRegion() != 3) ? 1 : 0;
    Gfx_IsSomeGfxRegion = (XboxGetAVRegion() == 1) ? 1 : 0;
    Gfx_IsWidescreen = (uint8_t)(videoMode & 1);
    Gfx_VideoModeBit3 = (uint8_t)((videoMode >> 3) & 1);
    Gfx_IsPalI = Gfx_IsNotPalI; // sic - see the comment above
    if (Gfx_IsPalI == 0)
        Gfx_VideoModeBit3 = 0;
    presentParams.FullScreen_RefreshRateInHz = (Gfx_IsPalI != 0) ? 60 : 50;
    if (Gfx_IsWidescreen != 0)
        presentParams.Flags |= 0x10;
    if (Gfx_VideoModeBit3 != 0)
        presentParams.Flags |= 0x40;

    // Remembered for d3dGetDisplayMode (the D3D8 GetDisplayMode replacement the video decoder ends up calling).
    g_d3dDisplayRefreshRate = presentParams.FullScreen_RefreshRateInHz;
    g_d3dDisplayFlags = presentParams.Flags;
    g_d3dDisplayFormat = presentParams.BackBufferFormat;

    if (Gfx_PushBuffersEnabled == 0)
        Gfx_D3DLastError = 0;
    else
        Gfx_D3DLastError = Direct3D_CreateDevice(0, 1, NULL, 0x40, &presentParams, Gfx_D3DDeviceAddr);

    d3dInitFallbackOverlayTexture();
    d3dSetup();
    d3dInitShadowBlurTextures();

    for (int i = 0; i < 128; i++) {
        const void *declaration;
        if ((i & 2) == 0)
            declaration = (i & 1) ? VtxShaderDeclTokens_Bit0 : VtxShaderDeclTokens_Plain;
        else
            declaration = (i & 1) ? VtxShaderDeclTokens_Bit0And1 : VtxShaderDeclTokens_Bit1;

        if (D3D_DeviceReady == 0)
            Gfx_D3DLastError = 0;
        else
            Gfx_D3DLastError = D3DDevice_CreateVertexShader(declaration, VtxShaderFunctionTokenTable[i], &VtxShaderHandles[i], 0);
    }

    if (D3D_DeviceReady == 0) {
        Gfx_D3DLastError = 0;
    } else {
        Gfx_D3DLastError = D3DDevice_CreateVertexShader(ImmediateModeVtxShaderDecl, ImmediateModeVtxShaderFunc, ImmediateModeVtxShaderHandleAddr, 0);
        if (D3D_DeviceReady == 0)
            Gfx_D3DLastError = 0;
        else
            Gfx_D3DLastError = D3DDevice_CreateVertexShader(OverlayVtxShaderDecl, OverlayVtxShaderFunc, OverlayVtxShaderHandleAddr, 0);
    }

    ConfigureGammaRamp(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < 2; i++) {
        d3dBeginFrame();
        if (D3D_DeviceReady != 0)
            D3DDevice_Clear(0, NULL, 0xF3, 0, 1.0f, 0);
        Gfx_D3DLastError = 0;
        d3dSwap(); // the original inlines exactly d3dSwap's body here
    }
}

// ---------------------------------------------------------------------------------------------------------------
// D3D8 entry points replaced at their OWN addresses: d3dGetDisplayMode, d3dGetRasterStatus, d3dGetSurfaceDesc,
// d3dLockSurface
// ---------------------------------------------------------------------------------------------------------------

// Everything above sits on top of D3D8 in Eurocom's own wrapper layer. These four are different: their callers
// are inside the XMV video decoder library (Microsoft code linked into the XBE - maybeXmvDecoderCreate calls
// GetDisplayMode, maybeXmvDecoderUpdate calls GetRasterStatus, maybeXmvDecoderWriteFrame calls Surface_GetDesc
// + Surface_LockRect), which isn't ours to reimplement. So the D3D8 library entry points themselves are hooked
// (FUNC_AT, like XLaunchNewImageA) and answered from the seam's own knowledge: the display mode xboxInitGraphics
// asked for, a wall-clock-derived raster position, and the texture headers RegisterTexture built. None of them
// consult D3D8 or its device state any more, so with these the seam is the only thing in the process that
// talks to D3D8.

// (The D3DDISPLAYMODE_Xbox / D3DRASTER_STATUS_Xbox / D3DSURFACE_DESC_Xbox / D3DLOCKED_RECT_Xbox / RECT_Xbox
// structs and the four prototypes live in d3dSeam.h, so the autogenerated injection table can see them.)

// Xbox X_D3DFMT_* bits per pixel for every format RegisterTexture can produce, plus the common depth formats.
static uint32_t XboxFormatBitsPerPixel(uint32_t format) {
    switch (format) {
        case 0x00: case 0x01: case 0x0b: case 0x13: case 0x19: case 0x1b: case 0x1f: return 8;   // L8, AL8, P8, LIN_L8, A8, LIN_AL8, LIN_A8
        case 0x0c: return 4;                                                                   // DXT1
        case 0x0e: case 0x0f: return 8;                                                        // DXT3, DXT5
        case 0x02: case 0x03: case 0x04: case 0x05: case 0x10: case 0x11: case 0x1c: case 0x1d:
        case 0x1a: case 0x20: case 0x24: case 0x25: case 0x28: case 0x29: case 0x2c: case 0x2d: case 0x30: return 16;
        case 0x06: case 0x07: case 0x12: case 0x1e: case 0x2a: case 0x2b: case 0x2e: case 0x2f: return 32;
        default:
            printf("[d3dSeam] XboxFormatBitsPerPixel: unknown Xbox format 0x%x, assuming 32 bpp.\n", format);
            return 32;
    }
}

// Level-0 geometry of an Xbox pixel container, decoded from the header XGSetTextureHeader built (Format dword
// at +0xc: format byte at bits 8-15, log2 width/height at bits 20-23/24-27; Size dword at +0x10: nonzero only
// for linear formats, packing (width-1) | (height-1) << 12 | (pitch/64 - 1) << 24). Mirrors D3D8's own
// FUN_00107610/FUN_00107820, including DXT's 4x4-block pitch and minimum-size rules.
static void XboxSurfaceLevel0(const uint32_t *header, uint32_t *widthOut, uint32_t *heightOut, uint32_t *pitchOut, uint32_t *bytesOut) {
    uint32_t formatWord = header[3];
    uint32_t sizeWord = header[4];
    uint32_t format = (formatWord >> 8) & 0xFF;
    uint32_t bpp = XboxFormatBitsPerPixel(format);
    bool dxt = (format == 0x0c || format == 0x0e || format == 0x0f);
    uint32_t width, height, pitch, rows;
    if (sizeWord != 0) {
        width = (sizeWord & 0xFFF) + 1;
        height = ((sizeWord >> 12) & 0xFFF) + 1;
        pitch = ((sizeWord >> 24) + 1) * 64;
        rows = height;
    } else {
        uint32_t logW = (formatWord >> 20) & 0xF;
        uint32_t logH = (formatWord >> 24) & 0xF;
        width = 1u << logW;
        height = 1u << logH;
        if (dxt) { if (logW < 2) logW = 2; if (logH < 2) logH = 2; }
        pitch = (format == 0x0c) ? ((1u << logW) * 2) : dxt ? ((1u << logW) * 4) : ((1u << logW) * bpp / 8);
        rows = dxt ? ((1u << logH) / 4) : height;
    }
    *widthOut = width;
    *heightOut = height;
    *pitchOut = pitch;
    *bytesOut = pitch * rows;
}

// FUNC_AT(00103b80)
void __stdcall d3dGetDisplayMode(D3DDISPLAYMODE_Xbox *mode) {
    D3DSeamTraceCall("d3dGetDisplayMode(hooked D3DDevice_GetDisplayMode)", (void*)mode);
    mode->Width = SCREEN_WIDTH;
    mode->Height = SCREEN_HEIGHT;
    mode->RefreshRate = g_d3dDisplayRefreshRate;
    mode->Flags = g_d3dDisplayFlags;
    mode->Format = g_d3dDisplayFormat;
}

// The original reads the CRTC's current scanline register and reports {0, line} while the beam is inside the
// visible area or {1, 0} during vertical blanking. The decoder uses it to estimate the time to the next vblank
// for frame pacing. There is no beam here, so synthesise one from the wall clock at the display refresh rate,
// with ~8% of each frame period spent in blanking - close enough for pacing, and no D3D8 involvement.
// FUNC_AT(00103ca0)
void __stdcall d3dGetRasterStatus(D3DRASTER_STATUS_Xbox *status) {
    D3DSeamTraceCall("d3dGetRasterStatus(hooked D3DDevice_GetRasterStatus)", (void*)status);
    double framePeriod = 1.0 / (double)(g_d3dDisplayRefreshRate ? g_d3dDisplayRefreshRate : 60);
    double phase = fmod(timestamp(), framePeriod) / framePeriod; // 0..1 through one frame
    uint32_t totalLines = SCREEN_HEIGHT + SCREEN_HEIGHT / 12;
    uint32_t line = (uint32_t)(phase * (double)totalLines);
    if (line < SCREEN_HEIGHT) {
        status->InVBlank = 0;
        status->ScanLine = line;
    } else {
        status->InVBlank = 1;
        status->ScanLine = 0;
    }
}

// FUNC_AT(0010bb00)
void __stdcall d3dGetSurfaceDesc(const uint32_t *surface, D3DSURFACE_DESC_Xbox *desc) {
    D3DSeamTraceCall("d3dGetSurfaceDesc(hooked D3DSurface_GetDesc)", (void*)surface);
    uint32_t width, height, pitch, bytes;
    XboxSurfaceLevel0(surface, &width, &height, &pitch, &bytes);
    uint32_t format = (surface[3] >> 8) & 0xFF;

    // D3DResource_GetType's mapping of the Common type bits (with the cube/volume sub-type flags in the
    // Format word), and the format table's render-target/depth usage bits - none of our formats set either.
    uint32_t type;
    switch (surface[0] & 0x70000) {
        case 0x00000: type = 6; break;                                              // vertex buffer
        case 0x10000: type = 7; break;                                              // index buffer
        case 0x20000: type = 8; break;                                              // push buffer
        case 0x30000: type = 9; break;                                              // palette
        case 0x40000: type = (surface[3] & 4) ? 5 : ((surface[3] & 0xF0) > 0x20 ? 4 : 3); break; // cube / volume / texture
        case 0x50000: type = ((surface[3] & 0xF0) > 0x20) ? 2 : 1; break;          // volume / surface
        default:      type = 10; break;                                             // fixup
    }
    uint32_t usage = 0;
    if (format == 0x2a || format == 0x2b || format == 0x2c || format == 0x2d || format == 0x2e || format == 0x2f || format == 0x30)
        usage = 2; // depth-stencil formats

    desc->Format = format;
    desc->Type = type;
    desc->Usage = usage;
    desc->Size = bytes;
    desc->MultiSampleType = 0x11; // D3DMULTISAMPLE_NONE - the original only reports otherwise for the backbuffer itself
    desc->Width = width;
    desc->Height = height;
}

// Level 0 of a pixel container, optionally offset to a rect. The original also spins on the GPU
// (D3D_BlockOnResource, unless flags & 0x20) - nothing to wait for here - and hands the pointer back through
// the 0xF0000000 write-combined alias when flags & 0x40 (how the decoder asks for it) rather than the plain
// 0x80000000 one; CXBX maps both to the same memory, so the ordinary uncached alias is used for both.
//
// The alias is NOT optional: a resource's Data word is a physical address with the top nibble stripped (the
// surface objects D3DTexture_GetSurfaceLevel2 creates store "data & 0x0fffffff", and D3DResource_Register does
// the same to texture headers), and the CPU-visible address is only recovered by OR-ing 0x80000000 back in,
// which is exactly what D3D8's own Lock2DSurface does. Returning the raw word here (the first attempt) sent
// the decoder's frames to the wrong address and left every FMV a uniform pink - the YUY2 decode of the heap's
// 0x98 fill pattern.
// FUNC_AT(0010bb20)
void __stdcall d3dLockSurface(const uint32_t *surface, D3DLOCKED_RECT_Xbox *locked, const RECT_Xbox *rect, uint32_t flags) {
    D3DSeamTraceCall("d3dLockSurface(hooked D3DSurface_LockRect)", (void*)surface, flags);
    uint32_t width, height, pitch, bytes;
    XboxSurfaceLevel0(surface, &width, &height, &pitch, &bytes);
    uint8_t *bits = (uint8_t*)D3D_UncachedAliasOf(surface[1]);
    if (rect != NULL) {
        uint32_t format = (surface[3] >> 8) & 0xFF;
        bits += (uint32_t)rect->top * pitch + ((uint32_t)rect->left * XboxFormatBitsPerPixel(format)) / 8;
    }
    locked->Pitch = pitch;
    locked->pBits = bits;
    if (g_gfxBackend == GFX_BACKEND_D3D9)
        D3D9_NotifyTextureModified((void*)surface); // the decoder writes a frame through pBits next
}

// ---------------------------------------------------------------------------------------------------------------
// The mesh draw path: d3dsetVertexShaderConstant, d3dsetVertexShader, d3dSetSkinMatrix, d3dDrawIndexedVertices
// ---------------------------------------------------------------------------------------------------------------

// These four were the last game-side callers of D3D8 outside this file - found by sweeping the callers of every
// one of the 283 D3D8/XGRAPHC-range entry points, after the call graph had missed them: Ghidra's body for
// RecurseAndDrawBoxes stops at 0x000dd56c although the code runs on to 0x000dd8d2, and it's that untracked tail
// (the per-primitive loop: set skin matrices, per-primitive render state, stream sources, texture, then draw)
// that calls them. They're also newer than tools/functions_action.json, hence FUNC_AT rather than AUTOINJECT.
//
// Together they explain where the world geometry goes: every model primitive is a triangle strip drawn by
// D3DDevice_DrawIndexedVertices straight from the bound index buffer's data, after the lazily-flushed
// per-draw state (view-space light constants, the vertex shader permutation) has been brought up to date.

#define D3DDevice_DrawIndexedVertices_ADDR 0x00104a60u // was undefined in Ghidra until now (hence invisible to the call graph); RET 0xc
typedef void(__stdcall *D3DDevice_DrawIndexedVerticesFn)(uint32_t primitiveType, uint32_t vertexCount, const void *pIndexData);
#define D3DDevice_DrawIndexedVertices (D3DSeamTraced("D3DDevice_DrawIndexedVertices", (D3DDevice_DrawIndexedVerticesFn)D3DDevice_DrawIndexedVertices_ADDR, D3D9_DrawIndexedVertices))

#define Gfx_LightConstantBlock      ((float*)0x002FF27Cu) // 36 floats -> vertex shader constants 0x69..0x71: 4 x view-space light position (xyz, pad), then the 4 x colour and 4 x 1/range d3dSetLight fills in
#define Gfx_LevelDirectionViewSpace ((float*)0x002FF388u) // 3 floats (+pad) -> vertex shader constant 0x7b
#define Gfx_LevelDirectionDirty     Gfx_MatrixGenFlag2     // 0x002FF278 - set to 1 by d3dSetLevelDirectionVector, -1 by d3dSetMatrix

// FUN_000e86c0 in the original: rotates a 3-vector by the upper-left 3x3 of a row-major 4x4 matrix, optionally
// adding the translation column - i.e. a full point transform (translate != 0) or a direction transform.
// Summation order matches the original's FPU sequence.
static void TransformVector3(float *out, const float *m, const float *v, bool translate) {
    out[0] = (m[2] * v[2] + m[1] * v[1]) + m[0] * v[0];
    out[1] = (m[6] * v[2] + m[5] * v[1]) + m[4] * v[0];
    out[2] = (m[10] * v[2] + m[9] * v[1]) + m[8] * v[0];
    if (translate) {
        out[0] += m[3];
        out[1] += m[7];
        out[2] += m[11];
    }
}

// Flushes the lazily-updated per-draw vertex shader constants: every light whose dirty bit is set (d3dSetLight/
// d3dDisableLight set individual bits, d3dSetMatrix sets them all) has its position re-transformed into view
// space (through the inverse of the active matrix) and the whole 9-register light block re-sent; likewise the
// level direction vector (as a direction, no translation) into constant 0x7b when its flag is set.
//
// FUNC_AT(000e4a50)
void d3dsetVertexShaderConstant(void) {
    D3DMATRIX inverseActive;
    memcpy(&inverseActive, Gfx_d3dActiveMatrix, sizeof(D3DMATRIX));
    maybeInvertRigidTransform(&inverseActive);
    const float *m = (const float*)&inverseActive;

    bool anyLight = false;
    for (int i = 0; i < 4; i++) {
        if ((Gfx_LightDirtyMask & (1u << i)) != 0) {
            TransformVector3(Gfx_LightConstantBlock + i * 4, m, (const float*)(Gfx_LightDirection + i * 3), true);
            anyLight = true;
        }
    }
    if (anyLight) {
        if (D3D_DeviceReady != 0)
            D3DDevice_SetVertexShaderConstantNotInline(0x69, Gfx_LightConstantBlock, 0x24);
        Gfx_D3DLastError = 0;
    }

    if (Gfx_LevelDirectionDirty != 0) {
        TransformVector3(Gfx_LevelDirectionViewSpace, m, Gfx_LevelDirectionVector, false);
        if (D3D_DeviceReady != 0)
            D3DDevice_SetVertexShaderConstant1(0x7b, Gfx_LevelDirectionViewSpace);
        Gfx_D3DLastError = 0;
    }

    Gfx_LightDirtyMask = 0;
    Gfx_LevelDirectionDirty = 0;
}

// Selects the vertex shader permutation for the current mode flags (Gfx_MiscModeFlags is literally the index
// into the 128 shaders xboxInitGraphics created - bit 0x2 = 0x20-byte vertex stride, 0x4/0x8 = two/four lights,
// 0x10 and 0x20 set elsewhere in this file). The "four lights" bit subsumes "two lights", so 0x4 is dropped
// when 0x8 is set. Gfx_MiscResetFlag caches the last-selected index; d3dBindBuffers/drawShard reset it to -1.
//
// FUNC_AT(000e4b30)
void d3dsetVertexShader(void) {
    uint32_t flags = Gfx_MiscModeFlags;
    if ((flags & 0x8u) != 0) {
        flags &= ~0x4u;
        Gfx_MiscModeFlags = flags;
    }
    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShader(VtxShaderHandles[flags]);
    Gfx_MiscResetFlag = Gfx_MiscModeFlags;
    Gfx_D3DLastError = 0;
}

// Uploads one 3x4 skinning matrix (12 floats) to the constant register the 53-entry table at 0x001B5208 maps
// the bone index to (plus the 0x60 base). Only caller is RecurseAndDrawBoxes' per-primitive loop.
//
// FUNC_AT(000e4e40)
void d3dSetSkinMatrix(const void *matrix3x4, uint32_t boneIndex) {
    if (boneIndex >= 0x35)
        return;
    if (D3D_DeviceReady != 0)
        D3DDevice_SetVertexShaderConstantNotInline((uint32_t)(D3D8_ShaderConstantSubIndexTable[boneIndex] + 0x60), (void*)matrix3x4, 0xc);
    Gfx_D3DLastError = 0;
}

// Draws one triangle strip of (indexCount + 2) vertices from the bound index buffer, starting at index
// startIndex, after flushing any pending light/direction constants and shader selection. The original reads
// the index data pointer back out of D3D8's own SetIndices global; g_d3dBoundIndexData (set by d3dBindBuffers)
// is the seam's copy of the same value.
//
// FUNC_AT(000e4b80)
void d3dDrawIndexedVertices(int startIndex, int indexCount) {
    if (Gfx_LightDirtyMask != 0 || Gfx_LevelDirectionDirty != 0)
        d3dsetVertexShaderConstant();
    if (Gfx_MiscResetFlag != Gfx_MiscModeFlags)
        d3dsetVertexShader();
    if (D3D_DeviceReady != 0)
        D3DDevice_DrawIndexedVertices(6 /* D3DPT_TRIANGLESTRIP */, (uint32_t)indexCount + 2, g_d3dBoundIndexData + startIndex);
    Gfx_D3DLastError = 0;
}
