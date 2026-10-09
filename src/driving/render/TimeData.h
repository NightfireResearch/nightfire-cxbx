#ifndef DRIVING_RENDER_TIMEDATA_H_
#define DRIVING_RENDER_TIMEDATA_H_

// ---------------------------------------------------------------------------------------------------------------
// RTimeData: the renderer's clock values - the video mode's rate and tick length, set once when the renderer is
// made, and each frame the game's tick and the data blocks that move with it ("GAME::SimStep" and the caustics',
// which CARP data reaches through the symbol table, data/RCARPFile.cpp). See TimeData.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stdint.h>

#include "../../helpers.h"

#define VideoModeRate FLOAT_AT(0x001f2a44)                  // frames a second
#define GameTick U32_AT(0x001f2a4c)                         // the simulation's step count, as of this frame
#define SimStep FLOAT_AT(0x001c465c)                        // "GAME::SimStep"'s first float: the same, as a float
#define CausticControl ((float *)0x001c467c)                // its [3] is its [0] times the step count

class RTimeData {
public:
    // VideoModeRate and TickSeconds (RRenderer's constructor)
    static void Init();                                                                             // 0x00095380
    // GameTick, SimStep and CausticControl[3] (RRenderHigh::Render, each frame)
    static void Update();                                                                           // 0x000953a0
};

#endif // DRIVING_RENDER_TIMEDATA_H_
