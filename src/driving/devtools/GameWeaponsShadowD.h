#ifndef DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWD_H_
#define DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWD_H_

// NIGHTFIRE_GAMEWEAPONSSHADOWD=1: the sentries, mines, smackables and humans (game/Sentry.cpp, Mine.cpp,
// Smackable.cpp, Human.cpp) against the originals, on synthetic objects (seeded from live ones where the mission
// has them), every call out recorded by fakes, once, from the first simulation tick. See GameWeaponsShadowD.cpp.
void GameWeaponsShadowD_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWD_H_
