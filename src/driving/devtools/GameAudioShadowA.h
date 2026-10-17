#ifndef DRIVING_DEVTOOLS_GAMEAUDIOSHADOWA_H_
#define DRIVING_DEVTOOLS_GAMEAUDIOSHADOWA_H_

// NIGHTFIRE_GAMEAUDIOSHADOWA=1: the engine sound (game/VehicleSound.cpp, VehicleSoundEngine.cpp: AVehicle, AHorn,
// AVehicleWind, AEngine, ARaceEngine, the play parameters) against the originals, on private objects and on copies
// of the live vehicles' sounds, every call out of them recorded by fakes, once, from the first simulation tick. See
// GameAudioShadowA.cpp.
void GameAudioShadowA_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEAUDIOSHADOWA_H_
