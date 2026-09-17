#include "d3d9Backend.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// See d3d9Backend.h for the overview. Checkpoint 1: device creation on CXBX's render window, clear, present,
// viewport and gamma; everything else is counted as missing. Later checkpoints add textures and the
// immediate-mode path, then the mesh path with translated vertex shaders, then render targets.

int g_gfxBackend = GFX_BACKEND_CXBX;

static IDirect3D9 *g_d3d = NULL;
static IDirect3DDevice9 *g_device = NULL;
static HWND g_window = NULL;
static D3DPRESENT_PARAMETERS g_presentParams;
static bool g_inScene = false;
static uint32_t g_frameCount = 0;

// ---------------------------------------------------------------------------------------------------------------
// Missing entry-point accounting
// ---------------------------------------------------------------------------------------------------------------

struct MissingEntry { const char *name; uint32_t count; };
static MissingEntry g_missing[128];
static int g_missingCount = 0;

void D3D9_BackendMissing(const char *entryPoint) {
    for (int i = 0; i < g_missingCount; i++) {
        if (g_missing[i].name == entryPoint || strcmp(g_missing[i].name, entryPoint) == 0) {
            g_missing[i].count++;
            return;
        }
    }
    if (g_missingCount < (int)(sizeof(g_missing) / sizeof(g_missing[0]))) {
        g_missing[g_missingCount].name = entryPoint;
        g_missing[g_missingCount].count = 1;
        g_missingCount++;
        printf("[d3d9] not implemented yet: %s\n", entryPoint);
    }
}

static void PrintMissingSummary(void) {
    if (g_missingCount == 0)
        return;
    printf("[d3d9] frame %u - entry points still unimplemented (calls so far):", g_frameCount);
    for (int i = 0; i < g_missingCount; i++)
        printf(" %s=%u", g_missing[i].name, g_missing[i].count);
    printf("\n");
}

// ---------------------------------------------------------------------------------------------------------------
// Window and device
// ---------------------------------------------------------------------------------------------------------------

// The launcher passes its own top-level window to CXBX as "/hwnd <decimal>" on the command line, and CXBX
// creates its "CxbxRender" child inside it. Prefer the child (it's the area CXBX itself would draw to and it
// tracks the launcher's resizing); fall back to the parent, then to a standalone CxbxRender window.
static HWND FindRenderWindow(void) {
    HWND parent = NULL;
    const char *cmd = GetCommandLineA();
    const char *p = (cmd != NULL) ? strstr(cmd, "/hwnd") : NULL;
    if (p != NULL)
        parent = (HWND)(uintptr_t)strtoul(p + 5, NULL, 10);

    HWND child = (parent != NULL) ? FindWindowExA(parent, NULL, "CxbxRender", NULL) : NULL;
    if (child != NULL)
        return child;
    if (parent != NULL)
        return parent;
    return FindWindowA("CxbxRender", NULL);
}

static void BeginSceneIfNeeded(void) {
    if (g_device != NULL && !g_inScene) {
        g_device->BeginScene();
        g_inScene = true;
    }
}

uint32_t D3D9_CreateDevice(uint32_t adapter, uint32_t deviceType, void *hFocusWindow, uint32_t behaviorFlags,
                           void *pPresentationParameters, void **ppDevice) {
    (void)adapter; (void)deviceType; (void)hFocusWindow; (void)behaviorFlags;
    const uint32_t *xboxParams = (const uint32_t*)pPresentationParameters; // Xbox D3DPRESENT_PARAMETERS: [0] width, [1] height, ..., [11] refresh rate
    uint32_t width = xboxParams[0], height = xboxParams[1];

    g_window = FindRenderWindow();
    if (g_window == NULL) {
        printf("[d3d9] no render window found (no /hwnd on the command line and no CxbxRender window).\n");
        return 0x8876086Cu; // D3DERR_INVALIDCALL
    }

    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (g_d3d == NULL) {
        printf("[d3d9] Direct3DCreate9 failed.\n");
        return 0x8876086Cu;
    }

    D3DADAPTER_IDENTIFIER9 ident;
    if (SUCCEEDED(g_d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &ident)))
        printf("[d3d9] adapter: %s\n", ident.Description);

    memset(&g_presentParams, 0, sizeof(g_presentParams));
    g_presentParams.Windowed = TRUE;
    g_presentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_presentParams.hDeviceWindow = g_window;
    g_presentParams.BackBufferWidth = width;
    g_presentParams.BackBufferHeight = height;
    g_presentParams.BackBufferFormat = D3DFMT_X8R8G8B8;
    g_presentParams.BackBufferCount = 1;
    g_presentParams.EnableAutoDepthStencil = TRUE;
    g_presentParams.AutoDepthStencilFormat = D3DFMT_D24S8;
    g_presentParams.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;
    HRESULT hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window, flags, &g_presentParams, &g_device);
    if (FAILED(hr)) {
        flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;
        hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window, flags, &g_presentParams, &g_device);
    }
    if (FAILED(hr)) {
        printf("[d3d9] CreateDevice failed: 0x%08lx\n", hr);
        return (uint32_t)hr;
    }

    printf("[d3d9] device created on window %p (%ux%u backbuffer, refresh %u requested).\n",
           (void*)g_window, width, height, xboxParams[11]);
    BeginSceneIfNeeded();
    *ppDevice = g_device;
    return 0;
}

uint32_t D3D9_ArePushBuffersSupported(uint32_t unused) {
    (void)unused;
    return 1; // what the D3D8 library answers too - the game keys "device exists" off it
}

void D3D9_SetPushBufferSize(uint32_t pushBufferSize, uint32_t kickOffSize) {
    (void)pushBufferSize; (void)kickOffSize; // no push buffer here
}

// Xbox clear flags: bits 4..7 are the per-channel colour masks (0xF0 = all), bit 0 = Z, bit 1 = stencil.
void D3D9_Clear(uint32_t rectCount, void *pRects, uint32_t flags, uint32_t colour, float z, uint32_t stencil) {
    (void)rectCount; (void)pRects;
    if (g_device == NULL)
        return;
    DWORD d3dFlags = 0;
    if (flags & 0xF0) d3dFlags |= D3DCLEAR_TARGET;
    if (flags & 0x01) d3dFlags |= D3DCLEAR_ZBUFFER;
    if (flags & 0x02) d3dFlags |= D3DCLEAR_STENCIL;
    if (d3dFlags != 0)
        g_device->Clear(0, NULL, d3dFlags, colour, z, stencil);
}

void D3D9_Swap(uint32_t type) {
    (void)type;
    if (g_device == NULL)
        return;
    if (g_inScene) {
        g_device->EndScene();
        g_inScene = false;
    }
    HRESULT hr = g_device->Present(NULL, NULL, NULL, NULL);
    if (hr == D3DERR_DEVICELOST) {
        if (g_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            printf("[d3d9] device lost - resetting (device state is not restored yet at this checkpoint).\n");
            g_device->Reset(&g_presentParams);
        }
    }
    g_frameCount++;
    if (g_frameCount == 1 || g_frameCount % 600 == 0)
        PrintMissingSummary();
    BeginSceneIfNeeded();
}

void D3D9_SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float minZ, float maxZ) {
    if (g_device == NULL)
        return;
    D3DVIEWPORT9 vp = { x, y, width, height, minZ, maxZ };
    g_device->SetViewport(&vp);
}

// Xbox D3DGAMMARAMP is three 256-entry byte tables; D3D9's is three 16-bit tables. (D3D9 only honours gamma
// ramps in fullscreen mode, so this is a no-op visually while we run windowed - kept for completeness.)
void D3D9_SetGammaRamp(uint32_t flags, void *pRamp) {
    (void)flags;
    if (g_device == NULL || pRamp == NULL)
        return;
    const uint8_t *xbox = (const uint8_t*)pRamp;
    D3DGAMMARAMP ramp;
    for (int i = 0; i < 256; i++) {
        ramp.red[i]   = (WORD)(xbox[i] * 257);
        ramp.green[i] = (WORD)(xbox[256 + i] * 257);
        ramp.blue[i]  = (WORD)(xbox[512 + i] * 257);
    }
    g_device->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &ramp);
}

// ---------------------------------------------------------------------------------------------------------------
// Vertex shaders - checkpoint 1 only records them. Handles are tagged tokens; nothing dereferences them.
// ---------------------------------------------------------------------------------------------------------------

struct RecordedVertexShader { const void *declaration; const void *function; };
static RecordedVertexShader g_vertexShaders[160];
static int g_vertexShaderCount = 0;

uint32_t D3D9_CreateVertexShader(const void *pDeclaration, const void *pFunction, void **pHandle, uint32_t usage) {
    (void)usage;
    if (g_vertexShaderCount >= (int)(sizeof(g_vertexShaders) / sizeof(g_vertexShaders[0])))
        return 0x8876086Cu;
    g_vertexShaders[g_vertexShaderCount].declaration = pDeclaration;
    g_vertexShaders[g_vertexShaderCount].function = pFunction;
    *pHandle = (void*)(uintptr_t)(0x56530000u | (uint32_t)g_vertexShaderCount); // 'VS' tag + index
    g_vertexShaderCount++;
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Surfaces. The seam and the video decoder expect Xbox-layout surface objects (Common, Data, Lock, Format,
// Size, Parent), so hand those out. There is no CPU-visible backbuffer here, so the dummy backbuffer's Data
// word is 0: psiBlurScreen (which reads the live backbuffer through the physical alias) gets a harmless read
// of Xbox physical page 0 under CXBX's memory map rather than real pixels - that path becomes a GPU copy in a
// later checkpoint.
// ---------------------------------------------------------------------------------------------------------------

struct XboxSurface { uint32_t Common, Data, Lock, Format, Size, Parent; };
#define XBOX_SURFACE_COMMON 0x01050001u // D3DCOMMON_TYPE_SURFACE, refcount 1
static XboxSurface g_dummyBackBuffer   = { XBOX_SURFACE_COMMON | 0x7FFE, 0, 0, 0, 0, 0 };
static XboxSurface g_dummyRenderTarget = { XBOX_SURFACE_COMMON | 0x7FFE, 0, 0, 0, 0, 0 };
static XboxSurface g_dummyDepthStencil = { XBOX_SURFACE_COMMON | 0x7FFE, 0, 0, 0, 0, 0 };
static XboxSurface *g_allocatedSurfaces[64];
static int g_allocatedSurfaceCount = 0;

uint32_t *D3D9_GetBackBuffer2(int32_t backBufferIndex) {
    (void)backBufferIndex;
    return (uint32_t*)&g_dummyBackBuffer;
}

// Same construction as D3D8's own GetSurfaceLevel2 (FUN_0010ba80): a fresh surface object aliasing the
// texture's level-0 data, with the top nibble of Data stripped, and the caller owns one reference.
void *D3D9_GetSurfaceLevel2(void *pTexture, uint32_t level) {
    (void)level; // only level 0 is ever asked for
    const uint32_t *header = (const uint32_t*)pTexture;
    XboxSurface *s = (XboxSurface*)malloc(sizeof(XboxSurface));
    if (s == NULL)
        return NULL;
    s->Common = XBOX_SURFACE_COMMON;
    s->Data = header[1] & 0x0FFFFFFFu;
    s->Lock = 0;
    s->Format = header[3];
    s->Size = header[4];
    s->Parent = (uint32_t)(uintptr_t)pTexture;
    if (g_allocatedSurfaceCount < (int)(sizeof(g_allocatedSurfaces) / sizeof(g_allocatedSurfaces[0])))
        g_allocatedSurfaces[g_allocatedSurfaceCount++] = s;
    return s;
}

void *D3D9_GetRenderTarget2(void) {
    return &g_dummyRenderTarget;
}

void *D3D9_GetDepthStencilSurface2(void) {
    return &g_dummyDepthStencil;
}

// Xbox refcount semantics on the Common word's low 16 bits; surface objects this backend allocated are freed
// on their last release, everything else (texture headers in Gfx's table, the static dummies) is just counted.
uint32_t D3D9_ResourceRelease(void *pResource) {
    if (pResource == NULL)
        return 0;
    uint32_t *common = (uint32_t*)pResource;
    uint32_t refCount = *common & 0xFFFFu;
    if (refCount > 1) {
        *common = *common - 1;
        return refCount - 1;
    }
    for (int i = 0; i < g_allocatedSurfaceCount; i++) {
        if (g_allocatedSurfaces[i] == pResource) {
            free(pResource);
            g_allocatedSurfaces[i] = g_allocatedSurfaces[--g_allocatedSurfaceCount];
            return 0;
        }
    }
    return 0;
}

void D3D9_BlockUntilNotBusy(void *pResource) {
    (void)pResource; // no GPU-side ownership of CPU memory here
}
