#ifndef DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWA_H_
#define DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWA_H_

// NIGHTFIRE_GAMEWEAPONSSHADOWA=1: the weapon manager (game/WeaponManager.cpp) against the originals, on perturbed
// copies of the live manager, every call out of it recorded by fakes, once, from the first simulation tick. See
// GameWeaponsShadowA.cpp.
void GameWeaponsShadowA_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWA_H_
