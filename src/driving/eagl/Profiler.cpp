#include "Profiler.h"
#include "../platform/XboxXapi.h"

#include "EaglFont.h"
#include "EaglGlobals.h"
#include "EaglOriginals.h"
#include "Model.h"
#include "Realgraph.h"
#include "RenderContext.h"
#include "Transform.h"
#include "View.h"
#include "../platform/RealSystem.h"
#include "../platform/X87.h"
#include "../../helpers.h"

#include <bit>
#include <intrin.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <xmmintrin.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL's PrintMessage and profiler (docs/driving/eagl.md 4.9).
//
// PrintMessage formats into a 512-byte buffer and hands it to OutputDebugStringA, unless the game installed a hook.
// The profiler: ProfilerRegions (a list at 0x0024053c) collect cycles from scoped timers that read RDTSC, the
// frame mark (0x000f52d0, from RenderContext::EndFrame) turns each region's cycles into a fraction of the frame
// and keeps a 32-frame history, and DrawRegions draws bars or graphs through a DrawGouraud with the default font's
// labels. The RDTSC sites are the ones src/driving/platform/XboxTimer.cpp lists (the rate cancels out: cycles are
// only compared with cycles); each is __rdtsc() here, its low half, where the original executes the instruction.
// The bare "rdtsc; ret" at 0x000f4520 is module F's range and XboxTimer.cpp's redirect, not here.
//
// The x87 arithmetic is in double, in the original's order, with a float rounding at every store.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

using EAGL::ProfilerRegion;
using EAGL::ProfilerTimer;

namespace {

// Constants the original reads from .rdata that are not exact small numbers (bit-checked against it)
constexpr float kPercentScale = 0.00078125f;   // 1 / 1280: the bar sum (40 per frame, 32 frames) to a fraction
constexpr float kBelowOne = 0.995f;
constexpr float kBelowTen = 9.95f;
static_assert(std::bit_cast<uint32_t>(kPercentScale) == 0x3a4ccccd, "the original's 0x001a0b78");
static_assert(std::bit_cast<uint32_t>(kBelowOne) == 0x3f7eb852, "the original's 0x001a0b74");
static_assert(std::bit_cast<uint32_t>(kBelowTen) == 0x411f3333, "the original's 0x001a0b70");
constexpr double kBarHeight = 1280.0;          // a full frame's bar sum
constexpr double kBarScale = 0.03125;          // bar sum to pixels (1 / 32)

// PrintMessage's state
#define PrintLevel I32_AT(0x001cdcc8)                       // messages above it are dropped
#define PrintHook (*(int (**)(const char *format, va_list arguments))0x00240268)
#define PrintBuffer ((char *)0x00240270)                    // 0x200 bytes

// The profiler's state
#define HistoryIndex U32_AT(0x00240528)                     // the ring slot this frame writes
#define Gouraud (*(EAGL::ProfilerDrawGouraud **)0x0024052c)
#define PageTick U32_AT(0x00240530)
#define Page U32_AT(0x00240534)                             // which region's label is shown
#define ScreenWidth FLOAT_AT(0x00240538)
#define RegionList (*(ProfilerRegion **)0x0024053c)
#define ProfilerViewPort (*(EAGL::ViewPort **)0x00240540)
#define DrawEnabled U8_AT(0x00240544)
#define BarMode U8_AT(0x00240545)
#define LastFrameTsc U32_AT(0x00240548)
#define FrameRegion ((ProfilerRegion *)0x00240560)          // the static region the frame mark times
#define Paging U8_AT(0x001ce018)
#define StartX FLOAT_AT(0x001ce01c)
#define StartY FLOAT_AT(0x001ce020)
#define CurrentTimer (*(ProfilerTimer **)0x001ce024)
#define SkipFrame U8_AT(0x001ce072)
#define PageTicks U32_AT(0x00242424)                        // TIMER ticks per page (and per flash)

#define NameDrawGouraudNew ((const char *)0x001ce05c)       // "EAGL::DrawGouraud new"
#define NameDynamicModelNew ((const char *)0x001ce3b0)      // "EAGL::DynamicModel new"

enum : uint32_t { kVertexDiffuse = 3, kVertexPosition = 0xffffffff };   // D3DVSDE_*
enum { kLineList = 2, kQuadStrip = 6 };             // D3DPT_*

// RDTSC's low half (all the profiler keeps)
inline uint32_t Rdtsc() {
    return (uint32_t)__rdtsc();
}

void SetVertex(uint32_t colour, float x, float y) {
    D3DDevice_SetVertexDataColor(kVertexDiffuse, colour);
    D3DDevice_SetVertexData4f(kVertexPosition, x, y, 0.0f, 1.0f);
}

// The end of each strip or list: DrawGouraud's begun flag cleared, D3DDevice_End, InternalFlush
void EndPrimitive() {
    EAGL::ProfilerDrawGouraud *drawGouraud = Gouraud;
    drawGouraud->begun = 0;
    D3DDevice_End();
    drawGouraud->InternalFlush();
}

}  // namespace

// FUNC_AT(0x000f42b0)
int EAGL::PrintMessage(int level, const char *format, ...) {
    if (level > PrintLevel)
        return 0;
    va_list arguments;
    va_start(arguments, format);
    int (*hook)(const char *, va_list) = PrintHook;
    if (hook != NULL) {
        int result = hook(format, arguments);
        va_end(arguments);
        return result;
    }
    int written = CrtVsnprintf(PrintBuffer, 0x200, format, arguments);
    PrintBuffer[0x1ff] = 0;
    Xbox_OutputDebugStringA(PrintBuffer);
    va_end(arguments);
    return written;
}

// D3DDevice_SetVertexShaderConstant with the count in EAX, the register (less 0x60) in ECX and the data in EDX,
// the three entry points' own register convention (the seam's __fastcall functions, ../gfx/D3D8.h): one and four
// constants go straight to their inline versions.

// FUNC_AT(0x000f4310)
__declspec(naked) void EAGL::ProfilerSetVertexShaderConstantRegs() {
    __asm {
        add ecx, 0x60
        cmp eax, 1
        jne NotOne
        jmp D3DDevice_SetVertexShaderConstant1
    NotOne:
        cmp eax, 4
        jne NotFour
        jmp D3DDevice_SetVertexShaderConstant4
    NotFour:
        shl eax, 2
        push eax
        call D3DDevice_SetVertexShaderConstantNotInline
        ret
    }
}

// FUNC_AT(0x000f4580)
void EAGL::ProfilerHistory::SetHistoryValue(uint32_t cycleCount, float value, int index) {
    cycleSum = cycleSum - cycles[index];
    double c = cycleCount;   // FILD, plus 2^32 when the top bit is set: exact
    cycles[index] = float(c);
    cycleSum = float(c + cycleSum);   // the unrounded count (FST keeps it on the stack)
    valueSum = valueSum - values[index];
    values[index] = value;
    valueSum = value + valueSum;
}

// FUNC_AT(0x000f45f0)
void EAGL::ProfilerResetMaxima() {
    for (ProfilerRegion *region = RegionList; region != NULL; region = region->next) {
        region->maxCycles = 0;
        region->maxFraction = 0.0f;
    }
}

// Turns the page of regions labelled every PageTicks ticks while paging is on, back to the first page past the
// last; with paging off it only keeps the tick.
// FUNC_AT(0x000f4620)
void EAGL::ProfilerAdvancePage(uint32_t count) {
    if (Paging != 0) {
        uint32_t interval = PageTicks;
        if (TIMER_gettick() - PageTick >= interval) {
            PageTick = TIMER_gettick();
            Page++;
        }
    } else {
        PageTick = TIMER_gettick();
    }
    if (Page >= count)
        Page = 0;
}

// FUNC_AT(0x000f4680)
void EAGL::ProfilerClearCycles() {
    for (ProfilerRegion *region = RegionList; region != NULL; region = region->next)
        region->cycles = 0;
}

// FUNC_AT(0x000f46b0)
void EAGL::ProfilerRegion::ProcessRegion(uint32_t frameCycles) {
    uint32_t c = cycles;
    float fraction = 0.0f;
    if (c == 0) {
        if (idleFrames < 0x20)
            idleFrames++;
    } else {
        if (c > maxCycles)
            maxCycles = c;
        idleFrames = 0;
        fraction = float(double(c) / frameCycles * 40.0);
        if (fraction > maxFraction)   // FCOMP, TEST AH,41h: kept unless greater (NaN never replaces)
            maxFraction = fraction;
    }
    history.SetHistoryValue(c, fraction, HistoryIndex);
}

// One region in one of DrawRegions' three passes: 0 the swatch and, on the labelled page, the label's backing,
// 1 the bar (bar mode) or the 32-frame graph, 2 the figures and the name. x and y move to the next column when the
// region would start past the screen's right edge. total accumulates the regions' frame fractions; the frame
// region shows what is left of the frame instead.
// FUNC_AT(0x000f4760)
void EAGL::ProfilerRegion::DrawRegion(float x, float y, int index, int pass, float *total) {
    float xSlot = x;   // the argument's slot, rewritten as the original does ([esp+0x48])
    bool label = false;
    double px = x, py;   // the pen, kept on the x87 stack
    if (px > double(ScreenWidth) - 32.0) {   // FCOMPP, TEST AH,41h: on only when greater (ordered)
        float shapeX, shapeY, shapeW, shapeH, shapeNear, shapeFar;
        ProfilerViewPort->GetShape(&shapeX, &shapeY, &shapeW, &shapeH, &shapeNear, &shapeFar);
        float limit = ScreenWidth - 32.0f;
        px = x;
        if (px > limit) {
            xSlot = float(double(shapeH) * 0.5 + shapeY);   // the viewport's middle, in the x slot
            float columnWidth = limit - StartX;
            py = y;
            do {
                if (StartY > xSlot)
                    py = py - 52.0;
                else
                    py = py + 52.0;
                px = px - columnWidth;
            } while (px > limit);
            xSlot = float(px);
        } else {
            py = y;
        }
    } else {
        py = y;
    }
    float penX = float(px);   // [esp+0xc]
    float penY = float(py);   // [esp+0x14]
    float rectX = 0.0f, rectY = 0.0f, rectW = 0.0f, rectH = 0.0f;
    uint32_t swatch;
    if (index == (int)Page && DefaultFont != NULL) {
        swatch = 0xff505050;
        label = true;
        FONT_getrectx_thunk(DefaultFont, (const uint8_t *)name, &rectX, &rectY, &rectW, &rectH);
    } else {
        swatch = 0xff000000;
    }
    if (flashTick != 0) {
        uint32_t now = TIMER_gettick();
        if (now - flashTick >= PageTicks)
            flashTick = 0;
        else if ((now & 8) != 0)
            swatch = 0xffaa0000;
    }
    float vertex[4];   // x, y, z, w for ProfilerVertex
    vertex[2] = 0.0f;
    vertex[3] = 1.0f;
    float scratch;     // [esp+0x34]: the swatch's right edge, a vertex colour, the graph's offset
    if (pass == 0) {
        Gouraud->Begin(kQuadStrip);
        SetVertex(swatch, penX, penY);
        scratch = penX + 32.0f;
        vertex[0] = scratch;
        SetVertex(swatch, vertex[0], penY);
        vertex[1] = penY + 40.0f;
        SetVertex(swatch, penX, vertex[1]);
        vertex[0] = scratch;
        SetVertex(swatch, vertex[0], vertex[1]);
        EndPrimitive();
        if (label && DefaultFont != NULL) {
            if (double(ScreenWidth) * 0.5 < penX)   // TEST AH,5 / JP: on when less (ordered)
                penX = float(32.0 - rectW + penX);
            Gouraud->Begin(kQuadStrip);
            uint32_t black = 0xff000000;
            vertex[1] = penY + 43.0f;
            vertex[0] = penX;
            ProfilerVertex(vertex, &black);
            vertex[0] = rectW + penX;
            ProfilerVertex(vertex, &black);
            vertex[1] = vertex[1] + rectH;
            vertex[0] = penX;
            ProfilerVertex(vertex, &black);
            vertex[0] = rectW + penX;
            ProfilerVertex(vertex, &black);
            EndPrimitive();
        }
        *total = history.valueSum + *total;
    }
    penY = penY + 40.0f;
    if (pass == 1) {
        if (BarMode != 0) {
            // A bar up from the pen, in the region's colour
            float bar = history.valueSum;
            if (this == FrameRegion)
                bar = float(kBarHeight - *total + bar);
            Gouraud->Begin(kQuadStrip);
            SetVertex(colour, penX, penY);
            scratch = penX + 32.0f;
            vertex[0] = scratch;
            SetVertex(colour, vertex[0], penY);
            vertex[1] = float(penY - double(bar) * kBarScale);
            SetVertex(colour, penX, vertex[1]);
            vertex[0] = scratch;
            SetVertex(colour, vertex[0], vertex[1]);
        } else {
            // The history as vertical lines, one a frame from the oldest, a pixel apart
            scratch = 0.0f;
            if (this == FrameRegion)
                scratch = float((kBarHeight - *total) * kBarScale);
            Gouraud->Begin(kLineList);
            uint32_t i = HistoryIndex;
            float lineY = penY;
            for (;;) {
                SetVertex(colour, penX, lineY);
                vertex[1] = float(penY - (double(scratch) + history.values[i]));
                SetVertex(colour, penX, vertex[1]);
                i++;
                penX = penX + 1.0f;
                if (i >= 0x20)
                    i = 0;
                if (i == HistoryIndex)
                    break;
            }
        }
        EndPrimitive();
    }
    Gouraud->InternalFlush();
    if (pass != 2)
        return;
    const uint8_t *font = DefaultFont;
    if (font == NULL)
        return;
    // The figure: per cent of the frame, or what is left of it for the frame region
    double sum = history.valueSum;
    if (this == FrameRegion)
        sum = sum + (kBarHeight - *total);
    double percent = sum * 100.0 * kPercentScale;
    float textX = penX + 2.0f;
    float textY = float(double(penY) - 40.0 + 5.0);
    if (percent < kBelowOne) {   // TEST AH,5 / JP: on when less (ordered)
        float hundredths = float(percent * 100.0);
        FONT_drawtextfa(font, textX, textY, ".%02d", RoundToInt(hundredths));
    } else if (percent < kBelowTen) {
        FONT_drawtextfa(font, textX, textY, "%.1f", percent);
    } else {
        FONT_drawtextfa(font, textX + 4.0f, textY, "%.0f", percent);
    }
    if (!label)
        return;
    if (double(ScreenWidth) * 0.5 < xSlot)
        xSlot = float(32.0 - rectW + xSlot);
    FONT_drawtextfa(DefaultFont, xSlot, penY + 3.0f, "%s", name);
}

// FUNC_AT(0x000f4e50)
void EAGL::ProfilerRegion::Link() {
    next = RegionList;
    RegionList = this;
}

// FUNC_AT(0x000f4e70)
void EAGL::ProfilerRegion::Unlink() {
    ProfilerRegion *p = RegionList;
    if (p == this) {
        RegionList = next;
        return;
    }
    if (p->next != this) {
        for (;;) {
            ProfilerRegion *n = p->next;
            if (n == NULL)
                break;
            p = n;
            if (p->next == this)
                break;
        }
        if (p->next != this)
            return;
    }
    p->next = next;
}

// FUNC_AT(0x000f5010)
void EAGL::ProfilerRegion::UnlinkThunk() {
    Unlink();
}

// FUNC_AT(0x000f4ec0)
void EAGL::ProfilerTimer::Start() {
    if (running == 0) {
        CurrentTimer = this;
        start = Rdtsc();   // RDTSC at 0x000f4ed2
        running = 1;
    }
}

// FUNC_AT(0x000f4ef0)
void EAGL::ProfilerTimer::Stop() {
    if (running != 0) {
        uint32_t now = Rdtsc();   // RDTSC at 0x000f4efd
        region->cycles += now - start;
        running = 0;
    }
}

// FUNC_AT(0x000f4f30)
void EAGL::ProfilerTimer::Destruct() {
    if (running != 0) {
        uint32_t now = Rdtsc();   // RDTSC at 0x000f4f3d
        region->cycles += now - start;
        running = 0;
        ProfilerTimer *p = parent;
        if (p->running == 0) {
            CurrentTimer = p;
            p->start = Rdtsc();   // RDTSC at 0x000f4f75
            p->running = 1;
        }
    } else if (child != NULL) {
        child->parent = parent;
    }
}

// FUNC_AT(0x000f4f90)
EAGL::ProfilerRegion* EAGL::ProfilerRegion::Construct(const char *regionName, uint32_t regionColour) {
    name = regionName;
    history.cycleSum = 0.0f;
    history.valueSum = 0.0f;
    for (int i = 0; i < 32; i++) {
        history.values[i] = 0.0f;
        history.cycles[i] = 0.0f;
    }
    colour = regionColour;
    cycles = 0;
    maxCycles = 0;
    maxFraction = 0.0f;
    idleFrames = 0x20;
    flashTick = 0;
    next = RegionList;
    RegionList = this;
    return this;
}

// FUNC_AT(0x000f5020)
void EAGL::ProfilerRegion::DrawRegions() {
    float total = 0.0f;
    if (ProfilerViewPort == NULL) {
        RenderContext *context = Device::Get()->GetCurrentRenderContext();
        ProfilerViewPort = context->NewViewPort();
        float width, height;
        context->GetSize(&width, &height);
        ProfilerViewPort->SetShape(0.0f, 0.0f, width, height, 0.0f, 1.0f);
        ProfilerViewPort->SetOrthographic(0.0f, 1.0f);
        ScreenWidth = width;
    }
    if (Gouraud == NULL) {
        void *memory = EaglMalloc(sizeof(DrawGouraud), NameDrawGouraudNew);
        DrawGouraud *drawGouraud = memory != NULL ? static_cast<DrawGouraud *>(memory)->Construct() : NULL;
        Gouraud = static_cast<ProfilerDrawGouraud *>(drawGouraud);
        drawGouraud->Init();
        Gouraud->state.SetDepthTestMethod(0x207);   // GL_ALWAYS
    }
    ProfilerViewPort->BeginView();
    if (DefaultFont != NULL)
        ((FNTXFont *)DefaultFont)->colour = 0xffaaaaaa;
    uint32_t drawn = 0;
    for (int pass = 0; pass < 3; pass++) {
        float x = StartX;
        float y = StartY;
        drawn = 0;
        for (ProfilerRegion *region = RegionList; region != NULL; region = region->next) {
            if (region->idleFrames < 0x20 && (region->flashTick == 0 || region->history.valueSum > 0.5f)) {
                region->DrawRegion(x, y, drawn, pass, &total);
                x = x + 34.0f;
                drawn++;
            } else {
                region->flashTick = TIMER_gettick();
            }
        }
        Gouraud->InternalFlush();
    }
    ProfilerViewPort->EndView();
    ProfilerAdvancePage(drawn);   // inline in the original
}

// FUNC_AT(0x000f5230)
EAGL::ProfilerTimer* EAGL::ProfilerTimer::Construct(ProfilerRegion *timedRegion) {
    region = timedRegion;
    Enter();
    return this;
}

// FUNC_AT(0x000f5250)
EAGL::ProfilerTimer* EAGL::ProfilerTimer::ConstructMaybeStarted(ProfilerRegion *timedRegion, bool startNow) {
    region = timedRegion;
    if (startNow) {
        Enter();
        return this;
    }
    running = 0;
    child = NULL;
    return this;
}

// FUNC_AT(0x000f5280)
void EAGL::ProfilerRegion::ProcessRegions(uint32_t frameCycles) {
    for (ProfilerRegion *region = RegionList; region != NULL; region = region->next)
        region->ProcessRegion(frameCycles);
    uint32_t index = HistoryIndex + 1;
    HistoryIndex = index;
    if (index >= 0x20)
        HistoryIndex = 0;
    if (DrawEnabled != 0)
        DrawRegions();   // a tail jump in the original
}

// The frame mark RenderContext::EndFrame calls: the cycles since the last mark go to ProcessRegions (except on
// the first frame), the regions' counts are cleared, all under a timer on the frame region. The original guards
// the timer with an exception frame; nothing here throws.
// FUNC_AT(0x000f52d0)
void EAGL::ProfilerFrameMark() {
    uint32_t frameCycles = Rdtsc() - LastFrameTsc;   // RDTSC at 0x000f52ea
    LastFrameTsc = Rdtsc();                          // RDTSC at 0x000f52f4
    ProfilerTimer timer;
    timer.region = FrameRegion;
    timer.Enter();
    if (SkipFrame != 0)
        SkipFrame = 0;
    else
        ProfilerRegion::ProcessRegions(frameCycles);
    for (ProfilerRegion *region = RegionList; region != NULL; region = region->next)
        region->cycles = 0;
    timer.Destruct();
}

// FUNC_AT(0x000f5370)
void EAGL::ProfilerDrawGouraud::Begin(uint32_t type) {
    if (type != primitiveType) {
        InternalFlush();
        primitiveType = type;
        state.SetPrimitiveType(type);
    }
    EAGL_SetVertexShader(vertexShader);
    EAGL_SetPixelShader(pixelShader);
    if (Device::Get() != NULL) {
        ViewPort *viewPort;
        if (Device::Get()->GetCurrentTextureRenderContext() != NULL)
            viewPort = Device::Get()->GetCurrentTextureRenderContext()->GetCurrentViewPort();
        else
            viewPort = Device::Get()->GetCurrentRenderContext()->GetCurrentViewPort();
        alignas(16) Transform t;
        t.BuildMatrix(viewPort->GetViewProjectionMatrix());
        t.PrependMatrix(matrix);
        t.Transpose();
        EAGL_SetVertexShaderConstant(0, t.m, 4);   // c0..c3
    }
    state.Apply();
    D3DDevice_Begin(primitiveType);
    begun = 1;
}

// FUNC_AT(0x000f5440)
void __stdcall EAGL::ProfilerVertex(const float *vertex, const uint32_t *colour) {
    D3DDevice_SetVertexDataColor(kVertexDiffuse, *colour);
    D3DDevice_SetVertexData4f(kVertexPosition, vertex[0], vertex[1], vertex[2], vertex[3]);
}

// Starts this timer under the current one, pausing it - unless the current one times the frame region, when this
// one is left stopped.
// FUNC_AT(0x000f5480)
void EAGL::ProfilerTimer::Enter() {
    child = NULL;
    running = 0;
    ProfilerTimer *current = CurrentTimer;
    if (current->region == FrameRegion)
        return;
    parent = current;
    parent->child = this;
    ProfilerTimer *p = parent;
    if (p->running != 0) {
        uint32_t now = Rdtsc();   // RDTSC at 0x000f54c1
        p->region->cycles += now - p->start;
        p->running = 0;
    }
    if (running == 0) {
        CurrentTimer = this;
        start = Rdtsc();   // RDTSC at 0x000f54e5
        running = 1;
    }
}

// FUNC_AT(0x000f5500)
EAGL::ProfilerDrawArray* EAGL::ProfilerDrawArray::Construct() {
    primitiveType = 1;
    model = NULL;
    geoPrim = NULL;
    userVars = NULL;
    paramCount = 0;
    unknown14 = 0;
    locked = 0;
    drawVerts = 0;
    maxVerts = 0;
    unknown24 = 0;
    unknown28 = 0;
    streamParam = -1;
    primitiveClass = 0;
    dirty = 1;
    keepCounts = 0;
    numVerts = 0;
    void *memory = EaglMalloc(sizeof(DynamicModel), NameDynamicModelNew);
    model = memory != NULL ? static_cast<DynamicModel *>(memory)->Construct() : NULL;
    stream.noCopy = 0;
    stream.user = 0;
    stream.unknown04 = 0;
    stream.vertexCount = 0;
    return this;
}
