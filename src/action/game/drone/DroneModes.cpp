// A drone's mode and type: the mode and type tables, the behaviour-driven settings and the per-mode and per-type
// initialisers (docs/drone/architecture/A2-mode-and-type-tables.md).
//
// A placement's mode (key 5) below 0x24 indexes DroneModeSettings (NDrone2_DoModeSettingsOLD), which gives the DTYPE,
// the side, the alertness and an initialiser that sets the behaviour properties; the type then gives the states
// (NDrone2_DoTypeSettingsOLD). Any other mode reads the placement's behaviour block (NDrone2_DoModeSettingsNEW), and
// the DTYPEs come from its class and attack modes (NDrone2_GetDroneTypeAttackTypeFriend).

#include "DroneModes.h"
#include "Behaviour.h"
#include "DroneTables.h"
#include "../../game.h"     // GameState

#include <string.h>

// ---- the mode initialisers (DroneModeSettings.init)

// What most modes start from. The modes whose initialiser is only this (Normal, Guard, Retreater, Attacker, Assassin
// and Interogator; one function each in the PS2 build) share a jump to it, NDrone2_init_DMODE_Normal (0x408a0).
// AUTOINJECT
void NDrone2_init_DMODE_Defaults(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_01, DRONE_PROP_06, DRONE_PROP_07, DRONE_PROP_0A, DRONE_PROP_0C, DRONE_PROP_48, DRONE_PROP_0F,
        DRONE_PROP_11, DRONE_PROP_13, DRONE_PROP_18, DRONE_PROP_1D, DRONE_PROP_4C, DRONE_PROP_21, DRONE_PROP_29,
        DRONE_PROP_2E, DRONE_PROP_37, DRONE_PROP_41, DRONE_PROP_42, DRONE_PROP_4F, DRONE_PROP_51, DRONE_PROP_5A,
        DRONE_PROP_24, DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_44, DRONE_PROP_33, DRONE_PROP_32, DRONE_PROP_31,
        DRONE_PROP_28, DRONE_PROP_27, DRONE_PROP_26, DRONE_PROP_30, DRONE_PROP_2F, DRONE_PROP_2A, DRONE_PROP_43,
        DRONE_PROP_3B, DRONE_PROP_0A, DRONE_PROP_0C, DRONE_PROP_17, DRONE_PROP_46,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// The shared jump (a thunk, so tagged by address)
// FUNC_AT(0x000408a0)
void NDrone2_init_DMODE_Normal(Drone_tag *drone) {
    NDrone2_init_DMODE_Defaults(drone);
}

// AUTOINJECT
void NDrone2_init_DMODE_Sniper(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_33, DRONE_PROP_32, DRONE_PROP_31,
        DRONE_PROP_43, DRONE_PROP_17, DRONE_PROP_39, DRONE_PROP_40, DRONE_PROP_5A,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_Stealth(Drone_tag *drone) {
    NDrone2_init_DMODE_Defaults(drone);
    behaviour_util_setProperty(DRONE_PROP_25, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_RunToPoint(Drone_tag *drone) {
    NDrone2_init_DMODE_Defaults(drone);
    behaviour_util_setProperty(DRONE_PROP_23, drone->currentBehaviour, 1);
    behaviour_util_setProperty(DRONE_PROP_58, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_HostageKiller(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_33, DRONE_PROP_32, DRONE_PROP_31, DRONE_PROP_29,
        DRONE_PROP_28, DRONE_PROP_27, DRONE_PROP_26, DRONE_PROP_43, DRONE_PROP_40, DRONE_PROP_22,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// Hostage and HostageTied share it
// AUTOINJECT
void NDrone2_init_DMODE_HostageTied(Drone_tag *drone) {
    static const DRONE_PROP props[] = {DRONE_PROP_24, DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_40, DRONE_PROP_4E};
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_JustStand4Demo(Drone_tag *drone) {
    behaviour_util_setProperty(DRONE_PROP_40, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_Civilian(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_1D, DRONE_PROP_24, DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_29, DRONE_PROP_46,
        DRONE_PROP_32, DRONE_PROP_31, DRONE_PROP_27, DRONE_PROP_26, DRONE_PROP_3B, DRONE_PROP_4C,
        DRONE_PROP_4F, DRONE_PROP_50, DRONE_PROP_51, DRONE_PROP_53,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_CivilianScared(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_32, DRONE_PROP_31, DRONE_PROP_27, DRONE_PROP_26,
        DRONE_PROP_3B, DRONE_PROP_4C, DRONE_PROP_4F, DRONE_PROP_50, DRONE_PROP_51, DRONE_PROP_53,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_MissionFailer(Drone_tag *drone) {
    behaviour_util_setProperty(DRONE_PROP_3C, drone->currentBehaviour, 1);
    behaviour_util_setProperty(DRONE_PROP_55, drone->currentBehaviour, 1);
}

// Mayhew and TruckDriver share it
// AUTOINJECT
void NDrone2_init_DMODE_Mayhew(Drone_tag *drone) {
    behaviour_util_setProperty(DRONE_PROP_1D, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_AlarmRaiser(Drone_tag *drone) {
    NDrone2_init_DMODE_Defaults(drone);
    behaviour_util_setProperty(DRONE_PROP_23, drone->currentBehaviour, 1);
    behaviour_util_setProperty(DRONE_PROP_56, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_SearchLight(Drone_tag *drone) {
    NDrone2_init_DMODE_Defaults(drone);
    behaviour_util_setProperty(DRONE_PROP_40, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_Ambush(Drone_tag *drone) {
    NDrone2_init_DMODE_Defaults(drone);
    behaviour_util_setProperty(DRONE_PROP_10, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_Zoe(Drone_tag *drone) {
    NDrone2_init_DMODE_Defaults(drone);
    behaviour_util_setProperty(DRONE_PROP_40, drone->currentBehaviour, 1);
    behaviour_util_setProperty(DRONE_PROP_21, drone->currentBehaviour, 0);
}

// AUTOINJECT
void NDrone2_init_DMODE_PartyGirl(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_1D, DRONE_PROP_24, DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_46,
        DRONE_PROP_26, DRONE_PROP_27, DRONE_PROP_28, DRONE_PROP_29, DRONE_PROP_51,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_CivilianGuard(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_1D, DRONE_PROP_24, DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_46, DRONE_PROP_26,
        DRONE_PROP_27, DRONE_PROP_28, DRONE_PROP_29, DRONE_PROP_4F, DRONE_PROP_50, DRONE_PROP_51,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_CivDoorGuard(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_1D, DRONE_PROP_18, DRONE_PROP_06, DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_46, DRONE_PROP_26,
        DRONE_PROP_27, DRONE_PROP_28, DRONE_PROP_29, DRONE_PROP_4F, DRONE_PROP_50, DRONE_PROP_51,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_CastleChatGuard1(Drone_tag *drone) {
    NDrone2_init_DMODE_Defaults(drone);
    drone->chatSwitchChannel = 0x19;
}

// AUTOINJECT
void NDrone2_init_DMODE_CastleChatGuard2(Drone_tag *drone) {
    NDrone2_init_DMODE_Defaults(drone);
    behaviour_util_setProperty(DRONE_PROP_1D, drone->currentBehaviour, 1);
    drone->chatSwitchChannel = 0x11;
}

// AUTOINJECT
void NDrone2_init_DMODE_SniperAlert(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_33, DRONE_PROP_32, DRONE_PROP_31,
        DRONE_PROP_43, DRONE_PROP_17, DRONE_PROP_39, DRONE_PROP_40,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
}

// AUTOINJECT
void NDrone2_init_DMODE_PartyGirlLooker(Drone_tag *drone) {
    static const DRONE_PROP props[] = {
        DRONE_PROP_1D, DRONE_PROP_24, DRONE_PROP_3C, DRONE_PROP_1F, DRONE_PROP_46, DRONE_PROP_47,
        DRONE_PROP_26, DRONE_PROP_27, DRONE_PROP_28, DRONE_PROP_29, DRONE_PROP_51,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
    drone->headMode = 2;
}

// ---- the type initialisers (DroneTypeSettings.init)

#define DRONE_FLAG_FROZEN 0x10

// AUTOINJECT
void NDrone2_initDTYPE_Sniper(Drone_tag *drone) {
    drone->flags |= DRONE_FLAG_FROZEN;
}

// The DCVars it fills are not used
// AUTOINJECT
void NDrone2_initDTYPE_SniperAlert(Drone_tag *drone) {
    DCVars_tag dcv;
    Drone_DCVfromOBJ(drone->gameObj, &dcv);
    drone->flags |= DRONE_FLAG_FROZEN;
}

// ---- modes and types

// The skin class with fixed extra properties, and the drone types the code singles out
#define SKINCLASS_ATTACKS_AT_ONCE 0xb   // alerted at the start: straight to its attack type, 100 health

// The DTYPE for a behaviour class and one of its attack modes (0 for the class's own). The original takes the drone
// in EDI and the mode in ESI; only NDrone2_GetDroneTypeAttackTypeFriend calls it, so it stays a helper of ours.
static uchar NDrone2_GetDTYPENEW(Drone_tag *drone, uint behaviourClass, uint mode) {
    if (mode == 0 && behaviour_util_getProperty(DRONE_PROP_10, drone->currentBehaviour) != 0)
        return DTYPE_Ambush;

    switch (behaviourClass) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
        if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1 && mode == 0)
            return behaviour_util_getProperty(DRONE_PROP_4B, drone->currentBehaviour) != 0 ? DTYPE_CivDoorGuard
                                                                                          : DTYPE_CivilianGuard;
        break;
    case 10:
    case 11:
        if (mode != 0)
            break;
        if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1) {
            behaviour_util_setProperty(DRONE_PROP_1F, drone->currentBehaviour, 0);
            behaviour_util_setProperty(DRONE_PROP_54, drone->currentBehaviour, 1);
        }
        if (drone->alertness >= 1.0f)
            return DTYPE_CivilianScared;
        return drone->unknown15 != 0 ? DTYPE_PartyGirl : DTYPE_Civilian;
    case 12:
        if (mode != 0)
            break;
        return drone->skinClass == 0xc ? DTYPE_Zoe : DTYPE_Mayhew;
    case 13:
        if (mode != 0)
            break;
        if (drone->skinClass == 0xf)
            return DTYPE_TruckDriver;
        if (GameState.CurrentLevelHashcode == HT_Level_CastleIndoors1)
            behaviour_util_setProperty(DRONE_PROP_1F, drone->currentBehaviour, 0);
        return drone->alertness >= 1.0f ? DTYPE_CivilianScared : DTYPE_Civilian;
    case 14:
        if (mode != 0)
            break;
        if (behaviour_util_getProperty(DRONE_PROP_4E, drone->currentBehaviour) != 0)
            return DTYPE_Hostage;
        return drone->alertness >= 1.0f ? DTYPE_CivilianScared : DTYPE_Civilian;
    case 15:
        if (mode != 0)
            break;
        return DTYPE_Ninja;
    case 16:
        return DTYPE_Bot;
    }

    switch (mode) {
    case 2:
        return drone->alertness >= 1.0f ? DTYPE_SniperAlert : DTYPE_Sniper;
    case 3:
        return behaviour_util_getProperty(DRONE_PROP_56, drone->currentBehaviour) != 0 ? DTYPE_AlarmRaiser
                                                                                      : DTYPE_RunToPoint;
    case 4:
        return DTYPE_HostageKiller;
    case 5:
        return DTYPE_DeleteMe;
    default:
        return DTYPE_Normal;
    }
}

// The state the drone goes to when it changes to its attack type, for a drone with no second type: the attack for
// those that start standing, idle, patrolling or guarding, and so on; 0 for the rest. The original takes the drone in
// EAX; only NDrone2_GetDroneTypeAttackTypeFriend calls it. Our name.
static short NDrone2_SecondStateFor(Drone_tag *drone) {
    switch (drone->initialState) {
    case DSTATE_Idle:
    case DSTATE_Alert:
    case DSTATE_InitPatrol:
    case DSTATE_HostageKiller:
    case DSTATE_CivilianGuard:
    case DSTATE_CivilianDoorGuard:
    case DSTATE_CastleChatGuard1:
    case DSTATE_AmbushInit:
    case DSTATE_EnemyRunToPoint:
    case DSTATE_RunToAlarm:
        return DSTATE_Attack;
    case DSTATE_CivilianInit:
    case DSTATE_PartyGirlInit:
    case DSTATE_TruckDriverInit:
        return DSTATE_CivilianScared;
    case DSTATE_SniperIdle:
        return DSTATE_SniperAim;
    case DSTATE_NinjaStand:
        return DSTATE_NinjaAttack;
    default:
        return 0;
    }
}

// The second state of the drone's types: its second type's, or failing that its own; none without a second type
static void NDrone2_SetSecondState(Drone_tag *drone) {
    if (drone->dtype2 == 0) {
        drone->secondState = 0;
        return;
    }
    drone->secondState = DroneTypeSettings[drone->dtype2].secondState;
    if (drone->secondState == 0)
        drone->secondState = DroneTypeSettings[drone->dtype].secondState;
}

// The DTYPEs a behaviour block gives (its class's own, and the class with each attack mode), the side, and the
// states they start in. Alerted at the start, the drone starts in the alerted form of its state; a drone of skin
// class 0xb changes to its attack type at once. Called by NDrone2_DoModeSettingsNEW.
// AUTOINJECT
void NDrone2_GetDroneTypeAttackTypeFriend(Drone_tag *drone) {
    if (drone->behaviour1Mode == drone->behaviour2Mode || drone->secondaryMode == 0x65)
        drone->secondaryMode = 0;

    drone->dtypeBase = NDrone2_GetDTYPENEW(drone, drone->behaviourClass, 0);
    drone->dtype = NDrone2_GetDTYPENEW(drone, drone->behaviourClass, drone->behaviour1Mode);
    if (drone->secondaryMode == 0 || drone->secondaryMode == 0x65 ||
        (drone->modeChangeSwitchChannel == 0 && drone->baseAlertness1 >= 1.0f))
        drone->dtype2 = 0;
    else
        drone->dtype2 = NDrone2_GetDTYPENEW(drone, drone->behaviourClass, drone->behaviour2Mode);

    switch (drone->behaviourClass) {
    case 10:
    case 11:
    case 13:
    case 14:
        drone->side = 3;
        break;
    case 12:
        drone->side = 2;
        break;
    default:
        drone->side = 1;
        break;
    }
    if (drone->gameObj != NULL)
        drone->gameObj->curState = 0;

    short initial = DroneTypeSettings[drone->dtype].initialState;
    if (initial >= 0) {
        drone->initialState = initial;
    } else {
        drone->initialState = DSTATE_Idle;
        switch (drone->dtype) {
        case DTYPE_Sniper:
            // The idle state is overwritten at once
            if (drone->alertness < 1.0f)
                drone->initialState = DSTATE_SniperIdle;
            drone->initialState = DSTATE_SniperAim;
            break;
        case DTYPE_HostageKiller:
            drone->initialState = DSTATE_HostageKiller;
            break;
        case DTYPE_RunToPoint:
            drone->initialState = DSTATE_EnemyRunToPoint;
            break;
        case DTYPE_Ambush:
            drone->initialState = DSTATE_AmbushInit;
            break;
        case DTYPE_CivilianGuard:
            drone->initialState = DSTATE_CivilianGuard;
            break;
        case DTYPE_CivDoorGuard:
            drone->initialState = DSTATE_CivilianDoorGuard;
            break;
        case DTYPE_CastleChatGuard:
            drone->initialState = DSTATE_CastleChatGuard1;
            break;
        default:
            if (behaviour_util_getProperty(DRONE_PROP_24, drone->currentBehaviour) != 0 ||
                behaviour_util_getProperty(DRONE_PROP_25, drone->currentBehaviour) != 0)
                drone->initialState = DSTATE_InitPatrol;
            break;
        }
    }

    NDrone2_SetSecondState(drone);
    if (DroneTypeSettings[drone->dtype].init != NULL)
        DroneTypeSettings[drone->dtype].init(drone);

    if (drone->alertness >= 1.0f) {
        if (drone->skinClass == SKINCLASS_ATTACKS_AT_ONCE) {
            NDrone2_ChangeToAttackMode(drone);
            drone->health = 100.0f;
            return;
        }
        switch (drone->initialState) {
        case DSTATE_Idle:
        case DSTATE_Alert:
        case DSTATE_InitPatrol:
        case DSTATE_CivilianGuard:
        case DSTATE_CivilianDoorGuard:
        case DSTATE_CastleChatGuard1:
        case DSTATE_Attack:
            drone->initialState = DSTATE_Attack;
            break;
        case DSTATE_SniperIdle:
        case DSTATE_SniperAim:
            drone->initialState = DSTATE_SniperAim;
            break;
        case DSTATE_CivilianInit:
        case DSTATE_CivilianScared:
        case DSTATE_PartyGirlInit:
        case DSTATE_TruckDriverInit:
        case DSTATE_FailMission:
            drone->initialState = DSTATE_CivilianScared;
            break;
        case DSTATE_NinjaStand:
        case DSTATE_NinjaAttack:
            drone->initialState = DSTATE_NinjaAttack;
            break;
        }
    }

    if (drone->dtype2 == 0 && drone->secondState == 0 &&
        (drone->modeChangeSwitchChannel != 0 || drone->baseAlertness2 >= 1.0f))
        drone->secondState = NDrone2_SecondStateFor(drone);
    if (drone->secondState != 0 && drone->secondaryMode == 0)
        drone->secondaryMode = 100;
}

// The initial state of a type the table leaves to be worked out (-1)
// AUTOINJECT
short NDrone2_DefaultInitialState(Drone_tag *drone) {
    switch (drone->dtype) {
    case DTYPE_Sniper:
        return drone->alertness < 1.0f ? DSTATE_SniperIdle : DSTATE_SniperAim;
    case DTYPE_HostageKiller:
        return DSTATE_HostageKiller;
    case DTYPE_RunToPoint:
        return DSTATE_EnemyRunToPoint;
    default:
        if (behaviour_util_getProperty(DRONE_PROP_24, drone->currentBehaviour) != 0 ||
            behaviour_util_getProperty(DRONE_PROP_25, drone->currentBehaviour) != 0)
            return DSTATE_InitPatrol;
        return DSTATE_Idle;
    }
}

// The states of the type a mode of the old table gives. Alerted at the start, a drone (other than a hostage) starts
// in its second state, or in the attack unless its state is one of the alerted kind already.
// AUTOINJECT
void NDrone2_DoTypeSettingsOLD(Drone_tag *drone, uchar dtype) {
    drone->dtype = dtype;
    short initial = DroneTypeSettings[dtype].initialState;
    if (initial < 0)
        initial = NDrone2_DefaultInitialState(drone);
    drone->initialState = initial;

    NDrone2_SetSecondState(drone);
    if (DroneTypeSettings[drone->dtype].init != NULL)
        DroneTypeSettings[drone->dtype].init(drone);

    if (drone->alertness >= 1.0f) {
        if (drone->skinClass == SKINCLASS_ATTACKS_AT_ONCE) {
            NDrone2_ChangeToAttackMode(drone);
            drone->health = 100.0f;
            return;
        }
        if (drone->dtype == DTYPE_Hostage)
            return;
        if (drone->secondState != 0) {
            drone->initialState = drone->secondState;
            return;
        }
        switch (drone->initialState) {
        case DSTATE_HostageKiller:
        case DSTATE_Hostage:
        case DSTATE_AllyLeadInit:
        case DSTATE_SniperAim:
        case DSTATE_EnemyRunToPoint:
        case DSTATE_RunToAlarm:
        case DSTATE_DeleteMe:
        case DSTATE_JustStand:
        case DSTATE_Tester1:
            break;
        default:
            drone->initialState = DSTATE_Attack;
            break;
        }
    }
}

// The switch to the attack type and its state, which it returns (0: the drone has none). A sniper becomes the alerted
// sniper type and attacks. Mode 100 also changes to the placement's second behaviour.
// AUTOINJECT
short NDrone2_ChangeToAttackMode(Drone_tag *drone) {
    short state = drone->secondState;
    if (state == 0)
        return state;
    if (drone->dtype2 != 0)
        drone->dtype = drone->dtype2;
    if (drone->dtype == DTYPE_Sniper) {
        drone->dtype = DTYPE_SniperAlert;
        if (DroneTypeSettings[DTYPE_SniperAlert].init != NULL)
            DroneTypeSettings[DTYPE_SniperAlert].init(drone);
        state = DSTATE_Attack;
    }
    drone->dtype2 = 0;
    drone->secondState = 0;
    if (drone->diVars.keys.mode == 100) {
        drone->currentBehaviour = drone->behaviour2;
        drone->usingSecondBehaviour = 1;
    }
    drone->attackModeTaken = 1;
    return state;
}

// The settings of a mode below 0x24 (DroneModeSettings): the DTYPE, the second DTYPE of the secondary mode, the side
// and the alertness, the mode's own properties and those every such drone has. Any other mode has a behaviour block
// (NDrone2_DoModeSettingsNEW). The secondary mode indexes the table unchecked. Modes 9, 10 and 0x13 take anim sets 1
// and 0xc, and empty hands (the anim state is not checked).
// AUTOINJECT
void NDrone2_DoModeSettingsOLD(Drone_tag *drone, uint mode, uint *behaviour, DIVars_tag *diVars) {
    if (mode > 0x23) {
        NDrone2_DoModeSettingsNEW(drone, mode, behaviour, diVars);
        return;
    }

    const DroneModeSetting *setting = &DroneModeSettings[mode];
    drone->modeFlag31 = 1;
    drone->modeFromOldTable = 1;
    drone->baseAlertness1 = setting->alertness;
    drone->alertness = setting->alertness;
    memset(drone->behaviour1, 0, sizeof(drone->behaviour1));
    memset(drone->behaviour2, 0, sizeof(drone->behaviour2));
    drone->currentBehaviour = drone->behaviour1;
    drone->dtype = setting->dtype;
    if (drone->secondaryMode != 0 && drone->secondaryMode != 0x65 && drone->modeChangeSwitchChannel == 0)
        drone->dtype2 = DroneModeSettings[(ushort)drone->secondaryMode].dtype;
    else
        drone->dtype2 = 0;
    drone->side = setting->side;
    if (setting->init != NULL)
        setting->init(drone);

    switch (drone->skinClass) {
    case 13:
        behaviour_util_setProperty(DRONE_PROP_4D, drone->currentBehaviour, 1);
        behaviour_util_setProperty(DRONE_PROP_4C, drone->currentBehaviour, 1);
        behaviour_util_setProperty(DRONE_PROP_52, drone->currentBehaviour, 1);
        break;
    case 16:
        behaviour_util_setProperty(DRONE_PROP_51, drone->currentBehaviour, 1);
        break;
    case 17:
        behaviour_util_setProperty(DRONE_PROP_4C, drone->currentBehaviour, 1);
        break;
    case 19:
        behaviour_util_setProperty(DRONE_PROP_4C, drone->currentBehaviour, 1);
        behaviour_util_setProperty(DRONE_PROP_52, drone->currentBehaviour, 1);
        behaviour_util_setProperty(DRONE_PROP_55, drone->currentBehaviour, 1);
        break;
    }

    static const DRONE_PROP props[] = {
        DRONE_PROP_18, DRONE_PROP_26, DRONE_PROP_27, DRONE_PROP_28, DRONE_PROP_29, DRONE_PROP_31,
        DRONE_PROP_32, DRONE_PROP_33, DRONE_PROP_4F, DRONE_PROP_1F, DRONE_PROP_3C, DRONE_PROP_30,
        DRONE_PROP_0A, DRONE_PROP_0C, DRONE_PROP_37, DRONE_PROP_38,
    };
    for (DRONE_PROP prop : props)
        behaviour_util_setProperty(prop, drone->currentBehaviour, 1);
    if (drone->dtype == DTYPE_Zoe) {
        behaviour_util_setProperty(DRONE_PROP_40, drone->currentBehaviour, 1);
        behaviour_util_setProperty(DRONE_PROP_53, drone->currentBehaviour, 1);
        behaviour_util_setProperty(DRONE_PROP_54, drone->currentBehaviour, 1);
    }

    if (mode == DMODE_Hostage || mode == DMODE_HostageTied) {
        drone->animSet = 1;
        drone->gameObj->animState->currentWeaponId = 0;
    } else if (mode == DMODE_SearchLight) {
        drone->animSet = 0xc;
        drone->gameObj->animState->currentWeaponId = 0;
    }
}

// The settings of a placement's behaviour block (behaviour_util_get, docs/drone/behaviours): its two behaviours'
// properties and alertness, the class and attack modes and the DTYPEs they give, the class's stats (10 health and
// accuracy 5 without), and fixes by level and play script. The mode is not read.
// AUTOINJECT
void NDrone2_DoModeSettingsNEW(Drone_tag *drone, uint mode, uint *behaviour, DIVars_tag *diVars) {
    // Behaviour property 0x20's alertness levels
    static const float alertLevels[4] = {0.0f, 0.0f, 0.66f, 1.0f};

    NF_WARN_IF(behaviour == NULL, "NULL behaviours pointer!");  // GC check (0x80099554)
    NF_WARN_IF(behaviour != NULL && *behaviour == 0,
               "No behaviour data stored but placement param claims there is!");  // GC check (0x80099554)
    drone->modeFlag31 = 1;
    drone->modeFromOldTable = 0;
    NF_WARN_IF(behaviour == NULL, "Drone has no behaviour data!");  // GC check (0x80099554)

    BehaviourStruct bs;
    memset(&bs, 0, sizeof(bs));
    bs.count1 = 3;
    bs.words1 = drone->behaviour1;
    bs.count2 = 3;
    bs.words2 = drone->behaviour2;
    behaviour_util_get(&bs, behaviour);
    drone->currentBehaviour = drone->behaviour1;
    float alertness = alertLevels[behaviour_util_getProperty(DRONE_PROP_20, drone->behaviour1)];
    drone->baseAlertness1 = alertness;
    drone->alertness = alertness;
    drone->baseAlertness2 = alertLevels[behaviour_util_getProperty(DRONE_PROP_20, drone->behaviour2)];
    drone->behaviourClass = bs.behaviourClass;
    drone->behaviour1Mode = bs.mode1;
    drone->behaviour2Mode = bs.mode2;
    NDrone2_GetDroneTypeAttackTypeFriend(drone);

    const DroneStatsRecord *stats = behaviour_util_getStats(drone->behaviourClass, 1);
    if (stats->field[DRONE_STAT_HEALTH] != 0) {
        drone->health = (float)stats->field[DRONE_STAT_HEALTH];
        drone->accuracy = stats->field[DRONE_STAT_ACCURACY];
        drone->aggression = stats->field[DRONE_STAT_AGGRESSION];
        drone->speed = stats->field[DRONE_STAT_SPEED];
        drone->stat9b = stats->field[DRONE_STAT_9B];
        drone->reaction = stats->field[DRONE_STAT_REACTION];
        drone->recover = stats->field[DRONE_STAT_RECOVER];
        drone->badSide = stats->field[DRONE_STAT_BADSIDE];
    } else {
        drone->health = 10.0f;
        drone->accuracy = 5;
        drone->aggression = 0;
        drone->speed = 0;
        drone->stat9b = 0;
        drone->reaction = 0;
        drone->recover = 0;
        drone->badSide = 0;
    }
    drone->accuracyF = drone->accuracy;
    if (drone->skinClass == 16)
        drone->health = 100.0f;

    HASHCODE level = GameState.CurrentLevelHashcode;
    if (level != HT_Level_CastleIndoors1) {
        if (level == HT_Level_TowerA || level == HT_Level_TowerB) {
            behaviour_util_setProperty(DRONE_PROP_1A, drone->currentBehaviour, 1);
            behaviour_util_setProperty(DRONE_PROP_56, drone->currentBehaviour, 1);
        } else {
            behaviour_util_setProperty(DRONE_PROP_1A, drone->currentBehaviour, 0);
        }
    }

    if (diVars == NULL)
        return;
    switch (diVars->keys.playScript) {
    case 0x600011d:
        behaviour_util_setProperty(DRONE_PROP_1D, drone->currentBehaviour, 1);
        drone->dtype = DTYPE_TruckDriver;
        break;
    case 0x600011f:
        drone->chatSwitchChannel = 0x19;
        drone->dtype = DTYPE_CastleChatGuard;
        break;
    case 0x6000124:
    case 0x6000126:
        drone->chatSwitchChannel = 0x11;
        drone->dtype = DTYPE_CastleChatGuard;
        break;
    case 0x600021f:
        behaviour_util_setProperty(DRONE_PROP_38, drone->currentBehaviour, 0);
        drone->flags |= DRONE_FLAG_FROZEN;
        break;
    }
}
