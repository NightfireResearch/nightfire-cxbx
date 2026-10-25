#ifndef DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWD_H_
#define DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWD_H_

// NIGHTFIRE_GAMEMISSIONSSHADOWD=1: the mission rules and timers (game/MissionRules.cpp) against the originals, on
// synthetic rules of every kind seeded from the live player, mission manager and weapon manager, every call out of
// them recorded by fakes, once, from the first simulation tick. See GameMissionsShadowD.cpp.
void GameMissionsShadowD_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWD_H_
