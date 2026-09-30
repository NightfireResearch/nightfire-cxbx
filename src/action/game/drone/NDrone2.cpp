#include "NDrone2.h"
#include "../sp/SwitchChannels.h" // switch_channels, switch_channels_MusicVars
#include "../../sound/music.h"    // Music_Event (ours, sound/music.cpp)
#include "../../game.h"           // GameState

// AUTOGEN
obj_tag* NDrone2_CreateObj(DIVars_tag *diVars); 

// AUTOGEN
obj_tag* NDrone2_CreateFromDIVars(DIVars_tag *diVars);

// NOAUTOINJECT
obj_tag* NDrone2_CreateFromDIVars1(DIVars_tag *diVars) {
    
    // if(diVars == NULL)
    //     return NULL;

    // obj_tag *newObj = NDrone2_CreateObj(diVars);

    // diVars->gameObj = newObj;

    // if(newObj == NULL)
    //     return NULL;

    // Drone_tag *newDrone = (Drone_tag*)diVars->gameObj->extraObjectData;
    
    // newDrone->creationTimeFrames = Rand_Rand(10000); // Some AI seed?

    // (newDrone->diVars).gameObj = newObj;
    // Vec_Copy(&diVars->position, &(newDrone->diVars).position);
    // Vec_Copy(&diVars->rotation, &(newDrone->diVars).rotation);
    // memcpy()

    // newDrone->someField1 = (diVars->animSkin).someThing1;
    // newDrone->someField2 = (diVars->animSkin).someThing2;

    // return newObj;
    return NULL;
}

// Music event 2 is the alarm music. Its value picks the cue: 3 for the castle's own alarm (channel 0xa4,
// CastleExterior only), 1 for the generic "alarm raised" channels 0x37 and 0x61.
#define MUSIC_EVENT_ALARM 2
#define ALARM_MUSIC_CASTLE 3
#define ALARM_MUSIC_GENERIC 1

// switch_channels_MusicVars[ch] counts the frames a channel has been on, for this function only: it is the
// last user, and Init_SwitchChannels clears it per level. The original inlines this three times. A counter
// at 0 goes straight to 2 (set to 1, then incremented - so the first frame already counts as two), and it
// sticks at 0xffff rather than wrapping. Nothing ever resets it while the channel stays on, and nothing
// here resets it when the channel goes off either, so each cue can only play once per level.
static ushort DroneFunc_TickAlarmTimer(int ch) {
    ushort frames = switch_channels_MusicVars[ch];
    if (frames == 0)
        frames = 1;
    if (frames < 0xffff)
        frames++;
    switch_channels_MusicVars[ch] = frames; // the original skips this store when saturated; same value
    return frames;
}

// Called every frame. While an alarm channel is on, keep asserting the alarm music event: for the first
// second of the castle alarm (0xa4, CastleExterior only), else for the first ten seconds of channel 0x37
// (both castle exterior levels) or channel 0x61 (any level). The earlier checks win and return, so a later
// channel's timer only advances on frames when no earlier cue fired.
//
// AUTOINJECT
void DroneFunc_CheckAlarmRaised(void) {

    uint frameRate = FRAME_RATE_INT; // read once, before any of the branches, as the original does

    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_CastleExterior:
        if (switch_channels[0xa4] != 0 && DroneFunc_TickAlarmTimer(0xa4) < frameRate) {
            Music_Event(MUSIC_EVENT_ALARM, ALARM_MUSIC_CASTLE);
            return;
        }
        // fall through: the castle alarm has run its second (or is off), try channel 0x37
    case HT_Level_CastleCourtyard:
        if (switch_channels[0x37] != 0 && DroneFunc_TickAlarmTimer(0x37) < frameRate * 10) {
            Music_Event(MUSIC_EVENT_ALARM, ALARM_MUSIC_GENERIC);
            return;
        }
        break;
    default:
        break;
    }

    if (switch_channels[0x61] != 0 && DroneFunc_TickAlarmTimer(0x61) < frameRate * 10)
        Music_Event(MUSIC_EVENT_ALARM, ALARM_MUSIC_GENERIC);
}
