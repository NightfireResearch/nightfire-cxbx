#include "Autoaim.h"
#include "mp/multiplayer.h"
#include "../math/X87Ops.h"
#include <math.h>

#include <stdio.h>

#define LoadMsg U32_AT(0x00279158)
#define FlashMod FLOAT_AT(0x0027911c)


// NOAUTOINJECT
void Check_Target(obj_tag* obj) {

    if(obj == NULL)
        return;

    if(LoadMsg)
        LoadMsg--;

    printf("Current movement type is %d\n", obj->subState);

    switch(obj->subState) {
        case MovementType_Grapple:
        case MovementType_Wire:
        case MovementType_Creep:
        case MovementType_RemoteControl:
        case MovementType_RCVehicle:
        case MovementType_Emplacement:
        case 13:
        case 14:
        case MovementType_Zipline:
        case MovementType_Ronin:
            return;
    }

    // Flashbang modifier?! No clue.
    // On the x87: FCOS, and the sum with 1, left unrounded
    double someNum = (double)(FCos((double)(int32_t)GameState.LoadTimeStart * 0.01f * 6.2831855f) + Extended(1.0f));
    FlashMod = (float)((someNum * 0.5f + 1.0f) * 0.5f);

    // Only process the main part of this every 6 frames
    if(GameState.NumFramesUnpaused %6 != 0)
        return;

    // Process the bulk of the autoaim logic
    BLData* blData = (BLData*) obj->extraObjectData;

    MPTeam team = MPSettings.maybeDroneAIEnabled ? MP_getObjectTeam(obj) : NO_TEAM;

    // TODO: The rest

}