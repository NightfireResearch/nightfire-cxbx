#ifndef DRIVING_DEVTOOLS_GAMEAUDIOSHADOWB_H_
#define DRIVING_DEVTOOLS_GAMEAUDIOSHADOWB_H_

// NIGHTFIRE_GAMEAUDIOSHADOWB=1: the vehicle kinds' sounds (game/VehicleSoundKinds.cpp: APlayerVehicle, APlayerHeli,
// AUltraLite, ASnowMobile, ASubmersible, APlayerTank, AHelicopter, ATrafficVehicle) against the originals. See
// GameAudioShadowB.cpp.
//
// GameAudioShadowB_Run: from the first simulation tick (the vehicles have made their sounds then).
void GameAudioShadowB_Run(void);

#endif // DRIVING_DEVTOOLS_GAMEAUDIOSHADOWB_H_
