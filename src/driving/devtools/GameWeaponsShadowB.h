#ifndef DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWB_H_
#define DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWB_H_

// NIGHTFIRE_GAMEWEAPONSSHADOWB=1: the missiles, the explosions and the face normal (game/Missile.cpp,
// Explosion.cpp) against the originals, on synthetic objects, every call out of them recorded by fakes, once, from
// the first simulation tick. See GameWeaponsShadowB.cpp.
void GameWeaponsShadowB_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEWEAPONSSHADOWB_H_
