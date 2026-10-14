#include "fxaa.h"

#include <string.h>

#include "fxaaBytecode.h"   // fxaa.hlsl, compiled by tools/gfx/fxaa_bytecode.py

// ---------------------------------------------------------------------------------------------------------------
// FXAA, as an experiment.
//
// Both engines draw straight into the backbuffer, so this runs once the 3D scene is there and before the
// overlay goes on top (d3d9Backend.cpp decides when): the frame is copied into a texture, and a full-screen
// triangle strip draws it back into the backbuffer through the FXAA pixel shader. Everything is restored afterwards from a state block, so the
// backend's own record of the device state stays true. Render targets are outside what a state block holds,
// and are put back by hand.
//
// The shader follows the structure of Timothy Lottes' FXAA 3.11 "quality" path: a local-contrast early out,
// the edge's orientation from a 3x3 neighbourhood, a search along the edge for its ends, and a blend towards
// the neighbour across the edge by whichever is larger of the edge-end estimate and a sub-pixel term. Luma is
// computed from RGB, since the backbuffer's alpha carries none. ps_3_0 for tex2Dlod, which D3D9 only accepts
// alongside a vs_3_0 vertex shader. The shaders are fxaa.hlsl, compiled once into fxaaBytecode.h by
// tools/gfx/fxaa_bytecode.py: nothing compiles HLSL at run time.
// ---------------------------------------------------------------------------------------------------------------

struct FxaaVertex { float x, y, z, w; float u, v; };

static bool g_failed = false;
static IDirect3DDevice9 *g_owner = NULL;          // the device the objects below belong to
static IDirect3DVertexShader9 *g_vs = NULL;
static IDirect3DPixelShader9 *g_ps = NULL;
static IDirect3DVertexDeclaration9 *g_decl = NULL;
static IDirect3DStateBlock9 *g_saved = NULL;
static IDirect3DTexture9 *g_copy = NULL;          // default pool: released on Reset, made again on demand
static IDirect3DSurface9 *g_copySurface = NULL;
static UINT g_copyWidth = 0, g_copyHeight = 0;

static void Fail(FxaaLogFn log, const char *what, HRESULT hr) {
    g_failed = true;
    if (log != NULL)
        log("[d3d9] FXAA: %s failed (0x%08lx) - off for the rest of the session\n", what, (unsigned long)hr);
}

// The shaders, the declaration and the state block: made once, and kept across Resets, which they survive.
static bool CreatePersistent(IDirect3DDevice9 *device, FxaaLogFn log) {
    if (g_vs != NULL)
        return true;
    HRESULT hr = device->CreateVertexShader(g_fxaaVertexShader, &g_vs);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(g_fxaaPixelShader, &g_ps);
    if (FAILED(hr)) { Fail(log, "creating the shaders", hr); return false; }

    static const D3DVERTEXELEMENT9 elements[] = {
        { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    hr = device->CreateVertexDeclaration(elements, &g_decl);
    if (SUCCEEDED(hr)) hr = device->CreateStateBlock(D3DSBT_ALL, &g_saved);
    if (FAILED(hr)) { Fail(log, "creating the vertex declaration or state block", hr); return false; }
    if (log != NULL)
        log("[d3d9] FXAA on (AntiAliasing=1 in settings.ini; 0 turns it off)\n");
    return true;
}

static bool EnsureCopy(IDirect3DDevice9 *device, UINT width, UINT height, D3DFORMAT format, FxaaLogFn log) {
    if (g_copy != NULL && g_copyWidth == width && g_copyHeight == height)
        return true;
    Fxaa_ReleaseDefaultPool();
    HRESULT hr = device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &g_copy, NULL);
    if (SUCCEEDED(hr)) hr = g_copy->GetSurfaceLevel(0, &g_copySurface);
    if (FAILED(hr)) { Fail(log, "creating the frame copy", hr); Fxaa_ReleaseDefaultPool(); return false; }
    g_copyWidth = width;
    g_copyHeight = height;
    return true;
}

void Fxaa_ReleaseDefaultPool(void) {
    if (g_copySurface != NULL) { g_copySurface->Release(); g_copySurface = NULL; }
    if (g_copy != NULL) { g_copy->Release(); g_copy = NULL; }
    g_copyWidth = g_copyHeight = 0;
}

void Fxaa_Apply(IDirect3DDevice9 *device, IDirect3DSurface9 *backBuffer, FxaaLogFn log) {
    if (g_failed || device == NULL || backBuffer == NULL)
        return;
    if (g_owner != device && g_owner != NULL) {   // a new device: nothing made for the old one can be used
        g_failed = true;
        return;
    }
    g_owner = device;

    D3DSURFACE_DESC desc;
    if (FAILED(backBuffer->GetDesc(&desc)))
        return;
    if (!CreatePersistent(device, log) || !EnsureCopy(device, desc.Width, desc.Height, desc.Format, log))
        return;
    if (FAILED(device->StretchRect(backBuffer, NULL, g_copySurface, NULL, D3DTEXF_NONE)))
        return;   // nothing drawn yet this frame, or a transient failure: present it as it is

    IDirect3DSurface9 *oldTarget = NULL;
    device->GetRenderTarget(0, &oldTarget);
    g_saved->Capture();

    device->SetRenderTarget(0, backBuffer);   // also resets the viewport to the whole of it
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetTexture(0, g_copy);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    device->SetVertexDeclaration(g_decl);
    device->SetVertexShader(g_vs);
    device->SetPixelShader(g_ps);
    const float texel[4] = { 1.0f / desc.Width, 1.0f / desc.Height, (float)desc.Width, (float)desc.Height };
    device->SetVertexShaderConstantF(0, texel, 1);
    device->SetPixelShaderConstantF(0, texel, 1);

    const FxaaVertex quad[4] = {
        { -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 0.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    };
    if (SUCCEEDED(device->BeginScene())) {
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(FxaaVertex));
        device->EndScene();
    }

    // The render target first, since setting one resets the viewport; then the saved state over the top.
    if (oldTarget != NULL) {
        device->SetRenderTarget(0, oldTarget);
        oldTarget->Release();
    }
    g_saved->Apply();
}
