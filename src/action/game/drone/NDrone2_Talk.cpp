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

#include <math.h>

// The originals, called by address: Ghidra's prototypes have untyped arguments
// The last three arguments as nightfire-port reads them: say it even over another line, move the face, and only
// with a facial anim
#define NDrone2_SpeechSFX ((int (__cdecl *)(DCVars_tag *dcv, uint sfx, char force, char face, char needFace))0x00037530)
#define Drone_maybeDisallowedFromSayingThisSfx ((bool (__cdecl *)(Drone_tag *drone, uint sfx))0x00036230)
#define DroneFunc_ChooseSFX ((uint (__cdecl *)(uint first, uint last))0x00036270)
#define Sound_IsFinnished ((bool (__cdecl *)(DYNAMICSOUNDS *handle))0x000cbac0)
#define MPDrone_MaybeIsDyingOrDead ((bool (__cdecl *)(obj_tag *gameObj))0x0001a190)
#define NDrone2_Player ((obj_tag *(__cdecl *)(void))0x00035f90)
#define NDrone2_CanSeeObject ((bool (__cdecl *)(Drone_tag *drone, obj_tag *obj, uint bone, ushort mask))0x000648b0)
// Returns in ST0, unrounded
#define Vec_SqDist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e50)

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
