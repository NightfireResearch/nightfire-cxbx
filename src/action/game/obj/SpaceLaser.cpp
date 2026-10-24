#include "SpaceLaser.h"

#include "bullet.h"
#include "control.h"
#include "Effect.h"
#include "Emitter.h"

#include "../../engine/Script.h"

// AUTOINJECT
obj_tag * Create_SpaceLaser(_VECTOR *pos,_VECTOR *rot, level_tag* lvl) {
    
    SpaceLaserPlacement *creationData = (SpaceLaserPlacement*)lvl;

    obj_tag *gameObj = Control_CreateObjEx(sizeof(ObjData_SpaceLaser), pos, rot, NULL, NULL, NULL, '\x01', 0, 1.0, 0, 0xff, 0xff, 0xff);
    ObjData_SpaceLaser *spaceLaserInfo = (ObjData_SpaceLaser *)gameObj->extraObjectData;
    gameObj->objectType = OBJECTTYPE_SPACELASER;

    spaceLaserInfo->shotScripts[0] = creationData->shotScript0;
    spaceLaserInfo->shotScripts[1] = creationData->shotScript1;
    spaceLaserInfo->finalScript = creationData->finalScript;
    spaceLaserInfo->finalDelay = (float)creationData->finalSeconds;
    spaceLaserInfo->doneDelay = (float)creationData->doneSeconds;
    spaceLaserInfo->doneChannel = creationData->doneChannel;
    spaceLaserInfo->laserShutoffSwitchChannel = creationData->laserShutoffSwitchChannel;
    spaceLaserInfo->laserStartupSwitchChannel = creationData->laserStartupSwitchChannel;
    spaceLaserInfo->shotDelay = 10.0;
    gameObj->curState = SPACELASER_WAITING;

    return gameObj;
}

// AUTOINJECT
void SpaceLaser_Register(obj_tag *obj, ushort unused, _VECTOR *posOut) {

    if(posOut == NULL)
        return;

    Vec_Copy(Mat_Position(obj->transformMatrix), posOut);

}

// AUTOINJECT
void SpaceLaser_Update(obj_tag *obj) {

    ObjData_SpaceLaser *spaceLaser = (ObjData_SpaceLaser*)obj->extraObjectData;

    if(SwitchChannel_IsActive(spaceLaser->laserStartupSwitchChannel)) {
        spaceLaser->finalDelay -= REC_FRAME_RATE;
    }

    // a shutoff channel of 0 is none
    if(obj->curState != SPACELASER_OFF && spaceLaser->laserShutoffSwitchChannel != 0 &&
       SwitchChannel_IsActive(spaceLaser->laserShutoffSwitchChannel)) {
        obj->curState = SPACELASER_OFF;
    }

    switch(obj->curState) {

        case SPACELASER_WAITING:

            if(!SwitchChannel_IsActive(spaceLaser->laserStartupSwitchChannel))
                break;

            if(spaceLaser->soundHandle != NULL) {
                Sound_Stop(spaceLaser->soundHandle);
                spaceLaser->soundHandle = NULL;
            }

            spaceLaser->shotDelay -= REC_FRAME_RATE;

            if(spaceLaser->finalDelay < 0.0f) {

                spaceLaser->scriptInfo = Script_Load(spaceLaser->finalScript, &CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR,
                                                     NULL, NULL, NULL, NULL);
                Script_Play(spaceLaser->scriptInfo, 1);
                Script_Update(spaceLaser->scriptInfo);
                obj->curState = SPACELASER_FINAL;

            } else if(spaceLaser->shotDelay < 0.0f) {

                int side = Rand_Random() & 1;
                spaceLaser->scriptInfo = Script_Load(spaceLaser->shotScripts[side], &CONST_ZERO_VECTOR,
                                                     &CONST_ZERO_VECTOR, NULL, NULL,
                                                     (ScriptPlayerCallback *)SpaceLaser_Register, spaceLaser);
                Script_Play(spaceLaser->scriptInfo, 0);
                Script_Update(spaceLaser->scriptInfo);
                Vec_Copy(&glb_players[0]->position, &spaceLaser->targetPnt);
                obj->curState = SPACELASER_CHARGING;

            }

            break;

        case SPACELASER_CHARGING:

            Script_Update(spaceLaser->scriptInfo);

            {
                // the warning loop from halfway through the script
                float halfWayFrameNum = (float) (spaceLaser->scriptInfo->endFrame / 2);
                float frameAt = spaceLaser->scriptInfo->elapsedFrames;

                if((frameAt > halfWayFrameNum) && (spaceLaser->soundHandle == NULL)) {
                    spaceLaser->soundHandle = Sound_PlayExt(SFX_ENV_SPACE_LASER_LOCK_WARNING_LOOP, 100.0f, 0, 0);
                }
            }

            if(!Script_IsPlaying(spaceLaser->scriptInfo)) {
                obj->curState = SPACELASER_FIRING;
                spaceLaser->scriptInfo = NULL;
            }

            break;
            
        case SPACELASER_FIRING:

        {
            _VECTOR beamDir;
            _VECTOR beamEnd;
            Vec_Subtract(&spaceLaser->targetPnt, &spaceLaser->emitterPnt, &beamDir);
            Vec_Normalise(&beamDir, &beamDir);
            auxVec_AddMulR32(&spaceLaser->emitterPnt, &beamDir, 1000.0f, &beamEnd);

            // the beam ends at the first hit within 1000
            HITDATA_tag *hitList = NULL;
            if(Collide_RayIntersect(&spaceLaser->emitterPnt, &beamEnd, obj->inCel, obj, NULL, &hitList, 0,
                                    HITMASK_SKIP_NO_CONTACT, 0)) {
                Vec_Copy(&(hitList[0].hitPosition), &spaceLaser->targetPnt);
            }
            Collide_FreeHitList(&hitList);

            Sound_Play3D(SFX_ENV_SPACE_LASER_FIRE_01, &spaceLaser->emitterPnt, 100.0f, -1.0f, -1.0f, 0, 0, 0);

            Bullet_init(-1, obj, NULL, &weapon_data[Weap_SpaceLaser], &spaceLaser->emitterPnt, &beamDir, SFX_NONE);

            // the beam
            obj_tag * hitEffect = control_create_object(sizeof(Effect_tag), &spaceLaser->emitterPnt, NULL, NULL);
            Effect_tag *effectInfo = (Effect_tag*)hitEffect->extraObjectData;
            hitEffect->objectType = OBJECTTYPE_EFFECT;
            effectInfo->life = 5;
            effectInfo->fade = 0;
            hashtable_set_object_to_entity_gfx(hitEffect, HT_Entity_lazer_Big);
            hitEffect->tweakA = 127 + Rand_Rand(128);
            control_link_object_to_cel(hitEffect, build_FindCel(&spaceLaser->emitterPnt, glb_world));

            PositionBeam(hitEffect, &spaceLaser->emitterPnt, &spaceLaser->targetPnt);

            Emitter_Create(HT_Emitter_SpaceLaserHit, &spaceLaser->targetPnt, &CONST_UP_VECTOR, 0.0f, 0, 0, 0, 0.0f);

            spaceLaser->shotDelay = 10.0f;
            obj->curState = SPACELASER_WAITING;

            if(spaceLaser->soundHandle != NULL) {
                Sound_Stop(spaceLaser->soundHandle);
                spaceLaser->soundHandle = NULL;
            }
        }
            break;

        case SPACELASER_FINAL:

            Script_Update(spaceLaser->scriptInfo);

            spaceLaser->doneDelay -= REC_FRAME_RATE;

            if(spaceLaser->doneDelay < 0.0f) {
                SwitchChannel_SetActive(spaceLaser->doneChannel);
            }

            if(spaceLaser->soundHandle != NULL) {
                Sound_Stop(spaceLaser->soundHandle);
                spaceLaser->soundHandle = NULL;
            }

            break;

        case SPACELASER_OFF:
            
            if(spaceLaser->soundHandle != NULL) {
                Sound_Stop(spaceLaser->soundHandle);
                spaceLaser->soundHandle = NULL;
            }
            
            obj->flags |= OBJFLAG_DELETE;

            break;

        default:
            break;
    }
}
