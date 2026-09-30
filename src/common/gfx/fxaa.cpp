#include "fxaa.h"

#include <d3dcompiler.h>
#include <string.h>

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
// alongside a vs_3_0 vertex shader.
// ---------------------------------------------------------------------------------------------------------------

static const char g_fxaaHlsl[] =
    "float4 texel : register(c0);   // 1/width, 1/height, width, height\n"
    "sampler2D frame : register(s0);\n"
    "\n"
    "struct VS_OUT { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
    "VS_OUT vsmain(float4 pos : POSITION, float2 uv : TEXCOORD0) {\n"
    "    VS_OUT o;\n"
    "    o.pos = float4(pos.x - texel.x, pos.y + texel.y, 0, 1);   // D3D9's half-pixel offset\n"
    "    o.uv = uv;\n"
    "    return o;\n"
    "}\n"
    "\n"
    "float4 Tap(float2 uv) { return tex2Dlod(frame, float4(uv, 0, 0)); }\n"
    "float Luma(float2 uv) { return dot(Tap(uv).rgb, float3(0.299, 0.587, 0.114)); }\n"
    "\n"
    "float4 psmain(float2 uv : TEXCOORD0) : COLOR {\n"
    "    const float edgeThreshold = 0.125, edgeThresholdMin = 0.0312, subpixelQuality = 0.75;\n"
    "    float2 px = texel.xy;\n"
    "    float4 centre = Tap(uv);\n"
    "    float lM = dot(centre.rgb, float3(0.299, 0.587, 0.114));\n"
    "    float lN = Luma(uv + float2(0, -px.y)), lS = Luma(uv + float2(0, px.y));\n"
    "    float lW = Luma(uv + float2(-px.x, 0)), lE = Luma(uv + float2(px.x, 0));\n"
    "    float lMax = max(lM, max(max(lN, lS), max(lW, lE)));\n"
    "    float lMin = min(lM, min(min(lN, lS), min(lW, lE)));\n"
    "    float range = lMax - lMin;\n"
    "    if (range < max(edgeThresholdMin, lMax * edgeThreshold))\n"
    "        return centre;\n"
    "\n"
    "    float lNW = Luma(uv - px), lSE = Luma(uv + px);\n"
    "    float lNE = Luma(uv + float2(px.x, -px.y)), lSW = Luma(uv + float2(-px.x, px.y));\n"
    "    float edgeH = abs(lNW + lSW - 2 * lW) + 2 * abs(lN + lS - 2 * lM) + abs(lNE + lSE - 2 * lE);\n"
    "    float edgeV = abs(lNW + lNE - 2 * lN) + 2 * abs(lW + lE - 2 * lM) + abs(lSW + lSE - 2 * lS);\n"
    "    bool horizontal = edgeH >= edgeV;   // the edge runs across; the step is up or down\n"
    "\n"
    "    float l1 = horizontal ? lN : lW, l2 = horizontal ? lS : lE;\n"
    "    float grad1 = l1 - lM, grad2 = l2 - lM;\n"
    "    bool towards1 = abs(grad1) >= abs(grad2);\n"
    "    float gradScaled = 0.25 * max(abs(grad1), abs(grad2));\n"
    "    float stepLength = horizontal ? px.y : px.x;\n"
    "    float localAverage;\n"
    "    if (towards1) { stepLength = -stepLength; localAverage = 0.5 * (l1 + lM); }\n"
    "    else          { localAverage = 0.5 * (l2 + lM); }\n"
    "\n"
    "    // Walk both ways along the edge, from halfway across it, until the luma leaves the edge's average.\n"
    "    float2 onEdge = uv;\n"
    "    if (horizontal) onEdge.y += 0.5 * stepLength; else onEdge.x += 0.5 * stepLength;\n"
    "    float2 along = horizontal ? float2(px.x, 0) : float2(0, px.y);\n"
    "    float2 uv1 = onEdge - along, uv2 = onEdge + along;\n"
    "    float end1 = Luma(uv1) - localAverage, end2 = Luma(uv2) - localAverage;\n"
    "    bool done1 = abs(end1) >= gradScaled, done2 = abs(end2) >= gradScaled;\n"
    "    const float stride[8] = { 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 4.0, 8.0 };\n"
    "    [unroll] for (int i = 0; i < 8; i++) {\n"
    "        if (!done1) { uv1 -= along * stride[i]; end1 = Luma(uv1) - localAverage; done1 = abs(end1) >= gradScaled; }\n"
    "        if (!done2) { uv2 += along * stride[i]; end2 = Luma(uv2) - localAverage; done2 = abs(end2) >= gradScaled; }\n"
    "    }\n"
    "\n"
    "    float dist1 = horizontal ? uv.x - uv1.x : uv.y - uv1.y;\n"
    "    float dist2 = horizontal ? uv2.x - uv.x : uv2.y - uv.y;\n"
    "    bool nearer1 = dist1 < dist2;\n"
    "    float edgeOffset = 0.5 - min(dist1, dist2) / (dist1 + dist2);\n"
    "    bool centreDarker = lM < localAverage;\n"
    "    if (((nearer1 ? end1 : end2) < 0) == centreDarker)\n"
    "        edgeOffset = 0;   // the nearer end continues the centre's side of the edge: nothing to blend\n"
    "\n"
    "    float average = (2 * (lN + lS + lW + lE) + (lNW + lNE + lSW + lSE)) / 12;\n"
    "    float sub = saturate(abs(average - lM) / range);\n"
    "    sub = (-2 * sub + 3) * sub * sub;\n"
    "    float offset = max(edgeOffset, sub * sub * subpixelQuality);\n"
    "\n"
    "    float2 finalUv = uv;\n"
    "    if (horizontal) finalUv.y += offset * stepLength; else finalUv.x += offset * stepLength;\n"
    "    return float4(Tap(finalUv).rgb, centre.a);\n"
    "}\n";

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

static ID3DBlob *Compile(const char *entry, const char *profile, FxaaLogFn log) {
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr = D3DCompile(g_fxaaHlsl, sizeof(g_fxaaHlsl) - 1, "fxaa", NULL, NULL, entry, profile,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr) && log != NULL)
        log("[d3d9] FXAA: compiling %s as %s failed:\n%s\n", entry, profile,
            errors != NULL ? (const char *)errors->GetBufferPointer() : "(no message)");
    if (errors != NULL)
        errors->Release();
    return SUCCEEDED(hr) ? code : NULL;
}

// The shaders, the declaration and the state block: made once, and kept across Resets, which they survive.
static bool CreatePersistent(IDirect3DDevice9 *device, FxaaLogFn log) {
    if (g_vs != NULL)
        return true;
    ID3DBlob *vs = Compile("vsmain", "vs_3_0", log), *ps = Compile("psmain", "ps_3_0", log);
    HRESULT hr = (vs != NULL && ps != NULL) ? S_OK : E_FAIL;
    if (SUCCEEDED(hr)) hr = device->CreateVertexShader((const DWORD *)vs->GetBufferPointer(), &g_vs);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader((const DWORD *)ps->GetBufferPointer(), &g_ps);
    if (vs != NULL) vs->Release();
    if (ps != NULL) ps->Release();
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
