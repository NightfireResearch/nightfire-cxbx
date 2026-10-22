#ifndef SWITCHCHANNELS_H_
#define SWITCHCHANNELS_H_

#include "../../actionhelpers.h"

// 256 bytes of per-level boolean state, cleared by Init_SwitchChannels on a cold level load - the wiring that
// most of a level's scripting is built out of. Nearly every use takes its channel number from the map
// data, so the number means whatever that level says it means; a few dozen are hard-coded in the
// engine instead, and those are tabulated in docs/switch-channels.md along with the reasons not to
// assume a hard-coded number means the same thing in every level.
extern char switch_channels[256];
extern char switch_channels_hold[256];
extern char switch_channels_prev[256];
extern uint32_t switch_channels_time[256];
extern ushort switch_channels_MusicVars[256]; // frames each channel has been on (DroneFunc_CheckAlarmRaised)

void Init_SwitchChannels(void);

void SwitchChannel_SetActive(int ch);
bool SwitchChannel_IsActive(int ch);

#endif // SWITCHCHANNELS_H_
