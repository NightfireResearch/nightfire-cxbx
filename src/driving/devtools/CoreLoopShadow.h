#ifndef DRIVING_DEVTOOLS_CORELOOPSHADOW_H_
#define DRIVING_DEVTOOLS_CORELOOPSHADOW_H_

// NIGHTFIRE_CORELOOPSHADOW=1: the scheduler's task lists, SimRandom, Noise, MissionNumToString and OptionParser
// against the originals (Scheduler.cpp, engine/SimRandom.cpp, engine/GameLoop.cpp). See CoreLoopShadow.cpp. Run
// at injection time, before the game.
void CoreLoopShadow_Run(void);

#endif // DRIVING_DEVTOOLS_CORELOOPSHADOW_H_
