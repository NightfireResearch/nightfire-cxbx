#include "NDrone2.h"
#include "../sp/SwitchChannels.h" // switch_channels, switch_channels_MusicVars
#include "../../sound/music.h"    // Music_Event (ours, sound/music.cpp)
#include "../../game.h"           // GameState
#include "../../util/Random.h"      // Rand_Rand (AUTOGEN'd in util/Random.cpp: still the original)
#include "../../engine/Collide.h"   // HITDATA_tag
#include "../obj/bullet.h"          // BU_tag

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
