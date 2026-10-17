// What drones say: the death cry, the lines said to the player when he looks at a drone, the patrol chatter, the
// Powerstation loudspeakers and the conversation in CastleIndoors2. The speech itself is NDrone2_SpeechSFX (still
// the game's), whose numbers index Drone_SfxInfo (docs/drone/behaviours/drone_sfx_info.csv).

#include "NDrone2.h"
#include "DroneTables.h"
#include "Behaviour.h"
#include "../../game.h"                 // GameState
#include "../../math/math.h"            // Vec_* (ours)
#include "../../util/Random.h"          // Rand_Rand
#include "../sp/SwitchChannels.h"       // switch_channels
#include "../mp/multiplayer.h"          // MPSettings
#include "../../sound/Sound.h"          // Sound_Play3D
#include "../../util/hashtable.h"       // hashtable_getitem
#include "DroneVision.h"
#include "NDrone2_Opponent.h"

#include <math.h>
#include <stdint.h>

// The originals, called by address: Ghidra's prototypes have untyped arguments
#define NDrone2_PlayFacialAnim ((void (__cdecl *)(DCVars_tag *dcv, HASHCODE script))0x000374e0)
#define Drone_maybeDisallowedFromSayingThisSfx ((bool (__cdecl *)(Drone_tag *drone, uint sfx))0x00036230)
#define DroneFunc_ChooseSFX ((uint (__cdecl *)(uint first, uint last))0x00036270)
// Our names. One of first..last, or of the run of as many after it for a drone with an odd id
#define DroneFunc_ChooseSFXByID ((uint (__cdecl *)(Drone_tag *drone, uint first, uint last))0x00036290)
// Says the line when Rand_Rand(0) gives 0 (it always does, but draws), and holds back the next for 5 s
#define DroneFunc_InjuredSFX ((bool (__cdecl *)(DCVars_tag *dcv, uint sfx))0x0003b830)
#define Sound_IsFinnished ((bool (__cdecl *)(DYNAMICSOUNDS *handle))0x000cbac0)
#define MPDrone_MaybeIsDyingOrDead ((bool (__cdecl *)(obj_tag *gameObj))0x0001a190)
// Returns in ST0, unrounded


// One entry of Drone_SfxInfo, per speech or sound number (docs/drone/behaviours/drone_sfx_info.csv)
typedef struct DroneSfxInfo {
    uint sfx;               // 0x00 - the sound; 0 for none
    HASHCODE facialAnim;    // 0x04 - the facial script said instead, when a face is asked for
    uint lastFrame;         // 0x08 - when it was last played
    uint interval;          // 0x0c - frames (at 60 a second) before it plays again
    float volume;           // 0x10
    float radiusOuter;      // 0x14
    float radiusInner;      // 0x18
    short pitchMin;         // 0x1c - the pitch is drawn between these
    short pitchMax;         // 0x1e
} DroneSfxInfo;
static_assert(sizeof(DroneSfxInfo) == 0x20, "DroneSfxInfo is 0x20 bytes");

#define DRONE_SFX_COUNT 0x15a

// XBE_GLOBAL(0x00174b28, 0x2b40)
#define Drone_SfxInfo ((DroneSfxInfo *)0x00174b28)

// The entry of a number, or NULL past the table (which the callers then read)
static DroneSfxInfo *SfxInfo(uint sfx) {
    return sfx < DRONE_SFX_COUNT ? &Drone_SfxInfo[sfx] : NULL;
}

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// No drone sound for the first 30 frames (at 60 a second) after the level is loaded; worked out on the x87
static bool TooSoonAfterLoad(uint now) {
    return (double)now < (double)FRAME_RATE_DIV * 30.0f + (double)NPCGlobals.postLoadFrame;
}

// Whether the entry played less than its interval ago (__ftol2 of the frame it may play again)
static bool PlayedTooRecently(const DroneSfxInfo *info, uint now) {
    return now < (uint)Ftol((double)info->interval * FRAME_RATE_DIV + (double)info->lastFrame);
}

// One Powerstation loudspeaker line: said once its channel comes on (the channel differs between the two levels)
typedef struct PowerstationLine {
    ushort channelA1;       // in PowerStationA1
    ushort channelA2;       // in PowerStationA2
    uint sfx;               // 0 ends the table
} PowerstationLine;
static_assert(sizeof(PowerstationLine) == 8, "PowerstationLine is 8 bytes");

// XBE_GLOBAL(0x00177668, 0x98)
#define PowerstationLines ((const PowerstationLine *)0x00177668)

// HandleTalking's countdown between tries (only it uses it)
// XBE_GLOBAL(0x001ee41c, 4)
#define HandleTalkingDelay I32_AT(0x001ee41c)

// The frame `ticks` (at 60 a second) from now, worked out on the x87 and truncated (__ftol2)
static uint TalkFrameAfter(double ticks) {
    return (uint)(long long)((double)FRAME_RATE_DIV * ticks + (double)(uint)GameState.NumFramesUnpaused);
}

// Whether the drone is still saying something; a line that has finished is let go.
// AUTOINJECT
bool Drone_IsTalking(Drone_tag *drone) {
    if (drone->speechChannel == NULL)
        return false;
    if (!Sound_IsFinnished(drone->speechChannel))
        return true;
    drone->speechChannel = NULL;
    return false;
}

// The death cry, by skin class; never in multiplayer, and not when it is the line being said already. Skin classes
// 5 and 6 in the Powerstation levels pick one of two, and neither if either is being said.
// AUTOINJECT
bool NDrone2_DeathTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.maybeDroneAIEnabled != 0)
        return false;

    uint sfx = 1;
    switch ((ushort)drone->skinClass) {
    case 1:
        sfx = 4;
        break;
    case 2:
        sfx = 5;
        break;
    case 3:
        sfx = 3;
        break;
    case 5:
    case 6:
        if (GameState.CurrentLevelHashcode == HT_Level_PowerStationA1 ||
            GameState.CurrentLevelHashcode == HT_Level_PowerStationA2) {
            if (Drone_maybeDisallowedFromSayingThisSfx(drone, 0xcd) || Drone_maybeDisallowedFromSayingThisSfx(drone, 0xd2))
                return false;
            sfx = Rand_Rand(2) != 0 ? 0xcd : 0xd2;
        }
        break;
    case 8:
    case 10:
        sfx = 10;
        break;
    case 12:
        sfx = 0xd;
        break;
    case 16:
        sfx = 8;
        break;
    case 18:
    case 19:
        sfx = 0xe;
        break;
    case 20:
        sfx = 2;
        break;
    default:
        sfx = drone->unknown15 ? 9 : 1;
        break;
    }

    if (Drone_maybeDisallowedFromSayingThisSfx(drone, sfx))
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 1, 0, 0);
    return true;
}

// A line on being hit: skin classes 5 to 7 in EvilBase, EvilSilo and EvilBaseC, and 16. Never in multiplayer, while
// it is still talking or once dead; for 3 s after unknown3d0 a hit says nothing one time in five.
// AUTOINJECT
bool NDrone2_HitTalkExtra(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.maybeDroneAIEnabled != 0)
        return false;
    if (Drone_IsTalking(drone))
        return false;
    if (drone->health <= 0.0f)
        return false;
    if (drone->unknown3d0 + FRAME_RATE_INT * 3 > GameState.NumFramesUnpaused && Rand_Rand(5) == 0)
        return false;

    uint sfx = 0;
    ushort skinClass = drone->skinClass;
    if (skinClass >= 5 && skinClass <= 7) {
        if (GameState.CurrentLevelHashcode == HT_Level_EvilBase ||
            GameState.CurrentLevelHashcode == HT_Level_EvilBaseC || GameState.CurrentLevelHashcode == HT_Level_EvilSilo)
            sfx = DroneFunc_ChooseSFXByID(drone, 0x133, 0x137);
    } else if (skinClass == 16) {
        sfx = DroneFunc_ChooseSFX(0xec, 0xf2);
    }
    if (sfx == 0)
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    return true;
}

// The cry of pain, by skin class (16 says its hit line instead when it can); never in multiplayer, and not when it is
// the line being said already.
// AUTOINJECT
bool NDrone2_PainTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.maybeDroneAIEnabled != 0)
        return false;

    uint sfx;
    switch ((ushort)drone->skinClass) {
    case 1:
        sfx = 0x12;
        break;
    case 2:
        sfx = 0x13;
        break;
    case 3:
        sfx = 0x11;
        break;
    case 7:
        sfx = 0x14;
        break;
    case 8:
    case 10:
        sfx = 0x18;
        break;
    case 9:
        sfx = 0x15;
        break;
    case 12:
        sfx = 0x1b;
        break;
    case 16:
        if (NDrone2_HitTalkExtra(dcv))
            return false;
        sfx = 0x16;
        break;
    case 18:
    case 19:
        sfx = 0x1c;
        break;
    case 20:
        sfx = 0x10;
        break;
    default:
        sfx = drone->unknown15 ? 0x17 : 0xf;
        break;
    }

    if (Drone_maybeDisallowedFromSayingThisSfx(drone, sfx))
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 1, 0, 0);
    return true;
}

// A line on being hurt, by level: only in multiplayer (as the original has it), and not while it is still talking or
// once dead. Skin class 16 says nothing in EvilBase, EvilSilo and EvilBaseC.
// AUTOINJECT
void DroneFunc_InjuredTalk(DCVars_tag *dcv) {
    if (MPSettings.maybeDroneAIEnabled == 0)
        return;
    Drone_tag *drone = dcv->drone;
    if (Drone_IsTalking(drone))
        return;
    if (drone->health <= 0.0f)
        return;

    uint sfx;
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_PowerStationA1:
    case HT_Level_PowerStationA2:
        sfx = DroneFunc_ChooseSFX(0xcc, 0xd1);
        break;
    case HT_Level_Tower2A:
    case HT_Level_Tower2B:
    case HT_Level_Tower2C:
    case HT_Level_Tower2Elevator:
        sfx = 0xb9;
        break;
    case HT_Level_EvilBase:
    case HT_Level_EvilSilo:
    case HT_Level_EvilBaseC:
        if (drone->skinClass == 16)
            return;
        sfx = DroneFunc_ChooseSFXByID(drone, 0x133, 0x137);
        break;
    default:
        return;
    }
    if (sfx != 0)
        DroneFunc_InjuredSFX(dcv, sfx);
}

// A line said to the player while he looks at the drone (inPlayersSights), turning its head to him for trackTicks:
// then nothing more for waitTicks (at 60 a second), and only once while he keeps looking unless `repeat` is set.
// stateScratch counts the lines, stateScratch2 is the next frame it may speak, stateScratch3 the lines since he
// started looking; 0xff in stateScratch (a state just entered) clears all three once he looks. True when it spoke,
// or tried: a line NDrone2_SpeechSFX turned down with no facial anim playing counts too, without the wait.
// AUTOINJECT
bool DroneFunc_DoBondTalk(DCVars_tag *dcv, uint sfx, int trackTicks, uint waitTicks, char repeat) {
    Drone_tag *drone = dcv->drone;
    obj_tag *player = NDrone2_Player();

    if (drone->stateScratch == 0xff && drone->inPlayersSights) {
        drone->stateScratch = 0;
        drone->stateScratch2 = 0;
        drone->stateScratch3 = 0;
    }
    if (!drone->inPlayersSights) {
        if (!repeat)
            drone->stateScratch = 0;
        drone->stateScratch3 = 0;
    }

    if (Drone_IsTalking(drone))
        return false;
    if (!drone->inPlayersSights || drone->stateScratch2 > GameState.NumFramesUnpaused)
        return false;
    if (drone->stateScratch3 != 0 && !repeat)
        return false;

    NDrone2_HeadTrackObj(player, trackTicks, 0.0f, dcv);
    if (NDrone2_SpeechSFX(dcv, sfx, 0, 1, 1) != 0 || drone->facialAnim != NULL) {
        drone->stateScratch++;
        drone->stateScratch2 = TalkFrameAfter(waitTicks);
        drone->stateScratch3++;
    }
    return true;
}

// What the guests in CastleIndoors1 say to the player, from the civilian, party girl and guard states: only while
// the drone is drawn, unarmed, within 3 m of the player and facing him. By skin class:
//   9:  0x95 or 0x96, once while he looks at it; again only after the state is entered anew
//   13: one of eleven lines in the skin's voice every 10 s while he looks (the first within a second)
//   14: one of 0x84-0x8c through DroneFunc_DoBondTalk
//   16: one of 0x97-0x9a every 8 s while he looks
//   17: 0x8e every 8 s while he looks (the first within a second)
//   18: a six-line conversation, whether he looks or not, 2 s after the state is entered
// False when it is not the time or place (or the conversation is over), else true.
// AUTOINJECT
bool NDrone2_BondTalk(DCVars_tag *dcv) {
    obj_tag *gameObj = dcv->gameObj;
    Drone_tag *drone = dcv->drone;
    if (!gameObj->drawnCountdown)
        return false;
    if (drone->distanceToTarget > 3.0f)
        return false;
    if (fabsf(drone->opponentBearing) > (float)M_PI_2)
        return false;
    if (NDrone2_ObjectIsArmed(gameObj))
        return false;
    if (GameState.CurrentLevelHashcode != HT_Level_CastleIndoors1)
        return false;
    obj_tag *player = NDrone2_Player();
    if (player == NULL)
        return false;

    uint now = GameState.NumFramesUnpaused;
    switch ((ushort)drone->skinClass) {
    case 9:
        if (Drone_IsTalking(drone))
            return true;
        if (drone->stateScratch == 0xff) {
            if (drone->inPlayersSights) {
                drone->stateScratch = 0;
                drone->stateScratch2 = 0;
            }
            return true;
        }
        if (!drone->inPlayersSights || drone->stateScratch >= 1 || drone->stateScratch2 >= now)
            return true;
        NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
        if (NDrone2_SpeechSFX(dcv, 0x95 + Rand_Rand(2), 0, 1, 1) == 0 && drone->facialAnim == NULL)
            return true;
        drone->stateScratch2 = TalkFrameAfter(480.0f);
        drone->stateScratch = 1;
        return true;

    case 13: {
        if (Drone_IsTalking(drone))
            return true;
        if (drone->stateScratch == 0xff) {
            if (drone->inPlayersSights) {
                drone->stateScratch = Rand_Rand(7);
                drone->stateScratch2 = Rand_Rand(FRAME_RATE_INT) + GameState.NumFramesUnpaused;
            }
            return true;
        }
        if (!drone->inPlayersSights || drone->stateScratch >= 7 || drone->stateScratch2 >= now)
            return true;
        ushort voice;
        switch ((uint)drone->skinHashcode) {
        case 0x5000020:
            voice = 0;
            break;
        case 0x5000022:
            voice = 1;
            break;
        case 0x5000023:
            voice = 2;
            break;
        default:
            voice = 3;
            break;
        }
        NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
        if (NDrone2_SpeechSFX(dcv, Rand_Rand(11) * 4 + voice + 0x58, 0, 1, 1) == 0 && drone->facialAnim == NULL)
            return true;
        drone->stateScratch2 = TalkFrameAfter(600.0f);
        drone->stateScratch = Rand_Rand(7);
        return true;
    }

    case 14:
        DroneFunc_DoBondTalk(dcv, DroneFunc_ChooseSFX(0x84, 0x8c), 200, 480, 0);
        return true;

    case 16:
        if (Drone_IsTalking(drone))
            return true;
        if (drone->stateScratch == 0xff) {
            if (drone->inPlayersSights) {
                drone->stateScratch = 0;
                drone->stateScratch2 = 0;
            }
            return true;
        }
        if (!drone->inPlayersSights || drone->stateScratch >= 1 || drone->stateScratch2 >= now)
            return true;
        NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
        if (NDrone2_SpeechSFX(dcv, 0x97 + Rand_Rand(4), 0, 1, 1) == 0 && drone->facialAnim == NULL)
            return true;
        drone->stateScratch2 = TalkFrameAfter(480.0f);
        drone->stateScratch = 0;
        return true;

    case 17:
        if (Drone_IsTalking(drone))
            return true;
        if (drone->stateScratch == 0xff) {
            if (drone->inPlayersSights) {
                drone->stateScratch = 0;
                drone->stateScratch2 = Rand_Rand(FRAME_RATE_INT) + GameState.NumFramesUnpaused;
            }
            return true;
        }
        if (!drone->inPlayersSights || drone->stateScratch >= 1 || drone->stateScratch2 >= now)
            return true;
        NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
        if (NDrone2_SpeechSFX(dcv, 0x8e, 0, 1, 1) == 0 && drone->facialAnim == NULL)
            return true;
        drone->stateScratch2 = TalkFrameAfter(480.0f);
        return true;

    case 18: {
        if (Drone_IsTalking(drone))
            return true;
        uint line = drone->stateScratch;
        if (line == 0xff) {
            drone->stateScratch = 0;
            drone->stateScratch2 = GameState.NumFramesUnpaused + FRAME_RATE_INT * 2;
            return true;
        }
        if (line >= 7)
            return false;
        if (drone->stateScratch2 >= now)
            return true;

        // Each line: the head turned to the player for so long (or not), the line, and the wait before the next
        double wait;
        switch (line) {
        case 0:
            NDrone2_HeadTrackObj(player, (int)(long long)((double)FRAME_RATE_DIV * 180.0f), 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, 0x52, 0, 1, 1) == 0 && drone->facialAnim == NULL)
                return true;
            wait = 960.0f;
            break;
        case 1:
            NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, 0x53, 0, 1, 1) == 0 && drone->facialAnim == NULL)
                return true;
            wait = 150.0f;
            break;
        case 2:
            if (NDrone2_SpeechSFX(dcv, 0x54, 0, 0, 0) == 0 && drone->facialAnim == NULL)
                return true;
            wait = 480.0f;
            break;
        case 3:
            NDrone2_HeadTrackObj(player, 240, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, 0x55, 0, 1, 1) == 0 && drone->facialAnim == NULL)
                return true;
            wait = 240.0f;
            break;
        case 4:
            if (NDrone2_SpeechSFX(dcv, 0x56, 0, 0, 0) == 0 && drone->facialAnim == NULL)
                return true;
            wait = 240.0f;
            break;
        case 5:
            NDrone2_HeadTrackObj(player, 180, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, 0x57, 0, 1, 1) == 0 && drone->facialAnim == NULL)
                return true;
            wait = 300.0f;
            break;
        default:
            // The end: the head let go
            NDrone2_HeadTrackObj(NULL, 600, 0.0f, dcv);
            drone->stateScratch++;
            return true;
        }
        drone->stateScratch2 = TalkFrameAfter(wait);
        drone->stateScratch++;
        return true;
    }

    default:
        return true;
    }
}

// Every live, active drone of type Mayhew says the line. The first such drone that is not active ends the search,
// so those after it say nothing.
// AUTOINJECT
void Drone_MayhewTalk(uint sfx) {
    if (Drone_bDisableSystem)
        return;
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        if (drone->dtype != DTYPE_Mayhew)
            continue;
        if (MPDrone_MaybeIsDyingOrDead(drone->gameObj))
            return;
        DCVars_tag dcv;
        if (Drone_DCVfromOBJ(drone->gameObj, &dcv))
            NDrone2_SpeechSFX(&dcv, sfx, 0, 0, 0);
    }
}

// Now and then a patrolling guard (skin class 3) mutters one of five lines: a chance in two at most every 5 s, then
// none for 10 s, shared by every drone. Never in multiplayer or once dead.
// AUTOINJECT
bool NDrone2_PatrolTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.maybeDroneAIEnabled != 0)
        return false;
    if (drone->health <= 0.0f)
        return false;
    if (Drone_IsTalking(drone))
        return false;
    if (NPCGlobals.nextPatrolTalkFrame > GameState.NumFramesUnpaused)
        return false;
    if (Rand_Rand(2) != 0) {
        NPCGlobals.nextPatrolTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 5;
        return false;
    }
    if (drone->skinClass != 3)
        return false;
    uint sfx = DroneFunc_ChooseSFX(0xb3, 0xb7);
    NPCGlobals.nextPatrolTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 10;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    return true;
}

// The Powerstation loudspeakers: while channel 0x98 is on, an active drone that is not talking says the line of
// each channel that has come on, except the last one said. Always false.
// AUTOINJECT
bool NDrone2_PowerstationTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (Drone_IsTalking(drone))
        return false;
    if (MPDrone_MaybeIsDyingOrDead(drone->gameObj) || !switch_channels[0x98])
        return false;
    bool a1 = GameState.CurrentLevelHashcode == HT_Level_PowerStationA1;
    if (!a1 && GameState.CurrentLevelHashcode != HT_Level_PowerStationA2)
        return false;
    if (PowerstationLines[0].sfx == 0)
        return false;

    const PowerstationLine *line = PowerstationLines;
    do {
        uint channel = a1 ? line->channelA1 : line->channelA2;
        if (channel != 0 && channel != NPCGlobals.lastPowerstationChannel && switch_channels[channel] &&
            NDrone2_SpeechSFX(dcv, line->sfx, 0, 0, 0) != 0)
            NPCGlobals.lastPowerstationChannel = channel;
        line++;
    } while (line->sfx != 0);
    return false;
}

// Each frame, per drone: the Powerstation loudspeakers, or in CastleIndoors2 the guards' (skin class 12) talk to a
// player within 3 m they can see, tried every 30 frames at most: three lines in turn (5 s after the first, 15 s
// after the others), or once channel 0x4a is on, a last one that ends it.
void _NDrone2_HandleTalking(DCVars_tag *dcv) {
    if (Drone_bDisableSystem)
        return;
    Drone_tag *drone = dcv->drone;
    obj_tag *player = NDrone2_Player();

    if (GameState.CurrentLevelHashcode != HT_Level_CastleIndoors2) {
        if (GameState.CurrentLevelHashcode == HT_Level_PowerStationA1 ||
            GameState.CurrentLevelHashcode == HT_Level_PowerStationA2)
            NDrone2_PowerstationTalk(dcv);
        return;
    }

    if (drone->skinClass != 12 || player == NULL)
        return;
    if (!(Vec_SqDist2D(&player->position, &dcv->gameObj->position) < 9.0f))
        return;
    if (drone->talkStep == 99 || drone->nextTalkFrame > GameState.NumFramesUnpaused)
        return;
    if (HandleTalkingDelay-- > 0)
        return;
    HandleTalkingDelay = 30;
    if (!NDrone2_CanSeeObject(drone, player, 5, 0))
        return;

    if (switch_channels[0x4a]) {
        NDrone2_SpeechSFX(dcv, 0x9e, 1, 0, 0);
        drone->talkStep = 99;
        return;
    }
    switch (drone->talkStep) {
    case 0:
        NDrone2_SpeechSFX(dcv, 0x9b, 1, 1, 0);
        drone->talkStep++;
        drone->nextTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 5;
        break;
    case 1:
        NDrone2_SpeechSFX(dcv, 0x9c, 1, 1, 0);
        drone->talkStep++;
        drone->nextTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 15;
        break;
    case 2:
        NDrone2_SpeechSFX(dcv, 0x9d, 1, 1, 0);
        drone->talkStep--;
        drone->nextTalkFrame = GameState.NumFramesUnpaused + FRAME_RATE_INT * 15;
        break;
    }
}

// The original takes the DCVars in EDI.
// AUTOLTCG
void __declspec(naked) NDrone2_HandleTalking(void) {
    _asm {
        push edi
        call _NDrone2_HandleTalking
        add esp, 4
        ret
    }
}

// A Powerstation guard (skin class 5 or 6) says it has seen its opponent, one of 0xd6 and 0xd7, and its target is
// marked seen where the opponent was. Not with no player, in multiplayer, while it talks or once dead.
// AUTOINJECT
bool DroneFunc_NewSightTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (NDrone2_Player() == NULL || MPSettings.maybeDroneAIEnabled != 0)
        return false;
    if (Drone_IsTalking(drone))
        return false;
    if (drone->health <= 0.0f)
        return false;
    if ((ushort)drone->skinClass < 5 || (ushort)drone->skinClass > 6)
        return false;
    if (GameState.CurrentLevelHashcode != HT_Level_PowerStationA1 &&
        GameState.CurrentLevelHashcode != HT_Level_PowerStationA2)
        return false;
    uint sfx = DroneFunc_ChooseSFX(0xd6, 0xd7);
    if (sfx == 0)
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    DroneFunc_TargetSeen(dcv->drone, &dcv->drone->opponentSeenPos);
    return true;
}

// The same guards, in the player's cel, say they have lost sight of him: one of 0xd3 to 0xd5. The player is not
// checked for NULL.
// AUTOINJECT
bool DroneFunc_LostSightTalk(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if (MPSettings.maybeDroneAIEnabled != 0)
        return false;
    if (Drone_IsTalking(drone))
        return false;
    if (drone->health <= 0.0f)
        return false;
    if ((ushort)drone->skinClass < 5 || (ushort)drone->skinClass > 6)
        return false;
    if (GameState.CurrentLevelHashcode != HT_Level_PowerStationA1 &&
        GameState.CurrentLevelHashcode != HT_Level_PowerStationA2)
        return false;
    obj_tag *player = NDrone2_Player();
    if (drone->gameObj->inCel != player->inCel)
        return false;
    uint sfx = DroneFunc_ChooseSFX(0xd3, 0xd5);
    if (sfx == 0)
        return false;
    NDrone2_SpeechSFX(dcv, sfx, 0, 0, 0);
    DroneFunc_TargetSeen(dcv->drone, &dcv->drone->opponentSeenPos);
    return true;
}

// Plays a Drone_SfxInfo sound at pos (or at the drone), its range scaled, at a pitch drawn from the entry's: not in
// the first frames after a load, nor with `check` within the entry's interval. Each try is logged in
// NPCGlobals.sfxLog and the frame kept in unknown3d0. A number past the table is read through NULL.
// AUTOINJECT
DYNAMICSOUNDS* NDrone2_PlaySFX(DCVars_tag *dcv, uint sfx, _VECTOR *pos, char check, float scale) {
    DroneSfxInfo *info = SfxInfo(sfx);
    if (info->sfx == 0)
        return NULL;
    uint now = GameState.NumFramesUnpaused;
    if (TooSoonAfterLoad(now))
        return NULL;
    if (check && PlayedTooRecently(info, now))
        return NULL;

    short pitch = 0;
    if (info->pitchMax != 0 || info->pitchMin != 0)
        pitch = (short)(Rand_Rand(info->pitchMax - info->pitchMin) + info->pitchMin);
    info->lastFrame = now;

    _VECTOR at;
    Vec_Zero(&at);
    if (pos != NULL)
        Vec_Copy(pos, &at);
    else if (dcv != NULL)
        Vec_Copy(&dcv->gameObj->position, &at);

    DYNAMICSOUNDS *handle = Sound_Play3D((Action_SFX)info->sfx, &at, info->volume, scale * info->radiusOuter,
                                         scale * info->radiusInner, pitch, dcv->aiStateMachine->id, 0);
    uint logged = 0;
    if (handle != NULL) {
        logged = sfx;
        info->lastFrame = now;
    }
    dcv->drone->unknown3d0 = now;
    NPCGlobals.sfxLog[NPCGlobals.sfxLogNext].sfx = logged;
    NPCGlobals.sfxLog[NPCGlobals.sfxLogNext].handle = handle;
    if (++NPCGlobals.sfxLogNext >= DRONE_SFX_LOG)
        NPCGlobals.sfxLogNext = 0;
    return handle;
}

// A line spoken, never in multiplayer, nor in the first frames after a load or within its interval. With `face`, an
// entry with a facial script (still loaded) plays that instead, unless one is playing; with needFace, so does one
// without, which then says nothing. Otherwise the sound, unless the drone is talking (and not `force`): it becomes
// speechChannel. Returns the sound, or NULL.
// AUTOINJECT
DYNAMICSOUNDS* NDrone2_SpeechSFX(DCVars_tag *dcv, uint sfx, char force, char face, char needFace) {
    if (MPSettings.maybeDroneAIEnabled != 0)
        return NULL;
    Drone_tag *drone = dcv->drone;
    uint now = GameState.NumFramesUnpaused;
    if (TooSoonAfterLoad(now))
        return NULL;
    DroneSfxInfo *info = SfxInfo(sfx);
    if (PlayedTooRecently(info, now))
        return NULL;

    if (face && (info->facialAnim != 0 || needFace)) {
        if (info->facialAnim == 0 || hashtable_getitem(info->facialAnim) != NULL) {
            if (info->facialAnim != 0 && !dcv->drone->facialAnimPlaying && !dcv->drone->unknown37)
                NDrone2_PlayFacialAnim(dcv, info->facialAnim);
            if (drone->facialAnim != NULL)
                info->lastFrame = now;
            return NULL;
        }
        // The script is no longer loaded: the entry goes on without it
        info->facialAnim = (HASHCODE)0;
    }

    if (info->sfx == 0 || info->facialAnim != 0)
        return NULL;
    if (!force && drone->speechChannel != NULL)
        return NULL;
    drone->speechChannel = NDrone2_PlaySFX(dcv, sfx, &dcv->gameObj->position, 0, 2.0f);
    return drone->speechChannel;
}

// NDrone2_SpeechSFX for a drone rather than its DCVars. The DCVars are used whether Drone_DCVfromOBJ finds them or
// not (ours start zeroed where the original's are stack).
// AUTOINJECT
void NDrone2_SpeechSFXd(Drone_tag *drone, uint sfx, char force, char face, char needFace) {
    DCVars_tag dcv = {};
    Drone_DCVfromOBJ(drone->gameObj, &dcv);
    NDrone2_SpeechSFX(&dcv, sfx, force, face, needFace);
}
