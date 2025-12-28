#ifndef SWITCHCHANNELS_H_
#define SWITCHCHANNELS_H_

#include "../../actionhelpers.h"

#define switch_channels (*(char(*)[256])0x001df138)
#define switch_channels_hold (*(char(*)[256])0x001df238)
#define switch_channels_prev (*(char(*)[256])0x001dee38)
#define switch_channels_time (*(uint32_t(*)[256])0x001df428)
#define switch_channels_MusicVars (*(ushort(*)[256])0x001def38)

void Init_SwitchChannels(void);

void SwitchChannel_SetActive(int ch);
bool SwitchChannel_IsActive(int ch);

#endif // SWITCHCHANNELS_H_