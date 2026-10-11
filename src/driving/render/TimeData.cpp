#include "TimeData.h"

#include "../engine/CoreFoundation.h"    // GetFoundationVideoModeRate

#pragma fp_contract(off)

// RTimeData (0x00095380, 0x000953a0), ported from the listing.

#define SimStepCount U32_AT(0x00234e34)

// XBE_GLOBAL(0x001f2a48, 4)
float TickSeconds;

// FUNC_AT(0x00095380)
void RTimeData::Init() {
    VideoModeRate = GetFoundationVideoModeRate();
    TickSeconds = 1.0f / VideoModeRate;
}

// FUNC_AT(0x000953a0)
void RTimeData::Update() {
    uint32_t count = SimStepCount;
    GameTick = count;
    SimStep = (float)(double)count;
    CausticControl[3] = CausticControl[0] * SimStep;
}
