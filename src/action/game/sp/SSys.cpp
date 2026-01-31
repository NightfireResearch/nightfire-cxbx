// SSys is the incredibly descriptive name that Eurocom gave to the PA announcements made in the Tower stealth levels
// relating to the camera and laser trip wire system tests.
#include "SSys.h"

#pragma pack(push, 1)

typedef struct {
    uint8_t switchChannel;
    uint8_t progress;
    uint16_t unknown; // Only ever written?
    float countdownSeconds;
} SSysItem;

static_assert(sizeof(SSysItem) == 8, "Bad size for SSysItem");

typedef struct {
    Action_SFX audioTrack;
    Action_TranslatedText caption;
} SSysAudioEvent;

#pragma pack(pop)

#define SSysItems ((SSysItem*)(0x0029aac8))

// "Laser1": Laser Test Cycle Messages 1
SSysAudioEvent Laser1[7] = {
    { SFX_CHR_SPEECH_M3_BUI_021, (Action_TranslatedText)0x02000032 },
    { SFX_CHR_SPEECH_M3_BUI_020, (Action_TranslatedText)0x0200001c },
    { SFX_CHR_SPEECH_M3_BUI_005, (Action_TranslatedText)0x0200001b },
    { SFX_CHR_SPEECH_M3_BUI_004, (Action_TranslatedText)0x0200001a },
    { SFX_CHR_SPEECH_M3_BUI_003, (Action_TranslatedText)0x02000019 },
    { SFX_CHR_SPEECH_M3_BUI_002, (Action_TranslatedText)0x02000017 },
    { SFX_CHR_SPEECH_M3_BUI_015, (Action_TranslatedText)0x0200001e }
};

// "Laser2": Laser Test Cycle Messages 2
SSysAudioEvent Laser2[7] = {
    { SFX_CHR_SPEECH_M3_BUI_042, (Action_TranslatedText)0x02000033 },
    { SFX_CHR_SPEECH_M3_BUI_041, (Action_TranslatedText)0x0200001c },
    { SFX_CHR_SPEECH_M3_BUI_005, (Action_TranslatedText)0x0200001b },
    { SFX_CHR_SPEECH_M3_BUI_004, (Action_TranslatedText)0x0200001a },
    { SFX_CHR_SPEECH_M3_BUI_003, (Action_TranslatedText)0x02000019 },
    { SFX_CHR_SPEECH_M3_BUI_002, (Action_TranslatedText)0x02000018 },
    { SFX_CHR_SPEECH_M3_BUI_037A, (Action_TranslatedText)0x0200001f }
};

// "Cam1": Camera Test Cycle Messages 1
SSysAudioEvent Cam1[7] = {
    { SFX_CHR_SPEECH_M3_BUI_014, (Action_TranslatedText)0x02000030 },
    { SFX_CHR_SPEECH_M3_BUI_013, (Action_TranslatedText)0x0200001c },
    { SFX_CHR_SPEECH_M3_BUI_005, (Action_TranslatedText)0x0200001b },
    { SFX_CHR_SPEECH_M3_BUI_004, (Action_TranslatedText)0x0200001a },
    { SFX_CHR_SPEECH_M3_BUI_003, (Action_TranslatedText)0x02000019 },
    { SFX_CHR_SPEECH_M3_BUI_002, (Action_TranslatedText)0x02000015 },
    { SFX_CHR_SPEECH_M3_BUI_008, TXT_NULL }
};

// "Cam2": Camera Test Cycle Messages 2
SSysAudioEvent Cam2[7] = {
    { SFX_CHR_SPEECH_M3_BUI_014, (Action_TranslatedText)0x02000031 },
    { SFX_CHR_SPEECH_M3_BUI_013, (Action_TranslatedText)0x0200001c },
    { SFX_CHR_SPEECH_M3_BUI_005, (Action_TranslatedText)0x0200001b },
    { SFX_CHR_SPEECH_M3_BUI_004, (Action_TranslatedText)0x0200001a },
    { SFX_CHR_SPEECH_M3_BUI_003, (Action_TranslatedText)0x02000019 },
    { SFX_CHR_SPEECH_M3_BUI_002, (Action_TranslatedText)0x02000016 },
    { SFX_CHR_SPEECH_M3_BUI_008, (Action_TranslatedText)0x0200001d }
};


// UNINJECTABLE - custom calling convention
void SSys_Msg(ushort msgType, ushort stage) {

    bool triggerState1 = (GameState.CurrentLevelHashcode == HT_Level_TowerC) && (switch_channels[0x76] == '\0');
    bool triggerState2 = (GameState.CurrentLevelHashcode != HT_Level_TowerB) && (!triggerState1);

    switch(msgType) {
        case 1:
        if (!triggerState1) {
            Sound_PlayExt(Laser1[stage].audioTrack, 100.0, 0, 0);
            return;
        }
        break;
        case 2:
        if (!triggerState1) {
            Sound_PlayExt(Laser2[stage].audioTrack, 100.0, 0, 0);
        }
        break;
        case 3:
        if (triggerState2) {
            Sound_PlayExt(Cam1[stage].audioTrack, 100.0, 0, 0);
            return;
        }
        break;
        case 4:
        if (!triggerState1) {
            Sound_PlayExt(Cam2[stage].audioTrack, 100.0, 0, 0);
            return;
        }
    }

}

#define IsWarmReset BOOL8_AT(0x00279250)

// AUTOINJECT
void SSys_Init(void) {
  
  if (IsWarmReset) 
    return;

    for(int i = 2; i < 6; i++) {
        SSysItems[i].switchChannel = 0xff;
        SSysItems[i].progress = 0;
        SSysItems[i].countdownSeconds = -1.0f;
    }

}