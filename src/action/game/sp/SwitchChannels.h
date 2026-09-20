#ifndef SWITCHCHANNELS_H_
#define SWITCHCHANNELS_H_

#include "../../actionhelpers.h"

// 256 bytes of per-level boolean state, cleared by Init_SwitchChannels at level load - the wiring that
// most of a level's scripting is built out of. Nearly every use takes its channel number from the map
// data, so the number means whatever that level says it means; a few dozen are hard-coded in the
// engine instead, and those are tabulated in docs/switch-channels.md along with the reasons not to
// assume a hard-coded number means the same thing in every level.
#define switch_channels (*(char(*)[256])0x001df138)
#define switch_channels_hold (*(char(*)[256])0x001df238)
#define switch_channels_prev (*(char(*)[256])0x001dee38)
#define switch_channels_time (*(uint32_t(*)[256])0x001df428)
#define switch_channels_MusicVars (*(ushort(*)[256])0x001def38)

void Init_SwitchChannels(void);

void SwitchChannel_SetActive(int ch);
bool SwitchChannel_IsActive(int ch);

#endif // SWITCHCHANNELS_H_