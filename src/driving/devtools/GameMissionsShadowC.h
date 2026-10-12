#ifndef DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWC_H_
#define DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWC_H_

// NIGHTFIRE_GAMEMISSIONSSHADOWC=1: the mission manager (game/MissionManager.cpp, game/MissionScores.cpp) against
// the originals, on a perturbed copy of the live mission manager with synthetic lists, objectives, AI elements and
// rules, every call out of it recorded by fakes, once, from the first simulation tick. See GameMissionsShadowC.cpp.
void GameMissionsShadowC_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEMISSIONSSHADOWC_H_
