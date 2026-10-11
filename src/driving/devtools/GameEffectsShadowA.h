#ifndef DRIVING_DEVTOOLS_GAMEEFFECTSSHADOWA_H_
#define DRIVING_DEVTOOLS_GAMEEFFECTSSHADOWA_H_

// NIGHTFIRE_GAMEEFFECTSSHADOWA=1: RVehicle, RTyreTrack, RDebris and RDebrisType (game/VehicleRender.cpp,
// TyreTrack.cpp, Debris.cpp) against the originals, on copies of the live cars' render objects and tyre tracks and
// of the debris, with the draws replaced by recording fakes. Call at the first simulation tick; returns at once
// unless the variable is set.
void GameEffectsShadowA_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEEFFECTSSHADOWA_H_
