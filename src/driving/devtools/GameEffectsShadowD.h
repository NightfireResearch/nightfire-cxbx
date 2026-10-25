#ifndef DRIVING_DEVTOOLS_GAMEEFFECTSSHADOWD_H_
#define DRIVING_DEVTOOLS_GAMEEFFECTSSHADOWD_H_

// NIGHTFIRE_GAMEEFFECTSSHADOWD=1: the weapon effects and the weapon cameras (game/WeaponEffects.cpp,
// WeaponCameras.cpp) against the originals, on synthetic objects and copies of the live ones, every draw and
// decal recorded by fakes, once, from the first simulation tick. See GameEffectsShadowD.cpp.
void GameEffectsShadowD_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEEFFECTSSHADOWD_H_
