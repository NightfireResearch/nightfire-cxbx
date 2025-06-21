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
