// A drone's creation and initialisation: the object, NDrone2_DefaultInit, its collision, groups and enabling.
//
// Drone_Create keeps a placement's keys in a DIVars_tag; NDrone2_CreateFromDIVars makes the object
// (NDrone2_CreateObj) and copies them into the drone's own diVars. After the load, NDrone2_PostLoad_Init sets the
// drone up from them (NDrone2_DefaultInit, then the mode and type settings), and NDrone2_SetupGroups links the
// groups and hands them to their spawners.

#include "DroneInit.h"
#include "NDrone2.h"
#include "NDrone2_Opponent.h"           // NDrone2_FindOpponent
#include "Behaviour.h"                  // behaviour_util_getProperty, behaviour_util_setProperty
#include "BOT.h"                        // BOT_vars_t
#include "DroneTables.h"                // DRONE_PROP_*
#include "DroneTuning.h"                // DroneCaptain_Mod_*
#include "../../game.h"                 // GameState
#include "../../memory.h"               // Mem_Malloc
#include "../../engine/AINetwork.h"     // AIPath_MaxNodes, AINetwork_ResetRoute, AINetwork_NavPathForPosition
#include "../../engine/Anim.h"          // AnimState, ANIM_CONTACT_FLOOR
#include "../../engine/Collide.h"       // Collide_RayIntersect, Collide_FreeHitList
#include "../../math/math.h"            // Vec_*
#include "../../util/LList.h"           // LList_Add
#include "../../util/Random.h"          // Float_FRand
#include "../mp/multiplayer.h"          // MPSettings
#include "../obj/build.h"               // build_LinkToRoom
#include "../obj/control.h"             // control_create_object, Control_CreateObjEx, control_init_object
#include "../obj/DroneSpawner.h"        // DroneSpawner_Init
#include "../sp/PlayerStats.h"          // PlrStat_LogEnemySpawned
#include "../sp/SwitchChannels.h"       // switch_channels
#include "../weapon_stats.h"            // weapon_data
#include "DroneModes.h"
#include "DroneAnim.h"

#include <bit>
#include <math.h>
#include <stdint.h>
#include <string.h>

#pragma fp_contract(off)

#include "../obj/BodyGlow.h"
#include "BOTWEAP.h"

// XBE_GLOBAL(0x00164068, 0x4)
float DroneDamage_Easy = 2.0f;
// XBE_GLOBAL(0x0016406c, 0x4)
float DroneDamage_Normal = 1.5f;
// XBE_GLOBAL(0x00164070, 0x4)
float DroneDamage_Hard = 0.5f;
// XBE_GLOBAL(0x00164074, 0x4)
float DroneDamage_Head = 100.0f;
// XBE_GLOBAL(0x00164078, 0x4)
float DroneDamage_Legs = 0.75f;
// XBE_GLOBAL(0x00164080, 0x4)
float DroneDamage_Torso = 1.0f;
// XBE_GLOBAL(0x00164084, 0x4)
float DroneArmour_Helmet = 0.5f;
// XBE_GLOBAL(0x00164090, 0x4)
float DroneArmour_Vest = 0.75f;
// XBE_GLOBAL(0x00164094, 0x4)
float DroneFiring_BurstDelay_Min = 15.0f;
// XBE_GLOBAL(0x00164098, 0x4)
float DroneFiring_BurstDelay_Normal = 45.0f;
// XBE_GLOBAL(0x0016409c, 0x4)
float DroneFiring_BurstDelay_Max = 60.0f;
// XBE_GLOBAL(0x001640a0, 0x4)
float DroneFiring_BurstDelay_MinDist = 4.0f;
// XBE_GLOBAL(0x001640a4, 0x4)
float DroneFiring_BurstDelay_MaxDist = 30.0f;
// XBE_GLOBAL(0x001640a8, 0x4)
float DroneFiring_Accuracy_Easy = 0.5f;
// XBE_GLOBAL(0x001640ac, 0x4)
float DroneFiring_Accuracy_Normal = 1.0f;
// XBE_GLOBAL(0x001640b0, 0x4)
float DroneFiring_Accuracy_Hard = 2.0f;
// XBE_GLOBAL(0x001640b4, 0x4)
float DroneFiring_NewSighting_TimeToHit = 2.0f;
// XBE_GLOBAL(0x001640b8, 0x4)
float DroneFiring_TargetFirstMoved_TimeToHit = 2.0f;
// XBE_GLOBAL(0x001640bc, 0x4)
float DroneFiring_TargetFirstMoved_Accuracy = 0.25f;
// XBE_GLOBAL(0x001640c0, 0x4)
float DroneFiring_TargetMoving_Accuracy = 0.75f;
// XBE_GLOBAL(0x001640c4, 0x4)
float DroneFiring_TargetFirstStopped_TimeToHit = 2.0f;
// XBE_GLOBAL(0x001640c8, 0x4)
float DroneFiring_TargetFirstStopped_Accuracy = 1.0f;
// XBE_GLOBAL(0x001640cc, 0x4)
float DroneFiring_TooClose_Distance = 4.0f;
// XBE_GLOBAL(0x001640d0, 0x4)
float DroneFiring_TooClose_Accuracy = 2.0f;
// XBE_GLOBAL(0x001640d4, 0x4)
float DroneFiring_TooClose_Damage = 2.0f;
// XBE_GLOBAL(0x001640d8, 0x4)
float DroneFiring_PlayerBackShot_Damage = 2.0f;
// XBE_GLOBAL(0x001640dc, 0x4)
float DroneCaptain_Mod_BulletDamage = 2.0f;
// XBE_GLOBAL(0x001640e0, 0x4)
float DroneCaptain_Mod_BulletAccuracy = 2.0f;
// XBE_GLOBAL(0x001640e4, 0x4)
float DroneCaptain_Mod_Health = 2.0f;
// XBE_GLOBAL(0x0016407c, 0x4)
float DroneDamage_Arms = 1.0f;
// XBE_GLOBAL(0x00164088, 0x4)
float DroneArmour_Combat = 0.25f;
// XBE_GLOBAL(0x0016408c, 0x4)
float DroneArmour_Jacket = 0.5f;

// Drone_tag.flags
#define DRONE_FLAG_FROZEN       0x10
#define DRONE_FLAG_COLLIDES     0x20
#define DRONE_FLAG_FALLS        0x80
#define DRONE_FLAG_AI_RUNNING   0x100
#define DRONE_FLAG_DEAD         0x200
#define DRONE_FLAG_ANIMATE      0x80000
#define DRONE_FLAG_ANIM_UPDATED 0x100000
#define DRONE_FLAG_2000000      0x2000000
#define DRONE_FLAG_ATTACK_MODE  0x4000000
#define DRONE_FLAG_ATTACK_ALARM 0x40000000
#define DRONE_FLAG_BOUNDS       0x80000000  // the AI network's bounds push the drone back
#define DRONE_FLAGS_ACTIVE (DRONE_FLAG_COLLIDES | DRONE_FLAG_FALLS | DRONE_FLAG_AI_RUNNING | DRONE_FLAG_ANIMATE)

// Drone_tag.flags2
#define DRONE_FLAG2_10 0x10

// obj_tag.effectFlags
#define OBJ_EFFECT_20 0x20
#define OBJ_EFFECT_400 0x400

// The heap types of the two route node lists (our names)
#define MALLOC_ROUTE1_NODES_ALIGN8 ((MallocFlags)0x1508)
#define MALLOC_ROUTE2_NODES_ALIGN8 ((MallocFlags)0x4d08)

// The anim states a drone starts in (DroneAnim_SetAnimD)
#define DANIM_START 0x11a
#define DANIM_START_SKINCLASS_0B 0x1a5
#define DANIM_START_SKINCLASS_18 0x82
#define DANIM_START_DTYPE_2 0x7d
#define DANIM_START_DTYPE_19 0x32

constexpr float kHalfPi = 1.5707964f;
constexpr float kQuarterPi = 0.7853982f;
constexpr float kFloatAcceleration = 0.075f;
static_assert(std::bit_cast<uint32_t>(kHalfPi) == 0x3fc90fdb, "not the original's pi/2");
static_assert(std::bit_cast<uint32_t>(kQuarterPi) == 0x3f490fdb, "not the original's pi/4");
static_assert(std::bit_cast<uint32_t>(kFloatAcceleration) == 0x3d99999a, "not the original's 0.075");

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

// The object: a plain one, or with diVars->glist one made with that model (the only placements with a glist are
// MINISUB's)
// AUTOINJECT
obj_tag* NDrone2_CreateObj(DIVars_tag *diVars) {
    obj_tag *gameObj;
    if (diVars->glist == NULL) {
        gameObj = control_create_object(sizeof(Drone_tag), NULL, NULL, NULL);
        if (gameObj == NULL)
            return NULL;
        gameObj->objectType = OBJECTTYPE_DRONE;
    } else {
        gameObj = Control_CreateObjEx(sizeof(Drone_tag), &diVars->position, &diVars->rotation, NULL, diVars->glist,
                                      NULL, 1, 0x44, 1.0f, 0, 0xff, 0xff, 0xff);
        if (gameObj == NULL)
            return NULL;
        gameObj->objectType = OBJECTTYPE_MINISUB;
    }

    Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
    if (drone == NULL)
        return NULL;
    drone->gameObj = gameObj;
    LList_Add(&NDrone2Info, (LLNODE_tag *)drone);
    return gameObj;
}

// A drone from its creation vars: the object, a random creation time, and the drone's own copy of the vars (all but
// the glist), with its group and key 7. NULL when no object could be made.
// AUTOINJECT
obj_tag* NDrone2_CreateFromDIVars(DIVars_tag *diVars) {
    if (diVars == NULL)
        return NULL;
    obj_tag *gameObj = NDrone2_CreateObj(diVars);
    diVars->gameObj = gameObj;
    if (gameObj == NULL)
        return NULL;
    Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
    gameObj->creationTimeFrames = Rand_Rand(10000);
    drone->diVars.gameObj = gameObj;
    Vec_Copy(&diVars->position, &drone->diVars.position);
    Vec_Copy(&diVars->rotation, &drone->diVars.rotation);
    drone->diVars.keys = diVars->keys;
    drone->group = diVars->keys.group;
    drone->key7 = diVars->keys.key7;
    return gameObj;
}

// After the load: sets the drone up from its own creation vars, gives it an opponent, a path and its state machine
// AUTOINJECT
void NDrone2_PostLoad_Init(obj_tag *gameObj) {
    Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
    drone->initialisedOnFrame = GameState.NumFramesUnpaused;
    NDrone2_DefaultInit(&drone->diVars);
    NDrone2_FindOpponent(drone);
    Drone_GetOpponentInfo(drone, gameObj);
    drone->unknown2d = 0;
    drone->headTarget = drone->opponent;
    drone->route1.path = AINetwork_NavPathForPosition(&gameObj->position, gameObj->inCel, 0);
    Drone_SM_InitObject(gameObj);
    if (MPSettings.dronesAreBots)
        BOTWEAP_changeWeapon(drone, drone->botVars->weapon);
}

// Links every drone with a group into it (NPCGlobals.groups, newest first), then lets each group's spawner take its
// drones. Neither checks the group against DRONE_GROUPS.
// AUTOINJECT
bool NDrone2_SetupGroups(void) {
    for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL; drone = drone->next) {
        int group = drone->group;
        if (group == 0)
            continue;
        // NDrone2_AddToGroup on the PS2; inline here
        drone->groupNext = NPCGlobals.groups[group];
        NPCGlobals.groups[group] = drone;
        NPCGlobals.groupCounts[group]++;
        drone->groupAfterSetup = group;
    }

    for (int group = 0; group < DRONE_GROUPS; group++) {
        if (NPCGlobals.spawners[group] != NULL && NPCGlobals.groupCounts[group] != 0)
            DroneSpawner_Init(NPCGlobals.spawners[group]);
    }
    return true;
}

// Switches a drone off (its script stops, the object hides) or on again, unless it is dead
// AUTOINJECT
void NDrone2_Enable(char enable, Drone_tag *drone) {
    if (enable) {
        if (drone->flags & DRONE_FLAG_DEAD)
            return;
        if (drone->animScript != NULL)
            drone->animScript->speed = 1.0f;
        drone->gameObj->effectFlags &= ~FLAG_HIDDEN;
        drone->gameObj->effectFlags &= ~OBJ_EFFECT_20;
        drone->flags |= DRONE_FLAGS_ACTIVE;
    } else {
        if (drone->animScript != NULL)
            drone->animScript->speed = 0.0f;
        drone->gameObj->effectFlags |= FLAG_HIDDEN;
        drone->gameObj->effectFlags |= OBJ_EFFECT_20;
        drone->flags &= ~(DRONE_FLAGS_ACTIVE | DRONE_FLAG_ANIM_UPDATED);
    }
}

// Puts the drone on the floor: 1 m above what a ray from 1 m above to 25 m below finds. Not for bots, drones with a
// death script, fliers, skin class 0x13 or DTYPE_TruckDriver.
// AUTOINJECT
void DroneInit_Collision(Drone_tag *drone) {
    obj_tag *gameObj = drone->gameObj;
    cel_tag *cel = gameObj->inCel;
    if (MPSettings.dronesAreBots || drone->deathScript != 0 || drone->flies || drone->skinClass == 0x13 ||
        drone->dtype == DTYPE_TruckDriver)
        return;

    drone->flags |= DRONE_FLAG_BOUNDS;
    _VECTOR from = gameObj->position;
    from.y += 1.0f;
    _VECTOR to = gameObj->position;
    to.y -= 25.0f;
    HITDATA_tag *hits;
    if (Collide_RayIntersect(&from, &to, cel, NULL, NULL, &hits, 0, 0x104, 0)) {
        Vec_Copy(&hits->hitPosition, &to);
        gameObj->animState->animFlags |= ANIM_CONTACT_FLOOR;
        gameObj->position.y = to.y + 1.0f;
        Collide_FreeHitList(&hits);
    }
}

// Starts the drone attacking, at full alertness. Outside TowerA and TowerB it sees as far as its longest combat
// range, all round.
// AUTOINJECT
void DroneFunc_SetAsAttacking(Drone_tag *drone) {
    drone->flags |= DRONE_FLAG_2000000;
    drone->secondaryMode = 0;
    drone->modeChangeSwitchChannel = 0;
    if (drone->alertedFrame == 0)
        drone->alertedFrame = GameState.NumFramesUnpaused;
    drone->flags |= DRONE_FLAG_ATTACK_MODE;
    drone->alertness = 1.0f;
    if (GameState.CurrentLevelHashcode >= HT_Level_TowerA && GameState.CurrentLevelHashcode <= HT_Level_TowerB)
        return;
    if (drone->viewRange < drone->combatRanges[4])
        drone->viewRange = drone->combatRanges[4];
    drone->viewAngle = 0.0f;
}

// Sets the drone up from its placement's keys: the object, the skin's class and anim set, the weapon, grenades, ranges,
// switch channels, the mode (and with it the type and behaviour), captains, the routes, the play script and the first
// anim. A drone outside every cel is marked for deletion and NULL returned; otherwise the object.
// AUTOINJECT
obj_tag* NDrone2_DefaultInit(DIVars_tag *diVars) {
    obj_tag *gameObj = diVars->gameObj;
    Drone_tag *drone = (Drone_tag *)gameObj->extraObjectData;
    DroneKeys *keys = &diVars->keys;

    control_init_object(gameObj);
    Vec_Copy2(&diVars->position, &gameObj->position, &gameObj->lastPosition);
    Vec_Copy2(&diVars->rotation, &gameObj->rotation, &gameObj->lastRotation);
    gameObj->effectFlags &= ~OBJ_EFFECT_20;
    gameObj->hitTest = &drone->collision;
    // Here the original calls an empty function with the object (nightfire-port's control_init_collision)
    Vec_Zero(&drone->velocity);
    build_LinkToRoom(gameObj, 0, glb_world);

    if (AIPath_MaxNodes != 0) {
        drone->route1.nodes = (ushort *)Mem_Malloc(AIPath_MaxNodes * sizeof(ushort), MALLOC_ROUTE1_NODES_ALIGN8, 0);
        drone->route2.nodes = (ushort *)Mem_Malloc(AIPath_MaxNodes * sizeof(ushort), MALLOC_ROUTE2_NODES_ALIGN8, 0);
        memset(drone->route1.nodes, 0, AIPath_MaxNodes * sizeof(ushort));
        memset(drone->route2.nodes, 0, AIPath_MaxNodes * sizeof(ushort));
    }

    drone->hitDamageScale = 1.0f;
    drone->damageScale = 1.0f;
    drone->prevRootHeight = 1.0f;
    drone->rootHeight = 1.0f;
    drone->unknown854 = 0.5f;
    drone->vulnerable = 1;
    drone->impactImmune = 0;
    drone->health = 10.0f;
    drone->ninjaEyes = 0;
    drone->rootHeightSet = 0;
    gameObj->curState = 0;
    gameObj->subState = 1;
    drone->unknown3f0 = 0;
    drone->flags = (drone->flags & 0xffea05e2) | 0x801e2;
    // Everything from the opponent to 0x230
    memset(&drone->opponent, 0, offsetof(Drone_tag, unknown230) - offsetof(Drone_tag, opponent));
    Vec_Zero(&drone->velocity);
    Vec_Zero(&drone->unknown3a0);
    Vec_Copy(&gameObj->position, &drone->moveTarget);
    drone->moveTargetCel = gameObj->inCel;
    Vec_Copy(&gameObj->rotation, &drone->moveTargetRot);
    drone->speechChannel = NULL;
    drone->unknown3ca = 0;
    drone->turnRate = 0.1f / FRAME_RATE_DIV;

    // The skin's class and anim set, and what else the skin says. The skin may be changed in the placement's own keys.
    if (GameState.CurrentLevelHashcode == HT_Level_EvilBase && keys->skin == HT_Skin_EliteCombat_B)
        keys->skin = HT_Skin_EliteCombat;
    drone->skinHashcode = keys->skin;
    ushort weapon = 1;
    switch (drone->skinHashcode) {
    case HT_Skin_Yakuza_Trenchcoat: case HT_Skin_Yakcoatopen: case HT_Skin_Yakuza_RedTrenchcoat:
    case HT_Skin_Yakuza_Leather: case HT_Skin_Yakuza_Suit:
        drone->skinClass = 1;
        drone->animSet = 0xc;
        break;
    case HT_Skin_SnowGuard_Head4: case HT_Skin_SnowGuard_Camera: case HT_Skin_SnowGuard_Hat:
        drone->skinClass = 2;
        drone->animSet = 0xc;
        break;
    case HT_Skin_BlackOps: case HT_Skin_BlackOps_RedVest: case HT_Skin_BlackOps_GasMask: case HT_Skin_00004c:
        drone->skinClass = 3;
        drone->animSet = 0xc;
        break;
    case HT_Skin_HazMat_body: case HT_Skin_Hazmat_heavy_1: case HT_Skin_Hazmat_NoMask: case HT_Skin_Hazmat_heavy_3:
        drone->skinClass = 4;
        drone->animSet = 0xc;
        break;
    case HT_Skin_Sniper:
        drone->skinClass = 5;
        drone->animSet = 0xc;
        break;
    case HT_Skin_Elite: case HT_Skin_Elite_B: case HT_Skin_Elite_Beret:
        drone->skinClass = 6;
        drone->animSet = 0xc;
        break;
    case HT_Skin_EliteCombat: case HT_Skin_EliteCombat_B: case HT_Skin_EliteCaptain:
        drone->skinClass = 7;
        drone->animSet = 0xc;
        break;
    case HT_Skin_Servant: case HT_Skin_00002d: case HT_Skin_TowelGirl:
        drone->skinClass = 8;
        drone->animSet = 1;
        drone->unknown15 = 1;
        break;
    case HT_Skin_Mayhew_Tux: case HT_Skin_Mayhew_Shirt:
        drone->animSet = 3;
        drone->skinClass = 9;
        break;
    case HT_Skin_Ninja:
        drone->animSet = 7;
        drone->skinClass = 0xb;
        weapon = 0;
        break;
    case HT_Skin_Zoe_PartyDress: case HT_Skin_zoe_partydress_b:
        drone->skinClass = 0xc;
        drone->animSet = 0xe;
        drone->vulnerable = 0;
        drone->unknown15 = 1;
        break;
    case HT_Skin_Dominique: case HT_Skin_PartyGirl01: case HT_Skin_PartyGirl02: case HT_Skin_PartyGirl03:
    case HT_Skin_PartyGirl04:
        drone->skinClass = 0xd;
        drone->animSet = 0xe;
        drone->unknown15 = 1;
        break;
    case HT_Skin_PartyGuest_OldMan: case HT_Skin_PartyGuest_Male:
        drone->skinClass = 0xe;
        drone->animSet = 0xc;
        break;
    case HT_Skin_TruckDriver:
        drone->skinClass = 0xf;
        drone->dtype = DTYPE_TruckDriver;
        drone->animSet = 0xc;
        break;
    case HT_Skin_Rook: case HT_Skin_Rook_Scarred:
        keys->drawWeapon = 6;
        drone->skinClass = 0x10;
        drone->animSet = 0xc;
        break;
    case HT_Skin_CastleGuard_2: case HT_Skin_CastleGuard: case HT_Skin_CastleGuard_3:
        drone->skinClass = 0x11;
        drone->animSet = 0xa;
        break;
    case HT_Skin_Kiko_Jacket: case HT_Skin_Kiko_Dress:
        drone->animSet = 0xe;
        drone->skinClass = 0x12;
        drone->unknown15 = 1;
        break;
    case HT_Skin_Kiko_Combat:
        drone->animSet = 0xd;
        drone->skinClass = 0x13;
        drone->unknown15 = 1;
        break;
    case HT_Skin_Drake_Tux: case HT_Skin_Drake_Spacesuit: case HT_Skin_Drake_SpacesuitBurnt:
        drone->skinClass = 0x14;
        drone->animSet = 0xc;
        break;
    case HT_Skin_BusinessWoman_A: case HT_Skin_BusinessWoman_B:
        drone->skinClass = 0x15;
        drone->animSet = 2;
        drone->unknown15 = 1;
        break;
    case HT_Skin_000010: case HT_Skin_000011: case HT_Skin_Dominique_Jacket:
        drone->skinClass = 0x16;
        drone->animSet = 2;
        drone->unknown15 = 1;
        break;
    case HT_Skin_Drake_SuitA:
        drone->vulnerable = 0;
        drone->impactImmune = 1;
        // fall through
    case HT_Skin_Guard_1: case HT_Skin_Guard_2: case HT_Skin_Guard_3: case HT_Skin_Guard_4: case HT_Skin_Tech_a:
    case HT_Skin_000032: case HT_Skin_000033:
        drone->skinClass = 0x17;
        drone->animSet = 0xb;
        break;
    case HT_Skin_Astronaut: case HT_Skin_000094: case HT_Skin_Drake_Astronaut:
        drone->flies = 1;
        drone->skinClass = 0x18;
        drone->animSet = 0x10;
        break;
    default:
        drone->skinClass = 0;
        drone->animSet = 0xc;
        break;
    }

    if (MPSettings.dronesAreBots)
        BOT_setGender(drone);

    // The weapon (key 9; with none, the skin's), its clip and the anim set it holds it with
    if (keys->weapon != 0)
        weapon = (ushort)keys->weapon;
    drone->secondWeapon = (char)keys->drawWeapon;
    switch (weapon) {
    case 2: case 6: case 0xa: case 0xb: case 0xe: case 0x10: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
    case 0x40:
        drone->damageScale = 2.0f;
        drone->clipSize = 0xc;
        drone->clip = 0xc;
        drone->animSet = drone->skinClass == 0xc ? 8 : 4;
        break;
    case 0x12: case 0x14: case 0x15:
        if (drone->skinClass == 0xb)
            weapon = 0x14;
        else
            drone->animSet = 6;
        drone->clipSize = drone->clip = 0x40;
        break;
    case 0x16: case 0x18: case 0x19: case 0x1a: case 0x1b:
        drone->animSet = 5;
        drone->clipSize = drone->clip = 0x40;
        break;
    case 0x1e: case 0x1f: case 0x24: case 0x25:
        drone->animSet = 0x13;
        drone->clipSize = drone->clip = 5;
        break;
    case 0x1c: case 0x1d: case 0x2a: case 0x2b: case 0x2c: case 0x2d:
        drone->animSet = 9;
        drone->clipSize = drone->clip = 8;
        break;
    case 0x30: case 0x32: case 0x33: case 0x6a: case 0x6e:
        if (drone->skinClass != 0x18)
            drone->animSet = 6;
        drone->clipSize = drone->clip = 10000;
        break;
    default:
        weapon = 1;
        drone->clipSize = drone->clip = 0;
        break;
    }

    // Grenades (key 11): the kind in the high bits, how many in the low four
    drone->grenadeWeapon = (ushort)(keys->grenades >> 4);
    drone->grenades = keys->grenades & 0xf;
    switch (drone->grenadeWeapon) {
    case 1:
        drone->grenadeWeapon = 0x36;
        break;
    case 2:
        drone->grenadeWeapon = 0x35;
        break;
    case 4:
        drone->grenadeWeapon = 0x34;
        break;
    case 5:
        drone->grenadeWeapon = drone->grenades + 0x3a;
        drone->grenades = 1;
        break;
    case 6:
        drone->grenadeWeapon = 0x6c;
        drone->grenades = 1;
        break;
    default:
        drone->grenades = 0;
        drone->grenadeWeapon = 0;
        break;
    }
    if (GameState.CurrentLevelHashcode == HT_Level_EvilBase && drone->grenadeWeapon >= 0x35 &&
        drone->grenadeWeapon <= 0x36)
        drone->grenadeWeapon = 0x34;

    // The ranges (key 14). The sums stay on the x87 stack before their store.
    drone->combatRanges[0] = 4.0f;
    drone->combatRanges[3] = 2.0f;
    double spread;
    switch (keys->combatRange) {
    case 1:
        drone->combatRanges[1] = Float_FRand(3.0f) + 12.0f;
        spread = Float_FRand(3.0f);
        drone->viewRange = 12.0f;
        drone->combatRanges[4] = (float)(spread + drone->combatRanges[1] + 1.0);
        break;
    case 2:
        drone->combatRanges[1] = Float_FRand(3.0f) + 12.0f;
        spread = Float_FRand(3.0f);
        drone->viewRange = 16.0f;
        drone->combatRanges[4] = (float)(spread + drone->combatRanges[1] + 1.0);
        break;
    case 3:
        drone->combatRanges[1] = Float_FRand(6.0f) + 25.0f;
        drone->combatRanges[4] = Float_FRand(6.0f) + 100.0f;
        drone->viewRange = 40.0f;
        break;
    case 4:
        drone->combatRanges[1] = Float_FRand(8.0f) + 75.0f;
        drone->combatRanges[4] = 1000.0f;
        drone->viewRange = 1000.0f;
        break;
    case 5:
        drone->combatRanges[1] = Float_FRand(4.0f) + 12.0f;
        spread = Float_FRand(4.0f);
        drone->viewRange = 0.0f;
        drone->viewAngle = 0.0f;
        drone->combatRanges[4] = (float)(spread + drone->combatRanges[1] + 1.0);
        break;
    default:
        drone->combatRanges[1] = Float_FRand(4.0f) + 12.0f;
        spread = Float_FRand(4.0f);
        drone->viewRange = 24.0f;
        drone->combatRanges[4] = (float)(spread + drone->combatRanges[1] + 1.0);
        break;
    }
    drone->unknownb4 = 0xf;
    drone->sightTestFrame = 0;
    drone->viewAngle = kHalfPi;
    if (GameState.CurrentLevelHashcode == HT_Level_Tower2A) {
        if (drone->animSet == 9) {
            drone->combatRanges[1] = 5.0f;
            drone->combatRanges[4] = 8.0f;
        } else {
            drone->combatRanges[1] = 6.0f;
            drone->combatRanges[4] = 9.0f;
        }
    } else if (GameState.CurrentLevelHashcode == HT_Level_EvilBase && drone->animSet != 5) {
        drone->combatRanges[1] = drone->animSet == 9 ? 7.0f : 10.0f;
    }
    drone->combatRanges[2] = Float_FRand(0.0f) + drone->combatRanges[0];

    // The switch channels, the mode, the group and the death script
    drone->waitSwitchChannel = (uchar)keys->waitSwitchChannel;
    drone->modeChangeSwitchChannel = (uchar)keys->modeChangeSwitchChannel;
    drone->deathSwitchChannel = (uchar)keys->deathSwitchChannel;
    drone->waitSwitchInitial = switch_channels[drone->waitSwitchChannel];
    drone->modeChangeSwitchInitial = switch_channels[drone->modeChangeSwitchChannel];
    drone->mode = (short)keys->mode;
    drone->secondaryMode = (short)keys->secondaryMode;
    drone->group = keys->group;
    drone->key7 = keys->key7;
    drone->deathScript = keys->deathScript;
    if (drone->deathScript != 0) {
        drone->scriptedMoveRot = gameObj->rotation;
        drone->scriptedMovePos = gameObj->position;
        if (drone->deathScript != HT_Script_DeathScriptIcyQuip)
            drone->flags |= DRONE_FLAG_FROZEN;
    }

    if (keys->mode >= DMODE_COUNT)
        NDrone2_DoModeSettingsNEW(drone, keys->mode, keys->behaviour, diVars);
    else
        NDrone2_DoModeSettingsOLD(drone, keys->mode, keys->behaviour, diVars);

    // A captain on the difficulties its behaviour says: an upgraded skin, if the class has one
    drone->captain = 0;
    if (behaviour_util_getProperty(DRONE_PROP_09, drone->currentBehaviour) && GameState.difficultyModifier == 1)
        drone->captain = 1;
    if (behaviour_util_getProperty(DRONE_PROP_05, drone->currentBehaviour) && GameState.difficultyModifier == 2)
        drone->captain = 1;
    if (behaviour_util_getProperty(DRONE_PROP_08, drone->currentBehaviour) && GameState.difficultyModifier == 3)
        drone->captain = 1;
    if (drone->captain) {
        switch (drone->skinClass) {
        case 1:
            drone->skinHashcode = HT_Skin_Yakuza_RedTrenchcoat;
            break;
        case 2:
            drone->skinHashcode = HT_Skin_SnowGuard_Camera;
            drone->ninjaEyes = 1;
            break;
        case 3:
            drone->skinHashcode = HT_Skin_BlackOps_RedVest;
            break;
        case 6:
            drone->skinHashcode = HT_Skin_Elite_Beret;
            break;
        case 7:
            drone->skinHashcode = HT_Skin_EliteCaptain;
            break;
        default:
            drone->captain = 0;
            break;
        }
        if (drone->captain) {
            drone->health = DroneCaptain_Mod_Health * drone->health;
            drone->damageScale = DroneCaptain_Mod_BulletDamage;
            drone->accuracy = (uchar)Ftol(1.0 / DroneCaptain_Mod_BulletAccuracy * drone->accuracy);
        }
    }

    // GC check (0x80097e84)
    NF_WARN_IF(!AnimObjectNew(gameObj, drone->skinHashcode), "Could not create AnimObjectNew(o,0x%08x)\n",
               drone->skinHashcode);
    drone->armour = 0;
    if (drone->behaviourClass == 0xc) {
        behaviour_util_setProperty(DRONE_PROP_14, drone->currentBehaviour, 0);
        behaviour_util_setProperty(DRONE_PROP_15, drone->currentBehaviour, 0);
        behaviour_util_setProperty(DRONE_PROP_16, drone->currentBehaviour, 0);
        if (drone->skinClass == 9)
            behaviour_util_setProperty(DRONE_PROP_4C, drone->currentBehaviour, 1);
    }

    // The routes: no straight lines for fliers
    drone->collision.type = HITTEST_CYLINDER;
    drone->activeRoute = NULL;
    AINetwork_ResetRoute(&drone->route1, gameObj, NULL);
    drone->route1.arriveRadius = 2.0f;
    drone->route1.creepReach = 2.0f;
    drone->route1.path = NULL;
    if (drone->flies) {
        drone->route1.flags |= AIROUTE_NO_DIRECT;
        drone->route1.creepReach = 4.0f;
    }
    AINetwork_ResetRoute(&drone->route2, gameObj, NULL);
    drone->route2.path = NULL;
    drone->route2.arriveRadius = 0.5f;
    drone->route2.creepReach = 2.0f;
    if (drone->flies) {
        drone->route2.flags |= AIROUTE_NO_DIRECT;
        drone->route2.creepReach = 4.0f;
    }
    memset(&drone->goalTarget, 0, sizeof(drone->goalTarget));

    gameObj->effectFlags &= ~(FLAG_HIDDEN | OBJ_EFFECT_20);
    drone->flags |= DRONE_FLAG_COLLIDES | DRONE_FLAG_FALLS | DRONE_FLAG_AI_RUNNING;
    if (drone->side == 1)
        PlrStat_LogEnemySpawned();

    // The play script (key 1); two of them make the drone an abseiler instead
    drone->playScript = (HASHCODE)0;
    // A placement's play script for none (DroneKeys.playScript), and the two that make an abseiler
    if (keys->playScript != HT_Script_HASHCODE_BASE) {
        drone->playScript = keys->playScript;
        if (keys->playScript == HT_Script_ScriptAbseil1C) {
            drone->dtype = DTYPE_Abseil28;
            drone->playScript = (HASHCODE)0;
            drone->savedAnimSet = drone->animSet;
            drone->animSet = 0xf;
        } else if (keys->playScript == HT_Script_ScriptAbseil1B) {
            drone->playScript = (HASHCODE)0;
            drone->dtype = DTYPE_Abseil27;
            drone->savedAnimSet = drone->animSet;
            drone->animSet = 0xf;
        }
    }

    // The first anim
    bool animStarted = false;
    drone->animRow = 0xffff;
    if (drone->skinClass == 0xb) {
        DroneAnim_SetAnimD(DANIM_START_SKINCLASS_0B, 0, drone);
        animStarted = true;
    } else if (drone->skinClass == 0x18) {
        DroneAnim_SetAnimD(DANIM_START_SKINCLASS_18, 0, drone);
        animStarted = true;
    }
    if (drone->dtype == DTYPE_Sniper && drone->animSet == 5) {
        drone->animSet = 0x12;
        DroneAnim_SetAnimD(DANIM_START_DTYPE_2, 0, drone);
    } else if (drone->dtype == DTYPE_SniperAlert && drone->animSet == 5) {
        drone->animSet = 0x12;
        DroneAnim_SetAnimD(DANIM_START_DTYPE_19, 0, drone);
    } else if (!animStarted) {
        DroneAnim_SetAnimD(DANIM_START, 0, drone);
    }
    if (gameObj->animState != NULL) {
        gameObj->animState->animObj.rootMotionCallback = DroneAnim_PreTransCallback;
        gameObj->animState->currentWeaponId = (char)weapon;
    }

    if (drone->modeFromOldTable && diVars->gameObj != NULL) {
        diVars->gameObj->curState = 0;
        NDrone2_DoTypeSettingsOLD(drone, drone->dtype);
    }

    if (gameObj->inCel == NULL) {
        gameObj->flags |= 1;
        return NULL;
    }
    gameObj->effectFlags &= ~OBJ_EFFECT_400;

    if (drone->initialState == DSTATE_Attack || drone->initialState == DSTATE_CivilianScared) {
        DroneFunc_SetAsAttacking(drone);
        drone->flags |= DRONE_FLAG_ATTACK_MODE | DRONE_FLAG_ATTACK_ALARM;
        drone->flags2 |= DRONE_FLAG2_10;
    }

    // What some skin classes and types get on top
    if (drone->skinClass == 0x10) {
        if (GameState.CurrentLevelHashcode == HT_Level_EvilBase)
            drone->health = drone->health * 3.0f;
    } else if (drone->skinClass == 0x13) {
        if (GameState.CurrentLevelHashcode == HT_Level_EvilBase) {
            drone->unknownb4 = 0x3c;
            drone->viewAngle = kQuarterPi;
        }
        if (GameState.CurrentLevelHashcode == HT_Level_EvilBaseC)
            drone->impactImmune = 1;
    } else if (drone->skinClass == 0x18) {
        drone->dtypeBase = DTYPE_Astronaut;
        drone->dtype = DTYPE_Astronaut;
        drone->dtype2 = DTYPE_Normal;
        drone->initialState = DSTATE_AstronautLaunch;
        drone->secondState = 0;
        // DroneInit_TakeXHits on the PS2, inline here: as much health as one or two hits of this weapon take
        float hits = GameState.difficultyModifier == 1 ? 1.0f : 2.0f;
        drone->health = weapon_data[Weap_LaserBeamFromSamurai].damage * hits;
        if (drone->skinHashcode == HT_Skin_Drake_Astronaut)
            drone->health = drone->health * 3.0f;
    }
    if (drone->dtype == DTYPE_Ninja) {
        behaviour_util_setProperty(DRONE_PROP_3C, drone->currentBehaviour, 1);
        behaviour_util_setProperty(DRONE_PROP_36, drone->currentBehaviour, 1);
        behaviour_util_setProperty(DRONE_PROP_35, drone->currentBehaviour, 1);
        drone->accuracy = 0;
        drone->health = 100.0f;
        drone->route1.creepReach = 8.0f;
        drone->combatRanges[0] = 2.0f;
    } else if (drone->dtype == DTYPE_Abseil27 || drone->dtype == DTYPE_Abseil28) {
        drone->flies = 1;
        drone->initialState = DSTATE_AbseilInit;
    }

    drone->floatsToMoveTarget = 1;
    if (drone->floatAcceleration == 0.0f)
        drone->floatAcceleration = kFloatAcceleration;
    DroneInit_Collision(drone);
    drone->startHealth = drone->health;

    if (MPSettings.dronesAreBots) {
        BOT_postLoadInit(drone);
    } else {
        behaviour_util_setProperty(DRONE_PROP_17, drone->currentBehaviour, 0);
        drone->accuracyF = 1.0f;
        drone->aimByEvent = 1;
        switch (drone->dtype) {
        case DTYPE_Mayhew:
            behaviour_util_setProperty(DRONE_PROP_34, drone->currentBehaviour, 0);
            behaviour_util_setProperty(DRONE_PROP_36, drone->currentBehaviour, 0);
            behaviour_util_setProperty(DRONE_PROP_3B, drone->currentBehaviour, 0);
            behaviour_util_setProperty(DRONE_PROP_3A, drone->currentBehaviour, 0);
            break;
        case DTYPE_Ninja:
        case DTYPE_Abseil27:
        case DTYPE_Abseil28:
        case DTYPE_Astronaut:
            drone->aimByEvent = 0;
            break;
        case DTYPE_Zoe:
            behaviour_util_setProperty(DRONE_PROP_3B, drone->currentBehaviour, 0);
            behaviour_util_setProperty(DRONE_PROP_3A, drone->currentBehaviour, 0);
            break;
        default:
            break;
        }
        // The animation is read without a test here
        char held = gameObj->animState->currentWeaponId;
        drone->firesByAnim = held >= 0x1c && held <= 0x1d;
        if ((GameState.CurrentLevelHashcode == HT_Level_CastleExterior ||
             GameState.CurrentLevelHashcode == HT_Level_CastleCourtyard) &&
            keys->weapon == 0x16) {
            behaviour_util_setProperty(DRONE_PROP_00, drone->currentBehaviour, 1);
            behaviour_util_setProperty(DRONE_PROP_01, drone->currentBehaviour, 1);
            behaviour_util_setProperty(DRONE_PROP_06, drone->currentBehaviour, 1);
            behaviour_util_setProperty(DRONE_PROP_13, drone->currentBehaviour, 0);
            behaviour_util_setProperty(DRONE_PROP_41, drone->currentBehaviour, 1);
            behaviour_util_setProperty(DRONE_PROP_42, drone->currentBehaviour, 1);
            drone->combatRanges[1] = 10.0f;
        }
    }

    drone->unknown824 = 0x80;
    drone->unknown825 = 0;
    drone->boundsPushFlags = 0x1c00;
    if (behaviour_util_getProperty(DRONE_PROP_34, drone->currentBehaviour) &&
        !behaviour_util_getProperty(DRONE_PROP_36, drone->currentBehaviour))
        drone->unknown825 |= 2;
    if (behaviour_util_getProperty(DRONE_PROP_3B, drone->currentBehaviour) ||
        behaviour_util_getProperty(DRONE_PROP_3A, drone->currentBehaviour))
        drone->unknown825 |= 1;

    drone->bodyGlow = BodyGlow_Create(diVars->gameObj);
    return diVars->gameObj;
}
