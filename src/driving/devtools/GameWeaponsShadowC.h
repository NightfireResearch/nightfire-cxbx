#ifndef DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWC_H_
#define DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWC_H_

// NIGHTFIRE_GAMEWEAPONSSHADOWC=1: the shells and grenades (game/Shell.cpp, Grenade.cpp) against the originals, on
// synthetic objects in a faked Simulation and rays and steps against the live world round the player, every call
// out recorded by fakes, once, from the first simulation tick. See GameWeaponsShadowC.cpp.
void GameWeaponsShadowC_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWC_H_
