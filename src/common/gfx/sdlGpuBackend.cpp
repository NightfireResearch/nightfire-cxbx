#include "d3d9Backend.h"

#include "backendHost.h"
#include "fixedFunction.h"
#include "nv2aPixelShader.h"
#include "nv2aVertexShader.h"
#include "shaderMsl.h"
#include "shaderSpirv.h"
#include "xboxTexture.h"
#include "../../driving/platform/Host.h"
#include "../../driving/platform/Ptr32.h"   // PtrFrom32, PtrTo32: the address words in the resource headers
#ifdef NF_ANTIALIASING
#include "fxaaSpirv.h"   // fxaa.hlsl's pixel shader, compiled by tools/gfx/fxaa_spirv.py
#endif

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// The D3D8 seam's backend on SDL3's GPU API (NIGHTFIRE_GFX=sdlgpu): the same D3D9_* entry points as d3d9Backend.cpp,
// for hosts without Direct3D 9 - SDL's Vulkan driver on Windows and Linux, its Metal driver on macOS (each module
// translated to MSL by SPIRV-Cross, shaderMsl.h; docs/driving/macos.md package G3). It follows d3d9Backend.cpp call for call: the same texture cache, the same
// stage packing and constants, the same render-target and visibility-test handling. What D3D9 did by itself is done
// here: the fixed-function pipeline is fixedFunction.cpp's programs, the NV2A translators write SPIR-V
// (shaderSpirv.h), the alpha test and fog blend are the end of every pixel program, and render state is baked into
// pipelines, cached by state.
//
// SDL's GPU API records into command buffers and uploads only between render passes, so the frame is recorded here
// and replayed at a flush: every upload of the batch first (the vertex and index ring, the textures), then the passes
// and draws in order. A flush happens at Swap, and earlier when something needs results now (a readback, a dump, a
// visibility test's count) or a texture is uploaded twice in one batch. Vertex data is copied per draw, exactly the
// range the draw reads, as d3d9Backend.cpp does for an engine with volatile streams (g_streamsVolatile).
//
// The back buffer is a texture of its own, presented by copying it into the window's swapchain on the window's
// thread. A run nobody watches (GfxHost_PresentEvery, the lockstep runs) never claims the window: it renders
// offscreen, and the frame dumps read the texture.
//
// Not carried over from d3d9Backend.cpp: texture replacement and texture dumps (textureReplace.cpp works on D3D9
// textures), the draw traces, DumpBurst, CheckVertices, and the gamma ramp (D3D9 ignores it in a window too).
// ---------------------------------------------------------------------------------------------------------------

static void GpuLog(const char *fmt, ...) {
    static FILE *logFile = NULL;
    static bool opened = false;
    if (!opened) { opened = true; logFile = fopen("sdlgpu_backend.log", "w"); }
    va_list ap;
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    if (logFile != NULL) { va_start(ap, fmt); vfprintf(logFile, fmt, ap); va_end(ap); fflush(logFile); }
    fflush(stdout);
}

static int SettingInt(const char *key, int defaultValue) {
    char value[64];
    if (HostGetProfileString("Settings", key, "", value, sizeof(value), ".\\settings.ini") == 0)
        return defaultValue;
    return (int)strtol(value, NULL, 10);
}

static SDL_GPUDevice *g_device = NULL;
static SDL_Window *g_window = NULL;   // claimed only when frames are presented
static uint32_t g_frameCount = 0;
static int g_targetFrameRate = 0;
static double g_nextFrameDeadline = 0;
static int64_t g_qpcFrequency = 1;

static double NowSeconds(void) {
    int64_t t;
    HostQueryPerformanceCounter(&t);
    return (double)t / (double)g_qpcFrequency;
}

static uint64_t g_statDraws = 0, g_statBatches = 0, g_statPipelines = 0, g_statTextureUploads = 0;

static void ReportFrameTiming(double arrivedAtPacer, double leftPacer) {
    if (!GfxHost_PerfLogEnabled())
        return;
    static double windowStart = 0.0, busySeconds = 0.0, pacedSeconds = 0.0, previousLeft = 0.0;
    static int frames = 0;
    if (windowStart == 0.0) {
        windowStart = arrivedAtPacer;
        previousLeft = leftPacer;
        return;
    }
    busySeconds += arrivedAtPacer - previousLeft;
    pacedSeconds += leftPacer - arrivedAtPacer;
    previousLeft = leftPacer;
    frames++;
    double elapsed = leftPacer - windowStart;
    if (elapsed < 5.0)
        return;
    printf("[perf] %.1f fps (asked for %d), %.1f ms working + %.1f ms waiting per frame\n", frames / elapsed,
           g_targetFrameRate, busySeconds / frames * 1000.0, pacedSeconds / frames * 1000.0);
    printf("[perf]   per frame: %llu draws, %llu batches, %llu texture uploads; %llu pipelines in all\n",
           (unsigned long long)(g_statDraws / frames), (unsigned long long)(g_statBatches / frames),
           (unsigned long long)(g_statTextureUploads / frames), (unsigned long long)g_statPipelines);
    g_statDraws = g_statBatches = g_statTextureUploads = 0;
    fflush(stdout);
    windowStart = leftPacer;
    busySeconds = pacedSeconds = 0.0;
    frames = 0;
    GfxHost_ReportPeriodic();
}

// The Xbox's Swap waited for the vertical blank; hold each frame to the period the same way d3d9Backend.cpp does.
static void PaceFrame(void) {
    if (g_targetFrameRate <= 0)
        return;
    double now = NowSeconds();
    if (GfxHost_PresentEvery() != 0) {
        ReportFrameTiming(now, now);
        return;
    }
    double period = 1.0 / (double)g_targetFrameRate;
    double arrived = now;
    if (g_nextFrameDeadline == 0 || now > g_nextFrameDeadline + 0.25)
        g_nextFrameDeadline = now;
    while (now < g_nextFrameDeadline) {
        if (g_nextFrameDeadline - now > 0.002)
            HostSleep(1);
        now = NowSeconds();
    }
    g_nextFrameDeadline += period;
    ReportFrameTiming(arrived, now);
}

// ---------------------------------------------------------------------------------------------------------------
// Missing entry-point accounting, as d3d9Backend.cpp keeps it.
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
        GpuLog("[sdlgpu] not implemented yet: %s\n", entryPoint);
    }
}

static void PrintMissingSummary(void) {
    if (g_missingCount == 0)
        return;
    GpuLog("[sdlgpu] frame %u - entry points still unimplemented (calls so far):", g_frameCount);
    for (int i = 0; i < g_missingCount; i++)
        GpuLog(" %s=%u", g_missing[i].name, g_missing[i].count);
    GpuLog("\n");
}

// ---------------------------------------------------------------------------------------------------------------
// The XBE's D3D8 state, read at draw time (see d3d9Backend.cpp).
// ---------------------------------------------------------------------------------------------------------------

uintptr_t g_xboxTextureStateTable = 0;
uintptr_t g_xboxRenderStateTable = 0;
uintptr_t g_overlayTableBase = 0;
uintptr_t g_overlayTableEnd = 0;
bool g_streamsVolatile = false;

#define XBOX_TEXTURE_STATE(stage, index) (*(const uint32_t*)(g_xboxTextureStateTable + (stage) * 0x80u + (index) * 4u))
#define XBOX_RENDER_STATE(index)         (*(const uint32_t*)(g_xboxRenderStateTable + (index) * 4u))
enum { XTSS_TEXCOORDINDEX = 28, XTSS_ADDRESSU = 0, XTSS_ADDRESSV = 1, XTSS_MAGFILTER = 3, XTSS_MINFILTER = 4, XTSS_MIPFILTER = 5, XTSS_MIPMAPLODBIAS = 6,
       XTSS_COLOROP = 12, XTSS_COLORARG0 = 13, XTSS_COLORARG1 = 14, XTSS_COLORARG2 = 15,
       XTSS_ALPHAOP = 16, XTSS_ALPHAARG0 = 17, XTSS_ALPHAARG1 = 18, XTSS_ALPHAARG2 = 19, XTSS_BORDERCOLOR = 29 };
enum { XRS_FOGENABLE = 92, XRS_FOGTABLEMODE = 93, XRS_FOGSTART = 94, XRS_FOGEND = 95, XRS_FOGDENSITY = 96 };
enum { XTSS_COLORSIGN = 10, XTSS_BUMPENVMAT00 = 22 };

#define BACKBUFFER_DATA_SENTINEL 0x0BB00000u
static XboxSurface g_dummyBackBuffer   = { XBOX_SURFACE_COMMON | 0x7FFE, BACKBUFFER_DATA_SENTINEL, 0, 0, 0, 0 };
static XboxSurface g_dummyRenderTarget = { XBOX_SURFACE_COMMON | 0x7FFE, 0, 0, 0, 0, 0 };
static XboxSurface g_dummyDepthStencil = { XBOX_SURFACE_COMMON | 0x7FFE, 0, 0, 0, 0, 0 };

void D3D9_XGSetTextureHeader(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage, int format,
                             uint32_t pool, void *pTexture, uint32_t data, uint32_t pitch) {
    (void)usage; (void)pool;
    XboxPixelContainer *h = (XboxPixelContainer*)pTexture;
    uint32_t f = (uint32_t)format & 0xFF;
    if (levels == 0) levels = 1;
    h->Common = XBOX_TEXTURE_COMMON;
    h->Data = data;
    h->Lock = 0;
    if (XboxFormatIsLinear(f)) {
        if (pitch == 0)
            pitch = ((width * XboxFormatBitsPerPixel(f) / 8) + 63) & ~63u;
        h->Format = 0x00000001u | (2u << 4) | (f << 8) | (1u << 16);
        h->Size = (width - 1) | ((height - 1) << 12) | ((pitch / 64 - 1) << 24);
    } else {
        h->Format = 0x00000001u | (2u << 4) | (f << 8) | ((levels & 0xF) << 16) | (Log2Floor(width) << 20) | (Log2Floor(height) << 24);
        h->Size = 0;
    }
}

static const uint8_t *XboxDataPointer(uint32_t dataWord) {
    return PtrFrom32<const uint8_t>(dataWord);
}

// ---------------------------------------------------------------------------------------------------------------
// The batch: what is recorded between flushes.
//
// Uploads go into two mapped transfer buffers - the ring, for vertices and indices, copied whole into one GPU buffer
// at the flush, and the texture area - and the commands into a list replayed in order.
// ---------------------------------------------------------------------------------------------------------------

#define RING_BYTES (16u << 20)
#define UPLOAD_BYTES (32u << 20)
#define RING_SLACK 8u   // a declaration longer than its stream's stride reads past the last vertex

static SDL_GPUBuffer *g_ring = NULL;
static SDL_GPUTransferBuffer *g_ringTransfer = NULL, *g_uploadTransfer = NULL, *g_downloadTransfer = NULL;
static uint8_t *g_ringMapped = NULL, *g_uploadMapped = NULL;
static uint32_t g_ringUsed = 0, g_uploadUsed = 0, g_downloadSize = 0, g_downloadUsed = 0;
static uint32_t g_batch = 1;   // counts flushes

struct TextureUpload { SDL_GPUTexture *texture; uint32_t level, layer, width, height, offset, rowTexels; };
static std::vector<TextureUpload> g_textureUploads;
static std::vector<SDL_GPUTexture *> g_releaseAfterFlush;

enum CommandKind { kCommandPass, kCommandDraw, kCommandBlit, kCommandDownload };
struct PassCommand {
    SDL_GPUTexture *colour, *depth;
    bool clearColour, clearDepth;
    SDL_FColor clearValue;
    float clearDepthValue;
};
struct DrawCommand {
    SDL_GPUGraphicsPipeline *pipeline;
    uint32_t bufferCount;
    uint32_t bufferOffset[16];
    bool indexed;
    uint32_t indexOffset;
    uint32_t count, instances;
    int32_t vertexOffset;
    int vertexUniforms, pixelUniforms;   // snapshots
    uint32_t pixelUniformBytes;
    uint32_t samplerCount;
    SDL_GPUTextureSamplerBinding samplers[4];
    SDL_GPUViewport viewport;
};
struct DownloadCommand { SDL_GPUTexture *texture; uint32_t width, height, offset; };
struct Command { CommandKind kind; uint32_t index; };
static std::vector<Command> g_commands;
static std::vector<PassCommand> g_passes;
static std::vector<DrawCommand> g_draws;
static std::vector<SDL_GPUBlitInfo> g_blits;
static std::vector<DownloadCommand> g_downloads;
static std::vector<float> g_vertexUniformSnapshots, g_pixelUniformSnapshots;
static bool g_passOpen = false;   // the last command is a pass that draws may join
static SDL_GPUTexture *g_passColour = NULL, *g_passDepth = NULL;

// Results of the last flush's downloads, by download index.
static std::vector<uint8_t> g_downloaded;

static void Flush(void);
static void CountVisibility(uint32_t batch);

static uint32_t Align16(uint32_t v) { return (v + 15u) & ~15u; }

static void MapTransferBuffers(void) {
    g_ringMapped = (uint8_t *)SDL_MapGPUTransferBuffer(g_device, g_ringTransfer, true);
    g_uploadMapped = (uint8_t *)SDL_MapGPUTransferBuffer(g_device, g_uploadTransfer, true);
    if (g_ringMapped == NULL || g_uploadMapped == NULL)
        GpuLog("[sdlgpu] mapping the transfer buffers failed: %s\n", SDL_GetError());
    g_ringUsed = g_uploadUsed = 0;
}

// Room for bytes more in the ring: the offset they go at. Flushes first if the batch's ring is full.
static uint32_t RingReserve(uint32_t bytes) {
    if (g_ringUsed + Align16(bytes) > RING_BYTES)
        Flush();
    uint32_t offset = g_ringUsed;
    g_ringUsed += Align16(bytes);
    return offset;
}

static uint32_t UploadReserve(uint32_t bytes) {
    if (g_uploadUsed + Align16(bytes) > UPLOAD_BYTES)
        Flush();
    uint32_t offset = g_uploadUsed;
    g_uploadUsed += Align16(bytes);
    return offset;
}

static void BeginPass(SDL_GPUTexture *colour, SDL_GPUTexture *depth) {
    PassCommand p;
    memset(&p, 0, sizeof(p));
    p.colour = colour;
    p.depth = depth;
    Command c = { kCommandPass, (uint32_t)g_passes.size() };
    g_passes.push_back(p);
    g_commands.push_back(c);
    g_passOpen = true;
    g_passColour = colour;
    g_passDepth = depth;
}

// A pass whose first command is a clear: an empty pass takes it on as its load operation.
static void ClearPass(SDL_GPUTexture *colour, SDL_GPUTexture *depth, bool clearColour, SDL_FColor value, bool clearDepth,
                      float depthValue) {
    bool joinable = g_passOpen && g_passColour == colour && g_passDepth == depth && !g_commands.empty() &&
                    g_commands.back().kind == kCommandPass;
    if (!joinable)
        BeginPass(colour, depth);
    PassCommand &p = g_passes.back();
    if (clearColour) { p.clearColour = true; p.clearValue = value; }
    if (clearDepth) { p.clearDepth = true; p.clearDepthValue = depthValue; }
}

static void RecordBlit(const SDL_GPUBlitInfo &blit) {
    Command c = { kCommandBlit, (uint32_t)g_blits.size() };
    g_blits.push_back(blit);
    g_commands.push_back(c);
    g_passOpen = false;
}

// A download of a whole texture level 0 into the download buffer; its bytes are in g_downloaded from that offset
// after the flush.
static uint32_t RecordDownload(SDL_GPUTexture *texture, uint32_t width, uint32_t height, uint32_t bytesPerPixel) {
    uint32_t bytes = width * height * bytesPerPixel;
    DownloadCommand d = { texture, width, height, g_downloadUsed };
    g_downloadUsed += Align16(bytes);
    Command c = { kCommandDownload, (uint32_t)g_downloads.size() };
    g_downloads.push_back(d);
    g_commands.push_back(c);
    g_passOpen = false;
    return d.offset;
}

static SDL_GPUFence *g_frameFence = NULL;   // the last frame's batch, waited for before the next is submitted
static bool g_fenceNextFlush = false;

static void Flush(void) {
    if (g_device == NULL)
        return;
    SDL_UnmapGPUTransferBuffer(g_device, g_ringTransfer);
    SDL_UnmapGPUTransferBuffer(g_device, g_uploadTransfer);
    if (g_downloadUsed > g_downloadSize) {
        if (g_downloadTransfer != NULL)
            SDL_ReleaseGPUTransferBuffer(g_device, g_downloadTransfer);
        g_downloadSize = g_downloadUsed + (4u << 20);
        SDL_GPUTransferBufferCreateInfo info = { SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, g_downloadSize, 0 };
        g_downloadTransfer = SDL_CreateGPUTransferBuffer(g_device, &info);
    }

    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g_device);
    if (cb == NULL) {
        GpuLog("[sdlgpu] no command buffer: %s\n", SDL_GetError());
    } else {
        if (g_ringUsed > 0 || !g_textureUploads.empty()) {
            SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cb);
            if (g_ringUsed > 0) {
                SDL_GPUTransferBufferLocation from = { g_ringTransfer, 0 };
                SDL_GPUBufferRegion to = { g_ring, 0, g_ringUsed };
                SDL_UploadToGPUBuffer(copy, &from, &to, true);
            }
            for (size_t i = 0; i < g_textureUploads.size(); i++) {
                const TextureUpload &u = g_textureUploads[i];
                SDL_GPUTextureTransferInfo from = { g_uploadTransfer, u.offset, u.rowTexels, 0 };
                SDL_GPUTextureRegion to;
                memset(&to, 0, sizeof(to));
                to.texture = u.texture;
                to.mip_level = u.level;
                to.layer = u.layer;
                to.w = u.width;
                to.h = u.height;
                to.d = 1;
                SDL_UploadToGPUTexture(copy, &from, &to, false);
            }
            SDL_EndGPUCopyPass(copy);
        }
        SDL_GPURenderPass *pass = NULL;
        SDL_GPUGraphicsPipeline *boundPipeline = NULL;
        int pushedVertex = -1, pushedPixel = -1;
        for (size_t i = 0; i < g_commands.size(); i++) {
            const Command &c = g_commands[i];
            if (c.kind != kCommandDraw && pass != NULL) {
                SDL_EndGPURenderPass(pass);
                pass = NULL;
            }
            if (c.kind == kCommandPass) {
                const PassCommand &p = g_passes[c.index];
                SDL_GPUColorTargetInfo colour;
                memset(&colour, 0, sizeof(colour));
                colour.texture = p.colour;
                colour.load_op = p.clearColour ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                colour.store_op = SDL_GPU_STOREOP_STORE;
                colour.clear_color = p.clearValue;
                SDL_GPUDepthStencilTargetInfo depth;
                memset(&depth, 0, sizeof(depth));
                depth.texture = p.depth;
                depth.load_op = p.clearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                depth.store_op = SDL_GPU_STOREOP_STORE;
                depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                depth.clear_depth = p.clearDepthValue;
                pass = SDL_BeginGPURenderPass(cb, &colour, p.colour != NULL ? 1 : 0, p.depth != NULL ? &depth : NULL);
                boundPipeline = NULL;
                pushedVertex = pushedPixel = -1;
            } else if (c.kind == kCommandDraw && pass != NULL) {
                const DrawCommand &d = g_draws[c.index];
                if (d.pipeline != boundPipeline) {
                    SDL_BindGPUGraphicsPipeline(pass, d.pipeline);
                    boundPipeline = d.pipeline;
                }
                SDL_SetGPUViewport(pass, &d.viewport);
                SDL_GPUBufferBinding buffers[16];
                for (uint32_t b = 0; b < d.bufferCount; b++) { buffers[b].buffer = g_ring; buffers[b].offset = d.bufferOffset[b]; }
                if (d.bufferCount > 0)
                    SDL_BindGPUVertexBuffers(pass, 0, buffers, d.bufferCount);
                if (d.samplerCount > 0)
                    SDL_BindGPUFragmentSamplers(pass, 0, d.samplers, d.samplerCount);
                if (d.vertexUniforms != pushedVertex) {
                    SDL_PushGPUVertexUniformData(cb, 0, &g_vertexUniformSnapshots[(size_t)d.vertexUniforms * kSpirvVertexConstants * 4],
                                                 kSpirvVertexConstants * 16);
                    pushedVertex = d.vertexUniforms;
                }
                if (d.pixelUniforms != pushedPixel) {
                    SDL_PushGPUFragmentUniformData(cb, 0, &g_pixelUniformSnapshots[(size_t)d.pixelUniforms * kSpirvPixelConstants * 4],
                                                   d.pixelUniformBytes);
                    pushedPixel = d.pixelUniforms;
                }
                if (d.indexed) {
                    SDL_GPUBufferBinding index = { g_ring, d.indexOffset };
                    SDL_BindGPUIndexBuffer(pass, &index, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                    SDL_DrawGPUIndexedPrimitives(pass, d.count, d.instances, 0, d.vertexOffset, 0);
                } else {
                    SDL_DrawGPUPrimitives(pass, d.count, d.instances, 0, 0);
                }
            } else if (c.kind == kCommandBlit) {
                SDL_BlitGPUTexture(cb, &g_blits[c.index]);
            } else if (c.kind == kCommandDownload) {
                const DownloadCommand &d = g_downloads[c.index];
                SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cb);
                SDL_GPUTextureRegion from;
                memset(&from, 0, sizeof(from));
                from.texture = d.texture;
                from.w = d.width;
                from.h = d.height;
                from.d = 1;
                SDL_GPUTextureTransferInfo to = { g_downloadTransfer, d.offset, d.width, d.height };
                SDL_DownloadFromGPUTexture(copy, &from, &to);
                SDL_EndGPUCopyPass(copy);
            }
        }
        if (pass != NULL)
            SDL_EndGPURenderPass(pass);
        if (!g_downloads.empty()) {
            SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
            if (fence != NULL) {
                SDL_WaitForGPUFences(g_device, true, &fence, 1);
                SDL_ReleaseGPUFence(g_device, fence);
            }
            g_downloaded.resize(g_downloadUsed);
            const uint8_t *mapped = (const uint8_t *)SDL_MapGPUTransferBuffer(g_device, g_downloadTransfer, false);
            if (mapped != NULL) {
                memcpy(g_downloaded.data(), mapped, g_downloadUsed);
                SDL_UnmapGPUTransferBuffer(g_device, g_downloadTransfer);
            }
            CountVisibility(g_batch);
        } else if (g_fenceNextFlush) {
            SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
            if (g_frameFence != NULL) {
                SDL_WaitForGPUFences(g_device, true, &g_frameFence, 1);
                SDL_ReleaseGPUFence(g_device, g_frameFence);
            }
            g_frameFence = fence;
        } else {
            SDL_SubmitGPUCommandBuffer(cb);
        }
    }
    g_fenceNextFlush = false;
    for (size_t i = 0; i < g_releaseAfterFlush.size(); i++)
        SDL_ReleaseGPUTexture(g_device, g_releaseAfterFlush[i]);
    g_releaseAfterFlush.clear();
    g_commands.clear();
    g_passes.clear();
    g_draws.clear();
    g_blits.clear();
    g_downloads.clear();
    g_textureUploads.clear();
    g_vertexUniformSnapshots.clear();
    g_pixelUniformSnapshots.clear();
    g_downloadUsed = 0;
    g_passOpen = false;
    g_batch++;
    g_statBatches++;
    MapTransferBuffers();
}

// ---------------------------------------------------------------------------------------------------------------
// Programs: SDL shaders from SPIR-V, or from its MSL where the device takes MSL (SDL's Metal driver), and
// NIGHTFIRE_SPIRVDUMP=1's copy of every module in spirv/, each also as MSL, for spirv-val and spirvmsl.
// ---------------------------------------------------------------------------------------------------------------

static bool g_shadersAsMsl = false;

static bool SpirvDumpWanted(void) {
    static int wanted = -1;
    if (wanted < 0) {
        const char *v = getenv("NIGHTFIRE_SPIRVDUMP");
        wanted = v != NULL && v[0] == '1';
        if (wanted) SDL_CreateDirectory("spirv");
    }
    return wanted != 0;
}

static void DumpShader(const char *name, const char *extension, const void *code, size_t bytes) {
    char path[128];
    snprintf(path, sizeof(path), "spirv/%s.%s", name, extension);
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return;
    fwrite(code, 1, bytes, f);
    fclose(f);
}

static SDL_GPUShader *CreateShader(const char *name, const std::vector<uint32_t> &words, bool pixel, uint32_t samplers,
                                   const char *entryPoint = "main") {
    bool dump = SpirvDumpWanted();
    if (dump)
        DumpShader(name, "spv", words.data(), words.size() * 4);
    std::string msl, mslEntryPoint;
    if (g_shadersAsMsl || dump) {
        std::string error;
        if (!ShaderMsl_Translate(words.data(), words.size(), &msl, &mslEntryPoint, &error)) {
            GpuLog("[sdlgpu] %s: no MSL: %s\n", name, error.c_str());
            if (g_shadersAsMsl)
                return NULL;
        } else if (dump) {
            DumpShader(name, "metal", msl.data(), msl.size());
        }
    }
    SDL_GPUShaderCreateInfo info;
    memset(&info, 0, sizeof(info));
    if (g_shadersAsMsl) {
        info.code_size = msl.size();
        info.code = (const Uint8 *)msl.c_str();
        info.entrypoint = mslEntryPoint.c_str();
        info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else {
        info.code_size = words.size() * 4;
        info.code = (const Uint8 *)words.data();
        info.entrypoint = entryPoint;
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    }
    info.stage = pixel ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX;
    info.num_samplers = samplers;
    info.num_uniform_buffers = 1;
    SDL_GPUShader *shader = SDL_CreateGPUShader(g_device, &info);
    if (shader == NULL)
        GpuLog("[sdlgpu] %s: SDL_CreateGPUShader failed: %s\n", name, SDL_GetError());
    return shader;
}

// The samplers a pixel program declares: one past the highest it reads.
static uint32_t SamplerCount(uint32_t mask) {
    uint32_t count = 0;
    for (uint32_t s = 0; s < 16; s++) if (mask & (1u << s)) count = s + 1;
    return count;
}

// ---------------------------------------------------------------------------------------------------------------
// Host textures, keyed by the Xbox header address, made and uploaded lazily at bind time (d3d9Backend.cpp's cache).
// Every format arrives as B8G8R8A8 - converted on the way in, X8 alpha set opaque, L8/A8/A8L8 and the 16-bit formats
// widened, P8 through its palette - except DXT, which stays BC1-3 where the device samples them.
// ---------------------------------------------------------------------------------------------------------------

struct HostTexture {
    const void *header;
    uint32_t data, format, size;
    SDL_GPUTexture *texture;
    uint32_t width, height;
    uint32_t uploadedFrame, uploadedBatch;
    bool dirty, renderTarget, opaque;
};
static HostTexture g_textures[2200];
static int g_textureCount = 0;
static bool g_bcSupported = false;
static SDL_GPUTexture *g_blackTexture = NULL, *g_blackCube = NULL;   // (0, 0, 0, 1), for what nothing is bound to

static HostTexture *FindHostTexture(const void *header) {
    for (int i = 0; i < g_textureCount; i++)
        if (g_textures[i].header == header)
            return &g_textures[i];
    return NULL;
}

static HostTexture *FindOrAddHostTexture(const void *header) {
    HostTexture *t = FindHostTexture(header);
    if (t != NULL)
        return t;
    if (g_textureCount >= (int)(sizeof(g_textures) / sizeof(g_textures[0])))
        return NULL;
    t = &g_textures[g_textureCount++];
    memset(t, 0, sizeof(*t));
    t->header = header;
    t->dirty = true;
    return t;
}

static void MarkTextureDirty(const void *header) {
    HostTexture *t = FindHostTexture(header);
    if (t != NULL && !t->renderTarget)
        t->dirty = true;
}

static void ReleaseHostTexture(HostTexture *t) {
    if (t->texture != NULL) g_releaseAfterFlush.push_back(t->texture);
    t->texture = NULL;
    t->renderTarget = false;
    t->dirty = true;
}

static void InvalidateHostBuffers(const void *obj) { (void)obj; }   // vertex data is copied per draw: nothing cached
static void CaptureBackBufferInto(void *header);

void D3D9_ResourceRegister(void *pResource, uint32_t data) {
    XboxPixelContainer *h = (XboxPixelContainer*)pResource;
    h->Data = h->Data + data;
    HostTexture *t = FindHostTexture(pResource);
    if (t != NULL && t->renderTarget)
        ReleaseHostTexture(t);
    MarkTextureDirty(pResource);
    InvalidateHostBuffers(pResource);
    if ((data | 0x80000000u) == (BACKBUFFER_DATA_SENTINEL | 0x80000000u))
        CaptureBackBufferInto(pResource);
}

void D3D9_NotifyTextureModified(void *pTextureOrSurface) {
    const uint32_t *obj = (const uint32_t*)pTextureOrSurface;
    if (obj == NULL)
        return;
    if ((obj[0] & 0x70000u) == 0x50000u) {
        const XboxSurface *s = (const XboxSurface*)obj;
        if (s->Parent != 0)
            MarkTextureDirty(PtrFrom32<const void>(s->Parent));
        return;
    }
    MarkTextureDirty(pTextureOrSurface);
}

static const uint32_t *g_palette[4];
static const uint32_t *g_uploadPalette = NULL;

void D3D9_SetPalette(uint32_t stage, const void *entries) {
    if (stage < 4)
        g_palette[stage] = (const uint32_t *)entries;
}

static uint8_t Expand5(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static uint8_t Expand6(uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); }

// One level's texels as B, G, R, A rows, tightly packed, from the Xbox's data (unswizzled already for swizzled
// formats: linear rows pitch bytes apart, swizzled ones width texels).
static void ConvertRow(uint8_t *out, const uint8_t *in, uint32_t width, uint32_t format) {
    for (uint32_t x = 0; x < width; x++) {
        uint8_t *p = out + x * 4;
        switch (format) {
            case XFMT_A8R8G8B8: case XFMT_LIN_A8R8G8B8:
                memcpy(p, in + x * 4, 4);
                break;
            case XFMT_X8R8G8B8: case XFMT_LIN_X8R8G8B8:
                memcpy(p, in + x * 4, 3); p[3] = 255;
                break;
            case XFMT_R5G6B5: case XFMT_LIN_R5G6B5: {
                uint32_t v = in[x * 2] | (in[x * 2 + 1] << 8);
                p[0] = Expand5(v & 31); p[1] = Expand6((v >> 5) & 63); p[2] = Expand5(v >> 11); p[3] = 255;
                break;
            }
            case XFMT_A1R5G5B5: case XFMT_LIN_A1R5G5B5: case XFMT_X1R5G5B5: case XFMT_LIN_X1R5G5B5: {
                uint32_t v = in[x * 2] | (in[x * 2 + 1] << 8);
                bool opaque = format == XFMT_X1R5G5B5 || format == XFMT_LIN_X1R5G5B5;
                p[0] = Expand5(v & 31); p[1] = Expand5((v >> 5) & 31); p[2] = Expand5((v >> 10) & 31);
                p[3] = (opaque || (v & 0x8000)) ? 255 : 0;
                break;
            }
            case XFMT_A4R4G4B4: case XFMT_LIN_A4R4G4B4: {
                uint32_t v = in[x * 2] | (in[x * 2 + 1] << 8);
                p[0] = (uint8_t)((v & 15) * 17); p[1] = (uint8_t)(((v >> 4) & 15) * 17);
                p[2] = (uint8_t)(((v >> 8) & 15) * 17); p[3] = (uint8_t)((v >> 12) * 17);
                break;
            }
            case XFMT_L8: case XFMT_LIN_L8:
                p[0] = p[1] = p[2] = in[x]; p[3] = 255;
                break;
            case XFMT_A8: case XFMT_LIN_A8:   // white with the texture's coverage, as the NV2A samples it
                p[0] = p[1] = p[2] = 255; p[3] = in[x];
                break;
            case XFMT_A8L8: case XFMT_LIN_A8L8:
                p[0] = p[1] = p[2] = in[x * 2]; p[3] = in[x * 2 + 1];
                break;
            case XFMT_P8: {
                uint32_t c = g_uploadPalette != NULL ? g_uploadPalette[in[x]] : 0xFFFFFFFFu;
                memcpy(p, &c, 4);
                break;
            }
            default:
                p[0] = 255; p[1] = 0; p[2] = 255; p[3] = 255;
                break;
        }
    }
}

static bool FormatSupported(uint32_t format) {
    switch (format) {
        case XFMT_A8R8G8B8: case XFMT_LIN_A8R8G8B8: case XFMT_X8R8G8B8: case XFMT_LIN_X8R8G8B8:
        case XFMT_R5G6B5: case XFMT_LIN_R5G6B5: case XFMT_A1R5G5B5: case XFMT_LIN_A1R5G5B5:
        case XFMT_X1R5G5B5: case XFMT_LIN_X1R5G5B5: case XFMT_A4R4G4B4: case XFMT_LIN_A4R4G4B4:
        case XFMT_L8: case XFMT_LIN_L8: case XFMT_A8: case XFMT_LIN_A8: case XFMT_A8L8: case XFMT_LIN_A8L8:
        case XFMT_P8: case XFMT_DXT1: case XFMT_DXT3: case XFMT_DXT5: case XFMT_YUY2:
            return true;
        default:
            return false;
    }
}

static SDL_GPUTexture *CreateTexture(SDL_GPUTextureFormat format, uint32_t width, uint32_t height, uint32_t levels,
                                     SDL_GPUTextureUsageFlags usage, SDL_GPUTextureType type = SDL_GPU_TEXTURETYPE_2D) {
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = type;
    info.format = format;
    info.usage = usage;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = type == SDL_GPU_TEXTURETYPE_CUBE ? 6 : 1;
    info.num_levels = levels;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(g_device, &info);
    if (texture == NULL)
        GpuLog("[sdlgpu] texture %ux%u format %d: %s\n", width, height, (int)format, SDL_GetError());
    return texture;
}

static SDL_GPUTexture *GetHostTexture(const void *headerPtr) {
    const XboxPixelContainer *h = (const XboxPixelContainer*)headerPtr;
    HostTexture *t = FindOrAddHostTexture(headerPtr);
    if (t == NULL || g_device == NULL)
        return NULL;
    if (t->renderTarget)
        return t->texture;

    bool rebuild = (t->texture == NULL) || t->format != h->Format || t->size != h->Size || t->data != h->Data;
    bool refresh = g_streamsVolatile && h->Size != 0 && t->uploadedFrame != g_frameCount;
    if (!rebuild && !t->dirty && !refresh)
        return t->texture;

    if ((h->Data | 0x80000000u) == (BACKBUFFER_DATA_SENTINEL | 0x80000000u)) {
        CaptureBackBufferInto((void *)headerPtr);
        return t->texture;
    }

    uint32_t xboxFormat = (h->Format >> 8) & 0xFF;
    bool linear = (h->Size != 0);
    bool dxt = XboxFormatIsDxt(xboxFormat);
    uint32_t levels = linear ? 1 : ((h->Format >> 16) & 0xF);
    if (levels == 0) levels = 1;
    uint32_t width, height;
    XboxTextureSize(h, &width, &height);
    uint32_t pitch = linear ? ((h->Size >> 24) + 1) * 64 : 0;
    if (!FormatSupported(xboxFormat)) {
        static uint32_t warned[64]; static int warnedCount = 0;
        bool seen = false;
        for (int i = 0; i < warnedCount; i++) if (warned[i] == xboxFormat) seen = true;
        if (!seen && warnedCount < 64) { warned[warnedCount++] = xboxFormat; GpuLog("[sdlgpu] texture format 0x%02x not handled yet (%ux%u)\n", xboxFormat, width, height); }
        return NULL;
    }
    bool compressed = dxt && g_bcSupported;
    // Draws of this batch read what was uploaded before, so new pixels in the same batch go to a new texture
    if (t->uploadedBatch == g_batch && t->texture != NULL)
        rebuild = true;

    if (rebuild) {
        if (t->texture != NULL) g_releaseAfterFlush.push_back(t->texture);
        SDL_GPUTextureFormat format = !compressed ? SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM
                                    : xboxFormat == XFMT_DXT1 ? SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM
                                    : xboxFormat == XFMT_DXT3 ? SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM
                                                              : SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
        t->texture = CreateTexture(format, width, height, levels, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if (t->texture == NULL)
            return NULL;
        t->format = h->Format; t->size = h->Size; t->data = h->Data;
        t->width = width; t->height = height;
    }

    g_statTextureUploads++;
    const uint8_t *src = XboxDataPointer(h->Data);
    uint32_t bpp = XboxFormatBitsPerPixel(xboxFormat);
    static std::vector<uint8_t> scratch;
    for (uint32_t level = 0; level < levels; level++) {
        uint32_t lw = width >> level, lh = height >> level;
        if (lw == 0) lw = 1;
        if (lh == 0) lh = 1;
        if (dxt) {
            uint32_t bw = (lw + 3) / 4, bh = (lh + 3) / 4;
            uint32_t blockBytes = (xboxFormat == XFMT_DXT1) ? 8 : 16;
            if (compressed) {
                uint32_t bytes = bw * bh * blockBytes;
                uint32_t offset = UploadReserve(bytes);
                memcpy(g_uploadMapped + offset, src, bytes);
                TextureUpload u = { t->texture, level, 0, lw, lh, offset, bw * 4 };
                g_textureUploads.push_back(u);
            } else {
                uint32_t offset = UploadReserve(lw * lh * 4);
                uint8_t *out = g_uploadMapped + offset;
                for (uint32_t by = 0; by < bh; by++) {
                    for (uint32_t bx = 0; bx < bw; bx++) {
                        uint8_t texels[16][4];
                        DecodeDxtBlock(src + (by * bw + bx) * blockBytes, xboxFormat, texels);
                        for (int i = 0; i < 16; i++) {
                            uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                            if (x < lw && y < lh) memcpy(out + (y * lw + x) * 4, texels[i], 4);
                        }
                    }
                }
                TextureUpload u = { t->texture, level, 0, lw, lh, offset, lw };
                g_textureUploads.push_back(u);
            }
            src += (size_t)bw * bh * blockBytes;
            continue;
        }
        uint32_t offset = UploadReserve(lw * lh * 4);
        uint8_t *out = g_uploadMapped + offset;
        if (xboxFormat == XFMT_YUY2) {
            for (uint32_t r = 0; r < lh; r++)
                ConvertYuy2Row((uint32_t *)(out + (size_t)r * lw * 4), src + (size_t)r * pitch, lw);
        } else if (linear) {
            for (uint32_t r = 0; r < lh; r++)
                ConvertRow(out + (size_t)r * lw * 4, src + (size_t)r * pitch, lw, xboxFormat);
        } else {
            uint32_t bytesPerPixel = bpp / 8;
            size_t levelBytes = (size_t)lw * lh * bytesPerPixel;
            if (scratch.size() < levelBytes) scratch.resize(levelBytes);
            Unswizzle(scratch.data(), src, lw, lh, bytesPerPixel);
            for (uint32_t r = 0; r < lh; r++)
                ConvertRow(out + (size_t)r * lw * 4, scratch.data() + (size_t)r * lw * bytesPerPixel, lw, xboxFormat);
            src += levelBytes;
        }
        TextureUpload u = { t->texture, level, 0, lw, lh, offset, lw };
        g_textureUploads.push_back(u);
    }
    t->dirty = false;
    t->uploadedFrame = g_frameCount;
    t->uploadedBatch = g_batch;
    return t->texture;
}

// ---------------------------------------------------------------------------------------------------------------
// Device, targets and render scale (RenderWidth/RenderHeight, as d3d9Backend.cpp describes them).
// ---------------------------------------------------------------------------------------------------------------

struct Target { SDL_GPUTexture *colour, *depth; uint32_t width, height; bool opaque; };
static Target g_backBuffer, g_currentTarget;
static SDL_GPUTextureFormat g_depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
static bool g_reversedDepth = false;
static uint32_t g_gameWidth = 640, g_gameHeight = 480;
static float g_renderScaleX = 1.0f, g_renderScaleY = 1.0f;
static bool g_renderScaled = false;
static bool g_targetIsBackBuffer = true;
static uint32_t g_xboxSamplesPerPixel = 1;
static SDL_GPUTexture *g_readBackScaled = NULL;
static inline bool ScalingTarget(void) { return g_renderScaled && g_targetIsBackBuffer; }

#ifdef NF_ANTIALIASING
enum AntiAliasingMethod { AA_OFF = 0, AA_FXAA = 1, AA_SMAA = 2 };
static int g_antiAliasing = AA_FXAA;
static bool g_aaSceneDrawn = false, g_aaApplied = false;
static void ApplyAntiAliasing(void);
static void AntiAliasBeforeOverlay(void) {
    if (g_antiAliasing == AA_OFF || g_aaApplied || !g_aaSceneDrawn || !g_targetIsBackBuffer || g_device == NULL)
        return;
    g_aaApplied = true;
    ApplyAntiAliasing();
}
#define AA_NOTE_SCENE_DRAW() do { if (g_targetIsBackBuffer) g_aaSceneDrawn = true; } while (0)
#define AA_BEFORE_OVERLAY() AntiAliasBeforeOverlay()
#else
#define AA_NOTE_SCENE_DRAW() ((void)0)
#define AA_BEFORE_OVERLAY() ((void)0)
#endif

struct DepthForSize { uint32_t width, height; SDL_GPUTexture *texture; };
static std::vector<DepthForSize> g_depthTextures;

static SDL_GPUTexture *GetDepthFor(uint32_t width, uint32_t height) {
    for (size_t i = 0; i < g_depthTextures.size(); i++)
        if (g_depthTextures[i].width == width && g_depthTextures[i].height == height)
            return g_depthTextures[i].texture;
    SDL_GPUTexture *texture = CreateTexture(g_depthFormat, width, height, 1, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    if (texture == NULL)
        return NULL;
    DepthForSize d = { width, height, texture };
    g_depthTextures.push_back(d);
    SDL_FColor none = { 0, 0, 0, 0 };
    ClearPass(NULL, texture, false, none, true, g_reversedDepth ? 0.0f : 1.0f);
    return texture;
}

static void SizeWindowToBackBuffer(uint32_t width, uint32_t height) {
    GfxHost_SizeWindow(width, height);
}

static void SDLCALL ClaimWindow(void *data) {
    bool *ok = (bool *)data;
    *ok = SDL_ClaimWindowForGPUDevice(g_device, g_window);
    if (*ok) {
        SDL_GPUPresentMode mode = SDL_WindowSupportsGPUPresentMode(g_device, g_window, SDL_GPU_PRESENTMODE_IMMEDIATE)
                                  ? SDL_GPU_PRESENTMODE_IMMEDIATE : SDL_GPU_PRESENTMODE_VSYNC;
        SDL_SetGPUSwapchainParameters(g_device, g_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
    }
}

// The back buffer into the window's swapchain, on the window's thread, which owns the swapchain.
static void SDLCALL PresentOnWindowThread(void *data) {
    (void)data;
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g_device);
    if (cb == NULL)
        return;
    SDL_GPUTexture *swapchain = NULL;
    Uint32 width = 0, height = 0;
    if (SDL_WaitAndAcquireGPUSwapchainTexture(cb, g_window, &swapchain, &width, &height) && swapchain != NULL) {
        SDL_GPUBlitInfo blit;
        memset(&blit, 0, sizeof(blit));
        blit.source.texture = g_backBuffer.colour;
        blit.source.w = g_backBuffer.width;
        blit.source.h = g_backBuffer.height;
        blit.destination.texture = swapchain;
        blit.destination.w = width;
        blit.destination.h = height;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(cb, &blit);
    }
    SDL_SubmitGPUCommandBuffer(cb);
}

static void InitPinnedConstants(void);
static void ApplyDefaultRenderState(void);
static SDL_GPUTexture *MakeConstantTexture(SDL_GPUTextureType type) {
    SDL_GPUTexture *texture = CreateTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, 1, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER, type);
    if (texture == NULL)
        return NULL;
    uint32_t faces = type == SDL_GPU_TEXTURETYPE_CUBE ? 6 : 1;
    for (uint32_t f = 0; f < faces; f++) {
        uint32_t offset = UploadReserve(4);
        static const uint8_t opaqueBlack[4] = { 0, 0, 0, 255 };
        memcpy(g_uploadMapped + offset, opaqueBlack, 4);
        TextureUpload u = { texture, 0, f, 1, 1, offset, 1 };
        g_textureUploads.push_back(u);
    }
    return texture;
}

uint32_t D3D9_CreateDevice(uint32_t adapter, uint32_t deviceType, void *hFocusWindow, uint32_t behaviorFlags,
                           void *pPresentationParameters, void **ppDevice) {
    (void)adapter; (void)deviceType; (void)hFocusWindow; (void)behaviorFlags;
    const uint32_t *xboxParams = (const uint32_t*)pPresentationParameters;
    uint32_t width = xboxParams[0], height = xboxParams[1];

    g_gameWidth = width; g_gameHeight = height;
    g_renderScaled = false; g_renderScaleX = g_renderScaleY = 1.0f; g_targetIsBackBuffer = true;
    {
        uint32_t multiSample = xboxParams[4];
        uint32_t across = (multiSample >> 4) & 0xF, down = multiSample & 0xF;
        g_xboxSamplesPerPixel = (across != 0 && down != 0) ? across * down : 1;
    }
    int renderWidth = SettingInt("RenderWidth", 0), renderHeight = SettingInt("RenderHeight", 0);
    if (renderWidth > 0 && renderHeight > 0 && ((uint32_t)renderWidth != width || (uint32_t)renderHeight != height)) {
        g_renderScaled = true;
        g_renderScaleX = (float)renderWidth / (float)width;
        g_renderScaleY = (float)renderHeight / (float)height;
        GpuLog("[sdlgpu] rendering the game's %ux%u at %dx%d (RenderWidth/RenderHeight in settings.ini)\n",
               width, height, renderWidth, renderHeight);
        width = (uint32_t)renderWidth;
        height = (uint32_t)renderHeight;
    }
#ifdef NF_ANTIALIASING
    g_antiAliasing = SettingInt("AntiAliasing", AA_FXAA);
    if (g_antiAliasing != AA_OFF && g_antiAliasing != AA_FXAA) {
        GpuLog("[sdlgpu] AntiAliasing=%d: only 0 (off) and 1 (FXAA) are done - no anti-aliasing\n", g_antiAliasing);
        g_antiAliasing = AA_OFF;
    }
#endif
    if (g_xboxTextureStateTable == 0 || g_xboxRenderStateTable == 0)
        GpuLog("[sdlgpu] the D3D8 state table addresses were never set - see g_xboxTextureStateTable\n");

    const char *debug = getenv("NIGHTFIRE_GPUDEBUG");
    g_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, debug != NULL && debug[0] == '1',
                                   NULL);
    if (g_device == NULL) {
        GpuLog("[sdlgpu] SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        return 0x8876086Cu;   // D3DERR_INVALIDCALL
    }
    g_shadersAsMsl = (SDL_GetGPUShaderFormats(g_device) & SDL_GPU_SHADERFORMAT_MSL) != 0;
    GpuLog("[sdlgpu] GPU device on SDL's %s driver, shaders as %s\n", SDL_GetGPUDeviceDriver(g_device),
           g_shadersAsMsl ? "MSL" : "SPIR-V");

    if (GfxHost_PresentEvery() == 0) {
        g_window = (SDL_Window *)GfxHost_SdlWindow();
        SizeWindowToBackBuffer(width, height);
        bool claimed = false;
        if (g_window != NULL)
            SDL_RunOnMainThread(ClaimWindow, &claimed, true);
        if (!claimed) {
            GpuLog("[sdlgpu] the window could not be claimed (%s): rendering offscreen\n", SDL_GetError());
            g_window = NULL;
        }
    }

    g_depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    g_reversedDepth = true;
    if (!SDL_GPUTextureSupportsFormat(g_device, g_depthFormat, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        g_depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
        g_reversedDepth = false;
    }
    g_bcSupported = SDL_GPUTextureSupportsFormat(g_device, SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                                 SDL_GPU_TEXTUREUSAGE_SAMPLER) &&
                    SDL_GPUTextureSupportsFormat(g_device, SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                                 SDL_GPU_TEXTUREUSAGE_SAMPLER);

    SDL_GPUBufferCreateInfo ringInfo = { SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX, RING_BYTES, 0 };
    g_ring = SDL_CreateGPUBuffer(g_device, &ringInfo);
    SDL_GPUTransferBufferCreateInfo ringTransfer = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, RING_BYTES, 0 };
    g_ringTransfer = SDL_CreateGPUTransferBuffer(g_device, &ringTransfer);
    SDL_GPUTransferBufferCreateInfo uploadTransfer = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, UPLOAD_BYTES, 0 };
    g_uploadTransfer = SDL_CreateGPUTransferBuffer(g_device, &uploadTransfer);
    if (g_ring == NULL || g_ringTransfer == NULL || g_uploadTransfer == NULL) {
        GpuLog("[sdlgpu] buffers: %s\n", SDL_GetError());
        return 0x8876086Cu;
    }
    MapTransferBuffers();

    g_backBuffer.colour = CreateTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, width, height, 1,
                                        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    g_backBuffer.width = width;
    g_backBuffer.height = height;
    g_backBuffer.opaque = true;   // X8R8G8B8: alpha reads as 1
    g_backBuffer.depth = GetDepthFor(width, height);
    if (g_backBuffer.colour == NULL || g_backBuffer.depth == NULL)
        return 0x8876086Cu;
    SDL_FColor black = { 0, 0, 0, 1 };
    ClearPass(g_backBuffer.colour, g_backBuffer.depth, true, black, true, g_reversedDepth ? 0.0f : 1.0f);
    g_currentTarget = g_backBuffer;
    g_blackTexture = MakeConstantTexture(SDL_GPU_TEXTURETYPE_2D);
    g_blackCube = MakeConstantTexture(SDL_GPU_TEXTURETYPE_CUBE);

    HostQueryPerformanceFrequency(&g_qpcFrequency);
    HostRaiseTimerResolution();
    int fpsOverride = GfxHost_FpsOverride();
    g_targetFrameRate = (fpsOverride > 0) ? fpsOverride : (int)xboxParams[11];
    GpuLog("[sdlgpu] device ready (%ux%u back buffer, %s depth, %s, %s, paced to %d Hz)\n", width, height,
           g_reversedDepth ? "32-bit float reversed" : "24-bit fixed", g_bcSupported ? "BC textures" : "DXT decoded",
           g_window != NULL ? "presenting" : "offscreen", g_targetFrameRate);
    InitPinnedConstants();
    ApplyDefaultRenderState();
    static uint32_t deviceToken = 0x53444C47u;   // 'SDLG': the game only checks the pointer is not null
    *ppDevice = &deviceToken;
    return 0;
}

uint32_t D3D9_ArePushBuffersSupported(uint32_t unused) {
    (void)unused;
    return 1;
}

void D3D9_SetPushBufferSize(uint32_t pushBufferSize, uint32_t kickOffSize) {
    (void)pushBufferSize; (void)kickOffSize;
}

// ---------------------------------------------------------------------------------------------------------------
// Render state, as D3D9 kept it: its defaults, the game's values mapped as d3d9Backend.cpp maps them.
// ---------------------------------------------------------------------------------------------------------------

struct RenderState {
    bool alphaTest, blend, zEnable, zWrite, fogEnable;
    uint8_t alphaFunc, alphaRef, zFunc;              // D3DCMP_* (SDL's compare ops have the same numbers)
    SDL_GPUBlendFactor src, dst;
    SDL_GPUBlendOp op;
    uint8_t writeMask;                               // SDL_GPU_COLORCOMPONENT_*
    uint8_t cull;                                    // D3DCULL_*
    float depthBias;
};
static RenderState g_state;

static void ApplyDefaultRenderState(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_state.zEnable = true;
    g_state.zWrite = true;
    g_state.zFunc = 4;        // LESSEQUAL, unreversed until the game sets one: D3D9's default
    g_state.alphaFunc = 8;    // ALWAYS
    g_state.src = SDL_GPU_BLENDFACTOR_ONE;
    g_state.dst = SDL_GPU_BLENDFACTOR_ZERO;
    g_state.op = SDL_GPU_BLENDOP_ADD;
    g_state.writeMask = 0xF;
    g_state.cull = 3;         // CCW
}

static uint8_t GlCompare(uint32_t gl) { return (gl >= 0x200 && gl <= 0x207) ? (uint8_t)(gl - 0x200 + 1) : 8; }
static uint8_t ReverseCompareIfNeeded(uint8_t f) {
    if (!g_reversedDepth) return f;
    switch (f) {
        case 2: return 5;   // LESS -> GREATER
        case 4: return 7;   // LESSEQUAL -> GREATEREQUAL
        case 5: return 2;
        case 7: return 4;
        default: return f;
    }
}
static SDL_GPUBlendFactor GlBlend(uint32_t gl) {
    switch (gl) {
        case 0: return SDL_GPU_BLENDFACTOR_ZERO;
        case 1: return SDL_GPU_BLENDFACTOR_ONE;
        case 0x300: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
        case 0x301: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case 0x302: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        case 0x303: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x304: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
        case 0x305: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        case 0x306: return SDL_GPU_BLENDFACTOR_DST_COLOR;
        case 0x307: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
        case 0x308: return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
        default: return SDL_GPU_BLENDFACTOR_ONE;
    }
}
static SDL_GPUBlendOp GlBlendOp(uint32_t gl) {
    switch (gl) {
        case 0x8007: return SDL_GPU_BLENDOP_MIN;
        case 0x8008: return SDL_GPU_BLENDOP_MAX;
        case 0x800a: return SDL_GPU_BLENDOP_SUBTRACT;
        case 0x800b: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        default: return SDL_GPU_BLENDOP_ADD;
    }
}

void D3D9_SetRenderStateSimple(uint32_t nv2aMethod, uint32_t value) {
    switch (nv2aMethod & 0xFFFF) {
        case 0x300: g_state.alphaTest = value != 0; break;
        case 0x304: g_state.blend = value != 0; break;
        case 0x310: break;   // dithering
        case 0x33c: g_state.alphaFunc = GlCompare(value); break;
        case 0x340: g_state.alphaRef = (uint8_t)(value & 0xFF); break;
        case 0x344: g_state.src = GlBlend(value); break;
        case 0x348: g_state.dst = GlBlend(value); break;
        case 0x350: g_state.op = GlBlendOp(value); break;
        case 0x354: g_state.zFunc = ReverseCompareIfNeeded(GlCompare(value)); break;
        case 0x358: {   // NV097_SET_COLOR_MASK: one byte per channel, A R G B from the top
            uint8_t mask = 0;
            if (value & 0x00FF0000) mask |= SDL_GPU_COLORCOMPONENT_R;
            if (value & 0x0000FF00) mask |= SDL_GPU_COLORCOMPONENT_G;
            if (value & 0x000000FF) mask |= SDL_GPU_COLORCOMPONENT_B;
            if (value & 0xFF000000) mask |= SDL_GPU_COLORCOMPONENT_A;
            g_state.writeMask = mask;
            break;
        }
        case 0x35c: g_state.zWrite = value != 0; break;
        default: {
            static uint32_t warned[16]; static int warnedCount = 0;
            bool seen = false;
            for (int i = 0; i < warnedCount; i++) if (warned[i] == nv2aMethod) seen = true;
            if (!seen && warnedCount < 16) { warned[warnedCount++] = nv2aMethod; GpuLog("[sdlgpu] unhandled NV2A render state method 0x%x = 0x%x\n", nv2aMethod, value); }
            break;
        }
    }
}

void D3D9_SetCullMode(int xboxCullMode) {
    g_state.cull = (xboxCullMode == 0x901) ? 3 : (xboxCullMode == 0x900) ? 2 : 1;   // CCW, CW, NONE
}
static uint32_t g_fogColour = 0;
void D3D9_SetFogColor(uint32_t colour) { g_fogColour = colour; }
void D3D9_SetZBias(int zBias) { g_state.depthBias = (float)zBias * (g_reversedDepth ? 0.00001f : -0.00001f); }
void D3D9_SetZEnable(uint32_t value) { g_state.zEnable = value != 0; }
void D3D9_SetNormalizeNormals(uint32_t value) { (void)value; }
void D3D9_SetVertexBlend(uint32_t value) { (void)value; }
void D3D9_SetShaderConstantMode(uint32_t value) { (void)value; }
void D3D9_SetYuvEnable(uint32_t enable) { (void)enable; }

static float g_depthClipNear = 0.0f, g_depthClipFar = 16777215.0f;
static bool g_depthClipPlanesSet = false;
void D3D9_SetDepthClipPlanes(uint32_t p1, uint32_t p2, uint32_t p3) {
    (void)p3;
    memcpy(&g_depthClipNear, &p1, 4);
    memcpy(&g_depthClipFar, &p2, 4);
    g_depthClipPlanesSet = true;
}

static SDL_GPUViewport g_viewport;

void D3D9_SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float minZ, float maxZ) {
    if (ScalingTarget()) {
        uint32_t right = (uint32_t)((float)(x + width) * g_renderScaleX + 0.5f);
        uint32_t bottom = (uint32_t)((float)(y + height) * g_renderScaleY + 0.5f);
        x = (uint32_t)((float)x * g_renderScaleX + 0.5f);
        y = (uint32_t)((float)y * g_renderScaleY + 0.5f);
        if (right > g_currentTarget.width) right = g_currentTarget.width;
        if (bottom > g_currentTarget.height) bottom = g_currentTarget.height;
        width = right > x ? right - x : 0;
        height = bottom > y ? bottom - y : 0;
    }
    g_viewport.x = (float)x;
    g_viewport.y = (float)y;
    g_viewport.w = (float)width;
    g_viewport.h = (float)height;
    g_viewport.min_depth = minZ;
    g_viewport.max_depth = maxZ;
}

void D3D9_SetGammaRamp(uint32_t flags, void *pRamp) { (void)flags; (void)pRamp; }

// ---------------------------------------------------------------------------------------------------------------
// Texture stages and samplers.
// ---------------------------------------------------------------------------------------------------------------

static const void *g_boundTexture[4];
static uint32_t g_borderColour[4];
static float g_texCoordScale[4][2] = { { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 } };

void D3D9_SetTextureBorderColor(uint32_t stage, uint32_t colour) { if (stage < 4) g_borderColour[stage] = colour; }
void D3D9_SetTexture(uint32_t stage, void *pTexture) { if (stage < 4) g_boundTexture[stage] = pTexture; }

// SDL has no border colour: D3DTADDRESS_BORDER (4) clamps to the edge instead.
static SDL_GPUSamplerAddressMode XboxAddress(uint32_t v) {
    switch (v) {
        case 2: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        case 3: case 4: case 5: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        default: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    }
}

static std::map<std::string, SDL_GPUSampler *> g_samplers;

// The sampler for one Xbox stage's state (D3DTEXF_NONE as the mip filter: no mipmapping).
static SDL_GPUSampler *SamplerFor(uint32_t addressU, uint32_t addressV, uint32_t magFilter, uint32_t minFilter,
                                  uint32_t mipFilter, uint32_t lodBias) {
    SDL_GPUSamplerCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.mag_filter = magFilter == 1 ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    info.min_filter = minFilter == 1 ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    if (magFilter == 0) info.mag_filter = SDL_GPU_FILTER_NEAREST;
    if (minFilter == 0) info.min_filter = SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = mipFilter == 2 || mipFilter == 3 ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = XboxAddress(addressU);
    info.address_mode_v = XboxAddress(addressV);
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    memcpy(&info.mip_lod_bias, &lodBias, 4);
    info.min_lod = 0.0f;
    info.max_lod = mipFilter == 0 ? 0.0f : 1000.0f;
    std::string key((const char *)&info, sizeof(info));
    std::map<std::string, SDL_GPUSampler *>::iterator found = g_samplers.find(key);
    if (found != g_samplers.end())
        return found->second;
    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(g_device, &info);
    g_samplers[key] = sampler;
    return sampler;
}

static SDL_GPUSampler *StageSampler(uint32_t s) {
    return SamplerFor(XBOX_TEXTURE_STATE(s, XTSS_ADDRESSU), XBOX_TEXTURE_STATE(s, XTSS_ADDRESSV),
                      XBOX_TEXTURE_STATE(s, XTSS_MAGFILTER), XBOX_TEXTURE_STATE(s, XTSS_MINFILTER),
                      XBOX_TEXTURE_STATE(s, XTSS_MIPFILTER), XBOX_TEXTURE_STATE(s, XTSS_MIPMAPLODBIAS));
}

// ---------------------------------------------------------------------------------------------------------------
// Pixel shaders: the NV2A combiner definitions translated to SPIR-V on first draw (d3d9Backend.cpp's handling).
// ---------------------------------------------------------------------------------------------------------------
#define PS_HANDLE_TAG 0x50530000u

struct TranslatedPixelShader {
    uint32_t def[60];
    Nv2aPixelShaderInfo info;
    SDL_GPUShader *shader;
    uint32_t samplers, cubeSamplers;
    bool attempted, failed;
};
static TranslatedPixelShader g_pixelShaders[512];
static int g_pixelShaderCount = 0;
static int g_currentPixelShader = -1;
static float g_pixelShaderFactors[NV2A_PS_FACTOR_COUNT][4];

static void DumpPixelShader(int index, const TranslatedPixelShader *ps, const char *result) {
    static FILE *f = NULL;
    static bool opened = false;
    if (!opened) { opened = true; f = fopen("d3d9_pixel_shaders.log", "w"); }
    if (f == NULL)
        return;
    fprintf(f, "==== pixel shader %d at %p\n", index, (const void*)ps->def);
    for (int i = 0; i < 60; i++) fprintf(f, "%s%08x", (i % 8) ? " " : "\n  ", ps->def[i]);
    fprintf(f, "\n%s\n", result);
    fflush(f);
}

static void BuildPixelShader(TranslatedPixelShader *ps, int index) {
    ps->attempted = true;
    ps->failed = true;
    std::vector<uint32_t> words;
    ShaderSpirvOptions options;
    memset(&options, 0, sizeof(options));
    options.hostPixelEnd = true;
    ShaderSpirvInfo spirvInfo;
    char error[256];
    if (!Nv2aPixelShader_TranslateSpirv(ps->def, options, &words, &ps->info, &spirvInfo, error, sizeof(error))) {
        GpuLog("[sdlgpu] pixel shader %d: translation failed: %s\n", index, error);
        DumpPixelShader(index, ps, "(translation failed)");
        return;
    }
    DumpPixelShader(index, ps, "(translated)");
    char name[32];
    snprintf(name, sizeof(name), "ps_%d", index);
    ps->samplers = spirvInfo.samplers;
    ps->cubeSamplers = spirvInfo.cubeSamplers;
    ps->shader = CreateShader(name, words, true, SamplerCount(spirvInfo.samplers));
    if (ps->shader == NULL)
        return;
    GpuLog("[sdlgpu] pixel shader %d translated: %u stages, texture modes %u/%u/%u/%u%s\n", index, ps->info.stageCount,
           ps->info.textureMode[0], ps->info.textureMode[1], ps->info.textureMode[2], ps->info.textureMode[3],
           ps->info.fogByHost ? ", fog by the host" : "");
    ps->failed = false;
}

uint32_t D3D9_CreatePixelShader(const void *definition, uint32_t *handleOut) {
    if (handleOut == NULL || definition == NULL)
        return 0x8876086Cu;
    if (g_pixelShaderCount >= (int)(sizeof(g_pixelShaders) / sizeof(g_pixelShaders[0])))
        return 0x8876086Cu;
    TranslatedPixelShader *ps = &g_pixelShaders[g_pixelShaderCount];
    memset(ps, 0, sizeof(*ps));
    memcpy(ps->def, definition, sizeof(ps->def));
    *handleOut = PS_HANDLE_TAG | (uint32_t)g_pixelShaderCount;
    g_pixelShaderCount++;
    return 0;
}

void D3D9_SetPixelShader(uint32_t handle) {
    g_currentPixelShader = ((handle & 0xFFFF0000u) == PS_HANDLE_TAG && (int)(handle & 0xFFFF) < g_pixelShaderCount)
                           ? (int)(handle & 0xFFFF) : -1;
    if (g_currentPixelShader >= 0)
        Nv2aPixelShader_LoadFactors(g_pixelShaders[g_currentPixelShader].def, g_pixelShaderFactors);
}

void D3D9_SetPixelShaderConstant(uint32_t reg, const float *values, uint32_t count) {
    if (g_currentPixelShader < 0)
        return;
    for (uint32_t i = 0; i < count && reg + i < 16; i++)
        Nv2aPixelShader_SetConstant(g_pixelShaders[g_currentPixelShader].def, reg + i, values + i * 4, g_pixelShaderFactors);
}

void D3D9_DeletePixelShader(uint32_t handle) { (void)handle; }

static TranslatedPixelShader *CurrentPixelShaderForDraw(void) {
    if (g_currentPixelShader < 0 || g_currentPixelShader >= g_pixelShaderCount)
        return NULL;
    TranslatedPixelShader *ps = &g_pixelShaders[g_currentPixelShader];
    if (!ps->attempted)
        BuildPixelShader(ps, g_currentPixelShader);
    return ps->failed ? NULL : ps;
}

// The fixed-function programs, by stage state.
static std::map<std::string, SDL_GPUShader *> g_fixedPrograms;
static std::map<std::string, uint32_t> g_fixedProgramSamplers;

static SDL_GPUShader *FixedProgram(const FixedPixelKey &key, uint32_t *samplerCount) {
    std::string k((const char *)&key, sizeof(key));
    std::map<std::string, SDL_GPUShader *>::iterator found = g_fixedPrograms.find(k);
    if (found != g_fixedPrograms.end()) {
        *samplerCount = g_fixedProgramSamplers[k];
        return found->second;
    }
    std::vector<uint32_t> words;
    ShaderSpirvInfo info;
    char error[256], name[32];
    SDL_GPUShader *shader = NULL;
    *samplerCount = 0;
    if (FixedFunction_PixelProgram(key, &words, &info, error, sizeof(error))) {
        snprintf(name, sizeof(name), "ff_%zu", g_fixedPrograms.size());
        *samplerCount = SamplerCount(info.samplers);
        shader = CreateShader(name, words, true, *samplerCount);
    } else {
        GpuLog("[sdlgpu] fixed-function program failed: %s\n", error);
    }
    g_fixedPrograms[k] = shader;
    g_fixedProgramSamplers[k] = *samplerCount;
    return shader;
}

// What a draw samples: the textures and samplers by host sampler, and the program reading them.
struct DrawTextures {
    SDL_GPUShader *pixelShader;
    uint32_t samplerCount;
    SDL_GPUTextureSamplerBinding samplers[4];
    bool fogByHost;
    const TranslatedPixelShader *translated;
};

static bool g_pointSpriteDraw = false;

// d3d9Backend.cpp's ApplyTextureStageState: a translated pixel shader addresses Xbox stages by number; otherwise
// stages 0, 1 and 3 that have a texture (and stage 0 always) are packed into host stages, or stage 3 alone for a
// point sprite. Also works out the coordinate scale linear textures need.
static SDL_GPUTexture *BindStage(uint32_t s, uint32_t coordSet) {
    g_uploadPalette = (s < 4) ? g_palette[s] : NULL;
    SDL_GPUTexture *texture = g_boundTexture[s] != NULL ? GetHostTexture(g_boundTexture[s]) : NULL;
    if (g_boundTexture[s] != NULL) {
        const XboxPixelContainer *header = (const XboxPixelContainer*)g_boundTexture[s];
        if (header->Size != 0 && coordSet < 4) {
            g_texCoordScale[coordSet][0] = 1.0f / (float)((header->Size & 0xFFF) + 1);
            g_texCoordScale[coordSet][1] = 1.0f / (float)(((header->Size >> 12) & 0xFFF) + 1);
        }
    }
    return texture != NULL ? texture : g_blackTexture;
}

static void ApplyTextureStages(bool shaderDraw, DrawTextures *out) {
    for (int t = 0; t < 4; t++) g_texCoordScale[t][0] = g_texCoordScale[t][1] = 1.0f;
    memset(out, 0, sizeof(*out));
    TranslatedPixelShader *ps = CurrentPixelShaderForDraw();
    if (ps != NULL) {
        out->pixelShader = ps->shader;
        out->samplerCount = SamplerCount(ps->samplers);
        for (uint32_t s = 0; s < out->samplerCount; s++) {
            bool cube = (ps->cubeSamplers >> s) & 1;
            if (ps->info.samplesStage[s] && g_boundTexture[s] != NULL && !cube) {
                out->samplers[s].texture = BindStage(s, s);
                out->samplers[s].sampler = StageSampler(s);
            } else {
                out->samplers[s].texture = cube ? g_blackCube : g_blackTexture;
                out->samplers[s].sampler = SamplerFor(1, 1, 2, 2, 0, 0);
            }
        }
        out->fogByHost = ps->info.fogByHost;
        out->translated = ps;
        return;
    }
    static const uint32_t xboxStages[3] = { 0, 1, 3 };
    static const uint32_t pointSpriteStages[1] = { 3 };
    bool pointSprite = g_pointSpriteDraw && g_boundTexture[3] != NULL;
    const uint32_t *stages = pointSprite ? pointSpriteStages : xboxStages;
    int stageCount = pointSprite ? 1 : 3;
    FixedPixelKey key;
    memset(&key, 0, sizeof(key));
    SDL_GPUTextureSamplerBinding bindings[4];
    uint32_t host = 0;
    for (int i = 0; i < stageCount; i++) {
        uint32_t s = stages[i];
        if (s != 0 && g_boundTexture[s] == NULL)
            continue;
        uint32_t h = host++;
        uint32_t tci = XBOX_TEXTURE_STATE(s, XTSS_TEXCOORDINDEX) & 0xFFFF;
        if (tci >= 4) tci = s;
        if (pointSprite)
            tci = 0;
        bindings[h].texture = BindStage(s, tci);
        bindings[h].sampler = StageSampler(s);
        FixedStage &stage = key.stages[h];
        uint32_t colorOp = XBOX_TEXTURE_STATE(s, XTSS_COLOROP), alphaOp = XBOX_TEXTURE_STATE(s, XTSS_ALPHAOP);
        stage.colourOp = (uint8_t)(colorOp == 0 ? 1 : colorOp);
        stage.colourArg[0] = (uint8_t)XBOX_TEXTURE_STATE(s, XTSS_COLORARG0);
        stage.colourArg[1] = (uint8_t)XBOX_TEXTURE_STATE(s, XTSS_COLORARG1);
        stage.colourArg[2] = (uint8_t)XBOX_TEXTURE_STATE(s, XTSS_COLORARG2);
        stage.alphaOp = (uint8_t)(alphaOp == 0 ? 1 : alphaOp);
        stage.alphaArg[0] = (uint8_t)XBOX_TEXTURE_STATE(s, XTSS_ALPHAARG0);
        stage.alphaArg[1] = (uint8_t)XBOX_TEXTURE_STATE(s, XTSS_ALPHAARG1);
        stage.alphaArg[2] = (uint8_t)XBOX_TEXTURE_STATE(s, XTSS_ALPHAARG2);
        stage.texCoord = (uint8_t)tci;
        stage.projected = shaderDraw && !pointSprite;
    }
    key.stageCount = (uint8_t)host;
    uint32_t samplerCount;
    out->pixelShader = FixedProgram(key, &samplerCount);
    out->samplerCount = samplerCount;
    for (uint32_t h = 0; h < samplerCount; h++)
        out->samplers[h] = bindings[h];
    out->fogByHost = true;
    out->translated = NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// Vertex shaders: NV2A programs to SPIR-V, with the Xbox declaration turned into SDL vertex attributes.
// ---------------------------------------------------------------------------------------------------------------
#define VS_HANDLE_TAG 0x56530000u

struct VertexElement { uint8_t stream, location, format; uint16_t offset; };
struct TranslatedVertexShader {
    const void *declaration, *function;
    SDL_GPUShader *shader, *spriteShader;
    VertexElement elements[16];
    int elementCount;
    uint32_t streamsUsed;
    uint16_t declaredStride[16];
    uint32_t normPacked;
    uint8_t inputComponents[16], inputKind[16];
    // Metal reads an attribute only at a multiple of 4 bytes, in a vertex of a multiple of 4: a stream with an element
    // of 2 or 6 bytes (SHORT1, SHORT3), or one placed off that, is copied out to each element rounded up to 4 bytes
    // in turn - on every host, so that the Vulkan runs test it. The components it gains the program does not read.
    uint8_t elementBytes[16];
    uint16_t packedOffset[16];   // per element
    uint16_t packedStride[16];   // per stream
    uint32_t streamsUnaligned, streamsRepackLogged;
    bool attempted, failed;
};
static TranslatedVertexShader g_vertexShaders[512];
static int g_vertexShaderCount = 0;
static int g_currentVertexShader = -1;
static float g_vertexConstants[192][4];
static bool g_vertexConstantsChanged = true;

// An Xbox D3DVSDT type: its bytes, SDL's element format, how the program reads it, and its components.
static bool XboxVertexType(uint32_t type, uint32_t *bytes, SDL_GPUVertexElementFormat *format, uint8_t *kind,
                           bool *normPacked, uint32_t *components) {
    *normPacked = false;
    *kind = kSpirvFloat;
    *components = (type >> 4) & 0xF;
    switch (type) {
        case 0x12: *bytes = 4;  *format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT; return true;
        case 0x22: *bytes = 8;  *format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2; return true;
        case 0x32: *bytes = 12; *format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; return true;
        case 0x42: *bytes = 16; *format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4; return true;
        case 0x40: *bytes = 4;  *format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; *kind = kSpirvColour; *components = 4; return true;
        case 0x16: *bytes = 4;  *format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4; *kind = kSpirvUnsigned; *normPacked = true; *components = 3; return true;
        case 0x15: *bytes = 2;  *format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT2; *kind = kSpirvSigned; return true;
        case 0x25: *bytes = 4;  *format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT2; *kind = kSpirvSigned; return true;
        case 0x35: *bytes = 6;  *format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT4; *kind = kSpirvSigned; return true;
        case 0x45: *bytes = 8;  *format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT4; *kind = kSpirvSigned; return true;
        case 0x11: *bytes = 2;  *format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM; return true;
        case 0x21: *bytes = 4;  *format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM; return true;
        case 0x41: *bytes = 8;  *format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM; return true;
        case 0x14: *bytes = 4;  *format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4; *kind = kSpirvUnsigned; return true;
        default: return false;
    }
}

static void DumpTranslatedShader(int index, const TranslatedVertexShader *vs, const char *result) {
    static FILE *f = NULL;
    static bool opened = false;
    if (!opened) { opened = true; f = fopen("d3d9_shaders.log", "w"); }
    if (f == NULL)
        return;
    fprintf(f, "==== shader %d: declaration %p function %p\n", index, vs->declaration, vs->function);
    fprintf(f, "decl:");
    for (const uint32_t *tok = (const uint32_t*)vs->declaration; ; tok++) {
        fprintf(f, " %08x", *tok);
        if (*tok == 0xFFFFFFFFu) break;
    }
    fprintf(f, "\nelements:");
    for (int i = 0; i < vs->elementCount; i++)
        fprintf(f, " [stream %u offset %u format %u location %u]", vs->elements[i].stream, vs->elements[i].offset,
                vs->elements[i].format, vs->elements[i].location);
    fprintf(f, "\nstreams 0x%03x\n", vs->streamsUsed);
    const uint8_t *fn = (const uint8_t*)vs->function;
    int count = fn[2];
    const uint32_t *words = (const uint32_t*)(fn + 8);
    fprintf(f, "microcode (%d instructions):\n", count);
    for (int i = 0; i < count; i++)
        fprintf(f, "  %2d: %08x %08x %08x\n", i, words[i * 4], words[i * 4 + 1], words[i * 4 + 2]);
    fputs(result, f);
    fputs("\n", f);
    fflush(f);
}

static SDL_GPUShader *TranslateVertexShader(TranslatedVertexShader *vs, int index, bool sprite) {
    std::vector<uint32_t> words;
    Nv2aVertexShaderInfo info;
    ShaderSpirvInfo spirvInfo;
    ShaderSpirvOptions options;
    memset(&options, 0, sizeof(options));
    memcpy(options.inputKind, vs->inputKind, 16);
    options.pointSprite = sprite;
    char error[256];
    if (!Nv2aVertexShader_TranslateSpirv((const uint8_t*)vs->function, vs->normPacked, vs->inputComponents, options,
                                         &words, &info, &spirvInfo, error, sizeof(error))) {
        GpuLog("[sdlgpu] shader %d: translation failed: %s\n", index, error);
        return NULL;
    }
    char name[32];
    snprintf(name, sizeof(name), "vs_%d%s", index, sprite ? "_sprite" : "");
    return CreateShader(name, words, false, 0);
}

static bool BuildVertexShader(TranslatedVertexShader *vs, int index) {
    vs->attempted = true;
    vs->failed = true;
    uint32_t stream = 0, offset = 0;
    vs->streamsUsed = 0;
    vs->streamsUnaligned = vs->streamsRepackLogged = 0;
    vs->elementCount = 0;
    vs->normPacked = 0;
    memset(vs->packedStride, 0, sizeof(vs->packedStride));
    memset(vs->inputComponents, 0, sizeof(vs->inputComponents));
    memset(vs->inputKind, kSpirvFloat, sizeof(vs->inputKind));
    for (const uint32_t *tok = (const uint32_t*)vs->declaration; *tok != 0xFFFFFFFFu; tok++) {
        if ((*tok & 0xF0000000u) == 0x20000000u) { stream = *tok & 0xF; offset = 0; continue; }
        if ((*tok & 0xF0000000u) != 0x40000000u) continue;
        uint32_t type = (*tok >> 16) & 0xFF, reg = *tok & 0xF;
        uint32_t bytes, components;
        SDL_GPUVertexElementFormat format;
        uint8_t kind;
        bool np;
        if (!XboxVertexType(type, &bytes, &format, &kind, &np, &components) || vs->elementCount >= 16) {
            GpuLog("[sdlgpu] shader %d: vertex type 0x%02x not handled\n", index, type);
            DumpTranslatedShader(index, vs, "(declaration not handled)");
            return false;
        }
        VertexElement e = { (uint8_t)stream, (uint8_t)reg, (uint8_t)format, (uint16_t)offset };
        vs->elementBytes[vs->elementCount] = (uint8_t)bytes;
        vs->packedOffset[vs->elementCount] = vs->packedStride[stream];
        vs->packedStride[stream] += (uint16_t)((bytes + 3) & ~3u);
        if (bytes % 4 != 0 || offset % 4 != 0)
            vs->streamsUnaligned |= 1u << stream;
        vs->elements[vs->elementCount++] = e;
        if (np) vs->normPacked |= 1u << reg;
        vs->inputComponents[reg] = (uint8_t)(components == 0 || components > 4 ? 4 : components);
        vs->inputKind[reg] = kind;
        vs->streamsUsed |= 1u << stream;
        offset += bytes;
        vs->declaredStride[stream] = (uint16_t)offset;
    }
    vs->shader = TranslateVertexShader(vs, index, false);
    DumpTranslatedShader(index, vs, vs->shader != NULL ? "(translated)" : "(translation failed)");
    if (vs->shader == NULL)
        return false;
    GpuLog("[sdlgpu] shader %d translated: streams 0x%03x\n", index, vs->streamsUsed);
    vs->failed = false;
    return true;
}

uint32_t D3D9_CreateVertexShader(const void *pDeclaration, const void *pFunction, void **pHandle, uint32_t usage) {
    (void)usage;
    if (g_vertexShaderCount >= (int)(sizeof(g_vertexShaders) / sizeof(g_vertexShaders[0])))
        return 0x8876086Cu;
    TranslatedVertexShader *vs = &g_vertexShaders[g_vertexShaderCount];
    memset(vs, 0, sizeof(*vs));
    vs->declaration = pDeclaration;
    vs->function = pFunction;
    *pHandle = (void*)(uintptr_t)(VS_HANDLE_TAG | (uint32_t)g_vertexShaderCount);
    g_vertexShaderCount++;
    return 0;
}

void D3D9_SetVertexShader(void *handle) {
    uint32_t h = (uint32_t)(uintptr_t)handle;
    g_currentVertexShader = ((h & 0xFFFF0000u) == VS_HANDLE_TAG) ? (int)(h & 0xFFFF) : -1;
}

static void StoreConstants(uint32_t index, const float *values, uint32_t count4) {
    for (uint32_t i = 0; i < count4; i++) {
        uint32_t k = index + i;
        if (k < 192 && k != 58 && k != 59)   // 58/59 are the pinned viewport constants (nv2aVertexShader.h)
            memcpy(g_vertexConstants[k], values + i * 4, 16);
    }
    g_vertexConstantsChanged = true;
}
void D3D9_SetVertexShaderConstant1(uint32_t constantIndex, float *pConstants) { StoreConstants(constantIndex, pConstants, 1); }
void D3D9_SetVertexShaderConstant4(uint32_t constantIndex, void *pMatrix) { StoreConstants(constantIndex, (const float*)pMatrix, 4); }
void D3D9_SetVertexShaderConstantNotInline(uint32_t constantIndex, void *pData, uint32_t countDwords) { StoreConstants(constantIndex, (const float*)pData, countDwords / 4); }

static void InitPinnedConstants(void) {
    static const float scale[4] = { 1, 1, 1, 1 }, offset[4] = { 0, 0, 0, 0 };
    memcpy(g_vertexConstants[58], scale, 16);
    memcpy(g_vertexConstants[59], offset, 16);
    g_vertexConstantsChanged = true;
}

// The vertex constant block for a draw: the game's 192, then the host's c200-c204 (nv2aVertexShader.h). c200.xy, the
// half-pixel offset, is zero (D3D10's pixel centres); .zw is 2 / the target's size, for point sprites and the
// pretransformed vertices.
static int VertexUniforms(void) {
    float host[5][4];
    memset(host, 0, sizeof(host));
    host[0][2] = 2.0f / (float)g_currentTarget.width;
    host[0][3] = 2.0f / (float)g_currentTarget.height;
    float range = (g_depthClipFar - g_depthClipNear) != 0.0f ? (g_depthClipFar - g_depthClipNear) : 1.0f;
    if (g_reversedDepth) { host[1][0] = -g_depthClipNear / range; host[1][1] = g_depthClipFar; }
    else                 { host[1][0] = g_depthClipFar / range;   host[1][1] = g_depthClipNear; }
    host[1][2] = g_depthClipPlanesSet ? 0.0f : 1.0f;
    host[1][3] = g_reversedDepth ? 1.0f : 0.0f;
    memcpy(&host[2][0], &XBOX_RENDER_STATE(XRS_FOGSTART), 4);
    memcpy(&host[2][1], &XBOX_RENDER_STATE(XRS_FOGEND), 4);
    memcpy(&host[2][2], &XBOX_RENDER_STATE(XRS_FOGDENSITY), 4);
    host[2][3] = (float)XBOX_RENDER_STATE(XRS_FOGTABLEMODE);
    host[3][0] = g_texCoordScale[0][0]; host[3][1] = g_texCoordScale[0][1];
    host[3][2] = g_texCoordScale[1][0]; host[3][3] = g_texCoordScale[1][1];
    host[4][0] = g_texCoordScale[2][0]; host[4][1] = g_texCoordScale[2][1];
    host[4][2] = g_texCoordScale[3][0]; host[4][3] = g_texCoordScale[3][1];
    const size_t blockFloats = kSpirvVertexConstants * 4;
    size_t count = g_vertexUniformSnapshots.size() / blockFloats;
    if (count > 0 && !g_vertexConstantsChanged &&
        memcmp(&g_vertexUniformSnapshots[(count - 1) * blockFloats + 200 * 4], host, sizeof(host)) == 0)
        return (int)(count - 1);
    g_vertexUniformSnapshots.resize((count + 1) * blockFloats, 0.0f);
    float *block = &g_vertexUniformSnapshots[count * blockFloats];
    memcpy(block, g_vertexConstants, sizeof(g_vertexConstants));
    memcpy(block + 200 * 4, host, sizeof(host));
    g_vertexConstantsChanged = false;
    return (int)count;
}

// The pixel constant block: a translated program's (Nv2aPixelShader_BuildConstants), then the host's end
// (shaderSpirv.h's kSpirvFogColour and kSpirvPixelState).
static int PixelUniforms(const TranslatedPixelShader *ps, bool fogByHost, bool visibilityMask) {
    float block[kSpirvPixelConstants][4];
    memset(block, 0, sizeof(block));
    if (ps != NULL) {
        uint32_t bumpEnv[4][6], colourSign[4];
        for (int s = 0; s < 4; s++) {
            for (int m = 0; m < 6; m++) bumpEnv[s][m] = XBOX_TEXTURE_STATE(s, XTSS_BUMPENVMAT00 + m);
            colourSign[s] = XBOX_TEXTURE_STATE(s, XTSS_COLORSIGN);
        }
        Nv2aPixelShader_BuildConstants(g_pixelShaderFactors, g_fogColour, bumpEnv, colourSign, block);
    }
    block[kSpirvFogColour][0] = ((g_fogColour >> 16) & 0xFF) / 255.0f;
    block[kSpirvFogColour][1] = ((g_fogColour >> 8) & 0xFF) / 255.0f;
    block[kSpirvFogColour][2] = (g_fogColour & 0xFF) / 255.0f;
    block[kSpirvPixelState][0] = fogByHost && XBOX_RENDER_STATE(XRS_FOGENABLE) != 0 ? 1.0f : 0.0f;
    block[kSpirvPixelState][1] = g_state.alphaTest ? (float)g_state.alphaFunc : 8.0f;
    block[kSpirvPixelState][2] = (float)g_state.alphaRef;
    block[kSpirvPixelState][3] = visibilityMask ? 1.0f : 0.0f;
    const size_t blockFloats = kSpirvPixelConstants * 4;
    size_t count = g_pixelUniformSnapshots.size() / blockFloats;
    if (count > 0 && memcmp(&g_pixelUniformSnapshots[(count - 1) * blockFloats], block, sizeof(block)) == 0)
        return (int)(count - 1);
    g_pixelUniformSnapshots.insert(g_pixelUniformSnapshots.end(), &block[0][0], &block[0][0] + blockFloats);
    return (int)count;
}

// ---------------------------------------------------------------------------------------------------------------
// Pipelines, cached by everything SDL bakes into one.
// ---------------------------------------------------------------------------------------------------------------

struct PipelineKey {
    SDL_GPUShader *vertexShader, *pixelShader;
    uint8_t primitive, cull, blend, src, dst, op, writeMask, depthTest, depthWrite, depthFunc, target, instanced;
    uint8_t attributeCount, bufferCount, pad[2];
    float depthBias;
    VertexElement attributes[16];   // stream holds the buffer slot here
    uint16_t pitch[16];
};
static std::map<std::string, SDL_GPUGraphicsPipeline *> g_pipelines;
enum { kTargetColour, kTargetMask };

static SDL_GPUGraphicsPipeline *PipelineFor(const PipelineKey &key) {
    std::string k((const char *)&key, sizeof(key));
    std::map<std::string, SDL_GPUGraphicsPipeline *>::iterator found = g_pipelines.find(k);
    if (found != g_pipelines.end())
        return found->second;
    SDL_GPUVertexBufferDescription buffers[16];
    SDL_GPUVertexAttribute attributes[16];
    for (uint32_t b = 0; b < key.bufferCount; b++) {
        buffers[b].slot = b;
        buffers[b].pitch = key.pitch[b];
        buffers[b].input_rate = key.instanced ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX;
        buffers[b].instance_step_rate = 0;
    }
    for (uint32_t a = 0; a < key.attributeCount; a++) {
        attributes[a].location = key.attributes[a].location;
        attributes[a].buffer_slot = key.attributes[a].stream;
        attributes[a].format = (SDL_GPUVertexElementFormat)key.attributes[a].format;
        attributes[a].offset = key.attributes[a].offset;
    }
    SDL_GPUColorTargetDescription colour;
    memset(&colour, 0, sizeof(colour));
    colour.format = key.target == kTargetMask ? SDL_GPU_TEXTUREFORMAT_R8_UNORM : SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    colour.blend_state.enable_blend = key.blend != 0;
    colour.blend_state.src_color_blendfactor = (SDL_GPUBlendFactor)key.src;
    colour.blend_state.dst_color_blendfactor = (SDL_GPUBlendFactor)key.dst;
    colour.blend_state.color_blend_op = (SDL_GPUBlendOp)key.op;
    colour.blend_state.src_alpha_blendfactor = (SDL_GPUBlendFactor)key.src;
    colour.blend_state.dst_alpha_blendfactor = (SDL_GPUBlendFactor)key.dst;
    colour.blend_state.alpha_blend_op = (SDL_GPUBlendOp)key.op;
    colour.blend_state.color_write_mask = key.writeMask;
    colour.blend_state.enable_color_write_mask = true;

    SDL_GPUGraphicsPipelineCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.vertex_shader = key.vertexShader;
    info.fragment_shader = key.pixelShader;
    info.vertex_input_state.vertex_buffer_descriptions = buffers;
    info.vertex_input_state.num_vertex_buffers = key.bufferCount;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = key.attributeCount;
    info.primitive_type = (SDL_GPUPrimitiveType)key.primitive;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    // D3D9 culls by the winding on screen; SDL's clockwise is the same as seen on the target
    info.rasterizer_state.cull_mode = key.cull == 2 ? SDL_GPU_CULLMODE_FRONT : key.cull == 3 ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
    info.rasterizer_state.enable_depth_bias = key.depthBias != 0.0f;
    info.rasterizer_state.depth_bias_constant_factor = key.depthBias * 16777216.0f;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.enable_depth_test = key.depthTest != 0;
    info.depth_stencil_state.enable_depth_write = key.depthWrite != 0;
    info.depth_stencil_state.compare_op = (SDL_GPUCompareOp)key.depthFunc;
    info.target_info.color_target_descriptions = &colour;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = g_depthFormat;
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(g_device, &info);
    if (pipeline == NULL)
        GpuLog("[sdlgpu] pipeline: %s\n", SDL_GetError());
    g_pipelines[k] = pipeline;
    g_statPipelines++;
    return pipeline;
}

// The render state a draw goes out with: the game's, with the target's opaque alpha kept.
static void StateKey(PipelineKey *key, bool depthTarget) {
    key->cull = g_state.cull;
    key->blend = g_state.blend;
    key->src = (uint8_t)g_state.src;
    key->dst = (uint8_t)g_state.dst;
    key->op = (uint8_t)g_state.op;
    key->writeMask = g_state.writeMask;
    if (g_currentTarget.opaque)
        key->writeMask &= (uint8_t)~SDL_GPU_COLORCOMPONENT_A;
    key->depthTest = depthTarget && g_state.zEnable;
    key->depthWrite = depthTarget && g_state.zEnable && g_state.zWrite;
    key->depthFunc = g_state.zFunc;
    key->depthBias = g_state.depthBias;
    key->target = kTargetColour;
}

// ---------------------------------------------------------------------------------------------------------------
// Render targets (d3d9Backend.cpp's: the character shadows' aux pass and blur, psiBlurScreen's capture).
// ---------------------------------------------------------------------------------------------------------------

static HostTexture *EnsureRenderTargetTexture(const void *headerPtr) {
    const XboxPixelContainer *h = (const XboxPixelContainer*)headerPtr;
    HostTexture *t = FindOrAddHostTexture(headerPtr);
    if (t == NULL || g_device == NULL)
        return NULL;
    if (t->renderTarget && t->texture != NULL && t->format == h->Format && t->size == h->Size)
        return t;
    ReleaseHostTexture(t);
    uint32_t width, height;
    XboxTextureSize(h, &width, &height);
    uint32_t xboxFormat = (h->Format >> 8) & 0xFF;
    t->texture = CreateTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, width, height, 1,
                               SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if (t->texture == NULL)
        return NULL;
    t->width = width;
    t->height = height;
    t->renderTarget = true;
    t->opaque = xboxFormat == XFMT_X8R8G8B8 || xboxFormat == XFMT_LIN_X8R8G8B8;
    t->dirty = false;
    t->format = h->Format; t->size = h->Size; t->data = h->Data;
    // Defined contents from the start, so that a run reads the same pixels each time
    SDL_FColor clear = { 0, 0, 0, t->opaque ? 1.0f : 0.0f };
    ClearPass(t->texture, GetDepthFor(width, height), true, clear, false, 0.0f);
    return t;
}

static void CaptureBackBufferInto(void *header) {
    HostTexture *t = EnsureRenderTargetTexture(header);
    if (t == NULL)
        return;
    SDL_GPUBlitInfo blit;
    memset(&blit, 0, sizeof(blit));
    blit.source.texture = g_backBuffer.colour;
    blit.source.w = g_backBuffer.width;
    blit.source.h = g_backBuffer.height;
    blit.destination.texture = t->texture;
    blit.destination.w = t->width;
    blit.destination.h = t->height;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = g_renderScaled ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    RecordBlit(blit);
}

void D3D9_SetRenderTarget(void *pRenderTarget, void *pDepthStencil) {
    (void)pDepthStencil;
    if (g_device == NULL)
        return;
    bool toBackBuffer = (pRenderTarget == NULL || pRenderTarget == &g_dummyBackBuffer || pRenderTarget == &g_dummyRenderTarget);
    if (!toBackBuffer) {
        const XboxSurface *surface = (const XboxSurface*)pRenderTarget;
        HostTexture *t = (surface->Parent != 0) ? EnsureRenderTargetTexture(PtrFrom32<const void>(surface->Parent)) : NULL;
        if (t != NULL) {
            g_currentTarget.colour = t->texture;
            g_currentTarget.depth = GetDepthFor(t->width, t->height);
            g_currentTarget.width = t->width;
            g_currentTarget.height = t->height;
            g_currentTarget.opaque = t->opaque;
            g_targetIsBackBuffer = false;
            return;
        }
        D3D9_BackendMissing("D3DDevice_SetRenderTarget(surface with no texture behind it)");
    }
    g_currentTarget = g_backBuffer;
    g_targetIsBackBuffer = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Visibility tests (d3d9Backend.cpp explains what the game does with them). SDL has no occlusion queries, so a
// test's draws are drawn a second time, first, into a mask of their own - depth-tested against the target's depth,
// writing nothing but the mask - whose pixels are counted once the batch has run.
// ---------------------------------------------------------------------------------------------------------------
#define VISIBILITY_INDICES 64
static bool g_visibilityOpen = false;
static SDL_GPUTexture *g_visibilityMask = NULL;
static uint32_t g_visibilityMaskWidth = 0, g_visibilityMaskHeight = 0;
struct VisibilityResult { int state; uint32_t batch, offset, width, height, count; };   // state: 0 none, 1 recorded, 2 counted
static VisibilityResult g_visibility[VISIBILITY_INDICES];

// The masks of the batch just run, counted from its downloads.
static void CountVisibility(uint32_t batch) {
    for (int i = 0; i < VISIBILITY_INDICES; i++) {
        VisibilityResult &v = g_visibility[i];
        if (v.state != 1 || v.batch != batch)
            continue;
        uint32_t count = 0;
        size_t bytes = (size_t)v.width * v.height;
        if (v.offset + bytes <= g_downloaded.size())
            for (size_t k = 0; k < bytes; k++) count += g_downloaded[v.offset + k] != 0;
        v.count = count;
        v.state = 2;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Draws.
// ---------------------------------------------------------------------------------------------------------------

// A draw's primitives as SDL takes them: lists and strips as they are; fans, quads and polygons through indices.
static bool XboxPrimitive(uint32_t xboxType, SDL_GPUPrimitiveType *type, bool *needsIndices) {
    *needsIndices = false;
    switch (xboxType) {
        case 1: *type = SDL_GPU_PRIMITIVETYPE_POINTLIST; return true;
        case 2: *type = SDL_GPU_PRIMITIVETYPE_LINELIST; return true;
        case 4: *type = SDL_GPU_PRIMITIVETYPE_LINESTRIP; return true;
        case 5: *type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; return true;
        case 6: case 9: *type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP; return true;   // a quad strip's order is a triangle strip's
        case 7: case 8: case 10: *type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; *needsIndices = true; return true;
        default: return false;
    }
}

// The triangle-list indices for a fan or polygon (0, i, i + 1) or a quad list (a, b, c, a, c, d), from the source
// indices or, without them, from first. Returns the count written.
static uint32_t ListIndices(uint16_t *dst, uint32_t xboxType, const uint16_t *source, uint32_t first, uint32_t count) {
    uint32_t out = 0;
    if (xboxType == 8) {
        for (uint32_t q = 0; q + 3 < count; q += 4) {
            uint16_t v[4];
            for (int k = 0; k < 4; k++) v[k] = source != NULL ? source[q + k] : (uint16_t)(first + q + k);
            dst[out++] = v[0]; dst[out++] = v[1]; dst[out++] = v[2];
            dst[out++] = v[0]; dst[out++] = v[2]; dst[out++] = v[3];
        }
    } else {
        for (uint32_t i = 1; i + 1 < count; i++) {
            dst[out++] = source != NULL ? source[0] : (uint16_t)first;
            dst[out++] = source != NULL ? source[i] : (uint16_t)(first + i);
            dst[out++] = source != NULL ? source[i + 1] : (uint16_t)(first + i + 1);
        }
    }
    return out;
}

static uint32_t ListIndexCount(uint32_t xboxType, uint32_t count) {
    if (xboxType == 8) return (count / 4) * 6;
    return count > 2 ? (count - 2) * 3 : 0;
}

// Opens a pass on the current target unless the last command is one draws can join.
static void EnsurePass(void) {
    if (!g_passOpen || g_passColour != g_currentTarget.colour || g_passDepth != g_currentTarget.depth)
        BeginPass(g_currentTarget.colour, g_currentTarget.depth);
}

static void RecordDraw(const DrawCommand &d, bool visibilityMask) {
    if (visibilityMask) {
        if (!g_passOpen || g_passColour != g_visibilityMask || g_passDepth != g_currentTarget.depth)
            BeginPass(g_visibilityMask, g_currentTarget.depth);
    } else {
        EnsurePass();
    }
    Command c = { kCommandDraw, (uint32_t)g_draws.size() };
    g_draws.push_back(d);
    g_commands.push_back(c);
    g_statDraws++;
}

// Records a draw, and its twin into the visibility mask first when a test is open.
static void SubmitDraw(DrawCommand d, PipelineKey key, const TranslatedPixelShader *ps, bool fogByHost) {
    d.viewport = g_viewport;
    d.pixelUniformBytes = kSpirvPixelConstants * 16;
    d.vertexUniforms = VertexUniforms();
    if (g_visibilityOpen && g_visibilityMask != NULL) {
        PipelineKey mask = key;
        mask.target = kTargetMask;
        mask.blend = 0;
        mask.writeMask = SDL_GPU_COLORCOMPONENT_R;
        mask.depthWrite = 0;
        DrawCommand m = d;
        m.pipeline = PipelineFor(mask);
        m.pixelUniforms = PixelUniforms(ps, fogByHost, true);
        if (m.pipeline != NULL)
            RecordDraw(m, true);
    }
    d.pipeline = PipelineFor(key);
    d.pixelUniforms = PixelUniforms(ps, fogByHost, false);
    if (d.pipeline != NULL)
        RecordDraw(d, false);
}

static const void *g_streams[16];
static int g_streamStrides[16];
static const void *g_indexBuffer = NULL;

void D3D9_SetStreamSource(int streamNumber, void *vertexBuffer, int stride) {
    if (streamNumber >= 0 && streamNumber < 16) { g_streams[streamNumber] = vertexBuffer; g_streamStrides[streamNumber] = stride; }
}
void D3D9_SetIndices(void *pIndexBuffer, uint32_t baseVertexIndex) { (void)baseVertexIndex; g_indexBuffer = pIndexBuffer; }

// A stream's vertices in the vertex shader's packed layout for it (TranslatedVertexShader::packedStride).
static void RepackStream(uint8_t *out, const uint8_t *in, uint32_t stride, uint32_t vertices,
                         const TranslatedVertexShader *vs, uint32_t stream) {
    uint32_t packedStride = vs->packedStride[stream];
    memset(out, 0, (size_t)vertices * packedStride);
    for (uint32_t v = 0; v < vertices; v++) {
        for (int e = 0; e < vs->elementCount; e++) {
            if (vs->elements[e].stream == stream)
                memcpy(out + (size_t)v * packedStride + vs->packedOffset[e],
                       in + (size_t)v * stride + vs->elements[e].offset, vs->elementBytes[e]);
        }
    }
}

// A programmable draw: the current vertex shader over the streams' vertices [first, limit), copied into the ring,
// with indices (already in the ring at indexOffset) or without. userData stands in for stream 0 (DrawVerticesUP).
static void ShaderDraw(uint32_t xboxType, uint32_t first, uint32_t limit, bool indexed, uint32_t indexOffset,
                       uint32_t count, const uint8_t *userData, uint32_t userStride) {
    if (g_currentVertexShader < 0 || g_currentVertexShader >= g_vertexShaderCount) {
        D3D9_BackendMissing("draw with no vertex shader selected");
        return;
    }
    TranslatedVertexShader *vs = &g_vertexShaders[g_currentVertexShader];
    if (!vs->attempted)
        BuildVertexShader(vs, g_currentVertexShader);
    if (vs->failed)
        return;
    bool sprite = xboxType == 1;
    if (sprite && vs->spriteShader == NULL)
        vs->spriteShader = TranslateVertexShader(vs, g_currentVertexShader, true);
    if (sprite && vs->spriteShader == NULL)
        return;
    DrawTextures textures;
    g_pointSpriteDraw = sprite;
    ApplyTextureStages(true, &textures);
    g_pointSpriteDraw = false;
    if (textures.pixelShader == NULL)
        return;

    PipelineKey key;
    memset(&key, 0, sizeof(key));
    key.vertexShader = sprite ? vs->spriteShader : vs->shader;
    key.pixelShader = textures.pixelShader;
    DrawCommand d;
    memset(&d, 0, sizeof(d));
    uint32_t vertices = limit - first;
    // The streams the declaration reads, each to the next buffer slot
    uint32_t slotOfStream[16];
    uint32_t streamsRepacked = 0;
    for (uint32_t s = 0; s < 16; s++) {
        if (!(vs->streamsUsed & (1u << s)))
            continue;
        uint32_t stride = userData != NULL && s == 0 ? userStride : g_streamStrides[s] > 0 ? (uint32_t)g_streamStrides[s] : 6u;
        bool repack = (vs->streamsUnaligned & (1u << s)) != 0 || stride % 4 != 0;
        uint32_t hostStride = stride;
        if (repack) {
            streamsRepacked |= 1u << s;
            hostStride = vs->packedStride[s];
            if (!(vs->streamsRepackLogged & (1u << s))) {
                vs->streamsRepackLogged |= 1u << s;
                GpuLog("[sdlgpu] shader %d: stream %u repacked, stride %u to %u\n", g_currentVertexShader, s, stride,
                       hostStride);
            }
        }
        uint32_t bytes = vertices * hostStride;
        uint32_t offset = RingReserve(bytes + RING_SLACK);
        const uint8_t *base = userData != NULL && s == 0 ? userData
                            : g_streams[s] != NULL ? XboxDataPointer(((const uint32_t *)g_streams[s])[1]) : NULL;
        const uint8_t *data = base != NULL ? base + (size_t)first * stride : NULL;
        if (data == NULL) {
            if (s == 0) {
                D3D9_BackendMissing("draw with stream 0 unbound");
                return;
            }
            memset(g_ringMapped + offset, 0, bytes);   // a morph stream with nothing bound: the NV2A read zero weights
        } else if (repack) {
            RepackStream(g_ringMapped + offset, data, stride, vertices, vs, s);
        } else {
            memcpy(g_ringMapped + offset, data, bytes);
        }
        memset(g_ringMapped + offset + bytes, 0, RING_SLACK);
        slotOfStream[s] = key.bufferCount;
        key.pitch[key.bufferCount] = (uint16_t)hostStride;
        d.bufferOffset[key.bufferCount] = offset;
        key.bufferCount++;
    }
    for (int e = 0; e < vs->elementCount; e++) {
        VertexElement a = vs->elements[e];
        if (streamsRepacked & (1u << a.stream))
            a.offset = vs->packedOffset[e];
        a.stream = (uint8_t)slotOfStream[a.stream];
        key.attributes[key.attributeCount++] = a;
    }
    d.bufferCount = key.bufferCount;
    StateKey(&key, true);
    bool needsIndices;
    SDL_GPUPrimitiveType type;
    XboxPrimitive(xboxType, &type, &needsIndices);
    if (sprite) {
        // Each point a quad of two triangles, its attributes the instance's
        key.primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        key.instanced = 1;
        key.cull = 1;
        d.count = 6;
        d.instances = vertices;
    } else {
        key.primitive = (uint8_t)type;
        d.indexed = indexed;
        d.indexOffset = indexOffset;
        d.count = count;
        d.instances = 1;
        d.vertexOffset = -(int32_t)first;
    }
    d.samplerCount = textures.samplerCount;
    memcpy(d.samplers, textures.samplers, sizeof(d.samplers));
    AA_NOTE_SCENE_DRAW();
    SubmitDraw(d, key, textures.translated, textures.fogByHost);
}

void D3D9_DrawIndexedVertices(uint32_t primitiveType, uint32_t vertexCount, const void *pIndexData) {
    if (g_device == NULL)
        return;
    SDL_GPUPrimitiveType type;
    bool needsIndices;
    if (!XboxPrimitive(primitiveType, &type, &needsIndices) || vertexCount == 0)
        return;
    const uint16_t *source = (const uint16_t *)pIndexData;
    uint16_t minIndex = 0xFFFF, maxIndex = 0;
    for (uint32_t i = 0; i < vertexCount; i++) {
        if (source[i] < minIndex) minIndex = source[i];
        if (source[i] > maxIndex) maxIndex = source[i];
    }
    uint32_t count = needsIndices ? ListIndexCount(primitiveType, vertexCount) : vertexCount;
    if (count == 0)
        return;
    uint32_t indexOffset = RingReserve(count * 2);
    uint16_t *dst = (uint16_t *)(g_ringMapped + indexOffset);
    if (needsIndices) ListIndices(dst, primitiveType, source, 0, vertexCount);
    else memcpy(dst, source, (size_t)count * 2);
    ShaderDraw(primitiveType, minIndex, (uint32_t)maxIndex + 1, true, indexOffset, count, NULL, 0);
}

void D3D9_DrawVertices(uint32_t primitiveType, uint32_t startVertex, uint32_t vertexCount) {
    if (g_device == NULL)
        return;
    SDL_GPUPrimitiveType type;
    bool needsIndices;
    if (!XboxPrimitive(primitiveType, &type, &needsIndices) || vertexCount == 0)
        return;
    if (needsIndices) {
        uint32_t count = ListIndexCount(primitiveType, vertexCount);
        if (count == 0)
            return;
        uint32_t indexOffset = RingReserve(count * 2);
        ListIndices((uint16_t *)(g_ringMapped + indexOffset), primitiveType, NULL, startVertex, vertexCount);
        ShaderDraw(primitiveType, startVertex, startVertex + vertexCount, true, indexOffset, count, NULL, 0);
        return;
    }
    ShaderDraw(primitiveType, startVertex, startVertex + vertexCount, false, 0, vertexCount, NULL, 0);
}

void D3D9_DrawVerticesUP(uint32_t primitiveType, uint32_t vertexCount, void *pVertexData, uint32_t stride);

// ---------------------------------------------------------------------------------------------------------------
// Pretransformed vertices: the immediate path, the screen-space quads, the movie frames and the partial clears,
// drawn with fixedFunction.cpp's vertex program. Positions in the target's pixels with D3D10's pixel centres: where
// d3d9Backend.cpp moved them by -0.5 for D3D9's, they are left alone, and where it did not, they move by +0.5.
// ---------------------------------------------------------------------------------------------------------------

struct RhwVertex { float x, y, z, rhw; uint32_t colour; float u, v, r, q; };
static SDL_GPUShader *g_pretransformedShader = NULL;
static uint8_t g_pretransformedKinds[16];

static SDL_GPUShader *PretransformedShader(void) {
    if (g_pretransformedShader == NULL) {
        std::vector<uint32_t> words;
        char error[256];
        if (FixedFunction_PretransformedProgram(&words, g_pretransformedKinds, error, sizeof(error)))
            g_pretransformedShader = CreateShader("pretransformed", words, false, 0);
        else
            GpuLog("[sdlgpu] pretransformed program: %s\n", error);
    }
    return g_pretransformedShader;
}

static void PretransformedLayout(PipelineKey *key) {
    key->bufferCount = 1;
    key->pitch[0] = sizeof(RhwVertex);
    key->attributeCount = 3;
    VertexElement position = { 0, 0, (uint8_t)SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0 };
    VertexElement colour = { 0, 3, (uint8_t)SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16 };
    VertexElement texCoord = { 0, 9, (uint8_t)SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 20 };
    key->attributes[0] = position;
    key->attributes[1] = colour;
    key->attributes[2] = texCoord;
}

// Draws pretransformed vertices with the current stage state (or the given program and samplers). Viewport: the
// whole target, since D3D9 does not apply one to them.
static void PretransformedDraw(uint32_t xboxType, const RhwVertex *vertices, uint32_t count, const DrawTextures &textures,
                               const PipelineKey *stateOverride) {
    SDL_GPUShader *vertexShader = PretransformedShader();
    if (vertexShader == NULL || textures.pixelShader == NULL || count == 0)
        return;
    SDL_GPUPrimitiveType type;
    bool needsIndices;
    if (!XboxPrimitive(xboxType, &type, &needsIndices))
        return;
    PipelineKey key;
    memset(&key, 0, sizeof(key));
    if (stateOverride != NULL)
        key = *stateOverride;
    else
        StateKey(&key, true);
    key.vertexShader = vertexShader;
    key.pixelShader = textures.pixelShader;
    key.primitive = (uint8_t)type;
    PretransformedLayout(&key);
    DrawCommand d;
    memset(&d, 0, sizeof(d));
    uint32_t bytes = count * (uint32_t)sizeof(RhwVertex);
    d.bufferOffset[0] = RingReserve(bytes);
    memcpy(g_ringMapped + d.bufferOffset[0], vertices, bytes);
    d.bufferCount = 1;
    d.instances = 1;
    d.count = count;
    if (needsIndices) {
        d.count = ListIndexCount(xboxType, count);
        if (d.count == 0)
            return;
        d.indexed = true;
        d.indexOffset = RingReserve(d.count * 2);
        ListIndices((uint16_t *)(g_ringMapped + d.indexOffset), xboxType, NULL, 0, count);
    }
    d.samplerCount = textures.samplerCount;
    memcpy(d.samplers, textures.samplers, sizeof(d.samplers));
    SDL_GPUViewport saved = g_viewport;
    g_viewport.x = 0;
    g_viewport.y = 0;
    g_viewport.w = (float)g_currentTarget.width;
    g_viewport.h = (float)g_currentTarget.height;
    g_viewport.min_depth = 0.0f;
    g_viewport.max_depth = 1.0f;
    SubmitDraw(d, key, textures.translated, textures.fogByHost);
    g_viewport = saved;
}

// Multiplies a packed D3DCOLOR by a float4 colour constant (what the immediate-mode shader's MUL oD0 does).
static uint32_t ModulateColour(uint32_t packed, const float *k) {
    uint32_t out = 0;
    for (int c = 0; c < 4; c++) {
        int shift = c * 8;
        float f = ((packed >> shift) & 0xFF) * k[(c == 0) ? 2 : (c == 1) ? 1 : (c == 2) ? 0 : 3];
        int v = (int)(f + 0.5f);
        out |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v) << shift;
    }
    return out;
}

// maybeImmediateModeFlush's quads (the action engine): {x, y, colour, u, v, pad} in screen pixels.
static void DrawImmediateQuads(uint32_t vertexCount, const uint8_t *data, uint32_t stride) {
    static RhwVertex vertices[64 * 4];
    if (vertexCount > 64 * 4) vertexCount = 64 * 4;
    AA_BEFORE_OVERLAY();
    DrawTextures textures;
    ApplyTextureStages(false, &textures);
    const float *k = g_vertexConstants[103];
    float scaleX = ScalingTarget() ? g_renderScaleX : 1.0f, scaleY = ScalingTarget() ? g_renderScaleY : 1.0f;
    for (uint32_t v = 0; v < vertexCount; v++) {
        const float *src = (const float*)(data + (size_t)v * stride);
        RhwVertex *d = &vertices[v];
        d->x = src[0] * scaleX;
        d->y = src[1] * scaleY;
        d->z = g_reversedDepth ? 0.0f : 1.0f;
        d->rhw = 1.0f;
        d->colour = ModulateColour(*(const uint32_t*)&src[2], k);
        d->u = src[3] * g_texCoordScale[0][0];
        d->v = src[4] * g_texCoordScale[0][1];
        d->r = 0.0f;
        d->q = 1.0f;
    }
    PretransformedDraw(8, vertices, vertexCount, textures, NULL);
}

void D3D9_DrawVerticesUP(uint32_t primitiveType, uint32_t vertexCount, void *pVertexData, uint32_t stride) {
    if (g_device == NULL)
        return;
    if (primitiveType == 8 && stride == 0x18) {
        DrawImmediateQuads(vertexCount, (const uint8_t*)pVertexData, stride);
        return;
    }
    SDL_GPUPrimitiveType type;
    bool needsIndices;
    if (!XboxPrimitive(primitiveType, &type, &needsIndices) || vertexCount == 0)
        return;
    if (needsIndices) {
        uint32_t count = ListIndexCount(primitiveType, vertexCount);
        if (count == 0)
            return;
        uint32_t indexOffset = RingReserve(count * 2);
        ListIndices((uint16_t *)(g_ringMapped + indexOffset), primitiveType, NULL, 0, vertexCount);
        ShaderDraw(primitiveType, 0, vertexCount, true, indexOffset, count, (const uint8_t *)pVertexData, stride);
        return;
    }
    ShaderDraw(primitiveType, 0, vertexCount, false, 0, vertexCount, (const uint8_t *)pVertexData, stride);
}

// Immediate mode (d3d9Backend.cpp's description): D3DDevice_Begin, SetVertexData*, End.
#define IMMEDIATE_MAX_VERTICES 4096
enum { XVSDE_POSITION = 0, XVSDE_DIFFUSE = 3, XVSDE_TEXCOORD0 = 9, XVSDE_VERTEX_END = 0xFFFFFFFFu };
static RhwVertex g_immediateVertices[IMMEDIATE_MAX_VERTICES];
static uint32_t g_immediateCount = 0;
static uint32_t g_immediatePrimitive = 0;
static bool g_immediateOpen = false;
static uint32_t g_immediateColour = 0xFFFFFFFFu;
static float g_immediateU = 0.0f, g_immediateV = 0.0f;

bool D3D9_DumpingTextures(void) { return false; }
bool D3D9_DescribeFont(const void *xboxTexture, const D3D9FontGlyph *glyphs, int count) {
    (void)xboxTexture; (void)glyphs; (void)count;
    return true;   // nothing to dump: the glyphs are not asked for again
}

void D3D9_ImmediateBegin(uint32_t primitiveType) {
    g_immediatePrimitive = primitiveType;
    g_immediateCount = 0;
    g_immediateOpen = true;
}

void D3D9_ImmediateColour(uint32_t reg, uint32_t colour) {
    if (reg == XVSDE_DIFFUSE)
        g_immediateColour = colour;
}

void D3D9_ImmediateTexCoord(uint32_t reg, float u, float v) {
    if (reg == XVSDE_TEXCOORD0) {
        g_immediateU = u;
        g_immediateV = v;
    }
}

void D3D9_ImmediateVertex(uint32_t reg, float x, float y, float z, float w) {
    if (reg != XVSDE_POSITION && reg != XVSDE_VERTEX_END)
        return;
    if (!g_immediateOpen || g_immediateCount >= IMMEDIATE_MAX_VERTICES)
        return;
    RhwVertex *v = &g_immediateVertices[g_immediateCount++];
    v->x = x;
    v->y = y;
    v->z = z;
    v->rhw = (w != 0.0f) ? w : 1.0f;
    v->colour = g_immediateColour;
    v->u = g_immediateU;
    v->v = g_immediateV;
    v->r = 0.0f;
    v->q = 1.0f;
}

void D3D9_ImmediateEnd(void) {
    g_immediateOpen = false;
    if (g_device == NULL || g_immediateCount == 0)
        return;
    AA_BEFORE_OVERLAY();
    DrawTextures textures;
    ApplyTextureStages(false, &textures);
    // d3d9Backend.cpp gives these positions to D3D9 as they are, where pixel centres are whole numbers: here,
    // with centres at halves, the same pixels are half a pixel further on
    float scaleU = g_texCoordScale[0][0], scaleV = g_texCoordScale[0][1];
    for (uint32_t i = 0; i < g_immediateCount; i++) {
        g_immediateVertices[i].u *= scaleU;
        g_immediateVertices[i].v *= scaleV;
        if (ScalingTarget()) {
            g_immediateVertices[i].x *= g_renderScaleX;
            g_immediateVertices[i].y *= g_renderScaleY;
        }
        g_immediateVertices[i].x += 0.5f;
        g_immediateVertices[i].y += 0.5f;
    }
    PretransformedDraw(g_immediatePrimitive, g_immediateVertices, g_immediateCount, textures, NULL);
    g_immediateCount = 0;
}

// The fixed-function program with one stage of its own: SELECTARG1 of an argument, alpha from another.
static SDL_GPUShader *SelectProgram(uint8_t colourArg, uint8_t alphaOp, uint8_t alphaArg, uint32_t *samplerCount) {
    FixedPixelKey key;
    memset(&key, 0, sizeof(key));
    key.stageCount = 1;
    key.stages[0].colourOp = 2;   // SELECTARG1
    key.stages[0].colourArg[1] = colourArg;
    key.stages[0].alphaOp = alphaOp;
    key.stages[0].alphaArg[1] = alphaArg;
    return FixedProgram(key, samplerCount);
}

// A movie frame from a player of ours: one quad with state of its own (d3d9Backend.cpp's D3D9_DrawMovieFrame).
static SDL_GPUTexture *g_movieTexture = NULL;
static uint32_t g_movieWidth = 0, g_movieHeight = 0, g_movieBatch = 0;

void D3D9_DrawMovieFrame(const void *pixels, uint32_t width, uint32_t height, uint32_t pitch,
                         float x0, float y0, float x1, float y1) {
    if (g_device == NULL || pixels == NULL)
        return;
    // a second frame in one batch goes to a new texture: the batch's draws so far read the first
    if (g_movieTexture == NULL || g_movieWidth != width || g_movieHeight != height || g_movieBatch == g_batch) {
        if (g_movieTexture != NULL)
            g_releaseAfterFlush.push_back(g_movieTexture);
        g_movieTexture = CreateTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, width, height, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if (g_movieTexture == NULL)
            return;
        g_movieWidth = width;
        g_movieHeight = height;
    }
    uint32_t offset = UploadReserve(width * height * 4);
    for (uint32_t y = 0; y < height; y++)
        memcpy(g_uploadMapped + offset + (size_t)y * width * 4, (const uint8_t *)pixels + (size_t)y * pitch, width * 4);
    TextureUpload u = { g_movieTexture, 0, 0, width, height, offset, width };
    g_textureUploads.push_back(u);
    g_movieBatch = g_batch;

    AA_BEFORE_OVERLAY();
    float scaleX = ScalingTarget() ? g_renderScaleX : 1.0f, scaleY = ScalingTarget() ? g_renderScaleY : 1.0f;
    RhwVertex quad[4] = {
        { x0 * scaleX, y0 * scaleY, 0.0f, 1.0f, 0xFFFFFFFFu, 0.0f, 0.0f, 0.0f, 1.0f },
        { x1 * scaleX, y0 * scaleY, 0.0f, 1.0f, 0xFFFFFFFFu, 1.0f, 0.0f, 0.0f, 1.0f },
        { x0 * scaleX, y1 * scaleY, 0.0f, 1.0f, 0xFFFFFFFFu, 0.0f, 1.0f, 0.0f, 1.0f },
        { x1 * scaleX, y1 * scaleY, 0.0f, 1.0f, 0xFFFFFFFFu, 1.0f, 1.0f, 0.0f, 1.0f },
    };
    DrawTextures textures;
    memset(&textures, 0, sizeof(textures));
    textures.pixelShader = SelectProgram(2, 1, 0, &textures.samplerCount);   // the texture's colour, alpha disabled
    textures.samplerCount = 1;
    textures.samplers[0].texture = g_movieTexture;
    textures.samplers[0].sampler = SamplerFor(3, 3, 2, 2, 0, 0);
    PipelineKey state;
    memset(&state, 0, sizeof(state));
    state.cull = 1;
    state.src = SDL_GPU_BLENDFACTOR_ONE;
    state.dst = SDL_GPU_BLENDFACTOR_ZERO;
    state.op = SDL_GPU_BLENDOP_ADD;
    state.writeMask = g_currentTarget.opaque ? 0x7 : 0xF;
    state.depthFunc = 8;
    RenderState saved = g_state;
    g_state.alphaTest = false;
    PretransformedDraw(6, quad, 4, textures, &state);
    g_state = saved;
}

// ---------------------------------------------------------------------------------------------------------------
// Clear: the viewport's rectangle, as D3D9 clears. The whole target is a pass's load operation; a part of it a quad.
// ---------------------------------------------------------------------------------------------------------------

void D3D9_Clear(uint32_t rectCount, void *pRects, uint32_t flags, uint32_t colour, float z, uint32_t stencil) {
    (void)rectCount; (void)pRects; (void)stencil;
    if (g_device == NULL)
        return;
    bool clearColour = (flags & 0xF0) != 0, clearDepth = (flags & 0x01) != 0;
    if (!clearColour && !clearDepth)
        return;
    if (g_reversedDepth) z = 1.0f - z;
    SDL_FColor value = { ((colour >> 16) & 0xFF) / 255.0f, ((colour >> 8) & 0xFF) / 255.0f, (colour & 0xFF) / 255.0f,
                         g_currentTarget.opaque ? 1.0f : ((colour >> 24) & 0xFF) / 255.0f };
    bool whole = g_viewport.x <= 0.0f && g_viewport.y <= 0.0f && g_viewport.x + g_viewport.w >= (float)g_currentTarget.width &&
                 g_viewport.y + g_viewport.h >= (float)g_currentTarget.height;
    if (whole) {
        ClearPass(g_currentTarget.colour, g_currentTarget.depth, clearColour, value, clearDepth, z);
        return;
    }
    float x0 = g_viewport.x, y0 = g_viewport.y, x1 = g_viewport.x + g_viewport.w, y1 = g_viewport.y + g_viewport.h;
    uint32_t packed = g_currentTarget.opaque ? (colour | 0xFF000000u) : colour;
    RhwVertex quad[4] = {
        { x0, y0, z, 1.0f, packed, 0, 0, 0, 1 }, { x1, y0, z, 1.0f, packed, 0, 0, 0, 1 },
        { x0, y1, z, 1.0f, packed, 0, 0, 0, 1 }, { x1, y1, z, 1.0f, packed, 0, 0, 0, 1 },
    };
    DrawTextures textures;
    memset(&textures, 0, sizeof(textures));
    textures.pixelShader = SelectProgram(0, 2, 0, &textures.samplerCount);   // the diffuse colour and alpha
    textures.samplers[0].texture = g_blackTexture;
    textures.samplers[0].sampler = SamplerFor(1, 1, 1, 1, 0, 0);
    PipelineKey state;
    memset(&state, 0, sizeof(state));
    state.cull = 1;
    state.src = SDL_GPU_BLENDFACTOR_ONE;
    state.dst = SDL_GPU_BLENDFACTOR_ZERO;
    state.op = SDL_GPU_BLENDOP_ADD;
    state.writeMask = clearColour ? (g_currentTarget.opaque ? 0x7 : 0xF) : 0;
    state.depthTest = clearDepth;
    state.depthWrite = clearDepth;
    state.depthFunc = 8;
    RenderState saved = g_state;
    g_state.alphaTest = false;
    SDL_GPUViewport savedViewport = g_viewport;
    PretransformedDraw(6, quad, 4, textures, &state);
    g_viewport = savedViewport;
    g_state = saved;
}

// ---------------------------------------------------------------------------------------------------------------
// FXAA over the 3D scene (d3d9Backend.cpp decides when; fxaa.cpp explains the method): the back buffer copied, then
// drawn back through fxaa.hlsl's pixel shader.
// ---------------------------------------------------------------------------------------------------------------
#ifdef NF_ANTIALIASING
static SDL_GPUTexture *g_fxaaCopy = NULL;
static SDL_GPUShader *g_fxaaVertexShader = NULL, *g_fxaaPixelShader = NULL;
static bool g_fxaaFailed = false;

static void ApplyAntiAliasing(void) {
    if (g_antiAliasing != AA_FXAA || g_fxaaFailed)
        return;
    if (g_fxaaPixelShader == NULL) {
        std::vector<uint32_t> words;
        uint8_t kinds[16];
        char error[256];
        if (FixedFunction_ScreenProgram(&words, kinds, error, sizeof(error)))
            g_fxaaVertexShader = CreateShader("fxaa_vs", words, false, 0);
        std::vector<uint32_t> pixel(g_fxaaPixelSpirv, g_fxaaPixelSpirv + sizeof(g_fxaaPixelSpirv) / 4);
        g_fxaaPixelShader = CreateShader("fxaa_ps", pixel, true, 1, "psmain");
        g_fxaaCopy = CreateTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, g_backBuffer.width, g_backBuffer.height, 1,
                                   SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if (g_fxaaVertexShader == NULL || g_fxaaPixelShader == NULL || g_fxaaCopy == NULL) {
            g_fxaaFailed = true;
            GpuLog("[sdlgpu] FXAA could not be set up - off for the rest of the session\n");
            return;
        }
        GpuLog("[sdlgpu] FXAA on (AntiAliasing=1 in settings.ini; 0 turns it off)\n");
    }
    SDL_GPUBlitInfo blit;
    memset(&blit, 0, sizeof(blit));
    blit.source.texture = g_backBuffer.colour;
    blit.source.w = g_backBuffer.width;
    blit.source.h = g_backBuffer.height;
    blit.destination.texture = g_fxaaCopy;
    blit.destination.w = g_backBuffer.width;
    blit.destination.h = g_backBuffer.height;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_NEAREST;
    RecordBlit(blit);

    PipelineKey key;
    memset(&key, 0, sizeof(key));
    key.vertexShader = g_fxaaVertexShader;
    key.pixelShader = g_fxaaPixelShader;
    key.primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    key.cull = 1;
    key.src = SDL_GPU_BLENDFACTOR_ONE;
    key.dst = SDL_GPU_BLENDFACTOR_ZERO;
    key.op = SDL_GPU_BLENDOP_ADD;
    key.writeMask = 0x7;   // the back buffer's alpha stays opaque
    key.depthFunc = 8;
    key.bufferCount = 1;
    key.pitch[0] = 24;
    key.attributeCount = 2;
    VertexElement position = { 0, 0, (uint8_t)SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0 };
    VertexElement texCoord = { 0, 9, (uint8_t)SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 16 };
    key.attributes[0] = position;
    key.attributes[1] = texCoord;
    static const float quad[4][6] = {
        { -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.0f }, {  1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 0.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f }, {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f },
    };
    DrawCommand d;
    memset(&d, 0, sizeof(d));
    d.bufferCount = 1;
    d.bufferOffset[0] = RingReserve(sizeof(quad));
    memcpy(g_ringMapped + d.bufferOffset[0], quad, sizeof(quad));
    d.count = 4;
    d.instances = 1;
    d.samplerCount = 1;
    d.samplers[0].texture = g_fxaaCopy;
    d.samplers[0].sampler = SamplerFor(3, 3, 2, 2, 0, 0);
    d.viewport.w = (float)g_backBuffer.width;
    d.viewport.h = (float)g_backBuffer.height;
    d.viewport.max_depth = 1.0f;
    d.pipeline = PipelineFor(key);
    d.vertexUniforms = VertexUniforms();
    // fxaa.hlsl's texel: 1/width, 1/height, width, height (its whole constant block)
    float texel[kSpirvPixelConstants][4];
    memset(texel, 0, sizeof(texel));
    texel[0][0] = 1.0f / (float)g_backBuffer.width;
    texel[0][1] = 1.0f / (float)g_backBuffer.height;
    texel[0][2] = (float)g_backBuffer.width;
    texel[0][3] = (float)g_backBuffer.height;
    d.pixelUniforms = (int)(g_pixelUniformSnapshots.size() / (kSpirvPixelConstants * 4));
    g_pixelUniformSnapshots.insert(g_pixelUniformSnapshots.end(), &texel[0][0], &texel[0][0] + kSpirvPixelConstants * 4);
    d.pixelUniformBytes = 16;
    if (d.pipeline == NULL)
        return;
    Target saved = g_currentTarget;
    g_currentTarget = g_backBuffer;
    RecordDraw(d, false);
    g_currentTarget = saved;
}
#endif

// ---------------------------------------------------------------------------------------------------------------
// Dumps, readbacks and Swap.
// ---------------------------------------------------------------------------------------------------------------

static uint32_t DumpEvery(void) {
    static int value = -1;
    if (value < 0)
        value = SettingInt("DumpEvery", 0);
    return (uint32_t)value;
}
static uint32_t g_dumpFrame = 0;
static volatile bool g_dumpRequested = false;
static char g_screenshotName[128];
static volatile bool g_screenshotRequested = false;

uint32_t D3D9_RequestDump(void) {
    g_dumpRequested = true;
    return g_frameCount + 1;
}

uint32_t D3D9_RequestScreenshot(const char *name) {
    snprintf(g_screenshotName, sizeof(g_screenshotName), "%s", name);
    g_screenshotRequested = true;
    return g_frameCount + 1;
}

void D3D9_TraceNote(const char *note) { (void)note; }

// The back buffer as B, G, R, A rows, read now.
static bool ReadTexture(SDL_GPUTexture *texture, uint32_t width, uint32_t height, std::vector<uint8_t> *pixels) {
    uint32_t offset = RecordDownload(texture, width, height, 4);
    Flush();
    if (offset + (size_t)width * height * 4 > g_downloaded.size())
        return false;
    pixels->assign(g_downloaded.begin() + offset, g_downloaded.begin() + offset + (size_t)width * height * 4);
    return true;
}

static void DumpBackBuffer(const char *name, bool alpha) {
    std::vector<uint8_t> pixels;
    if (!ReadTexture(g_backBuffer.colour, g_backBuffer.width, g_backBuffer.height, &pixels))
        return;
    uint32_t width = g_backBuffer.width, height = g_backBuffer.height;
    size_t rowBytes = (size_t)width * 3;
    std::vector<uint8_t> colour(rowBytes * height), alphaBytes(rowBytes * height);
    for (size_t i = 0; i < (size_t)width * height; i++) {
        memcpy(&colour[i * 3], &pixels[i * 4], 3);
        memset(&alphaBytes[i * 3], pixels[i * 4 + 3], 3);
    }
    char path[160];
    snprintf(path, sizeof(path), "%s.bmp", name);
    WriteBmp24(path, width, height, colour.data(), rowBytes);
    if (alpha) {
        snprintf(path, sizeof(path), "%s_alpha.bmp", name);
        WriteBmp24(path, width, height, alphaBytes.data(), rowBytes);
    }
}

void D3D9_Swap(uint32_t type) {
    (void)type;
    if (g_device == NULL)
        return;
    if (g_screenshotRequested) {
        g_screenshotRequested = false;
        DumpBackBuffer(g_screenshotName, false);
    }
    if (g_dumpFrame != 0) {
        char name[64];
        snprintf(name, sizeof(name), "d3d9_dump_frame_%u", g_dumpFrame);
        DumpBackBuffer(name, true);
        DumpBackBuffer("d3d9_dump_frame", true);
        g_dumpFrame = 0;
    }
    if (g_dumpRequested || (DumpEvery() > 0 && (g_frameCount + 1) % DumpEvery() == 0)) {
        g_dumpRequested = false;
        g_dumpFrame = g_frameCount + 1;
    }
#ifdef NF_ANTIALIASING
    if (g_antiAliasing != AA_OFF && g_aaSceneDrawn && !g_aaApplied)
        ApplyAntiAliasing();
    g_aaSceneDrawn = g_aaApplied = false;
#endif
    g_fenceNextFlush = true;   // at most one frame queued behind this one
    Flush();
    int presentEvery = GfxHost_PresentEvery();
    if (g_window != NULL && (presentEvery == 0 || g_frameCount % (uint32_t)presentEvery == 0))
        SDL_RunOnMainThread(PresentOnWindowThread, NULL, true);
    PaceFrame();
    g_frameCount++;
    if (g_frameCount == 1 || g_frameCount % 600 == 0)
        PrintMissingSummary();
}

// ---------------------------------------------------------------------------------------------------------------
// Surfaces (d3d9Backend.cpp's stand-ins and Xbox-layout surface objects).
// ---------------------------------------------------------------------------------------------------------------

static XboxSurface *g_allocatedSurfaces[64];
static int g_allocatedSurfaceCount = 0;

uint32_t *D3D9_GetBackBuffer2(int32_t backBufferIndex) {
    (void)backBufferIndex;
    return (uint32_t*)&g_dummyBackBuffer;
}

#if UINTPTR_MAX > 0xffffffffu
static const uint32_t kSurfaceDataMask = 0xFFFFFFFFu;
#else
static const uint32_t kSurfaceDataMask = 0x0FFFFFFFu;
#endif

void *D3D9_GetSurfaceLevel2(void *pTexture, uint32_t level) {
    (void)level;
    const uint32_t *header = (const uint32_t*)pTexture;
    XboxSurface *s = (XboxSurface*)malloc(sizeof(XboxSurface));
    if (s == NULL)
        return NULL;
    s->Common = XBOX_SURFACE_COMMON;
    s->Data = header[1] & kSurfaceDataMask;
    s->Lock = 0;
    s->Format = header[3];
    s->Size = header[4];
    s->Parent = PtrTo32(pTexture);
    if (g_allocatedSurfaceCount < (int)(sizeof(g_allocatedSurfaces) / sizeof(g_allocatedSurfaces[0])))
        g_allocatedSurfaces[g_allocatedSurfaceCount++] = s;
    return s;
}

void *D3D9_GetRenderTarget2(void) { return &g_dummyRenderTarget; }
void *D3D9_GetDepthStencilSurface2(void) { return &g_dummyDepthStencil; }

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

void D3D9_GetSurfaceDesc(void *pSurface, uint32_t *format, uint32_t *width, uint32_t *height) {
    uint32_t f = XFMT_LIN_X8R8G8B8, w = g_gameWidth, h = g_gameHeight;
    if (pSurface == &g_dummyDepthStencil) {
        f = XFMT_LIN_D24S8;
    } else if (pSurface != &g_dummyBackBuffer && pSurface != &g_dummyRenderTarget && pSurface != NULL) {
        const XboxSurface *s = (const XboxSurface*)pSurface;
        f = (s->Format >> 8) & 0xFF;
        if (XboxFormatIsLinear(f)) {
            w = (s->Size & 0xFFF) + 1;
            h = ((s->Size >> 12) & 0xFFF) + 1;
        } else {
            w = 1u << ((s->Format >> 20) & 0xF);
            h = 1u << ((s->Format >> 24) & 0xF);
        }
    }
    if (format != NULL) *format = f;
    if (width != NULL)  *width = w;
    if (height != NULL) *height = h;
}

// The pause menu's lock of the back buffer: its pixels, at the game's size, read now.
bool D3D9_ReadBackBuffer(void *destination, uint32_t pitch, uint32_t width, uint32_t height) {
    if (g_device == NULL || destination == NULL)
        return false;
    SDL_GPUTexture *source = g_backBuffer.colour;
    uint32_t sourceWidth = g_backBuffer.width, sourceHeight = g_backBuffer.height;
    if (g_renderScaled) {
        if (g_readBackScaled == NULL)
            g_readBackScaled = CreateTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, g_gameWidth, g_gameHeight, 1,
                                             SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if (g_readBackScaled == NULL)
            return false;
        SDL_GPUBlitInfo blit;
        memset(&blit, 0, sizeof(blit));
        blit.source.texture = g_backBuffer.colour;
        blit.source.w = g_backBuffer.width;
        blit.source.h = g_backBuffer.height;
        blit.destination.texture = g_readBackScaled;
        blit.destination.w = g_gameWidth;
        blit.destination.h = g_gameHeight;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_LINEAR;
        RecordBlit(blit);
        source = g_readBackScaled;
        sourceWidth = g_gameWidth;
        sourceHeight = g_gameHeight;
    }
    std::vector<uint8_t> pixels;
    if (!ReadTexture(source, sourceWidth, sourceHeight, &pixels))
        return false;
    uint32_t rows = height < sourceHeight ? height : sourceHeight;
    uint32_t bytes = (width < sourceWidth ? width : sourceWidth) * 4;
    if (bytes > pitch) bytes = pitch;
    for (uint32_t y = 0; y < rows; y++)
        memcpy((uint8_t*)destination + (size_t)y * pitch, &pixels[(size_t)y * sourceWidth * 4], bytes);
    static bool said = false;
    if (!said) { said = true; GpuLog("[sdlgpu] the game read the back buffer back (%ux%u)\n", width, rows); }
    return true;
}

bool D3D9_IsStandInSurface(const void *pSurface) {
    return pSurface == &g_dummyBackBuffer || pSurface == &g_dummyRenderTarget || pSurface == &g_dummyDepthStencil;
}

// ---------------------------------------------------------------------------------------------------------------
// Visibility tests: Begin opens the mask, End files its download under the index, and the count is the mask's
// covered pixels once the batch holding it has run - flushing it first if it has not.
// ---------------------------------------------------------------------------------------------------------------
#define X_D3DERR_TESTINCOMPLETE 0x8876085Cu

void D3D9_BeginVisibilityTest(void) {
    if (g_device == NULL)
        return;
    if (g_visibilityMask == NULL || g_visibilityMaskWidth != g_currentTarget.width ||
        g_visibilityMaskHeight != g_currentTarget.height) {
        if (g_visibilityMask != NULL)
            g_releaseAfterFlush.push_back(g_visibilityMask);
        g_visibilityMask = CreateTexture(SDL_GPU_TEXTUREFORMAT_R8_UNORM, g_currentTarget.width, g_currentTarget.height, 1,
                                         SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
        g_visibilityMaskWidth = g_currentTarget.width;
        g_visibilityMaskHeight = g_currentTarget.height;
    }
    if (g_visibilityMask == NULL)
        return;
    SDL_FColor none = { 0, 0, 0, 0 };
    BeginPass(g_visibilityMask, g_currentTarget.depth);
    g_passes.back().clearColour = true;
    g_passes.back().clearValue = none;
    g_visibilityOpen = true;
}

void D3D9_EndVisibilityTest(uint32_t index) {
    if (!g_visibilityOpen)
        return;
    g_visibilityOpen = false;
    if (index >= VISIBILITY_INDICES)
        return;
    VisibilityResult &v = g_visibility[index];
    v.offset = RecordDownload(g_visibilityMask, g_visibilityMaskWidth, g_visibilityMaskHeight, 1);
    v.width = g_visibilityMaskWidth;
    v.height = g_visibilityMaskHeight;
    v.batch = g_batch;
    v.state = 1;
}

uint32_t D3D9_GetVisibilityTestResult(uint32_t index, uint32_t *result, uint64_t *timeStamp) {
    if (timeStamp != NULL) *timeStamp = 0;
    if (result != NULL) *result = 0;
    if (index >= VISIBILITY_INDICES || g_visibility[index].state == 0)
        return 0;
    if (g_visibility[index].state == 1)
        Flush();
    if (g_visibility[index].state != 2)
        return X_D3DERR_TESTINCOMPLETE;
    // Samples in the game's own 640x480 at its multisample type, as the NV2A counted them
    float scale = (float)g_xboxSamplesPerPixel;
    if (g_renderScaled)
        scale /= g_renderScaleX * g_renderScaleY;
    uint32_t count = (uint32_t)((float)g_visibility[index].count * scale + 0.5f);
    if (result != NULL)
        *result = count;
    return 0;
}

void D3D9_BlockUntilNotBusy(void *pResource) { (void)pResource; }
void D3D9_KickOffAndWaitForIdle(void) {}
