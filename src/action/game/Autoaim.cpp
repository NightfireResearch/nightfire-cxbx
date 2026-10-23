#include "Autoaim.h"
#include "mp/multiplayer.h"
#include "obj/Door.h"
#include "obj/Trigger.h"
#include "sp/SwitchChannels.h"
#include "../math/X87Ops.h"
#include <math.h>

#pragma fp_contract(off)

// Frames until a load trigger's message may show again
// XBE_GLOBAL(0x00279158, 0x4)
uint32_t LoadMsg;
// The door messages shown: three in all
// XBE_GLOBAL(0x00279154, 0x4)
uint32_t DoorMsg;
// A pulse once a second between 0.5 and 1, which pickups and other marked objects flash by
// XBE_GLOBAL(0x0027911c, 0x4)
float FlashMod;

// The levels whose crosshair is marked on any drone, the one with no door messages and the one with no load
// trigger message
#define MARK_ALL_FIRST HT_Level_CastleExterior
#define MARK_ALL_LAST HT_Level_CastleIndoors2
#define NO_DOOR_MSG_LEVEL HT_Level_Tower2Elevator
#define NO_LOAD_MSG_LEVEL HT_Level_TowerA

#define TEXT_PRESS_ACTION ((Action_TranslatedText)0x02000009)

// What the crosshair shows (BLData::crosshairType)
enum {
    CROSSHAIR_PLAIN = 2,
    CROSSHAIR_MARKED = 3,
};

// The weapons that do not mark the crosshair on a drone: the fist, and 0x35, 0x36, 0x43, 0x44, 0x4a-0x4d
static bool MarksDrones(char weapon) {
    switch (weapon) {
    case 1:
    case 0x35:
    case 0x36:
    case 0x43:
    case 0x44:
    case 0x4a:
    case 0x4b:
    case 0x4c:
    case 0x4d:
        return false;
    }
    return true;
}

// Each frame: FlashMod. Every sixth, but in the substates that do not aim: what the player's gun points at within
// 50 (BLData::gadgetTarget). A drone learns it is in the sights and marks the crosshair if it is an enemy and the
// weapon can hurt it (any weapon on four levels; in multiplayer, by team), another player by team; a door within 5
// shows its message, three in all, and an unlocked load trigger within 5 its "press action", at most every 20
// seconds.
// AUTOINJECT
void Check_Target(obj_tag* obj) {
    if (obj == NULL)
        return;
    if (LoadMsg)
        LoadMsg--;

    switch (obj->subState) {
    case MovementType_Grapple:
    case MovementType_Wire:
    case MovementType_Creep:
    case MovementType_RemoteControl:
    case MovementType_RCVehicle:
    case MovementType_Emplacement:
    case MovementType_Dead:
    case MovementType_DeadInWater:
    case MovementType_Zipline:
    case MovementType_Ronin:
        return;
    }

    // FCOS and the sums kept on the x87, unrounded
    double pulse = (double)(FCos((double)(int32_t)GameState.LoadTimeStart * 0.01f * 6.2831855f) + Extended(1.0f));
    FlashMod = (float)((pulse * 0.5f + 1.0f) * 0.5f);
    if (GameState.NumFramesUnpaused % 6 != 0)
        return;

    BLData* blData = (BLData*) obj->extraObjectData;
    uint team = 2;
    if (MPSettings.dronesAreBots)
        team = MP_getObjectTeam(obj);
    blData->field_0xf2 = 0;
    blData->crosshairType = CROSSHAIR_PLAIN;
    blData->gadgetTarget = NULL;

    _VECTOR head, dir, end;
    Player_GetHeadPos(&head, obj, NULL);
    Mat_GetDir(&dir, &blData->weaponObject->transformMatrix);
    auxVec_AddMulR32(&head, &dir, 50.0f, &end);
    HITDATA_tag *hits = NULL;
    Collide_RayIntersect(&head, &end, obj->inCel, obj, NULL, &hits, 0, 0xc, 0);

    if (hits != NULL && hits->hitObj != NULL) {
        obj_tag *target = hits->hitObj;
        blData->gadgetTarget = target;
        switch (target->objectType) {
        case OBJECTTYPE_PLAYER:
            if (MPSettings.dronesAreBots && MPSettings.FriendlyFire && MP_isObjectOnTeam(target, team)) {
                blData->crosshairType = CROSSHAIR_MARKED;
                blData->field_0xf2 = 1;
            }
            break;

        case OBJECTTYPE_DRONE: {
            Drone_tag *drone = (Drone_tag *)target->extraObjectData;
            bool spared;
            if (MPSettings.dronesAreBots)
                spared = !MPSettings.FriendlyFire || !MP_isObjectOnTeam(drone->gameObj, team);
            else
                spared = drone->side == 1;
            drone->inPlayersSights = 1;
            drone->inPlayersSightsFrame = GameState.NumFramesUnpaused;
            if (spared)
                break;
            uint level = GameState.CurrentLevelHashcode;
            if ((level >= MARK_ALL_FIRST && level <= MARK_ALL_LAST) || MarksDrones(obj->animState->currentWeaponId)) {
                blData->crosshairType = CROSSHAIR_MARKED;
                blData->field_0xf2 = 1;
            }
            break;
        }

        case OBJECTTYPE_DOOR:
            if (GameState.CurrentLevelHashcode != NO_DOOR_MSG_LEVEL && hits->dist < 5.0f && DoorMsg < 3) {
                char *msg = Door_DoMsg(target);
                if (msg != NULL) {
                    DoorMsg++;
                    Text_AddMsg(blData->playerNum, 0, 1, msg, 0, 0xb4);
                }
            }
            break;

        case OBJECTTYPE_TRIGGER:
            if (Trigger_Type(target) == 10 && hits->dist < 5.0f && LoadMsg == 0 &&
                GameState.CurrentLevelHashcode != NO_LOAD_MSG_LEVEL) {
                LoadMsg = FRAME_RATE_INT * 20;
                if (!Trigger_Locked(hits->hitObj))
                    Text_AddMsg(blData->playerNum, 0, 1, (char *)Txt_BindLabel(TEXT_PRESS_ACTION, 0), 0, 0xb4);
            }
            break;
        }
    }
    Collide_FreeHitList(&hits);
}
