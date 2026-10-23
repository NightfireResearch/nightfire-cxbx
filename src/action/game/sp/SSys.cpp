#include "SSys.h"
#include "SwitchChannels.h"
#include "../../engine/Script.h"
#include "../../game.h"
#include "../../sound/Sound.h"

#pragma fp_contract(off)

// XBE_GLOBAL(0x0029aacc, 0x28)
extern SSYSITEM SSysItems[SSYS_ITEMS];
// Seconds item 2 waits after it has cut item 1's countdown short
// XBE_GLOBAL(0x0029aac8, 0x4)
float SSysDelayTimer;

extern uint8_t IsWarmReset;

// XBE_GLOBAL(0x0018cc50, 0x38) readonly relocate
SSYSMSG SSysMsgs1[7] = {
    {(Action_TranslatedText)0x0200001f, 676},
    {(Action_TranslatedText)0x02000032, 675},
    {(Action_TranslatedText)0x0200001c, 664},
    {(Action_TranslatedText)0x0200001b, 663},
    {(Action_TranslatedText)0x0200001a, 662},
    {(Action_TranslatedText)0x02000019, 661},
    {(Action_TranslatedText)0x02000017, 674},
};
// XBE_GLOBAL(0x0018cc18, 0x38) readonly relocate
SSYSMSG SSysMsgs2[7] = {
    {(Action_TranslatedText)0x02000020, 1073},
    {(Action_TranslatedText)0x02000033, 1085},
    {(Action_TranslatedText)0x0200001c, 664},
    {(Action_TranslatedText)0x0200001b, 663},
    {(Action_TranslatedText)0x0200001a, 662},
    {(Action_TranslatedText)0x02000019, 661},
    {(Action_TranslatedText)0x02000018, 1080},
};
// XBE_GLOBAL(0x0018ccc0, 0x38) readonly relocate
SSYSMSG SSysMsgs3[7] = {
    {(Action_TranslatedText)0x0200001d, 673},
    {(Action_TranslatedText)0x02000030, 672},
    {(Action_TranslatedText)0x0200001c, 664},
    {(Action_TranslatedText)0x0200001b, 663},
    {(Action_TranslatedText)0x0200001a, 662},
    {(Action_TranslatedText)0x02000019, 661},
    {(Action_TranslatedText)0x02000015, 667},
};
// XBE_GLOBAL(0x0018cc88, 0x38) readonly relocate
SSYSMSG SSysMsgs4[7] = {
    {(Action_TranslatedText)0x0200001e, 673},
    {(Action_TranslatedText)0x02000031, 672},
    {(Action_TranslatedText)0x0200001c, 664},
    {(Action_TranslatedText)0x0200001b, 663},
    {(Action_TranslatedText)0x0200001a, 662},
    {(Action_TranslatedText)0x02000019, 661},
    {(Action_TranslatedText)0x02000016, 667},
};

// In HT_Level_TowerC, only item 2 speaks, and only once this channel is set
#define CHANNEL_SSYS_SPEAKS 0x76

// A countdown runs four minutes; its messages come with three, two and one minute left, then six seconds, then at the
// end.
#define SSYS_COUNTDOWN 240.0f
#define SSYS_ITEM2_DELAY 8.0f

// AUTOINJECT
void SSys_Init(void) {
    if (IsWarmReset)
        return;
    for (int i = 1; i < SSYS_ITEMS; i++) {
        SSysItems[i].channel = SSYS_NO_CHANNEL;
        SSysItems[i].state = 0;
        SSysItems[i].timer = -1.0f;
    }
}

// Item 3 says nothing in HT_Level_TowerB.
void SSys_Msg(ushort item, ushort msg) {
    bool quiet = GameState.CurrentLevelHashcode == HT_Level_TowerC && !switch_channels[CHANNEL_SSYS_SPEAKS];

    SSYSMSG *msgs;
    switch (item) {
    case 1:
        msgs = SSysMsgs1;
        break;
    case 2:
        msgs = SSysMsgs2;
        break;
    case 3:
        if (GameState.CurrentLevelHashcode == HT_Level_TowerB)
            return;
        msgs = SSysMsgs3;
        break;
    case 4:
        msgs = SSysMsgs4;
        break;
    default:
        return;
    }
    if (quiet)
        return;
    Sound_PlayExt((Action_SFX)msgs[msg].sound, 100.0f, 0, 0);
}

// Once a frame, outside cutscenes: an item's channel being set starts its countdown, and clearing it stops the
// countdown. At the end the item clears its channel. Item 2, starting, first ends item 1's countdown for good (setting
// item 1's channel) and waits eight seconds.
// AUTOINJECT
void SSys_Monitor(void) {
    if (ScriptCam != 0)
        return;

    for (ushort i = 1; i < SSYS_ITEMS; i++) {
        SSYSITEM *item = &SSysItems[i];
        if (item->channel == SSYS_NO_CHANNEL)
            continue;

        if (!switch_channels[item->channel]) {
            if (item->state != 0) {
                item->state = 0;
                SSys_Msg(i, 0);
                item->timer = -1.0f;
            }
            continue;
        }

        if (item->timer <= -1.0f) {
            if (i == 2) {
                if (SSysDelayTimer > 0.0f) {
                    SSysDelayTimer = SSysDelayTimer - REC_FRAME_RATE;
                    continue;
                }
                SSYSITEM *item1 = &SSysItems[1];
                if (item1->channel != SSYS_NO_CHANNEL) {
                    SSys_Msg(1, 0);
                    switch_channels[item1->channel] = 1;
                    item1->channel = SSYS_NO_CHANNEL;
                    item1->state = 0;
                    item1->timer = -1.0f;
                    SSysDelayTimer = SSYS_ITEM2_DELAY;
                    continue;
                }
            }
            item->state = 6;
            item->timer = SSYS_COUNTDOWN;
            SSys_Msg(i, 6);
        } else {
            item->timer = item->timer - REC_FRAME_RATE;
        }

        switch (item->state) {
        case 6:
            if (item->timer < 180.0f) {
                item->state = 5;
                SSys_Msg(i, 5);
            }
            break;
        case 5:
            if (item->timer < 120.0f) {
                item->state = 4;
                SSys_Msg(i, 4);
            }
            break;
        case 4:
            if (item->timer < 60.0f) {
                item->state = 3;
                SSys_Msg(i, 3);
            }
            break;
        case 3:
            if (item->timer < 6.0f) {
                item->state = 2;
                SSys_Msg(i, 2);
            }
            break;
        case 2:
            if (item->timer <= 0.0f) {
                item->state = 1;
                SSys_Msg(i, 1);
                if (item->channel != 0)
                    switch_channels[item->channel] = 0;
                item->state = 0;
                item->timer = -1.0f;
            }
            break;
        }
    }
}

// Puts back the copy Player_RamSave made, but for item 2 in HT_Level_TowerB; in HT_Level_TowerC item 1 is reset.
// AUTOINJECT
void SSys_RamLoad(SSYSITEM *saved) {
    SSysItems[1] = saved[1];
    if (GameState.CurrentLevelHashcode != HT_Level_TowerB)
        SSysItems[2] = saved[2];
    SSysItems[3] = saved[3];
    SSysItems[4] = saved[4];
    SSysDelayTimer = 0.0f;
    if (GameState.CurrentLevelHashcode == HT_Level_TowerC) {
        SSysItems[1].channel = SSYS_NO_CHANNEL;
        SSysItems[1].state = 0;
        SSysItems[1].timer = -1.0f;
    }
}
