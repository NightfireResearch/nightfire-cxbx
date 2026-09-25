#pragma once

#include "../common/xbeClass.h"

// One of the scheduler's task lists (Ghidra: Schedule, 44 bytes). An overlay class - see src/common/xbeClass.h:
// the game's own instances, reinterpreted, with its methods either reimplemented or, as here, declared
// AUTOGEN so that calling one reaches the original.
struct Schedule {
    void **vtable;             // Schedule is virtual; Process is the second entry
    Schedule *next;
    void *listOfTasks[8];      // one task list per priority bucket
    unsigned int numTasks;

    // Runs the tasks in one bucket whose spacing matches `priority` (0x0005bce0).
    // AUTOGEN
    void RunTasks(int bucket, unsigned short priority);
};

XBE_CLASS_SIZE(Schedule, 0x2c);
XBE_FIELD(Schedule, next, 0x04);
XBE_FIELD(Schedule, listOfTasks, 0x08);
XBE_FIELD(Schedule, numTasks, 0x28);
