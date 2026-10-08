#ifndef DRIVING_DEVTOOLS_PLAYERCAMSHADOWC_H_
#define DRIVING_DEVTOOLS_PLAYERCAMSHADOWC_H_

// NIGHTFIRE_PLAYERCAMSHADOWC=1: camera/PlayerCameraC.cpp against the originals on copies of the live player
// camera - the aim matrix, explosion shaking, the object and world collisions, and the heli, tumble and ellipse
// cameras. Call once the player camera exists (the first simulation tick); returns at once unless the variable
// is set.
void PlayerCamShadowC_Run(void);

#endif // DRIVING_DEVTOOLS_PLAYERCAMSHADOWC_H_
