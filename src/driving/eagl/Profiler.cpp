#include "Profiler.h"

#include "Realgraph.h"
#include "Transform.h"
#include "../platform/RealSystem.h"

#include <intrin.h>
#include <xmmintrin.h>
#include <stdarg.h>
#include <string.h>

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

// Constants in .rdata, as the original reads them
inline double K(uint32_t address) {
    return *(const float *)(uintptr_t)address;
}

const uint32_t kTwo32 = 0x0018a4a8;           // 4294967296.0f
const uint32_t kHalf = 0x00189eb0;            // 0.5f
const uint32_t kOne = 0x00189de8;             // 1.0f
const uint32_t kTwo = 0x00189e00;             // 2.0f
const uint32_t kThree = 0x0018a9f4;           // 3.0f
const uint32_t kFour = 0x00189ed4;            // 4.0f
const uint32_t kFive = 0x00189ff4;            // 5.0f
const uint32_t kThirtyTwo = 0x0018b8d0;       // 32.0f
const uint32_t kThirtyFour = 0x001a0b88;      // 34.0f
const uint32_t kForty = 0x0018a8ec;           // 40.0f
const uint32_t kFortyThree = 0x001a0b84;      // 43.0f
const uint32_t kFiftyTwo = 0x001904f4;        // 52.0f
const uint32_t kHundred = 0x0018b5b4;         // 100.0f
const uint32_t kBarHeight = 0x001a0b80;       // 1280.0f
const uint32_t kBarScale = 0x001a0b7c;        // 0.03125f
const uint32_t kPercentScale = 0x001a0b78;    // 0.00078125f
const uint32_t kBelowOne = 0x001a0b74;        // 0.995f
const uint32_t kBelowTen = 0x001a0b70;        // 9.95f

// The profiler's state
const uint32_t kHistoryIndex = 0x00240528, kDrawGouraud = 0x0024052c, kPageTick = 0x00240530,
               kPage = 0x00240534, kScreenWidth = 0x00240538, kViewPort = 0x00240540, kDrawEnabled = 0x00240544,
               kBarMode = 0x00240545, kLastFrameTsc = 0x00240548, kRegionList = 0x0024053c;
const uint32_t kFrameRegion = 0x00240560;     // the static region the frame mark times
const uint32_t kPaging = 0x001ce018, kStartX = 0x001ce01c, kStartY = 0x001ce020, kCurrentTimer = 0x001ce024,
               kSkipFrame = 0x001ce072;
const uint32_t kPageTicks = 0x00242424;       // TIMER ticks per page (and per flash)
const uint32_t kDefaultFont = 0x00241be0;

inline EAGL::ProfilerRegion *&RegionList() {
    return *(EAGL::ProfilerRegion **)(uintptr_t)kRegionList;
}

inline EAGL::ProfilerTimer *&CurrentTimer() {
    return *(EAGL::ProfilerTimer **)(uintptr_t)kCurrentTimer;
}

inline EAGL::ProfilerDrawGouraud *DrawGouraud() {
    return *(EAGL::ProfilerDrawGouraud **)(uintptr_t)kDrawGouraud;
}

inline const uint8_t *DefaultFont() {
    return *(const uint8_t **)(uintptr_t)kDefaultFont;
}

inline void *ProfilerViewPort() {
    return *(void **)(uintptr_t)kViewPort;
}

// RDTSC's low half (all the profiler keeps)
inline uint32_t Rdtsc() {
    return (uint32_t)__rdtsc();
}

inline void InternalFlush(EAGL::ProfilerDrawGouraud *drawGouraud) {
    ((void (__fastcall *)(void *, int))0x000f59f0)(drawGouraud, 0);   // DrawGouraud::InternalFlush (module F)
}

inline void SetVertexDataColor(uint32_t colour) {
    ((void (__stdcall *)(int, uint32_t))0x0016b9d0)(3, colour);   // D3DDevice_SetVertexDataColor(D3DVSDE_DIFFUSE)
}

inline void SetVertex(float x, float y) {
    ((void (__stdcall *)(int, float, float, float, float))0x0016b970)(-1, x, y, 0.0f, 1.0f);   // SetVertexData4f
}

// The end of each strip or list: DrawGouraud's begun flag cleared, D3DDevice_End, InternalFlush
inline void EndPrimitive() {
    EAGL::ProfilerDrawGouraud *drawGouraud = DrawGouraud();
    drawGouraud->begun = 0;
    ((void (__stdcall *)())0x0016ba60)();   // D3DDevice_End
    InternalFlush(drawGouraud);
}

void *DeviceGet() {
    return ((void *(*)())0x000e8a40)();   // EAGL::Device::Get
}

void *CurrentTextureRenderContext(void *device) {
    return ((void *(__fastcall *)(void *, int))0x000e89f0)(device, 0);
}

void *CurrentRenderContext(void *device) {
    return ((void *(__fastcall *)(void *, int))0x000e89e0)(device, 0);
}

}  // namespace

// FUNC_AT(0x000f42b0)
int EAGL::PrintMessage(int level, const char *format, ...) {
    if (level > *(const int32_t *)0x001cdcc8u)
        return 0;
    va_list arguments;
    va_start(arguments, format);
    int (*hook)(const char *, va_list) = *(int (**)(const char *, va_list))0x00240268u;
    if (hook != 0) {
        int result = hook(format, arguments);
        va_end(arguments);
        return result;
    }
    char *buffer = (char *)0x00240270u;
    int written = ((int (*)(char *, int, const char *, va_list))0x001340f5)(buffer, 0x200, format, arguments);
    buffer[0x1ff] = 0;
    ((void (__stdcall *)(const char *))0x0010e832)(buffer);   // XAPILIB::OutputDebugStringA
    va_end(arguments);
    return written;
}

// D3DDevice_SetVertexShaderConstant with the count in EAX, the register (less 0x60) in ECX and the data in EDX,
// the three entry points' own register convention: one and four constants go straight to their inline versions.
static const uint32_t kSetVertexShaderConstant1 = 0x0016a790;
static const uint32_t kSetVertexShaderConstant4 = 0x0016a7f0;
static const uint32_t kSetVertexShaderConstantNotInline = 0x0016a980;

// FUNC_AT(0x000f4310)
__declspec(naked) void EAGL::ProfilerSetVertexShaderConstantRegs() {
    __asm {
        add ecx, 0x60
        cmp eax, 1
        jne NotOne
        jmp dword ptr [kSetVertexShaderConstant1]
    NotOne:
        cmp eax, 4
        jne NotFour
        jmp dword ptr [kSetVertexShaderConstant4]
    NotFour:
        shl eax, 2
        push eax
        call dword ptr [kSetVertexShaderConstantNotInline]
        ret
    }
}

// FUNC_AT(0x000f4580)
void EAGL::ProfilerHistory::SetHistoryValue(uint32_t cycleCount, float value, int index) {
    cycleSum = (float)((double)cycleSum - (double)cycles[index]);
    double c = (double)cycleCount;   // FILD, plus 2^32 when the top bit is set: exact
    cycles[index] = (float)c;
    cycleSum = (float)(c + (double)cycleSum);   // the unrounded count (FST keeps it on the stack)
    valueSum = (float)((double)valueSum - (double)values[index]);
    values[index] = value;
    valueSum = (float)((double)value + (double)valueSum);
}

// FUNC_AT(0x000f45f0)
void EAGL::ProfilerResetMaxima() {
    for (ProfilerRegion *region = RegionList(); region != 0; region = region->next) {
        region->maxCycles = 0;
        region->maxFraction = 0.0f;
    }
}

// Turns the page of regions labelled every kPageTicks ticks while paging is on, back to the first page past the
// last; with paging off it only keeps the tick.
// FUNC_AT(0x000f4620)
void EAGL::ProfilerAdvancePage(uint32_t count) {
    if (U8(kPaging) != 0) {
        uint32_t interval = U32(kPageTicks);
        if ((uint32_t)TIMER_gettick() - U32(kPageTick) >= interval) {
            U32(kPageTick) = (uint32_t)TIMER_gettick();
            U32(kPage)++;
        }
    } else {
        U32(kPageTick) = (uint32_t)TIMER_gettick();
    }
    if (U32(kPage) >= count)
        U32(kPage) = 0;
}

// FUNC_AT(0x000f4680)
void EAGL::ProfilerClearCycles() {
    for (ProfilerRegion *region = RegionList(); region != 0; region = region->next)
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
        fraction = (float)((double)c / (double)frameCycles * K(kForty));
        if (fraction > maxFraction)   // FCOMP, TEST AH,41h: kept unless greater (NaN never replaces)
            maxFraction = fraction;
    }
    history.SetHistoryValue(c, fraction, (int)U32(kHistoryIndex));
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
    double width = F32(kScreenWidth);
    if (px > width - K(kThirtyTwo)) {   // FCOMPP, TEST AH,41h: on only when greater (ordered)
        float shapeX, shapeY, shapeW, shapeH, shapeNear, shapeFar;
        ((void (__fastcall *)(void *, int, float *, float *, float *, float *, float *, float *))0x000e4680)(
            ProfilerViewPort(), 0, &shapeX, &shapeY, &shapeW, &shapeH, &shapeNear, &shapeFar);   // ViewPort::GetShape
        float limit = (float)((double)F32(kScreenWidth) - K(kThirtyTwo));
        px = x;
        if (px > limit) {
            xSlot = (float)((double)shapeH * K(kHalf) + (double)shapeY);   // the viewport's middle, in the x slot
            float columnWidth = (float)((double)limit - K(kStartX));
            py = y;
            do {
                if (F32(kStartY) > xSlot)
                    py = py - K(kFiftyTwo);
                else
                    py = py + K(kFiftyTwo);
                px = px - (double)columnWidth;
            } while (px > limit);
            xSlot = (float)px;
        } else {
            py = y;
        }
    } else {
        py = y;
    }
    float penX = (float)px;   // [esp+0xc]
    float penY = (float)py;   // [esp+0x14]
    float rectX = 0.0f, rectY = 0.0f, rectW = 0.0f, rectH = 0.0f;
    uint32_t colour;
    if (index == (int)U32(kPage) && DefaultFont() != 0) {
        colour = 0xff505050;
        label = true;
        FONT_getrectx_thunk(DefaultFont(), (const uint8_t *)name, &rectX, &rectY, &rectW, &rectH);
    } else {
        colour = 0xff000000;
    }
    if (flashTick != 0) {
        uint32_t now = (uint32_t)TIMER_gettick();
        if (now - flashTick >= U32(kPageTicks))
            flashTick = 0;
        else if ((now & 8) != 0)
            colour = 0xffaa0000;
    }
    float vertex[4];   // x, y, z, w for ProfilerVertex
    vertex[2] = 0.0f;
    vertex[3] = 1.0f;
    float scratch;     // [esp+0x34]: the swatch's right edge, a vertex colour, the graph's offset
    if (pass == 0) {
        DrawGouraud()->Begin(6);
        SetVertexDataColor(colour);
        SetVertex(penX, penY);
        double right = (double)penX + K(kThirtyTwo);
        scratch = (float)right;
        vertex[0] = (float)right;
        SetVertexDataColor(colour);
        SetVertex(vertex[0], penY);
        vertex[1] = (float)((double)penY + K(kForty));
        SetVertexDataColor(colour);
        SetVertex(penX, vertex[1]);
        vertex[0] = scratch;
        SetVertexDataColor(colour);
        SetVertex(vertex[0], vertex[1]);
        EndPrimitive();
        if (label && DefaultFont() != 0) {
            if ((double)F32(kScreenWidth) * K(kHalf) < (double)penX)   // TEST AH,5 / JP: on when less (ordered)
                penX = (float)((K(kThirtyTwo) - (double)rectW) + (double)penX);
            DrawGouraud()->Begin(6);
            uint32_t black = 0xff000000;
            vertex[1] = (float)((double)penY + K(kFortyThree));
            vertex[0] = penX;
            ProfilerVertex(vertex, &black);
            vertex[0] = (float)((double)rectW + (double)penX);
            ProfilerVertex(vertex, &black);
            vertex[1] = (float)((double)vertex[1] + (double)rectH);
            vertex[0] = penX;
            ProfilerVertex(vertex, &black);
            vertex[0] = (float)((double)rectW + (double)penX);
            ProfilerVertex(vertex, &black);
            EndPrimitive();
        }
        *total = (float)((double)history.valueSum + (double)*total);
    }
    penY = (float)((double)penY + K(kForty));
    if (pass == 1) {
        if (U8(kBarMode) != 0) {
            // A bar up from the pen, in the region's colour
            float bar = history.valueSum;
            if (this == (ProfilerRegion *)(uintptr_t)kFrameRegion)
                bar = (float)((K(kBarHeight) - (double)*total) + (double)bar);
            DrawGouraud()->Begin(6);
            SetVertexDataColor(this->colour);
            SetVertex(penX, penY);
            double right = (double)penX + K(kThirtyTwo);
            scratch = (float)right;
            vertex[0] = (float)right;
            SetVertexDataColor(this->colour);
            SetVertex(vertex[0], penY);
            vertex[1] = (float)((double)penY - (double)bar * K(kBarScale));
            SetVertexDataColor(this->colour);
            SetVertex(penX, vertex[1]);
            vertex[0] = scratch;
            SetVertexDataColor(this->colour);
            SetVertex(vertex[0], vertex[1]);
        } else {
            // The history as vertical lines, one a frame from the oldest, a column of kOne apart
            scratch = 0.0f;
            if (this == (ProfilerRegion *)(uintptr_t)kFrameRegion)
                scratch = (float)((K(kBarHeight) - (double)*total) * K(kBarScale));
            DrawGouraud()->Begin(2);
            uint32_t i = U32(kHistoryIndex);
            float lineY = penY;
            for (;;) {
                SetVertexDataColor(this->colour);
                SetVertex(penX, lineY);
                vertex[1] = (float)((double)penY - ((double)scratch + (double)history.values[i]));
                SetVertexDataColor(this->colour);
                SetVertex(penX, vertex[1]);
                i++;
                penX = (float)((double)penX + K(kOne));
                if (i >= 0x20)
                    i = 0;
                if (i == U32(kHistoryIndex))
                    break;
            }
        }
        EndPrimitive();
    }
    InternalFlush(DrawGouraud());
    if (pass != 2)
        return;
    const uint8_t *font = DefaultFont();
    if (font == 0)
        return;
    // The figure: per cent of the frame, or what is left of it for the frame region
    double sum = history.valueSum;
    if (this == (ProfilerRegion *)(uintptr_t)kFrameRegion)
        sum = sum + (K(kBarHeight) - (double)*total);
    double percent = sum * K(kHundred) * K(kPercentScale);
    float textX = (float)((double)penX + K(kTwo));
    float textY = (float)(((double)penY - K(kForty)) + K(kFive));
    if (percent < K(kBelowOne)) {   // TEST AH,5 / JP: on when less (ordered)
        float hundredths = (float)(percent * K(kHundred));
        int value = _mm_cvtss_si32(_mm_set_ss(hundredths));   // CVTSS2SI: rounded as MXCSR says (to nearest)
        FONT_drawtextfa(font, textX, textY, (const char *)0x001ce040u, value);   // ".%02d"
    } else if (percent < K(kBelowTen)) {
        FONT_drawtextfa(font, textX, textY, (const char *)0x001ce048u, percent);   // "%.1f"
    } else {
        FONT_drawtextfa(font, (float)((double)textX + K(kFour)), textY, (const char *)0x001ce050u,
                        percent);   // "%.0f"
    }
    if (!label)
        return;
    if ((double)F32(kScreenWidth) * K(kHalf) < (double)xSlot)
        xSlot = (float)((K(kThirtyTwo) - (double)rectW) + (double)xSlot);
    FONT_drawtextfa(DefaultFont(), xSlot, (float)((double)penY + K(kThree)), (const char *)0x001ce058u,
                    name);   // "%s"
}

// FUNC_AT(0x000f4e50)
void EAGL::ProfilerRegion::Link() {
    next = RegionList();
    RegionList() = this;
}

// FUNC_AT(0x000f4e70)
void EAGL::ProfilerRegion::Unlink() {
    ProfilerRegion *p = RegionList();
    if (p == this) {
        RegionList() = next;
        return;
    }
    if (p->next != this) {
        for (;;) {
            ProfilerRegion *n = p->next;
            if (n == 0)
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
        CurrentTimer() = this;
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
            CurrentTimer() = p;
            p->start = Rdtsc();   // RDTSC at 0x000f4f75
            p->running = 1;
        }
    } else if (child != 0) {
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
    next = RegionList();
    RegionList() = this;
    return this;
}

// FUNC_AT(0x000f5020)
void EAGL::ProfilerRegion::DrawRegions() {
    float total = 0.0f;
    if (ProfilerViewPort() == 0) {
        void *context = CurrentRenderContext(DeviceGet());
        U32(kViewPort) = (uint32_t)(uintptr_t)((void *(__fastcall *)(void *, int))0x000ee010)(context, 0);  // NewViewPort
        float width, height;
        ((void (__fastcall *)(void *, int, float *, float *))0x000e6a80)(context, 0, &width, &height);   // GetSize
        ((void (__fastcall *)(void *, int, float, float, float, float, float, float))0x000e4340)(
            ProfilerViewPort(), 0, 0.0f, 0.0f, width, height, 0.0f, 1.0f);   // ViewPort::SetShape
        ((void (__fastcall *)(void *, int, float, float))0x000e4900)(ProfilerViewPort(), 0, 0.0f, 1.0f);  // SetOrthographic
        F32(kScreenWidth) = width;
    }
    if (U32(kDrawGouraud) == 0) {
        void *memory = (*(void *(**)(uint32_t, const char *))0x001caf68u)(0x9c, (const char *)0x001ce05cu);
        void *drawGouraud = memory != 0 ? ((void *(__fastcall *)(void *, int))0x000f58b0)(memory, 0) : 0;
        U32(kDrawGouraud) = (uint32_t)(uintptr_t)drawGouraud;
        ((void (__fastcall *)(void *, int))0x000f5970)(drawGouraud, 0);   // DrawGouraud::Init
        DrawGouraud()->state.SetDepthTestMethod(0x207);                    // GL_ALWAYS
    }
    ((void (__fastcall *)(void *, int))0x000e4be0)(ProfilerViewPort(), 0);   // ViewPort::BeginView
    if (DefaultFont() != 0)
        *(uint32_t *)(DefaultFont() + 0x20) = 0xffaaaaaa;
    uint32_t drawn = 0;
    for (int pass = 0; pass < 3; pass++) {
        float x = F32(kStartX);
        float y = F32(kStartY);
        drawn = 0;
        for (ProfilerRegion *region = RegionList(); region != 0; region = region->next) {
            if (region->idleFrames < 0x20 &&
                (region->flashTick == 0 || region->history.valueSum > F32(kHalf))) {
                region->DrawRegion(x, y, (int)drawn, pass, &total);
                x = (float)((double)x + K(kThirtyFour));
                drawn++;
            } else {
                region->flashTick = (uint32_t)TIMER_gettick();
            }
        }
        InternalFlush(DrawGouraud());
    }
    ((void (__fastcall *)(void *, int))0x000e49a0)(ProfilerViewPort(), 0);   // ViewPort::EndView
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
    child = 0;
    return this;
}

// FUNC_AT(0x000f5280)
void EAGL::ProfilerRegion::ProcessRegions(uint32_t frameCycles) {
    for (ProfilerRegion *region = RegionList(); region != 0; region = region->next)
        region->ProcessRegion(frameCycles);
    uint32_t index = U32(kHistoryIndex) + 1;
    U32(kHistoryIndex) = index;
    if (index >= 0x20)
        U32(kHistoryIndex) = 0;
    if (U8(kDrawEnabled) != 0)
        DrawRegions();   // a tail jump in the original
}

// The frame mark RenderContext::EndFrame calls: the cycles since the last mark go to ProcessRegions (except on
// the first frame), the regions' counts are cleared, all under a timer on the frame region. The original guards
// the timer with an exception frame; nothing here throws.
// FUNC_AT(0x000f52d0)
void EAGL::ProfilerFrameMark() {
    uint32_t frameCycles = Rdtsc() - U32(kLastFrameTsc);   // RDTSC at 0x000f52ea
    U32(kLastFrameTsc) = Rdtsc();                          // RDTSC at 0x000f52f4
    ProfilerTimer timer;
    timer.region = (ProfilerRegion *)(uintptr_t)kFrameRegion;
    timer.Enter();
    if (U8(kSkipFrame) != 0)
        U8(kSkipFrame) = 0;
    else
        ProfilerRegion::ProcessRegions(frameCycles);
    for (ProfilerRegion *region = RegionList(); region != 0; region = region->next)
        region->cycles = 0;
    timer.Destruct();
}

// FUNC_AT(0x000f5370)
void EAGL::ProfilerDrawGouraud::Begin(uint32_t type) {
    if (type != primitiveType) {
        InternalFlush(this);
        primitiveType = type;
        state.SetPrimitiveType(type);
    }
    ((void (*)(void *))0x000f4350)(vertexShader);   // module F's SetVertexShader wrapper
    ((void (*)(void *))0x000f4380)(pixelShader);    // and SetPixelShader
    if (DeviceGet() != 0) {
        void *viewPort;
        if (CurrentTextureRenderContext(DeviceGet()) != 0)
            viewPort = ((void *(__fastcall *)(void *, int))0x000f3520)(CurrentTextureRenderContext(DeviceGet()), 0);
        else
            viewPort = ((void *(__fastcall *)(void *, int))0x000ee080)(CurrentRenderContext(DeviceGet()), 0);
        const float *viewProjection = ((const float *(__fastcall *)(void *, int))0x000f39b0)(viewPort, 0);
        alignas(16) Transform t;
        t.BuildMatrix(viewProjection);
        t.PrependMatrix(matrix);
        t.Transpose();
        ((void (*)(int, const float *, int))0x000f43a0)(0, t.m, 4);   // vertex shader constants c0..c3
    }
    state.Apply();
    ((void (__stdcall *)(uint32_t))0x0016ba20)(primitiveType);   // D3DDevice_Begin
    begun = 1;
}

// FUNC_AT(0x000f5440)
void __stdcall EAGL::ProfilerVertex(const float *vertex, const uint32_t *colour) {
    SetVertexDataColor(*colour);
    ((void (__stdcall *)(int, float, float, float, float))0x0016b970)(-1, vertex[0], vertex[1], vertex[2], vertex[3]);
}

// Starts this timer under the current one, pausing it - unless the current one times the frame region, when this
// one is left stopped.
// FUNC_AT(0x000f5480)
void EAGL::ProfilerTimer::Enter() {
    child = 0;
    running = 0;
    ProfilerTimer *current = CurrentTimer();
    if (current->region == (ProfilerRegion *)(uintptr_t)kFrameRegion)
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
        CurrentTimer() = this;
        start = Rdtsc();   // RDTSC at 0x000f54e5
        running = 1;
    }
}

// FUNC_AT(0x000f5500)
EAGL::ProfilerDrawArray* EAGL::ProfilerDrawArray::Construct() {
    field00 = 1;
    dynamicModel = 0;
    field08 = 0;
    field0c = 0;
    field10 = 0;
    field14 = 0;
    field18 = 0;
    field1c = 0;
    field20 = 0;
    field24 = 0;
    field28 = 0;
    field2c = -1;
    field30 = 0;
    field34 = 1;
    field35 = 0;
    field38 = 0;
    void *memory = (*(void *(**)(uint32_t, const char *))0x001caf68u)(0x58, (const char *)0x001ce3b0u);
    void *model = memory != 0 ? ((void *(__fastcall *)(void *, int))0x000e9760)(memory, 0) : 0;  // DynamicModel
    dynamicModel = model;
    field48 = 0;
    field44 = 0;
    field40 = 0;
    field3c = 0;
    return this;
}
