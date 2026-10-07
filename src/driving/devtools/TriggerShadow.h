#ifndef DRIVING_DEVTOOLS_TRIGGERSHADOW_H_
#define DRIVING_DEVTOOLS_TRIGGERSHADOW_H_

// NIGHTFIRE_TRIGGERSHADOW=1: the triggers' tests and processing (world/Trigger.cpp, TriggerManager.cpp,
// SimpleZone.cpp) against the originals, over the loaded track's triggers. See TriggerShadow.cpp. Run from the
// first simulation tick, once the track's triggers are loaded.
void TriggerShadow_Run(void);

#endif // DRIVING_DEVTOOLS_TRIGGERSHADOW_H_
