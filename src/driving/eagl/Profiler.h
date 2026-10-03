#ifndef DRIVING_EAGL_PROFILER_H_
#define DRIVING_EAGL_PROFILER_H_

// EAGL's PrintMessage and profiler (docs/driving/eagl.md 4.9): the ranges 0x000f42b0..0x000f4340 and
// 0x000f4580..0x000f55a0. See Profiler.cpp. In namespace EAGL, as in Ghidra; invented names are marked.

#include <stdint.h>

#include "RenderMethod.h"

namespace EAGL {

// Two 32-entry rings with running sums: cycles and the per-frame value derived from them
struct ProfilerHistory {             // 0x108
    float cycles[32];                // +0x00
    float cycleSum;                  // +0x80
    float values[32];                // +0x84
    float valueSum;                  // +0x104

    void SetHistoryValue(uint32_t cycleCount, float value, int index);       // 0x000f4580
};
static_assert(sizeof(ProfilerHistory) == 0x108, "a ProfilerRegion::History is 0x108 bytes");

struct ProfilerRegion {              // 0x128; the regions are a list at 0x0024053c
    const char *name;                // +0x00
    ProfilerHistory history;         // +0x04
    uint32_t colour;                 // +0x10c
    uint32_t cycles;                 // +0x110 cycles timed this frame (ProfilerTimer)
    uint32_t maxCycles;              // +0x114
    float maxFraction;               // +0x118
    uint32_t idleFrames;             // +0x11c frames without cycles, up to 32
    uint32_t flashTick;              // +0x120 TIMER_gettick when the region was last hidden
    ProfilerRegion *next;            // +0x124

    ProfilerRegion* Construct(const char *regionName, uint32_t regionColour); // 0x000f4f90 (invented)
    void Link();                                                             // 0x000f4e50 (invented)
    void Unlink();                                                           // 0x000f4e70 (invented)
    void UnlinkThunk();                                                      // 0x000f5010 (thunk to Unlink)
    void ProcessRegion(uint32_t frameCycles);                                // 0x000f46b0
    void DrawRegion(float x, float y, int index, int pass, float *total);    // 0x000f4760
    static void DrawRegions();                                               // 0x000f5020
    static void ProcessRegions(uint32_t frameCycles);                        // 0x000f5280
};
static_assert(sizeof(ProfilerRegion) == 0x128, "a ProfilerRegion is 0x128 bytes");

// A scoped timer: charges the cycles between its start and stop to its region, pausing its parent meanwhile
// (invented name; the current one is at 0x001ce024)
struct ProfilerTimer {               // 0x14
    ProfilerRegion *region;          // +0x00
    ProfilerTimer *parent;           // +0x04
    ProfilerTimer *child;            // +0x08
    uint32_t start;                  // +0x0c RDTSC's low half
    uint8_t running;                 // +0x10
    uint8_t pad11[3];

    ProfilerTimer* Construct(ProfilerRegion *timedRegion);                   // 0x000f5230 (invented)
    ProfilerTimer* ConstructMaybeStarted(ProfilerRegion *timedRegion, bool startNow); // 0x000f5250 (invented)
    void Enter();                                                            // 0x000f5480 (invented)
    void Start();                                                            // 0x000f4ec0 (invented)
    void Stop();                                                             // 0x000f4ef0 (invented)
    void Destruct();                                                         // 0x000f4f30 (invented)
};
static_assert(sizeof(ProfilerTimer) == 0x14, "a profiler timer is 0x14 bytes");

// EAGL::DrawGouraud's Begin, which lies in the profiler's range (the class is RenderMethod.h's)
struct ProfilerDrawGouraud : DrawGouraud {
    void Begin(uint32_t type);                                               // 0x000f5370 (invented)
};
static_assert(sizeof(ProfilerDrawGouraud) == sizeof(DrawGouraud), "the DrawGouraud itself");

// EAGL::DrawArray's constructor, which lies in the profiler's range (the class is RenderMethod.h's)
struct ProfilerDrawArray : DrawArray {
    ProfilerDrawArray* Construct();                                          // 0x000f5500
};
static_assert(sizeof(ProfilerDrawArray) == sizeof(DrawArray), "the DrawArray itself");

int PrintMessage(int level, const char *format, ...);                        // 0x000f42b0
void ProfilerSetVertexShaderConstantRegs();  // 0x000f4310 naked: EAX = count, ECX = register, EDX = data (invented)
void ProfilerResetMaxima();                                                  // 0x000f45f0 (invented)
void ProfilerAdvancePage(uint32_t count);                                    // 0x000f4620 (invented)
void ProfilerClearCycles();                                                  // 0x000f4680 (invented)
void ProfilerFrameMark();                                                    // 0x000f52d0 (invented)
void __stdcall ProfilerVertex(const float *vertex, const uint32_t *colour);  // 0x000f5440 (invented)

}  // namespace EAGL

#endif // DRIVING_EAGL_PROFILER_H_
