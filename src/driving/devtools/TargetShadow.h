#ifndef DRIVING_DEVTOOLS_TARGETSHADOW_H_
#define DRIVING_DEVTOOLS_TARGETSHADOW_H_

// NIGHTFIRE_TARGETSHADOW=1: world/Targeting.cpp's screen-space and distance maths, WTargetable's construction and
// small methods, the picker's selection steps, and the std::map and std::list code of world/SoundGroup.cpp and
// Targeting.cpp, against the originals with the live camera. Call once the track and its camera views exist (the
// first simulation tick); returns at once unless the variable is set.
void TargetShadow_Run(void);

#endif // DRIVING_DEVTOOLS_TARGETSHADOW_H_
