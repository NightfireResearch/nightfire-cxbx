#include "SpaceLaser.h"

#include "bullet.h"
#include "control.h"
#include "Effect.h"
#include "Emitter.h"

#include "../../engine/Script.h"

typedef struct {
    ObjectCreationData_Basic baseData;
    HASHCODE someScript0;
    HASHCODE someScript1;
    HASHCODE someScript;
    uint someNum;
    uint someOtherNum;
    uint someThingBecomesShort1;
    uint laserShutoffSwitchChannel;
    uint laserStartupSwitchChannel;
} SpaceLaserCreationData;


// AUTOINJECT
obj_tag * Create_SpaceLaser(_VECTOR *pos,_VECTOR *rot, level_tag* lvl) {
    
    SpaceLaserCreationData *creationData = (SpaceLaserCreationData*)lvl;

    obj_tag *gameObj = Control_CreateObjEx(sizeof(ObjData_SpaceLaser), pos, rot, NULL, NULL, NULL, '\x01', 0, 1.0, 0, 0xff, 0xff, 0xff);
    ObjData_SpaceLaser *spaceLaserInfo = (ObjData_SpaceLaser *)gameObj->extraObjectData;
    gameObj->objectType = OBJECTTYPE_SPACELASER;

    spaceLaserInfo->someScripts[0] = creationData->someScript0;
    spaceLaserInfo->someScripts[1] = creationData->someScript1;
    spaceLaserInfo->someScript = creationData->someScript;
    spaceLaserInfo->someCountdown = (float)creationData->someNum;
    spaceLaserInfo->unknown2 = (float)creationData->someOtherNum;
    spaceLaserInfo->unknown3 = creationData->someThingBecomesShort1;
    spaceLaserInfo->laserShutoffSwitchChannel = creationData->laserShutoffSwitchChannel;
    spaceLaserInfo->laserStartupSwitchChannel = creationData->laserStartupSwitchChannel;
    spaceLaserInfo->someTimerBeforeRunningScript = 10.0;
    gameObj->curState = 0;

    return gameObj;
}

// AUTOINJECT
void SpaceLaser_Register(obj_tag *obj, ushort param_2, _VECTOR *param_3) {

    if(param_3 == NULL)
        return;

    Vec_Copy(Mat_Position(obj->transformMatrix), param_3);

}

// AUTOINJECT
void SpaceLaser_Update(obj_tag *obj) {

    ObjData_SpaceLaser *spaceLaser = (ObjData_SpaceLaser*)obj->extraObjectData;

    if(SwitchChannel_IsActive(spaceLaser->laserStartupSwitchChannel)) {
        spaceLaser->someCountdown -= REC_FRAME_RATE;
    }

    if(obj->curState != 4 && SwitchChannel_IsActive(spaceLaser->laserShutoffSwitchChannel)) {
        obj->curState = 4;
    }

    switch(obj->curState) {

        case 0: // Delay before / between shots

            if(!SwitchChannel_IsActive(spaceLaser->laserStartupSwitchChannel))
                break;

            if(spaceLaser->soundHandle != NULL) {
                Sound_Stop(spaceLaser->soundHandle);
                spaceLaser->soundHandle = NULL;
            }

            spaceLaser->someTimerBeforeRunningScript -= REC_FRAME_RATE;

            if(spaceLaser->someCountdown < 0.0f) {

                // Massive laser burst which destroys the shuttle. Shuttle animation still plays and Drake still spawns, this just turns the laser visual on.
                spaceLaser->scriptInfo = Script_Load(spaceLaser->someScript, &CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, NULL, NULL, NULL, NULL);
                Script_Play(spaceLaser->scriptInfo, 1);
                Script_Update(spaceLaser->scriptInfo);
                obj->curState = 3;

            } else if(spaceLaser->someTimerBeforeRunningScript < 0.0f) {

                // Randomly pick one of two side lasers to fire?
                int side = Rand_Random() & 1;
                spaceLaser->scriptInfo = Script_Load(spaceLaser->someScripts[side], &CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, NULL, NULL, SpaceLaser_Register, spaceLaser);
                Script_Play(spaceLaser->scriptInfo, 0);
                Script_Update(spaceLaser->scriptInfo);
                Vec_Copy(&glb_players[0]->position, &spaceLaser->targetPnt);
                obj->curState = 1;

            }

            break;

        case 1: // Charge-up sequence

            Script_Update(spaceLaser->scriptInfo);

            {
                // Half way into the script?? ProgressFrames > NumFrames / 2 ?
                float maybeHalfWayFrameNum = (float) (spaceLaser->scriptInfo->maybeNumFrames / 2);
                float maybeFrameAt = spaceLaser->scriptInfo->maybeProgressFrames;

                if((maybeFrameAt > maybeHalfWayFrameNum) && (spaceLaser->soundHandle == NULL)) {
                    spaceLaser->soundHandle = Sound_PlayExt(SFX_ENV_SPACE_LASER_LOCK_WARNING_LOOP, 100.0f, 0, 0);
                }
            }

            if(!Script_IsPlaying(spaceLaser->scriptInfo)) {
                obj->curState = 2;
                spaceLaser->scriptInfo = NULL;
            }

            break;
            
        case 2: // Fire the shot

        {
            // Determine beam direction and end point
            _VECTOR beamDir;
            _VECTOR beamEnd;
            Vec_Subtract(&spaceLaser->targetPnt, &spaceLaser->emitterPnt, &beamDir);
            Vec_Normalise(&beamDir, &beamDir); // Direction of the shot
            auxVec_AddMulR32(&spaceLaser->emitterPnt, &beamDir, 1000.0f, &beamEnd); // End = (Start + 1000 * direction)

            // Cast a ray. If the ray hits something, update the end point accordingly?
            HITDATA_tag *hitList = NULL;
            if(Collide_RayIntersect(&spaceLaser->emitterPnt, &beamEnd, obj->inCel, obj, NULL, &hitList, 0, 0x0008, 0)) {
                Vec_Copy(&(hitList[0].hitPosition), &spaceLaser->targetPnt);
            }
            Collide_FreeHitList(&hitList);
                                                              
            // Play the SFX
            Sound_Play3D(SFX_ENV_SPACE_LASER_FIRE_01, &spaceLaser->emitterPnt, 100.0f, -1.0f, -1.0f, 0, 0, 0);

            // Spawn the Bullet                                                                               
            Bullet_init(-1, obj, NULL, &weapon_data[Weap_SpaceLaser], &spaceLaser->emitterPnt, &beamDir, 0xffff);

            // Spawn the beam
            obj_tag * hitEffect = control_create_object(sizeof(Effect_tag), &spaceLaser->emitterPnt, NULL, NULL);
            Effect_tag *effectInfo = (Effect_tag*)hitEffect->extraObjectData;
            hitEffect->objectType = OBJECTTYPE_EFFECT;
            effectInfo->someThing = 5;
            effectInfo->someOther = 0;
            hashtable_set_object_to_entity_gfx(hitEffect, (HASHCODE)0x20005cd);
            hitEffect->maybeBrightness = 127 + Rand_Rand(128);
            control_link_object_to_cel(hitEffect, build_FindCel(&spaceLaser->emitterPnt, glb_world));

            // Position the beam
            PositionBeam(hitEffect, &spaceLaser->emitterPnt, &spaceLaser->targetPnt);

            // Spawn an Emitter
            Emitter_Create((HASHCODE)0xc000010, &spaceLaser->targetPnt, &CONST_UP_VECTOR, 0.0f, 0, 0, 0, 0.0f);

            // Return to the idle state
            spaceLaser->someTimerBeforeRunningScript = 10.0f;
            obj->curState = 0;

            if(spaceLaser->soundHandle != NULL) {
                Sound_Stop(spaceLaser->soundHandle);
                spaceLaser->soundHandle = NULL;
            }
        }
            break;

        case 3: // Maybe endgame sequence which eventually triggers Drake to spawn?

            Script_Update(spaceLaser->scriptInfo);

            spaceLaser->unknown2 -= REC_FRAME_RATE;

            if(spaceLaser->unknown2 < 0.0f) {
                SwitchChannel_SetActive(spaceLaser->unknown3);
            }

            if(spaceLaser->soundHandle != NULL) {
                Sound_Stop(spaceLaser->soundHandle);
                spaceLaser->soundHandle = NULL;
            }

            break;

        case 4:
            
            if(spaceLaser->soundHandle != NULL) {
                Sound_Stop(spaceLaser->soundHandle);
                spaceLaser->soundHandle = NULL;
            }
            
            obj->flags |= 1;

            break;

        default:
            break;
    }
}
