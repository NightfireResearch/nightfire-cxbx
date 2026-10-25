#ifndef DRIVING_DEVTOOLS_ANIMENGINESHADOW_H_
#define DRIVING_DEVTOOLS_ANIMENGINESHADOW_H_

// NIGHTFIRE_ANIMENGINESHADOW=1: RAnimEngine, its handles and the proc-anim functions (anim/AnimEngine.cpp,
// ProcAnim.cpp) against the originals, on the loaded track's instances and on synthetic animated ones. See
// AnimEngineShadow.cpp. Run from the first simulation tick, once the track is loaded.
void AnimEngineShadow_Run(void);

#endif // DRIVING_DEVTOOLS_ANIMENGINESHADOW_H_
