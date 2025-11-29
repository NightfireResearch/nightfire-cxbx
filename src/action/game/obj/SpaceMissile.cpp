#include "SpaceMissile.h"

#include "../../engine/Script.h"

#pragma pack(push, 1)
typedef struct {
    SCRIPTINFO* currentAnimScript;
    HASHCODE missileLaunchCouplerIntactAnim; // Maybe deploy? TBC
    HASHCODE missileLaunchCouplerDestroyedAnim; // Maybe launch? TBC
    ushort missileNum; // 1-indexed
    ushort switchChannelOnLaunchedWithCouplerIntact; // triggered on animation completion
    ushort switchChannelOnLaunchedWithCouplerDestroyed; // triggered on animation completion
    ushort deployOnChannel; // monitored to start the launch animation (pending delay)
    float deployDelay; // Delay in seconds to begin launching this missile, after deployOnChannel is set
    SCRIPTINFO* animScriptInfo1;
    SCRIPTINFO* animScriptInfo2;
    SCRIPTINFO* animScriptInfo3;
    SCRIPTINFO* animScriptInfo4;
} ObjData_SpaceMissile;

static_assert(sizeof(ObjData_SpaceMissile) == 0x28, "Bad size for ObjData_SpaceMissile");

typedef struct {
    ObjectCreationData_Basic baseCreation; // 0x00-0x2b
    HASHCODE missileDeploymentAnim; // 0x2c
    HASHCODE missileLaunchCouplerIntactAnim; // 0x30
    HASHCODE missileLaunchCouplerDestroyedAnim; // 0x34
    int deployDelay; // 0x38
    ushort missileNum; // 0x3c
    ushort _pad_1;
    ushort switchChannelOnLaunchedWithCouplerIntact; // 0x40
    ushort _pad_2;
    ushort switchChannelOnLaunchedWithCouplerDestroyed; // 0x44
    ushort _pad_3;
    ushort deployOnChannel; // 0x48
    ushort _pad_4;
    HASHCODE animScript1;
    HASHCODE animScript2;
    HASHCODE animScript3;
    HASHCODE animScript4;
} Create_SpaceMissile_Params;

#pragma pack(pop)

// 8 one-byte bools, representing whether each has been started, found at 0x0029a28c
// Used by the HUD
#define MissileDeploy (*(uchar(*)[8])(0x0029a28c))

// AUTOINJECT
obj_tag * Create_SpaceMissile(_VECTOR *pos,_VECTOR *rot, level_tag* lvl) {
    
    Create_SpaceMissile_Params *createParams = (Create_SpaceMissile_Params*)lvl;

    obj_tag* gameObj = Control_CreateObjEx(sizeof(ObjData_SpaceMissile), pos, rot, NULL, NULL, NULL, 1, 0, 1.0f, 0, 0xFF, 0xFF, 0xFF);
    gameObj->objectType = OBJECTTYPE_SPACEMISSILE;

    ObjData_SpaceMissile *spaceMissile = (ObjData_SpaceMissile*)gameObj->extraObjectData;

    spaceMissile->missileLaunchCouplerIntactAnim = createParams->missileLaunchCouplerIntactAnim;
    spaceMissile->missileLaunchCouplerDestroyedAnim = createParams->missileLaunchCouplerDestroyedAnim;
    spaceMissile->deployDelay = (float)createParams->deployDelay;
    spaceMissile->missileNum = createParams->missileNum;
    spaceMissile->switchChannelOnLaunchedWithCouplerIntact = createParams->switchChannelOnLaunchedWithCouplerIntact;
    spaceMissile->switchChannelOnLaunchedWithCouplerDestroyed = createParams->switchChannelOnLaunchedWithCouplerDestroyed;
    spaceMissile->deployOnChannel = createParams->deployOnChannel;
    
    spaceMissile->currentAnimScript = Script_Load(createParams->missileDeploymentAnim, pos, rot, NULL, NULL, NULL, NULL);
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
    
    // Clear the space missile HUD icons, each time any missile is created.
    // This is what the original game code does - untidy but functional.
    for(int i = 0; i < 8; i++) {
        MissileDeploy[i] = 0;
    }

    return gameObj;
}

// AUTOINJECT
void SpaceMissile_Update(obj_tag *obj) { 

    ObjData_SpaceMissile *spaceMissile = (ObjData_SpaceMissile*)obj->extraObjectData;

    if(spaceMissile->currentAnimScript == NULL)
        return;

    if((obj->curState != 0) && (obj->curState != 4)) {

        // Idle animations?

        if(spaceMissile->animScriptInfo1 != NULL)
            Script_Update(spaceMissile->animScriptInfo1);

        if(spaceMissile->animScriptInfo2 != NULL)
            Script_Update(spaceMissile->animScriptInfo2);
            
        if(spaceMissile->animScriptInfo3 != NULL)
            Script_Update(spaceMissile->animScriptInfo3);
            
        if(spaceMissile->animScriptInfo4 != NULL)
            Script_Update(spaceMissile->animScriptInfo4);

    }


    switch(obj->curState) {
        case 0: // Inactive, waiting to be triggered and timer to reach zero
            if ((spaceMissile->deployOnChannel == 0) || (switch_channels[spaceMissile->deployOnChannel])) {
                
                spaceMissile->deployDelay -= REC_FRAME_RATE;

                if(spaceMissile->deployDelay < 0) {
                    obj->curState = 1;
                    MissileDeploy[spaceMissile->missileNum - 1] = 1;
                }

            }
            break;

        case 1: // Deploy and countdown animation running?
            Script_Update(spaceMissile->currentAnimScript);

            // Animation complete?
            if(spaceMissile->currentAnimScript->maybeSomeSleepFrames != 0)
                obj->curState = 2;
            break;

        case 2: // Countdown over - decide whether it flies off target or succeeds 

            if(switch_channels[spaceMissile->missileNum] != 0) {
                
                // Player managed to destroy the coupler - missile flies off target
                Script_Free(spaceMissile->currentAnimScript);
                spaceMissile->currentAnimScript = Script_Load(spaceMissile->missileLaunchCouplerDestroyedAnim, &obj->position, &obj->rotation, NULL, NULL, NULL, NULL);
                Script_Play(spaceMissile->currentAnimScript, 0);
                Script_Update(spaceMissile->currentAnimScript);
                obj->curState = 3;
                obj->subState = 0;
                MissileDeploy[spaceMissile->missileNum - 1] = 2; // Set the HUD icon for this missile to solid red

            } else {

                // Player did not destroy the coupler - missile launch succeeds, player fails
                Script_Free(spaceMissile->currentAnimScript);
                spaceMissile->currentAnimScript = Script_Load(spaceMissile->missileLaunchCouplerIntactAnim, &obj->position, &obj->rotation, NULL, NULL, NULL, NULL);
                Script_Play(spaceMissile->currentAnimScript, 0);
                Script_Update(spaceMissile->currentAnimScript);
                obj->curState = 3;
                obj->subState = 1;
                MissileDeploy[spaceMissile->missileNum - 1] = 3; // HUD icon to solid green

            }
            break;

        case 3: // Play out the launch animation

            Script_Update(spaceMissile->currentAnimScript);

            if(!Script_IsPlaying(spaceMissile->currentAnimScript)) {

                // Missile launch animation has finished

                if(obj->subState == 0) { 
                    // Missile off target
                    SwitchChannel_SetActive(spaceMissile->switchChannelOnLaunchedWithCouplerDestroyed);
                } else {
                    // Missile on target
                    SwitchChannel_SetActive(spaceMissile->switchChannelOnLaunchedWithCouplerIntact);
                }

                obj->curState = 4;
                spaceMissile->currentAnimScript = NULL;

            }

            break;

    }



}