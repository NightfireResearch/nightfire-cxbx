#pragma once

#include "../common/xbeClass.h"
#include "Schedule.hpp"

// The game's scheduler (Ghidra: Scheduler, 36 bytes), an overlay class - see src/common/xbeClass.h.
class Scheduler {

public:
    Schedule *s_oncePerGameLoop;
    Schedule *s_SimRate;
    Schedule *s_halfSimRate;
    Schedule *s_quarterSimRate;
    void *listOfSchedules;
    int lastTickCount;
    int maybeElapsedTime;
    int cinematicModeSkippingAndStuff;   // the byte at +0x1d is the cinematic skip flag
    float timeScale;

    void Run(int i);
};

XBE_CLASS_SIZE(Scheduler, 0x24);
XBE_FIELD(Scheduler, s_SimRate, 0x04);
XBE_FIELD(Scheduler, listOfSchedules, 0x10);
XBE_FIELD(Scheduler, lastTickCount, 0x14);
XBE_FIELD(Scheduler, timeScale, 0x20);
