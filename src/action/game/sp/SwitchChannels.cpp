#include "SwitchChannels.h"

// AUTOINJECT
void Init_SwitchChannels(void) {

    uint currentTime = GameState.NumFramesUnpaused;

    for (int i = 0; i < ARRAY_SIZE(switch_channels_MusicVars); i++) {
        switch_channels_MusicVars[i] = 0;
    }

    for (int i = 0; i < ARRAY_SIZE(switch_channels_time); i++) {
        switch_channels_time[i] = currentTime;
    }

    for(int i = 0; i < ARRAY_SIZE(switch_channels_hold); i++) {
        switch_channels_hold[i] = 0;
    }

    for (int i = 0; i < ARRAY_SIZE(switch_channels_prev); i++) {
        switch_channels_prev[i] = 0;
    }

    for(int i = 0; i < ARRAY_SIZE(switch_channels); i++) {
        switch_channels[i] = false;
    }

}

// Lots of places in the code follow a similar pattern: 
// if (ch != 0) {
//     switch_channels[ch] = 1;
//     switch_channels_time[ch] = GameState.NumFramesUnpaused;
// }
// I assume that these are all some helper function/macro which got inlined.
void SwitchChannel_SetActive(int ch) {

    // 0 is usually a special case for "no channel specified"
    // Inlined, original code doesn't usually check for in-bounds but we can.
    if(ch <= 0 || ch >= ARRAY_SIZE(switch_channels))
        return;

    switch_channels[ch] = 1;
    switch_channels_time[ch] = GameState.NumFramesUnpaused;
}

bool SwitchChannel_IsActive(int ch) {

    // Game code does not check channel is valid
    return switch_channels[ch];
    
}