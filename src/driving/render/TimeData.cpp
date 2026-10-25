#include "TimeData.h"

#include "../engine/CoreFoundation.h"    // GetFoundationVideoModeRate
#include "../game/Simulation.h"

#pragma fp_contract(off)

// RTimeData (0x00095380, 0x000953a0), ported from the listing.


// XBE_GLOBAL(0x001f2a44, 0x4)
float VideoModeRate;
// XBE_GLOBAL(0x001f2a48, 4)
float TickSeconds;
// XBE_GLOBAL(0x001c465c, 0x10)
alignas(16) SimStepBlock SimStep = { 0.0f, -0.002f, 0.0f, 0.0f };
// XBE_GLOBAL(0x001c467c, 0x10)
alignas(16) float CausticControl[4] = { 0.014f, 0.45f, 0.54f, 0.64f };

// FUNC_AT(0x00095380)
void RTimeData::Init() {
    VideoModeRate = GetFoundationVideoModeRate();
    TickSeconds = 1.0f / VideoModeRate;
}

// FUNC_AT(0x000953a0)
void RTimeData::Update() {
    uint32_t count = fgSimulation.stepCount;
    GameTick = count;
    SimStep.step = (float)(double)count;
    CausticControl[3] = CausticControl[0] * SimStep.step;
}
