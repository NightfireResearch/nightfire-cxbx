#include "Autoaim.h"
#include "mp/multiplayer.h"
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
        case MovementType_FlyingRocket:
        case 11:
        case 12:
        case 13:
        case 14:
        case MovementType_Zipline:
        case MovementType_Ronin:
            return;
    }

    // Flashbang modifier?! No clue.
    float someNum = cosf(GameState.LoadTimeStart * 0.01 * 6.2831855f);
    FlashMod = ((someNum + 1.0) * 0.5 + 1.0) * 0.5;

    // Only process the main part of this every 6 frames
    if(GameState.NumFramesUnpaused %6 != 0)
        return;

    // Process the bulk of the autoaim logic
    BLData* blData = (BLData*) obj->extraObjectData;

    MPTeam team = MPSettings.maybeDroneAIEnabled ? MP_getObjectTeam(obj) : NO_TEAM;

    // TODO: The rest

}