#ifndef DRIVING_DEVTOOLS_PLAYERCAMSHADOWA_H_
#define DRIVING_DEVTOOLS_PLAYERCAMSHADOWA_H_

// NIGHTFIRE_PLAYERCAMSHADOWA=1: camera/PlayerCameraA.cpp (RPlayerCamera, 0x00080a60..0x00083190) against the
// originals, on copies of the live player camera with perturbed modes, positions, inputs and state. Call once the
// player camera exists (the first simulation tick); returns at once unless the variable is set.
void PlayerCamShadowA_Run(void);

#endif // DRIVING_DEVTOOLS_PLAYERCAMSHADOWA_H_
