#ifndef DRIVING_RENDER_TIMEDATA_H_
#define DRIVING_RENDER_TIMEDATA_H_

// ---------------------------------------------------------------------------------------------------------------
// RTimeData: the renderer's clock values - the video mode's rate and tick length, set once when the renderer is
// made, and each frame the game's tick and the data blocks that move with it ("GAME::SimStep" and the caustics',
// which CARP data reaches through the symbol table, data/RCARPFile.cpp). See TimeData.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stdint.h>

#include "../../helpers.h"
#include "../data/XbeData.h"

extern float VideoModeRate;                                 // frames a second
extern float TickSeconds;                                   // 1 / VideoModeRate
extern uint32_t GameTick;                                   // the simulation's step count, as of this frame (camera/WorldCamera.cpp)

// "GAME::SimStep": CARP data and the EMP's render methods read it as four floats
struct SimStepBlock {
    float step;                         // +0x0 GameTick, as a float
    float unknown4;                     // +0x4 a constant of the XBE's
    float unknown8;                     // +0x8
    float frameAnimationTime;           // +0xc ESetFrameAnimationTime's tick
};
static_assert(sizeof(SimStepBlock) == 0x10, "GAME::SimStep is four floats");
extern SimStepBlock SimStep;

#define CausticControl ((float *)0x001c467c)                // its [3] is its [0] times the step count

class RTimeData {
public:
    // VideoModeRate and TickSeconds (RRenderer's constructor)
    static void Init();                                                                             // 0x00095380
    // GameTick, SimStep and CausticControl[3] (RRenderHigh::Render, each frame)
    static void Update();                                                                           // 0x000953a0
};

#endif // DRIVING_RENDER_TIMEDATA_H_
