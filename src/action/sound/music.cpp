#include "music.h"
#include "SFX.h"
#include <string.h>
#include <stdio.h>

// The music: each level's track and the jumps between its sections, driven by the events of the frame
// (Music_Event). UpdateMusicalEvents runs once a frame; the level's UpdateMFX_ function asks for sections from
// the lists of its MapMusic row.

#define MusicEventList (*(uint32_t(*)[0x40])0x0029a180)
// The frame's events, kept after UpdateMusicalEvents clears them, and the last frame's
#define BackupEventList (*(uint32_t(*)[0x40])0x002980e8)
#define LastEventList (*(uint32_t(*)[0x40])0x00297fe8)

#define NUM_MAPMUSIC 35
#define MapMusic (*(MUSICDATA(*)[NUM_MAPMUSIC])0x00181cd0)
#define NUM_MAPDRONEDATA 12
#define MapDroneData (*(MUSICAIVARS(*)[NUM_MAPDRONEDATA])0x00181c10)

// The level's MapMusic row
#define MapMusicData (*(MUSICDATA **)0x00182734)
// The section asked for last, and the one the stream is playing
#define MusicLastJumpRequest I32_AT(0x00182738)
#define MusicLastJump I32_AT(0x0018273c)
// Frames of alert (event 2) before the combat music, and of calm before it ends
#define MusicUnderAttackCount2 I32_AT(0x00182744)
#define MusicUnderAttackCount I32_AT(0x00182740)

#define SfxNisMusicTrigger I32_AT(0x00299328)
#define NisPlaying I32_AT(0x0029932c)
#define NisQuitten I32_AT(0x00299330)    // counts down 5 frames from a cut scene ending
#define NisEndedNow I32_AT(0x00299334)
#define MusicNowPlaying I32_AT(0x00299338)
#define MusicUnderAttack I32_AT(0x0029933c)
#define MusicWaitForEnd I32_AT(0x00299340)
#define MusicWaitForNisSfxTrigger I32_AT(0x00299344)
#define MusicUnderAttackBonusCount I32_AT(0x00299348)
#define MusicTemp1 I32_AT(0x0029934c)
#define MusicTemp2 I32_AT(0x00299350)
#define MusicTemp3 I32_AT(0x00299354)
#define MusicTemp4 I32_AT(0x00299358)
#define MusicTemp5 I32_AT(0x0029935c)
#define MusicTemp6 I32_AT(0x00299360)
#define MusicTemp7 I32_AT(0x00299364)
#define MusicTemp8 I32_AT(0x00299368)
#define MusicTemp9 I32_AT(0x0029936c)
#define WhichStealth I32_AT(0x00299370)
#define WhichCombat I32_AT(0x00299374)
#define SFXPaused U32_AT(0x00299378)

// The level functions' own turns between two sections
#define WhichAttack_MayhewA I32_AT(0x002999f8)
#define WhichNonAttack_MayhewA I32_AT(0x002999fc)
#define WhichNonAttack_MayhewB I32_AT(0x00299a00)
#define WhichNonAttack_MayhewC I32_AT(0x00299a04)
#define WhichAttack_MayhewC I32_AT(0x00299a08)

// The stream's side: the section it jumps to at its next marker and whether at once, and the section it plays
#define SFXJumpMarker I32_AT(0x00182b6c)
#define SFXJumpInstant I32_AT(0x002997d8)
#define SFXActiveMarker I32_AT(0x002997ac)
// Frames before the stream's section is read back into MusicLastJump
#define IOP_DelayUpdateTimer I32_AT(0x0027aed8)

#define MUSIC_COMBAT_THRESHOLD_ALERT 1
#define MUSIC_NO_LEVEL (-99)

// AUTOINJECT
void __cdecl Music_Event(uint evtId,undefined4 val) {

  if (evtId < ARRAY_SIZE(MusicEventList)) {
    MusicEventList[evtId] = val;
  } else {
    printf("FYI::INVALID MUSIC EVENT ID = %d\n", evtId);
  }

}

// AUTOINJECT
uint32_t __cdecl Get_Music_Event(int evtId) {
  return MusicEventList[evtId];
}

// Asks the stream for a section (inline in the original; ours)
static void Music_JumpTo(int section, int instant) {
    MusicLastJumpRequest = section;
    SFXJumpMarker = section;
    SFXJumpInstant = instant;
}

// Whether a section is in one of the row's lists (up to 5, ended by -1)
static int Music_InList(const int *list, int section) {
    for (int i = 0; i < MUSIC_LIST_LENGTH; i++) {
        if (list[i] == -1)
            return 0;
        if (section == list[i])
            return 1;
    }
    return 0;
}

static int Music_ListLength(const int *list) {
    int n = 0;
    for (int i = 0; i < MUSIC_LIST_LENGTH && list[i] != -1; i++)
        n++;
    return n;
}

// The four list tests (names ours)
// FUNC_AT(0x000c4da0)
int Music_PlayingCombat(void) {
    return Music_InList(MapMusicData->combat, MusicLastJump);
}

// FUNC_AT(0x000c4de0)
int Music_RequestedCombat(void) {
    return Music_InList(MapMusicData->combat, MusicLastJumpRequest);
}

// FUNC_AT(0x000c4e20)
int Music_PlayingStealth(void) {
    return Music_InList(MapMusicData->stealth, MusicLastJump);
}

// FUNC_AT(0x000c4e60)
int Music_RequestedStealth(void) {
    return Music_InList(MapMusicData->stealth, MusicLastJumpRequest);
}

// Events 11 and 12 start and end a cut scene; event 5 ends one for the next 5 frames (name ours)
// FUNC_AT(0x000c4cd0)
void Music_TrackNis(void) {
    if (MusicEventList[MUSICEV_NIS_STARTED] != 0)
        NisPlaying = 1;
    if (MusicEventList[MUSICEV_NIS_ENDED] != 0 || MusicEventList[MUSICEV_NIS_DONE] != 0) {
        NisPlaying = 0;
        if (MusicEventList[MUSICEV_NIS_DONE] != 0) {
            NisQuitten = 5;
            return;
        }
    }
    if (NisQuitten != 0)
        NisQuitten--;
}

// The drones' music thresholds for a level, or the defaults (name ours)
// FUNC_AT(0x000c4d20)
void Music_SetAIVars(HASHCODE level) {
    for (uint i = 0; i < NUM_MAPDRONEDATA; i++) {
        if (level == MapDroneData[i].level) {
            MusicVars.combatThreshold = MapDroneData[i].combatThreshold;
            MusicVars.calmThreshold = MapDroneData[i].calmThreshold;
            MusicVars.seenSeconds = MapDroneData[i].seenSeconds;
            MusicVars.closeRange = MapDroneData[i].closeRange;
            return;
        }
    }
    MusicVars.combatThreshold = 2;
    MusicVars.calmThreshold = 0;
    MusicVars.seenSeconds = 8;
    MusicVars.closeRange = 25.0f;
}

// Leaving combat with no stealth section asked for or playing, the next of the stealth list in turn. An empty list
// divides by zero.
// AUTOINJECT
void PickRandomStealth(void) {
    MUSICDATA *data = MapMusicData;
    if (Music_RequestedStealth())
        return;
    if (Music_PlayingStealth())
        return;
    if (!Music_PlayingCombat())
        return;
    int n = Music_ListLength(data->stealth);
    int pick = WhichStealth++ % n;
    Music_JumpTo(data->stealth[pick], 0);
}

// With no combat section asked for or playing, the next of the combat list in turn. An empty list divides by
// zero.
// AUTOINJECT
void PickRandomCombat(int instant) {
    MUSICDATA *data = MapMusicData;
    if (Music_RequestedCombat())
        return;
    if (Music_PlayingCombat())
        return;
    int n = Music_ListLength(data->combat);
    int pick = WhichCombat++ % n;
    Music_JumpTo(data->combat[pick], instant);
}

// The mission's end: its section from combat or otherwise, at once, and the music faded but in CastleCourtyard
// AUTOINJECT
void MusicFadeDown(void) {
    MUSICDATA *data = MapMusicData;
    Music_JumpTo(Music_PlayingCombat() ? data->endFromCombat : data->endSection, 1);
    if (data->level != HT_Level_CastleCourtyard)
        SFXFadeDown(0);
}

// Events 3 and 8 end the mission; whether the music waits for the end (name ours)
// FUNC_AT(0x000c70a0)
int Music_MissionEnded(void) {
    if (MusicEventList[MUSICEV_END] != 0 || MusicEventList[MUSICEV_END_B] != 0) {
        MusicFadeDown();
        MusicWaitForEnd = 1;
        return 1;
    }
    return MusicWaitForEnd;
}

// AUTOINJECT
void UpdateMFX_02MayhewA(void) {
    if (Music_MissionEnded())
        return;

    if (MusicUnderAttack == 0 && MusicEventList[MUSICEV_NIS_ENDED] != 0x6000087) {
        if (MusicTemp3 < 0) {
            if (Music_PlayingCombat() && MusicTemp1 == 0) {
                Music_JumpTo(5, 0);
                MusicTemp1 = 1;
            }
            if (Music_PlayingCombat() && !Music_RequestedStealth()) {
                if (WhichNonAttack_MayhewA == 0)
                    Music_JumpTo(7, 0);
                else if (WhichNonAttack_MayhewA == 1)
                    Music_JumpTo(9, 0);
                if (++WhichNonAttack_MayhewA > 1)
                    WhichNonAttack_MayhewA = 0;
            }
        }
    } else {
        if (MusicTemp2 == 0) {
            Music_JumpTo(3, 0);
            MusicTemp2 = 1;
            MusicTemp3 = 500;
        }
        if (!Music_PlayingCombat() && !Music_RequestedCombat()) {
            if (WhichAttack_MayhewA == 0)
                Music_JumpTo(0xb, 0);
            else if (WhichAttack_MayhewA == 1)
                Music_JumpTo(0xe, 0);
            if (++WhichAttack_MayhewA > 1)
                WhichAttack_MayhewA = 0;
        }
    }
    MusicTemp3--;
}

// AUTOINJECT
void UpdateMFX_02MayhewB(void) {
    int lastJump = MusicLastJump;
    if (Music_MissionEnded())
        return;

    if (MusicLastJump == 0) {
        if (MusicEventList[MUSICEV_NIS_ENDED] == 0x6000084 || MusicEventList[36] != 0)
            Music_JumpTo(2, 0);
        return;
    }

    if (MusicEventList[37] == 0)
        Music_SetAIVars(MapMusicData->level);
    else
        MusicVars.closeRange = 1.0f;

    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x600007f) {
        MusicTemp7 = 0;
        Music_JumpTo(6, 1);
        MusicTemp4 = 1;
    }
    if (MusicTemp4 != 0) {
        if (MusicEventList[MUSICEV_NIS_ENDED] == 0x600007f) {
            Music_JumpTo(8, 1);
            MusicTemp4 = 0;
        }
        return;
    }

    if (MusicTemp1 != 0) {
        if (MusicEventList[15] == 0 && MusicEventList[16] == 0)
            return;
        PickRandomCombat(0);
        MusicTemp1 = 0;
        MusicTemp9 = 300;
    }
    if (MusicTemp9-- > 0)
        return;

    if (MusicEventList[31] != 0 && MusicTemp2 == 0) {
        Music_JumpTo(4, 1);
        MusicTemp2 = 1;
        MusicTemp1 = 1;
        MusicVars.combatThreshold = MUSIC_COMBAT_THRESHOLD_ALERT;
        MusicUnderAttackBonusCount = 600;
        return;
    }
    if (MusicEventList[32] != 0 && MusicTemp3 == 0) {
        Music_JumpTo(4, 0);
        MusicTemp3 = 1;
        MusicTemp1 = 1;
        MusicVars.combatThreshold = MUSIC_COMBAT_THRESHOLD_ALERT;
        MusicUnderAttackBonusCount = 600;
        return;
    }
    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
        return;
    }

    if (MusicTemp5 == 0) {
        if (!Music_RequestedStealth()) {
            int instant = lastJump == 4;
            if (WhichNonAttack_MayhewB == 0)
                Music_JumpTo(0x14, instant);
            else if (WhichNonAttack_MayhewB == 1)
                Music_JumpTo(0x16, instant);
            if (++WhichNonAttack_MayhewB > 1)
                WhichNonAttack_MayhewB = 0;
        }
    } else {
        Music_JumpTo(0x12, 0);
    }

    if (MusicEventList[30] == 0)
        return;
    MusicTemp5 = 1;
    if (Music_PlayingCombat())
        Music_JumpTo(0x12, 0);
    else if (lastJump != 0x12)
        Music_JumpTo(10, 0);
}

// AUTOINJECT
void UpdateMFX_02MayhewC(void) {
    if (Music_MissionEnded())
        return;

    if (MusicTemp1 != 0) {
        if (MusicEventList[15] == 0 && MusicEventList[16] == 0)
            return;
        PickRandomCombat(0);
        MusicTemp1 = 0;
        MusicTemp9 = 120;
    }
    if (MusicTemp9-- > 0)
        return;

    if (MusicEventList[33] != 0 && MusicTemp2 == 0) {
        MusicTemp2 = 1;
        Music_JumpTo(10, 0);
        MusicTemp1 = 1;
        MusicVars.combatThreshold = MUSIC_COMBAT_THRESHOLD_ALERT;
        MusicUnderAttackBonusCount = 600;
        return;
    }
    if (MusicEventList[34] != 0 && MusicTemp3 == 0) {
        Music_JumpTo(0xc, 0);
        MusicVars.combatThreshold = MUSIC_COMBAT_THRESHOLD_ALERT;
        MusicUnderAttackBonusCount = 600;
        MusicTemp1 = 1;
        MusicTemp3 = 1;
        return;
    }

    if (MusicUnderAttack != 0) {
        if (MusicEventList[35] != 0) {
            if (!Music_PlayingCombat() && !Music_RequestedCombat())
                Music_JumpTo(0xe, 0);
            return;
        }
        if (Music_PlayingCombat() || Music_RequestedCombat())
            return;
        if (MusicLastJump == 0xe || MusicLastJumpRequest == 0xe)
            return;
        if (WhichAttack_MayhewC == 0)
            Music_JumpTo(2, 0);
        else if (WhichAttack_MayhewC == 1)
            Music_JumpTo(4, 0);
        if (++WhichAttack_MayhewC > 1)
            WhichAttack_MayhewC = 0;
        return;
    }

    if (Music_RequestedStealth())
        return;
    if (Music_PlayingCombat()) {
        if (WhichNonAttack_MayhewC == 0)
            Music_JumpTo(6, 0);
        else if (WhichNonAttack_MayhewC == 1)
            Music_JumpTo(8, 0);
        if (++WhichNonAttack_MayhewC > 1)
            WhichNonAttack_MayhewC = 0;
        return;
    }
    if (MusicLastJump != 10 && MusicLastJump != 0xc)
        Music_JumpTo(0, 0);
    else if (MusicUnderAttackBonusCount < 0)
        Music_JumpTo(0, 1);
}

// AUTOINJECT
void UpdateMFX_MayhewD(void) {
    if (Music_MissionEnded())
        return;

    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x6000082) {
        Music_JumpTo(2, 1);
        MusicTemp2 = 1;
    }
    if (SfxNisMusicTrigger != 0) {
        if (MusicTemp1 != 0) {
            Music_JumpTo(4, 1);
        } else {
            Music_JumpTo(3, 1);
            MusicTemp1 = 1;
        }
        SfxNisMusicTrigger = 0;
    } else if (MusicEventList[MUSICEV_NIS_DONE] != 0 && MusicTemp2 != 0) {
        Music_JumpTo(4, 1);
        SfxNisMusicTrigger = 0;
    }
}

// AUTOINJECT
void UpdateMFX_01CastleA(void) {
    if (MusicEventList[MUSICEV_ALERT] == 3 && MusicTemp4 == 0) {
        MusicTemp4 = 1;
        MusicTemp3 = 1200;
        PickRandomCombat(0);
        return;
    }
    if (MusicTemp3 > 0) {
        MusicTemp3--;
        return;
    }
    if (MusicEventList[MUSICEV_NIS_ENDED] == 0x6000994) {
        MusicTemp1 = 0;
        MusicTemp2 = 65;
        return;
    }
    if (MusicTemp2 > 0) {
        MusicTemp2--;
        return;
    }

    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x6000994 || MusicTemp1 != 0) {
        Music_JumpTo(Music_PlayingCombat() ? 6 : 2, 0);
        MusicTemp1 = 1;
        return;
    }

    if (MusicUnderAttack != 0)
        PickRandomCombat(0);
    else if (MusicEventList[13] != 0)
        Music_JumpTo(Music_PlayingCombat() ? 6 : 2, 0);
    else if (MusicLastJumpRequest == 2)
        Music_JumpTo(0x14, 0);
    else
        PickRandomStealth();

    if (!Music_MissionEnded() && MusicEventList[4] != 0) {
        Music_JumpTo(9, 0);
        MusicWaitForEnd = 1;
    }
}

// AUTOINJECT
void UpdateMFX_01CastleB(void) {
    if (MusicWaitForNisSfxTrigger != 0) {
        if (SfxNisMusicTrigger != 0) {
            Music_JumpTo(0xc, 0);
            SfxNisMusicTrigger = 0;
            MusicWaitForEnd = 1;
        }
        return;
    }
    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x6000071) {
        MusicFadeDown();
        MusicWaitForNisSfxTrigger = 1;
    }
    if (MusicUnderAttack != 0)
        PickRandomCombat(0);
    else
        PickRandomStealth();
}

// AUTOINJECT
void UpdateMFX_01CastleC(void) {
    if (Music_MissionEnded())
        return;
    if (MusicTemp2 != 0)
        return;

    if (MusicTemp1 == 0) {
        if (MusicEventList[MUSICEV_NIS_STARTED] == 0x6000072)
            Music_JumpTo(2, 0);
        if (MusicEventList[MUSICEV_NIS_ENDED] == 0x6000072 || SfxNisMusicTrigger != 0) {
            Music_JumpTo(5, 0);
            SfxNisMusicTrigger = 0;
            MusicTemp1 = 1;
        }
    }

    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x6000073) {
        Music_JumpTo(7, 0);
        MusicTemp4 = 1;
    }
    if (MusicTemp4 != 0 && (MusicEventList[MUSICEV_NIS_ENDED] == 0x6000073 || SfxNisMusicTrigger != 0)) {
        Music_JumpTo(10, 0);
        SfxNisMusicTrigger = 0;
        MusicTemp1 = 10;
    }
    if (MusicTemp1 == 10 && MusicUnderAttack != 0) {
        Music_JumpTo(0xe, 0);
        MusicTemp2 = 1;
    }

    if (MusicEventList[13] != 0)
        Music_JumpTo(0xc, 0);
}

// AUTOINJECT
void UpdateMFX_01CastleD(void) {
    if (MusicLastJump == 0 && MusicEventList[MUSICEV_NIS_STARTED] == 0x6000077)
        Music_JumpTo(2, 1);
    if (MusicEventList[MUSICEV_NIS_ENDED] == 0x6000077)
        Music_JumpTo(4, 1);

    if (Music_MissionEnded())
        return;

    if (MusicEventList[13] != 0 && MusicTemp1 == 0) {
        Music_JumpTo(6, 0);
        MusicTemp1 = 1;
    }
    if (SfxNisMusicTrigger != 0) {
        Music_JumpTo(8, 1);
        SfxNisMusicTrigger = 0;
    }
}

// AUTOINJECT
void UpdateMFX_03TowerA(void) {
    if (Music_MissionEnded())
        return;

    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
        return;
    }
    if (MusicEventList[43] != 0) {
        if (!Music_RequestedStealth())
            Music_JumpTo(Music_PlayingCombat() ? 0xc : 2, 0);
        return;
    }
    if (MusicLastJumpRequest == 4 || MusicLastJumpRequest == 0 || MusicLastJumpRequest == 0x10)
        return;
    if (Music_PlayingCombat())
        Music_JumpTo(0x10, 0);
    else if (MusicLastJump == 2)
        Music_JumpTo(4, 0);
    else
        Music_JumpTo(0, 0);
}

// AUTOINJECT
void UpdateMFX_03TowerB(void) {
    if (Music_MissionEnded())
        return;

    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
        return;
    }
    if (MusicEventList[40] != 0 || MusicTemp1 != 0) {
        MusicTemp1 = 1;
        if (!Music_RequestedStealth())
            Music_JumpTo(Music_PlayingCombat() ? 0xe : 0x13, 0);
        return;
    }
    if (MusicLastJumpRequest == 4 || MusicLastJumpRequest == 0x1a || MusicLastJumpRequest == 0x1c)
        return;
    if (Music_PlayingCombat())
        Music_JumpTo(0x1c, 0);
    else if (MusicLastJump == 0x13)
        Music_JumpTo(4, 0);
    else
        Music_JumpTo(0x1a, 0);
}

// MusicTemp1 is the stage
// AUTOINJECT
void UpdateMFX_03TowerC(void) {
    switch (MusicTemp1) {
    case 0:
        if (MusicEventList[MUSICEV_NIS_STARTED] == 0x60004f9) {
            MusicTemp1 = 1;
            Music_JumpTo(2, 0);
        }
        break;
    case 1:
        if (MusicEventList[44] != 0) {
            Music_JumpTo(4, 0);
            MusicTemp1 = 2;
        }
        break;
    case 2:
        if (MusicEventList[41] != 0) {
            Music_JumpTo(6, 0);
            MusicTemp1 = 3;
        }
        break;
    case 3:
        if (MusicEventList[17] != 0) {
            Music_JumpTo(8, 1);
            MusicTemp1 = 4;
        }
        break;
    case 4:
        if (MusicUnderAttack != 0) {
            MusicTemp2 = 1;
            Music_JumpTo(8, 0);
        } else if (MusicTemp2 != 0) {
            Music_JumpTo(0x15, 0);
        }
        if (MusicEventList[MUSICEV_NIS_STARTED] == 0x60004fa) {
            Music_JumpTo(MusicUnderAttack != 0 ? 10 : 0x17, 0);
            MusicTemp1 = 5;
        }
        break;
    case 5:
        if (MusicEventList[MUSICEV_NIS_ENDED] == 0x60004fa) {
            Music_JumpTo(0xc, 1);
            MusicTemp1 = 6;
        }
        break;
    case 6:
        if (MusicEventList[MUSICEV_END] != 0 || MusicEventList[MUSICEV_END_B] != 0) {
            Music_JumpTo(0xe, 1);
            MusicTemp1 = 99;
        }
        break;
    }
}

// AUTOINJECT
void UpdateMFX_PowerStationA1(void) {
    if (Music_MissionEnded())
        return;

    if (MusicEventList[45] != 0 && MusicTemp2 == 0) {
        if (Music_RequestedCombat() && Music_PlayingCombat()) {
            Music_JumpTo(0x36, 0);
            MusicTemp1 = 1;
            MusicTemp2 = 1;
        } else {
            Music_JumpTo(0x24, 1);
            MusicTemp1 = 1;
            MusicTemp2 = 1;
        }
        return;
    }
    if (MusicTemp1 != 0)
        return;
    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
        return;
    }
    if (MusicEventList[13] != 0) {
        if (!Music_PlayingCombat() && MusicLastJump != 0x38)
            Music_JumpTo(0x22, 0);
        else
            Music_JumpTo(0x38, 0);
        return;
    }
    if (Music_RequestedCombat() && Music_PlayingCombat()) {
        Music_JumpTo(0xf, 0);
        return;
    }
    if (Music_RequestedStealth())
        return;
    if (Music_PlayingStealth())
        return;
    Music_JumpTo(0x14, 1);
}

// AUTOINJECT
void UpdateMFX_PowerStationA2(void) {
    if (Music_MissionEnded())
        return;

    if (MusicEventList[MUSICEV_NIS_ENDED] == 0x600068f || SfxNisMusicTrigger != 0) {
        MusicTemp1 = 1;
        MusicTemp2 = 600;
        Music_JumpTo(0x2b, 1);
        return;
    }
    if (MusicTemp1 == 0)
        return;
    if (MusicTemp2-- > 0)
        return;

    if (MusicEventList[45] != 0 || MusicTemp4 != 0) {
        Music_JumpTo(0x3a, MusicLastJumpRequest == 0x22);
        MusicTemp4 = 1;
        return;
    }
    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
        return;
    }
    if (MusicEventList[13] != 0) {
        if (!Music_PlayingCombat() && MusicLastJump != 0x38)
            Music_JumpTo(0x22, 0);
        else
            Music_JumpTo(0x38, 0);
        return;
    }
    if (Music_PlayingCombat())
        Music_JumpTo(0xc, 0);
}

// AUTOINJECT
void UpdateMFX_HT_Level_Tower2A(void) {
    if (MusicEventList[47] != 0)
        Music_JumpTo(0xc, 1);
    Music_MissionEnded();
}

// MusicTemp1 is the stage
// AUTOINJECT
void UpdateMFX_HT_Level_Tower2B(void) {
    if (Music_MissionEnded())
        return;

    if (MusicTemp1 == 0) {
        if (MusicUnderAttack != 0) {
            Music_JumpTo(0xf, 1);
            MusicTemp1 = 1;
        }
    } else if (MusicTemp1 == 1) {
        if (MusicUnderAttack == 0) {
            Music_JumpTo(0x11, 0);
            MusicTemp1 = 2;
        }
    } else if (MusicTemp1 == 2) {
        if (MusicUnderAttack != 0)
            PickRandomCombat(0);
        else if (Music_PlayingCombat())
            Music_JumpTo(0x16, 0);
    }
}

// AUTOINJECT
void UpdateMFX_HT_Level_Tower2Elevator(void) {
    if (Music_MissionEnded())
        return;
    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x60006d3)
        Music_JumpTo(4, 0);
}

// AUTOINJECT
void UpdateMFX_HT_Level_Tower2C(void) {
    if (MusicUnderAttack != 0)
        Music_JumpTo(0, 0);
    Music_MissionEnded();
}

// AUTOINJECT
void UpdateMFX_HT_Level_EvilBase(void) {
    if (Music_MissionEnded())
        return;

    if (MusicEventList[MUSICEV_NIS_ENDED] == 0x6000848 || MusicTemp3 != 0) {
        MusicTemp3 = 1;
        Music_JumpTo(0x18, 1);
        return;
    }
    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x6000848 || MusicTemp6 != 0) {
        MusicTemp6 = 1;
        Music_JumpTo(0x22, 1);
    }

    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x600062f) {
        if (MusicTemp2 != 0)
            return;
        MusicTemp1 = 0;
        MusicTemp2 = 1;
        if (MusicUnderAttack == 0)
            Music_JumpTo(5, 1);
        else
            Music_JumpTo(0x12, 0);
    } else if (MusicTemp2 != 0) {
        return;
    }

    if (MusicEventList[MUSICEV_NIS_ENDED] == 0x6000833 || MusicTemp1 != 0) {
        MusicTemp1++;
        if (MusicUnderAttack == 0) {
            if (Music_PlayingCombat()) {
                Music_JumpTo(0xf, 0);
                return;
            }
            if (MusicLastJumpRequest == 0xf)
                return;
        } else if (MusicTemp1 > 55) {
            PickRandomCombat(1);
            return;
        }
        Music_JumpTo(0x14, 1);
        return;
    }

    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x6000833) {
        if (MusicTemp4 == 0) {
            MusicTemp4 = 1;
            if (MusicLastJump == 0x2e)
                Music_JumpTo(0x34, 0);
            else
                Music_JumpTo(0x16, 1);
        }
    } else if (MusicTemp4 == 0) {
        if (MusicUnderAttack != 0)
            Music_JumpTo(0x2e, 1);
        else if (MusicLastJump != 5)
            Music_JumpTo(0x32, 0);
    }
}

// AUTOINJECT
void UpdateMFX_HT_Level_EvilSilo(void) {
    if (Music_MissionEnded())
        return;
    if (MusicUnderAttack != 0)
        PickRandomCombat(0);
    else
        PickRandomStealth();
}

// MusicTemp1 is the stage
// AUTOINJECT
void UpdateMFX_HT_Level_EvilBaseC(void) {
    if (Music_MissionEnded())
        return;

    if (MusicEventList[MUSICEV_NIS_STARTED] == 0x6000631) {
        Music_JumpTo(0x24, 1);
        MusicTemp1 = 1;
    } else if (MusicTemp1 == 0) {
        if (MusicUnderAttack != 0)
            PickRandomCombat(0);
        else
            PickRandomStealth();
        return;
    }

    if (MusicTemp1 == 1) {
        if (MusicEventList[MUSICEV_NIS_ENDED] == 0x6000631) {
            Music_JumpTo(0x27, 1);
            MusicTemp1 = 2;
        }
    } else if (MusicTemp1 == 2 && MusicEventList[MUSICEV_NIS_STARTED] == 0x6000632) {
        Music_JumpTo(0x29, 0);
        MusicTemp1 = 3;
    }
}

// AUTOINJECT
void UpdateMFX_HT_Level_SpaceStationD(void) {
    if (Music_MissionEnded())
        return;
    if (MusicEventList[MUSICEV_NIS_ENDED] == 0x600085c)
        Music_JumpTo(0, 0);
}

// Once a frame: a level's track started on its first frame, the alert's and the cut scenes' effect on the drones'
// thresholds, the death and victory sections, then the level's own function. The events are cleared after.
// AUTOINJECT
void UpdateMusicalEvents(void) {
    MUSICDATA *data = MapMusicData;
    int bonusCount = MusicUnderAttackBonusCount;

    bool started = false;
    if (MusicEventList[MUSICEV_NEW_LEVEL] != 0 && MusicEventList[MUSICEV_LEVEL] != 0) {
        if (MusicEventList[MUSICEV_LEVEL] == HT_Level_Menu_Pre) {
            started = true;
        } else {
            for (int i = 0; i < NUM_MAPMUSIC; i++) {
                if (MapMusic[i].level != MusicEventList[MUSICEV_LEVEL])
                    continue;
                if (MapMusic[i].track != 0) {
                    MapMusicData = &MapMusic[i];
                    NisPlaying = 0;
                    NisQuitten = 0;
                    NisEndedNow = 0;
                    SFXStopMusic();
                    MusicLastJumpRequest = -1;
                    MusicLastJump = MapMusicData->startSection;
                    MusicNowPlaying = MapMusicData->track;
                    SFXStartMusic(MusicNowPlaying, MusicLastJump);
                    data = MapMusicData;
                    MusicWaitForEnd = 0;
                    SfxNisMusicTrigger = 0;
                    MusicWaitForNisSfxTrigger = 0;
                    MusicTemp1 = 0;
                    MusicTemp2 = 0;
                    MusicTemp3 = 0;
                    MusicTemp4 = 0;
                    MusicTemp5 = 0;
                    MusicTemp6 = 0;
                    MusicTemp7 = 0;
                    MusicTemp8 = 0;
                    MusicTemp9 = 0;
                    MusicUnderAttack = 0;
                    MusicUnderAttackCount = data->attackCount;
                    MusicUnderAttackCount2 = data->alertCount;
                    bonusCount = 0;
                    MusicUnderAttackBonusCount = 0;
                    WhichStealth = 0;
                    WhichCombat = 0;
                    Music_SetAIVars(data->level);
                    IOP_DelayUpdateTimer = 10;
                }
                started = true;
                break;
            }
        }
    }
    if (!started && SFXPaused != 0)
        return;

    if (IOP_DelayUpdateTimer-- < 0)
        MusicLastJump = SFXActiveMarker;
    Music_TrackNis();

    if (MusicEventList[MUSICEV_COMBAT] != 0)
        MusicVars.combatThreshold = MUSIC_COMBAT_THRESHOLD_ALERT;
    else if (bonusCount < 0)
        Music_SetAIVars(data->level);

    if (NisPlaying == 0) {
        bool calming = true;
        if (MusicEventList[MUSICEV_ALERT] == 2) {
            MusicUnderAttack = 1;
            bonusCount = 600;
            Music_SetAIVars((HASHCODE)MUSIC_NO_LEVEL);
        } else if (MusicEventList[MUSICEV_ALERT] != 0) {
            calming = false;
            if (MusicUnderAttackCount2-- < 0) {
                Music_SetAIVars(data->level);
                MusicUnderAttackBonusCount = 0;
                MusicUnderAttack = 1;
                MusicUnderAttackCount = data->attackCount;
            }
        }
        if (calming) {
            MusicUnderAttackBonusCount = bonusCount - 1;
            if (bonusCount < 0 && MusicUnderAttackCount-- < 0) {
                MusicUnderAttack = 0;
                MusicUnderAttackCount2 = data->alertCount;
            }
        }
    }

    if (MusicEventList[MUSICEV_DEATH] != 0) {
        Music_JumpTo(data->deathSection, 1);
        SFXFadeDown(0);
        MusicWaitForEnd = 1;
    }
    if (MusicEventList[MUSICEV_VICTORY] != 0) {
        Music_JumpTo(data->victorySection, 1);
        SFXFadeDown(0);
        MusicWaitForEnd = 1;
    }

    memcpy(BackupEventList, MusicEventList, sizeof(BackupEventList));
    if (MusicWaitForEnd == 0) {
        switch (data->level) {
        case HT_Level_HendersonA:       UpdateMFX_02MayhewA(); break;
        case HT_Level_HendersonB:       UpdateMFX_02MayhewB(); break;
        case HT_Level_HendersonC:       UpdateMFX_02MayhewC(); break;
        case HT_Level_HendersonD:       UpdateMFX_MayhewD(); break;
        case HT_Level_CastleExterior:   UpdateMFX_01CastleA(); break;
        case HT_Level_CastleCourtyard:  UpdateMFX_01CastleB(); break;
        case HT_Level_CastleIndoors1:   UpdateMFX_01CastleC(); break;
        case HT_Level_CastleIndoors2:   UpdateMFX_01CastleD(); break;
        case HT_Level_TowerA:           UpdateMFX_03TowerA(); break;
        case HT_Level_TowerB:           UpdateMFX_03TowerB(); break;
        case HT_Level_TowerC:           UpdateMFX_03TowerC(); break;
        case HT_Level_PowerStationA1:   UpdateMFX_PowerStationA1(); break;
        case HT_Level_PowerStationA2:   UpdateMFX_PowerStationA2(); break;
        case HT_Level_Tower2A:          UpdateMFX_HT_Level_Tower2A(); break;
        case HT_Level_Tower2B:          UpdateMFX_HT_Level_Tower2B(); break;
        case HT_Level_Tower2C:          UpdateMFX_HT_Level_Tower2C(); break;
        case HT_Level_EvilBase:         UpdateMFX_HT_Level_EvilBase(); break;
        case HT_Level_EvilSilo:         UpdateMFX_HT_Level_EvilSilo(); break;
        case HT_Level_EvilBaseC:        UpdateMFX_HT_Level_EvilBaseC(); break;
        case HT_Level_SpaceStationD:    UpdateMFX_HT_Level_SpaceStationD(); break;
        case HT_Level_Tower2Elevator:   UpdateMFX_HT_Level_Tower2Elevator(); break;
        default: break;
        }
    }
    memset(MusicEventList, 0, sizeof(MusicEventList));
    memcpy(LastEventList, BackupEventList, sizeof(LastEventList));
}
