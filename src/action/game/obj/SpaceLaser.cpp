#include "SpaceLaser.h"

// AUTOGEN
void SpaceLaser_Update(obj_tag *obj);

typedef struct {
    ObjectCreationData_Basic baseData;
    HASHCODE someScript0;
    HASHCODE someScript1;
    HASHCODE someScript;
    uint someNum;
    uint someOtherNum;
    uint someThingBecomesShort1;
    uint someThingBecomesShort2;
    uint someThingBecomesShort3;
} SpaceLaserCreationData;


// AUTOINJECT
void __cdecl Create_SpaceLaser(_VECTOR *pos,_VECTOR *rot, level_tag* lvl) {
    
    SpaceLaserCreationData *creationData = (SpaceLaserCreationData*)lvl;

    obj_tag *gameObj = Control_CreateObjEx(sizeof(ObjData_SpaceLaser), pos, rot, NULL, NULL, NULL, '\x01', 0, 1.0, 0, 0xff, 0xff, 0xff);
    ObjData_SpaceLaser *spaceLaserInfo = (ObjData_SpaceLaser *)gameObj->extraObjectData;
    gameObj->objectType = OBJECTTYPE_SPACELASER;

    spaceLaserInfo->someScripts[0] = creationData->someScript0;
    spaceLaserInfo->someScripts[1] = creationData->someScript1;
    spaceLaserInfo->someScript = creationData->someScript;
    spaceLaserInfo->unknown1 = (float)creationData->someNum;
    spaceLaserInfo->unknown2 = (float)creationData->someOtherNum;
    spaceLaserInfo->unknown3 = creationData->someThingBecomesShort1;
    spaceLaserInfo->otherSwitchChannel = creationData->someThingBecomesShort2;
    spaceLaserInfo->someSwitchChannel = creationData->someThingBecomesShort3;
    spaceLaserInfo->someTimerBeforeRunningScript = 10.0;
    gameObj->curState = 0;

}