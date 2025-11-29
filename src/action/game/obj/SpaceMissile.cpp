#include "SpaceMissile.h"

#include "../../engine/Anim.h"

#pragma pack(push, 1)
typedef struct {
    SCRIPTINFO* currentAnimScript;
    HASHCODE launchCouplerAnim; // Maybe deploy? TBC
    HASHCODE missileDeployAnim; // Maybe launch? TBC
    ushort missileNum;
    ushort maybeSwitchOnLaunched; // Switch channel, triggered by SpaceMissile_Update when the missile enters some state (launched?)
    ushort maybeSwitchOnDisarmed; // Switch channel, triggered by SpaceMissile_Update when the missile enters some other state (disarmed?)
    ushort deployOnChannel; // Switch channel, monitored to start the launch animation (with delay)
    float deployDelay;
    SCRIPTINFO* animScriptInfo1;
    SCRIPTINFO* animScriptInfo2;
    SCRIPTINFO* animScriptInfo3;
    SCRIPTINFO* animScriptInfo4;
} ObjData_SpaceMissile;

static_assert(sizeof(ObjData_SpaceMissile) == 0x28, "Bad size for ObjData_SpaceMissile");

typedef struct {
    ObjectCreationData_Basic baseCreation; // 0x00-0x2b
    HASHCODE defaultAnim; // 0x2c
    HASHCODE launchCouplerAnim; // 0x30
    HASHCODE missileDeployAnim; // 0x34
    int deployDelay; // 0x38
    ushort missileNum; // 0x3c
    ushort _pad_1;
    ushort maybeSwitchOnLaunched; // 0x40
    ushort _pad_2;
    ushort maybeSwitchOnDisarmed; // 0x44
    ushort _pad_3;
    ushort deployOnChannel; // 0x48
    ushort _pad_4;
    HASHCODE animScript1;
    HASHCODE animScript2;
    HASHCODE animScript3;
    HASHCODE animScript4;
} Create_SpaceMissile_Params;

#pragma pack(pop)

// AUTOGEN
void SpaceMissile_Update(obj_tag *obj);

// 8 one-byte bools, representing whether each has been started, found at 0x0029a28c
#define MissileDeploy (*(uchar(*)[8])(0x0029a28c))

// AUTOINJECT
obj_tag * Create_SpaceMissile(_VECTOR *pos,_VECTOR *rot, level_tag* lvl) {
    
    Create_SpaceMissile_Params *createParams = (Create_SpaceMissile_Params*)lvl;

    obj_tag* gameObj = Control_CreateObjEx(sizeof(ObjData_SpaceMissile), pos, rot, NULL, NULL, NULL, 1, 0, 1.0f, 0, 0xFF, 0xFF, 0xFF);
    gameObj->objectType = OBJECTTYPE_SPACEMISSILE;

    ObjData_SpaceMissile *spaceMissile = (ObjData_SpaceMissile*)gameObj->extraObjectData;

    spaceMissile->launchCouplerAnim = createParams->launchCouplerAnim;
    spaceMissile->missileDeployAnim = createParams->missileDeployAnim;
    spaceMissile->deployDelay = (float) createParams->deployDelay;
    spaceMissile->missileNum = createParams->missileNum;
    spaceMissile->maybeSwitchOnLaunched = createParams->maybeSwitchOnLaunched;
    spaceMissile->maybeSwitchOnDisarmed = createParams->maybeSwitchOnDisarmed;
    spaceMissile->deployOnChannel = createParams->deployOnChannel;
    
    spaceMissile->currentAnimScript = Script_Load(createParams->defaultAnim, pos, rot, NULL, NULL, NULL, NULL);
    Script_Play(spaceMissile->currentAnimScript, 0);

    if(createParams->animScript1 != 0) {
        spaceMissile->animScriptInfo1 = Script_Load(createParams->animScript1, pos, rot, NULL, NULL, NULL, NULL);
        Script_Play(spaceMissile->animScriptInfo1, 0);
    }

    if(createParams->animScript2 != 0) {
        spaceMissile->animScriptInfo2 = Script_Load(createParams->animScript2, pos, rot, NULL, NULL, NULL, NULL);
        Script_Play(spaceMissile->animScriptInfo2, 0);
    }

    if(createParams->animScript3 != 0) {
        spaceMissile->animScriptInfo3 = Script_Load(createParams->animScript3, pos, rot, NULL, NULL, NULL, NULL);
        Script_Play(spaceMissile->animScriptInfo3, 0);
    }

    if(createParams->animScript4 != 0) {
        spaceMissile->animScriptInfo4 = Script_Load(createParams->animScript4, pos, rot, NULL, NULL, NULL, NULL);
        Script_Play(spaceMissile->animScriptInfo4, 0);
    }

    gameObj->curState = 0;
    Script_Update(spaceMissile->currentAnimScript);
    
    for(int i = 0; i < 8; i++) {
        MissileDeploy[i] = 0;
    }

    return gameObj;
}
