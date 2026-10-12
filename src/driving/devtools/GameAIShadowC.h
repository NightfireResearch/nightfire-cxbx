#ifndef DRIVING_DEVTOOLS_GAMEAISHADOWC_H_
#define DRIVING_DEVTOOLS_GAMEAISHADOWC_H_

// NIGHTFIRE_GAMEAISHADOWC=1: the AI characters' base, the passenger and the pedestrians (game/AICharacter.cpp,
// AIPedestrian.cpp) against the originals, on a synthetic character and a synthetic pedestrian (copied from a live
// one where the level has pedestrians) among the live pedestrians, every call out recorded by fakes, once, from the
// first simulation tick. See GameAIShadowC.cpp.
void GameAIShadowC_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEAISHADOWC_H_
