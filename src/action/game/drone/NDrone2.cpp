#include "NDrone2.h"
#include "../sp/SwitchChannels.h" // switch_channels, switch_channels_MusicVars
#include "../../sound/music.h"    // Music_Event (ours, sound/music.cpp)
#include "../../game.h"           // GameState
#include "../../util/Random.h"      // Rand_Rand (AUTOGEN'd in util/Random.cpp: still the original)
#include "../../engine/Collide.h"   // HITDATA_tag
#include "../obj/bullet.h"          // BU_tag
#include "../obj/Player.h"          // BLData
#include "../obj/control.h"         // control_first_object
#include "../sp/PlayerStats.h"      // PlrStat_OkToUpdate
#include "../mp/multiplayer.h"      // MPSettings, MP_getObjectTeam
#include "BOT.h"
#include <bit>

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

// ---------------------------------------------------------------------------------------------------------------

// Stun weapons with their own recovery base (weapon_definition_tag.weaponVariantNum). 0x43/0x44 and 0x4a/0x4c are
// the dart gun and taser and their upgrades (UpgradedDartGuns / UpgradedTasers in game/Upgrade.cpp); 0x35 is
// unnamed (the PS2 build has the same case, no name either).
#define WEAPVAR_STUN_0x35 0x35
#define WEAPVAR_DARTGUN 0x43
#define WEAPVAR_DARTGUN_UPGRADED 0x44
#define WEAPVAR_TASER 0x4a
#define WEAPVAR_TASER_UPGRADED 0x4c

#define RECOVER_BASE_SECONDS 60
#define REACTION_STAT_SCALE 200     // reaction and recovery stats count down from this: lower stat = slower

// How long, in frames, a stunned drone stays down (NDrone2_DSTATE_Taser, _Stunned, _StunGrenade*, _StunDart*
// store it at Drone_tag+0x938): (200 - recovery stat) percent of a base time. The base is 60 seconds, or per stun
// weapon when the hit came from a bullet object: 30 s, 60 s (dart), 120 s (upgraded dart), 10 s (either taser).
//
// The original works in x87 floats and truncates with __ftol2 (the high half in EDX is never used: the callers
// store EAX). Every product here is exact in a double (the frame counts are integers and 0.01f has a 24-bit
// mantissa), so double arithmetic truncates the same way - including the quirk that 0.01f is slightly under 0.01,
// so an exact result such as (200-80) x 3600 / 100 = 4320 comes out as 4319.
//
// AUTOINJECT
uint DroneFunc_RecoverTime(DCVars_tag *dcv, HITDATA_tag *hit) {
    Drone_tag *drone = dcv->drone;
    // FRAME_RATE_INT is unsigned: the original corrects the fild by 2^32 when it looks negative
    double baseFrames = (double)FRAME_RATE_INT * 60.0f;

    if (hit != NULL && hit->hitObj != NULL && hit->hitObj->objectType == OBJECTTYPE_BULLET) {
        BU_tag *bullet = (BU_tag *)hit->hitObj->extraObjectData;
        // In the weapon cases the multiply is an integer one (wrapping), then converted as unsigned
        switch ((ushort)bullet->wpnDef->weaponVariantNum) {
        case WEAPVAR_STUN_0x35:
            baseFrames = (double)(uint)(FRAME_RATE_INT * 30);
            break;
        case WEAPVAR_DARTGUN:
            baseFrames = (double)(uint)(FRAME_RATE_INT * 60);
            break;
        case WEAPVAR_DARTGUN_UPGRADED:
            baseFrames = (double)(uint)(FRAME_RATE_INT * 120);
            break;
        case WEAPVAR_TASER:
        case WEAPVAR_TASER_UPGRADED:
            baseFrames = (double)(uint)(FRAME_RATE_INT * 10);
            break;
        default:
            break;
        }
    }

    // ((200 - recover) * base) * 0.01f, in that order. A recovery stat over 200 gives a negative count (__ftol2 of
    // a negative value; only the low 32 bits are kept).
    double frames = (double)(REACTION_STAT_SCALE - (int)drone->recover) * baseFrames * (double)0.01f;
    return (uint)(long long)frames;
}

// How many frames of continuous sight a drone needs before it registers the opponent (DroneVision_HaveOpponentSight
// compares it, unsigned, with Drone_tag+0x200). Only on Henderson B/C, Castle Exterior and Tower A/B does it
// depend on the drone: 0 once it has reacted to a first sighting, or when it is already fully alert; otherwise
// (200 x FRAME_RATE_DIV - reaction) x (1 - alertness) x 30 / 100. Everywhere else a flat 10 x FRAME_RATE_DIV.
//
// x87 in the original, truncated with __ftol2 (EAX only; the return-0 path does not set EDX). The product of
// the last two multiplies can need more than 53 bits for odd alertness values, so a double could in principle
// round across an integer where the x87 did not - the shadow test covers it (notes.md).
//
// AUTOINJECT
uint DroneFunc_ReactionTime(Drone_tag *drone) {
    switch (GameState.CurrentLevelHashcode) {
    case HT_Level_HendersonB:
    case HT_Level_HendersonC:
    case HT_Level_CastleExterior:
    case HT_Level_TowerA:
    case HT_Level_TowerB:
        if (drone->sightFlags & DRONE_SIGHT_REACTED)
            return 0;
        // fcomp/test ah,1: computed when alertness < 1.0 or unordered (NaN), so only >= 1.0 returns 0
        if (drone->alertness >= 1.0f)
            return 0;
        {
            double a = (double)FRAME_RATE_DIV * 200.0f - (int)drone->reaction;
            double b = (1.0f - (double)drone->alertness) * 30.0f;
            return (uint)(long long)(a * b * (double)0.01f);
        }
    default:
        return (uint)(long long)((double)FRAME_RATE_DIV * 10.0f);
    }
}

// Arms the idle/patrol timer: PreDroneControl sends message 4 once GameState.NumFramesUnpaused passes
// Drone_tag.stateTimeoutFrame. The wait is minSeconds plus a random 0..randSeconds-1 whole seconds (callers:
// Idle, Alert, Patrol with (dcv, 45, 5) and similar; SpaceDrake).
//
// AUTOINJECT
void NDrone2_SetIdleTimeOut(DCVars_tag *dcv, int minSeconds, uint randSeconds) {
    uint seconds = Rand_Rand(randSeconds) + minSeconds;
    dcv->drone->stateTimeoutFrame = seconds * FRAME_RATE_INT + GameState.NumFramesUnpaused;
}

// Records a change of the drone's alert status: the old one, the frame it changed, and a changed flag. Setting
// the status it already has does nothing (the frame is not refreshed).
//
// AUTOINJECT
void Drone_AlertStatusSet(char newStatus, DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if ((char)drone->alertStatus == newStatus)
        return;
    drone->prevAlertStatus = drone->alertStatus;
    drone->alertStatus = newStatus;
    drone->alertStatusChangedFrame = GameState.NumFramesUnpaused;
    drone->alertStatusChanged = 1;
}

// ---------------------------------------------------------------------------------------------------------------

// AUTOGEN
void __cdecl NDrone2_SetOpponent(Drone_tag *param_1, obj_tag *param_2);
// AUTOGEN
bool __cdecl MPDrone_MaybeIsDyingOrDead(obj_tag *param_1);
// AUTOGEN
int __cdecl BOT_handleOpponentHistory(int param_1, int param_2);
// AUTOGEN
undefined4 __cdecl BOTSTATE_increaseDistraction(int param_1, float param_2);
// AUTOGEN
void * __stdcall MP_getProtectionObj_NotExt(void);
// AUTOGEN
obj_tag * __stdcall MP_getDemolitionObj_NotExt(void);
bool __cdecl NDrone2_CanSeeObject(Drone_tag *param_1, obj_tag *param_2, uint param_3, ushort param_4);
void __cdecl MP_sendBotMessage(obj_tag *param_1, uint param_2, uint param_3, uint param_4);
// AUTOGEN
undefined4 __cdecl FUN_00037bc0(int param_1, int param_2, _VECTOR *param_3);

// The squared distance in x and z, left unrounded in ST0 (Ghidra has no prototype for it)
#define Vec_SqDist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e50)

// The game's radians-to-degrees factor, one bit under 180/pi as a float
static constexpr float RadToDeg = 57.295776f;
static_assert(std::bit_cast<uint32_t>(RadToDeg) == 0x42652ee0, "not the original's 180/pi");

#define FAR_AWAY_SQ 640000.0f       // 800 m, squared: no candidate yet

// BOT_stats_t.personality
#define BOT_PERSONALITY_GUARDIAN 2
#define BOT_PERSONALITY_BERSERKER 5

// Drone_tag.flags
#define DRONE_FLAG_AI_RUNNING 0x100
#define DRONE_FLAG_DEAD 0x200
#define DRONE_FLAG_DYING 0x400

// The bearing from `from` to `to`, relative to where `from` faces, in degrees (unrounded: the product stays in ST0)
static double NDrone2_AngleTo(obj_tag *from, obj_tag *to) {
    _VECTOR v;
    Vec_Subtract(&from->position, &to->position, &v);
    return Vec_AngleDifference(from->rotation.y, maybeAtan2(v.x, v.z) + (float)M_PI) * (double)RadToDeg;
}

// A multiplayer bot's choice of opponent, from what it knows of every agent (BOT_vars_t.players): the nearest one
// present and visible within its sight range (combatRanges[1]), weighted - a preferred opponent and an agent aiming
// at it count as 25 times nearer, an agent with any of MPGame.players[].flags's low 4 bits set as 4 or 16 times
// nearer, and for Guardians and Berserkers an agent another bot already targets (or a guarded bot) as 16 times
// farther. The best one in view (within combatRanges[0] either side) goes to BOT_handleOpponentHistory, whose result
// is returned. A bot attacking on sight (trait 8) sees out to 300 m and takes the nearest whatever its angle.
// Failing that, in Protection and Demolition the attacking side takes the objective when it is within 4 m, in view
// and visible.
static int NDrone2_FindBotOpponent(Drone_tag *drone) {
    obj_tag *self = drone->gameObj;
    if (MPDrone_MaybeIsDyingOrDead(self)) {
        if (drone->opponent != NULL)
            NDrone2_SetOpponent(drone, NULL);
        return 0;
    }

    BOT_vars_t *bot = drone->botVars;
    int bestIdx = -1;
    obj_tag *best = NULL;
    float bestDist = FAR_AWAY_SQ;
    MPTeam team = MP_getObjectTeam(self);
    ushort botIndex = bot->botIndex;
    float halfFov = drone->combatRanges[0] * RadToDeg;
    bool takeNearest = bot->atObjective;
    float rangeSq = drone->combatRanges[1] * drone->combatRanges[1];

    // The index is read signed (movsx)
    obj_tag *preferred = NULL;
    if (bot->preferredOpponent != 0xff)
        preferred = MPGame.players[(signed char)bot->preferredOpponent].playerObj;
    bool attackOnSight = bot->stats.traitFlags & 8;
    if (preferred != NULL && preferred->objectType != OBJECTTYPE_PLAYER && preferred->objectType != OBJECTTYPE_DRONE) {
        preferred = NULL;
        bot->preferredOpponent = 0xff;
    }

    DCVars_tag dcVars;
    Drone_DCVfromOBJ(self, &dcVars);    // not used
    bot->atObjective = 0;
    if (attackOnSight) {
        takeNearest = true;
        rangeSq = 90000.0f;
    }

    // Keep a live opponent seen within the last 20 seconds (scaled by aggression); else drop it - but in Protection
    // and Demolition only a drone or player, not the objective.
    obj_tag *opponent = drone->opponent;
    if (opponent != NULL) {
        bool keep = false;
        if (opponent->objectType != OBJECTTYPE_GFX
            && !(opponent->objectType == OBJECTTYPE_DRONE && MPDrone_MaybeIsDyingOrDead(opponent))) {
            uint interval = (uint)(long long)(BOT_getAggressionMul(drone) * ((double)_FRAME_RATE * 20.0f));
            keep = drone->framesSinceSeen <= interval;
        }
        if (!keep) {
            if ((MPSettings.GameMode != GM_PROTECTION && MPSettings.GameMode != GM_DEMOLITION)
                || drone->opponent->objectType == OBJECTTYPE_DRONE || drone->opponent->objectType == OBJECTTYPE_PLAYER)
                NDrone2_SetOpponent(drone, NULL);
        }
    }

    float score[NUM_AGENTS];
    bool aimingAtMe[NUM_AGENTS];
    for (int i = 0; i < NUM_AGENTS; i++) {
        obj_tag *agent = MPGame.players[i].playerObj;
        BOT_playerInfo_t *info = &bot->players[i];
        score[i] = FAR_AWAY_SQ;
        aimingAtMe[i] = false;

        if (agent == NULL || agent == self)
            continue;
        char type = agent->objectType;
        if (type == OBJECTTYPE_DRONE && MPDrone_MaybeIsDyingOrDead(agent))
            continue;
        if (type == OBJECTTYPE_DEAD_DRONE || type == OBJECTTYPE_DEAD_PLAYER || type == OBJECTTYPE_GFX)
            continue;
        uchar flags = info->flags;
        if (!(flags & BOTPLAYER_PRESENT) || !(info->distanceSq < rangeSq))
            continue;
        if (!(flags & BOTPLAYER_VISIBLE) && !attackOnSight)
            continue;

        if (flags & BOTPLAYER_TEAMMATE) {
            if (!attackOnSight)
                NDrone2_AngleTo(self, agent);   // worked out and not used
            continue;
        }

        // An x87 chain: rounded to a float only where the original stores it
        double dist = info->distanceSq;
        ushort status = MPGame.players[i].flags;
        if (agent == preferred)
            dist *= 0.04f;
        if (status & 0xf)
            dist *= 0.25f;
        if (info->facingAngle > -10.0f && info->facingAngle < 10.0f) {
            if (flags & BOTPLAYER_FIRING) {
                if (type != OBJECTTYPE_DRONE || ((Drone_tag *)agent->extraObjectData)->opponent == self) {
                    dist = (float)(dist * 0.04f);
                    aimingAtMe[i] = true;
                    BOTSTATE_increaseDistraction((int)drone, 4.0f);
                }
            } else if (status & 0xf) {
                dist *= 0.25f;
            }
        }

        if (agent != drone->opponent && agent != preferred
            && (bot->stats.personality == BOT_PERSONALITY_BERSERKER || bot->stats.personality == BOT_PERSONALITY_GUARDIAN)) {
            bool taken = false;
            if (i < NUM_PLAYERS) {
                for (int j = NUM_PLAYERS; j < NUM_AGENTS; j++) {
                    obj_tag *other = MPGame.players[j].playerObj;
                    if (j != i && other != NULL && ((Drone_tag *)other->extraObjectData)->opponent == agent) {
                        taken = true;
                        break;
                    }
                }
            } else {
                taken = ((Drone_tag *)agent->extraObjectData)->botVars->targeted != 0;
            }
            if (taken)
                dist *= 16.0f;
        }

        score[i] = (float)dist;
        if (dist < bestDist) {
            bestDist = (float)dist;
            bestIdx = i;
            best = agent;
        }
    }

    if (bestIdx != -1) {
        if (best == drone->opponent)
            return BOT_handleOpponentHistory((int)drone, (int)best);

        // The best one in view; each one out of view is struck off and the next best tried
        while (best != NULL) {
            if (takeNearest || aimingAtMe[bestIdx])
                return BOT_handleOpponentHistory((int)drone, (int)best);

            double angle;
            if (bestIdx < NUM_PLAYERS)
                angle = NDrone2_AngleTo(self, best);
            else    // a bug: this bot's entry is players[bot->playerIndex], not [botIndex]
                angle = ((Drone_tag *)best->extraObjectData)->botVars->players[botIndex].facingAngle;
            if (!(angle < -halfFov) && !(angle > halfFov))
                return BOT_handleOpponentHistory((int)drone, (int)best);

            score[bestIdx] = FAR_AWAY_SQ;
            bestIdx = -1;
            float lowest = FAR_AWAY_SQ;
            for (int i = 0; i < NUM_AGENTS; i++) {
                if (score[i] < lowest) {
                    lowest = score[i];
                    bestIdx = i;
                }
            }
            if (bestIdx == -1)
                break;
            best = MPGame.players[bestIdx].playerObj;
        }
    }

    obj_tag *objective;
    if (MPSettings.GameMode == GM_PROTECTION) {
        if (team != PHOENIX)
            return 0;
        objective = (obj_tag *)MP_getProtectionObj_NotExt();
    } else if (MPSettings.GameMode == GM_DEMOLITION) {
        if (team != MI6)
            return 0;
        objective = MP_getDemolitionObj_NotExt();
    } else {
        return 0;
    }
    if (objective == NULL || objective->subState == 1 || objective == drone->opponent)
        return 0;
    if (!(Vec_SqDist3D(&self->position, &objective->position) < 16.0f))
        return 0;
    double angle = NDrone2_AngleTo(self, objective);
    if (!(angle > -halfFov) || !(angle < halfFov))
        return 0;
    if (!NDrone2_CanSeeObject(drone, objective, 5, 0))
        return 0;
    MP_sendBotMessage(drone->gameObj, 0x3b, 1, 0);
    return BOT_handleOpponentHistory((int)drone, (int)objective);
}

// An ally's choice (single player): a copter (curState 1..3) from 10 degrees one side to 30 the other (as
// FUN_00037bc0 measures), else its opponent while it can see it and it is alive, else a visible live enemy drone
// nearer than the opponent. The drone list is walked round robin from where the last call stopped; a call stops at
// the first drone it passes over once any line-of-sight test has been made since `sightTests` was read (at entry).
// Returns the new enemy drone's object, else 0.
static int NDrone2_FindAllyOpponent(Drone_tag *drone, ushort sightTests) {
    float bestDist = FAR_AWAY_SQ;

    for (obj_tag *obj = control_first_object(); obj != NULL; ) {
        obj_tag *next = obj->nextObject;
        if (obj->objectType == OBJECTTYPE_COPTER && (short)obj->curState > 0 && (short)obj->curState < 4) {
            _VECTOR dir;
            bool measured = (uchar)FUN_00037bc0((int)drone, (int)obj, &dir);
            if (!(measured && dir.x > 0.17453294f) && !(dir.x < -0.5235988f)) {
                NDrone2_SetOpponent(drone, obj);
                return 0;
            }
        }
        obj = next;
    }

    // Drop an opponent it cannot see, or a dying or dead drone, or a copter at curState 4 or more
    if (drone->opponent != NULL) {
        bool lost = true;
        if (NDrone2_CanSeeObject(drone, drone->opponent, 5, 0)) {
            obj_tag *opponent = drone->opponent;
            char type = opponent->objectType;
            if (!(type == OBJECTTYPE_DRONE && MPDrone_MaybeIsDyingOrDead(opponent))
                && !(type == OBJECTTYPE_COPTER && (short)opponent->curState >= 4))
                lost = false;
        }
        if (lost) {
            drone->opponent = NULL;
            if (MPSettings.maybeDroneAIEnabled)
                MP_sendBotMessage(drone->gameObj, 0x45, 0, 0);
            Vec_Zero(&drone->maybeVectorToOpponent);
            drone->targetSlot = 0xff;
        }
    }

    obj_tag *opponent = drone->opponent;
    if (opponent != NULL)
        bestDist = drone->distanceToTarget * drone->distanceToTarget;
    Drone_tag *other = drone->opponentScanResume != NULL ? drone->opponentScanResume->next : NPCGlobals.NDrone2List;
    Drone_tag *opponentDrone = opponent != NULL ? (Drone_tag *)opponent->extraObjectData : NULL;

    for (; other != NULL && other != drone->opponentScanResume; other = other->next) {
        if (other == drone || other == opponentDrone)
            continue;
        obj_tag *obj = other->gameObj;
        if (obj != NULL) {
            Drone_tag *enemy = (Drone_tag *)obj->extraObjectData;
            if (!(enemy->flags & (DRONE_FLAG_DEAD | DRONE_FLAG_DYING)) && (enemy->flags & DRONE_FLAG_AI_RUNNING)
                && !(enemy->health <= 0.0f) && obj->objectType != OBJECTTYPE_DEAD_DRONE && !(obj->flags & 1)
                && other->side == 1
                && Vec_SqDist2D(&drone->gameObj->position, &obj->position) < bestDist
                && NDrone2_CanSeeObject(drone, other->gameObj, 5, 0)) {
                NDrone2_SetOpponent(drone, other->gameObj);
                drone->opponentScanResume = other;
                return (int)other->gameObj;
            }
        }
        if (sightTests < NPCGlobals.lineOfSightTests)
            break;
    }
    drone->opponentScanResume = other;
    return 0;
}

// Picks the drone's opponent (NDrone2_SetOpponent); from NDrone2_ControlSTANDARD and NDrone2_PostLoad_Init.
// Multiplayer bots and single-player allies have their own searches; any other drone targets the player (or
// NPCGlobals.blankSpawner when targetBlankSpawner is set), or nobody while the player is dead or player stats are
// not being updated. Returns what the bot or ally search returns, else 0.
// AUTOINJECT
int NDrone2_FindOpponent(Drone_tag *drone) {
    ushort sightTests = NPCGlobals.lineOfSightTests;

    if (MPSettings.maybeDroneAIEnabled)
        return NDrone2_FindBotOpponent(drone);
    if (drone->side == 2)
        return NDrone2_FindAllyOpponent(drone, sightTests);

    obj_tag *target = NPCGlobals.targetBlankSpawner ? NPCGlobals.blankSpawner : glb_players[0];
    if (!PlrStat_OkToUpdate()) {
        target = NULL;
    } else if (target != NULL) {
        if (target->objectType == OBJECTTYPE_PLAYER) {
            if (((BLData *)target->extraObjectData)->health <= 0.0f)
                target = NULL;
        } else if (target->objectType == OBJECTTYPE_DEAD_PLAYER) {
            target = NULL;
        }
    }
    NDrone2_SetOpponent(drone, target);
    return 0;
}
