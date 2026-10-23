#include "Pickup.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "../../game.h"
#include "../../engine/Text.h"
#include "../../math/X87Ops.h"
#include "../../sound/Sound.h"
#include "../Upgrade.h"
#include "../weapon_stats.h"
#include "../drone/BOT.h"
#include "../drone/BOTSTATE.h"
#include "../drone/BOTWEAP.h"
#include "../mp/multiplayer.h"
#include "../sp/PlayerStats.h"
#include "build.h"
#include "control.h"
#include "PlayerWeapon.h"

// XBE_GLOBAL(0x0017e5d8, 0x6e)
short PickupMatrix[PICKUP_SETS][5] = {
    {6, 28, 30, 24, 44},
    {2, 6, 10, 16, 66},
    {18, 21, 24, 28, 82},
    {14, 30, 17, 36, 50},
    {42, 44, 58, 46, 26},
    {53, 52, 58, 55, 59},
    {2, 17, 55, 36, 66},
    {10, 30, 82, 26, 50},
    {26, 59, 50, 58, 44},
    {21, 22, 55, 17, 36},
};

#pragma fp_contract(off)

// XBE_GLOBAL(0x0017e648, 0x36) readonly relocate
short UseableGuns[27] = {
    2,
    6,
    10,
    12,
    14,
    16,
    18,
    20,
    22,
    25,
    26,
    28,
    30,
    36,
    42,
    44,
    46,
    51,
    55,
    53,
    54,
    52,
    58,
    59,
    66,
    17,
    82,
};

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

// The bot's armour shown: empty on the Xbox, where the linker shares one empty function among several
static void BOTWEAP_setDroneArmour(Drone_tag *drone) {}

// The level whose ammo 0x2c pickups always hold 4
#define PICKUP_LEVEL_FOUR HT_Level_CastleIndoors2

// What the multiplayer game modes and their teams change about bots' weapons (MPSettings.GameMode)
#define GAMEMODE_TEAM0_RULE 0x20000080
#define GAMEMODE_TEAM1_RULE 0x20000040

// The pickups' messages
#define TEXT_GOT_WEAPON ((Action_TranslatedText)0x0200000d)
#define TEXT_GOT_AMMO ((Action_TranslatedText)0x0200000e)
#define TEXT_GOT_ARMOUR ((Action_TranslatedText)0x0200000f)
#define TEXT_GOT_KEY ((Action_TranslatedText)0x02000010)
#define TEXT_GOT_INVISIBILITY ((Action_TranslatedText)0x02000013)
#define TEXT_GOT_BONUS ((Action_TranslatedText)0x0200004a)

#define PICKUP_SOUND ((ushort)0xf5)
#define PICKUP_ARMOUR_SOUND ((ushort)0x141)
#define PICKUP_BONUS_SOUND ((ushort)0x14c)

static void SetChannel(ushort channel) {
    if (channel == 0)
        return;
    switch_channels[channel] = 1;
    switch_channels_time[channel] = GameState.NumFramesUnpaused;
}

// A pickup on the floor below pos (or the matrix's translation, which is also its matrix). Placed ones stay; others
// are turned upright, pushed out of whatever they are in, put down on their model's base and fall. Not made without a
// model, for item 0xffff, or for the bonus without a gold medal; kinds 7-11 are the multiplayer set's
// (Pickup_CreateFromSet). Lit by their cel but for the bonus, armour (which flashes) and multiplayer's, which spin.
// AUTOINJECT
obj_tag* Pickup_Create(_VECTOR *pos, _VECTOR *rot, _MATRIX *mtx, celglist_tag *celgl, ushort kind, ushort item,
                       uint count, uint channel, uint sound, char placed, ushort respawn, ushort lifetime,
                       uint label) {
    if (celgl == NULL || item == 0xffff)
        return NULL;
    if (kind == PICKUP_KIND_BONUS && !PlrStats_HasGoldMedal())
        return NULL;

    _VECTOR fromMatrix;
    if (pos == NULL) {
        Vec_Copy((_VECTOR *)&mtx->m[12], &fromMatrix);
        pos = &fromMatrix;
    }
    if (kind >= PICKUP_KIND_SET_FIRST && kind <= PICKUP_KIND_SET_LAST)
        return Pickup_CreateFromSet(pos, rot, kind);

    obj_tag *obj = control_create_object(sizeof(PICKUPINFO), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    if (mtx != NULL)
        Mat_Copy(mtx, &obj->transformMatrix);

    PICKUPINFO *info = (PICKUPINFO *)obj->extraObjectData;
    if (GameState.CurrentLevelHashcode == PICKUP_LEVEL_FOUR && item == 0x2c)
        count = 4;
    info->count = (short)count;
    info->kind = kind;
    info->item = item;
    info->channel = (ushort)channel;
    info->sound = sound != 0 ? (ushort)sound : 0xffff;
    info->respawn = respawn;
    info->lifetime = lifetime;
    info->mpIndex = -1;
    info->label = (Action_TranslatedText)label;
    Vec_Zero(&info->vel);

    obj->objGraphics = celgl;
    obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX;
    obj->effectFlags = celgl->applyFlagsToObject | 0x840;
    obj->effectFlags &= ~0x2000;
    if (kind == PICKUP_KIND_BONUS || MPSettings.isMultiplayer) {
        info->spin = 1;
        obj->effectFlags &= ~0x800;
    } else {
        info->spin = 0;
    }
    if (kind == PICKUP_KIND_ARMOUR)
        obj->effectFlags = (obj->effectFlags & ~0x800) | 0x80000;

    _VECTOR floor;
    Vec_Copy(pos, &floor);
    build_PointOnFloor(build_FindCel(pos, glb_world), NULL, &floor, 1.0f, NULL);
    Vec_Copy2(&floor, &obj->position, &obj->lastPosition);
    build_LinkToRoom(obj, 0, glb_world);
    Control_BuildWorldSph(obj);
    obj->objectType = OBJECTTYPE_PICKUP;

    _VECTOR *at = (_VECTOR *)&obj->transformMatrix.m[12];
    if (placed) {
        info->state = PICKUP_RESTING;
    } else {
        _VECTOR dir;
        Mat_GetDir(&dir, &obj->transformMatrix);
        Mat_Align2Up(obj->transformMatrix.m, &CONST_UP_VECTOR.x, &dir.x);
        _VECTOR centre;
        HITDATA_tag *hits = NULL;
        Vec_Copy(&obj->centrePoint, &centre);
        Collide_SphereIntersect(&centre, obj->radius, obj->inCel, obj, NULL, &hits, 0, 0, 0x50d, 8);
        Collide_FreeHitList(&hits);
        _VECTOR push;
        Vec_Subtract(&centre, &obj->centrePoint, &push);
        Vec_Add2(at, &push, at);
        at->y = (float)(at->y - ((double)obj->radius - AbsFchs(obj->objGraphics->extentMin.y)));
        Control_BuildWorldSph(obj);
        info->state = PICKUP_FALLING;
    }
    obj->transformFlags |= TRANSFORM_MOVED;
    Vec_Subtract(at, &obj->centrePoint, &info->offset);

    if (MPSettings.isMultiplayer) {
        info->mpIndex = -1;
        if (info->state != PICKUP_FALLING)
            MP_RegisterPickup(obj);
    }
    return obj;
}

// Riding what it lands on, but for drones, bullets, dead drones and rigid bodies
bool Pickup_CanInheritPosition(obj_tag *pickup, obj_tag *from) {
    if (from == NULL)
        return false;
    switch (from->objectType) {
    case OBJECTTYPE_DRONE:
    case OBJECTTYPE_BULLET:
    case OBJECTTYPE_DEAD_DRONE:
    case OBJECTTYPE_RIGIDBODY:
        return false;
    }
    Control_InheritVelocity(pickup, from, 3.0f);
    Vec_Copy(&pickup->position, (_VECTOR *)&pickup->transformMatrix.m[12]);
    return true;
}

// Back after its respawn time; falling, to the floor under its model's base (riding what it lands on); spinning;
// taken by the first player, then (with bots) the first bot, within its radius and 1.1 that can see it; gone at the end
// of its lifetime
// AUTOINJECT
void Pickup_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    PICKUPINFO *info = (PICKUPINFO *)obj->extraObjectData;
    _VECTOR *at = (_VECTOR *)&obj->transformMatrix.m[12];

    switch (info->state) {
    case PICKUP_RESPAWNING:
        if (GameState.NumFramesUnpaused - obj->creationTimeFrames > info->respawn * FRAME_RATE_INT * 10) {
            obj->effectFlags &= ~FLAG_HIDDEN;
            info->state = PICKUP_RESTING;
        }
        return;

    case PICKUP_FALLING: {
        auxVec_AddMulR32(&info->vel, &GRAVITY_VECTOR, REC_FRAME_RATE * REC_FRAME_RATE, &info->vel);
        Vec_Add2(at, &info->vel, at);
        obj->transformFlags |= TRANSFORM_MOVED;
        _VECTOR end;
        Vec_Copy(at, &end);
        end.y = (float)(end.y - AbsFchs(obj->objGraphics->extentMin.y));
        // Uninitialised in the original; Collide_RayIntersect sets it
        HITDATA_tag *hits = NULL;
        if (!Collide_RayIntersect(&obj->lastPosition, &end, obj->inCel, obj, NULL, &hits, 0, 0x50d, 0))
            break;
        float below = at->y - hits->hitPosition.y;
        if (below < 0.0f) {
            bool riding = false;
            for (HITDATA_tag *hit = hits; hit != NULL; hit = hit->next)
                riding |= Pickup_CanInheritPosition(obj, hit->hitObj);
            _VECTOR base;
            auxVec_AddMulR32(&hits->hitPosition, &hits->hitDirection,
                             (float)-AbsFchs(obj->objGraphics->extentMin.y), &base);
            Vec_Add2(&base, &info->offset, at);
            Vec_Copy(at, &obj->position);
            if (base.y != 0.0f)
                Vec_Zero(&info->vel);
            if (!riding) {
                info->state = PICKUP_RESTING;
                MP_RegisterPickup(obj);
            }
        }
        Collide_FreeHitList(&hits);
        break;
    }
    }

    if (info->spin) {
        _VECTOR turn;
        turn.z = 0.0f;
        turn.x = 0.0f;
        turn.y = REC_FRAME_RATE;
        _MATRIX mtx;
        Mat_IdentityT(&mtx);
        RotMatrix(&turn, &mtx);
        Concat(&obj->transformMatrix, &mtx);
        Mat_CopyRot(&mtx, &obj->transformMatrix);
    }

    double reach = (double)obj->radius + 1.1f;
    float reachSq = (float)(reach * reach);
    for (ushort i = 0; i < NUM_PLAYERS; i++) {
        obj_tag *player = glb_players[i];
        if (player == NULL || player->curState != 1 || player->objectType == OBJECTTYPE_DEAD_PLAYER)
            continue;
        if (!(Vec_SqDist3D(&obj->centrePoint, &player->position) >= reachSq) &&
            Collide_LineOfSight(&glb_players[i]->centrePoint, &obj->centrePoint, glb_players[i]->inCel, obj, NULL,
                                0x721)) {
            Pickup_Handler(obj, info, glb_players[i]);
            return;
        }
    }

    if (MPSettings.dronesAreBots) {
        for (ushort i = 0; i < NUM_BOTS; i++) {
            obj_tag *bot = MPGame.players[NUM_PLAYERS + i].playerObj;
            if (bot == NULL || bot->objectType == OBJECTTYPE_DEAD_DRONE)
                continue;
            Drone_tag *drone = (Drone_tag *)bot->extraObjectData;
            if ((drone->flags & 0x600) || !(drone->flags & 0x100) || drone->health < 0.0f || drone->health == 0.0f)
                continue;
            if (bot->flags & 1)
                continue;
            if (!(Vec_SqDist3D(&obj->centrePoint, &bot->position) >= reachSq) &&
                Collide_LineOfSight(&bot->position, &obj->centrePoint, bot->inCel, obj, NULL, 0x721)) {
                Pickup_Handler(obj, info, bot);
                return;
            }
        }
    }

    if (info->lifetime != 0) {
        info->lifetime--;
        if (info->lifetime == 0) {
            obj->flags |= 1;
            MP_UnregisterPickup(obj);
        }
    }
}

// A bot takes it: weapons, ammo and armour only. False when it is left.
static bool BotTakes(PICKUPINFO *info, obj_tag *by) {
    Drone_tag *drone = (Drone_tag *)by->extraObjectData;
    BOT_vars_t *bot = drone->botVars;
    Control_Plr2Ind(by);
    int item = info->item;
    if (MPSettings.isMultiplayer) {
        if (MPSettings.Player[drone->botVars->playerIndex].TeamId != 0) {
            if (item == 13)
                item = 10;
        } else if (item == 10 && bot->weapons[10].held) {
            item = 13;
        }
    }

    switch (info->kind) {
    case PICKUP_KIND_WEAPON:
        if (!bot->weapons[item].held) {
            short drop = 0;
            if (MPSettings.dronesAreBots && MPSettings.TripleDamageModifierProfessionalMode) {
                // Weapon 0x3b is kept by team 0 in one mode and team 1 in another
                bool keep59 = false;
                if (MPSettings.GameMode == GAMEMODE_TEAM0_RULE)
                    keep59 = MPSettings.Player[bot->playerIndex].TeamId == 0;
                else if (MPSettings.GameMode == GAMEMODE_TEAM1_RULE)
                    keep59 = MPSettings.Player[bot->playerIndex].TeamId == 1;
                short klass = weapon_data[PickupMatrix[MPSettings.weaponSet][0]].weaponBaseNum;
                for (short k = 2; k < 0x53; k++) {
                    if (k == klass || !drone->botVars->weapons[k].held)
                        continue;
                    if (k == 0x3b && keep59)
                        continue;
                    drop = k;
                    break;
                }
            }
            if (!BOTWEAP_EquipWeapon(drone, item, info->count))
                return false;
            if (drop != 0) {
                drone->botVars->weapons[drop].held = 0;
                BOTSTATE_pickupWeaponChangeChoice(drone, item, 0, 0);
            }
            if (!BOTWEAP_hasLoadedWeapon(drone, item))
                return false;
            BOTSTATE_pickupWeaponChangeChoice(drone, item, 0, 0);
        } else {
            BOTWEAP_EquipAmmo(drone, item, info->count);
            if (!BOTWEAP_hasLoadedWeapon(drone, item))
                return false;
            BOTSTATE_pickupWeaponChangeChoice(drone, item, 1, 0);
        }
        BOTSTATE_setPickupVisitTime(drone, MP_getMPpickup(info));
        return true;

    case PICKUP_KIND_AMMO:
        BOTWEAP_EquipAmmo(drone, item, info->count);
        if (!BOTWEAP_hasLoadedWeapon(drone, item))
            return false;
        BOTSTATE_pickupWeaponChangeChoice(drone, item, 1, 0);
        BOTSTATE_setPickupVisitTime(drone, MP_getMPpickup(info));
        return true;

    case PICKUP_KIND_ARMOUR: {
        float health = drone->health + 20.0f;
        drone->health = health;
        if (health < 0.0f)
            drone->health = 0.0f;
        else if (health > (float)bot->stats.health)
            drone->health = (float)bot->stats.health;
        bot->armour += (uchar)info->count;
        if (bot->armour > 50)
            bot->armour = 50;
        BOTWEAP_setDroneArmour(drone);
        BOTSTATE_setPickupVisitTime(drone, MP_getMPpickup(info));
        return true;
    }
    }
    return true;
}

// A player takes it: the weapon (upgraded; in multiplayer as the character has it), and with one-weapon rules the
// first other one is dropped; ammo; a key; armour up to 50; invisibility; the bonus. The message and the channel,
// then for everyone the sound, and the pickup goes, comes back later, or stays. Left when nothing of it was taken.
void Pickup_Handler(obj_tag *obj, PICKUPINFO *info, obj_tag *by) {
    ushort shown = info->kind;
    if (by->objectType == OBJECTTYPE_DRONE) {
        if (!BotTakes(info, by))
            return;
    } else {
        BLData *blData = (BLData *)by->extraObjectData;
        int playerNum = blData->playerNum;
        uint item = info->item;
        if (MPSettings.isMultiplayer) {
            BOT_stats_t *stats = BOT_getDefaultStats(MPSettings.Player[playerNum].SkinNum);
            if (stats->isBad == 1) {
                if (item == 10 && blData->weaponStats[10].enabled)
                    item = 13;
            } else if (item == 13) {
                item = 10;
            }
            item = Upgrade_MPWeapon(blData->playerNum, item);
        } else {
            item = Upgrade_Weapon(item);
        }

        short taken = 0;
        switch (info->kind) {
        case PICKUP_KIND_WEAPON:
            if (!blData->weaponStats[item].enabled) {
                short drop = 0;
                if (MPSettings.dronesAreBots && MPSettings.TripleDamageModifierProfessionalMode) {
                    short klass = weapon_data[PickupMatrix[MPSettings.weaponSet][0]].weaponBaseNum;
                    for (short k = 2; k < 0x48; k++) {
                        if (k != klass && blData->weaponStats[k].enabled) {
                            drop = k;
                            break;
                        }
                    }
                }
                if (!Player_EquipWeapon(blData, item, info->count))
                    return;
                if (drop != 0) {
                    blData->weaponStats[drop].enabled = 0;
                    blData->weaponStats[drop].clipOrCooldown = 0;
                    AnimState *anim = by->animState;
                    if (anim->currentWeaponId == drop)
                        anim->switchingToWeaponId = (char)item;
                }
                shown = PICKUP_KIND_WEAPON;
                break;
            }
            taken = Player_EquipAmmo(blData, item, info->count);
            if (taken == 0)
                return;
            shown = PICKUP_KIND_AMMO;
            break;
        case PICKUP_KIND_AMMO:
            taken = Player_EquipAmmo(blData, item, info->count);
            if (taken == 0)
                return;
            break;
        case PICKUP_KIND_KEY:
            Player_EquipKey(blData, info->item);
            break;
        case PICKUP_KIND_ARMOUR:
            if (blData->armor >= 50.0f)
                return;
            blData->armor = 50.0f;
            blData->hudFadeIn = 1.0f;
            break;
        case PICKUP_KIND_INVISIBILITY:
            if (by->objectType == OBJECTTYPE_PLAYER)
                ((BLData *)by->extraObjectData)->alpha = 0.0f;
            break;
        case PICKUP_KIND_BONUS:
            PlrStat_LogBondBonus(0);
            break;
        }

        char *text = NULL;
        bool show = true;
        switch (shown) {
        case PICKUP_KIND_WEAPON:
            text = Txt_GetStringFromHeap(0);
            if (MPSettings.isMultiplayer) {
                sprintf(text, Txt_BindLabel(weapon_data[info->item].nameShort, 0));
            } else {
                const char *name = Txt_BindLabel(weapon_data[info->item].nameLong, 0);
                sprintf(text, Txt_BindLabel(TEXT_GOT_WEAPON, 0), name);
            }
            info->sound = PICKUP_SOUND;
            break;
        case PICKUP_KIND_AMMO: {
            text = Txt_GetStringFromHeap(0);
            Action_TranslatedText ammo = ammo_data[weapon_data[info->item].ammoType].ammoName;
            if (MPSettings.isMultiplayer) {
                sprintf(text, "%d x %s", taken, Txt_BindLabel(ammo, 0));
            } else if (GetLanguage() == 1) {
                const char *name = Txt_BindLabel(ammo, 0);
                sprintf(text, Txt_BindLabel(TEXT_GOT_AMMO, 0), name, taken);
            } else {
                const char *name = Txt_BindLabel(ammo, 0);
                sprintf(text, Txt_BindLabel(TEXT_GOT_AMMO, 0), taken, name);
            }
            info->sound = PICKUP_SOUND;
            break;
        }
        case PICKUP_KIND_KEY:
            text = Txt_GetStringFromHeap(0);
            sprintf(text, Txt_BindLabel(TEXT_GOT_KEY, 0));
            break;
        case PICKUP_KIND_ARMOUR:
            text = Txt_GetStringFromHeap(0);
            sprintf(text, Txt_BindLabel(TEXT_GOT_ARMOUR, 0));
            info->sound = PICKUP_ARMOUR_SOUND;
            break;
        case PICKUP_KIND_MISSION:
            if (info->label != 0)
                text = (char *)Txt_BindLabel(info->label, 0);
            else
                show = false;
            break;
        case PICKUP_KIND_INVISIBILITY:
            text = Txt_GetStringFromHeap(0);
            sprintf(text, Txt_BindLabel(TEXT_GOT_INVISIBILITY, 0));
            break;
        case PICKUP_KIND_BONUS:
            text = Txt_GetStringFromHeap(0);
            sprintf(text, Txt_BindLabel(TEXT_GOT_BONUS, 0));
            info->sound = PICKUP_BONUS_SOUND;
            break;
        default:
            show = false;
            break;
        }
        if (show && text != NULL)
            Text_AddMsg(playerNum, 0, 6, text, 0, (short)Ftol(MPSettings.isMultiplayer ? 90.0f : 180.0f));
        SetChannel(info->channel);
    }

    Sound_Play3D((Action_SFX)info->sound, (_VECTOR *)&obj->transformMatrix.m[12], 100.0f, -1.0f, -1.0f, 0, 0, 0);
    if (info->respawn == 0) {
        obj->flags |= 1;
        if (MPSettings.isMultiplayer)
            MP_UnregisterPickup(obj);
    } else if (info->respawn != 0xffff) {
        obj->effectFlags |= FLAG_HIDDEN;
        info->state = PICKUP_RESPAWNING;
        obj->creationTimeFrames = GameState.NumFramesUnpaused;
    }
}

// Set 10 of PickupMatrix: five different weapons drawn from UseableGuns, the first from the first 26
// AUTOINJECT
void __stdcall Pickup_MakeRandomWeaponSet(void) {
    short *set = PickupMatrix[10];
    for (int i = 0; i < 5; i++)
        set[i] = 0;
    for (short i = 0; i < 5; i++) {
        short gun;
        bool taken;
        do {
            gun = UseableGuns[(short)Rand_Rand(i != 0 ? 0x1b : 0x1a)];
            taken = false;
            for (short j = 0; j < i; j++) {
                if (set[j] == gun)
                    taken = true;
            }
        } while (taken);
        set[i] = gun;
    }
}

// Kinds 7-11: the weapon of the game's set for the kind, as its base weapon, two clips of it, placed and back after
// 30 seconds. Weapon 0x1a also has its ammo 0x1b a little to one side, half size, which is what is returned.
// AUTOINJECT
obj_tag* Pickup_CreateFromSet(_VECTOR *pos, _VECTOR *rot, ushort kind) {
    short gun = PickupMatrix[(short)MPSettings.weaponSet][(ushort)(kind - PICKUP_KIND_SET_FIRST)];
    weapon_definition_tag *base = &weapon_data[weapon_data[gun].weaponBaseNum];
    celglist_tag *model = hashtable_hashcode_to_celglist(base->worldModelGfx);
    obj_tag *obj = Pickup_Create(pos, rot, NULL, model, PICKUP_KIND_WEAPON, base->weaponVariantNum,
                                 base->clipSize * 2, 0, PICKUP_SOUND, 1, 3, 0, 0);
    if (gun == 0x1a) {
        _VECTOR beside;
        Vec_Copy(pos, &beside);
        beside.x = beside.x + 0.1f;
        beside.z = beside.z + 0.1f;
        weapon_definition_tag *ammo = &weapon_data[0x1b];
        model = hashtable_hashcode_to_celglist(HT_Entity_AmmoPickupModel);
        obj = Pickup_Create(&beside, rot, NULL, model, PICKUP_KIND_AMMO, ammo->weaponVariantNum, ammo->clipSize, 0,
                            0xffff, 1, 3, 0, 0);
        if (obj == NULL)
            return NULL;
        obj->scale = 0.5f;
    }
    return obj;
}

// A weapon dropped at the matrix with count rounds, which falls and goes after two minutes (30 seconds with bots).
// Nothing for no rounds, a weapon out of range or one of those that are not dropped; 0x6a and 0x6e drop as 0x33.
// AUTOINJECT
obj_tag* Pickup_CreateSimple(_MATRIX *mtx, short weapon, short count, ushort kind) {
    if (count == 0)
        return NULL;
    switch (weapon) {
    case 1:
    case 0x45:
    case 0x47:
    case 0x50:
    case 0x51:
    case 0x53:
        return NULL;
    case 0x6a:
    case 0x6e:
        weapon = 0x33;
        break;
    }
    if (weapon <= 0 || weapon >= 0x72)
        return NULL;
    weapon_definition_tag *def = &weapon_data[weapon];
    ushort lifetime = FRAME_RATE_INT * (MPSettings.dronesAreBots ? 30 : 120);
    return Pickup_Create(NULL, NULL, mtx, hashtable_hashcode_to_celglist(def->worldModelGfx), kind,
                         def->weaponVariantNum, count, 0, PICKUP_SOUND, 0, 0, lifetime, 0);
}
